// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

/** The Paopoa cat: a small neon-outline line drawing (original artwork, drawn
    with vector paths so it's sharp at any window size). */
namespace neoncat
{
/** Draws a line drawing with a neon glow (soft wide halo, brighter core). */
void strokeNeon(juce::Graphics&, const juce::Path&, float lineWidth, juce::Colour);

/** The cat's head, facing you (the logo). Fits inside `area`. */
void paintHead(juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

/** The walking cat, side view facing right. `phase` advances the walk cycle
    (one full stride per 2*pi); its height is `height`, its left edge at `x`,
    its paws on `baseline`. Returns the area it covers. */
juce::Rectangle<float> paintWalking(juce::Graphics&, float x, float baseline, float height, float phase, juce::Colour);
} // namespace neoncat

/** A slim strip along the bottom of the window where the cat walks from left
    to right, over and over. Repaints only around the cat, ~30 times a second. */
class NeonCatStrip final : public juce::Component
{
public:
    NeonCatStrip();
    /** Moves the cat one frame on (call from the window's 30 Hz timer). */
    void advance();
    void paint(juce::Graphics&) override;

private:
    float x = -60.0f, phase = 0.0f;
    juce::Rectangle<int> lastArea;
};

} // namespace stagemic
