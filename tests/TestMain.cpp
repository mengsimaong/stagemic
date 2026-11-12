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
    for (const auto& [preset, tab] : {std::pair<juce::String, int>{"Clean Vocal", 0}, {"Karaoke Star", 0}, {"Karaoke Star", 1}})
    {
        stagemic::StageMicProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 1024);
        processor.prepareToPlay(48000.0, 1024);
        processor.getPresetManager().loadFactoryPreset(preset);
        if (tab == 1)
            processor.getParameters().getParameter(stagemic::pid::mbOn)->setValueNotifyingHost(1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
        auto* stageMicEditor = dynamic_cast<stagemic::StageMicEditor*>(editor.get());
        if (stageMicEditor != nullptr)
            stageMicEditor->showDisplayTab(tab);

        // Feed a voice-like signal (220 Hz with harmonics + a little breath noise)
        // so the meters and spectrum show something realistic.
        juce::Random rng(7);
        juce::AudioBuffer<float> block(2, 480);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 480; ++i)
            {
                float v = 0.0f;
                for (int h = 1; h <= 24; ++h)
                    v += (float)std::sin(phase * h) / (float)(h * h) * (h == 3 || h == 4 ? 2.5f : 1.0f);
                v = 0.35f * v + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                block.setSample(0, i, v);
                block.setSample(1, i, v);
            }
            processor.processBlock(block, midi);
            if (stageMicEditor != nullptr && b % 4 == 0)
                stageMicEditor->refreshFromProcessor(true);
        }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        auto file = dir.getChildFile("editor-" + preset.replaceCharacter(' ', '-').toLowerCase() + (tab == 1 ? "-multiband" : "") + ".png");
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
