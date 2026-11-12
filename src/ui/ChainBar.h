// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

/** The signal chain as a row of chips, in processing order:
      Gate > EQ > Auto Level > ... > [Chorus | Delay | Reverb] > Limiter
    Drag a chip to move that module. Lit chips are on, dim chips are off.
    The Limiter stays last (it protects the stream from clipping).
    "Recommended order" restores the default. */
class ChainBar final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit ChainBar(StageMicProcessor&);

    /** Call regularly: repaints when the order, on/off states or activity change. */
    void refresh();
    void setActivity(const dsp::VocalChain::Activity&);

    /** Clicking a chip (without dragging) asks the window to show that module. */
    std::function<void(ModuleId)> onModuleClicked;

    juce::String getTooltip() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    static juce::String chipName(ModuleId);

private:
    juce::Rectangle<float> chipBounds(int index) const;
    juce::Rectangle<float> ledBounds(int index) const;
    float activityOf(ModuleId) const;
    int indexAt(juce::Point<float>) const;
    int dropIndexFor(float x) const;
    bool isLocked(int index) const;

    StageMicProcessor& processor;
    dsp::VocalChain::Order shown{};
    std::array<bool, kNumModules> enabled{};
    juce::TextButton resetButton{"Recommended order"};
    juce::Rectangle<int> chipArea;
    int dragging = -1, dropAt = -1, pressed = -1, hovered = -1;
    float dragX = 0.0f;
    bool movedEnough = false;
    dsp::VocalChain::Activity activity;
};

} // namespace stagemic
