// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 2
#include "AllocationGuard.h"
#include "TestHelpers.h"
#include "core/dsp/Safety.h"
#include "core/presets/PresetManager.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <limits>

namespace stagemic::test
{

class ChainTests final : public juce::UnitTest
{
public:
    ChainTests()
        : juce::UnitTest("Built-in chain and presets", "StageMic")
    {
    }

    void runTest() override
    {
        beginTest("Module table is complete and in order");
        {
            const auto& table = moduleDescriptors();
            for (int i = 0; i < kNumModules; ++i)
            {
                expect((int)table[(size_t)i].id == i, "entry " + juce::String(i) + " is in the right slot");
                expect(table[(size_t)i].key != nullptr && table[(size_t)i].enableParam != nullptr,
                       "entry " + juce::String(i) + " is filled in");
            }
        }

        testEffectsDoTheirJob();
        testSoundQualityRound();
        testReverb();
        testColor();
        testDeEsserAndOrder();
        testSafety();
        testZeroLatency();
        testModuleOrder();
        testTypedValues();
        testPresets();
        testCpu();
    }

private:
    static constexpr double sr = 48000.0;

    //==========================================================================
    void testEffectsDoTheirJob()
    {
        beginTest("EQ: low cut removes rumble, mid bell boosts by the set amount");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Equalizer);
            setParam(*p, pid::eqHighGain, 0.0f);
            setParam(*p, pid::eqLowCut, 120.0f);

            auto rumble = sine(30.0, 0.25f, 48000);
            processSignal(*p, rumble);
            expectLessThan(rumble.getRMSLevel(0, 24000, 24000), 0.25f * 0.7071f * dsp::dbToGain(-18.0f),
                           "30 Hz should drop by more than 18 dB");

            setParam(*p, pid::eqLowCut, 20.0f);
            setParam(*p, pid::eqMidFreq, 1000.0f);
            setParam(*p, pid::eqMidGain, 6.0f);
            p->getChain().requestSnap();
            auto tone = sine(1000.0, 0.1f, 48000);
            processSignal(*p, tone);
            expectWithinAbsoluteError(rmsDb(tone, 24000, 24000) - juce::Decibels::gainToDecibels(0.1f * 0.70710678f), 6.0f, 0.5f);
        }

