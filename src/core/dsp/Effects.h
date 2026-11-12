// SPDX-License-Identifier: AGPL-3.0-or-later
// Chorus, Delay and Reverb. The dry voice always passes straight through
// (zero latency); only the added effect is delayed, as it should be.
#pragma once

#include "core/dsp/Module.h"

#include <vector>

namespace stagemic::dsp
{

/** Circular buffer with fractional (linearly interpolated) reads. */
class DelayLine
{
public:
    void allocate(int maxDelaySamples);
    void clear() noexcept;

    inline void push(float x) noexcept
    {
        buffer[(size_t)writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** delay is in samples, >= 1 (1 = the sample pushed most recently). */
    inline float read(float delay) const noexcept
    {
        const int whole = (int)delay;
        const float frac = delay - (float)whole;
        const int i0 = (writePos - whole) & mask;
        const int i1 = (i0 - 1) & mask;
        return buffer[(size_t)i0] + frac * (buffer[(size_t)i1] - buffer[(size_t)i0]);
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

class Chorus final : public Module
{
public:
    explicit Chorus(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*rate, *depth, *mix;
    double sr = 48000.0;
    DelayLine lines[kMaxChannels];
    BlockSmoothed rateHz, depthAmount, mixAmount;
    double phase = 0.0;
};

class Delay final : public Module
{
public:
    explicit Delay(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*time, *feedback, *tone, *mix;
    double sr = 48000.0;
    DelayLine lines[kMaxChannels];
    float delaySamples[kMaxChannels]{};
    float targetSamples = 0.0f, glideCoef = 0.0f;
    float lowpassState[kMaxChannels]{};
    float lowpassCoef = 1.0f;
    BlockSmoothed feedbackAmount, mixAmount;
};

class Reverb final : public Module
{
public:
    explicit Reverb(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*size, *damping, *width, *mix;
    juce::Reverb reverb;
    juce::Reverb::Parameters current;
    Biquad sendHighPass; // keeps low rumble out of the reverb
    BlockSmoothed mixAmount;
    std::vector<float> scratch[kMaxChannels];
};

} // namespace stagemic::dsp
