// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Tone.h"

#include "core/Parameters.h"
#include "core/dsp/VocalChain.h"

namespace stagemic::dsp
{

//==============================================================================
Equalizer::Equalizer(juce::AudioProcessorValueTreeState& s)
    : lowCut(param(s, pid::eqLowCut))
    , lowCutSlope(param(s, pid::eqLowCutSlope))
    , lowFreq(param(s, pid::eqLowFreq))
    , lowGain(param(s, pid::eqLowGain))
    , midFreq(param(s, pid::eqMidFreq))
    , midGain(param(s, pid::eqMidGain))
    , midQ(param(s, pid::eqMidQ))
    , highFreq(param(s, pid::eqHighFreq))
    , highGain(param(s, pid::eqHighGain))
    , loMidFreq(param(s, pid::eqLoMidFreq))
    , loMidGain(param(s, pid::eqLoMidGain))
    , loMidQ(param(s, pid::eqLoMidQ))
    , hiMidFreq(param(s, pid::eqHiMidFreq))
    , hiMidGain(param(s, pid::eqHiMidGain))
    , hiMidQ(param(s, pid::eqHiMidQ))
    , tameFreq(param(s, pid::eqTameFreq))
    , tameThreshold(param(s, pid::eqTameThreshold))
    , tameRange(param(s, pid::eqTameRange))
    , plosive(param(s, pid::eqPlosive))
{
    for (int i = 0; i < 7; ++i)
        pointOn[i] = param(s, pid::eqPointOn[i]);
}

void Equalizer::prepare(double sampleRate, int)
{
    sr = sampleRate;
    tameAttack = timeToCoef(2.0f, sr);
    tameRelease = timeToCoef(60.0f, sr);
    bodyAttack = timeToCoef(10.0f, sr);
    bodyRelease = timeToCoef(150.0f, sr);
    tameBody.c = biquad::bandPass(sr, 450.0, 0.5);
    tameGr.setTimes(3.0f, 90.0f, sr);
    popDetect.c = biquad::lowpass(sr, 200.0, 0.5); // wide and low-Q: reacts within a few ms
    popBody.c = biquad::bandPass(sr, 1000.0, 0.5);
    popAttack = timeToCoef(0.5f, sr);
    popRelease = timeToCoef(40.0f, sr);
    popBodyAttack = timeToCoef(0.5f, sr); // as fast as the low end, so a sung note's start
    popBodyRelease = timeToCoef(150.0f, sr);
    popMixAttack = timeToCoef(0.3f, sr);
    popMixRelease = timeToCoef(120.0f, sr);
    update(true);
    reset();
}

void Equalizer::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
    tameBand.reset();
    tameBody.reset();
    tameEnv = bodyEnv = 0.0f;
    tameGr.state = 0.0f;
    tameMeter.store(0.0f, std::memory_order_relaxed);
    for (auto& stage : popSvf)
        for (auto& st : stage)
            st = {};
    popDetect.reset();
    popBody.reset();
    popEnv = popBodyEnv = popMix = 0.0f;
    popMeter.store(0.0f, std::memory_order_relaxed);
}

void Equalizer::setBand(Band band, const BiquadCoeffs& coeffs, bool snap) noexcept
{
    if (snap)
    {
        filters[band].c = coeffs;
        gliding[band] = false;
    }
    else
    {
        glideTo[band] = coeffs;
        gliding[band] = true;
    }
}

