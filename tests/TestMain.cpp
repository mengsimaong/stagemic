// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/StageMicProcessor.h"
#include "core/presets/PresetManager.h"
#include "ui/StageMicEditor.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{
/** Developer aid: STAGEMIC_SNAPSHOT_DIR=<folder> renders the editor to PNG files. */
void writeEditorSnapshots(const juce::File& dir)
{
    dir.createDirectory();
    for (const auto& [preset, tab] : {std::pair<juce::String, int>{"Clean Vocal", 0}, {"Karaoke Star", 0}, {"Karaoke Star", 1}, {"Karaoke Star", 2}, {"Karaoke Star", 3}, {"Karaoke Star", 4}})
    {
        stagemic::StageMicProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 1024);
        processor.prepareToPlay(48000.0, 1024);
        processor.getPresetManager().loadFactoryPreset(preset);
        if (tab == 1)
            processor.getParameters().getParameter(stagemic::pid::mbOn)->setValueNotifyingHost(1.0f);
        if (preset == "Karaoke Star" && tab == 0)
        {
            // Show the harshness tamer working.
            auto* range = processor.getParameters().getParameter(stagemic::pid::eqTameRange);
            range->setValueNotifyingHost(range->convertTo0to1(6.0f));
            auto* thr = processor.getParameters().getParameter(stagemic::pid::eqTameThreshold);
            thr->setValueNotifyingHost(thr->convertTo0to1(-12.0f));
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
        auto* stageMicEditor = dynamic_cast<stagemic::StageMicEditor*>(editor.get());
        if (tab == 4)
        {
            // Tune: E major, "Pop" speed.
            auto set = [&](const char* id, float v) {
                auto* p = processor.getParameters().getParameter(id);
                p->setValueNotifyingHost(p->convertTo0to1(v));
            };
            set(stagemic::pid::tuneOn, 1.0f);
            set(stagemic::pid::tuneKey, 4.0f);
            set(stagemic::pid::tuneScale, 1.0f);
            set(stagemic::pid::tuneSpeed, 12.0f);
        }
        if (stageMicEditor != nullptr)
            stageMicEditor->showDisplayTab(tab == 1 ? 1 : (tab == 4 ? 2 : 0));
        if (stageMicEditor != nullptr && (tab == 2 || tab == 3))
        {
            stageMicEditor->getVoiceSetup().open();
            if (tab == 3)
                stageMicEditor->getVoiceSetup().startMeasuring();
        }

        // Feed a voice-like signal (220 Hz with harmonics + a little breath noise)
        // so the meters and spectrum show something realistic.
        juce::Random rng(7);
        juce::AudioBuffer<float> block(2, 480);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const int blocks = tab == 3 ? 1250 : (tab == 4 ? 360 : 200); // tab 3: a whole voice measurement (3 s quiet + 8 s voice)
        for (int b = 0; b < blocks; ++b)
        {
            const bool quiet = tab == 3 && b < 340;
            for (int i = 0; i < 480; ++i)
            {
                if (quiet)
                {
                    const float n = 0.0005f * (rng.nextFloat() * 2.0f - 1.0f);
                    block.setSample(0, i, n);
                    block.setSample(1, i, n);
                    continue;
                }
                float v = 0.0f;
                for (int h = 1; h <= 24; ++h)
                    v += (float)std::sin(phase * h) / (float)(h * h) * (h == 3 || h == 4 ? 2.5f : 1.0f);
                v = 0.35f * v + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
                double f0 = 220.0;
                if (tab == 4)
                {
                    // A little melody, sung slightly off with vibrato and a breath between phrases.
                    const double t = (b * 480 + i) / 48000.0;
                    const double notes[] = {64.3, 66.2, 68.35, 68.1, 71.25, 69.3, 68.2};
                    const double midi = notes[juce::jmin(6, (int)(t / 0.5))] + 0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 5.5 * t);
                    f0 = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
                    if (std::fmod(t, 1.5) > 1.38)
                        v = 0.0f;
                }
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / 48000.0;
                block.setSample(0, i, v);
                block.setSample(1, i, v);
            }
            processor.processBlock(block, midi);
            if (stageMicEditor != nullptr && b % 4 == 0)
                stageMicEditor->refreshFromProcessor(true);
        }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        auto file = dir.getChildFile("editor-" + preset.replaceCharacter(' ', '-').toLowerCase() + (tab == 1 ? "-multiband" : tab == 2 ? "-voice-setup" : tab == 3 ? "-voice-result" : tab == 4 ? "-tune" : "") + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
        std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (const auto dir = juce::SystemStats::getEnvironmentVariable("STAGEMIC_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        writeEditorSnapshots(juce::File(dir));
        return 0;
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("StageMic");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;

    std::printf("\n%s: %d test group(s), %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", runner.getNumResults(), failures);
    return failures == 0 ? 0 : 1;
}
