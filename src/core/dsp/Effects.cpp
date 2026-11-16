// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Effects.h"

#include "core/Parameters.h"
#include "core/dsp/VocalChain.h"

namespace stagemic::dsp
{

namespace
{
inline float peakAt(float* const* ch, int numChannels, int i) noexcept
{
    float p = std::abs(ch[0][i]);
    for (int c = 1; c < numChannels; ++c)
        p = std::max(p, std::abs(ch[c][i]));
    return p;
}
} // namespace

//==============================================================================
void DelayLine::allocate(int maxDelaySamples)
{
    int size = 1;
    while (size < maxDelaySamples + 2)
        size <<= 1;
    buffer.assign((size_t)size, 0.0f);
    mask = size - 1;
    writePos = 0;
}

void DelayLine::clear() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

//==============================================================================
void Ducker::prepare(double sampleRate) noexcept
{
    attack = timeToCoef(15.0f, sampleRate);
    release = timeToCoef(120.0f, sampleRate);
    reset();
}

float Ducker::next(float voicePeak, float amountDb) noexcept
{
    envelope = voicePeak + (voicePeak > envelope ? attack : release) * (envelope - voicePeak);
    if (amountDb <= 0.0f)
        return 1.0f;
    // Full ducking while the voice is above -24 dBFS
    const float amount = juce::jlimit(0.0f, 1.0f, (gainToDb(envelope) + 38.0f) / 14.0f);
    return dbToGain(-amountDb * amount);
}

//==============================================================================
namespace
{
// Delay-line lengths in ms
constexpr float plateLineMs[PlateReverb::kLines] = {23.1f, 27.7f, 31.9f, 35.3f, 39.7f, 43.9f, 48.3f, 53.9f};
constexpr float diffuserMs[4] = {4.77f, 3.59f, 12.7f, 9.31f};
constexpr float diffuserGain[4] = {0.75f, 0.75f, 0.625f, 0.625f};
constexpr float lfoHz[PlateReverb::kLines] = {0.31f, 0.47f, 0.53f, 0.67f, 0.71f, 0.83f, 0.97f, 1.07f};

// In-place 8-point fast Walsh-Hadamard transform, normalised
inline void hadamard8(float* x) noexcept
{
    for (int span = 1; span < 8; span <<= 1)
        for (int i = 0; i < 8; i += span << 1)
            for (int j = i; j < i + span; ++j)
            {
                const float a = x[j], b = x[j + span];
                x[j] = a + b;
                x[j + span] = a - b;
            }
    constexpr float norm = 0.35355339059327373f; // 1/sqrt
    for (int i = 0; i < 8; ++i)
        x[i] *= norm;
}
} // namespace

void PlateReverb::prepare(double sampleRate)
{
    sr = sampleRate;
    const float msToSamples = (float)(sr / 1000.0);
    for (int d = 0; d < 4; ++d)
    {
        diffusers[d].delay = diffuserMs[d] * msToSamples;
        diffusers[d].gain = diffuserGain[d];
        diffusers[d].line.allocate((int)diffusers[d].delay + 4);
    }

    modDepth = 0.3f * msToSamples; // +/-0.3 ms
    for (int i = 0; i < kLines; ++i)
    {
        lengths[i] = plateLineMs[i] * msToSamples;
        lines[i].allocate((int)(lengths[i] + modDepth) + 4);
        const double w = 2.0 * kPi * lfoHz[i] / sr;
        rotCos[i] = (float)std::cos(w);
        rotSin[i] = (float)std::sin(w);
    }

    setDecaySeconds(1.2f);
    setDamping(0.45f);
    reset();
}

void PlateReverb::reset() noexcept
{
    for (auto& d : diffusers)
        d.line.clear();
    for (int i = 0; i < kLines; ++i)
    {
        lines[i].clear();
        lowpass[i] = 0.0f;
        const double phase = 2.0 * kPi * i / kLines; // spread the LFO phases
        modCos[i] = (float)std::cos(phase);
        modSin[i] = (float)std::sin(phase);
    }
    renormCounter = 0;
}

void PlateReverb::setDecaySeconds(float rt60) noexcept
{
    rt60 = juce::jmax(0.1f, rt60);
    for (int i = 0; i < kLines; ++i)
        feedback[i] = std::pow(10.0f, -3.0f * lengths[i] / (rt60 * (float)sr)); // -60 dB after rt60 seconds
}

void PlateReverb::setDamping(float amount) noexcept
{
    // Low-pass inside the loop
    const double cutoff = 12000.0 * std::pow(2000.0 / 12000.0, (double)juce::jlimit(0.0f, 1.0f, amount));
    dampCoef = (float)(1.0 - std::exp(-2.0 * kPi * cutoff / sr));
}

inline void PlateReverb::processSample(float in, float& outL, float& outR) noexcept
{
    float x = in;
    for (auto& d : diffusers)
        x = d.process(x);

    float out[kLines];
    for (int i = 0; i < kLines; ++i)
    {
        // Slowly wobble each line's length (quadrature oscillator
        const float c = modCos[i] * rotCos[i] - modSin[i] * rotSin[i];
        modSin[i] = modSin[i] * rotCos[i] + modCos[i] * rotSin[i];
        modCos[i] = c;

        out[i] = lines[i].read(lengths[i] + modDepth * modSin[i]);
        lowpass[i] += dampCoef * (out[i] - lowpass[i]);
    }

    float fb[kLines];
    for (int i = 0; i < kLines; ++i)
        fb[i] = lowpass[i] * feedback[i];
    hadamard8(fb);

    for (int i = 0; i < kLines; ++i)
        lines[i].push(fb[i] + ((i & 1) ? -x : x) * 0.5f);

    // Two orthogonal sign patterns give a wide, decorrelated stereo tail
    const float l = out[0] + out[1] - out[2] - out[3] + out[4] - out[5] + out[6] - out[7];
    const float r = out[0] - out[1] + out[2] - out[3] - out[4] - out[5] + out[6] + out[7];
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
    constexpr float outGain = 0.5f;
    outL = (mid + side) * outGain;
    outR = (mid - side) * outGain;

    // Keep the oscillators' amplitude at exactly 1 despite rounding
    if (++renormCounter >= 4096)
    {
        renormCounter = 0;
        for (int i = 0; i < kLines; ++i)
        {
            const float mag = std::sqrt(modCos[i] * modCos[i] + modSin[i] * modSin[i]);
            modCos[i] /= mag;
            modSin[i] /= mag;
        }
    }
}

//==============================================================================
Chorus::Chorus(juce::AudioProcessorValueTreeState& s)
    : rate(param(s, pid::chRate))
    , depth(param(s, pid::chDepth))
    , mix(param(s, pid::chMix))
{
}

void Chorus::prepare(double sampleRate, int)
{
    sr = sampleRate;
    for (auto& l : lines)
        l.allocate((int)(0.025 * sr));
    update(true);
    reset();
}

void Chorus::reset() noexcept
{
    for (auto& l : lines)
        l.clear();
    phase = 0.0;
}

void Chorus::update(bool snap) noexcept
{
    rateHz.step(load(rate), snap);
    depthAmount.step(load(depth) * 0.01f, snap);
    mixAmount.step(load(mix) * 0.01f, snap);
}

void Chorus::process(float* const* ch, int numChannels, int n) noexcept
{
    const float base = (float)(0.012 * sr);
    const float sweep = (float)(0.006 * sr) * depthAmount.current;
    const double increment = 2.0 * kPi * rateHz.current / sr;
    const float level = mixAmount.current;

    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            const float lfo = (float)std::sin(phase + (c == 1 ? kPi * 0.5 : 0.0));
            lines[c].push(ch[c][i]);
            ch[c][i] = level * lines[c].read(base + sweep * lfo);
        }

