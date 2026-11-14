// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/StageMicProcessor.h"
#include "core/presets/PresetManager.h"
#include "ui/StageMicEditor.h"
#include "ui/NeonCat.h"
#include "ui/EditorWindow.h"

#include <thread>
#include <map>
#include <functional>
#include <typeinfo>
#include <time.h>
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
    for (const auto& [preset, tab] : {std::pair<juce::String, int>{"Clean Vocal", 0}, {"Karaoke Star", 0}, {"Karaoke Star", 1}, {"Karaoke Star", 2}, {"Karaoke Star", 3}, {"Karaoke Star", 4}, {"Pop Tune", 5}, {"", 6}})
    {
        stagemic::StageMicProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 1024);
        processor.prepareToPlay(48000.0, 1024);
        if (preset.isNotEmpty()) // tab 6: a brand-new filter, exactly as a first-time user sees it
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

        processor.setAdvancedMode(tab < 5); // tabs 5 and 6: Simple mode
        if (tab != 6)
            processor.setVoiceSetupOffered(); // only the first-open shot shows the automatic welcome
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
        auto file = dir.getChildFile((tab == 6 ? juce::String("editor-first-open") : "editor-" + preset.replaceCharacter(' ', '-').toLowerCase() + (tab == 1 ? "-multiband" : tab == 2 ? "-voice-setup" : tab == 3 ? "-voice-result" : tab == 4 ? "-tune" : tab == 5 ? "-simple" : "")) + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
        std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
}
} // namespace

/** Developer aid: STAGEMIC_COVER=<file.png> draws the itch.io cover image (1260 x 1000). */
void writeCover(const juce::File& file)
{
    // A screenshot of the Simple window (Pop Tune, a short melody on the pitch graph).
    stagemic::StageMicProcessor processor;
    processor.setVoiceSetupOffered(); // not the first-time welcome
    processor.setRateAndBufferSizeDetails(48000.0, 1024);
    processor.prepareToPlay(48000.0, 1024);
    processor.getPresetManager().loadFactoryPreset("Pop Tune");
    processor.setAdvancedMode(false);
    auto set = [&](const char* id, float v) {
        auto* p = processor.getParameters().getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set(stagemic::pid::tuneKey, 4.0f);
    set(stagemic::pid::tuneScale, 1.0f);
    set(stagemic::pid::tuneVoice, 0.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    auto* ed = dynamic_cast<stagemic::StageMicEditor*>(editor.get());
    juce::AudioBuffer<float> block(2, 480);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int b = 0; b < 360; ++b)
    {
        for (int i = 0; i < 480; ++i)
        {
            const double t = (b * 480 + i) / 48000.0;
            const double notes[] = {64.3, 66.2, 68.35, 68.1, 71.25, 69.3, 68.2};
            const double midiNote = notes[juce::jmin(6, (int)(t / 0.5))] + 0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 5.5 * t);
            float v = 0.0f;
            for (int h = 1; h <= 24; ++h)
                v += (float)std::sin(phase * h) / (float)(h * h) * (h == 3 || h == 4 ? 2.5f : 1.0f);
            v *= 0.35f;
            if (std::fmod(t, 1.5) > 1.38)
                v = 0.0f;
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0) / 48000.0;
            block.setSample(0, i, v);
            block.setSample(1, i, v);
        }
        processor.processBlock(block, midi);
        if (ed != nullptr && b % 4 == 0)
            ed->refreshFromProcessor(true);
    }
    const auto shot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);

    constexpr int W = 1260, H = 1000;
    juce::Image cover(juce::Image::ARGB, W, H, true);
    juce::Graphics g(cover);
    // Background: deep night purple with a soft pink glow behind the logo.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1022), 0.0f, 0.0f, juce::Colour(0xff0b0b12), 0.0f, (float)H, false));
    g.fillAll();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x55ff4f9a), W * 0.5f, 250.0f, juce::Colour(0x00ff4f9a), W * 0.5f + 520.0f, 250.0f, true));
    g.fillRect(0, 0, W, H);

    // Logo: big neon cat head and the name.
    const auto accent = juce::Colour(0xffff4f9a);
    stagemic::neoncat::paintHead(g, juce::Rectangle<float>(W * 0.5f - 300.0f, 70.0f, 190.0f, 170.0f), accent);
    g.setColour(juce::Colour(0xffff8cbf));
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold).withKerningFactor(0.35f));
    g.drawText("PAOPOA", juce::Rectangle<int>(W / 2 - 90, 92, 440, 40), juce::Justification::centredLeft);
    g.setColour(accent);
    g.setFont(juce::FontOptions(92.0f, juce::Font::bold));
    g.drawText("StageMic", juce::Rectangle<int>(W / 2 - 96, 124, 520, 110), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffe9e9f0));
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText("Your vocal studio inside OBS", juce::Rectangle<int>(0, 268, W, 44), juce::Justification::centred);
    g.setColour(juce::Colour(0xff9a96a8));
    g.setFont(juce::FontOptions(24.0f));
    g.drawText(juce::String(juce::CharPointer_UTF8("Clean  \u00b7  Polished  \u00b7  Auto-tuned  \u00b7  Fun voices  \u00b7  Free")),
               juce::Rectangle<int>(0, 318, W, 34), juce::Justification::centred);

    // The window, slightly smaller, with a glow, fading into the bottom.
    const float shotW = 1020.0f, shotH = shotW * (float)shot.getHeight() / (float)shot.getWidth();
    const auto shotArea = juce::Rectangle<float>((W - shotW) * 0.5f, 380.0f, shotW, shotH);
    g.setColour(accent.withAlpha(0.18f));
    for (float grow : {18.0f, 10.0f, 4.0f})
        g.fillRoundedRectangle(shotArea.expanded(grow), 16.0f + grow);
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(shotArea, 14.0f);
    g.reduceClipRegion(clip);
    g.setColour(juce::Colour(0xff101116)); // the window's own background, so the glow doesn't show through
    g.fillRect(shotArea);
    g.drawImage(shot, shotArea, juce::RectanglePlacement::fillDestination);
    g.restoreState();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x000b0b12), 0.0f, H - 300.0f, juce::Colour(0xff0b0b12), 0.0f, H - 80.0f, false));
    g.fillRect(0, H - 300, W, 300);

    // The walking cat at the bottom.
    g.addTransform(juce::AffineTransform::scale(1.6f, 1.6f, W * 0.5f, H - 30.0f));
    stagemic::neoncat::paintWalking(g, W * 0.5f - 50.0f, H - 30.0f, 52.0f, 1.2f, accent);
    g.addTransform(juce::AffineTransform::scale(1.0f / 1.6f, 1.0f / 1.6f, W * 0.5f, H - 30.0f));
    g.setColour(juce::Colour(0xff9a96a8));
    g.setFont(juce::FontOptions(20.0f));
    g.drawText(juce::String(juce::CharPointer_UTF8("Windows  \u00b7  OBS Studio 31+")), juce::Rectangle<int>(W - 330, H - 52, 300, 30),
               juce::Justification::centredRight);

    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(cover, out);
    std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
}

