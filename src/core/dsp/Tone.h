// SPDX-License-Identifier: AGPL-3.0-or-later
// Equalizer and Saturation. Zero latency.
#pragma once

#include "core/dsp/Module.h"

#include <juce_dsp/juce_dsp.h>
#include <memory>

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

    /** Harshness tamer: how much it is cutting right now (dB, positive). Any thread. */
    float getTameReductionDb() const noexcept { return tameMeter.load(std::memory_order_relaxed); }
    /** Plosive tamer: 0..1, how much it is filtering right now. Any thread. */
    float getPlosiveActivity() const noexcept { return popMeter.load(std::memory_order_relaxed); }

private:
    void processTame(float* const* channels, int numChannels, int numSamples) noexcept;
    void processPlosive(float* const* channels, int numChannels, int numSamples) noexcept;

    enum Band
    {
        LowCut,
        LowCut2, // second stage for the 24 dB/oct slope (pass-through at 12 dB/oct)
        LowShelf,
        LoMid, // extra bell; skipped entirely while its gain is 0 dB
        Mid,
        HiMid, // extra bell; skipped entirely while its gain is 0 dB
        High,
        NumBands
    };

    std::atomic<float>*lowCut, *lowCutSlope, *lowFreq, *lowGain, *midFreq, *midGain, *midQ, *highFreq, *highGain;
    std::atomic<float>*loMidFreq, *loMidGain, *loMidQ, *hiMidFreq, *hiMidGain, *hiMidQ;
    std::atomic<float>* pointOn[7] = {}; // on/off per EQ point
    bool pointWasOn[7] = {true, true, true, true, true, true, true}; // only the low cut needs it
    bool tameOn = true;
    std::atomic<float>*tameFreq, *tameThreshold, *tameRange, *plosive;
    int slope = -1;
    double sr = 48000.0;
    BlockSmoothed sLowCut, sLowFreq, sLowGain, sMidFreq, sMidGain, sMidQ, sHighFreq, sHighGain, sTameFreq;
    BlockSmoothed sLoMidFreq, sLoMidGain, sLoMidQ, sHiMidFreq, sHiMidGain, sHiMidQ;
    bool bandIdle[NumBands]{}; // an extra bell at exactly 0 dB: not run at all
    Biquad filters[NumBands];
    // While a setting moves, each filter glides to its new shape sample by sample
    // instead of jumping once per block (which is heard as a faint crackle).
    BiquadCoeffs glideTo[NumBands];
    bool gliding[NumBands]{};
    void setBand(Band band, const BiquadCoeffs& coeffs, bool snap) noexcept;
    void runCascade(float* const* channels, int numChannels, int numSamples, const int* active, int count) noexcept;

    // Harshness tamer: y = x + (g - 1) * band(x), with g following how far the
    // band sticks out above the body of the voice (so it's level-independent).
    Biquad tameBand, tameBody;
    float tameEnv = 0.0f, bodyEnv = 0.0f, tameAttack = 0.0f, tameRelease = 0.0f, bodyAttack = 0.0f, bodyRelease = 0.0f;
    float tameThresholdDb = -3.0f, tameMaxCutDb = 0.0f;
    GainSmoother tameGr;

    // Plosive tamer: a 24 dB/oct high-pass whose cutoff sweeps from 10 Hz up to
    // 160 Hz while the low end jumps far above the body of the voice (a "p"/"b"
    // thump), then glides back. Built from state-variable filters, which stay
    // smooth while their frequency moves (no clicks from the sweep).
    struct SvfState { float ic1 = 0.0f, ic2 = 0.0f; };
    SvfState popSvf[2][kMaxChannels];
    Biquad popDetect, popBody;
    float popEnv = 0.0f, popBodyEnv = 0.0f, popMix = 0.0f, popAmount = 0.0f;
    float popAttack = 0.0f, popRelease = 0.0f, popBodyAttack = 0.0f, popBodyRelease = 0.0f, popMixAttack = 0.0f, popMixRelease = 0.0f;
    std::atomic<float> tameMeter{0.0f}, popMeter{0.0f};
};

/** Saturation with 4x oversampling (minimum-phase, ~0.05 ms) and three models:
      Tube    - asymmetric tanh: warm, mostly even harmonics
      Tape    - soft symmetric curve plus gentle high-frequency softening
      Console - subtle cubic curve: density without obvious distortion
    The voice is pre-emphasised before the curve and restored after it, so the
    mids saturate more than the lows and the "s" range (thicker, never harsh).
    "Character" adds asymmetry (warmth); a DC blocker removes the offset it makes. */
class Saturation final : public Module
{
public:
    explicit Saturation(juce::AudioProcessorValueTreeState&);
    ~Saturation() override;
    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

    /** The oversampler's delay on the saturated signal, in samples at the base rate. */
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

    // A lopsided curve also makes a DC offset that rises and falls with loudness:
    // a sub-bass "thump" following every syllable. Below 150 Hz only the voice's
    // own (clean) part is kept; the distortion made there is removed.
    Biquad junkHigh1, junkHigh2;             // 24 dB/oct high-pass at 150 Hz on what the curve added
    float cleanDelay[kMaxChannels][8]{};     // lines the clean copy up with the oversampled path
    int cleanDelayLength = 0, cleanDelayPos = 0;
};

/** Exciter: takes only the upper range of the voice (above Freq), generates
    fresh harmonics there (4x oversampled), and mixes a little back on top of
    the untouched voice. Adds "air" and presence without simply boosting hiss. */
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
    Biquad bandIn1, bandIn2, bandOut; // 4th-order high-pass in, 2nd-order after the curve
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};

} // namespace stagemic::dsp
