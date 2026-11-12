// SPDX-License-Identifier: AGPL-3.0-or-later
// Gate/Expander, Auto Level, De-Esser, Compressor, Multiband Compressor and Limiter.
// All zero-latency: no lookahead buffers.
#pragma once

#include "core/dsp/Module.h"

namespace stagemic::dsp
{

class Gate final : public Module
{
public:
    explicit Gate(juce::AudioProcessorValueTreeState&);
    /** Current gate gain (1 = open). Any thread. */
    float getGain() const noexcept { return gainMeter.load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*threshold, *range, *release, *ratio;
    double sr = 48000.0;
    float thresholdDb = -55.0f, rangeDb = 24.0f, ratioValue = 2.0f;
    float envelope = 0.0f, gainDb = 0.0f;
    float envAttack = 0.0f, envRelease = 0.0f, openCoef = 0.0f, closeCoef = 0.0f;
    int holdSamples = 0, holdCounter = 0;
    std::atomic<float> gainMeter{1.0f};
};

/** Auto Level: a slow "vocal rider" that nudges the voice toward a target
    level, so quiet talking and loud singing end up closer together before the
    compressor. It holds still during pauses so it never pumps up room noise. */
class Leveler final : public Module
{
public:
    explicit Leveler(juce::AudioProcessorValueTreeState&);
    /** Current gain change in dB (positive = boosting). Any thread. */
    float getGainDb() const noexcept { return gainMeter.load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*target, *range, *speed;
    double sr = 48000.0;
    float targetDb = -18.0f, rangeDb = 9.0f;
    float meanSquare = 0.0f, rmsCoef = 0.0f;
    float gainDb = 0.0f, upCoef = 0.0f, downCoef = 0.0f;
    std::atomic<float> gainMeter{0.0f};
};

/** De-esser.
      Band  (default) - cuts only a narrow band around the "s" frequency, so the
                        voice keeps its air and sparkle (dynamic-EQ style).
      Split           - turns down everything above the frequency (for very harsh mics).
    The threshold follows the body of the voice, so talking and loud singing are
    treated alike. Auto finds the sibilant frequency (4-11 kHz) by itself.
    Listen outputs only what the de-esser is listening to, for tuning by ear. */
class DeEsser final : public Module
{
public:
    DeEsser(juce::AudioProcessorValueTreeState&, const std::atomic<bool>& listen);
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }
    /** The frequency in use (the detected one when Auto is on). Any thread. */
    float getFrequency() const noexcept { return freqMeter.load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    static constexpr int kCandidates = 6;
    void setFrequency(float hz) noexcept;

    std::atomic<float>*freq, *threshold, *range, *mode, *autoFreq;
    const std::atomic<bool>& listen;
    double sr = 48000.0;
    BlockSmoothed freqSmooth;
    LinkwitzRiley splitter; // Split mode: low | high, in phase with each other
    Biquad band;            // Band mode: constant-0dB band-pass around the "s"
    Biquad candidates[kCandidates];
    float candidateEnv[kCandidates]{}, candidateAttack = 0.0f, candidateRelease = 0.0f;
    float detectedFreq = 6500.0f;
    float envelope = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
    float voiceEnvelope = 0.0f, voiceAttack = 0.0f, voiceRelease = 0.0f; // overall level, for a relative threshold
    GainSmoother gr;
    float thresholdDb = 0.0f, maxCutDb = 0.0f;
    bool splitMode = false, autoOn = false;
    std::atomic<float> grMeter{0.0f}, freqMeter{6500.0f};
};

class Compressor final : public Module
{
public:
    explicit Compressor(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

    /** Current gain reduction in dB (>= 0), for meters. Any thread. */
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }

private:
    std::atomic<float>*threshold, *ratio, *attack, *release, *makeup, *mix;
    double sr = 48000.0;
    GainSmoother gr;
    Biquad detectorLowCut;      // the detector ignores bass and plosives ("sidechain HPF")
    float meanSquare = 0.0f, rmsCoef = 0.0f;
    float thresholdDb = 0.0f, ratioValue = 1.0f;
    BlockSmoothed makeupDb, mixAmount;
    std::atomic<float> grMeter{0.0f};
};

class MultibandCompressor final : public Module
{
public:
    static constexpr int kBands = 3;
    explicit MultibandCompressor(juce::AudioProcessorValueTreeState&);
    float getGainReductionDb(int band) const noexcept { return grMeter[band].load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*lowCross, *highCross, *ratio, *attack, *release;
    std::atomic<float>* thresholds[kBands];
    std::atomic<float>* gains[kBands];
    double sr = 48000.0;

    BlockSmoothed lowCrossSmooth, highCrossSmooth;
    BlockSmoothed bandGainDb[kBands];
    LinkwitzRiley splitLow;     // input -> low | rest
    LinkwitzRiley splitHigh;    // rest  -> mid | high
    LinkwitzRiley lowPhaseFix;  // keeps the low band phase-aligned with mid + high
    GainSmoother gr[kBands];
    float thresholdDb[kBands]{};
    float ratioValue = 1.0f;
    std::atomic<float> grMeter[kBands]{};
};

class Limiter final : public Module
{
public:
    explicit Limiter(juce::AudioProcessorValueTreeState&);
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*ceiling, *release;
    double sr = 48000.0;
    float ceilingGain = 1.0f, attackCoef = 0.0f, releaseCoef = 0.0f, gain = 1.0f;
    std::atomic<float> grMeter{0.0f};
};

} // namespace stagemic::dsp