        beginTest("Gate: expander turns noise down gently; hard gate by its full range; singing untouched");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Gate);
            setParam(*p, pid::gateThreshold, -50.0f);
            setParam(*p, pid::gateRange, 30.0f);
            setParam(*p, pid::gateRatio, 2.0f);

            // 2:1 expander
            auto hiss = sine(500.0, dsp::dbToGain(-70.0f), 96000);
            processSignal(*p, hiss);
            expectWithinAbsoluteError(rmsDb(hiss, 72000, 24000) - (-70.0f - 3.01f), -20.0f, 1.0f);

            // 10:1 behaves as a hard gate
            setParam(*p, pid::gateRatio, 10.0f);
            auto hiss2 = sine(500.0, dsp::dbToGain(-70.0f), 96000);
            processSignal(*p, hiss2);
            expectWithinAbsoluteError(rmsDb(hiss2, 72000, 24000) - (-70.0f - 3.01f), -30.0f, 1.0f);

            auto voice = sine(500.0, dsp::dbToGain(-20.0f), 48000);
            processSignal(*p, voice);
            expectWithinAbsoluteError(rmsDb(voice, 24000, 24000) - (-20.0f - 3.01f), 0.0f, 0.2f);
        }

        beginTest("Compressor: steady level follows threshold + (input - threshold) / ratio (RMS detection)");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Compressor);
            setParam(*p, pid::compThreshold, -24.0f);
            setParam(*p, pid::compRatio, 4.0f);
            setParam(*p, pid::compMakeup, 0.0f);

            // Peak -6 dBFS sine = -9 dB RMS: 15 dB over the threshold -> 3.75 dB over
            auto loud = sine(300.0, dsp::dbToGain(-6.0f), 96000);
            processSignal(*p, loud);
            const float peakOut = juce::Decibels::gainToDecibels(loud.getMagnitude(0, 72000, 24000));
            expectWithinAbsoluteError(peakOut, -17.3f, 1.0f);

            // Mix 0% = fully parallel
            setParam(*p, pid::compMix, 0.0f);
            auto dryThrough = sine(300.0, dsp::dbToGain(-6.0f), 48000);
            processSignal(*p, dryThrough);
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(dryThrough.getMagnitude(0, 24000, 24000)), -6.0f, 0.3f);
            expectGreaterThan(p->getChain().getCompressorGainReductionDb(), 10.0f);
        }

        beginTest("De-esser: tames loud 's' sounds, leaves the body of the voice alone");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::DeEsser);
            setParam(*p, pid::dsThreshold, -30.0f);
            setParam(*p, pid::dsRange, 12.0f);
            setParam(*p, pid::dsFreq, 8000.0f);
            p->getChain().requestSnap();

            auto ess = sine(8000.0, dsp::dbToGain(-6.0f), 48000);
            processSignal(*p, ess);
            expectLessThan(rmsDb(ess, 24000, 24000) - (-6.0f - 3.01f), -6.0f);

            auto body = sine(300.0, dsp::dbToGain(-6.0f), 48000);
            processSignal(*p, body);
            expectWithinAbsoluteError(rmsDb(body, 24000, 24000) - (-6.0f - 3.01f), 0.0f, 0.3f);
        }

        beginTest("Multiband: with no compression the three bands add back up flat");
        {
            for (double f : {80.0, 220.0, 1000.0, 4000.0, 12000.0})
            {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Multiband);
                for (auto* id : {pid::mbLowThreshold, pid::mbMidThreshold, pid::mbHighThreshold})
                    setParam(*p, id, 0.0f);
                p->getChain().requestSnap();

                auto tone = sine(f, 0.1f, 48000);
                processSignal(*p, tone);
                expectWithinAbsoluteError(rmsDb(tone, 24000, 24000) - juce::Decibels::gainToDecibels(0.1f * 0.70710678f), 0.0f, 0.1f,
                                          "at " + juce::String(f) + " Hz");
            }
        }

        beginTest("Limiter: nothing ever gets past the ceiling");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Limiter);
            setParam(*p, pid::limCeiling, -3.0f);

            auto screaming = sine(440.0, 4.0f, 48000); // +12 dBFS
            processSignal(*p, screaming);
            expectLessOrEqual(screaming.getMagnitude(0, screaming.getNumSamples()), dsp::dbToGain(-3.0f) * 1.0001f);
        }

        beginTest("Saturation, chorus, delay and reverb change the sound and stay stable");
        for (auto id : {ModuleId::Saturation, ModuleId::Chorus, ModuleId::Delay, ModuleId::Reverb})
        {
            auto p = makeProcessor();
            onlyModule(*p, id);
            auto original = sine(440.0, 0.3f, 48000);
            auto processed = original;
            processSignal(*p, processed);
            expect(allFinite(processed), describe(id).displayName);

            float diff = 0.0f;
            for (int i = 0; i < processed.getNumSamples(); ++i)
                diff = juce::jmax(diff, std::abs(processed.getSample(0, i) - original.getSample(0, i)));
            expectGreaterThan(diff, 0.01f, juce::String(describe(id).displayName) + " should audibly change the sound");
        }
    }

    //==========================================================================
    void testSoundQualityRound()
    {
        beginTest("Auto Level: raises quiet talking toward the target, holds still in silence");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Leveler);
            setParam(*p, pid::levTarget, -18.0f);
            setParam(*p, pid::levRange, 9.0f);
            setParam(*p, pid::levSpeed, 500.0f);

            // Quiet voice at -30 dB RMS: wants +12, limited to the 9 dB range
            auto quiet = sine(250.0, dsp::dbToGain(-27.0f), 48000 * 4);
            processSignal(*p, quiet);
            expectWithinAbsoluteError(rmsDb(quiet, 48000 * 3, 48000) - (-30.0f), 9.0f, 0.5f);

            // Then silence-level noise
            const float gainBefore = p->getChain().getActivity().levelerGainDb;
            auto pause = sine(250.0, dsp::dbToGain(-65.0f), 48000 * 2);
            processSignal(*p, pause);
            expectWithinAbsoluteError(p->getChain().getActivity().levelerGainDb, gainBefore, 0.5f);
        }

        beginTest("Parallel effects: the dry voice stays untouched; effects are added on top");
        for (auto id : {ModuleId::Chorus, ModuleId::Delay, ModuleId::Reverb})
        {
            auto p = makeProcessor();
            onlyModule(*p, id);
            juce::AudioBuffer<float> impulse(2, 480);
            impulse.clear();
            impulse.setSample(0, 0, 0.5f);
            impulse.setSample(1, 0, 0.5f);
            juce::MidiBuffer midi;
            p->processBlock(impulse, midi);
            expectWithinAbsoluteError(impulse.getSample(0, 0), 0.5f, 1.0e-6f,
                                      juce::String(describe(id).displayName) + " must not change the dry voice");
        }

        beginTest("Ducking: echoes are much quieter while singing, and come back in the gaps");
        {
            // Render the same phrase with and without ducking
            auto render = [&](float duckDb) {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Delay);
                setParam(*p, pid::dlyTime, 200.0f);
                setParam(*p, pid::dlyFeedback, 50.0f);
                setParam(*p, pid::dlyMix, 100.0f);
                setParam(*p, pid::dlyDuck, duckDb);
                p->getChain().requestSnap();
                auto signal = sine(440.0, 0.5f, 72000);
                for (int c = 0; c < 2; ++c)
                    juce::FloatVectorOperations::clear(signal.getWritePointer(c, 24000), 48000);
                auto dry = signal;
                processSignal(*p, signal);
                for (int c = 0; c < 2; ++c)
                    juce::FloatVectorOperations::subtract(signal.getWritePointer(c), dry.getReadPointer(c), signal.getNumSamples());
                return signal; // echo only
            };

            const auto plain = render(0.0f);
            const auto ducked = render(18.0f);
            const float duringSinging = rmsDb(ducked, 12000, 9600) - rmsDb(plain, 12000, 9600);
            const float laterInGap = rmsDb(ducked, 24000 + 19200, 14400) - rmsDb(plain, 24000 + 19200, 14400);
            logMessage("Ducking: echo " + juce::String(duringSinging, 1) + " dB while singing, " + juce::String(laterInGap, 1) +
                       " dB 0.4-0.7 s after the phrase");
            expectLessThan(duringSinging, -12.0f, "echo ducked under the voice");
            expectGreaterThan(laterInGap, -6.0f, "echo blooms back in the gap");
        }

        beginTest("Low cut: 24 dB/oct removes more rumble than 12 dB/oct");
        {
            float result[2];
            for (int slope = 0; slope < 2; ++slope)
            {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Equalizer);
                setParam(*p, pid::eqHighGain, 0.0f);
                setParam(*p, pid::eqLowCut, 120.0f);
                setParam(*p, pid::eqLowCutSlope, (float)slope);
                p->getChain().requestSnap();
                auto rumble = sine(40.0, 0.25f, 48000);
                processSignal(*p, rumble);
                result[slope] = rmsDb(rumble, 24000, 24000);
            }
            expectLessThan(result[1], result[0] - 10.0f);
        }

        beginTest("Older presets: a newly added module lands in a sensible place");
        {
            const auto merged =
                dsp::VocalChain::orderFromString("gate,eq,deesser,compressor,multiband,saturation,chorus,delay,reverb,limiter");
            expect(merged.has_value());
            expect((*merged)[1] == ModuleId::Tune, "Tune goes right after the Gate");
            expect((*merged)[3] == ModuleId::Leveler, "Auto Level goes right after EQ");
            expect((*merged)[(size_t)kNumModules - 1] == ModuleId::Limiter, "the limiter stays last");
        }
    }

    void testReverb()
    {
        // Reverb-only processor
        auto makeReverb = [&](float sizePercent, float dampingPercent) {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Reverb);
            setParam(*p, pid::revSize, sizePercent);
            setParam(*p, pid::revDamping, dampingPercent);
            setParam(*p, pid::revMix, 100.0f);
            setParam(*p, pid::revDuck, 0.0f);
            setParam(*p, pid::revPreDelay, 0.0f);
            setParam(*p, pid::revLowCut, 50.0f);
            setParam(*p, pid::revHighCut, 16000.0f);
            p->getChain().requestSnap();
            return p;
        };

        beginTest("Reverb: decay time follows the Size setting");
        for (float size : {30.0f, 55.0f, 80.0f})
        {
            auto p = makeReverb(size, 20.0f);
            const int length = 48000 * 8;
            juce::AudioBuffer<float> b(2, length);
            b.clear();
            b.setSample(0, 0, 0.5f);
            b.setSample(1, 0, 0.5f);
            processSignal(*p, b);
            b.setSample(0, 0, 0.0f); // remove the dry impulse, keep only the tail
            b.setSample(1, 0, 0.0f);

            // Schroeder backward integration -> T30, times 2 = RT60
            std::vector<double> energy((size_t)length);
            double acc = 0.0;
            for (int i = length; --i >= 0;)
            {
                const double v = b.getSample(0, i);
                acc += v * v;
                energy[(size_t)i] = acc;
            }
            int t5 = -1, t35 = -1;
            for (int i = 0; i < length; ++i)
            {
                const double db = 10.0 * std::log10(energy[(size_t)i] / energy[0] + 1.0e-30);
                if (t5 < 0 && db <= -5.0)
                    t5 = i;
                if (t35 < 0 && db <= -35.0)
                {
                    t35 = i;
                    break;
                }
            }
            const float measured = 2.0f * (float)(t35 - t5) / 48000.0f;
            const float expected = 0.3f * std::pow(4.0f / 0.3f, size * 0.01f);
            logMessage("Reverb size " + juce::String(size) + "%: RT60 " + juce::String(measured, 2) + " s (target " +
                       juce::String(expected, 2) + " s)");
            expectWithinAbsoluteError(measured, expected, expected * 0.3f, "RT60 at size " + juce::String(size));
        }

        beginTest("Reverb: never runs away, even at maximum size and no damping");
        {
            auto p = makeReverb(100.0f, 0.0f);
            juce::Random rng(3);
            juce::AudioBuffer<float> b(2, 48000 * 16);
            b.clear();
            for (int i = 0; i < 48000 * 4; ++i)
                b.setSample(0, i, rng.nextFloat() - 0.5f), b.setSample(1, i, b.getSample(0, i));
            processSignal(*p, b);
            expect(allFinite(b));
            expectLessThan(juce::Decibels::gainToDecibels(b.getMagnitude(0, 48000 * 15, 48000)), -80.0f,
                           "tail has died away 12 s after the input stopped");
        }

        beginTest("Reverb: about as loud as the previous reverb, so presets keep their balance");
        {
            juce::Random rng(5);
            juce::AudioBuffer<float> noise(2, 48000 * 3);
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample(0, i, 0.3f * (rng.nextFloat() - 0.5f)), noise.setSample(1, i, noise.getSample(0, i));

            auto ours = noise;
            auto p = makeReverb(55.0f, 45.0f);
            processSignal(*p, ours);
            for (int c = 0; c < 2; ++c) // keep only the reverb
                juce::FloatVectorOperations::subtract(ours.getWritePointer(c), noise.getReadPointer(c), ours.getNumSamples());

            auto classic = noise;
            juce::Reverb reference;
            reference.setSampleRate(48000.0);
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.55f;
            rp.damping = 0.45f;
            rp.width = 1.0f;
            rp.wetLevel = 1.0f / 3.0f;
            rp.dryLevel = 0.0f;
            reference.setParameters(rp);
            reference.processStereo(classic.getWritePointer(0), classic.getWritePointer(1), classic.getNumSamples());

            const float difference = rmsDb(ours, 48000, 96000) - rmsDb(classic, 48000, 96000);
            logMessage("Reverb level vs previous reverb: " + juce::String(difference, 1) + " dB");
            expectWithinAbsoluteError(difference, 0.0f, 4.0f);
        }
    }

    // Magnitude spectrum of channel 0 over the last 2^15 samples
    static std::vector<double> powerSpectrum(const juce::AudioBuffer<float>& b)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft(order);
        juce::dsp::WindowingFunction<float> window((size_t)size, juce::dsp::WindowingFunction<float>::blackmanHarris);
        std::vector<float> data((size_t)size * 2, 0.0f);
        const int start = b.getNumSamples() - size;
        for (int i = 0; i < size; ++i)
            data[(size_t)i] = b.getSample(0, start + i);
        window.multiplyWithWindowingTable(data.data(), (size_t)size);
        fft.performFrequencyOnlyForwardTransform(data.data());
        std::vector<double> power((size_t)size / 2);
        for (int i = 0; i < size / 2; ++i)
            power[(size_t)i] = (double)data[(size_t)i] * data[(size_t)i];
        return power;
    }

    // Energy that is NOT at a harmonic of f0, relative to the fundamental
    static float aliasingDb(const std::vector<double>& power, double f0, double sampleRate)
    {
        const double binHz = sampleRate / (double)(power.size() * 2);
        double fundamental = 0.0, other = 0.0;
        for (size_t i = 1; i < power.size(); ++i)
        {
            const double f = (double)i * binHz;
            if (f < 50.0 || f > 20000.0)
                continue;
            const double harmonic = std::round(f / f0);
            const bool nearHarmonic = harmonic >= 1.0 && std::abs(f - harmonic * f0) < 8.0 * binHz;
            if (nearHarmonic && harmonic == 1.0)
                fundamental += power[i];
            else if (!nearHarmonic)
                other += power[i];
        }
        return (float)(10.0 * std::log10((other + 1.0e-30) / (fundamental + 1.0e-30)));
    }

    static float harmonicDb(const std::vector<double>& power, double f, double sampleRate)
    {
        const double binHz = sampleRate / (double)(power.size() * 2);
        const int centre = (int)std::lround(f / binHz);
        double e = 0.0;
        for (int i = centre - 6; i <= centre + 6; ++i)
            e += power[(size_t)juce::jlimit(0, (int)power.size() - 1, i)];
        return (float)(10.0 * std::log10(e + 1.0e-30));
    }

    void testColor()
    {
        auto makeSaturation = [&](int model, float driveDb, float character) {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Saturation);
            setParam(*p, pid::satModel, (float)model);
            setParam(*p, pid::satDrive, driveDb);
            setParam(*p, pid::satMix, 100.0f);
            setParam(*p, pid::satCharacter, character);
            p->getChain().requestSnap();
            return p;
        };

        beginTest("Saturation: oversampling keeps aliasing far below a plain saturator");
        {
            const double f0 = 6997.0; // high, bright note
            auto p = makeSaturation(0, 18.0f, 35.0f);
            auto ours = sine(f0, 0.5f, 65536);
            processSignal(*p, ours);

            // Reference
            auto plain = sine(f0, 0.5f, 65536);
            const float k = dsp::dbToGain(18.0f), b = 0.15f + 1.5f * (0.05f + 35.0f * 0.0035f), tb = std::tanh(b);
            for (int i = 0; i < plain.getNumSamples(); ++i)
                plain.setSample(0, i, std::tanh(plain.getSample(0, i) * k + b) - tb);

            const float oursDb = aliasingDb(powerSpectrum(ours), f0, sr);
            const float plainDb = aliasingDb(powerSpectrum(plain), f0, sr);
            logMessage("Aliasing at 7 kHz, +18 dB drive: StageMic " + juce::String(oursDb, 1) + " dB, plain saturator " +
                       juce::String(plainDb, 1) + " dB (relative to the note)");
            expectLessThan(oursDb, plainDb - 15.0f, "at least 15 dB less aliasing than a plain saturator");
            expectLessThan(oursDb, -50.0f);
        }

        beginTest("Saturation models have their own harmonic character");
        {
            float h2[3], h3[3], thd[3];
            for (int model = 0; model < 3; ++model)
            {
                auto p = makeSaturation(model, 12.0f, model == 0 ? 50.0f : 0.0f);
                auto tone = sine(1000.0, 0.3f, 65536);
                processSignal(*p, tone);
                const auto power = powerSpectrum(tone);
                const float f1 = harmonicDb(power, 1000.0, sr);
                h2[model] = harmonicDb(power, 2000.0, sr) - f1;
                h3[model] = harmonicDb(power, 3000.0, sr) - f1;
                thd[model] = juce::jmax(h2[model], h3[model]);
                logMessage(juce::String(model == 0 ? "Tube" : (model == 1 ? "Tape" : "Console")) + ": 2nd harmonic " +
                           juce::String(h2[model], 1) + " dB, 3rd " + juce::String(h3[model], 1) + " dB");
            }
            expectGreaterThan(h2[0], h3[0], "Tube: even (2nd) harmonic dominates - warm");
            expectGreaterThan(h3[1], h2[1], "Tape: odd (3rd) harmonic dominates - soft and round");
            expectLessThan(thd[2], thd[0], "Console is the subtlest model");
        }

        beginTest("Saturation adds almost no delay (oversampling filters are minimum-phase)");
        {
            auto p = makeSaturation(0, 0.0f, 0.0f);
            juce::AudioBuffer<float> impulse(2, 480);
            impulse.clear();
            impulse.setSample(0, 0, 0.01f); // small
            impulse.setSample(1, 0, 0.01f);
            juce::MidiBuffer midi;
            p->processBlock(impulse, midi);
            int peak = 0;
            for (int i = 1; i < 480; ++i)
                if (std::abs(impulse.getSample(0, i)) > std::abs(impulse.getSample(0, peak)))
                    peak = i;
            logMessage("Saturation (100% wet) impulse peak at sample " + juce::String(peak) + " = " + juce::String(peak / 48.0, 3) + " ms");
            expectLessOrEqual(peak, 5, "under about 0.1 ms");
        }

        beginTest("Exciter: adds harmonics above its frequency, leaves the body of the voice alone");
        {
            auto make = [&] {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Exciter);
                setParam(*p, pid::exFreq, 2500.0f);
                setParam(*p, pid::exAmount, 70.0f);
                setParam(*p, pid::exMix, 50.0f);
                p->getChain().requestSnap();
                return p;
            };

            auto p = make();
            auto bright = sine(3000.0, 0.3f, 65536);
            const auto before = powerSpectrum(bright);
            processSignal(*p, bright);
            const auto after = powerSpectrum(bright);
            const float added = harmonicDb(after, 6000.0, sr) - harmonicDb(before, 6000.0, sr);
            logMessage("Exciter: energy at 6 kHz (2nd harmonic of 3 kHz) rose by " + juce::String(added, 1) + " dB");
            expectGreaterThan(added, 30.0f, "new upper harmonics appear");

            auto q = make();
            auto body = sine(300.0, 0.3f, 48000);
            processSignal(*q, body);
            expectWithinAbsoluteError(rmsDb(body, 24000, 24000) - juce::Decibels::gainToDecibels(0.3f * 0.70710678f), 0.0f, 0.3f,
                                      "a 300 Hz voice body passes unchanged");
        }
    }

    void testDeEsserAndOrder()
    {
        // A voice-like test signal
        auto voiceWithEss = [] {
            juce::AudioBuffer<float> b(2, 65536 * 2);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / 48000.0;
                const bool essOn = (i / 4800) % 2 == 1; // 100 ms on / 100 ms off
                const float v = 0.15f * (float)std::sin(2.0 * dsp::kPi * 300.0 * t) +
                                0.03f * (float)std::sin(2.0 * dsp::kPi * 12000.0 * t) +
                                (essOn ? 0.3f * (float)std::sin(2.0 * dsp::kPi * 7000.0 * t) : 0.0f);
                b.setSample(0, i, v);
                b.setSample(1, i, v);
            }
            return b;
        };
        auto deEsser = [&](int mode, bool autoFreq) {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::DeEsser);
            setParam(*p, pid::dsMode, (float)mode);
            setParam(*p, pid::dsAuto, autoFreq ? 1.0f : 0.0f);
            setParam(*p, pid::dsFreq, 7000.0f);
            setParam(*p, pid::dsThreshold, -35.0f);
            setParam(*p, pid::dsRange, 12.0f);
            p->getChain().requestSnap();
            return p;
        };

        beginTest("De-esser Band mode cuts the s, keeps the sparkle; Split mode dulls it");
        {
            const auto original = voiceWithEss();
            const auto inPower = powerSpectrum(original);
            float essCut[2], airChange[2];
            for (int mode = 0; mode < 2; ++mode)
            {
                auto out = original;
                auto p = deEsser(mode, false);
                processSignal(*p, out);
                const auto outPower = powerSpectrum(out);
                essCut[mode] = harmonicDb(outPower, 7000.0, sr) - harmonicDb(inPower, 7000.0, sr);
                airChange[mode] = harmonicDb(outPower, 12000.0, sr) - harmonicDb(inPower, 12000.0, sr);
                logMessage(juce::String(mode == 0 ? "Band" : "Split") + " mode: s (7 kHz) " + juce::String(essCut[mode], 1) +
                           " dB, air (12 kHz) " + juce::String(airChange[mode], 1) + " dB");
            }
            expectLessThan(essCut[0], -4.0f, "Band mode reduces the s");
            expectGreaterThan(airChange[0], -1.0f, "Band mode leaves the 12 kHz sparkle alone");
            expectLessThan(airChange[1], airChange[0] - 1.0f, "Split mode turns the sparkle down too");
        }

        beginTest("De-esser Auto finds the sibilant frequency");
        {
            auto p = deEsser(0, true);
            setParam(*p, pid::dsFreq, 4500.0f); // start far away
            auto b = voiceWithEss();
            for (int c = 0; c < 2; ++c) // move the s to about 7.8 kHz
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const bool essOn = (i / 4800) % 2 == 1;
                    const double t = i / 48000.0;
                    b.setSample(c, i,
                                0.15f * (float)std::sin(2.0 * dsp::kPi * 300.0 * t) +
                                    (essOn ? 0.3f * (float)std::sin(2.0 * dsp::kPi * 7800.0 * t) : 0.0f));
                }
            processSignal(*p, b);
            const float found = p->getChain().getActivity().deEsserFrequency;
            logMessage("De-esser Auto settled at " + juce::String(found / 1000.0f, 2) + " kHz (s at 7.8 kHz)");
            expectWithinAbsoluteError(found, 7800.0f, 800.0f);
        }

        beginTest("De-esser Listen plays only what it listens to");
        {
            auto p = deEsser(0, false);
            p->getChain().setDeEsserListen(true);
            auto body = sine(300.0, 0.3f, 48000);
            processSignal(*p, body);
            expectLessThan(rmsDb(body, 24000, 24000) - juce::Decibels::gainToDecibels(0.3f * 0.70710678f), -20.0f,
                           "the body of the voice is not in the Listen signal");
            p->getChain().setDeEsserListen(false);
        }

        beginTest("Recommended order: De-Ess after Compressor, Warmth and Air; Limiter last");
        {
            const auto order = dsp::VocalChain::defaultOrder();
            auto indexOf = [&](ModuleId id) { return (int)(std::find(order.begin(), order.end(), id) - order.begin()); };
            expect(indexOf(ModuleId::DeEsser) > indexOf(ModuleId::Compressor));
            expect(indexOf(ModuleId::DeEsser) > indexOf(ModuleId::Exciter));
            expect(indexOf(ModuleId::DeEsser) > indexOf(ModuleId::Saturation));
            expect(order.back() == ModuleId::Limiter);
            expect(dsp::VocalChain::isValidOrder(order));
        }

        beginTest("Re-ordering while audio plays doesn't click");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Equalizer);
            setParam(*p, pid::revOn, 1.0f);
            setParam(*p, pid::eqHighGain, 6.0f);
            p->getChain().requestSnap();
            auto signal = sine(220.0, 0.5f, 48000);
            juce::MidiBuffer midi;
            float worstJump = 0.0f, previous = 0.0f;
            for (int start = 0; start < signal.getNumSamples(); start += 480)
            {
                if (start == 24000) // swap EQ and Reverb mid-signal
                {
                    auto order = dsp::VocalChain::defaultOrder();
                    std::iter_swap(std::find(order.begin(), order.end(), ModuleId::Equalizer),
                                   std::find(order.begin(), order.end(), ModuleId::Reverb));
                    p->getChain().setOrder(order);
                }
                juce::AudioBuffer<float> view(signal.getArrayOfWritePointers(), 2, start, 480);
                p->processBlock(view, midi);
                for (int i = 0; i < 480; ++i)
                {
                    const float v = view.getSample(0, i);
                    if (start > 0 || i > 0)
                        worstJump = juce::jmax(worstJump, std::abs(v - previous));
                    previous = v;
                }
            }
            // A 220 Hz sine at 0.5 changes by at most ~0.015 per sample
            expectLessThan(worstJump, 0.06f);
        }

        beginTest("Saved v0.2.0 setups move to the new order; deliberate orders are kept");
        {
            auto stateWithOrder = [](const juce::String& order) {
                juce::XmlElement xml(StageMicProcessor::stateTag);
                xml.setAttribute("version", 2);
                xml.setAttribute("order", order);
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(xml, block);
                return block;
            };

            auto p = makeProcessor();
            auto legacy = stateWithOrder(dsp::VocalChain::legacyDefaultOrderString());
            p->setStateInformation(legacy.getData(), (int)legacy.getSize());
            expect(p->getChain().getOrder() == dsp::VocalChain::defaultOrder(), "old default -> new recommended order");

            auto custom = stateWithOrder("reverb,gate,eq,deesser,compressor,multiband,saturation,chorus,delay,limiter");
            p->setStateInformation(custom.getData(), (int)custom.getSize());
            expect(p->getChain().getOrder()[0] == ModuleId::Reverb, "a deliberately chosen order is kept");
        }
    }

    void testSafety()
    {
        beginTest("Every module on: the audio path never allocates memory");
        {
            auto p = makeProcessor();
            for (const auto& d : moduleDescriptors())
                setParam(*p, d.enableParam, 1.0f);
            p->getChain().requestSnap();

            auto signal = sine(220.0, 0.5f, 48000 * 3);
            juce::AudioBuffer<float> warmup(2, 480);
            juce::MidiBuffer midi;
            p->processBlock(warmup, midi);

            std::size_t allocations = 0;
            {
                AllocationGuard guard;
                processSignal(*p, signal);
                allocations = guard.count();
            }
            expectEquals((int)allocations, 0, "heap allocations with the full chain running");
        }

        beginTest("Switching modules and presets while audio runs never allocates on the audio thread");
        {
            auto p = makeProcessor();
            auto& presets = p->getPresetManager();
            juce::MidiBuffer midi;
            auto block = sine(330.0, 0.4f, 480);
            std::size_t allocations = 0;

            for (const auto& name : PresetManager::getFactoryPresetNames())
            {
                presets.loadFactoryPreset(name); // "UI thread"
                AllocationGuard guard;           // "audio thread"
                for (int i = 0; i < 20; ++i)
                    p->processBlock(block, midi);
                allocations += guard.count();
            }
            expectEquals((int)allocations, 0);
        }

        beginTest("Random settings, silence and extreme input never produce NaN or blow up");
        {
            juce::Random rng(42);
            auto p = makeProcessor();
            auto& params = p->getParameters();

            for (int round = 0; round < 60; ++round)
            {
                for (auto* param : p->getParameters().processor.getParameters())
                    param->setValueNotifyingHost(rng.nextFloat());
                juce::ignoreUnused(params);
                setParam(*p, pid::limOn, 1.0f);
                setParam(*p, pid::outGain, 0.0f);

                juce::AudioBuffer<float> b(2, 4800);
                const int kind = round % 3;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample(c, i, kind == 0 ? 0.0f : (kind == 1 ? rng.nextFloat() * 2.0f - 1.0f : (i % 2 ? 8.0f : -8.0f)));

                processSignal(*p, b);
                expect(allFinite(b), "round " + juce::String(round));
                const float ceiling = dsp::dbToGain(getParam(*p, pid::limCeiling));
                expectLessOrEqual(b.getMagnitude(0, 2400, 2400), ceiling * 1.001f, "limiter ceiling, round " + juce::String(round));
            }
        }

        beginTest("Mono input works with every module on");
        {
            auto p = makeProcessor(sr, 1);
            for (const auto& d : moduleDescriptors())
                setParam(*p, d.enableParam, 1.0f);
            auto b = sine(440.0, 0.5f, 24000, sr, 1);
            processSignal(*p, b);
            expect(allFinite(b));
        }
    }

    //==========================================================================
    void testZeroLatency()
    {
        beginTest("Zero latency: every module responds on the very first sample (Tune excepted: it's tested on its own)");
        for (const auto& d : moduleDescriptors())
        {
            if (d.id == ModuleId::Tune)
                continue;
            auto p = makeProcessor();
            onlyModule(*p, d.id);
            setParam(*p, pid::gateThreshold, -80.0f); // gate open
            setParam(*p, pid::compThreshold, 0.0f);   // no compression
            setParam(*p, pid::dsThreshold, 0.0f);
            p->getChain().requestSnap();

            juce::AudioBuffer<float> impulse(2, 480);
            impulse.clear();
            impulse.setSample(0, 0, 0.5f);
            impulse.setSample(1, 0, 0.5f);
            juce::MidiBuffer midi;
            p->processBlock(impulse, midi);

            expectGreaterThan(std::abs(impulse.getSample(0, 0)), 0.05f, juce::String(d.displayName) + " must not delay the voice");
        }
        expectEquals(makeProcessor()->getLatencySamples(), 0);
    }

    //==========================================================================
    void testModuleOrder()
    {
        beginTest("Module order: can be changed, is saved, and bad orders are rejected");
        {
            auto p = makeProcessor();
            auto order = dsp::VocalChain::defaultOrder();
            std::swap(order[0], order[9]);
            expect(p->getChain().setOrder(order));

            juce::MemoryBlock state;
            p->getStateInformation(state);
            auto q = makeProcessor();
            q->setStateInformation(state.getData(), (int)state.getSize());
            expect(q->getChain().getOrder() == order, "order survives save/restore");

            auto bad = dsp::VocalChain::defaultOrder();
            bad[1] = bad[0];
            expect(!p->getChain().setOrder(bad), "duplicate modules rejected");
            expect(!dsp::VocalChain::orderFromString("gate,gate").has_value());

            // A preset from an older version that doesn't know every module still
            auto partial = dsp::VocalChain::orderFromString("limiter,gate");
            expect(partial.has_value() && (*partial)[0] == ModuleId::Limiter && dsp::VocalChain::isValidOrder(*partial));
        }
    }

    //==========================================================================
    void testTypedValues()
    {
        beginTest("Typed values: units and shorthand are understood");
        auto p = makeProcessor();
        auto typed = [&](const char* id, const char* text) {
            auto* param = p->getParameters().getParameter(id);
            return param->convertFrom0to1(param->getValueForText(text));
        };
        expectWithinAbsoluteError(typed(pid::eqMidFreq, "2.5k"), 2500.0f, 1.0f);
        expectWithinAbsoluteError(typed(pid::eqMidFreq, "3 kHz"), 3000.0f, 1.0f);
        expectWithinAbsoluteError(typed(pid::eqMidFreq, "800hz"), 800.0f, 1.0f);
        expectWithinAbsoluteError(typed(pid::eqMidGain, "-4.5 dB"), -4.5f, 0.01f);
        expectWithinAbsoluteError(typed(pid::eqMidGain, "+3"), 3.0f, 0.01f);
        expectWithinAbsoluteError(typed(pid::compRatio, "4:1"), 4.0f, 0.01f);
        expectWithinAbsoluteError(typed(pid::dlyTime, "0.5 s"), 500.0f, 0.5f);
        expectWithinAbsoluteError(typed(pid::dlyTime, "250ms"), 250.0f, 0.5f);
        expectWithinAbsoluteError(typed(pid::revMix, "35%"), 35.0f, 0.01f);
        expectWithinAbsoluteError(typed(pid::eqMidGain, "99"), 12.0f, 0.01f, "out-of-range input is clamped");
    }

    void testPresets()
    {
        auto dir = juce::File::createTempFile("stagemic-presets");
        dir.createDirectory();

        beginTest("Every factory preset loads");
        {
            auto p = makeProcessor();
            PresetManager presets(*p, dir);
            for (const auto& name : PresetManager::getFactoryPresetNames())
            {
                expect(presets.loadFactoryPreset(name), name);
                expectEquals(p->getCurrentPresetName(), name);
            }
            expect(!presets.loadFactoryPreset("Does Not Exist"));

            presets.loadFactoryPreset("Radio Voice");
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 280.0f, 0.5f);
            presets.loadFactoryPreset("Clean Vocal");
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 80.0f, 0.5f, "switching presets resets unlisted settings");
        }

        beginTest("Save, load, export, import and delete user presets");
        {
            auto p = makeProcessor();
            PresetManager presets(*p, dir);

            setParam(*p, pid::revMix, 42.0f);
            setParam(*p, pid::satOn, 1.0f);
            const auto saved = presets.savePreset("My Big Room");
            expect(saved.existsAsFile());
            expectEquals(presets.getUserPresetFiles().size(), 1);

            presets.loadFactoryPreset("Clean Vocal");
            expect(presets.loadPresetFile(saved));
            expectWithinAbsoluteError(getParam(*p, pid::revMix), 42.0f, 0.01f);
            expectEquals(getParam(*p, pid::satOn), 1.0f);
            expectEquals(p->getCurrentPresetName(), juce::String("My Big Room"));

            const auto exported = dir.getSiblingFile("shared-sound");
            expect(presets.exportPreset(exported));
            const auto exportedFile = exported.withFileExtension(PresetManager::fileExtension);
            expect(exportedFile.existsAsFile());

            const auto imported = presets.importPreset(exportedFile);
            expect(imported.existsAsFile());
            expectEquals(imported.getFileNameWithoutExtension(), juce::String("My Big Room (2)"), "import never overwrites");

            expect(presets.deletePreset(imported));
            expect(!presets.deletePreset(exportedFile), "only files inside the preset folder can be deleted");
            exportedFile.deleteFile();
        }

        beginTest("Damaged or foreign files are rejected without changing the sound");
        {
            auto p = makeProcessor();
            PresetManager presets(*p, dir);
            setParam(*p, pid::revMix, 33.0f);

            auto junk = dir.getChildFile("junk.stagemicpreset");
            junk.replaceWithText("this is not xml");
            expect(!presets.loadPresetFile(junk));
            auto foreign = dir.getChildFile("foreign.stagemicpreset");
            foreign.replaceWithText("<SomeOtherPlugin/>");
            expect(!presets.loadPresetFile(foreign));
            expect(!presets.importPreset(foreign).existsAsFile());
            expectWithinAbsoluteError(getParam(*p, pid::revMix), 33.0f, 0.01f);
        }

        beginTest("A preset that lacks newer settings fills them with defaults");
        {
            auto p = makeProcessor();
            PresetManager presets(*p, dir);
            auto old = dir.getChildFile("old.stagemicpreset");
            old.replaceWithText("<StageMicPreset name=\"Old\" formatVersion=\"1\"><PARAMS>"
                                "<PARAM id=\"rev_mix\" value=\"60\"/></PARAMS></StageMicPreset>");
            setParam(*p, pid::eqLowCut, 300.0f);
            expect(presets.loadPresetFile(old));
            expectWithinAbsoluteError(getParam(*p, pid::revMix), 60.0f, 0.01f);
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 80.0f, 0.5f);
        }

        beginTest("Phase 1 saved state (no settings) loads as the default sound");
        {
            auto p = makeProcessor();
            setParam(*p, pid::revMix, 77.0f);
            juce::MemoryBlock v1;
            juce::XmlElement xml(StageMicProcessor::stateTag);
            xml.setAttribute("version", 1);
            juce::AudioProcessor::copyXmlToBinary(xml, v1);
            p->setStateInformation(v1.getData(), (int)v1.getSize());
            expectWithinAbsoluteError(getParam(*p, pid::revMix), 18.0f, 0.01f);
        }

        dir.deleteRecursively();
    }

    //==========================================================================
    void testCpu()
    {
        beginTest("Warmth: no sub-bass thump following the syllables");
        {
            // Syllables
            auto run = [&](bool satOn, int model) {
                auto p = makeProcessor();
                allModulesOff(*p);
                setParam(*p, pid::satOn, satOn ? 1.0f : 0.0f);
                setParam(*p, pid::satModel, (float)model);
                setParam(*p, pid::satDrive, 14.0f);
                setParam(*p, pid::satMix, 70.0f);
                setParam(*p, pid::satCharacter, 60.0f);
                // This much drive pushes the voice past 0 dBFS
                setParam(*p, pid::outGain, -12.0f);
                p->getChain().requestSnap();
                juce::AudioBuffer<float> b(2, (int)sr * 2);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    const double env = std::pow(std::sin(juce::MathConstants<double>::pi * 4.0 * t), 2.0);
                    double v = 0.0;
                    for (int h = 1; h <= 12; ++h)
                        v += std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * h * t) / h;
                    b.setSample(0, i, (float)(0.3 * env * v));
                    b.setSample(1, i, (float)(0.3 * env * v));
                }
                processSignal(*p, b);
                dsp::Biquad lp1, lp2;
                lp1.c = dsp::biquad::lowpass(sr, 40.0, 0.54);
                lp2.c = dsp::biquad::lowpass(sr, 40.0, 1.31);
                double low = 0.0, all = 0.0;
                for (int i = (int)sr / 2; i < b.getNumSamples(); ++i)
                {
                    const float x = b.getSample(0, i), y = lp2.process(lp1.process(x, 0), 0);
                    low += (double)y * y;
                    all += (double)x * x;
                }
                return 10.0 * std::log10(low / all + 1e-20);
            };
            const double clean = run(false, 0);
            for (int model = 0; model < 3; ++model)
            {
                const double sat = run(true, model);
                logMessage("below 40 Hz: no Warmth " + juce::String(clean, 1) + " dB, model " + juce::String(model) + " " +
                           juce::String(sat, 1) + " dB");
                expectLessThan(sat, clean + 6.0, "no thump");
            }
        }

        beginTest("Broken input (NaN / infinity) never breaks the sound for good");
        {
            for (const auto& preset : PresetManager::getFactoryPresetNames())
            {
                auto p = makeProcessor();
                p->getPresetManager().loadFactoryPreset(preset);
                setParam(*p, pid::tuneOn, 1.0f);
                p->getChain().requestSnap();
                auto before = sine(220.0, 0.3f, 4800);
                processSignal(*p, before);
                auto bad = sine(220.0, 0.3f, 480);
                for (int i = 0; i < 480; i += 3)
                    bad.setSample(0, i, i % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity());
                processSignal(*p, bad);
                auto after = sine(220.0, 0.3f, 48000);
                processSignal(*p, after);
                bool finite = true;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < after.getNumSamples(); ++i)
                        finite = finite && std::isfinite(after.getSample(c, i));
                expect(finite, preset + ": output stays a number");
                expectGreaterThan(after.getRMSLevel(0, 24000, 24000), 0.01f, preset + ": the voice comes back");
            }
        }

        beginTest("A mic on one side only (common with audio interfaces) is heard on both sides");
        {
            auto p = makeProcessor();
            p->getPresetManager().loadFactoryPreset("Clean Vocal");
            auto leftOnly = sine(220.0, 0.2f, 48000 * 3);
            leftOnly.clear(1, 0, leftOnly.getNumSamples());
            processSignal(*p, leftOnly);
            expectEquals(p->getMirroredChannel(), 0);
            const float left = leftOnly.getRMSLevel(0, 48000 * 2, 48000), right = leftOnly.getRMSLevel(1, 48000 * 2, 48000);
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(right / left), 0.0f, 1.0f, "same level both sides");

            // A real stereo source is left alone
            auto q = makeProcessor();
            q->getPresetManager().loadFactoryPreset("Clean Vocal");
            auto stereo = sine(220.0, 0.2f, 48000 * 3);
            stereo.applyGain(1, 0, stereo.getNumSamples(), 0.01f);
            processSignal(*q, stereo);
            expectEquals(q->getMirroredChannel(), -1);
        }

        beginTest("A damaged or hand-edited preset can't break the sound (garbage values in every setting)");
        {
            for (const char* garbage : {"nan", "inf", "-inf", "1e30", "-1e30", "abc", ""})
            {
                auto p = makeProcessor();
                auto xml = p->createPresetXml("Broken");
                int changed = 0;
                if (auto* params = xml->getChildByName("PARAMS"))
                    for (auto* e : params->getChildIterator())
                        if (e->hasAttribute("value"))
                        {
                            e->setAttribute("value", garbage);
                            ++changed;
                        }
                expectGreaterThan(changed, 50);
                p->applyPresetXml(*xml);
                bool sane = true;
                for (auto* param : p->getParameters().processor.getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
                    {
                        const float v = r->convertFrom0to1(r->getValue());
                        const auto range = r->getNormalisableRange();
                        sane = sane && std::isfinite(v) && v >= range.start && v <= range.end;
                    }
                expect(sane, juce::String("every setting stays inside its range after \"") + garbage + "\"");
                auto voice = sine(220.0, 0.2f, 96000);
                processSignal(*p, voice);
                bool finite = true;
                for (int i = 0; i < voice.getNumSamples(); ++i)
                    finite = finite && std::isfinite(voice.getSample(0, i));
                expect(finite, juce::String("output stays a number after \"") + garbage + "\"");
                expectGreaterThan(voice.getRMSLevel(0, 48000, 48000), 0.0001f,
                                  juce::String("the voice still comes through after \"") + garbage + "\"");
            }
        }

        beginTest("ASMR preset: a soft whisper is never chopped (no gate), and comes out at a usable level");
        {
            auto p = makeProcessor();
            expect(p->getPresetManager().loadFactoryPreset("ASMR"));
            expectLessThan(getParam(*p, pid::gateOn), 0.5f, "no gate");
            expectLessThan(getParam(*p, pid::revOn), 0.5f, "no reverb");
            p->getChain().requestSnap();
            juce::AudioBuffer<float> whisper(2, 48000 * 6);
            juce::Random rng(23);
            dsp::Biquad shape;
            shape.c = dsp::biquad::bandPass(48000.0, 2500.0, 0.8);
            for (int i = 0; i < whisper.getNumSamples(); ++i)
            {
                const double t = i / 48000.0;
                const float swell = 0.6f + 0.4f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 0.7 * t);
                const float v = shape.process(0.012f * swell * (rng.nextFloat() * 2.0f - 1.0f), 0);
                whisper.setSample(0, i, v);
                whisper.setSample(1, i, v);
            }
            // What "Set up my voice" would do with a whisper this quiet
            const auto input = whisper;
            juce::AudioBuffer<float> setupRun(input);
            p->getVoiceAnalyzer().start();
            {
                juce::AudioBuffer<float> quiet(2, 48000 * 3 + 2400);
                quiet.clear();
                processSignal(*p, quiet);
                processSignal(*p, setupRun);
                juce::AudioBuffer<float> more(input);
                processSignal(*p, more);
            }
            const auto r = VoiceAnalyzer::evaluate(p->getVoiceAnalyzer().getMeasurements());
            expect(r.heardVoice, "Set up my voice hears a whisper");
            p->applyVoiceSetup(r);
            logMessage("whisper setup: input gain " + juce::String(r.inputGainDb, 1) + " dB");
            p->getChain().requestSnap();

            processSignal(*p, whisper);
            // The gain the preset applies, window by window, after it has settled
            std::vector<std::pair<float, float>> windows; // input level, gain
            float loudest = 0.0f;
            for (int start = 96000; start + 2400 <= whisper.getNumSamples(); start += 2400)
            {
                const float in = input.getRMSLevel(0, start, 2400), out = whisper.getRMSLevel(0, start, 2400);
                windows.push_back({in, out / std::max(1.0e-9f, in)});
                loudest = std::max(loudest, out);
            }
            std::sort(windows.begin(), windows.end());
            const size_t quarter = windows.size() / 4;
            float quietGain = 0.0f, loudGain = 0.0f;
            for (size_t i = 0; i < quarter; ++i)
            {
                quietGain += windows[i].second / (float)quarter;
                loudGain += windows[windows.size() - 1 - i].second / (float)quarter;
            }
            const float difference = juce::Decibels::gainToDecibels(quietGain / loudGain);
            logMessage("whisper: quietest quarter gets " + juce::String(difference, 1) + " dB more gain than the loudest; loudest " +
                       juce::String(juce::Decibels::gainToDecibels(loudest), 1) + " dBFS");
            expectGreaterThan(difference, -1.0f, "soft parts are never turned down (no gate, no chopping)");
            expectGreaterThan(juce::Decibels::gainToDecibels(loudest), -30.0f, "brought up to a usable level");
        }

        beginTest("Output never passes 0 dBFS, even with Output +24 dB and the Limiter off");
        {
            auto p = makeProcessor();
            p->getPresetManager().loadFactoryPreset("Karaoke Star");
            setParam(*p, pid::limOn, 0.0f);
            setParam(*p, pid::outGain, 24.0f);
            p->getChain().requestSnap();
            auto loud = sine(220.0, 0.5f, 48000);
            processSignal(*p, loud);
            expectLessOrEqual(loud.getMagnitude(0, loud.getNumSamples()), 1.0f);

            // The guards themselves, fed bad values directly
            const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
            expect(!dsp::isNumber(nan) && !dsp::isNumber(inf) && !dsp::isNumber(-inf) && dsp::isNumber(0.0f) && dsp::isNumber(-1.0e30f));
            float left[4] = {0.1f, nan, -inf, 0.2f}, right[4] = {inf, 0.3f, 0.0f, nan};
            float* bad[2] = {left, right};
            float envelope = 0.0f;
            expect(!dsp::safetyLimit(bad, 2, 4, envelope, 0.999f), "notices NaN and infinity coming out of the chain");
            expectEquals(dsp::scrubNonFinite(bad, 2, 4), 4);
            expect(dsp::safetyLimit(bad, 2, 4, envelope, 0.999f) && left[0] == 0.1f && right[1] == 0.3f, "clean audio is left alone");
        }

        beginTest("Harshness tamer: dips 2-5 kHz only when a belted note gets harsh");
        {
            auto voice = [&](bool harsh, float level) {
                juce::AudioBuffer<float> b(2, (int)sr);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    double v = 0.0;
                    for (int h = 1; h <= 40; ++h)
                    {
                        const double f = 220.0 * h;
                        double a = 1.0 / h;
                        if (harsh && f > 2400.0 && f < 4200.0)
                            a *= 6.0; // about +15 dB
                        v += a * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                    }
                    b.setSample(0, i, level * (float)v);
                    b.setSample(1, i, level * (float)v);
                }
                return b;
            };
            auto tameCut = [&](bool harsh, float level) {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Equalizer);
                setParam(*p, pid::eqHighGain, 0.0f);
                setParam(*p, pid::eqLowCut, 20.0f);
                setParam(*p, pid::eqTameRange, 6.0f);
                p->getChain().requestSnap();
                auto b = voice(harsh, level);
                processSignal(*p, b);
                return p->getChain().getActivity().tameReductionDb;
            };
            const float normalLoud = tameCut(false, 0.15f), normalQuiet = tameCut(false, 0.02f);
            const float harshLoud = tameCut(true, 0.15f), harshQuiet = tameCut(true, 0.02f);
            logMessage("tame cut: normal " + juce::String(normalLoud, 2) + " / " + juce::String(normalQuiet, 2) + " dB, belted " +
                       juce::String(harshLoud, 2) + " / " + juce::String(harshQuiet, 2) + " dB (loud / quiet)");
            expectLessThan(normalLoud, 1.0f, "a normal voice is left alone");
            expectGreaterThan(harshLoud, 3.0f, "a harsh belt is tamed");
            expectWithinAbsoluteError(harshQuiet, harshLoud, 1.0f, "works the same at any level");

            // Max Cut 0 = off
            auto a = makeProcessor(), b = makeProcessor();
            onlyModule(*a, ModuleId::Equalizer);
            onlyModule(*b, ModuleId::Equalizer);
            setParam(*b, pid::eqTameThreshold, -30.0f);
            auto x = voice(true, 0.15f), y = voice(true, 0.15f);
            processSignal(*a, x);
            processSignal(*b, y);
            float diff = 0.0f;
            for (int i = 0; i < x.getNumSamples(); ++i)
                diff = std::max(diff, std::abs(x.getSample(0, i) - y.getSample(0, i)));
            expectEquals(diff, 0.0f, "off means off");
        }

        beginTest("Plosive tamer: takes the thump out of \"p\" and \"b\", leaves sung low notes alone");
        {
            // A sung voice with optional "p" thumps
            auto voice = [&](double f0, bool pops) {
                juce::AudioBuffer<float> b(2, (int)sr * 2);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    double v = 0.0;
                    for (int h = 1; h <= 30; ++h)
                        v += std::sin(2.0 * juce::MathConstants<double>::pi * f0 * h * t) / h;
                    v *= 0.05;
                    const double sincePop = std::fmod(t, 0.5);
                    if (pops && t > 0.25)
                        v += 0.6 * std::exp(-sincePop / 0.025) * std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * sincePop);
                    b.setSample(0, i, (float)v);
                    b.setSample(1, i, (float)v);
                }
                return b;
            };
            auto lowDb = [&](const juce::AudioBuffer<float>& b, double from, double to) {
                // Level below 100 Hz from..to seconds after each pop
                dsp::Biquad lp;
                lp.c = dsp::biquad::lowpass(sr, 100.0, 0.707);
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float y = lp.process(b.getSample(0, i), 0);
                    const double t = i / sr;
                    if (t > 0.5 && std::fmod(t, 0.5) >= from && std::fmod(t, 0.5) < to)
                    {
                        sum += (double)y * y;
                        ++count;
                    }
                }
                return 10.0f * (float)std::log10(sum / juce::jmax(1, count) + 1.0e-20);
            };
            auto run = [&](double f0, bool pops, float amount) {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Equalizer);
                setParam(*p, pid::eqLowCut, 20.0f);
                setParam(*p, pid::eqHighGain, 0.0f);
                setParam(*p, pid::eqPlosive, amount);
                p->getChain().requestSnap();
                auto b = voice(f0, pops);
                processSignal(*p, b);
                return b;
            };

            const auto dry = run(220.0, true, 0.0f), tamed = run(220.0, true, 100.0f);
            const float whole = lowDb(dry, 0.0, 0.08) - lowDb(tamed, 0.0, 0.08);
            const float boom = lowDb(dry, 0.01, 0.08) - lowDb(tamed, 0.01, 0.08);
            logMessage("thump below 100 Hz cut by " + juce::String(whole, 1) + " dB overall, boom after the first 10 ms by " +
                       juce::String(boom, 1) + " dB (zero latency: the first few ms can't be caught)");
            expectGreaterThan(whole, 5.0f, "the thump is cut");
            expectGreaterThan(boom, 12.0f, "the boom is gone");

            // Sung low notes don't trigger it
            for (double f0 : {98.0, 130.0, 220.0})
            {
                auto off = run(f0, false, 0.0f), on = run(f0, false, 100.0f);
                // Level over every 4 whole cycles of the note, including its sudden start
                const int step = (int)std::lround(4.0 * sr / f0);
                float worst = 0.0f;
                for (int start = 0; start + step <= off.getNumSamples(); start += step)
                    worst = std::max(worst, std::abs(rmsDb(on, start, step) - rmsDb(off, start, step)));
                logMessage("sung " + juce::String(f0) + " Hz: biggest level change " + juce::String(worst, 3) + " dB");
                expectLessThan(worst, 0.1f, "sung notes untouched");
            }
        }

        beginTest("Set up my voice silences a laptop fan between phrases (the Gate reacts to the noise's peaks)");
        {
            // Fan
            juce::Random rng(11);
            dsp::Biquad fanTone;
            fanTone.c = dsp::biquad::lowpass(sr, 1500.0, 0.707);
            auto fan = [&] { return fanTone.process((rng.nextFloat() * 2.0f - 1.0f) * 0.0105f, 0); };
            auto mic = [&](double seconds, bool voice, double startT) {
                juce::AudioBuffer<float> b(2, (int)(sr * seconds));
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = startT + i / sr;
                    float x = fan();
                    if (voice)
                        x += (float)(0.06 * std::pow(std::sin(juce::MathConstants<double>::pi * 2.0 * t), 2.0) *
                                     (std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t) +
                                      0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * t)));
                    b.setSample(0, i, x);
                    b.setSample(1, i, x);
                }
                return b;
            };
            auto fanAfter = [&](bool setUp) {
                auto p = makeProcessor();
                p->getPresetManager().loadFactoryPreset("Clean Vocal");
                if (setUp)
                {
                    juce::AudioBuffer<float> m(2, (int)(sr * 11.5));
                    auto quiet = mic(3.3, false, 0.0), voice = mic(8.2, true, 3.3);
                    for (int c = 0; c < 2; ++c)
                    {
                        m.copyFrom(c, 0, quiet, c, 0, quiet.getNumSamples());
                        m.copyFrom(c, quiet.getNumSamples(), voice, c, 0, voice.getNumSamples());
                    }
                    p->getVoiceAnalyzer().start();
                    processSignal(*p, m);
                    const auto r = VoiceAnalyzer::evaluate(p->getVoiceAnalyzer().getMeasurements());
                    logMessage("fan " + juce::String(r.noiseDb, 1) + " dB (peaks " + juce::String(r.noisePeakDb, 1) + "), voice " +
                               juce::String(r.voiceDb, 1) + " dB -> input " + juce::String(r.inputGainDb, 1) + " dB, gate " +
                               juce::String(r.gateThresholdDb, 1) + " dB");
                    p->applyVoiceSetup(r);
                }
                auto sing = mic(4.0, true, 0.0);
                processSignal(*p, sing);
                const float voiceRms = sing.getRMSLevel(0, (int)sr * 2, (int)sr * 2);
                auto pause = mic(3.0, false, 0.0);
                processSignal(*p, pause);
                const float fanRms = pause.getRMSLevel(0, (int)sr, (int)sr * 2);
                return std::pair<float, float>{juce::Decibels::gainToDecibels(voiceRms), juce::Decibels::gainToDecibels(fanRms)};
            };
            const auto [voiceBefore, fanBefore] = fanAfter(false);
            const auto [voiceSetUp, fanSetUp] = fanAfter(true);
            logMessage("fan between phrases: " + juce::String(fanBefore, 1) + " dB with defaults, " + juce::String(fanSetUp, 1) +
                       " dB after Set up my voice; voice " + juce::String(voiceBefore, 1) + " / " + juce::String(voiceSetUp, 1) + " dB");
            // Setup also raises a quiet mic, so compare the fan against the voice
            expectGreaterThan((voiceSetUp - fanSetUp) - (voiceBefore - fanBefore), 10.0f, "the fan is gated after setup");
            expectGreaterThan(voiceSetUp, voiceBefore - 3.0f, "the voice is not cut");
        }

        beginTest("Set up my voice: measures room, level and \"s\" frequency, and survives preset changes");
        {
            auto p = makeProcessor();
            const int n = (int)sr * 12;
            juce::AudioBuffer<float> mic(2, n);
            juce::Random rng(7);
            dsp::Biquad sFilter;
            sFilter.c = dsp::biquad::bandPass(sr, 7800.0, 4.0);
            const int quietEnd = (int)(sr * 3.3); // a little slack after the 3 s quiet part
            for (int i = 0; i < n; ++i)
            {
                const float noise = (rng.nextFloat() * 2.0f - 1.0f) * 0.0005f; // about -71 dBFS RMS
                float x = noise;
                if (i >= quietEnd)
                {
                    const double t = (double)i / sr;
                    const bool sSound = std::fmod(t, 1.0) > 0.8; // 200 ms "s" every second
                    if (sSound)
                        x += sFilter.process((rng.nextFloat() * 2.0f - 1.0f) * 0.25f, 0);
                    else
                        x += 0.045f * (float)(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t) +
                                              0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * t)); // about -27 dBFS
                }
                mic.setSample(0, i, x);
                mic.setSample(1, i, x);
            }

            p->getVoiceAnalyzer().start();
            processSignal(*p, mic);
            expect(p->getVoiceAnalyzer().getPhase() == VoiceAnalyzer::Phase::Done, "measurement finished");

            const auto r = VoiceAnalyzer::evaluate(p->getVoiceAnalyzer().getMeasurements());
            logMessage("noise " + juce::String(r.noiseDb) + " dB, voice " + juce::String(r.voiceDb) + " dB, peak " +
                       juce::String(r.peakDb) + " dB -> input " + juce::String(r.inputGainDb) + " dB, gate " +
                       juce::String(r.gateThresholdDb) + " dB, s at " + juce::String(r.sibilantHz) + " Hz");
            expect(r.heardVoice);
            expectWithinAbsoluteError(r.noiseDb, -71.0f, 3.0f, "room noise");
            expectWithinAbsoluteError(r.inputGainDb, 7.0f, 3.0f, "input gain brings the voice to about -20 dBFS");
            expect(r.gateThresholdDb > r.noiseDb + r.inputGainDb + 5.0f, "gate above the room noise");
            expect(r.gateThresholdDb < r.voiceDb + r.inputGainDb - 10.0f, "gate well below the voice");
            expectEquals(r.sibilantHz, 7800.0f, "found the s frequency");
            expect(!r.clipped && !r.noisy && !r.veryQuiet);

            p->applyVoiceSetup(r);
            expectWithinAbsoluteError(getParam(*p, pid::inGain), r.inputGainDb, 0.01f);
            expectWithinAbsoluteError(getParam(*p, pid::dsFreq), 7800.0f, 1.0f);

            auto voiceDir = juce::File::createTempFile("stagemic-voice");
            PresetManager presets(*p, voiceDir);
            presets.loadFactoryPreset("Radio Voice");
            expectWithinAbsoluteError(getParam(*p, pid::inGain), r.inputGainDb, 0.01f, "mic settings stay across presets");
            expectWithinAbsoluteError(getParam(*p, pid::gateThreshold), r.gateThresholdDb, 0.01f);
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 280.0f, 0.5f, "the preset's sound still loads");

            juce::MemoryBlock saved;
            p->getStateInformation(saved);
            auto q = makeProcessor();
            q->setStateInformation(saved.getData(), (int)saved.getSize());
            expect(q->hasVoiceSetup(), "voice setup is remembered by OBS");

            // Silence only
            auto silent = makeProcessor();
            juce::AudioBuffer<float> nothing(2, n);
            nothing.clear();
            silent->getVoiceAnalyzer().start();
            processSignal(*silent, nothing);
            expect(!VoiceAnalyzer::evaluate(silent->getVoiceAnalyzer().getMeasurements()).heardVoice, "no voice, no changes");
        }

        beginTest("Simple / Advanced mode is remembered, and Simple is the default");
        {
            auto p = makeProcessor();
            expect(!p->isAdvancedMode(), "Simple by default");
            p->setAdvancedMode(true);
            juce::MemoryBlock saved;
            p->getStateInformation(saved);
            auto q = makeProcessor();
            q->setStateInformation(saved.getData(), (int)saved.getSize());
            expect(q->isAdvancedMode(), "Advanced is remembered");
        }

        beginTest("CPU: the whole chain is lightweight");

        auto p = makeProcessor();
        for (const auto& d : moduleDescriptors())
            setParam(*p, d.enableParam, 1.0f);
        p->getChain().requestSnap();

        // Best of three runs
        constexpr int seconds = 8;
        auto q = makeProcessor();
        double percentOfOneCore = 1.0e9, defaultPercent = 1.0e9;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            auto signal = sine(220.0, 0.4f, (int)sr * seconds);
            const auto start = std::chrono::steady_clock::now();
            processSignal(*p, signal);
            percentOfOneCore = std::min(percentOfOneCore,
                                        100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / seconds);

            auto defaults = sine(220.0, 0.4f, (int)sr * seconds);
            const auto start2 = std::chrono::steady_clock::now();
            processSignal(*q, defaults);
            defaultPercent = std::min(defaultPercent,
                                      100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start2).count() / seconds);
        }

        logMessage("CPU at 48 kHz stereo: every module on = " + juce::String(percentOfOneCore, 2) +
                   "% of one core, default sound = " + juce::String(defaultPercent, 2) + "%");

        // Target is ~2%; generous limit for noisy CI machines
        expectLessThan(percentOfOneCore, 10.0, "full chain CPU");
    }
};

static ChainTests chainTests;

} // namespace stagemic::test
