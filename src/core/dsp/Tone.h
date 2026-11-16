// SPDX-License-Identifier: AGPL-3.0-or-later
// Equalizer and Saturation
#pragma once

#include "core/dsp/Module.h"

#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace stagemic::dsp
{

// Low cut, low shelf, mid bell and high "air" shelf
class Equalizer final : public Module
{
public:
    explicit Equalizer(juce::AudioProcessorValueTreeState&);
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

    // Harshness tamer
    float getTameReductionDb() const noexcept { return tameMeter.load(std::memory_order_relaxed); }
    // Plosive tamer
    float getPlosiveActivity() const noexcept { return popMeter.load(std::memory_order_relaxed); }

private:
    void processTame(float* const* channels, int numChannels, int numSamples) noexcept;
    void processPlosive(float* const* channels, int numChannels, int numSamples) noexcept;

    enum Band
    {
        LowCut,
        LowCut2, // second stage for the 24 dB/oct slope
        LowShelf,
        LoMid, // extra bell
        Mid,
        HiMid, // extra bell
        High,
        NumBands
    };

    std::atomic<float>*lowCut, *lowCutSlope, *lowFreq, *lowGain, *midFreq, *midGain, *midQ, *highFreq, *highGain;
    std::atomic<float>*loMidFreq, *loMidGain, *loMidQ, *hiMidFreq, *hiMidGain, *hiMidQ;
    std::atomic<float>* pointOn[7] = {};                             // on/off per EQ point
    bool pointWasOn[7] = {true, true, true, true, true, true, true}; // only the low cut needs it
    bool tameOn = true;
    std::atomic<float>*tameFreq, *tameThreshold, *tameRange, *plosive;
    int slope = -1;
    double sr = 48000.0;
    BlockSmoothed sLowCut, sLowFreq, sLowGain, sMidFreq, sMidGain, sMidQ, sHighFreq, sHighGain, sTameFreq;
    BlockSmoothed sLoMidFreq, sLoMidGain, sLoMidQ, sHiMidFreq, sHiMidGain, sHiMidQ;
    bool bandIdle[NumBands]{}; // an extra bell at exactly 0 dB: not run at all
    Biquad filters[NumBands];
    // Coefficient glide while a setting moves
    BiquadCoeffs glideTo[NumBands];
    bool gliding[NumBands]{};
    void setBand(Band band, const BiquadCoeffs& coeffs, bool snap) noexcept;
    void runCascade(float* const* channels, int numChannels, int numSamples, const int* active, int count) noexcept;

    // Harshness tamer
    Biquad tameBand, tameBody;
    float tameEnv = 0.0f, bodyEnv = 0.0f, tameAttack = 0.0f, tameRelease = 0.0f, bodyAttack = 0.0f, bodyRelease = 0.0f;
    float tameThresholdDb = -3.0f, tameMaxCutDb = 0.0f;
    GainSmoother tameGr;

    struct SvfState
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };
    SvfState popSvf[2][kMaxChannels];
    Biquad popDetect, popBody;
    float popEnv = 0.0f, popBodyEnv = 0.0f, popMix = 0.0f, popAmount = 0.0f;
    float popAttack = 0.0f, popRelease = 0.0f, popBodyAttack = 0.0f, popBodyRelease = 0.0f, popMixAttack = 0.0f, popMixRelease = 0.0f;
    std::atomic<float> tameMeter{0.0f}, popMeter{0.0f};
};

// Saturation with 4x oversampling and three models
class Saturation final : public Module
{
public:
    explicit Saturation(juce::AudioProcessorValueTreeState&);
    ~Saturation() override;
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

    // The oversampler's delay on the saturated signal
    float getLatencySamples() const noexcept;

private:
    std::atomic<float>*drive, *mix, *character, *model;
    double sr = 48000.0;
    int modelIndex = 0;
    BlockSmoothed driveDb, mixAmount, bias;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    Biquad preLow, preHigh, postLow, postHigh; // emphasis around the curve
    float tapeLowpass[kMaxChannels]{}, tapeCoef = 1.0f;
    float dcIn[kMaxChannels]{}, dcOut[kMaxChannels]{}, dcCoef = 0.999f;

    // DC blocker for the asymmetric curve
    Biquad junkHigh1, junkHigh2;         // 24 dB/oct high-pass at 150 Hz on what the curve
    float cleanDelay[kMaxChannels][8]{}; // lines the clean copy up with the oversampled
    int cleanDelayLength = 0, cleanDelayPos = 0;
};

class Exciter final : public Module
{
public:
    explicit Exciter(juce::AudioProcessorValueTreeState&);
    ~Exciter() override;
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

private:
    std::atomic<float>*freq, *amount, *mix;
    double sr = 48000.0;
    BlockSmoothed freqSmooth, driveGain, mixAmount;
    Biquad bandIn1, bandIn2, bandOut;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};

} // namespace stagemic::dsp
