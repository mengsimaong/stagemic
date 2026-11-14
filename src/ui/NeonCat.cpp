// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/NeonCat.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace neoncat
{
void strokeNeon(juce::Graphics& g, const juce::Path& p, float w, juce::Colour colour)
{
    const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;
    const auto cap = juce::PathStrokeType::rounded;
    g.setColour(colour.withAlpha(0.10f));
    g.strokePath(p, juce::PathStrokeType(w * 4.5f, joint, cap));
    g.setColour(colour.withAlpha(0.22f));
    g.strokePath(p, juce::PathStrokeType(w * 2.4f, joint, cap));
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(w, joint, cap));
    g.setColour(colour.interpolatedWith(juce::Colours::white, 0.55f));
    g.strokePath(p, juce::PathStrokeType(w * 0.4f, joint, cap));
}

void paintHead(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    // Drawn in a 40 x 36 box, then scaled into `area`.
    juce::Path p;
    // Head: a soft rounded shape, slightly wider at the cheeks.
    p.startNewSubPath(8.0f, 14.0f);
    p.quadraticTo(4.0f, 4.0f, 9.0f, 2.0f);        // left ear, outer edge up to the tip
    p.lineTo(15.0f, 9.0f);                         // left ear, inner edge
    p.quadraticTo(20.0f, 7.5f, 25.0f, 9.0f);       // top of the head
    p.lineTo(31.0f, 2.0f);                         // right ear
    p.quadraticTo(36.0f, 4.0f, 32.0f, 14.0f);
    p.quadraticTo(37.0f, 24.0f, 30.0f, 30.0f);     // right cheek
    p.quadraticTo(20.0f, 35.0f, 10.0f, 30.0f);     // chin
    p.quadraticTo(3.0f, 24.0f, 8.0f, 14.0f);       // left cheek
    // Happy closed eyes (^ ^).
    p.startNewSubPath(11.5f, 19.0f);
    p.quadraticTo(14.0f, 15.5f, 16.5f, 19.0f);
    p.startNewSubPath(23.5f, 19.0f);
    p.quadraticTo(26.0f, 15.5f, 28.5f, 19.0f);
    // Little "w" mouth.
    p.startNewSubPath(17.0f, 23.5f);
    p.quadraticTo(18.5f, 26.0f, 20.0f, 23.5f);
    p.quadraticTo(21.5f, 26.0f, 23.0f, 23.5f);
    // Whiskers.
    for (float side : {-1.0f, 1.0f})
    {
        const float cx = 20.0f + side * 9.0f;
        p.startNewSubPath(cx, 24.0f);
        p.lineTo(cx + side * 9.0f, 22.5f);
        p.startNewSubPath(cx, 26.0f);
        p.lineTo(cx + side * 9.0f, 27.0f);
    }

    const float scale = juce::jmin(area.getWidth() / 40.0f, area.getHeight() / 36.0f);
    const auto box = juce::Rectangle<float>(40.0f * scale, 36.0f * scale).withCentre(area.getCentre());
    p.applyTransform(juce::AffineTransform::scale(scale).translated(box.getX(), box.getY()));
    strokeNeon(g, p, 1.6f * scale / 1.0f * 0.75f, colour);
}

