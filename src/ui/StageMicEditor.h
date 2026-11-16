// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"
#include "ui/ChainBar.h"
#include "ui/Displays.h"
#include "ui/MasterPanel.h"
#include "ui/NeonCat.h"
#include "ui/PresetBar.h"
#include "ui/SimpleView.h"
#include "ui/Strips.h"
#include "ui/Theme.h"
#include "ui/VoiceSetup.h"

namespace stagemic
{

class StageMicEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    static constexpr int designWidth = 1120;
    static constexpr int designHeight = 832;
    static constexpr int footerHeight = 32; // where the cat walks
    static constexpr int bodyBottom = designHeight - footerHeight;
    // How often meters, graphs and the cat update
    static constexpr int refreshRateHz = 20;

    explicit StageMicEditor(StageMicProcessor&);
    ~StageMicEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Pulls meters/spectrum from the processor now
    void refreshFromProcessor(bool evenIfHidden = false);

    // 0 = EQ, 1 = Multiband
    void showDisplayTab(int tab) { display.select(tab); }
    VoiceSetup& getVoiceSetup() { return voiceSetup; }

private:
    void timerCallback() override { refreshFromProcessor(); }

    StageMicProcessor& processor;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips{this, 700};

    // Briefly outlines the panel a chain chip was clicked for
    struct Flash final : juce::Component
    {
        Flash() { setInterceptsMouseClicks(false, false); }
        // Hidden while idle, so it costs nothing on the frames when nothing
        void show(juce::Rectangle<int> r)
        {
            target = r;
            alpha = 1.0f;
            setVisible(true);
            repaint();
        }
        bool step()
        {
            if (alpha <= 0.0f)
                return false;
            alpha = juce::jmax(0.0f, alpha - 0.09f);
            repaint();
            if (alpha <= 0.0f)
                setVisible(false);
            return true;
        }
        void paint(juce::Graphics& g) override
        {
            if (alpha <= 0.0f)
                return;
            g.setColour(juce::Colour(0xff6aa9ff).withAlpha(alpha));
            g.drawRoundedRectangle(target.toFloat().reduced(1.0f), 8.0f, 2.5f);
        }
        juce::Rectangle<int> target;
        float alpha = 0.0f;
    };
    void showModule(ModuleId);
    void applyMode(bool advanced);

    juce::Component content;
    Flash flash;
    VoiceSetup voiceSetup;
    PresetBar presetBar;
    ChainBar chainBar;
    DisplayPanel display;
    GateStrip gate;
    DeEsserStrip deEsser;
    CompressorStrip compressor;
    TabbedModules color;
    TabbedModules space;
    EffectRow limiter;
    MasterPanel master;
    SimpleView simple;
    bool wasShowing = true;
    bool sizeRestored = false; // ignore the sizes set while building the window
    NeonCatStrip cat;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageMicEditor)
};

} // namespace stagemic