void Equalizer::update(bool snap) noexcept
{
    // A point switched off glides its gain to 0 dB, like turning a knob
    bool on[7];
    for (int i = 0; i < 7; ++i)
        on[i] = pointOn[i] == nullptr || load(pointOn[i]) >= 0.5f;
    constexpr float lowCutOff = 10.0f;

    const int newSlope = load(lowCutSlope) >= 0.5f ? 24 : 12;
    if (sLowCut.step(on[0] ? load(lowCut) : lowCutOff, snap) || snap || newSlope != slope || on[0] != pointWasOn[0])
    {
        slope = newSlope;
        if (!on[0] && sLowCut.current <= lowCutOff + 0.01f)
        {
            setBand(LowCut, BiquadCoeffs{}, snap); // fully off
            setBand(LowCut2, BiquadCoeffs{}, snap);
        }
        else if (slope == 24)
        {
            // 4th-order Butterworth
            setBand(LowCut, biquad::highpass(sr, sLowCut.current, 0.54119610), snap);
            setBand(LowCut2, biquad::highpass(sr, sLowCut.current, 1.30656296), snap);
        }
        else
        {
            setBand(LowCut, biquad::highpass(sr, sLowCut.current, 0.70710678), snap);
            setBand(LowCut2, BiquadCoeffs{}, snap); // pass-through
        }
    }
    pointWasOn[0] = on[0];

    const bool lf = sLowFreq.step(load(lowFreq), snap);
    const bool lg = sLowGain.step(on[1] ? load(lowGain) : 0.0f, snap);
    if (lf || lg || snap)
        setBand(LowShelf, biquad::lowShelf(sr, sLowFreq.current, sLowGain.current), snap);

    auto bell = [&](Band band, int point, BlockSmoothed& f, BlockSmoothed& g, BlockSmoothed& q, std::atomic<float>* fp,
                    std::atomic<float>* gp, std::atomic<float>* qp) {
        const bool cf = f.step(load(fp), snap), cg = g.step(on[point] ? load(gp) : 0.0f, snap), cq = q.step(load(qp), snap);
        if (cf || cg || cq || snap)
            setBand(band, biquad::peak(sr, f.current, std::max(0.1f, q.current), g.current), snap);
    };
    bell(LoMid, 2, sLoMidFreq, sLoMidGain, sLoMidQ, loMidFreq, loMidGain, loMidQ);
    bell(Mid, 3, sMidFreq, sMidGain, sMidQ, midFreq, midGain, midQ);
    bell(HiMid, 4, sHiMidFreq, sHiMidGain, sHiMidQ, hiMidFreq, hiMidGain, hiMidQ);

    const bool hf = sHighFreq.step(load(highFreq), snap);
    const bool hg = sHighGain.step(on[5] ? load(highGain) : 0.0f, snap);
    if (hf || hg || snap)
        setBand(High, biquad::highShelf(sr, sHighFreq.current, sHighGain.current), snap);

    // A band that does nothing is left out of the signal path
    for (int band = 0; band < NumBands; ++band)
    {
        const auto& c = filters[band].c;
        const bool idle = !gliding[band] && c.b0 == 1.0f && c.b1 == c.a1 && c.b2 == c.a2;
        if (idle && !bandIdle[band])
            filters[band].reset();
        bandIdle[band] = idle;
    }
    tameOn = on[6];

    if (sTameFreq.step(load(tameFreq), snap) || snap)
        tameBand.c = biquad::bandPass(sr, sTameFreq.current, 1.2);
    tameThresholdDb = load(tameThreshold);
    tameMaxCutDb = tameOn ? load(tameRange) : 0.0f;
    popAmount = load(plosive) * 0.01f;
}

// The EQ's filters in series, with nothing moving
void Equalizer::runCascade(float* const* ch, int numChannels, int n, const int* active, int count) noexcept
{
    for (int j = 0; j < count; ++j)
    {
        auto& f = filters[active[j]];
        const BiquadCoeffs k = f.c;
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = ch[c];
            float s1 = f.s1[c], s2 = f.s2[c];
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                const float y = k.b0 * in + s1;
                s1 = k.b1 * in - k.a1 * y + s2;
                s2 = k.b2 * in - k.a2 * y;
                x[i] = y;
            }
            f.s1[c] = s1;
            f.s2[c] = s2;
        }
    }
}

