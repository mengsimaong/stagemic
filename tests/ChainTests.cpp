// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 2: built-in effects, presets, real-time safety, zero latency and CPU use.
#include "AllocationGuard.h"
#include "TestHelpers.h"
#include "core/presets/PresetManager.h"

#include <chrono>

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
        testEffectsDoTheirJob();
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

        beginTest("Gate: turns quiet noise down by its range, leaves singing alone");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Gate);
            setParam(*p, pid::gateThreshold, -50.0f);
            setParam(*p, pid::gateRange, 30.0f);

            auto hiss = sine(500.0, dsp::dbToGain(-70.0f), 96000);
            processSignal(*p, hiss);
            expectWithinAbsoluteError(rmsDb(hiss, 72000, 24000) - (-70.0f - 3.01f), -30.0f, 1.0f);

            auto voice = sine(500.0, dsp::dbToGain(-20.0f), 48000);
            processSignal(*p, voice);
            expectWithinAbsoluteError(rmsDb(voice, 24000, 24000) - (-20.0f - 3.01f), 0.0f, 0.2f);
        }

        beginTest("Compressor: steady level follows threshold + (input - threshold) / ratio");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Compressor);
            setParam(*p, pid::compThreshold, -24.0f);
            setParam(*p, pid::compRatio, 4.0f);
            setParam(*p, pid::compMakeup, 0.0f);

            // Peak -6 dBFS: 18 dB over the threshold -> 4.5 dB over after 4:1 -> 13.5 dB of reduction.
            auto loud = sine(300.0, dsp::dbToGain(-6.0f), 96000);
            processSignal(*p, loud);
            const float peakOut = juce::Decibels::gainToDecibels(loud.getMagnitude(0, 72000, 24000));
            expectWithinAbsoluteError(peakOut, -19.5f, 1.5f);
            expectGreaterThan(p->getChain().getCompressorGainReductionDb(), 10.0f);
        }

        beginTest("De-esser: tames loud 's' sounds, leaves the body of the voice alone");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::DeEsser);
            setParam(*p, pid::dsThreshold, -30.0f);
            setParam(*p, pid::dsRange, 12.0f);

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
                expectWithinAbsoluteError(rmsDb(tone, 24000, 24000) - juce::Decibels::gainToDecibels(0.1f * 0.70710678f),
                                          0.0f, 0.1f, "at " + juce::String(f) + " Hz");
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
        beginTest("Zero latency: every module responds on the very first sample");
        for (const auto& d : moduleDescriptors())
        {
            auto p = makeProcessor();
            onlyModule(*p, d.id);
            setParam(*p, pid::gateThreshold, -80.0f);   // gate open
            setParam(*p, pid::compThreshold, 0.0f);     // no compression
            setParam(*p, pid::dsThreshold, 0.0f);
            p->getChain().requestSnap();

            juce::AudioBuffer<float> impulse(2, 480);
            impulse.clear();
            impulse.setSample(0, 0, 0.5f);
            impulse.setSample(1, 0, 0.5f);
            juce::MidiBuffer midi;
            p->processBlock(impulse, midi);

            expectGreaterThan(std::abs(impulse.getSample(0, 0)), 0.05f,
                              juce::String(d.displayName) + " must not delay the voice");
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

            // A preset from an older version that doesn't know every module still loads sensibly.
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
        beginTest("CPU: the whole chain is lightweight");

        auto p = makeProcessor();
        for (const auto& d : moduleDescriptors())
            setParam(*p, d.enableParam, 1.0f);
        p->getChain().requestSnap();

        constexpr int seconds = 20;
        auto signal = sine(220.0, 0.4f, (int)sr * seconds);

        const auto start = std::chrono::steady_clock::now();
        processSignal(*p, signal);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double percentOfOneCore = 100.0 * elapsed / seconds;

        auto q = makeProcessor();
        auto defaults = sine(220.0, 0.4f, (int)sr * seconds);
        const auto start2 = std::chrono::steady_clock::now();
        processSignal(*q, defaults);
        const double defaultPercent = 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start2).count() / seconds;

        logMessage("CPU at 48 kHz stereo: every module on = " + juce::String(percentOfOneCore, 2)
                   + "% of one core, default sound = " + juce::String(defaultPercent, 2) + "%");

        // Target is ~2%; the limit is generous because shared CI machines are noisy.
        expectLessThan(percentOfOneCore, 10.0, "full chain CPU");
    }
};

static ChainTests chainTests;

} // namespace stagemic::test