/** Developer aid: STAGEMIC_UI_LIVE=1 opens the window on screen for 10 s with audio
    running in real time on another thread, and reports the CPU each part used. */
void liveUi()
{
    const auto only = juce::SystemStats::getEnvironmentVariable("STAGEMIC_UI_LIVE", {});
    for (bool advanced : {false, true})
        for (bool open : {false, true})
        {
            if ((only == "simple" || only == "profile" || only == "count") && (advanced || !open))
                continue;
            if (only == "storm" && !open)
                continue;
            stagemic::StageMicProcessor processor;
            processor.setVoiceSetupOffered(); // not the first-time welcome
            processor.setRateAndBufferSizeDetails(48000.0, 480);
            processor.prepareToPlay(48000.0, 480);
            processor.getPresetManager().loadFactoryPreset(only == "profile" || juce::SystemStats::getEnvironmentVariable("STAGEMIC_UI_PRESET", {}) == "clean" ? "Clean Vocal" : "Pop Tune");
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
            const double seconds = only == "profile" ? 40.0 : (only == "simple" ? 12.0 : (only == "storm" ? 20.0 : 10.0));
            // CPU time used by this (the window's) thread. Windows has no clock_gettime:
            // there this developer aid falls back to the whole process.
            auto threadCpu = [] {
#if defined(CLOCK_THREAD_CPUTIME_ID)
                timespec ts{};
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
                return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
                return (double)std::clock() / CLOCKS_PER_SEC;
#endif
            };
            const double ui0 = threadCpu();
            const auto cpu0 = std::clock();
            // "storm": everything a user can do, done 20 times a second while audio runs
            // (for the thread sanitizer: STAGEMIC_UI_LIVE=storm).
            struct Storm final : juce::Timer
            {
                Storm(stagemic::StageMicProcessor& p, juce::AudioProcessorEditor* e) : processor(p), editor(dynamic_cast<stagemic::StageMicEditor*>(e)) {}
                void timerCallback() override
                {
                    const auto names = stagemic::PresetManager::getFactoryPresetNames();
                    auto& all = processor.getParameters().processor.getParameters();
                    switch (step++ % 8)
                    {
                        case 0: processor.getPresetManager().loadFactoryPreset(names[rng.nextInt(names.size())]); break;
                        case 1:
                            for (int k = 0; k < 6; ++k)
                                all[rng.nextInt(all.size())]->setValueNotifyingHost(rng.nextFloat());
                            break;
                        case 2:
                        {
                            auto order = processor.getChain().getOrder();
                            std::swap(order[(size_t)rng.nextInt((int)order.size())], order[(size_t)rng.nextInt((int)order.size())]);
                            processor.getChain().setOrder(order);
                            break;
                        }
                        case 3:
                        {
                            juce::MemoryBlock block;
                            processor.getStateInformation(block);
                            processor.setStateInformation(block.getData(), (int)block.getSize());
                            break;
                        }
                        case 4: processor.getVoiceAnalyzer().start(); break;
                        case 5:
                            if (processor.getVoiceAnalyzer().getPhase() == stagemic::VoiceAnalyzer::Phase::Done)
                                processor.applyVoiceSetup(stagemic::VoiceAnalyzer::evaluate(processor.getVoiceAnalyzer().getMeasurements()));
                            else
                                processor.getVoiceAnalyzer().cancel();
                            break;
                        case 6:
                            if (editor != nullptr)
                                editor->showDisplayTab(rng.nextInt(3));
                            break;
                        default: processor.resetToDefaults(); break;
                    }
                }
                stagemic::StageMicProcessor& processor;
                stagemic::StageMicEditor* editor;
                juce::Random rng{99};
                int step = 0;
            } storm(processor, editor.get());
            if (only == "storm")
                storm.startTimerHz(20);
            juce::MessageManager::getInstance()->runDispatchLoopUntil((int)(seconds * 1000));
            const double uiCpu = threadCpu() - ui0;
            if (open && only == "count")
            {
                // How often each part of the window was redrawn.
                std::map<std::string, int> counts;
                std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
                    if (auto n = (int)c.getProperties().getWithDefault("paints", 0); n > 0)
                    {
                        std::string name = typeid(c).name();
                        if (auto* b = dynamic_cast<juce::Button*>(&c))
                            name += " '" + b->getButtonText().toStdString() + "' in " + typeid(*c.getParentComponent()).name();
                        counts[name] += n;
                    }
                    for (auto* child : c.getChildren())
                        walk(*child);
                };
                walk(*editor);
                for (auto& [name, n] : counts)
                    std::printf("  %-60s %6d paints (%.1f per second)\n", name.c_str(), n, n / seconds);
            }
            const double processCpu = (double)(std::clock() - cpu0) / CLOCKS_PER_SEC;
            stop = true;
            audio.join();
            const double audioCpu = audioMs / 1000.0;
            std::printf("%-8s window %-6s: audio %.1f%% of one core, window thread %.2f%% (process total %.1f%%)\n",
                        advanced ? "Advanced" : "Simple", open ? "open" : "closed", 100.0 * audioCpu / seconds,
                        100.0 * uiCpu / seconds, 100.0 * processCpu / seconds);
            editor.reset();
        }
}

