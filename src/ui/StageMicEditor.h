// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"
#include "ui/Displays.h"
#include "ui/MasterPanel.h"
#include "ui/PresetBar.h"
#include "ui/Strips.h"
#include "ui/Theme.h"

namespace stagemic
{

/** The StageMic window: one compact screen with every module visible.

    Everything is laid out at a fixed design size (1120 x 764) inside 'content',
    which is scaled as a whole when the window is resized. */
class StageMicEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    static constexpr int designWidth = 1120;
    static constexpr int designHeight = 764;

    explicit StageMicEditor(StageMicProcessor&);
    ~StageMicEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Pulls meters/spectrum from the processor now (the timer does this 30x a second).
        Public so tools can render screenshots. */
    void refreshFromProcessor(bool evenIfHidden = false);

    /** 0 = EQ, 1 = Multiband. */
    void showDisplayTab(int tab) { display.select(tab); }

private:
    void timerCallback() override { refreshFromProcessor(); }

    StageMicProcessor& processor;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips{this, 700};

    juce::Component content;
    PresetBar presetBar;
    DisplayPanel display;
    GateStrip gate;
    DeEsserStrip deEsser;
    CompressorStrip compressor;
    SaturationStrip saturation;
    EffectRow chorus, delay, reverb, limiter;
    MasterPanel master;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageMicEditor)
};

} // namespace stagemic
