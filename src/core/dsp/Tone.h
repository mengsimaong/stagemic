// SPDX-License-Identifier: AGPL-3.0-or-later
// Equalizer and Saturation. Zero latency.
#pragma once

#include "core/dsp/Module.h"

namespace stagemic::dsp
{

/** Low cut, low shelf, mid bell and high "air" shelf. */
class Equalizer final : public Module
{
public:
    explicit Equalizer(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    enum Band
    {
        LowCut,
        LowShelf,
        Mid,
        High,
        NumBands
    };

    std::atomic<float>*lowCut, *lowFreq, *lowGain, *midFreq, *midGain, *midQ, *highFreq, *highGain;
    double sr = 48000.0;
    BlockSmoothed sLowCut, sLowFreq, sLowGain, sMidFreq, sMidGain, sMidQ, sHighFreq, sHighGain;
    Biquad filters[NumBands];
};

/** Warm tanh saturation with first-order anti-aliasing (ADAA), dry/wet mix. */
class Saturation final : public Module
{
public:
    explicit Saturation(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*drive, *mix;
    BlockSmoothed driveDb, mixAmount;
    double previous[kMaxChannels]{};
};

} // namespace stagemic::dsp
