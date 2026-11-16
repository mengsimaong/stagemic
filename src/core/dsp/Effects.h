// SPDX-License-Identifier: AGPL-3.0-or-later
// Chorus, Delay and Reverb
#pragma once

#include "core/dsp/Module.h"

#include <vector>

namespace stagemic::dsp
{

// Circular buffer with fractional reads
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

    // delay is in samples, >= 1
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

// Lowers an effect while the singer is loud
class Ducker
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept { envelope = 0.0f; }
    float next(float voicePeak, float amountDb) noexcept;

private:
    float envelope = 0.0f, attack = 0.0f, release = 0.0f;
};

class PlateReverb
{
public:
    static constexpr int kLines = 8;

    void prepare(double sampleRate);
    void reset() noexcept;

    void setDecaySeconds(float rt60) noexcept;
    void setDamping(float amount01) noexcept; // 0 = bright, 1 = dark
    void setWidth(float width01) noexcept { width = width01; }

    // Processes one mono input sample
    inline void processSample(float in, float& outL, float& outR) noexcept;

private:
    struct AllPass
    {
        DelayLine line;
        float delay = 1.0f;
        float gain = 0.7f;
        inline float process(float x) noexcept
        {
            const float delayed = line.read(delay);
            const float w = x + gain * delayed;
            line.push(w);
            return delayed - gain * w;
        }
    };

    double sr = 48000.0;
    AllPass diffusers[4];
    DelayLine lines[kLines];
    float lengths[kLines]{};  // samples
    float feedback[kLines]{}; // per-line gain for the chosen decay time
    float lowpass[kLines]{};  // damping filter state
    float dampCoef = 0.5f;
    float modCos[kLines]{}, modSin[kLines]{}, rotCos[kLines]{}, rotSin[kLines]{};
    float modDepth = 0.0f;
    float width = 1.0f;
    int renormCounter = 0;
};

class Chorus final : public Module
{
public:
    bool isSend() const noexcept override { return true; }
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
    bool isSend() const noexcept override { return true; }
    explicit Delay(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*time, *feedback, *tone, *mix, *duck;
    double sr = 48000.0;
    DelayLine lines[kMaxChannels];
    float delaySamples[kMaxChannels]{};
    float targetSamples = 0.0f, glideCoef = 0.0f;
    float lowpassState[kMaxChannels]{};
    float lowpassCoef = 1.0f;
    Biquad lowCut; // keeps echoes from getting boomy
    Ducker ducker;
    BlockSmoothed feedbackAmount, mixAmount, duckDb;
};

class Reverb final : public Module
{
public:
    bool isSend() const noexcept override { return true; }
    explicit Reverb(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*size, *damping, *width, *mix, *preDelay, *lowCutFreq, *highCutFreq, *duck;
    double sr = 48000.0;
    PlateReverb plate;
    BlockSmoothed decaySeconds, dampAmount, widthAmount;
    DelayLine preDelayLines[kMaxChannels];
    BlockSmoothed preDelaySamples, lowCutSmooth, highCutSmooth, mixAmount, duckDb;
    Biquad lowCut, highCut; // shape the reverb itself
    Ducker ducker;
};

} // namespace stagemic::dsp
