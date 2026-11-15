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
    // Feature badges: two rows of pills. The first row is what makes it different.
    auto pills = [&](std::initializer_list<const char*> labels, float y, bool highlight) {
        const juce::Font font(juce::FontOptions(21.0f, highlight ? juce::Font::bold : juce::Font::plain));
        constexpr float padX = 18.0f, gap = 12.0f, h = 40.0f;
        float total = 0.0f;
        for (auto* l : labels)
            total += juce::GlyphArrangement::getStringWidth(font, juce::String(juce::CharPointer_UTF8(l))) + 2.0f * padX + gap;
        float x = (W - (total - gap)) * 0.5f;
        g.setFont(font);
        for (auto* l : labels)
        {
            const juce::String text{juce::CharPointer_UTF8(l)};
            const float w = juce::GlyphArrangement::getStringWidth(font, text) + 2.0f * padX;
            const auto r = juce::Rectangle<float>(x, y, w, h);
            g.setColour(highlight ? accent.withAlpha(0.18f) : juce::Colour(0xff1d1f28));
            g.fillRoundedRectangle(r, h * 0.5f);
            g.setColour(highlight ? accent.withAlpha(0.9f) : juce::Colour(0xff3a3d4a));
            g.drawRoundedRectangle(r.reduced(0.75f), h * 0.5f, 1.5f);
            g.setColour(juce::Colour(0xffe9e9f0));
            g.drawText(text, r, juce::Justification::centred);
            x += w + gap;
        }
    };
    pills({"Zero latency*", "Free & open source", "Runs inside OBS", "Light on low-end PCs"}, 326.0f, true);
    pills({"Autotune", "Noise gate", "7-point EQ", "Compressor", "De-esser", "Reverb & echo", "Fun voices", "ASMR"}, 378.0f, false);

    // The window, slightly smaller, with a glow, fading into the bottom.
    const float shotW = 960.0f, shotH = shotW * (float)shot.getHeight() / (float)shot.getWidth();
    const auto shotArea = juce::Rectangle<float>((W - shotW) * 0.5f, 448.0f, shotW, shotH);
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
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x000b0b12), 0.0f, H - 260.0f, juce::Colour(0xff0b0b12), 0.0f, H - 70.0f, false));
    g.fillRect(0, H - 260, W, 260);

    // The small print for the asterisk, and what it needs.
    g.setColour(juce::Colour(0xff9a96a8));
    g.setFont(juce::FontOptions(18.0f));
    g.drawText("* The vocal chain adds no delay. Autotune adds 20 ms, only while it's on.", juce::Rectangle<int>(30, H - 52, 640, 30),
               juce::Justification::centredLeft);

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

/** Developer aid: STAGEMIC_HEADER=<file.png> draws an X (Twitter) profile header, 1500 x 500
    (drawn at 2x). Important content stays clear of the bottom-left (profile picture) and
    of the top and bottom edges (cropped on phones). */
