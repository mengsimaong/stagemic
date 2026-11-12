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
// Gate / expander: below the threshold the voice is turned down gradually
// (ratio 2:1 = gentle expander, 10:1 = hard gate), never by more than "range".
// A 40 ms hold keeps word endings and sung note tails from being chopped.
Gate::Gate(juce::AudioProcessorValueTreeState& s)
    : threshold(param(s, pid::gateThreshold))
    , range(param(s, pid::gateRange))
    , release(param(s, pid::gateRelease))
    , ratio(param(s, pid::gateRatio))
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
    gainDb = 0.0f;
    holdCounter = holdSamples;
    gainMeter.store(1.0f, std::memory_order_relaxed);
}

void Gate::update(bool) noexcept
{
    thresholdDb = load(threshold);
    rangeDb = load(range);
    ratioValue = std::max(1.0f, load(ratio));
    closeCoef = timeToCoef(load(release), sr);
}

void Gate::process(float* const* ch, int numChannels, int n) noexcept
{
    float gain = dbToGain(gainDb);
    for (int i = 0; i < n; ++i)
    {
        const float peak = peakAcross(ch, numChannels, i);
        envelope = peak + (peak > envelope ? envAttack : envRelease) * (envelope - peak);

        const float below = gainToDb(envelope) - thresholdDb;
        float targetDb = 0.0f;
        if (below >= 0.0f)
            holdCounter = holdSamples;
        else if (holdCounter > 0)
            --holdCounter;
        else
            targetDb = std::max(-rangeDb, below * (ratioValue - 1.0f));

        gainDb = targetDb + (targetDb > gainDb ? openCoef : closeCoef) * (gainDb - targetDb);
        gain = dbToGain(gainDb);

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= gain;
    }
    gainMeter.store(gain, std::memory_order_relaxed);
}

//==============================================================================
// Auto Level: measures the voice's level over ~300 ms and slowly steers it
// toward the target (by at most +/- range). It reacts 3x faster when the voice
// gets louder than when it gets quieter, and freezes during pauses.
Leveler::Leveler(juce::AudioProcessorValueTreeState& s)
    : target(param(s, pid::levTarget))
    , range(param(s, pid::levRange))
    , speed(param(s, pid::levSpeed))
{
}

void Leveler::prepare(double sampleRate, int)
{
    sr = sampleRate;
    rmsCoef = timeToCoef(300.0f, sr);
    update(true);
    reset();
}

void Leveler::reset() noexcept
{
    meanSquare = 0.0f;
    gainDb = 0.0f;
    gainMeter.store(0.0f, std::memory_order_relaxed);
}

void Leveler::update(bool) noexcept
{
    targetDb = load(target);
    rangeDb = load(range);
    const float ms = load(speed);
    upCoef = timeToCoef(ms, sr);
    downCoef = timeToCoef(ms / 3.0f, sr);
}

void Leveler::process(float* const* ch, int numChannels, int n) noexcept
{
    constexpr float silenceDb = -50.0f; // below this it's a pause: hold the current gain
    for (int i = 0; i < n; ++i)
    {
        float sq = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            sq = std::max(sq, ch[c][i] * ch[c][i]);
        meanSquare = sq + rmsCoef * (meanSquare - sq);

        const float levelDb = 10.0f * std::log10(std::max(meanSquare, 1.0e-12f));
        if (levelDb > silenceDb)
        {
            const float wanted = juce::jlimit(-rangeDb, rangeDb, targetDb - levelDb);
            gainDb = wanted + (wanted < gainDb ? downCoef : upCoef) * (gainDb - wanted);
        }

        const float g = dbToGain(gainDb);
        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= g;
    }
    gainMeter.store(gainDb, std::memory_order_relaxed);
}

//==============================================================================
// De-esser (see Dynamics.h for the modes).
namespace
{
constexpr float candidateHz[] = {4500.0f, 5500.0f, 6600.0f, 7800.0f, 9200.0f, 10800.0f};

using biquad::bandPass;
} // namespace

