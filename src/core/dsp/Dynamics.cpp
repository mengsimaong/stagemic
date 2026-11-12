// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Dynamics.h"

#include "core/Parameters.h"

namespace stagemic::dsp
{

namespace
{
inline float peakAcross(float* const* ch, int numChannels, int i) noexcept
{
    float p = std::abs(ch[0][i]);
    for (int c = 1; c < numChannels; ++c)
        p = std::max(p, std::abs(ch[c][i]));
    return p;
}
} // namespace

//==============================================================================
// Gate: opens above the threshold, closes 4 dB below it (hysteresis) after a
// short hold, and turns the signal down by "range" dB while closed.
Gate::Gate(juce::AudioProcessorValueTreeState& s)
    : threshold(param(s, pid::gateThreshold))
    , range(param(s, pid::gateRange))
    , release(param(s, pid::gateRelease))
{
}

void Gate::prepare(double sampleRate, int)
{
    sr = sampleRate;
    envAttack = timeToCoef(0.3f, sr);
    envRelease = timeToCoef(30.0f, sr);
    openCoef = timeToCoef(1.0f, sr);
    holdSamples = (int)(0.04 * sr);
    reset();
}

void Gate::reset() noexcept
{
    envelope = 0.0f;
    gain = 1.0f;
    open = true;
    holdCounter = holdSamples;
}

void Gate::update(bool) noexcept
{
    const float thrDb = load(threshold);
    thresholdGain = dbToGain(thrDb);
    closeGain = dbToGain(thrDb - 4.0f);
    floorGain = dbToGain(-load(range));
    closeCoef = timeToCoef(load(release), sr);
}

void Gate::process(float* const* ch, int numChannels, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float peak = peakAcross(ch, numChannels, i);
        envelope = peak + (peak > envelope ? envAttack : envRelease) * (envelope - peak);

        if (envelope > thresholdGain)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (open && envelope < closeGain)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                open = false;
        }

        const float target = open ? 1.0f : floorGain;
        gain = target + (target > gain ? openCoef : closeCoef) * (gain - target);

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= gain;
    }
    gainMeter.store(gain, std::memory_order_relaxed);
}

//==============================================================================
// De-esser: splits the voice into low | high with a Linkwitz-Riley crossover
// (the two halves add back up flat), and turns only the high "s" band down
// while it is louder than the threshold.
DeEsser::DeEsser(juce::AudioProcessorValueTreeState& s)
    : freq(param(s, pid::dsFreq))
    , threshold(param(s, pid::dsThreshold))
    , range(param(s, pid::dsRange))
{
}

void DeEsser::prepare(double sampleRate, int)
{
    sr = sampleRate;
    envAttack = timeToCoef(0.5f, sr);
    envRelease = timeToCoef(40.0f, sr);
    gr.setTimes(1.0f, 60.0f, sr);
    freqSmooth.snap(load(freq));
    splitter.setCutoff(sr, freqSmooth.current);
    reset();
}

void DeEsser::reset() noexcept
{
    splitter.reset();
    envelope = 0.0f;
    gr.state = 0.0f;
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::update(bool snap) noexcept
{
    if (freqSmooth.step(load(freq), snap) || snap)
        splitter.setCutoff(sr, freqSmooth.current);
    thresholdDb = load(threshold);
    maxCutDb = load(range);
}

void DeEsser::process(float* const* ch, int numChannels, int n) noexcept
{
    float low[kMaxChannels], high[kMaxChannels];

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            splitter.split(ch[c][i], c, low[c], high[c]);
            peak = std::max(peak, std::abs(high[c]));
        }

        envelope = peak + (peak > envelope ? envAttack : envRelease) * (envelope - peak);
        const float over = gainToDb(envelope) - thresholdDb;
        const float targetDb = over > 0.0f ? -std::min(maxCutDb, over * 0.75f) : 0.0f; // ~4:1
        const float g = dbToGain(gr.process(targetDb));

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] = low[c] + g * high[c];
    }
    grMeter.store(-gr.state, std::memory_order_relaxed);
}

//==============================================================================
// Compressor: feed-forward, stereo-linked peak detector, 6 dB soft knee.
Compressor::Compressor(juce::AudioProcessorValueTreeState& s)
    : threshold(param(s, pid::compThreshold))
    , ratio(param(s, pid::compRatio))
    , attack(param(s, pid::compAttack))
    , release(param(s, pid::compRelease))
    , makeup(param(s, pid::compMakeup))
{
}

void Compressor::prepare(double sampleRate, int)
{
    sr = sampleRate;
    reset();
}