/** Developer aid: STAGEMIC_UI_BENCH=1 measures how long the window takes to draw. */
void benchmarkUi()
{
    for (bool advanced : {false, true})
    {
        stagemic::StageMicProcessor processor;
        processor.setVoiceSetupOffered(); // not the first-time welcome
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

    if (const auto cover = juce::SystemStats::getEnvironmentVariable("STAGEMIC_COVER", {}); cover.isNotEmpty())
    {
        writeCover(juce::File(cover));
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable("STAGEMIC_UI_LIVE", {}).isNotEmpty())
    {
        liveUi();
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable("STAGEMIC_FIT", {}).isNotEmpty())
    {
        // Developer aid: opens the real window and reports whether it fits the screen.
        stagemic::StageMicProcessor processor;
        processor.prepareToPlay(48000.0, 1024);
        stagemic::EditorWindow window("fit", processor);
        window.setVisible(true);
        const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto area = d->userBounds.toNearestInt();
        std::printf("screen work area %s, window %s, editor %dx%d -> %s\n", area.toString().toRawUTF8(),
                    window.getBounds().toString().toRawUTF8(), window.getContentComponent()->getWidth(),
                    window.getContentComponent()->getHeight(), area.contains(window.getBounds()) ? "FITS" : "DOES NOT FIT");
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
