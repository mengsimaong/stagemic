// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 4: Tune (real-time pitch correction).
#include "AllocationGuard.h"
#include "TestHelpers.h"
#include "core/dsp/Tune.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cmath>
#include <functional>

namespace stagemic::test
{

namespace
{
constexpr double kRate = 48000.0;

double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

/** A voice-like tone: 24 harmonics under a gentle vowel-ish envelope, with a
    pitch that may change over time (midiAt gives the pitch at time t). */
juce::AudioBuffer<float> voice(std::function<double(double)> midiAt, double seconds, float level = 0.2f)
{
    juce::AudioBuffer<float> b(2, (int)(kRate * seconds));
    double phase = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double f0 = midiToHz(midiAt(i / kRate));
        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
        {
            const double f = f0 * h;
            if (f > 20000.0)
                break;
            // Formant-like bumps around 700 Hz and 2.5 kHz on a falling slope.
            const double env = 1.0 / h * (1.0 + 2.0 * std::exp(-std::pow((f - 700.0) / 300.0, 2.0))
                                          + 1.0 * std::exp(-std::pow((f - 2500.0) / 600.0, 2.0)));
            v += env * std::sin(phase * h);
        }
        phase += 2.0 * juce::MathConstants<double>::pi * f0 / kRate;
        if (phase > 2.0 * juce::MathConstants<double>::pi * 1000.0)
            phase -= 2.0 * juce::MathConstants<double>::pi * 1000.0;
        b.setSample(0, i, level * (float)v);
        b.setSample(1, i, level * (float)v);
    }
    return b;
}

/** Independent pitch measurement (plain full-rate YIN) of one frame. Returns MIDI, or -1. */
double measureMidi(const juce::AudioBuffer<float>& b, int start, int length = 2048)
{
    const int maxTau = (int)(kRate / 70.0), minTau = (int)(kRate / 1200.0);
    if (start + length + maxTau > b.getNumSamples())
        return -1.0;
    const float* x = b.getReadPointer(0, start);
    std::vector<double> d((size_t)maxTau + 2, 0.0);
    double running = 0.0;
    d[0] = 1.0;
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        double diff = 0.0;
        for (int j = 0; j < length; ++j)
        {
            const double e = (double)x[j] - (double)x[j + tau];
            diff += e * e;
        }
        running += diff;
        d[(size_t)tau] = running > 0.0 ? diff * tau / running : 1.0;
    }
    int best = -1;
    for (int tau = minTau; tau < maxTau; ++tau)
        if (d[(size_t)tau] < 0.12)
        {
            while (tau + 1 < maxTau && d[(size_t)tau + 1] < d[(size_t)tau])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
        return -1.0;
    const double y0 = d[(size_t)best - 1], y1 = d[(size_t)best], y2 = d[(size_t)best + 1];
    const double denom = y0 - 2.0 * y1 + y2;
    const double tau = best + (std::abs(denom) > 1e-12 ? 0.5 * (y0 - y2) / denom : 0.0);
    return hzToMidi(kRate / tau);
}

/** Median pitch (MIDI) over [from, to) seconds. */
double medianMidi(const juce::AudioBuffer<float>& b, double from, double to)
{
    std::vector<double> v;
    for (int s = (int)(from * kRate); s < (int)(to * kRate); s += 480)
        if (const double m = measureMidi(b, s); m > 0.0)
            v.push_back(m);
    if (v.empty())
        return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::unique_ptr<StageMicProcessor> tuneProcessor(float speedMs, int scale = 0, int key = 0)
{
    auto p = makeProcessor();
    onlyModule(*p, ModuleId::Tune);
    setParam(*p, pid::tuneSpeed, speedMs);
    setParam(*p, pid::tuneScale, (float)scale);
    setParam(*p, pid::tuneKey, (float)key);
    p->getChain().requestSnap();
    return p;
}
} // namespace

class TuneTests final : public juce::UnitTest
{
public:
    TuneTests()
        : juce::UnitTest("Tune (pitch correction)", "StageMic")
    {
    }