        phase += increment;
        if (phase > 2.0 * kPi)
            phase -= 2.0 * kPi;
    }
}

//==============================================================================
Delay::Delay(juce::AudioProcessorValueTreeState& s)
    : time(param(s, pid::dlyTime))
    , feedback(param(s, pid::dlyFeedback))
    , tone(param(s, pid::dlyTone))
    , mix(param(s, pid::dlyMix))
    , duck(param(s, pid::dlyDuck))
{
}

void Delay::prepare(double sampleRate, int)
{
    sr = sampleRate;
    for (auto& l : lines)
        l.allocate((int)(1.3 * sr));
    glideCoef = timeToCoef(60.0f, sr);
    lowCut.c = biquad::highpass(sr, 150.0, 0.70710678);
    ducker.prepare(sr);
    update(true);
    reset();
}

void Delay::reset() noexcept
{
    for (auto& l : lines)
        l.clear();
    std::fill(std::begin(lowpassState), std::end(lowpassState), 0.0f);
    lowCut.reset();
    ducker.reset();
    delaySamples[0] = targetSamples;
    delaySamples[1] = targetSamples * 1.05f;
}

void Delay::update(bool snap) noexcept
{
    targetSamples = juce::jlimit(1.0f, (float)(1.2 * sr), load(time) * 0.001f * (float)sr);
    if (snap)
    {
        delaySamples[0] = targetSamples;
        delaySamples[1] = targetSamples * 1.05f;
    }
    lowpassCoef = 1.0f - std::exp((float)(-2.0 * kPi * load(tone) / sr));
    feedbackAmount.step(load(feedback) * 0.01f, snap);
    mixAmount.step(load(mix) * 0.01f, snap);
    duckDb.step(load(duck), snap);
}

