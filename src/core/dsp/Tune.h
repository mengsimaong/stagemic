// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/dsp/Module.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace stagemic::dsp
{

class Tune final : public Module
{
public:
    explicit Tune(juce::AudioProcessorValueTreeState&);

    void prepare(double sampleRate, int numChannels) override;
    void reset() noexcept override;
    void update(bool snap) noexcept override;
    void process(float* const* channels, int numChannels, int numSamples) noexcept override;

    static constexpr double latencyMs = 20.0;
    int getLatencySamples() const noexcept { return delay; }

    // Scales as pitch-class masks
    static constexpr std::array<uint16_t, 8> scaleMasks{
        0xFFF,                                                                       // Chromatic
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11), // Major
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10), // Minor
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11), // Harmonic minor
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                        // Major pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                       // Minor pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),            // Blues
        (1 << 0),                                                                    // Robot
    };
    // Nearest note of the scale to a pitch
    static int nearestNote(float midi, int key, uint16_t mask) noexcept;

    // Pitch history for the window's graph
    static constexpr int kHistory = 1024;
    struct History
    {
        std::array<std::atomic<float>, kHistory> input{}, output{};
        std::atomic<int> written{0}; // total points written
    };
    const History& getHistory() const noexcept { return history; }

private:
    struct Mark
    {
        int64_t pos = 0;
        double exact = 0.0; // sub-sample position
        float period = 0.0f;
        bool voiced = false;
    };

    float monoAt(int64_t t) const noexcept { return mono[(size_t)(t & mask)]; }
    float inputAt(int c, double t) const noexcept;
    void detect() noexcept;
    float periodAt(int64_t t, bool& voiced) const noexcept;
    void addMarks() noexcept;
    int markFor(double target) const noexcept;
    void addGrain(double centre, double source, int left, int right, float readScale) noexcept;
    void emitGrain() noexcept;

    std::atomic<float>*keyParam, *scaleParam, *speedParam, *amountParam, *formantParam, *transposeParam, *voiceParam;

    double sr = 48000.0;
    int numCh = 2;
    int delay = 960; // samples
    int maxPeriod = 600, minPeriod = 48;

    static constexpr int kRing = 8192;
    static constexpr int64_t mask = kRing - 1;
    std::vector<float> in[kMaxChannels], out[kMaxChannels], weight, mono, lowMono;
    int64_t now = 0; // samples written so far

    // Detection
    int decimation = 4, decimPhase = 0, hopCounter = 0;
    Biquad aa1, aa2;        // anti-alias before downsampling
    std::vector<float> dec; // downsampled ring
    int64_t decWritten = 0;
    static constexpr int kDecRing = 1024, kWindow = 192, kHop = 32;
    int decTauMin = 12, decTauMax = 150;
    std::vector<float> yin; // scratch, size decTauMax + 2

    static constexpr int kTrack = 256;
    struct TrackPoint
    {
        int64_t time = -1;
        float period = 0.0f;
        bool voiced = false;
    };
    std::array<TrackPoint, kTrack> track{};
    float confirmed = 0.0f, pending = 0.0f; // jump confirmation
    int misses = 0;                         // unclear readings held inside a note
    int trackWritten = 0;

    // Analysis marks
    static constexpr int kMarks = 128;
    std::array<Mark, kMarks> marks{};
    int marksWritten = 0;
    int64_t lastMarkPos = 0;

    // Synthesis
    double nextGrain = 0.0;  // position of the next output grain
    double readOffset = 0.0; // grain read position minus write position
    bool lastGrainVoiced = false;

    // Correction state
    int key = 0, scale = 0;
    double voiceMinHz = 100.0, voiceMaxHz = 800.0;
    float speedMs = 20.0f, amount = 1.0f, formantRatio = 1.0f, transpose = 0.0f;
    float correction = 0.0f; // semitones currently applied
    int currentNote = -1;
    float lastInputMidi = -1.0f, lastOutputMidi = -1.0f;
    int historyCounter = 0;
    History history;
};

} // namespace stagemic::dsp
