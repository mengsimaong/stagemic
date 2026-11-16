// SPDX-License-Identifier: AGPL-3.0-or-later
// The EQ graph and the band bar under it
#include "ui/Displays.h"

#include "ui/GraphHelpers.h"
#include "ui/Theme.h"

namespace stagemic
{

using namespace graph;

//==============================================================================
EqDisplay::EqDisplay(APVTS& s, SpectrumAnalyzer& a, std::function<double()> sr)
    : FrequencyGraph(a)
    , state(s)
    , sampleRate(std::move(sr))
    , nodes{{{"Low Cut", pid::eqLowCut, nullptr, nullptr},
             {"Low Shelf", pid::eqLowFreq, pid::eqLowGain, nullptr},
             {"Low Mid", pid::eqLoMidFreq, pid::eqLoMidGain, pid::eqLoMidQ},
             {"Mid", pid::eqMidFreq, pid::eqMidGain, pid::eqMidQ},
             {"High Mid", pid::eqHiMidFreq, pid::eqHiMidGain, pid::eqHiMidQ},
             {"Air", pid::eqHighFreq, pid::eqHighGain, nullptr},
             {"Tame", pid::eqTameFreq, pid::eqTameRange, nullptr, true}}}
{
    setTooltip("Drag a point to shape the EQ. Mouse wheel on points 3, 4 and 5 changes their width. Double-click a point to reset it.\n"
               "Point 7 (Tame) only cuts while the voice gets harsh: drag it down to allow more cut.");
}

void EqDisplay::setSelected(int index)
{
    lastUsed = juce::jlimit(0, numNodes - 1, index);
    repaint();
}

float EqDisplay::value(const char* id) const
{
    return state.getRawParameterValue(id)->load();
}

void EqDisplay::set(const char* id, float v)
{
    if (auto* p = state.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

float EqDisplay::gainToY(float db) const
{
    const auto a = plotArea();
    return a.getCentreY() - (db / rangeDb) * a.getHeight() * 0.5f;
}

float EqDisplay::yToGain(float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * rangeDb;
}

juce::Point<float> EqDisplay::nodePosition(int i) const
{
    const auto& n = nodes[(size_t)i];
    const float gain = n.gainId == nullptr ? 0.0f : (n.tame ? -value(n.gainId) : value(n.gainId));
    return {freqToX(value(n.freqId)), gainToY(gain)};
}

int EqDisplay::nodeAt(juce::Point<float> p) const
{
    // The nearest point within reach
    constexpr float reach = 14.0f, tie = 5.0f;
    int best = -1;
    float bestDist = reach;
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        const float d = nodePosition(i).getDistanceFrom(p);
        if (d >= reach)
            continue;
        const bool nearer = d < bestDist - tie;
        const bool sameSpot = best >= 0 && std::abs(d - bestDist) <= tie;
        if (best < 0 || nearer || (sameSpot && best != lastUsed))
        {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

bool EqDisplay::pointIsOn(int index) const
{
    return value(pid::eqPointOn[index]) >= 0.5f;
}

float EqDisplay::responseDb(float freq) const
{
    const double sr = sampleRate ? juce::jmax(8000.0, sampleRate()) : 48000.0;
    // The low cut exactly as the EQ builds it
    float db = 0.0f;
    if (!pointIsOn(0))
        ; // low cut switched off
    else if (value(pid::eqLowCutSlope) >= 0.5f)
        db = biquadMagnitudeDb(dsp::biquad::highpass(sr, value(pid::eqLowCut), 0.54119610), freq, sr) +
             biquadMagnitudeDb(dsp::biquad::highpass(sr, value(pid::eqLowCut), 1.30656296), freq, sr);
    else
        db = biquadMagnitudeDb(dsp::biquad::highpass(sr, value(pid::eqLowCut), 0.70710678), freq, sr);
    if (pointIsOn(1))
        db += biquadMagnitudeDb(dsp::biquad::lowShelf(sr, value(pid::eqLowFreq), value(pid::eqLowGain)), freq, sr);
    if (pointIsOn(3))
        db += biquadMagnitudeDb(dsp::biquad::peak(sr, value(pid::eqMidFreq), juce::jmax(0.1f, value(pid::eqMidQ)), value(pid::eqMidGain)),
                                freq, sr);
    if (pointIsOn(5))
        db += biquadMagnitudeDb(dsp::biquad::highShelf(sr, value(pid::eqHighFreq), value(pid::eqHighGain)), freq, sr);
    for (const auto& [f, g, q, point] : {std::tuple{pid::eqLoMidFreq, pid::eqLoMidGain, pid::eqLoMidQ, 2},
                                         std::tuple{pid::eqHiMidFreq, pid::eqHiMidGain, pid::eqHiMidQ, 4}})
        if (value(g) != 0.0f && pointIsOn(point))
            db += biquadMagnitudeDb(dsp::biquad::peak(sr, value(f), juce::jmax(0.1f, value(q)), value(g)), freq, sr);
    if (tameCutDb > 0.05f && value(pid::eqTameRange) > 0.0f && pointIsOn(6))
        db += biquadMagnitudeDb(dsp::biquad::peak(sr, value(pid::eqTameFreq), 1.2, -tameCutDb), freq, sr);
    return db;
}

void EqDisplay::paint(juce::Graphics& g)
{
    const auto a = plotArea();
    const bool on = value(pid::eqOn) >= 0.5f;

    drawSpectrum(g);
    drawFrequencyGrid(g);

    g.setFont(theme::font(10.0f));
    for (float db : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f})
    {
        const float y = gainToY(db);
        g.setColour(db == 0.0f ? theme::dimText.withAlpha(0.5f) : theme::outline.withAlpha(0.6f));
        g.drawHorizontalLine((int)y, a.getX(), a.getRight());
        g.setColour(theme::faintText);
        g.drawText((db > 0 ? "+" : "") + juce::String((int)db), juce::Rectangle<float>(a.getRight() - 26.0f, y - 12.0f, 24.0f, 11.0f),
                   juce::Justification::centredRight);
    }

    juce::Path curve;
    for (float x = a.getX(); x <= a.getRight(); x += 1.5f)
    {
        const float y = juce::jlimit(a.getY(), a.getBottom(), gainToY(on ? responseDb(xToFreq(x)) : 0.0f));
        if (x == a.getX())
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    const auto curveColour = on ? theme::family(ModuleId::Equalizer) : theme::faintText;
    juce::Path fill(curve);
    fill.lineTo(a.getRight(), gainToY(0.0f));
    fill.lineTo(a.getX(), gainToY(0.0f));
    fill.closeSubPath();
    g.setColour(curveColour.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(curveColour);
    g.strokePath(curve, juce::PathStrokeType(2.2f));

    // Points, in number order, with the one used last on top
    for (int k = 0; k < (int)nodes.size(); ++k)
    {
        const int top = lastUsed >= 0 ? lastUsed : (int)nodes.size() - 1;
        const int i = k == (int)nodes.size() - 1 ? top : (k < top ? k : k + 1);
        const auto p = nodePosition(i);
        const bool active = i == hovered || i == dragging;
        if (active)
        {
            g.setColour(curveColour.withAlpha(0.25f));
            g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre(p));
        }
        else if (i == lastUsed)
        {
            // The selected point
            g.setColour(curveColour.withAlpha(0.5f));
            g.drawEllipse(juce::Rectangle<float>(21.0f, 21.0f).withCentre(p), 1.2f);
        }
        const bool tame = nodes[(size_t)i].tame;
        const bool pointOn = pointIsOn(i);
        const auto pointColour = !pointOn
                                     ? theme::faintText
                                     : (tame ? (on && value(pid::eqTameRange) > 0.0f ? theme::reduction : theme::faintText) : curveColour);
        if (tame && on && pointOn && value(pid::eqTameRange) > 0.0f)
        {
            // Dashed line from 0 dB down to the most it may cut
            const float dashes[] = {3.0f, 3.0f};
            g.setColour(theme::reduction.withAlpha(0.6f));
            g.drawDashedLine(juce::Line<float>(p.x, gainToY(0.0f), p.x, p.y), dashes, 2, 1.2f);
        }
        g.setColour(on ? theme::panelRaised : theme::panel);
        g.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(p));
        g.setColour(pointColour);
        g.drawEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(p), 2.0f);
        g.setColour(on && pointOn ? theme::text : theme::dimText);
        g.setFont(theme::font(10.0f, true));
        g.drawText(juce::String(i + 1), juce::Rectangle<float>(13.0f, 13.0f).withCentre(p), juce::Justification::centred);
    }

    // Read-out for the point under the mouse
    const int shown = dragging >= 0 ? dragging : hovered;
    if (shown >= 0)
    {
        const auto& n = nodes[(size_t)shown];
        juce::String text = juce::String(n.name).toUpperCase() + "   " + formatFreq(value(n.freqId));
        if (n.tame)
            text << "   max cut " << juce::String(value(n.gainId), 1) << " dB   now " << juce::String(tameCutDb, 1) << " dB";
        else if (n.gainId != nullptr)
            text << "   " << (value(n.gainId) > 0 ? "+" : "") << juce::String(value(n.gainId), 1) << " dB";
        if (n.qId != nullptr)
            text << "   Q " << juce::String(value(n.qId), 2);

        const auto p = nodePosition(shown);
        auto box = juce::Rectangle<float>(text.length() * 6.6f + 16.0f, 22.0f).withCentre({p.x, p.y - 26.0f});
        box = box.constrainedWithin(a);
        g.setColour(theme::panelRaised.withAlpha(0.95f));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(theme::outline);
        g.drawRoundedRectangle(box, 4.0f, 1.0f);
        g.setColour(theme::text);
        g.setFont(theme::font(11.5f));
        g.drawText(text, box, juce::Justification::centred);
    }
}

void EqDisplay::beginGesture(int i)
{
    beginParam(state, nodes[(size_t)i].freqId);
    beginParam(state, nodes[(size_t)i].gainId);
}

void EqDisplay::endGesture(int i)
{
    endParam(state, nodes[(size_t)i].freqId);
    endParam(state, nodes[(size_t)i].gainId);
}

void EqDisplay::mouseMove(const juce::MouseEvent& e)
{
    const int n = nodeAt(e.position);
    if (n != hovered)
    {
        hovered = n;
        setMouseCursor(n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqDisplay::mouseExit(const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void EqDisplay::mouseDown(const juce::MouseEvent& e)
{
    dragging = nodeAt(e.position);
    if (dragging >= 0)
    {
        lastUsed = dragging;
        beginGesture(dragging);
        if (onSelect)
            onSelect(dragging);
    }
}

void EqDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto& n = nodes[(size_t)dragging];
    set(n.freqId, xToFreq(e.position.x));
    if (n.gainId != nullptr)
        set(n.gainId, n.tame ? -yToGain(e.position.y) : yToGain(e.position.y));
    repaint();
}

void EqDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragging >= 0)
        endGesture(dragging);
    dragging = -1;
    repaint();
}

void EqDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int n = nodeAt(e.position);
    if (n < 0)
        return;
    resetParam(state, nodes[(size_t)n].freqId);
    resetParam(state, nodes[(size_t)n].gainId);
    resetParam(state, nodes[(size_t)n].qId);
    repaint();
}

void EqDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int n = nodeAt(e.position);
    if (n < 0 || nodes[(size_t)n].qId == nullptr)
        return;
    const char* id = nodes[(size_t)n].qId;
    beginParam(state, id);
    set(id, value(id) * std::pow(1.25f, wheel.deltaY * 4.0f));
    endParam(state, id);
    repaint();
}

//==============================================================================
EqBandBar::EqBandBar(APVTS& s)
    : state(s)
    , bands{{"Low Cut", pid::eqLowCut, nullptr, false},
            {"Low Shelf", pid::eqLowFreq, pid::eqLowGain, false},
            {"Low Mid", pid::eqLoMidFreq, pid::eqLoMidGain, false},
            {"Mid", pid::eqMidFreq, pid::eqMidGain, false},
            {"High Mid", pid::eqHiMidFreq, pid::eqHiMidGain, false},
            {"Air", pid::eqHighFreq, pid::eqHighGain, false},
            {"Tame", pid::eqTameFreq, pid::eqTameRange, true}}
{
    using Fields = std::vector<std::pair<const char*, juce::String>>;
    const std::vector<Fields> fields = {{{pid::eqLowCut, "Freq"}, {pid::eqLowCutSlope, "Slope"}, {pid::eqPlosive, "Pops"}},
                                        {{pid::eqLowFreq, "Freq"}, {pid::eqLowGain, "Gain"}},
                                        {{pid::eqLoMidFreq, "Freq"}, {pid::eqLoMidGain, "Gain"}, {pid::eqLoMidQ, "Q"}},
                                        {{pid::eqMidFreq, "Freq"}, {pid::eqMidGain, "Gain"}, {pid::eqMidQ, "Q"}},
                                        {{pid::eqHiMidFreq, "Freq"}, {pid::eqHiMidGain, "Gain"}, {pid::eqHiMidQ, "Q"}},
                                        {{pid::eqHighFreq, "Freq"}, {pid::eqHighGain, "Gain"}},
                                        {{pid::eqTameFreq, "Freq"}, {pid::eqTameThreshold, "Thresh"}, {pid::eqTameRange, "Max Cut"}}};
    for (size_t i = 0; i < bands.size(); ++i)
    {
        rows.push_back(std::make_unique<FieldRow>(s, std::vector<FieldRow::Group>{{juce::String((int)i + 1), bands[i].name, fields[i]}}));
        addChildComponent(*rows.back());

        // Its own on/off switch, on the chip
        pointPower[i].setTooltip("Switch point " + juce::String((int)i + 1) + " (" + bands[i].name + ") on or off, to hear what it does");
        pointPower[i].onStateChange = [this, i] {
            if (i < chips.size())
                repaint(chips[i]);
        };
        pointAttachments.push_back(std::make_unique<APVTS::ButtonAttachment>(s, pid::eqPointOn[i], pointPower[i]));
        addAndMakeVisible(pointPower[i]);
    }
    setTooltip("Click a point's chip (or the point on the graph) to show its knobs.");
    select(selected);
}

void EqBandBar::select(int index)
{
    selected = juce::jlimit(0, (int)bands.size() - 1, index);
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setVisible((int)i == selected);
    repaint();
}

juce::String EqBandBar::readOut(int index, int line) const
{
    const auto& b = bands[(size_t)index];
    const char* id = line == 0 ? b.freqId : b.gainId;
    if (id == nullptr)
        return state.getRawParameterValue(pid::eqLowCutSlope)->load() >= 0.5f ? "24 dB/oct" : "12 dB/oct";
    auto* p = state.getParameter(id);
    auto text = p->getText(p->getValue(), 16);
    if (b.tame && line == 1)
    {
        const float maxCut = state.getRawParameterValue(id)->load();
        text = maxCut > 0.0f ? "-" + juce::String(maxCut, 1) + " dB max" : juce::String("off");
    }
    return text;
}

void EqBandBar::refresh()
{
    // Compares the raw numbers
    for (int i = 0; i < (int)bands.size(); ++i)
    {
        const auto& b = bands[(size_t)i];
        const float f = state.getRawParameterValue(b.freqId)->load();
        const float g = state.getRawParameterValue(b.gainId != nullptr ? b.gainId : pid::eqLowCutSlope)->load();
        if (f != shownFreq[(size_t)i] || g != shownGain[(size_t)i])
        {
            shownFreq[(size_t)i] = f;
            shownGain[(size_t)i] = g;
            if (i < (int)chips.size())
                repaint(chips[(size_t)i]);
        }
    }
}

int EqBandBar::chipAt(juce::Point<int> p) const
{
    for (int i = 0; i < (int)chips.size(); ++i)
        if (chips[(size_t)i].contains(p))
            return i;
    return -1;
}

void EqBandBar::mouseDown(const juce::MouseEvent& e)
{
    if (const int i = chipAt(e.getPosition()); i >= 0)
    {
        select(i);
        if (onSelect)
            onSelect(i);
    }
}

void EqBandBar::mouseMove(const juce::MouseEvent& e)
{
    if (const int i = chipAt(e.getPosition()); i != hover)
    {
        hover = i;
        setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqBandBar::mouseExit(const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void EqBandBar::resized()
{
    constexpr int gap = 4, knobsWidth = 262;
    auto area = getLocalBounds();
    const auto knobs = area.removeFromRight(knobsWidth);
    for (auto& r : rows)
        r->setBounds(knobs);
    area.removeFromRight(8);
    chips.clear();
    const int n = (int)bands.size();
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        chips.push_back(area.removeFromLeft(w));
        area.removeFromLeft(gap);
        pointPower[i].setBounds(chips.back().getRight() - 26, chips.back().getBottom() - 26, 22, 20);
    }
}

void EqBandBar::paint(juce::Graphics& g)
{
    const auto tint = theme::tintOf(*this);
    for (int i = 0; i < (int)chips.size(); ++i)
    {
        const auto r = chips[(size_t)i].toFloat().reduced(0.5f);
        const bool on = i == selected;
        g.setColour(on ? theme::panelRaised : theme::panelRaised.withAlpha(i == hover ? 0.75f : 0.4f));
        g.fillRoundedRectangle(r, 5.0f);
        if (on)
        {
            g.setColour(tint.withAlpha(0.85f));
            g.drawRoundedRectangle(r, 5.0f, 1.2f);
        }

        const bool pointOn = pointPower[i].getToggleState();
        auto inner = chips[(size_t)i].reduced(7, 6);
        auto top = inner.removeFromTop(18);
        const auto badge = top.removeFromLeft(15).toFloat().withSizeKeepingCentre(15.0f, 15.0f);
        const bool tame = bands[(size_t)i].tame;
        g.setColour(!pointOn ? theme::faintText : (tame ? theme::reduction : tint));
        g.drawEllipse(badge.reduced(0.75f), 1.5f);
        g.setColour(theme::text);
        g.setFont(theme::font(10.0f, true));
        g.drawText(juce::String(i + 1), badge, juce::Justification::centred);
        top.removeFromLeft(5);
        g.setColour(on ? theme::text : theme::dimText);
        g.setFont(theme::font(11.0f, true));
        g.drawFittedText(bands[(size_t)i].name.toUpperCase(), top, juce::Justification::centredLeft, 1, 0.7f);

        inner.removeFromTop(4);
        g.setColour(!pointOn ? theme::faintText : (on ? theme::text : theme::dimText));
        g.setFont(theme::font(12.0f, on));
        g.drawFittedText(readOut(i, 0), inner.removeFromTop(17), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(!pointOn ? theme::faintText : theme::dimText);
        g.setFont(theme::font(11.5f));
        g.drawFittedText(pointOn ? readOut(i, 1) : juce::String("off"), inner.removeFromTop(16).withTrimmedRight(24),
                         juce::Justification::centredLeft, 1, 0.8f);
    }
}

} // namespace stagemic