void Delay::process(float* const* ch, int numChannels, int n) noexcept
{
    const float fb = feedbackAmount.current;
    const float level = mixAmount.current;

    for (int i = 0; i < n; ++i)
    {
        const float duckGain = ducker.next(peakAt(ch, numChannels, i), duckDb.current);

        for (int c = 0; c < numChannels; ++c)
        {
            const float target = targetSamples * (c == 1 ? 1.05f : 1.0f);
            delaySamples[c] = target + glideCoef * (delaySamples[c] - target);

            auto& lp = lowpassState[c];
            lp += lowpassCoef * (lines[c].read(delaySamples[c]) - lp);
            const float echo = lowCut.process(lp, c);

            lines[c].push(ch[c][i] + echo * fb);
            ch[c][i] = level * duckGain * echo;
        }
    }
}

//==============================================================================
Reverb::Reverb(juce::AudioProcessorValueTreeState& s)
    : size(param(s, pid::revSize))
    , damping(param(s, pid::revDamping))
    , width(param(s, pid::revWidth))
    , mix(param(s, pid::revMix))
    , preDelay(param(s, pid::revPreDelay))
    , lowCutFreq(param(s, pid::revLowCut))
    , highCutFreq(param(s, pid::revHighCut))
    , duck(param(s, pid::revDuck))
{
}

void Reverb::prepare(double sampleRate, int)
{
    sr = sampleRate;
    plate.prepare(sampleRate);
    for (auto& l : preDelayLines)
        l.allocate((int)(0.2 * sr));
    ducker.prepare(sr);
    update(true);
    reset();
}

void Reverb::reset() noexcept
{
    plate.reset();
    for (auto& l : preDelayLines)
        l.clear();
    lowCut.reset();
    highCut.reset();
    ducker.reset();
}

void Reverb::update(bool snap) noexcept
{
    // 1.12 compensates the slight shortening caused by damping in the loop
    const float rt60 = 1.12f * 0.3f * std::pow(4.0f / 0.3f, load(size) * 0.01f);
    if (decaySeconds.step(rt60, snap) || snap)
        plate.setDecaySeconds(decaySeconds.current);
    if (dampAmount.step(load(damping) * 0.01f, snap) || snap)
        plate.setDamping(dampAmount.current);
    widthAmount.step(load(width) * 0.01f, snap);
    plate.setWidth(widthAmount.current);

    preDelaySamples.step(juce::jmax(1.0f, load(preDelay) * 0.001f * (float)sr), snap);
    if (lowCutSmooth.step(load(lowCutFreq), snap) || snap)
        lowCut.c = biquad::highpass(sr, lowCutSmooth.current, 0.70710678);
    if (highCutSmooth.step(load(highCutFreq), snap) || snap)
        highCut.c = biquad::lowpass(sr, highCutSmooth.current, 0.70710678);
    mixAmount.step(load(mix) * 0.01f, snap);
    duckDb.step(load(duck), snap);
}

void Reverb::process(float* const* ch, int numChannels, int n) noexcept
{
    // Ducking follows the dry voice
    float duckGain[VocalChain::kSubBlock];
    for (int i = 0; i < n; ++i)
        duckGain[i] = ducker.next(peakAt(ch, numChannels, i), duckDb.current);

    const float delay = preDelaySamples.current;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
        {
            preDelayLines[c].push(ch[c][i]);
            ch[c][i] = preDelayLines[c].read(delay);
        }

    for (int i = 0; i < n; ++i)
    {
        const float in = numChannels >= 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
        float l, r;
        plate.processSample(in, l, r);
        ch[0][i] = l;
        if (numChannels >= 2)
            ch[1][i] = r;
        else
            ch[0][i] = 0.5f * (l + r);
    }

    const float level = mixAmount.current;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = level * duckGain[i] * highCut.process(lowCut.process(ch[c][i], c), c);
}

} // namespace stagemic::dsp