    void runTest() override
    {
        beginTest("Scales: nearest note");
        {
            using dsp::Tune;
            expectEquals(Tune::nearestNote(60.4f, 0, Tune::scaleMasks[0]), 60);
            expectEquals(Tune::nearestNote(60.6f, 0, Tune::scaleMasks[0]), 61);
            expectEquals(Tune::nearestNote(61.3f, 0, Tune::scaleMasks[1]), 62, "C# in C major goes to D when closer");
            expectEquals(Tune::nearestNote(61.6f, 9, Tune::scaleMasks[2]), 62, "A minor");
            expectEquals(Tune::nearestNote(66.0f, 0, Tune::scaleMasks[4]), 67, "C major pentatonic has no F");
        }

        beginTest("A sharp note is pulled exactly in tune, across the vocal range");
        for (double note : {45.0, 52.0, 57.0, 64.0, 69.0, 76.0, 81.0})
        {
            auto p = tuneProcessor(0.0f);
            auto b = voice([note](double) { return note + 0.35; }, 1.5);
            const double before = medianMidi(b, 0.5, 1.4);
            processSignal(*p, b);
            const double after = medianMidi(b, 0.5, 1.4);
            logMessage("note " + juce::String(note) + ": in " + juce::String((before - note) * 100.0, 1) + " cents, out "
                       + juce::String((after - note) * 100.0, 1) + " cents");
            expectWithinAbsoluteError(after, note, 0.05, "within 5 cents");
        }

        beginTest("Note jumps and phrase starts never show a wrong note");
        {
            auto p = tuneProcessor(12.0f, 1, 4); // E major, "Pop"
            // Sung: G#4 (a bit sharp), then a sudden jump up to B4, a breath, then E4.
            auto b = voice([](double t) { return t < 0.6 ? 68.2 : (t < 1.2 ? 71.25 : 64.3); }, 1.8);
            for (int i = (int)(1.12 * kRate); i < (int)(1.2 * kRate); ++i)
            {
                b.setSample(0, i, 0.0f);
                b.setSample(1, i, 0.0f);
            }
            const auto& h = p->getChain().getTune().getHistory();
            juce::MidiBuffer midi;
            float lowest = 1000.0f, highest = 0.0f;
            int seen = 0;
            for (int start = 0; start + 480 <= b.getNumSamples(); start += 480)
            {
                juce::AudioBuffer<float> blk(b.getArrayOfWritePointers(), 2, start, 480);
                p->processBlock(blk, midi);
                for (; seen < h.written.load(); ++seen)
                    if (const float out = h.output[(size_t)(seen % dsp::Tune::kHistory)].load(); out > 0.0f)
                    {
                        lowest = std::min(lowest, out);
                        highest = std::max(highest, out);
                    }
            }
            logMessage("corrected pitch stayed within " + juce::String(lowest, 2) + " .. " + juce::String(highest, 2));
            expectGreaterThan(lowest, 63.9f, "nothing below E4");
            expectLessThan(highest, 71.6f, "nothing above B4");
        }

        beginTest("A rough, real-voice-like signal passes through cleanly (no crackle)");
        {
            // Two similar peaks per cycle (so "the biggest peak" keeps changing), cycle-to-cycle
            // jitter and shimmer, vibrato and breath noise: what real voices are like and
            // clean test tones are not. (Found on a real recording: grains then overlapped
            // out of step, partly cancelled and crackled.)
            juce::Random rng(11);
            const int n = (int)(kRate * 2.0);
            juce::AudioBuffer<float> rough(2, n);
            double phase = 0.0, jitter = 0.0, shimmer = 1.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / kRate;
                if (phase >= 2.0 * juce::MathConstants<double>::pi)
                {
                    phase -= 2.0 * juce::MathConstants<double>::pi;
                    jitter = (rng.nextDouble() - 0.5) * 0.02;  // +-1% period jitter per cycle
                    shimmer = 1.0 + (rng.nextDouble() - 0.5) * 0.1;
                }
                const double f0 = 392.0 * (1.0 + jitter) * std::pow(2.0, (0.3 + 0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / 12.0);
                double v = std::sin(phase) + 0.95 * std::sin(2.0 * phase + 1.2) + 0.4 * std::sin(3.0 * phase + 0.4);
                for (int h = 4; h <= 20; ++h)
                    v += std::sin(h * phase + h) / (h * 1.5);
                v = 0.12 * shimmer * v + 0.004 * (rng.nextDouble() * 2.0 - 1.0);
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / kRate;
                rough.setSample(0, i, (float)v);
                rough.setSample(1, i, (float)v);
            }
            juce::AudioBuffer<float> original;
            original.makeCopyOf(rough);

            // With no correction it must come out the same (only delayed).
            auto p = tuneProcessor(0.0f);
            setParam(*p, pid::tuneAmount, 0.0f);
            auto passed = rough;
            processSignal(*p, passed);
            const int lag = (int)std::lround(dsp::Tune::latencyMs * 0.001 * kRate);
            std::vector<double> frameSnr;
            for (int s = (int)(0.5 * kRate); s + 480 < n - lag; s += 480)
            {
                double err = 0.0, ref = 0.0;
                for (int i = s; i < s + 480; ++i)
                {
                    const double e = passed.getSample(0, i + lag) - original.getSample(0, i);
                    err += e * e;
                    ref += (double)original.getSample(0, i) * original.getSample(0, i);
                }
                frameSnr.push_back(10.0 * std::log10(ref / std::max(err, 1e-20)));
            }
            std::sort(frameSnr.begin(), frameSnr.end());
            const double worstTenth = frameSnr[frameSnr.size() / 10];
            logMessage("rough voice, no correction: 10% worst moments match the input to " + juce::String(worstTenth, 1) + " dB");
            expectGreaterThan(worstTenth, 40.0, "passes through cleanly");

            // With hard correction, the level must stay steady (no partial cancellation).
            auto q = tuneProcessor(0.0f);
            auto tuned = rough;
            processSignal(*q, tuned);
            double lo = 1e9, hi = -1e9;
            for (int s = (int)(0.5 * kRate); s + 960 < n; s += 960)
            {
                const double d = rmsDb(tuned, s + lag, 960) - rmsDb(original, s, 960);
                lo = std::min(lo, d);
                hi = std::max(hi, d);
            }
            logMessage("rough voice, hard tune: level vs input stays within " + juce::String(lo, 2) + " .. " + juce::String(hi, 2) + " dB");
            expectGreaterThan(lo, -1.0, "no dips");
            expectLessThan(hi, 1.0, "no bumps");
            expectWithinAbsoluteError(medianMidi(tuned, 0.5, 1.9), 67.0, 0.1, "and in tune (G4)");
        }

        beginTest("Voice = High: vocal fry (every other cycle stronger) doesn't read an octave low");
        {
            // F#4 sung with fry-like period doubling: alternate cycles 60% louder. The
            // waveform then truly repeats only every two cycles (F#3).
            juce::AudioBuffer<float> fry(2, (int)(kRate * 1.5));
            double phase = 0.0;
            const double f0 = midiToHz(66.3);
            for (int i = 0; i < fry.getNumSamples(); ++i)
            {
                double v = 0.0;
                for (int h = 1; h <= 16; ++h)
                    v += std::sin(h * phase) / h;
                const bool strongCycle = std::fmod(phase, 4.0 * juce::MathConstants<double>::pi) < 2.0 * juce::MathConstants<double>::pi;
                v *= strongCycle ? 0.16 : 0.10;
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / kRate;
                fry.setSample(0, i, (float)v);
                fry.setSample(1, i, (float)v);
            }
            auto p = tuneProcessor(0.0f);
            setParam(*p, pid::tuneVoice, 0.0f); // High
            processSignal(*p, fry);
            const auto& h = p->getChain().getTune().getHistory();
            int low = 0, total = 0;
            for (int k = 0; k < h.written.load(); ++k)
                if (const float in = h.input[(size_t)(k % dsp::Tune::kHistory)].load(); in > 0.0f)
                {
                    ++total;
                    low += in < 60.0f ? 1 : 0;
                }
            logMessage("fry: " + juce::String(low) + " of " + juce::String(total) + " readings an octave low");
            expectGreaterThan(total, 100);
            expectEquals(low, 0, "never reads the octave below");
        }

        beginTest("Key and scale: off-scale notes go to the nearest note in the key");
        {
            auto p = tuneProcessor(0.0f, 1, 0); // C major
            auto b = voice([](double) { return 66.3; }, 1.5); // F# + 30 cents -> G
            processSignal(*p, b);
            expectWithinAbsoluteError(medianMidi(b, 0.5, 1.4), 67.0, 0.05);
        }

        beginTest("Pitch: transposes by whole semitones");
        {
            auto p = tuneProcessor(0.0f);
            setParam(*p, pid::tuneTranspose, 7.0f);
            auto b = voice([](double) { return 57.0; }, 1.5);
            processSignal(*p, b);
            expectWithinAbsoluteError(medianMidi(b, 0.5, 1.4), 64.0, 0.06);
        }

        beginTest("Speed: 0 ms snaps at once and flattens vibrato; slow speed keeps vibrato");
        {
            auto vibrato = [](double t) { return 69.0 + 0.4 * std::sin(2.0 * juce::MathConstants<double>::pi * 5.5 * t); };
            auto spread = [&](float speed) {
                auto p = tuneProcessor(speed);
                auto b = voice(vibrato, 2.0);
                processSignal(*p, b);
                double lo = 1e9, hi = -1e9;
                for (int s = (int)(0.6 * kRate); s < (int)(1.8 * kRate); s += 240)
                    if (const double m = measureMidi(b, s, 1024); m > 0.0)
                    {
                        lo = std::min(lo, m);
                        hi = std::max(hi, m);
                    }
                return hi - lo;
            };
            const double hard = spread(0.0f), natural = spread(300.0f);
            logMessage("vibrato (input 80 cents wide): hard tune " + juce::String(hard * 100.0, 1) + " cents, slow "
                       + juce::String(natural * 100.0, 1) + " cents");
            expectLessThan(hard, 0.25, "hard tune flattens vibrato");
            expectGreaterThan(natural, 0.5, "slow speed keeps vibrato");
        }

        beginTest("Speed: a note change glides at the set speed");
        {
            auto glideTime = [&](float speed) {
                auto p = tuneProcessor(speed);
                // Sung 30 cents flat; the correction takes time to arrive.
                auto b = voice([](double) { return 68.7; }, 1.5);
                processSignal(*p, b);
                for (double t = 0.1; t < 1.4; t += 0.01)
                    if (const double m = measureMidi(b, (int)(t * kRate), 1024); m > 68.95)
                        return t;
                return 9.0;
            };
            const double fast = glideTime(0.0f), slow = glideTime(150.0f);
            logMessage("in tune after: 0 ms speed " + juce::String(fast, 2) + " s, 150 ms speed " + juce::String(slow, 2) + " s");
            expectLessThan(fast, 0.15);
            expectGreaterThan(slow, fast + 0.1);
            expectLessThan(slow, 1.0);
        }

        beginTest("Amount: 50% corrects half way");
        {
            auto p = tuneProcessor(0.0f);
            setParam(*p, pid::tuneAmount, 50.0f);
            auto b = voice([](double) { return 69.4; }, 1.5);
            processSignal(*p, b);
            expectWithinAbsoluteError(medianMidi(b, 0.5, 1.4), 69.2, 0.05);
        }

        beginTest("Formant: changes the voice's character, not its pitch");
        {
            auto centroid = [](const juce::AudioBuffer<float>& b) {
                // Crude spectral balance: high-passed energy over total energy.
                dsp::Biquad hp;
                hp.c = dsp::biquad::highpass(kRate, 1500.0, 0.707);
                double hi = 0.0, all = 0.0;
                for (int i = (int)(0.5 * kRate); i < b.getNumSamples(); ++i)
                {
                    const float x = b.getSample(0, i), y = hp.process(x, 0);
                    hi += (double)y * y;
                    all += (double)x * x;
                }
                return 10.0 * std::log10(hi / all);
            };
            auto run = [&](float formant) {
                auto p = tuneProcessor(0.0f);
                setParam(*p, pid::tuneFormant, formant);
                auto b = voice([](double) { return 60.0; }, 1.5);
                processSignal(*p, b);
                return std::make_pair(medianMidi(b, 0.5, 1.4), centroid(b));
            };
            const auto normal = run(0.0f), up = run(4.0f), down = run(-4.0f);
            logMessage("brightness (dB above 1.5 kHz): formant -4 " + juce::String(down.second, 1) + ", 0 "
                       + juce::String(normal.second, 1) + ", +4 " + juce::String(up.second, 1));
            expectWithinAbsoluteError(up.first, 60.0, 0.06, "pitch unchanged (formant up)");
            expectWithinAbsoluteError(down.first, 60.0, 0.06, "pitch unchanged (formant down)");
            expectGreaterThan(up.second, normal.second + 1.0, "formant up sounds smaller/brighter");
            expectLessThan(down.second, normal.second - 1.0, "formant down sounds bigger/darker");
        }

        beginTest("Formant (fun voices): breaths and \"s\" sounds get the same shift as the voice");
        {
            // A hiss (an "s"): brightness measured as the energy-weighted centre frequency.
            auto centroid = [](const juce::AudioBuffer<float>& b, int from, int count) {
                juce::dsp::FFT fft(12);
                const int size = 1 << 12;
                double num = 0.0, den = 0.0;
                std::vector<float> data((size_t)size * 2);
                for (int start = from; start + size <= from + count; start += size)
                {
                    std::fill(data.begin(), data.end(), 0.0f);
                    for (int i = 0; i < size; ++i)
                        data[(size_t)i] = b.getSample(0, start + i) * (0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)size));
                    fft.performFrequencyOnlyForwardTransform(data.data());
                    // Within 200 Hz .. 12 kHz, so content near the top of the range
                    // (which the shift can push out) doesn't skew it.
                    for (int k = 1; k < size / 2; ++k)
                    {
                        const double f = (double)k * kRate / size;
                        if (f < 200.0 || f > 12000.0)
                            continue;
                        num += std::log(f) * (double)data[(size_t)k] * data[(size_t)k];
                        den += (double)data[(size_t)k] * data[(size_t)k];
                    }
                }
                return std::exp(num / den); // energy-weighted centre on a log scale
            };
            auto hiss = [] {
                juce::AudioBuffer<float> b(2, (int)kRate * 2);
                juce::Random rng(17);
                dsp::Biquad band;
                band.c = dsp::biquad::bandPass(kRate, 3000.0, 0.7);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = band.process(0.2f * (rng.nextFloat() * 2.0f - 1.0f), 0);
                    b.setSample(0, i, v);
                    b.setSample(1, i, v);
                }
                return b;
            };
            const auto dry = hiss();
            const double dryCentre = centroid(dry, (int)kRate / 2, (int)kRate);
            for (float formant : {4.0f, -5.0f})
            {
                auto p = tuneProcessor(20.0f);
                setParam(*p, pid::tuneFormant, formant);
                p->getChain().requestSnap();
                auto b = hiss();
                processSignal(*p, b);
                const double ratio = centroid(b, (int)kRate / 2, (int)kRate) / dryCentre;
                const double expected = std::pow(2.0, formant / 12.0);
                logMessage("formant " + juce::String(formant, 0) + " st: hiss brightness x" + juce::String(ratio, 2) + " (voice x"
                           + juce::String(expected, 2) + ")");
                expectWithinAbsoluteError(ratio, expected, 0.12, "the hiss moves with the voice");
                const float level = juce::Decibels::gainToDecibels(b.getRMSLevel(0, (int)kRate / 2, (int)kRate) / dry.getRMSLevel(0, (int)kRate / 2, (int)kRate));
                expectWithinAbsoluteError(level, 0.0f, 3.0f, "and keeps its level");
                expect(allFinite(b));
            }
        }

        beginTest("Level is kept, and breaths/noise pass through untouched (just delayed)");
        {
            auto p = tuneProcessor(0.0f);
            auto b = voice([](double) { return 64.3; }, 1.5);
            const float inDb = rmsDb(b, 24000, 48000);
            processSignal(*p, b);
            expectWithinAbsoluteError(rmsDb(b, 24000, 48000), inDb, 1.0f, "sung level");

            auto q = tuneProcessor(0.0f);
            juce::AudioBuffer<float> noise(2, 48000), copy(2, 48000);
            juce::Random rng(3);
            for (int i = 0; i < 48000; ++i)
            {
                const float v = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
                noise.setSample(0, i, v);
                noise.setSample(1, i, v);
            }
            copy.makeCopyOf(noise);
            processSignal(*q, noise);
            const int lag = (int)std::lround(dsp::Tune::latencyMs * 0.001 * kRate);
            double err = 0.0, ref = 0.0;
            for (int i = 24000; i < 47000; ++i)
            {
                const double e = noise.getSample(0, i) - copy.getSample(0, i - lag);
                err += e * e;
                ref += (double)copy.getSample(0, i - lag) * copy.getSample(0, i - lag);
            }
            const double snr = 10.0 * std::log10(ref / std::max(err, 1e-20));
            logMessage("noise passes through: " + juce::String(snr, 1) + " dB match with the input delayed by "
                       + juce::String(dsp::Tune::latencyMs) + " ms");
            expectGreaterThan(snr, 40.0);
        }

        beginTest("Off means off: no latency and bit-exact when Tune is switched off");
        {
            auto p = makeProcessor();
            allModulesOff(*p);
            auto b = voice([](double) { return 64.3; }, 0.5);
            juce::AudioBuffer<float> copy;
            copy.makeCopyOf(b);
            processSignal(*p, b);
            float diff = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i)
                diff = std::max(diff, std::abs(b.getSample(0, i) - copy.getSample(0, i)));
            expectEquals(diff, 0.0f);
        }

        beginTest("Tune never allocates on the audio thread, and is light on CPU");
        {
            auto p = tuneProcessor(20.0f);
            auto b = voice([](double t) { return 64.0 + 3.0 * std::sin(t); }, 4.0);
            juce::AudioBuffer<float> warmup(2, 480);
            juce::MidiBuffer midi;
            p->processBlock(warmup, midi);
            std::size_t allocations = 0;
            {
                AllocationGuard guard;
                processSignal(*p, b);
                allocations = guard.count();
            }
            expectEquals((int)allocations, 0, "allocations");
            // Best of three runs (shared CI machines are noisy).
            auto q = tuneProcessor(20.0f);
            auto c = voice([](double t) { return 64.0 + 3.0 * std::sin(t); }, 10.0);
            double seconds = 1.0e9;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                c = voice([](double t) { return 64.0 + 3.0 * std::sin(t); }, 10.0);
                const auto start = std::chrono::steady_clock::now();
                processSignal(*q, c);
                seconds = std::min(seconds, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            }
            logMessage("Tune CPU: " + juce::String(100.0 * seconds / 10.0, 2) + "% of one core");
            expectLessThan(100.0 * seconds / 10.0, 5.0);
            expect(allFinite(c));
        }
    }
};

static TuneTests tuneTests;

} // namespace stagemic::test
