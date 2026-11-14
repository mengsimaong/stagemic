// SPDX-License-Identifier: AGPL-3.0-or-later
//
// stagemic_bench: stress test of the audio engine, the way OBS drives it.
//
//   stagemic_bench <voice recording> [seconds per run]
//
// 1. Cost: every factory preset plus "everything on", at 48 and 44.1 kHz,
//    stereo, in OBS-sized blocks. Prints average CPU, the slowest 0.1% of
//    blocks and the single slowest block, against OBS's deadline per block.
// 2. Abuse: very hot input, knob storms, preset hopping, broken input (NaN),
//    left-channel-only mics, odd block sizes, long silence after loud sound.
//    Each must give finite output that never goes past 0 dBFS (the limiter).

#include "core/FilterInstance.h"
#include "core/StageMicProcessor.h"
#include "core/presets/PresetManager.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>
#include <cstdio>
#include <random>

using namespace stagemic;

namespace
{
using Clock = std::chrono::steady_clock;

const char* allOn[] = {"gate_on", "lev_on", "eq_on", "ds_on", "comp_on", "mb_on", "sat_on",
                       "ex_on",   "ch_on",  "dly_on", "rev_on", "lim_on", "tune_on"};

void set(StageMicProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getParameters().getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

juce::RangedAudioParameter* randomParam(StageMicProcessor& p, std::mt19937& rng)
{
    auto& all = p.getParameters().processor.getParameters();
    std::uniform_int_distribution<int> pick(0, all.size() - 1);
    return dynamic_cast<juce::RangedAudioParameter*>(all[pick(rng)]);
}

struct Result
{
    double meanPct = 0, p999Ms = 0, maxMs = 0;
    float peak = 0;
    int64_t nonFinite = 0;
    float deadlineMs = 0;
};

/** Runs `audio` (stereo) through a fresh filter, like OBS: `block` frames per call. */
template <typename PerBlock>
Result run(const juce::AudioBuffer<float>& source, double sr, int seconds, const juce::String& preset, bool everything,
           int block, int channels, PerBlock&& perBlock, float inputGainDb = 0.0f)
{
    FilterInstance::Config cfg;
    cfg.sampleRate = sr;
    cfg.numHostChannels = channels;
    cfg.maxBlockSize = 1024;
    FilterInstance inst(cfg);
    auto& p = inst.getProcessor();
    if (preset.isNotEmpty())
        p.getPresetManager().loadFactoryPreset(preset);
    if (everything)
        for (auto* id : allOn)
            set(p, id, 1.0f);
    if (inputGainDb != 0.0f)
        set(p, "in_gain", inputGainDb);
    p.getChain().requestSnap();

    const int total = (int)(seconds * sr);
    std::vector<float> buf[8];
    for (auto& b : buf)
        b.resize((size_t)block);
    float* ptrs[8];
    for (int c = 0; c < 8; ++c)
        ptrs[c] = buf[c].data();

    std::vector<double> times;
    times.reserve((size_t)(total / block + 1));
    Result r;
    r.deadlineMs = (float)(1000.0 * block / sr);
    double busy = 0;
    int pos = 0, blockIndex = 0;
    while (pos < total)
    {
        const int n = std::min(block, total - pos);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                buf[c][(size_t)i] = source.getSample(std::min(c, source.getNumChannels() - 1), (pos + i) % source.getNumSamples());
        perBlock(p, buf, channels, n, blockIndex, pos);

        const auto t0 = Clock::now();
        inst.process(ptrs, channels, n);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        times.push_back(ms);
        busy += ms;

        for (int c = 0; c < std::min(channels, 2); ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf[c][(size_t)i];
                if (!std::isfinite(v))
                    ++r.nonFinite;
                else
                    r.peak = std::max(r.peak, std::abs(v));
            }
        pos += n;
        ++blockIndex;
    }
    r.meanPct = 100.0 * busy / (1000.0 * total / sr);
    std::sort(times.begin(), times.end());
    r.p999Ms = times[std::min(times.size() - 1, (size_t)(times.size() * 0.999))];
    r.maxMs = times.back();
    return r;
}

auto none = [](StageMicProcessor&, std::vector<float>*, int, int, int, int) {};

void printCost(const char* name, const Result& r)
{
    std::printf("  %-26s %5.2f%% cpu   slowest 0.1%% %5.2f ms   worst %5.2f ms  (deadline %4.1f)\n", name, r.meanPct, r.p999Ms,
                r.maxMs, r.deadlineMs);
}

bool check(const char* name, const Result& r, float maxPeak = 1.0f)
{
    const bool ok = r.nonFinite == 0 && r.peak <= maxPeak;
    std::printf("  %-44s %s  peak %+6.1f dBFS  non-finite %lld   worst block %5.2f ms\n", name, ok ? "ok  " : "FAIL",
                juce::Decibels::gainToDecibels(r.peak, -120.0f), (long long)r.nonFinite, r.maxMs);
    return ok;
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2)
    {
        std::printf("usage: stagemic_bench <voice recording> [seconds per run]\n");
        return 1;
    }
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 30;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])));
    if (reader == nullptr)
    {
        std::printf("cannot read %s\n", argv[1]);
        return 1;
    }
    juce::AudioBuffer<float> voice(2, (int)reader->lengthInSamples);
    reader->read(&voice, 0, voice.getNumSamples(), 0, true, true);

    // "cost" mode: a compact table for comparing two builds (best of 5 runs each, so a
    // busy machine can't make one look slower): each module alone, then the presets.
    if (argc > 3 && juce::String(argv[3]) == "cost")
    {
        auto best = [&](const juce::String& preset, bool everything, auto&& perBlock) {
            double pct = 1.0e9;
            for (int attempt = 0; attempt < 5; ++attempt)
                pct = std::min(pct, run(voice, 48000.0, seconds, preset, everything, 1024, 2, perBlock).meanPct);
            return pct;
        };
        std::printf("module alone (others off), %% of one core, 48 kHz stereo:\n");
        std::printf("  %-22s %6.3f\n", "nothing on", best({}, false, [](StageMicProcessor& p, std::vector<float>*, int, int, int b, int) {
                        if (b == 0)
                        {
                            for (auto* id : allOn)
                                set(p, id, 0.0f);
                            p.getChain().requestSnap();
                        }
                    }));
        for (auto* module : allOn)
            std::printf("  %-22s %6.3f\n", module, best({}, false, [module](StageMicProcessor& p, std::vector<float>*, int, int, int b, int) {
                            if (b == 0)
                            {
                                for (auto* id : allOn)
                                    set(p, id, id == module ? 1.0f : 0.0f);
                                p.getChain().requestSnap();
                            }
                        }));
        std::printf("presets:\n");
        for (const auto& name : PresetManager::getFactoryPresetNames())
            std::printf("  %-22s %6.3f\n", name.toRawUTF8(), best(name, false, none));
        std::printf("  %-22s %6.3f\n", "EVERYTHING ON", best("Pop Tune", true, none));
        return 0;
    }

    // --- 1. Cost ------------------------------------------------------------
    for (double sr : {48000.0, 44100.0})
    {
        std::printf("\nCOST at %.1f kHz, stereo, 1024-frame blocks (%d s of singing per run)\n", sr / 1000.0, seconds);
        printCost("Bypass-ish (all off)", run(voice, sr, seconds, {}, false, 1024, 2,
                                              [](StageMicProcessor& p, std::vector<float>*, int, int, int b, int) {
                                                  if (b == 0)
                                                      for (auto* id : allOn)
                                                          set(p, id, 0.0f);
                                              }));
        for (const auto& name : PresetManager::getFactoryPresetNames())
            printCost(name.toRawUTF8(), run(voice, sr, seconds, name, false, 1024, 2, none));
        printCost("EVERYTHING ON (+Tune)", run(voice, sr, seconds, "Pop Tune", true, 1024, 2, none));
    }

    // --- 2. Abuse -----------------------------------------------------------
    std::printf("\nABUSE (output must stay finite and at or under 0 dBFS)\n");
    bool ok = true;
    std::mt19937 rng(1234);

    ok &= check("Hot input (+24 dB input gain), everything on",
                run(voice, 48000, seconds, "Karaoke Star", true, 1024, 2, none, 24.0f));
    ok &= check("Full-scale square wave, everything on",
                run(voice, 48000, 10, "Pop Tune", true, 1024, 2, [](StageMicProcessor&, std::vector<float>* b, int ch, int n, int, int pos) {
                    for (int c = 0; c < ch; ++c)
                        for (int i = 0; i < n; ++i)
                            b[c][(size_t)i] = ((pos + i) / 50) % 2 ? 1.0f : -1.0f;
                }));
    ok &= check("Knob storm: 8 random settings changed every block",
                run(voice, 48000, seconds, "Karaoke Star", true, 1024, 2,
                    [&rng](StageMicProcessor& p, std::vector<float>*, int, int, int, int) {
                        std::uniform_real_distribution<float> u(0.0f, 1.0f);
                        for (int k = 0; k < 8; ++k)
                            if (auto* param = randomParam(p, rng))
                                param->setValueNotifyingHost(u(rng));
                    }));
    {
        const auto names = PresetManager::getFactoryPresetNames();
        ok &= check("Preset hopping every 100 ms",
                    run(voice, 48000, seconds, {}, false, 480, 2,
                        [&names](StageMicProcessor& p, std::vector<float>*, int, int, int b, int) {
                            if (b % 10 == 0)
                                p.getPresetManager().loadFactoryPreset(names[(b / 10) % names.size()]);
                        }));
    }
    {
        // A broken source or filter before us sends one block of NaN; afterwards the voice must come back.
        auto r = run(voice, 48000, 10, "Pop Tune", true, 1024, 2, [](StageMicProcessor&, std::vector<float>* b, int ch, int n, int blk, int) {
            if (blk == 20)
                for (int c = 0; c < ch; ++c)
                    for (int i = 0; i < n; ++i)
                        b[c][(size_t)i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        });
        std::printf("  %-44s %s  non-finite samples out: %lld (one bad block in = %d samples)\n", "NaN/inf input for one block",
                    r.nonFinite <= 2048 ? "ok  " : "FAIL", (long long)r.nonFinite, 2048);
        ok &= r.nonFinite <= 2048;
    }
    ok &= check("Mic on the left channel only (right silent)",
                run(voice, 48000, seconds, "Karaoke Star", false, 1024, 2, [](StageMicProcessor&, std::vector<float>* b, int, int n, int, int) {
                    std::fill(b[1].begin(), b[1].begin() + n, 0.0f);
                }));
    for (int block : {1, 7, 480, 1024, 4096})
    {
        char name[64];
        std::snprintf(name, sizeof name, "Block size %d frames, everything on", block);
        ok &= check(name, run(voice, 48000, block == 1 ? 3 : 10, "Pop Tune", true, block, 2, none));
    }
    ok &= check("Mono output setting (1 channel)", run(voice, 48000, 10, "Pop Tune", true, 1024, 1, none));
    ok &= check("5.1 output setting (6 channels)", run(voice, 48000, 10, "Pop Tune", true, 1024, 6, none));
    {
        // Loud, then 20 s of digital silence then near-silence: tails decay into tiny numbers (denormals) and must stay cheap.
        auto r = run(voice, 48000, 30, "Concert Hall", true, 1024, 2, [](StageMicProcessor&, std::vector<float>* b, int ch, int n, int, int pos) {
            if (pos > 48000 * 5)
                for (int c = 0; c < ch; ++c)
                    for (int i = 0; i < n; ++i)
                        b[c][(size_t)i] = pos > 48000 * 20 ? 1e-30f : 0.0f;
        });
        ok &= check("Loud then 25 s silence (denormal trap)", r);
        std::printf("      average %.2f%% cpu over the silence run\n", r.meanPct);
    }

    std::printf("\n%s\n", ok ? "ALL ABUSE CHECKS PASSED" : "SOME ABUSE CHECKS FAILED");
    return ok ? 0 : 2;
}