juce::Rectangle<float> paintWalking(juce::Graphics& g, float x, float baseline, float height, float phase, juce::Colour colour)
{
    // Drawn in a 64 x 36 box (paws at y = 36), then scaled.
    const float bob = 0.8f * std::sin(phase * 2.0f);
    juce::Path p;
    // Body: curved back, round rump, soft belly.
    p.startNewSubPath(45.0f, 13.5f + bob);
    p.cubicTo(38.0f, 9.0f + bob, 24.0f, 9.5f + bob, 17.5f, 14.0f + bob);
    p.quadraticTo(12.5f, 18.5f + bob, 17.0f, 24.0f + bob);
    p.cubicTo(24.0f, 26.5f + bob, 36.0f, 26.5f + bob, 42.5f, 23.5f + bob);
    p.quadraticTo(46.5f, 19.5f + bob, 45.0f, 13.5f + bob);
    // Head with ears (side view, facing right).
    const float hy = 11.0f + bob;
    p.startNewSubPath(45.0f, hy + 3.0f);
    p.quadraticTo(44.0f, hy - 4.0f, 47.0f, hy - 6.5f);
    p.lineTo(48.5f, hy - 12.0f);               // back ear
    p.lineTo(51.0f, hy - 7.5f);
    p.lineTo(54.5f, hy - 12.5f);               // front ear
    p.lineTo(55.5f, hy - 6.0f);
    p.quadraticTo(60.0f, hy - 2.0f, 58.5f, hy + 2.0f); // nose and muzzle
    p.quadraticTo(56.0f, hy + 6.5f, 49.0f, hy + 6.0f);
    p.quadraticTo(46.0f, hy + 5.5f, 45.0f, hy + 3.0f);
    // Closed happy eye.
    p.startNewSubPath(52.0f, hy - 1.0f);
    p.quadraticTo(53.5f, hy - 3.0f, 55.0f, hy - 1.0f);
    // Whisker.
    p.startNewSubPath(57.0f, hy + 2.5f);
    p.lineTo(62.5f, hy + 1.5f);
    // Tail, curling up and swaying.
    const float sway = 2.5f * std::sin(phase);
    p.startNewSubPath(17.0f, 17.0f + bob);
    p.cubicTo(9.0f, 15.0f, 6.0f + sway, 6.0f, 11.0f + sway, 1.0f);
    // Legs: diagonal pairs move together (a real cat's walk).
    auto leg = [&](float hipX, float offset) {
        const float swing = 3.2f * std::sin(phase + offset);
        const float lift = juce::jmax(0.0f, 1.8f * std::cos(phase + offset));
        p.startNewSubPath(hipX, 24.0f + bob);
        p.lineTo(hipX + swing, 36.0f - lift);
    };
    leg(41.0f, 0.0f);
    leg(44.0f, juce::MathConstants<float>::pi);
    leg(20.0f, juce::MathConstants<float>::pi);
    leg(23.0f, 0.0f);

    const float scale = height / 36.0f;
    p.applyTransform(juce::AffineTransform::scale(scale).translated(x, baseline - height));
    strokeNeon(g, p, 1.6f, colour);
    return juce::Rectangle<float>(x, baseline - height, 64.0f * scale, height).expanded(8.0f);
}
} // namespace neoncat

//==============================================================================
NeonCatStrip::NeonCatStrip()
{
    setInterceptsMouseClicks(false, false);
}

void NeonCatStrip::advance()
{
    if (getWidth() <= 0)
        return;
    const float height = (float)getHeight() - 3.0f;
    const float catWidth = 64.0f * height / 36.0f;
    x += 3.3f;                       // ~66 px a second at 20 frames a second
    phase += 0.48f;                  // about 1.5 strides a second
    if (phase > juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;
    if (x > (float)getWidth() + 10.0f)
        x = -catWidth - 10.0f;
    const auto area = juce::Rectangle<float>(x - 12.0f, 0.0f, catWidth + 24.0f, (float)getHeight()).getSmallestIntegerContainer();
    // Repaint where the cat was and where it is now (separately: when it wraps
    // around, one region spanning the whole width would be wasteful).
    repaint(lastArea);
    repaint(area);
    lastArea = area;
}

void NeonCatStrip::paint(juce::Graphics& g)
{
    // The neon glow is costly to draw, so the walk cycle is drawn once into a few
    // small pictures, and each frame just copies one into place.
    const float height = (float)getHeight() - 3.0f;
    constexpr float pad = 8.0f;
    // Drawn at the screen's real pixel size, so showing a frame is a plain copy.
    const float oversample = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float frameW = 64.0f * height / 36.0f + 2.0f * pad, frameH = (float)getHeight();
    if (frames.empty() || cachedHeight != getHeight() || std::abs(cachedScale - oversample) > 0.01f)
    {
        frames.clear();
        cachedHeight = getHeight();
        cachedScale = oversample;
        for (int i = 0; i < kFrames; ++i)
        {
            juce::Image img(juce::Image::ARGB, (int)std::ceil(frameW * oversample), (int)std::ceil(frameH * oversample), true);
            juce::Graphics fg(img);
            fg.addTransform(juce::AffineTransform::scale(oversample));
            neoncat::paintWalking(fg, pad, frameH - 2.0f, height, juce::MathConstants<float>::twoPi * (float)i / (float)kFrames,
                                  theme::accent);
            frames.push_back(img);
        }
    }
    const int index = (int)std::lround(phase / juce::MathConstants<float>::twoPi * (float)kFrames) % kFrames;
    // Snap to a whole screen pixel so no resampling is needed.
    const float left = std::round((x - pad) * oversample) / oversample;
    g.drawImageTransformed(frames[(size_t)index], juce::AffineTransform::scale(1.0f / oversample).translated(left, 0.0f));
}

} // namespace stagemic