void Equalizer::process(float* const* ch, int numChannels, int n) noexcept
{
    // Plosives first, on the raw voice
    if (popAmount > 0.0f || popMix > 1.0e-4f)
        processPlosive(ch, numChannels, n);
    else
        popMeter.store(0.0f, std::memory_order_relaxed);

    // Steady state
    bool anyGliding = false;
    for (int band = 0; band < NumBands; ++band)
        anyGliding = anyGliding || (gliding[band] && !bandIdle[band]);
    if (!anyGliding)
    {
        int active[NumBands];
        int count = 0;
        for (int band = 0; band < NumBands; ++band)
            if (!bandIdle[band])
                active[count++] = band;
        runCascade(ch, numChannels, n, active, count);
    }

    // A setting is moving
    for (int band = 0; anyGliding && band < NumBands; ++band)
    {
        auto& f = filters[band];
        if (bandIdle[band])
            continue;
        if (!gliding[band])
        {
            // Coefficients and state in locals, so the loop runs in registers
            const BiquadCoeffs k = f.c;
            for (int c = 0; c < numChannels; ++c)
            {
                float* x = ch[c];
                float s1 = f.s1[c], s2 = f.s2[c];
                for (int i = 0; i < n; ++i)
                {
                    const float in = x[i];
                    const float y = k.b0 * in + s1;
                    s1 = k.b1 * in - k.a1 * y + s2;
                    s2 = k.b2 * in - k.a2 * y;
                    x[i] = y;
                }
                f.s1[c] = s1;
                f.s2[c] = s2;
            }
            continue;
        }

        const auto& to = glideTo[band];
        const float inv = 1.0f / (float)n;
        const BiquadCoeffs d{(to.b0 - f.c.b0) * inv, (to.b1 - f.c.b1) * inv, (to.b2 - f.c.b2) * inv, (to.a1 - f.c.a1) * inv,
                             (to.a2 - f.c.a2) * inv};
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = ch[c];
            BiquadCoeffs k = f.c;
            float s1 = f.s1[c], s2 = f.s2[c];
            for (int i = 0; i < n; ++i)
            {
                k.b0 += d.b0;
                k.b1 += d.b1;
                k.b2 += d.b2;
                k.a1 += d.a1;
                k.a2 += d.a2;
                const float in = x[i];
                const float y = k.b0 * in + s1;
                s1 = k.b1 * in - k.a1 * y + s2;
                s2 = k.b2 * in - k.a2 * y;
                x[i] = y;
            }
            f.s1[c] = s1;
            f.s2[c] = s2;
        }
        f.c = to;
        gliding[band] = false;
    }

    if (tameMaxCutDb > 0.0f || tameGr.state < -0.01f)
        processTame(ch, numChannels, n);
    else
        tameMeter.store(0.0f, std::memory_order_relaxed);
}

void Equalizer::processPlosive(float* const* ch, int numChannels, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float low = 0.0f, body = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            low = std::max(low, std::abs(popDetect.process(ch[c][i], c)));
            body = std::max(body, std::abs(popBody.process(ch[c][i], c)));
        }
        popEnv = low + (low > popEnv ? popAttack : popRelease) * (popEnv - low);
        popBodyEnv = body + (body > popBodyEnv ? popBodyAttack : popBodyRelease) * (popBodyEnv - body);

        // A thump
        const float jumpDb = gainToDb(popEnv) - gainToDb(std::max(popBodyEnv, 1.0e-5f));
        const float target = popEnv > 3.0e-3f ? popAmount * std::clamp((jumpDb - 7.0f) / 5.0f, 0.0f, 1.0f) : 0.0f;
        popMix = target + (target > popMix ? popMixAttack : popMixRelease) * (popMix - target);

        // Cutoff 10 Hz .. 160 Hz, exponential in the mix amount
        const float fc = 10.0f * std::exp(2.7725887f * popMix); // ln = 2.77
        const float g = std::tan((float)kPi * fc / (float)sr);
        constexpr float k = 1.41421356f; // Q 0.707 per stage
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

        for (int c = 0; c < numChannels; ++c)
        {
            float v = ch[c][i];
            for (auto& stage : popSvf)
            {
                auto& st = stage[c];
                const float v3 = v - st.ic2;
                const float v1 = a1 * st.ic1 + a2 * v3;
                const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
                st.ic1 = 2.0f * v1 - st.ic1;
                st.ic2 = 2.0f * v2 - st.ic2;
                v = v - k * v1 - v2; // high-pass output
            }
            // No thump
            if (popMix > 1.0e-3f)
                ch[c][i] = v;
        }
    }
    popMeter.store(popMix, std::memory_order_relaxed);
}

void Equalizer::processTame(float* const* ch, int numChannels, int n) noexcept
{
    float band[kMaxChannels];
    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f, bodyPeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            band[c] = tameBand.process(ch[c][i], c);
            peak = std::max(peak, std::abs(band[c]));
            bodyPeak = std::max(bodyPeak, std::abs(tameBody.process(ch[c][i], c)));
        }
        tameEnv = peak + (peak > tameEnv ? tameAttack : tameRelease) * (tameEnv - peak);
        bodyEnv = bodyPeak + (bodyPeak > bodyEnv ? bodyAttack : bodyRelease) * (bodyEnv - bodyPeak);

        // How far the band sticks out above the body of the voice
        const float stickOutDb = gainToDb(tameEnv) - gainToDb(bodyEnv);
        float targetDb = 0.0f;
        if (tameEnv > 1.0e-4f)
            targetDb = std::max(-tameMaxCutDb, compressorGainDb(stickOutDb, tameThresholdDb, 3.0f, 6.0f));
        const float g = dbToGain(tameGr.process(targetDb));

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] += (g - 1.0f) * band[c];
    }
    tameMeter.store(-tameGr.state, std::memory_order_relaxed);
}