DeEsser::DeEsser(juce::AudioProcessorValueTreeState& s, const std::atomic<bool>& listenFlag)
    : freq(param(s, pid::dsFreq))
    , threshold(param(s, pid::dsThreshold))
    , range(param(s, pid::dsRange))
    , mode(param(s, pid::dsMode))
    , autoFreq(param(s, pid::dsAuto))
    , listen(listenFlag)
{
}

void DeEsser::prepare(double sampleRate, int)
{
    sr = sampleRate;
    envAttack = timeToCoef(0.5f, sr);
    envRelease = timeToCoef(40.0f, sr);
    voiceAttack = timeToCoef(5.0f, sr);
    voiceRelease = timeToCoef(200.0f, sr);
    candidateAttack = timeToCoef(3.0f, sr);
    candidateRelease = timeToCoef(120.0f, sr);
    gr.setTimes(1.0f, 60.0f, sr);
    for (int i = 0; i < kCandidates; ++i)
        candidates[i].c = bandPass(sr, candidateHz[i], 2.5);
    freqSmooth.snap(load(freq));
    setFrequency(freqSmooth.current);
    reset();
}

void DeEsser::setFrequency(float hz) noexcept
{
    splitter.setCutoff(sr, hz);
    band.c = bandPass(sr, hz, 1.4);
    freqMeter.store(hz, std::memory_order_relaxed);
}

void DeEsser::reset() noexcept
{
    splitter.reset();
    band.reset();
    for (auto& c : candidates)
        c.reset();
    std::fill(std::begin(candidateEnv), std::end(candidateEnv), 0.0f);
    envelope = 0.0f;
    voiceEnvelope = 0.0f;
    gr.state = 0.0f;
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::update(bool snap) noexcept
{
    autoOn = load(autoFreq) >= 0.5f;
    splitMode = load(mode) >= 0.5f;

    if (autoOn)
    {
        // Follow the loudest candidate band, but only while something sibilant is happening.
        int loudest = 0;
        for (int i = 1; i < kCandidates; ++i)
            if (candidateEnv[i] > candidateEnv[loudest])
                loudest = i;
        if (gainToDb(candidateEnv[loudest]) > gainToDb(voiceEnvelope) - 12.0f && candidateEnv[loudest] > 1.0e-4f)
            detectedFreq = candidateHz[loudest];
    }

    const float target = autoOn ? detectedFreq : load(freq);
    if (freqSmooth.step(target, snap, autoOn ? 0.05f : 0.3f) || snap)
        setFrequency(freqSmooth.current);

    thresholdDb = load(threshold);
    maxCutDb = load(range);
}

void DeEsser::process(float* const* ch, int numChannels, int n) noexcept
{
    const bool listening = listen.load(std::memory_order_relaxed);
    float low[kMaxChannels], high[kMaxChannels];

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f, voicePeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = ch[c][i];
            if (splitMode)
                splitter.split(x, c, low[c], high[c]); // high = everything above Freq
            else
            {
                high[c] = band.process(x, c);           // high = just the "s" band
                low[c] = x - high[c];                   // everything else (exact reconstruction)
            }
            peak = std::max(peak, std::abs(high[c]));
            voicePeak = std::max(voicePeak, std::abs(low[c]));

            if (autoOn && c == 0)
                for (int k = 0; k < kCandidates; ++k)
                {
                    const float v = std::abs(candidates[k].process(x, 0));
                    candidateEnv[k] = v + (v > candidateEnv[k] ? candidateAttack : candidateRelease) * (candidateEnv[k] - v);
                }
        }

        envelope = peak + (peak > envelope ? envAttack : envRelease) * (envelope - peak);
        voiceEnvelope = voicePeak + (voicePeak > voiceEnvelope ? voiceAttack : voiceRelease) * (voiceEnvelope - voicePeak);
        // The threshold follows the body of the voice (set for around -24 dBFS),
        // so an "s" can never raise its own threshold.
        const float follow = juce::jlimit(-12.0f, 18.0f, gainToDb(voiceEnvelope) + 24.0f);
        const float over = gainToDb(envelope) - (thresholdDb + follow);
        const float targetDb = over > 0.0f ? -std::min(maxCutDb, over * 0.75f) : 0.0f; // ~4:1
        const float g = dbToGain(gr.process(targetDb));

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] = listening ? high[c] : low[c] + g * high[c];
    }
    grMeter.store(-gr.state, std::memory_order_relaxed);
}

