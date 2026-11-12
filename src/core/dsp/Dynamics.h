// SPDX-License-Identifier: AGPL-3.0-or-later
// Gate, De-Esser, Compressor, Multiband Compressor and Limiter.
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
    std::atomic<float>*threshold, *range, *release;
    double sr = 48000.0;
    float thresholdGain = 0.0f, closeGain = 0.0f, floorGain = 1.0f;
    float envelope = 0.0f, gain = 1.0f;
    float envAttack = 0.0f, envRelease = 0.0f, openCoef = 0.0f, closeCoef = 0.0f;
    int holdSamples = 0, holdCounter = 0;
    bool open = false;
    std::atomic<float> gainMeter{1.0f};
};

class DeEsser final : public Module
{
public:
    explicit DeEsser(juce::AudioProcessorValueTreeState&);
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*freq, *threshold, *range;
    double sr = 48000.0;
    BlockSmoothed freqSmooth;
    LinkwitzRiley splitter; // low | high, in phase with each other, so cutting the high band really cuts it
    float envelope = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
    GainSmoother gr;
    float thresholdDb = 0.0f, maxCutDb = 0.0f;
    std::atomic<float> grMeter{0.0f};
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
    std::atomic<float>*threshold, *ratio, *attack, *release, *makeup;
    double sr = 48000.0;
    GainSmoother gr;
    float thresholdDb = 0.0f, ratioValue = 1.0f;
    BlockSmoothed makeupDb;
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
    float ceilingGain = 1.0f, releaseCoef = 0.0f, gain = 1.0f;
    std::atomic<float> grMeter{0.0f};
};

} // namespace stagemic::dsp