//==============================================================================
namespace
{
constexpr float emphasisDb = 5.0f; // how much more the mids are driven than the lows

std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler(int numChannels)
{
    // 4x, minimum-phase IIR half-band filters
    auto os = std::make_unique<juce::dsp::Oversampling<float>>((size_t)juce::jlimit(1, kMaxChannels, numChannels), 2,
                                                               juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing((size_t)VocalChain::kSubBlock);
    return os;
}

inline float shapeTube(float x, float b, float tanhB) noexcept
{
    return std::tanh(x + b) - tanhB;
}

inline float shapeTape(float x, float b) noexcept
{
    // Soft, rounded knee
    const float u = x + b;
    const float y = u / std::pow(1.0f + std::pow(std::abs(u), 2.5f), 0.4f);
    const float y0 = b / std::pow(1.0f + std::pow(std::abs(b), 2.5f), 0.4f);
    return y - y0;
}

inline float shapeConsole(float x, float b) noexcept
{
    // Gentle cubic driven at 40%: a little density, rarely reaching the clamp
    auto cubic = [](float u) {
        u = juce::jlimit(-1.0f, 1.0f, u);
        return 1.5f * (u - u * u * u / 3.0f);
    };
    return (cubic(0.4f * x + b) - cubic(b)) * 2.5f;
}
} // namespace

Saturation::Saturation(juce::AudioProcessorValueTreeState& s)
    : drive(param(s, pid::satDrive))
    , mix(param(s, pid::satMix))
    , character(param(s, pid::satCharacter))
    , model(param(s, pid::satModel))
{
}

Saturation::~Saturation() = default;

void Saturation::prepare(double sampleRate, int numChannels)
{
    sr = sampleRate;
    oversampler = makeOversampler(numChannels);
    preLow.c = biquad::lowShelf(sr, 200.0, -emphasisDb);
    preHigh.c = biquad::highShelf(sr, 4500.0, -emphasisDb);
    postLow.c = biquad::lowShelf(sr, 200.0, emphasisDb);
    postHigh.c = biquad::highShelf(sr, 4500.0, emphasisDb);
    tapeCoef = (float)(1.0 - std::exp(-2.0 * kPi * 13000.0 / sr));
    dcCoef = (float)(1.0 - 2.0 * kPi * 10.0 / sr);
    junkHigh1.c = biquad::highpass(sr, 150.0, 0.54119610); // 4th-order Butterworth
    junkHigh2.c = biquad::highpass(sr, 150.0, 1.30656296);
    cleanDelayLength = juce::jlimit(0, 7, (int)std::lround(oversampler->getLatencyInSamples()));
    update(true);
    reset();
}

void Saturation::reset() noexcept
{
    if (oversampler != nullptr)
        oversampler->reset();
    for (auto* f : {&preLow, &preHigh, &postLow, &postHigh})
        f->reset();
    std::fill(std::begin(tapeLowpass), std::end(tapeLowpass), 0.0f);
    std::fill(std::begin(dcIn), std::end(dcIn), 0.0f);
    std::fill(std::begin(dcOut), std::end(dcOut), 0.0f);
    junkHigh1.reset();
    junkHigh2.reset();
    for (auto& line : cleanDelay)
        std::fill(std::begin(line), std::end(line), 0.0f);
    cleanDelayPos = 0;
}

void Saturation::update(bool snap) noexcept
{
    driveDb.step(load(drive), snap);
    mixAmount.step(load(mix) * 0.01f, snap);
    bias.step(0.05f + load(character) * 0.0035f, snap); // 0.05 .. 0.40
    modelIndex = juce::jlimit(0, 2, (int)std::lround(load(model)));
}

float Saturation::getLatencySamples() const noexcept
{
    return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f;
}

void Saturation::process(float* const* ch, int numChannels, int n) noexcept
{
    const float k = dbToGain(driveDb.current);
    const float makeup = 1.0f / std::pow(k, 0.6f); // quiet parts stay level, loud parts get denser
    const float wet = mixAmount.current;
    const float b = bias.current;
    // Tube leans further off-centre than the other models
    const float tubeBias = 0.15f + 1.5f * b;
    const float tanhTubeBias = std::tanh(tubeBias);

    // Keep the dry voice, then pre-emphasise the copy that gets saturated
    float dry[kMaxChannels][VocalChain::kSubBlock], clean[kMaxChannels][VocalChain::kSubBlock];
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
        {
            dry[c][i] = ch[c][i];
            ch[c][i] = clean[c][i] = preHigh.process(preLow.process(ch[c][i], c), c);
        }

    // The curve's small-signal gain
    auto shape = [&](float in) {
        return modelIndex == 0 ? shapeTube(in, tubeBias, tanhTubeBias)
                               : (modelIndex == 1 ? shapeTape(in, b * 0.5f) : shapeConsole(in, b * 0.3f));
    };
    constexpr float eps = 1.0e-3f;
    const float cleanGain = (shape(k * eps) - shape(-k * eps)) / (2.0f * eps);

    juce::dsp::AudioBlock<float> block(ch, (size_t)numChannels, (size_t)n);
    auto up = oversampler->processSamplesUp(block);
    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        auto* x = up.getChannelPointer(c);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            x[i] = shape(x[i] * k);
        }
    }
    oversampler->processSamplesDown(block);

    for (int i = 0; i < n; ++i)
    {
        const int readPos = (cleanDelayPos - cleanDelayLength + 8) & 7;
        for (int c = 0; c < numChannels; ++c)
        {
            cleanDelay[c][cleanDelayPos] = clean[c][i];
            // Clean voice + only what the curve added above 150 Hz
            const float cleanPart = cleanGain * cleanDelay[c][readPos];
            ch[c][i] = cleanPart + junkHigh2.process(junkHigh1.process(ch[c][i] - cleanPart, c), c);
        }
        cleanDelayPos = (cleanDelayPos + 1) & 7;
    }

    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
        {
            float y = postHigh.process(postLow.process(ch[c][i] * makeup, c), c);
            if (modelIndex == 1) // tape
                y = (tapeLowpass[c] += tapeCoef * (y - tapeLowpass[c]));

            const float blocked = y - dcIn[c] + dcCoef * dcOut[c];
            dcIn[c] = y;
            dcOut[c] = blocked;
            ch[c][i] = dry[c][i] + wet * (blocked - dry[c][i]);
        }
}