//==============================================================================
// Compressor: feed-forward, stereo-linked, 6 dB soft knee. The detector hears
// the voice through a 100 Hz low cut (so plosives and rumble don't pump it) and
// measures a short 5 ms RMS level, which sounds smoother on vocals than raw
// peaks. "Mix" blends in the uncompressed voice (parallel compression).
Compressor::Compressor(juce::AudioProcessorValueTreeState& s)
    : threshold(param(s, pid::compThreshold))
    , ratio(param(s, pid::compRatio))
    , attack(param(s, pid::compAttack))
    , release(param(s, pid::compRelease))
    , makeup(param(s, pid::compMakeup))
    , mix(param(s, pid::compMix))
{
}

void Compressor::prepare(double sampleRate, int)
{
    sr = sampleRate;
    detectorLowCut.c = biquad::highpass(sr, 100.0, 0.70710678);
    rmsCoef = timeToCoef(5.0f, sr);
    reset();
}

void Compressor::reset() noexcept
{
    gr.state = 0.0f;
    meanSquare = 0.0f;
    detectorLowCut.reset();
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void Compressor::update(bool snap) noexcept
{
    thresholdDb = load(threshold);
    ratioValue = std::max(1.0f, load(ratio));
    gr.setTimes(load(attack), load(release), sr);
    makeupDb.step(load(makeup), snap);
    mixAmount.step(load(mix) * 0.01f, snap);
}

void Compressor::process(float* const* ch, int numChannels, int n) noexcept
{
    const float makeupGain = dbToGain(makeupDb.current);
    const float wet = mixAmount.current;

    for (int i = 0; i < n; ++i)
    {
        float sq = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float d = detectorLowCut.process(ch[c][i], c);
            sq = std::max(sq, d * d);
        }
        meanSquare = sq + rmsCoef * (meanSquare - sq);

        const float levelDb = 10.0f * std::log10(std::max(meanSquare, 1.0e-12f));
        const float compressed = dbToGain(gr.process(compressorGainDb(levelDb, thresholdDb, ratioValue))) * makeupGain;
        const float g = (1.0f - wet) + wet * compressed;

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
// Limiter: a fast (0.3 ms) attack and smooth release keep the level under the
// ceiling without the harshness of an instant gain jump; a soft clipper then
// catches whatever slips through, so the output can never exceed the ceiling.
// No lookahead (zero latency).
Limiter::Limiter(juce::AudioProcessorValueTreeState& s)
    : ceiling(param(s, pid::limCeiling))
    , release(param(s, pid::limRelease))
{
}

void Limiter::prepare(double sampleRate, int)
{
    sr = sampleRate;
    attackCoef = timeToCoef(0.3f, sr);
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

namespace
{
// Linear up to 'knee' (as a fraction of the ceiling), then bends smoothly toward
// the ceiling and never reaches it.
inline float softClip(float x, float ceilingGain) noexcept
{
    constexpr float knee = 0.88f;
    const float u = std::abs(x) / ceilingGain;
    if (u <= knee)
        return x;
    const float bent = knee + (1.0f - knee) * std::tanh((u - knee) / (1.0f - knee));
    return std::copysign(bent * ceilingGain, x);
}
} // namespace

void Limiter::process(float* const* ch, int numChannels, int n) noexcept
{
    float lowestGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float peak = peakAcross(ch, numChannels, i);
        const float target = peak > ceilingGain ? ceilingGain / peak : 1.0f;
        gain = target + (target < gain ? attackCoef : releaseCoef) * (gain - target);
        lowestGain = std::min(lowestGain, gain);

        for (int c = 0; c < numChannels; ++c)
            ch[c][i] = softClip(ch[c][i] * gain, ceilingGain);
    }
    grMeter.store(-gainToDb(lowestGain), std::memory_order_relaxed);
}

} // namespace stagemic::dsp
