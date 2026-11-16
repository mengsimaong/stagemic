// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

namespace neoncat
{
// Draws a line drawing with a neon glow
void strokeNeon(juce::Graphics&, const juce::Path&, float lineWidth, juce::Colour);

// The cat's head, facing you
void paintHead(juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

// The walking cat, side view facing right
juce::Rectangle<float> paintWalking(juce::Graphics&, float x, float baseline, float height, float phase, juce::Colour);
} // namespace neoncat

// Footer strip with the walking cat
class NeonCatStrip final : public juce::Component
{
public:
    NeonCatStrip();
    // Moves the cat one frame on
    void advance();
    void paint(juce::Graphics&) override;

private:
    static constexpr int kFrames = 32; // one walk cycle
    float x = -60.0f, phase = 0.0f;
    juce::Rectangle<int> lastArea;
    std::vector<juce::Image> frames;
    int cachedHeight = 0;
    float cachedScale = 0.0f;
};

} // namespace stagemic
