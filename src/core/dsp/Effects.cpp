// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Effects.h"

#include "core/Parameters.h"
#include "core/dsp/VocalChain.h"

namespace stagemic::dsp
{

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
// Chorus: one modulated voice per channel, the right channel's LFO a quarter
// cycle ahead for width. 12 ms base delay, up to +/-6 ms sweep.
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
    const float wet = mixAmount.current;

    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            const float lfo = (float)std::sin(phase + (c == 1 ? kPi * 0.5 : 0.0));
            lines[c].push(ch[c][i]);
            const float voice = lines[c].read(base + sweep * lfo);
            ch[c][i] += wet * (voice - ch[c][i]);
        }

        phase += increment;
        if (phase > 2.0 * kPi)
            phase -= 2.0 * kPi;
    }
}

//==============================================================================
// Delay: echoes with a darkening tone filter in the loop. Time changes glide
// smoothly (a short pitch bend, no clicks). The right channel is 5% longer for width.
Delay::Delay(juce::AudioProcessorValueTreeState& s)
    : time(param(s, pid::dlyTime))
    , feedback(param(s, pid::dlyFeedback))
    , tone(param(s, pid::dlyTone))
    , mix(param(s, pid::dlyMix))
{
}

void Delay::prepare(double sampleRate, int)
{
    sr = sampleRate;
    for (auto& l : lines)
        l.allocate((int)(1.3 * sr));
    glideCoef = timeToCoef(60.0f, sr);
    update(true);
    reset();
}

void Delay::reset() noexcept
{
    for (auto& l : lines)
        l.clear();
    std::fill(std::begin(lowpassState), std::end(lowpassState), 0.0f);
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
}

void Delay::process(float* const* ch, int numChannels, int n) noexcept
{
    const float fb = feedbackAmount.current;
    const float wet = mixAmount.current;

    for (int c = 0; c < numChannels; ++c)
    {
        const float target = targetSamples * (c == 1 ? 1.05f : 1.0f);
        float d = delaySamples[c];
        float lp = lowpassState[c];
        auto& line = lines[c];
        float* x = ch[c];

        for (int i = 0; i < n; ++i)
        {
            d = target + glideCoef * (d - target);
            lp += lowpassCoef * (line.read(d) - lp);
            line.push(x[i] + lp * fb);
            x[i] += wet * lp;
        }

        delaySamples[c] = d;
        lowpassState[c] = lp;
    }
}

//==============================================================================
// Reverb: JUCE's Freeverb-style reverb on a high-passed copy of the voice,
// added back on top of the untouched dry signal.
Reverb::Reverb(juce::AudioProcessorValueTreeState& s)
    : size(param(s, pid::revSize))
    , damping(param(s, pid::revDamping))
    , width(param(s, pid::revWidth))
    , mix(param(s, pid::revMix))
{
}

void Reverb::prepare(double sampleRate, int)
{
    reverb.setSampleRate(sampleRate);
    sendHighPass.c = biquad::highpass(sampleRate, 150.0, 0.70710678);
    for (auto& s : scratch)
        s.assign((size_t)VocalChain::kSubBlock, 0.0f);
    update(true);
    reset();
}

void Reverb::reset() noexcept
{
    reverb.reset();
    sendHighPass.reset();
}

void Reverb::update(bool snap) noexcept
{
    juce::Reverb::Parameters p;
    p.roomSize = load(size) * 0.01f;
    p.damping = load(damping) * 0.01f;
    p.width = load(width) * 0.01f;
    p.wetLevel = 1.0f / 3.0f; // JUCE scales wet by 3, so this is unity
    p.dryLevel = 0.0f;
    p.freezeMode = 0.0f;

    if (snap || p.roomSize != current.roomSize || p.damping != current.damping || p.width != current.width)
    {
        current = p;
        reverb.setParameters(p); // JUCE smooths these internally
    }

    mixAmount.step(load(mix) * 0.01f, snap);
}

void Reverb::process(float* const* ch, int numChannels, int n) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            scratch[c][(size_t)i] = sendHighPass.process(ch[c][i], c);

    if (numChannels >= 2)
        reverb.processStereo(scratch[0].data(), scratch[1].data(), n);
    else
        reverb.processMono(scratch[0].data(), n);

    const float wet = mixAmount.current;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] += wet * scratch[c][(size_t)i];
}

} // namespace stagemic::dsp
