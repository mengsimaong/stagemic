// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/dsp/Module.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace stagemic::dsp
{

/** Real-time pitch correction ("Tune").

    1. Pitch detection: YIN on a 4x-downsampled copy of the voice (cheap), then
       refined at full rate. 80 Hz .. 1 kHz.
    2. Correction: the sung pitch is pulled toward the nearest note of the chosen
       key/scale. Speed = how fast (0 ms = instant, the hard "robot" stepping;
       slower lets vibrato and slides through). Amount scales the correction.
    3. Pitch shifting: PSOLA. Each vocal cycle is cut out as a small grain and the
       grains are laid down at the new spacing, which keeps the voice's own
       character (formants). Formant moves that character on its own by
       resampling each grain. Breaths and "s" sounds (unvoiced) pass through.

    Latency: 20 ms, only while Tune is on (it must hear a cycle or two first). */
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

    /** Scales as pitch-class masks (bit n = n semitones above the key). */
    static constexpr std::array<uint16_t, 8> scaleMasks{
        0xFFF,                                                  // Chromatic
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11), // Major
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10), // Minor
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11), // Harmonic minor
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                        // Major pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                       // Minor pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),            // Blues
        (1 << 0),                                                                     // Robot: one note
    };
    /** Nearest note (MIDI number) of the scale to a pitch. */
    static int nearestNote(float midi, int key, uint16_t mask) noexcept;

    /** Pitch history for the window's graph: written by the audio thread
        about 190 times a second, read by the window. -1 = no pitch (silence,
        breath, "s"). */
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
        double exact = 0.0;   // sub-sample position (pos rounded = pos)
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
    int delay = 960;           // samples
    int maxPeriod = 600, minPeriod = 48;

    // Rings (power-of-two sizes; allocated in prepare).
    static constexpr int kRing = 8192;
    static constexpr int64_t mask = kRing - 1;
    std::vector<float> in[kMaxChannels], out[kMaxChannels], weight, mono, lowMono;
    int64_t now = 0;           // samples written so far

    // Detection (on a downsampled copy).
    int decimation = 4, decimPhase = 0, hopCounter = 0;
    Biquad aa1, aa2;           // anti-alias before downsampling
    std::vector<float> dec;    // downsampled ring
    int64_t decWritten = 0;
    static constexpr int kDecRing = 1024, kWindow = 192, kHop = 32;
    int decTauMin = 12, decTauMax = 150;
    std::vector<float> yin;    // scratch, size decTauMax + 2

    static constexpr int kTrack = 256;
    struct TrackPoint { int64_t time = -1; float period = 0.0f; bool voiced = false; };
    std::array<TrackPoint, kTrack> track{};
    float confirmed = 0.0f, pending = 0.0f; // jump confirmation (periods; 0 = none)
    int misses = 0;                         // unclear readings held inside a note
    int trackWritten = 0;

    // Analysis marks (one per vocal cycle; a fixed 5 ms grid when unvoiced).
    static constexpr int kMarks = 128;
    std::array<Mark, kMarks> marks{};
    int marksWritten = 0;
    int64_t lastMarkPos = 0;

    // Synthesis.
    double nextGrain = 0.0;    // position (input time) of the next output grain
    double readOffset = 0.0;   // where grains read from, relative to where they're placed
    bool lastGrainVoiced = false;

    // Correction state.
    int key = 0, scale = 0;
    double voiceMinHz = 100.0, voiceMaxHz = 800.0;
    float speedMs = 20.0f, amount = 1.0f, formantRatio = 1.0f, transpose = 0.0f;
    float correction = 0.0f;   // semitones currently applied (before transpose)
    int currentNote = -1;
    float lastInputMidi = -1.0f, lastOutputMidi = -1.0f;
    int historyCounter = 0;
    History history;
};

} // namespace stagemic::dsp