void writeHeader(const juce::File& file, bool warm, bool textless)
{
    constexpr int W = 1500, H = 500, scale = 2;
    juce::Image header(juce::Image::ARGB, W * scale, H * scale, true);
    juce::Graphics g(header);
    g.addTransform(juce::AffineTransform::scale((float)scale));
    const auto accent = juce::Colour(0xffff4f9a);
    const auto blue = juce::Colour(0xff6aa9ff), cyan = juce::Colour(0xff4fd1e8), text = juce::Colour(0xffe9e9f0),
               dim = juce::Colour(0xff9a96a8);

    // Background: night purple to black, a soft pink glow behind the logo. The warm
    // version starts from soft taupe and peach on the left, the colours of a warm photo
    // (the profile picture), and fades into the night on the right.
    if (warm)
    {
        juce::ColourGradient bg(juce::Colour(0xff9a7866), 0.0f, (float)H, juce::Colour(0xff160f1c), (float)W, 0.0f, false);
        bg.addColour(0.30, juce::Colour(0xff6b4a4e));
        bg.addColour(0.55, juce::Colour(0xff35202f));
        g.setGradientFill(bg);
    }
    else
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1022), 0.0f, 0.0f, juce::Colour(0xff0a0a10), (float)W, (float)H, false));
    g.fillAll();
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.30f), 900.0f, 210.0f, accent.withAlpha(0.0f), 900.0f + 560.0f, 210.0f, true));
    g.fillRect(0, 0, W, H);

    // A faint editor grid.
    g.setColour(juce::Colours::white.withAlpha(0.035f));
    for (int x = 0; x < W; x += 40)
        g.drawVerticalLine(x, 0.0f, (float)H);
    for (int y = 0; y < H; y += 40)
        g.drawHorizontalLine(y, 0.0f, (float)W);

    // Real code from the plugin, as texture on the left (the profile picture may cover part of it).
    if (!textless)
    {
        const juce::Font mono(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain));
        g.setFont(mono);
        const char* lines[] = {"// zero latency. free forever.",
                               "void processBlock(AudioBuffer& buffer)",
                               "{",
                               "    scrubNonFinite(ptrs, channels, n);",
                               "    applyGain(inGain, ptrs, channels, n);",
                               "    chain.process(ptrs, channels, n);",
                               "    safetyLimit(ptrs, channels, n);",
                               "}",
                               "",
                               "gate -> eq -> de-ess -> compressor",
                               "     -> warmth -> reverb -> limiter",
                               "",
                               "tune.setKey(Key::E, Scale::Major);"};
        float y = 52.0f;
        for (auto* l : lines)
        {
            const juce::String line(l);
            if (warm)
                g.setColour(juce::Colour(0xff2a1a1c).withAlpha(line.startsWith("//") ? 0.45f : 0.30f));
            else
                g.setColour(line.startsWith("//") ? juce::Colour(0xff5d8a5d).withAlpha(0.55f) : dim.withAlpha(0.32f));
            g.drawText(line, juce::Rectangle<float>(40.0f, y, 520.0f, 22.0f), juce::Justification::centredLeft);
            y += 24.0f;
        }
    }

    // A glowing EQ curve across the whole header, with its points.
    {
        juce::Path curve;
        auto yAt = [&](float x) {
            const float t = x / (float)W;
            return 420.0f - 26.0f * std::exp(-std::pow((t - 0.62f) / 0.06f, 2.0f)) - 18.0f * std::exp(-std::pow((t - 0.86f) / 0.08f, 2.0f))
                   + 40.0f * std::exp(-t / 0.035f) - 12.0f * (1.0f / (1.0f + std::exp(-(t - 0.95f) / 0.02f)));
        };
        for (float x = 0.0f; x <= (float)W; x += 3.0f)
            x == 0.0f ? curve.startNewSubPath(x, yAt(x)) : curve.lineTo(x, yAt(x));
        stagemic::neoncat::strokeNeon(g, curve, 2.4f, blue);
        int n = 1;
        for (float t : {0.08f, 0.2f, 0.36f, 0.62f, 0.74f, 0.86f})
        {
            const juce::Point<float> p(t * W, yAt(t * W));
            g.setColour(juce::Colour(0xff14151c));
            g.fillEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(p));
            g.setColour(blue);
            g.drawEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(p), 2.0f);
            if (!textless)
            {
                g.setColour(text);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(juce::String(n++), juce::Rectangle<float>(18.0f, 18.0f).withCentre(p), juce::Justification::centred);
            }
        }
        // A sung pitch line, snapping to notes (the autotune), above it.
        juce::Path pitch;
        const float notes[] = {330.0f, 330.0f, 312.0f, 296.0f, 296.0f, 312.0f, 280.0f, 280.0f};
        for (int i = 0; i < 8; ++i)
        {
            const float x0 = 1060.0f + i * 50.0f, x1 = x0 + 44.0f;
            if (i == 0)
                pitch.startNewSubPath(x0, notes[i]);
            else
                pitch.lineTo(x0, notes[i]);
            pitch.lineTo(x1, notes[i]);
        }
        stagemic::neoncat::strokeNeon(g, pitch, 2.0f, cyan);
    }

    // Where X puts the profile picture (bottom-left): clear the busy code and curve there,
    // and give it a soft warm glow, so a bright photo blends in instead of standing out
    // against black.
    {
        const juce::Point<float> avatar(207.0f, 500.0f);
        const auto clear = warm ? juce::Colour(0xff9a7866) : juce::Colour(0xff160f1c);
        g.setGradientFill(juce::ColourGradient(clear, avatar.x, avatar.y, clear.withAlpha(0.0f), avatar.x + 330.0f, avatar.y, true));
        g.fillRect(0, 0, 640, H);
        const auto glow = warm ? juce::Colour(0x55ffe2c4) : juce::Colour(0x66ffb38a);
        g.setGradientFill(juce::ColourGradient(glow, avatar.x, avatar.y, glow.withAlpha(0.0f), avatar.x + 300.0f, avatar.y, true));
        g.fillRect(0, 0, 640, H);
    }

    // Who this is: the developer, not one product (centre-right, in the safe area).
    if (!textless)
    {
        // A neon "</>" mark, in the same style as the cat.
        juce::Path mark;
        mark.startNewSubPath(642.0f, 120.0f);
        mark.lineTo(612.0f, 150.0f);
        mark.lineTo(642.0f, 180.0f);
        mark.startNewSubPath(684.0f, 112.0f);
        mark.lineTo(664.0f, 188.0f);
        mark.startNewSubPath(706.0f, 120.0f);
        mark.lineTo(736.0f, 150.0f);
        mark.lineTo(706.0f, 180.0f);
        stagemic::neoncat::strokeNeon(g, mark, 4.0f, accent);
    }
    if (!textless)
    {
        g.setColour(accent);
        g.setFont(juce::FontOptions(78.0f, juce::Font::bold));
        g.drawText("mengsimaong", juce::Rectangle<int>(762, 104, 700, 92), juce::Justification::centredLeft);
        g.setColour(text);
        g.setFont(juce::FontOptions(27.0f, juce::Font::bold));
        g.drawText("Free & open source tools for streamers and VTubers", juce::Rectangle<int>(604, 214, 860, 36), juce::Justification::centredLeft);
    }

    // What it's built with, as small code-style tags, then the latest release.
    if (!textless)
    {
        const juce::Font mono(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
        g.setFont(mono);
        float x = 604.0f;
        for (const char* t : {"C++", "JUCE", "audio DSP", "OBS plugins", "open source"})
        {
            const juce::String label(t);
            const float w = juce::GlyphArrangement::getStringWidth(mono, label) + 26.0f;
            const auto r = juce::Rectangle<float>(x, 266.0f, w, 32.0f);
            g.setColour(juce::Colour(0xff1d1f28));
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(juce::Colour(0xff3a3d4a));
            g.drawRoundedRectangle(r.reduced(0.75f), 7.0f, 1.2f);
            g.setColour(text.withAlpha(0.9f));
            g.drawText(label, r, juce::Justification::centred);
            x += w + 10.0f;
        }

        const juce::Font small(juce::FontOptions(18.0f, juce::Font::plain));
        const juce::String latest("latest:  Paopoa StageMic, vocal filter for OBS");
        const float w = juce::GlyphArrangement::getStringWidth(small, latest) + 30.0f;
        const auto r = juce::Rectangle<float>(604.0f, 312.0f, w, 32.0f);
        g.setColour(accent.withAlpha(0.14f));
        g.fillRoundedRectangle(r, 16.0f);
        g.setColour(accent.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(0.75f), 16.0f, 1.2f);
        g.setColour(text.withAlpha(0.9f));
        g.setFont(small);
        g.drawText(latest, r, juce::Justification::centred);
    }

    // The walking cat, on the right end of the curve.
    stagemic::neoncat::paintWalking(g, 1340.0f, 420.0f - 4.0f, 46.0f, 1.2f, accent);

    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(header, out);
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
    if (const auto header = juce::SystemStats::getEnvironmentVariable("STAGEMIC_HEADER", {}); header.isNotEmpty())
    {
        writeHeader(juce::File(header), juce::SystemStats::getEnvironmentVariable("STAGEMIC_HEADER_WARM", {}).isNotEmpty(),
                    juce::SystemStats::getEnvironmentVariable("STAGEMIC_HEADER_TEXTLESS", {}).isNotEmpty());
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
