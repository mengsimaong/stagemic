// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/dsp/DspUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace stagemic
{

class VoiceAnalyzer
{
public:
    enum class Phase : int
    {
        Idle,
        Silence,
        Voice,
        Done
    };

    static constexpr double silenceSeconds = 3.0;
    static constexpr double voiceSeconds = 8.0;
    static constexpr int kBins = 121; // 1 dB histogram bins
    static constexpr int kCandidates = 6;
    static constexpr float candidateHz[kCandidates] = {4500.0f, 5500.0f, 6600.0f, 7800.0f, 9200.0f, 10800.0f};

    // What was measured (valid once getPhase == Done)
    struct Measurements
    {
        std::array<int, kBins> noise{}, voice{};
        std::array<int, kBins> noisePeak{}; // room noise as the Gate hears it
        float peakDb = -120.0f;
        int sibilantWindows = 0;
        std::array<double, kCandidates> sibilance{};
    };

    // What we'd set, worked out from Measurements
    struct Result
    {
        bool heardVoice = false;
        float noiseDb = -120.0f, voiceDb = -120.0f, peakDb = -120.0f; // raw mic levels
        float noisePeakDb = -120.0f;                                  // the room noise's peaks, as the Gate sees them
        float inputGainDb = 0.0f, gateThresholdDb = -55.0f, gateRatio = 2.0f;
        float sibilantHz = 0.0f; // 0 = not enough "s" sounds to tell
        bool clipped = false, noisy = false, veryQuiet = false;
    };

    // ---- message thread
    void start() noexcept { command.store(1, std::memory_order_release); }
    void cancel() noexcept { command.store(2, std::memory_order_release); }
    Phase getPhase() const noexcept { return (Phase)phase.load(std::memory_order_acquire); }
    float getProgress() const noexcept { return progress.load(std::memory_order_relaxed); }
    const Measurements& getMeasurements() const noexcept { return m; } // only when Done

    static Result evaluate(const Measurements& m) noexcept
    {
        Result r;
        r.peakDb = m.peakDb;
        r.noiseDb = percentile(m.noise, 0, 0.5f);
        const int voiceFrom = std::clamp(binOf(r.noiseDb + 10.0f), 0, kBins - 1);
        int voiceCount = 0;
        for (int b = voiceFrom; b < kBins; ++b)
            voiceCount += m.voice[(size_t)b];
        r.heardVoice = voiceCount >= 20; // at least one second of voice
        if (!r.heardVoice)
            return r;

        r.voiceDb = percentile(m.voice, voiceFrom, 0.5f);

        // Typical voice level to about -20 dBFS RMS
        constexpr float targetDb = -20.0f;
        float gain = std::min(targetDb - r.voiceDb, -3.0f - r.peakDb);
        r.veryQuiet = gain > 24.0f;
        r.inputGainDb = std::round(std::clamp(gain, -24.0f, 24.0f) * 2.0f) * 0.5f;

        // Gate between the room noise and the quiet end of the voice
        const float noiseAfter = r.noiseDb + r.inputGainDb, voiceAfter = r.voiceDb + r.inputGainDb;
        float gate = noiseAfter + 0.35f * (voiceAfter - noiseAfter);
        gate = std::clamp(gate, noiseAfter + 6.0f, std::max(noiseAfter + 6.0f, voiceAfter - 15.0f));
        // The Gate reacts to peaks, and noise peaks about 10 dB above its average
        r.noisePeakDb = percentile(m.noisePeak, 0, 0.9f);
        const float noisePeakAfter = r.noisePeakDb + r.inputGainDb;
        gate = std::max(gate, std::min(noisePeakAfter + 6.0f, voiceAfter - 6.0f));
        // Clearly audible noise
        r.gateRatio = noiseAfter > -58.0f ? 4.0f : 2.0f;
        r.gateThresholdDb = std::round(std::clamp(gate, -80.0f, -10.0f));

        r.clipped = r.peakDb > -1.0f;
        r.noisy = r.voiceDb - r.noiseDb < 25.0f;

        if (m.sibilantWindows >= 3)
        {
            int best = 0;
            for (int i = 1; i < kCandidates; ++i)
                if (m.sibilance[(size_t)i] > m.sibilance[(size_t)best])
                    best = i;
            r.sibilantHz = candidateHz[best];
        }
        return r;
    }

    // ---- audio thread
    void prepare(double sampleRate) noexcept
    {
        sr = sampleRate;
        phase.store((int)Phase::Idle, std::memory_order_release);
    }

    void push(float* const* ch, int numChannels, int n) noexcept
    {
        if (const int cmd = command.exchange(0, std::memory_order_acq_rel); cmd == 1)
            begin();
        else if (cmd == 2)
            phase.store((int)Phase::Idle, std::memory_order_release);

        const auto p = (Phase)phase.load(std::memory_order_relaxed);
        if (p != Phase::Silence && p != Phase::Voice)
            return;

        const float norm = 1.0f / (float)numChannels;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                x += ch[c][i];
            x *= norm;

            peakAbs = std::max(peakAbs, std::abs(x));
            // Same peak follower as the Gate
            float across = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                across = std::max(across, std::abs(ch[c][i]));
            gateEnvelope = across + (across > gateEnvelope ? envAttack : envRelease) * (gateEnvelope - across);
            windowEnvelopeMax = std::max(windowEnvelopeMax, gateEnvelope);
            windowSum += (double)x * x;
            const float b = body.process(x, 0), h = high.process(x, 0);
            bodySum += (double)b * b;
            highSum += (double)h * h;
            for (int k = 0; k < kCandidates; ++k)
            {
                const float y = bands[k].process(x, 0);
                bandSum[k] += (double)y * y;
            }
            if (++windowCount >= windowLength)
                endWindow();
        }
    }

private:
    static int binOf(float db) noexcept { return (int)std::lround(std::clamp(db, -120.0f, 0.0f)) + 120; }

    static float percentile(const std::array<int, kBins>& h, int from, float fraction) noexcept
    {
        int total = 0;
        for (int b = from; b < kBins; ++b)
            total += h[(size_t)b];
        if (total == 0)
            return -120.0f;
        int seen = 0;
        for (int b = from; b < kBins; ++b)
            if ((seen += h[(size_t)b]) >= (int)std::ceil((float)total * fraction))
                return (float)(b - 120);
        return 0.0f;
    }

    void begin() noexcept
    {
        m = {};
        peakAbs = 0.0f;
        windowLength = std::max(1, (int)(sr * 0.05));
        envAttack = (float)std::exp(-1.0 / (0.0003 * sr));
        envRelease = (float)std::exp(-1.0 / (0.030 * sr));
        gateEnvelope = 0.0f;
        totalWindows = 0;
        body.c = dsp::biquad::bandPass(sr, 400.0, 0.5);
        high.c = dsp::biquad::highpass(sr, 4000.0, 0.707);
        body.reset();
        high.reset();
        for (int k = 0; k < kCandidates; ++k)
        {
            bands[k].c = dsp::biquad::bandPass(sr, candidateHz[k], 2.5);
            bands[k].reset();
        }
        clearWindow();
        progress.store(0.0f, std::memory_order_relaxed);
        phase.store((int)Phase::Silence, std::memory_order_release);
    }

    void clearWindow() noexcept
    {
        windowCount = 0;
        windowEnvelopeMax = 0.0f;
        windowSum = bodySum = highSum = 0.0;
        bandSum.fill(0.0);
    }

    void endWindow() noexcept
    {
        const float db = windowSum > 1.0e-12 ? 10.0f * (float)std::log10(windowSum / windowCount) : -120.0f;
        const auto p = (Phase)phase.load(std::memory_order_relaxed);
        const int silenceWindows = (int)std::lround(silenceSeconds / 0.05), voiceWindows = (int)std::lround(voiceSeconds / 0.05);

        if (p == Phase::Silence)
        {
            // Skip the first quarter second
            if (totalWindows >= 5)
            {
                ++m.noise[(size_t)binOf(db)];
                ++m.noisePeak[(size_t)binOf(windowEnvelopeMax > 1.0e-6f ? 20.0f * std::log10(windowEnvelopeMax) : -120.0f)];
            }
            if (++totalWindows >= silenceWindows)
            {
                noiseFloor = percentile(m.noise, 0, 0.5f);
                totalWindows = 0;
                phase.store((int)Phase::Voice, std::memory_order_release);
            }
            progress.store((float)totalWindows / (float)silenceWindows, std::memory_order_relaxed);
        }
        else
        {
            ++m.voice[(size_t)binOf(db)];
            if (db > noiseFloor + 12.0f && highSum > 0.5 * bodySum && highSum > 1.0e-12)
            {
                ++m.sibilantWindows;
                for (int k = 0; k < kCandidates; ++k)
                    m.sibilance[(size_t)k] += bandSum[(size_t)k] / highSum;
            }
            if (++totalWindows >= voiceWindows)
            {
                m.peakDb = peakAbs > 1.0e-6f ? 20.0f * std::log10(peakAbs) : -120.0f;
                progress.store(1.0f, std::memory_order_relaxed);
                phase.store((int)Phase::Done, std::memory_order_release);
                clearWindow();
                return;
            }
            progress.store((float)totalWindows / (float)voiceWindows, std::memory_order_relaxed);
        }
        clearWindow();
    }

    std::atomic<int> command{0}, phase{(int)Phase::Idle};
    std::atomic<float> progress{0.0f};

    double sr = 48000.0;
    Measurements m;
    dsp::Biquad body, high, bands[kCandidates];
    int windowLength = 2400, windowCount = 0, totalWindows = 0;
    double windowSum = 0.0, bodySum = 0.0, highSum = 0.0;
    std::array<double, kCandidates> bandSum{};
    float noiseFloor = -120.0f, peakAbs = 0.0f;
    float envAttack = 0.0f, envRelease = 0.0f, gateEnvelope = 0.0f, windowEnvelopeMax = 0.0f;
};

} // namespace stagemic
