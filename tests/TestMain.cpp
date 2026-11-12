// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/StageMicProcessor.h"
#include "core/presets/PresetManager.h"
#include "ui/StageMicEditor.h"
#include "ui/NeonCat.h"

#include <thread>
#include <chrono>
#include <ctime>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{
/** Developer aid: STAGEMIC_SNAPSHOT_DIR=<folder> renders the editor to PNG files. */
void writeEditorSnapshots(const juce::File& dir)
{
    dir.createDirectory();
    for (const auto& [preset, tab] : {std::pair<juce::String, int>{"Clean Vocal", 0}, {"Karaoke Star", 0}, {"Karaoke Star", 1}, {"Karaoke Star", 2}, {"Karaoke Star", 3}, {"Karaoke Star", 4}, {"Pop Tune", 5}})
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

        processor.setAdvancedMode(tab != 5); // tab 5: Simple mode
        if (tab == 5)
        {
            auto set = [&](const char* id, float v) {
                auto* p = processor.getParameters().getParameter(id);
                p->setValueNotifyingHost(p->convertTo0to1(v));
            };
            set(stagemic::pid::tuneKey, 4.0f);
            set(stagemic::pid::tuneScale, 1.0f);
            set(stagemic::pid::tuneVoice, 0.0f);
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
        const int blocks = tab == 3 ? 1250 : (tab == 4 || tab == 5 ? 360 : 200); // tab 3: a whole voice measurement (3 s quiet + 8 s voice)
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
                if (tab == 4 || tab == 5)
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
        auto file = dir.getChildFile("editor-" + preset.replaceCharacter(' ', '-').toLowerCase() + (tab == 1 ? "-multiband" : tab == 2 ? "-voice-setup" : tab == 3 ? "-voice-result" : tab == 4 ? "-tune" : tab == 5 ? "-simple" : "") + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
        std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
}
} // namespace

/** Developer aid: STAGEMIC_UI_LIVE=1 opens the window on screen for 10 s with audio
    running in real time on another thread, and reports the CPU each part used. */
void liveUi()
{
    for (bool advanced : {false, true})
        for (bool open : {false, true})
        {
            stagemic::StageMicProcessor processor;
            processor.setRateAndBufferSizeDetails(48000.0, 480);
            processor.prepareToPlay(48000.0, 480);
            processor.getPresetManager().loadFactoryPreset("Pop Tune");
            processor.setAdvancedMode(advanced);
            std::unique_ptr<juce::AudioProcessorEditor> editor;
            if (open)
            {
                editor.reset(processor.createEditorAndMakeActive());
                editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
                editor->setVisible(true);
            }
            std::atomic<bool> stop{false};
            std::atomic<double> audioMs{0.0};
            std::thread audio([&] {
                juce::AudioBuffer<float> block(2, 480);
                juce::MidiBuffer midi;
                double phase = 0.0;
                auto next = std::chrono::steady_clock::now();
                while (!stop)
                {
                    for (int i = 0; i < 480; ++i)
                    {
                        const float v = 0.2f * (float)std::sin(phase);
                        phase += 2.0 * juce::MathConstants<double>::pi * 330.0 / 48000.0;
                        block.setSample(0, i, v);
                        block.setSample(1, i, v);
                    }
                    const auto t0 = std::chrono::steady_clock::now();
                    processor.processBlock(block, midi);
                    audioMs = audioMs + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    next += std::chrono::microseconds(10000);
                    std::this_thread::sleep_until(next);
                }
            });
            const double seconds = 10.0;
            const auto cpu0 = std::clock();
            juce::MessageManager::getInstance()->runDispatchLoopUntil((int)(seconds * 1000));
            const double processCpu = (double)(std::clock() - cpu0) / CLOCKS_PER_SEC;
            stop = true;
            audio.join();
            const double audioCpu = audioMs / 1000.0;
            std::printf("%-8s window %-6s: audio %.1f%% of one core, window drawing %.1f%%\n", advanced ? "Advanced" : "Simple",
                        open ? "open" : "closed", 100.0 * audioCpu / seconds, 100.0 * std::max(0.0, processCpu - audioCpu) / seconds);
            editor.reset();
        }
}

/** Developer aid: STAGEMIC_UI_BENCH=1 measures how long the window takes to draw. */
void benchmarkUi()
{
    for (bool advanced : {false, true})
    {
        stagemic::StageMicProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 1024);
        processor.prepareToPlay(48000.0, 1024);
        processor.getPresetManager().loadFactoryPreset("Pop Tune");
        processor.setAdvancedMode(advanced);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
        auto* ed = dynamic_cast<stagemic::StageMicEditor*>(editor.get());
        juce::Image img(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        constexpr int frames = 60;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < frames; ++i)
        {
            juce::Graphics g(img);
            ed->refreshFromProcessor(true);
            editor->paintEntireComponent(g, true);
        }
        const double full = (juce::Time::getMillisecondCounterHiRes() - t0) / frames;
        // The cat alone: the small area it repaints each frame.
        juce::Image strip(juce::Image::ARGB, 120, 32, true);
        t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 300; ++i)
        {
            juce::Graphics g(strip);
            g.fillAll(juce::Colours::black);
            stagemic::neoncat::paintWalking(g, 10.0f, 30.0f, 29.0f, (float)i * 0.32f, juce::Colours::hotpink);
        }
        const double cat = (juce::Time::getMillisecondCounterHiRes() - t0) / 300.0;
        std::printf("%s: whole window %.2f ms per frame (%.1f%% of one core at 30 fps), cat %.3f ms per frame (%.2f%%)\n",
                    advanced ? "Advanced" : "Simple", full, full * 30.0 / 10.0, cat, cat * 30.0 / 10.0);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (juce::SystemStats::getEnvironmentVariable("STAGEMIC_UI_LIVE", {}).isNotEmpty())
    {
        liveUi();
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable("STAGEMIC_UI_BENCH", {}).isNotEmpty())
    {
        benchmarkUi();
        return 0;
    }
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