void Compressor::reset() noexcept
{
    gr.state = 0.0f;
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void Compressor::update(bool snap) noexcept
{
    thresholdDb = load(threshold);
    ratioValue = std::max(1.0f, load(ratio));
    gr.setTimes(load(attack), load(release), sr);
    makeupDb.step(load(makeup), snap);
}

void Compressor::process(float* const* ch, int numChannels, int n) noexcept
{
    const float makeupGain = dbToGain(makeupDb.current);

    for (int i = 0; i < n; ++i)
    {
        const float levelDb = gainToDb(peakAcross(ch, numChannels, i));
        const float g = dbToGain(gr.process(compressorGainDb(levelDb, thresholdDb, ratioValue))) * makeupGain;

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= g;
    }

    grMeter.store(-gr.state, std::memory_order_relaxed);
}

//==============================================================================
// Multiband: Linkwitz-Riley 24 dB/oct crossovers split low | mid | high, each band
// has its own compressor and output gain. With no compression the bands sum flat.
MultibandCompressor::MultibandCompressor(juce::AudioProcessorValueTreeState& s)
    : lowCross(param(s, pid::mbLowCross))
    , highCross(param(s, pid::mbHighCross))
    , ratio(param(s, pid::mbRatio))
    , attack(param(s, pid::mbAttack))
    , release(param(s, pid::mbRelease))
    , thresholds{param(s, pid::mbLowThreshold), param(s, pid::mbMidThreshold), param(s, pid::mbHighThreshold)}
    , gains{param(s, pid::mbLowGain), param(s, pid::mbMidGain), param(s, pid::mbHighGain)}
{
}

void MultibandCompressor::prepare(double sampleRate, int)
{
    sr = sampleRate;
    update(true);
    reset();
}

void MultibandCompressor::reset() noexcept
{
    splitLow.reset();
    splitHigh.reset();
    lowPhaseFix.reset();
    for (auto& g : gr)
        g.state = 0.0f;
}

void MultibandCompressor::update(bool snap) noexcept
{
    const float lc = load(lowCross);
    const float hc = std::max(load(highCross), lc * 2.0f); // keep the mid band at least an octave wide

    const bool lowChanged = lowCrossSmooth.step(lc, snap);
    const bool highChanged = highCrossSmooth.step(hc, snap);

    if (lowChanged || snap)
        splitLow.setCutoff(sr, lowCrossSmooth.current);
    if (highChanged || snap)
    {
        splitHigh.setCutoff(sr, highCrossSmooth.current);
        lowPhaseFix.setCutoff(sr, highCrossSmooth.current);
    }

    ratioValue = std::max(1.0f, load(ratio));
    const float a = load(attack), r = load(release);
    for (int b = 0; b < kBands; ++b)
    {
        gr[b].setTimes(a, r, sr);
        thresholdDb[b] = load(thresholds[b]);
        bandGainDb[b].step(load(gains[b]), snap);
    }
}

void MultibandCompressor::process(float* const* ch, int numChannels, int n) noexcept
{
    float bandGain[kBands];
    for (int b = 0; b < kBands; ++b)
        bandGain[b] = dbToGain(bandGainDb[b].current);

    float bands[kMaxChannels][kBands];

    for (int i = 0; i < n; ++i)
    {
        float peak[kBands] = {0.0f, 0.0f, 0.0f};

        for (int c = 0; c < numChannels; ++c)
        {
            float low, rest, mid, high, lowA, lowB;
            splitLow.split(ch[c][i], c, low, rest);
            splitHigh.split(rest, c, mid, high);
            lowPhaseFix.split(low, c, lowA, lowB);

            bands[c][0] = lowA + lowB;
            bands[c][1] = mid;
            bands[c][2] = high;

            for (int b = 0; b < kBands; ++b)
                peak[b] = std::max(peak[b], std::abs(bands[c][b]));
        }

        float g[kBands];
        for (int b = 0; b < kBands; ++b)
            g[b] = dbToGain(gr[b].process(compressorGainDb(gainToDb(peak[b]), thresholdDb[b], ratioValue))) * bandGain[b];

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] = bands[c][0] * g[0] + bands[c][1] * g[1] + bands[c][2] * g[2];
    }
    for (int b = 0; b < kBands; ++b)
        grMeter[b].store(-gr[b].state, std::memory_order_relaxed);
}

//==============================================================================
// Limiter: instant attack, smooth release - the output can never exceed the
// ceiling, and there is no lookahead (zero latency).
Limiter::Limiter(juce::AudioProcessorValueTreeState& s)
    : ceiling(param(s, pid::limCeiling))
    , release(param(s, pid::limRelease))
{
}

void Limiter::prepare(double sampleRate, int)
{
    sr = sampleRate;
    reset();
}

void Limiter::reset() noexcept
{
    gain = 1.0f;
}

void Limiter::update(bool) noexcept
{
    ceilingGain = dbToGain(load(ceiling));
    releaseCoef = timeToCoef(load(release), sr);
}

void Limiter::process(float* const* ch, int numChannels, int n) noexcept
{
    float lowestGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float peak = peakAcross(ch, numChannels, i);
        const float target = peak > ceilingGain ? ceilingGain / peak : 1.0f;

        // Release glides up toward the target; attack is instant. Either way gain <= target.
        gain = target < gain ? target : target + releaseCoef * (gain - target);
        lowestGain = std::min(lowestGain, gain);

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= gain;
    }
    grMeter.store(-gainToDb(lowestGain), std::memory_order_relaxed);
}

} // namespace stagemic::dsp
