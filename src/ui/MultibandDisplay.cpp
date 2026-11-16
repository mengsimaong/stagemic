// SPDX-License-Identifier: AGPL-3.0-or-later
// The multiband graph
#include "ui/Displays.h"

#include "ui/GraphHelpers.h"
#include "ui/Theme.h"

namespace stagemic
{

using namespace graph;

//==============================================================================
MultibandDisplay::MultibandDisplay(APVTS& s, SpectrumAnalyzer& a)
    : FrequencyGraph(a)
    , state(s)
{
    setTooltip("Drag the dashed lines to move the band splits, and each band's bar to set its threshold.");
}

juce::Rectangle<float> MultibandDisplay::graphArea() const
{
    return plotArea();
}

float MultibandDisplay::dbToY(float db) const
{
    const auto a = graphArea();
    return a.getY() + a.getHeight() * juce::jlimit(0.0f, 1.0f, db / -60.0f);
}

float MultibandDisplay::yToDb(float y) const
{
    const auto a = graphArea();
    return -60.0f * (y - a.getY()) / a.getHeight();
}

float MultibandDisplay::bandLeft(int band) const
{
    const auto a = graphArea();
    if (band == 0)
        return a.getX();
    return juce::jmin(a.getRight(), freqToX(state.getRawParameterValue(band == 1 ? pid::mbLowCross : pid::mbHighCross)->load()));
}

float MultibandDisplay::bandRight(int band) const
{
    return band == 2 ? graphArea().getRight() : bandLeft(band + 1);
}

const char* MultibandDisplay::paramFor(Target t) const
{
    switch (t)
    {
        case lowSplit:
            return pid::mbLowCross;
        case highSplit:
            return pid::mbHighCross;
        case lowThreshold:
            return pid::mbLowThreshold;
        case midThreshold:
            return pid::mbMidThreshold;
        case highThreshold:
            return pid::mbHighThreshold;
        case none:
            break;
    }
    return nullptr;
}

MultibandDisplay::Target MultibandDisplay::targetAt(juce::Point<float> p) const
{
    if (!graphArea().expanded(4.0f).contains(p))
        return none;
    for (int s = 1; s <= 2; ++s)
        if (std::abs(p.x - bandLeft(s)) < 6.0f)
            return s == 1 ? lowSplit : highSplit;
    for (int b = 0; b < 3; ++b)
    {
        const char* id = b == 0 ? pid::mbLowThreshold : (b == 1 ? pid::mbMidThreshold : pid::mbHighThreshold);
        if (p.x >= bandLeft(b) && p.x < bandRight(b) && std::abs(p.y - dbToY(state.getRawParameterValue(id)->load())) < 8.0f)
            return (Target)(lowThreshold + b);
    }
    return none;
}

void MultibandDisplay::setReductions(const float* db3)
{
    for (int b = 0; b < 3; ++b)
        reductionDb[b] = db3[b];
}

void MultibandDisplay::paint(juce::Graphics& g)
{
    const auto a = graphArea();
    const bool on = state.getRawParameterValue(pid::mbOn)->load() >= 0.5f;

    g.saveState();
    g.reduceClipRegion(a.toNearestInt().withBottom(getHeight()));
    drawSpectrum(g);
    drawFrequencyGrid(g);
    g.restoreState();

    static const char* names[] = {"LOW", "MID", "HIGH"};
    const char* thresholdIds[] = {pid::mbLowThreshold, pid::mbMidThreshold, pid::mbHighThreshold};
    const auto colour = on ? theme::family(ModuleId::Multiband) : theme::faintText;

    for (int b = 0; b < 3; ++b)
    {
        const float l = bandLeft(b), r = bandRight(b);
        const float thr = state.getRawParameterValue(thresholdIds[b])->load();
        const float y = dbToY(thr);

        // Threshold
        g.setColour(colour.withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>(l, a.getY(), r - l, y - a.getY()));
        const bool active = hovered == lowThreshold + b || dragging == lowThreshold + b;
        g.setColour(colour.withAlpha(active ? 1.0f : 0.75f));
        g.fillRect(juce::Rectangle<float>(l + 2.0f, y - (active ? 1.5f : 1.0f), r - l - 4.0f, active ? 3.0f : 2.0f));

        // Live gain reduction, hanging from the top of the band
        if (on && reductionDb[b] > 0.05f)
        {
            g.setColour(theme::reduction.withAlpha(0.7f));
            g.fillRect(
                juce::Rectangle<float>((l + r) * 0.5f - 6.0f, a.getY(), 12.0f, a.getHeight() * juce::jmin(1.0f, reductionDb[b] / 24.0f)));
        }

        g.setColour(on ? theme::text : theme::dimText);
        g.setFont(theme::font(11.0f, true));
        g.drawText(names[b], juce::Rectangle<float>(l, a.getY() + 4.0f, r - l, 14.0f), juce::Justification::centred);
        g.setColour(theme::dimText);
        g.setFont(theme::font(10.5f));
        g.drawText(juce::String(thr, 1) + " dB" +
                       (reductionDb[b] > 0.05f && on ? "   -" + juce::String(reductionDb[b], 1) : juce::String()),
                   juce::Rectangle<float>(l, y + 3.0f, r - l, 13.0f), juce::Justification::centred);
    }

    for (int s = 1; s <= 2; ++s)
    {
        const float x = bandLeft(s);
        const bool active = hovered == s - 1 || dragging == s - 1;
        g.setColour(colour.withAlpha(active ? 1.0f : 0.6f));
        const float dashes[] = {5.0f, 4.0f};
        g.drawDashedLine(juce::Line<float>(x, a.getY(), x, a.getBottom()), dashes, 2, active ? 2.0f : 1.4f);
        g.setFont(theme::font(10.5f));
        g.drawText(formatFreq(xToFreq(x)), juce::Rectangle<float>(x - 40.0f, a.getBottom() - 16.0f, 80.0f, 13.0f),
                   juce::Justification::centred);
    }
}

void MultibandDisplay::mouseMove(const juce::MouseEvent& e)
{
    const auto t = targetAt(e.position);
    if (t != hovered)
    {
        hovered = t;
        setMouseCursor(t == none ? juce::MouseCursor::NormalCursor
                                 : (t <= highSplit ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor));
        repaint();
    }
}

void MultibandDisplay::mouseDown(const juce::MouseEvent& e)
{
    dragging = targetAt(e.position);
    beginParam(state, paramFor(dragging));
}

void MultibandDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const char* id = paramFor(dragging);
    if (id == nullptr)
        return;
    auto* p = state.getParameter(id);
    const float v = dragging <= highSplit ? xToFreq(e.position.x) : yToDb(e.position.y);
    p->setValueNotifyingHost(p->convertTo0to1(v));
    repaint();
}

void MultibandDisplay::mouseUp(const juce::MouseEvent&)
{
    endParam(state, paramFor(dragging));
    dragging = none;
    repaint();
}

void MultibandDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    resetParam(state, paramFor(targetAt(e.position)));
    repaint();
}

} // namespace stagemic
