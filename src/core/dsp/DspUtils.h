// SPDX-License-Identifier: AGPL-3.0-or-later
// Small, allocation-free DSP building blocks shared by the StageMic
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

namespace stagemic::dsp
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxChannels = 2;

inline float dbToGain(float db) noexcept
{
    return std::exp(db * 0.11512925464970229f); // ln/20
}

inline float gainToDb(float gain) noexcept
{
    return 8.685889638065035f * std::log(std::max(gain, 1.0e-9f)); // 20/ln
}

// One-pole smoothing coefficient for a time constant in milliseconds
inline float timeToCoef(float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f)
        return 0.0f;
    return (float)std::exp(-1.0 / (ms * 0.001 * sampleRate));
}

inline float load(const std::atomic<float>* p) noexcept
{
    return p->load(std::memory_order_relaxed);
}

//==============================================================================
// A parameter value that glides toward its target
struct BlockSmoothed
{
    float current = 0.0f;
    float target = 0.0f;

    void snap(float v) noexcept { current = target = v; }

    // Returns true if the value changed this step
    bool step(float newTarget, bool snapNow, float amount = 0.3f) noexcept
    {
        target = newTarget;
        if (snapNow)
        {
            const bool changed = current != target;
            current = target;
            return changed;
        }

        if (current == target)
            return false;

        current += (target - current) * amount;
        if (std::abs(target - current) <= 1.0e-4f * std::max(1.0f, std::abs(target)))
            current = target;
        return true;
    }
};

//==============================================================================
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

// Biquad designs from the RBJ "Audio EQ Cookbook"
namespace biquad
{
inline double clampFreq(double f, double sampleRate) noexcept
{
    return std::clamp(f, 10.0, sampleRate * 0.45);
}

inline BiquadCoeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    const double inv = 1.0 / a0;
    return {(float)(b0 * inv), (float)(b1 * inv), (float)(b2 * inv), (float)(a1 * inv), (float)(a2 * inv)};
}

inline BiquadCoeffs lowpass(double sampleRate, double freq, double q) noexcept
{
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

inline BiquadCoeffs highpass(double sampleRate, double freq, double q) noexcept
{
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

// Band-pass with 0 dB gain at its centre
inline BiquadCoeffs bandPass(double sampleRate, double freq, double q) noexcept
{
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    return normalise(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * std::cos(w), 1.0 - alpha);
}

inline BiquadCoeffs peak(double sampleRate, double freq, double q, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

inline BiquadCoeffs lowShelf(double sampleRate, double freq, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double beta = 2.0 * std::sqrt(A) * (s / 2.0 * std::sqrt(2.0)); // shelf slope S = 1
    return normalise(A * ((A + 1) - (A - 1) * c + beta), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - beta),
                     (A + 1) + (A - 1) * c + beta, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - beta);
}

inline BiquadCoeffs highShelf(double sampleRate, double freq, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double beta = 2.0 * std::sqrt(A) * (s / 2.0 * std::sqrt(2.0));
    return normalise(A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - beta),
                     (A + 1) - (A - 1) * c + beta, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
}
} // namespace biquad

// Transposed direct form II biquad with per-channel state
struct Biquad
{
    BiquadCoeffs c;
    float s1[kMaxChannels]{};
    float s2[kMaxChannels]{};

    void reset() noexcept
    {
        std::fill(std::begin(s1), std::end(s1), 0.0f);
        std::fill(std::begin(s2), std::end(s2), 0.0f);
    }

    inline float process(float x, int ch) noexcept
    {
        const float y = c.b0 * x + s1[ch];
        s1[ch] = c.b1 * x - c.a1 * y + s2[ch];
        s2[ch] = c.b2 * x - c.a2 * y;
        return y;
    }
};

// 4th-order Linkwitz-Riley low/high pass pair
struct LinkwitzRiley
{
    Biquad lp1, lp2, hp1, hp2;

    void setCutoff(double sampleRate, double freq) noexcept
    {
        constexpr double q = 0.70710678118654752;
        lp1.c = lp2.c = biquad::lowpass(sampleRate, freq, q);
        hp1.c = hp2.c = biquad::highpass(sampleRate, freq, q);
    }

    void reset() noexcept
    {
        lp1.reset();
        lp2.reset();
        hp1.reset();
        hp2.reset();
    }

    inline void split(float x, int ch, float& low, float& high) noexcept
    {
        low = lp2.process(lp1.process(x, ch), ch);
        high = hp2.process(hp1.process(x, ch), ch);
    }
};

//==============================================================================
// Static gain curve of a compressor with a soft knee
inline float compressorGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb = 6.0f) noexcept
{
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;

    if (2.0f * over <= -kneeDb)
        return 0.0f;
    if (2.0f * over >= kneeDb)
        return slope * over;

    const float x = over + kneeDb * 0.5f;
    return slope * x * x / (2.0f * kneeDb);
}

// Smooths a gain-reduction value with separate attack and release
struct GainSmoother
{
    float state = 0.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;

    void setTimes(float attackMs, float releaseMs, double sampleRate) noexcept
    {
        attackCoef = timeToCoef(attackMs, sampleRate);
        releaseCoef = timeToCoef(releaseMs, sampleRate);
    }

    inline float process(float targetDb) noexcept
    {
        const float coef = targetDb < state ? attackCoef : releaseCoef;
        state = targetDb + coef * (state - targetDb);
        return state;
    }
};

} // namespace stagemic::dsp