//==============================================================================
Exciter::Exciter(juce::AudioProcessorValueTreeState& s)
    : freq(param(s, pid::exFreq))
    , amount(param(s, pid::exAmount))
    , mix(param(s, pid::exMix))
{
}

Exciter::~Exciter() = default;

void Exciter::prepare(double sampleRate, int numChannels)
{
    sr = sampleRate;
    oversampler = makeOversampler(numChannels);
    update(true);
    reset();
}

void Exciter::reset() noexcept
{
    if (oversampler != nullptr)
        oversampler->reset();
    bandIn1.reset();
    bandIn2.reset();
    bandOut.reset();
}

void Exciter::update(bool snap) noexcept
{
    if (freqSmooth.step(load(freq), snap) || snap)
    {
        bandIn1.c = biquad::highpass(sr, freqSmooth.current, 0.54119610);
        bandIn2.c = biquad::highpass(sr, freqSmooth.current, 1.30656296);
        bandOut.c = biquad::highpass(sr, freqSmooth.current * 0.8, 0.70710678);
    }
    driveGain.step(dbToGain(load(amount) * 0.3f), snap); // 0 .. +30 dB into the curve
    mixAmount.step(load(mix) * 0.005f, snap);            // 0 .. 0.5 added on top
}

void Exciter::process(float* const* ch, int numChannels, int n) noexcept
{
    const float k = driveGain.current;
    const float makeup = 1.0f / std::pow(k, 0.8f);
    constexpr float b = 0.2f;
    const float tanhB = std::tanh(b);

    float dry[kMaxChannels][VocalChain::kSubBlock];
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
        {
            dry[c][i] = ch[c][i];
            ch[c][i] = bandIn2.process(bandIn1.process(ch[c][i], c), c); // only the top of the voice
        }

    juce::dsp::AudioBlock<float> block(ch, (size_t)numChannels, (size_t)n);
    auto up = oversampler->processSamplesUp(block);
    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        auto* x = up.getChannelPointer(c);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            x[i] = shapeTube(x[i] * k, b, tanhB); // asymmetric
    }
    oversampler->processSamplesDown(block);

    const float level = mixAmount.current;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = dry[c][i] + level * bandOut.process(ch[c][i] * makeup, c);
}

} // namespace stagemic::dsp
