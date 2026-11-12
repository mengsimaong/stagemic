// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Displays.h"

#include "core/dsp/DspUtils.h"
#include "ui/Theme.h"

#include <complex>

namespace stagemic
{

namespace
{
constexpr float minFreq = 20.0f, maxFreq = 20000.0f;

juce::String formatFreq(float f)
{
    return f >= 1000.0f ? juce::String(f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz";
}

float biquadMagnitudeDb(const dsp::BiquadCoeffs& c, double freq, double sampleRate)
{
    const double w = 2.0 * dsp::kPi * freq / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const auto num = (double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2;
    const auto den = 1.0 + (double)c.a1 * z1 + (double)c.a2 * z2;
    return (float)(20.0 * std::log10(std::max(1.0e-9, std::abs(num / den))));
}

void beginParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
            p->beginChangeGesture();
}

void endParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
            p->endChangeGesture();
}

void resetParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            p->endChangeGesture();
        }
}
} // namespace

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer() = default;

bool SpectrumAnalyzer::update(SpectrumTap& tap, double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int got = tap.pull(incoming.data(), (int)incoming.size());
    for (int i = 0; i < got; ++i)
    {
        history[(size_t)historyPos] = incoming[(size_t)i];
        historyPos = (historyPos + 1) % size;
    }
    newSamples += got;

    if (newSamples < size / 4)
    {
        // Nothing new (silence or paused): let the display fall gently.
        if (got == 0)
            for (auto& v : smoothedDb)
                v = juce::jmax(-100.0f, v - 1.5f);
        return got == 0;
    }
    newSamples = 0;

    for (int i = 0; i < size; ++i)
        fftData[(size_t)i] = history[(size_t)((historyPos + i) % size)];
    std::fill(fftData.begin() + size, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable(fftData.data(), (size_t)size);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    const float norm = 4.0f / (float)size; // sine amplitude A shows as ~A
    for (int b = 0; b < size / 2; ++b)
    {
        const float db = juce::Decibels::gainToDecibels(fftData[(size_t)b] * norm, -100.0f);
        auto& s = smoothedDb[(size_t)b];
        s = db > s ? db : s + 0.25f * (db - s); // fast rise, slow fall
    }
    return true;
}

float SpectrumAnalyzer::levelAt(float freq) const
{
    const float bin = freq * (float)size / (float)sr;
    const int i0 = juce::jlimit(1, size / 2 - 2, (int)bin);
    const float frac = juce::jlimit(0.0f, 1.0f, bin - (float)i0);
    // Average a few neighbouring bins at high frequencies so the line stays readable.
    return smoothedDb[(size_t)i0] + frac * (smoothedDb[(size_t)i0 + 1] - smoothedDb[(size_t)i0]);
}

//==============================================================================
FrequencyGraph::FrequencyGraph(SpectrumAnalyzer& a)
    : analyzer(a)
{
}

juce::Rectangle<float> FrequencyGraph::plotArea() const
{
    return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedBottom(14.0f);
}

float FrequencyGraph::freqToX(float freq) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log(freq / minFreq) / std::log(maxFreq / minFreq);
}

float FrequencyGraph::xToFreq(float x) const
{
    const auto a = plotArea();
    return minFreq * std::pow(maxFreq / minFreq, juce::jlimit(0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

void FrequencyGraph::drawFrequencyGrid(juce::Graphics& g) const
{
    const auto a = plotArea();
    g.setFont(theme::font(10.0f));
    for (float f : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f})
    {
        const float x = freqToX(f);
        g.setColour(theme::outline.withAlpha(0.6f));
        g.drawVerticalLine((int)x, a.getY(), a.getBottom());
        g.setColour(theme::faintText);
        g.drawText(f >= 1000.0f ? juce::String((int)(f / 1000.0f)) + "k" : juce::String((int)f),
                   juce::Rectangle<float>(x - 20.0f, a.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
}

void FrequencyGraph::drawSpectrum(juce::Graphics& g) const
{
    const auto a = plotArea();
    constexpr float floorDb = -84.0f;
    juce::Path p;
    p.startNewSubPath(a.getX(), a.getBottom());
    for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
    {
        const float db = juce::jlimit(floorDb, 0.0f, analyzer.levelAt(xToFreq(x)) + 6.0f);
        p.lineTo(x, a.getBottom() - a.getHeight() * (db - floorDb) / -floorDb);
    }
    p.lineTo(a.getRight(), a.getBottom());
    p.closeSubPath();

    g.setGradientFill(juce::ColourGradient(theme::spectrum.withAlpha(0.45f), 0.0f, a.getY(), theme::spectrum.withAlpha(0.05f), 0.0f,
                                           a.getBottom(), false));
    g.fillPath(p);
    g.setColour(theme::spectrum.withAlpha(0.7f));
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

//==============================================================================
EqDisplay::EqDisplay(APVTS& s, SpectrumAnalyzer& a, std::function<double()> sr)
    : FrequencyGraph(a)
    , state(s)
    , sampleRate(std::move(sr))
    , nodes{{{"Low Cut", pid::eqLowCut, nullptr, nullptr},
             {"Low", pid::eqLowFreq, pid::eqLowGain, nullptr},
             {"Mid", pid::eqMidFreq, pid::eqMidGain, pid::eqMidQ},
             {"Air", pid::eqHighFreq, pid::eqHighGain, nullptr},
             {"Tame", pid::eqTameFreq, pid::eqTameRange, nullptr, true}}}
{
    setTooltip("Drag a point to shape the EQ. Mouse wheel on Mid changes its width. Double-click a point to reset it.\n"
               "Point 5 (Tame) only cuts while the voice gets harsh: drag it down to allow more cut.");
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
    int best = -1;
    float bestDist = 14.0f;
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        const float d = nodePosition(i).getDistanceFrom(p);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

float EqDisplay::responseDb(float freq) const
{
    const double sr = sampleRate ? juce::jmax(8000.0, sampleRate()) : 48000.0;
    float db = biquadMagnitudeDb(dsp::biquad::highpass(sr, value(pid::eqLowCut), 0.70710678), freq, sr);
    db += biquadMagnitudeDb(dsp::biquad::lowShelf(sr, value(pid::eqLowFreq), value(pid::eqLowGain)), freq, sr);
    db += biquadMagnitudeDb(dsp::biquad::peak(sr, value(pid::eqMidFreq), juce::jmax(0.1f, value(pid::eqMidQ)), value(pid::eqMidGain)), freq, sr);
    db += biquadMagnitudeDb(dsp::biquad::highShelf(sr, value(pid::eqHighFreq), value(pid::eqHighGain)), freq, sr);
    if (tameCutDb > 0.05f && value(pid::eqTameRange) > 0.0f)
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

    // Curve
    juce::Path curve;
    for (float x = a.getX(); x <= a.getRight(); x += 1.5f)
    {
        const float y = juce::jlimit(a.getY(), a.getBottom(), gainToY(on ? responseDb(xToFreq(x)) : 0.0f));
        if (x == a.getX())
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    const auto curveColour = on ? theme::accent : theme::faintText;
    juce::Path fill(curve);
    fill.lineTo(a.getRight(), gainToY(0.0f));
    fill.lineTo(a.getX(), gainToY(0.0f));
    fill.closeSubPath();
    g.setColour(curveColour.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(curveColour);
    g.strokePath(curve, juce::PathStrokeType(2.2f));

    // Points
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        const auto p = nodePosition(i);
        const bool active = i == hovered || i == dragging;
        if (active)
        {
            g.setColour(curveColour.withAlpha(0.25f));
            g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre(p));
        }
        const bool tame = nodes[(size_t)i].tame;
        const auto pointColour = tame ? (on && value(pid::eqTameRange) > 0.0f ? theme::reduction : theme::faintText) : curveColour;
        if (tame && on && value(pid::eqTameRange) > 0.0f)
        {
            // Dashed line from 0 dB down to the most it may cut.
            const float dashes[] = {3.0f, 3.0f};
            g.setColour(theme::reduction.withAlpha(0.6f));
            g.drawDashedLine(juce::Line<float>(p.x, gainToY(0.0f), p.x, p.y), dashes, 2, 1.2f);
        }
        g.setColour(on ? theme::panelRaised : theme::panel);
        g.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(p));
        g.setColour(pointColour);
        g.drawEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(p), 2.0f);
        g.setColour(on ? theme::text : theme::dimText);
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
        beginGesture(dragging);
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
        case lowSplit: return pid::mbLowCross;
        case highSplit: return pid::mbHighCross;
        case lowThreshold: return pid::mbLowThreshold;
        case midThreshold: return pid::mbMidThreshold;
        case highThreshold: return pid::mbHighThreshold;
        case none: break;
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
    const auto colour = on ? theme::accent : theme::faintText;

    for (int b = 0; b < 3; ++b)
    {
        const float l = bandLeft(b), r = bandRight(b);
        const float thr = state.getRawParameterValue(thresholdIds[b])->load();
        const float y = dbToY(thr);

        // Threshold: everything above the line gets compressed.
        g.setColour(colour.withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>(l, a.getY(), r - l, y - a.getY()));
        const bool active = hovered == lowThreshold + b || dragging == lowThreshold + b;
        g.setColour(colour.withAlpha(active ? 1.0f : 0.75f));
        g.fillRect(juce::Rectangle<float>(l + 2.0f, y - (active ? 1.5f : 1.0f), r - l - 4.0f, active ? 3.0f : 2.0f));

        // Live gain reduction, hanging from the top of the band.
        if (on && reductionDb[b] > 0.05f)
        {
            g.setColour(theme::reduction.withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>((l + r) * 0.5f - 6.0f, a.getY(), 12.0f, a.getHeight() * juce::jmin(1.0f, reductionDb[b] / 24.0f)));
        }

        g.setColour(on ? theme::text : theme::dimText);
        g.setFont(theme::font(11.0f, true));
        g.drawText(names[b], juce::Rectangle<float>(l, a.getY() + 4.0f, r - l, 14.0f), juce::Justification::centred);
        g.setColour(theme::dimText);
        g.setFont(theme::font(10.5f));
        g.drawText(juce::String(thr, 1) + " dB" + (reductionDb[b] > 0.05f && on ? "   -" + juce::String(reductionDb[b], 1) : juce::String()),
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

//==============================================================================
FieldRow::FieldRow(APVTS& s, std::vector<Group> g)
    : groups(std::move(g))
{
    for (const auto& group : groups)
        for (const auto& [id, name] : group.fields)
        {
            if (dynamic_cast<juce::AudioParameterChoice*>(s.getParameter(id)) != nullptr)
                fields.push_back(std::make_unique<ChoiceToggle>(s, id, name));
            else
                fields.push_back(std::make_unique<ValueKnob>(s, id, name));
            addAndMakeVisible(*fields.back());
        }
}

void FieldRow::resized()
{
    constexpr int gap = 6, padding = 5;
    int totalFields = 0;
    for (const auto& g : groups)
        totalFields += (int)g.fields.size();

    const int available = getWidth() - gap * ((int)groups.size() - 1);
    groupAreas.clear();
    int x = 0;
    size_t f = 0;
    for (const auto& g : groups)
    {
        const int w = available * (int)g.fields.size() / juce::jmax(1, totalFields);
        auto area = juce::Rectangle<int>(x, 0, w, getHeight());
        groupAreas.push_back(area);
        x += w + gap;

        auto inner = area.reduced(padding, 0).withTrimmedTop(18).withTrimmedBottom(4);
        const int fw = inner.getWidth() / juce::jmax(1, (int)g.fields.size());
        for (size_t i = 0; i < g.fields.size(); ++i, ++f)
            fields[f]->setBounds(inner.removeFromLeft(fw).reduced(3, 0));
    }
}

void FieldRow::paint(juce::Graphics& g)
{
    for (size_t i = 0; i < groups.size() && i < groupAreas.size(); ++i)
    {
        const auto area = groupAreas[i].toFloat();
        g.setColour(theme::panelRaised.withAlpha(0.6f));
        g.fillRoundedRectangle(area, 5.0f);

        auto title = area.withHeight(20.0f).withTrimmedLeft(8.0f);
        if (groups[i].badge.isNotEmpty())
        {
            const auto badge = title.removeFromLeft(15.0f).withSizeKeepingCentre(15.0f, 15.0f);
            g.setColour(theme::accent);
            g.drawEllipse(badge.reduced(0.75f), 1.5f);
            g.setColour(theme::text);
            g.setFont(theme::font(10.0f, true));
            g.drawText(groups[i].badge, badge, juce::Justification::centred);
            title.removeFromLeft(6.0f);
        }
        g.setColour(theme::dimText);
        g.setFont(theme::font(11.0f, true));
        g.drawText(groups[i].title.toUpperCase(), title, juce::Justification::centredLeft);
    }
}

//==============================================================================
ModuleTab::ModuleTab(APVTS& s, const char* enableParam, juce::String t, std::function<void()> select)
    : title(std::move(t))
    , onSelect(std::move(select))
    , attachment(s, enableParam, power)
{
    power.setTooltip("Switch " + title + " on or off");
    power.onStateChange = [this] { repaint(); };
    addAndMakeVisible(power);
}

void ModuleTab::setSelected(bool s)
{
    selected = s;
    repaint();
}

void ModuleTab::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (selected)
    {
        g.setColour(theme::panelRaised);
        g.fillRoundedRectangle(r.withTrimmedBottom(2.0f), 5.0f);
        g.setColour(theme::accent);
        g.fillRect(r.removeFromBottom(2.0f).reduced(6.0f, 0.0f));
    }
    g.setColour(selected ? theme::text : theme::dimText);
    g.setFont(theme::font(12.0f, true));
    g.drawFittedText(title.toUpperCase(), getLocalBounds().withTrimmedLeft(30).withTrimmedRight(6), juce::Justification::centredLeft, 1, 0.8f);
}

void ModuleTab::resized()
{
    power.setBounds(6, getHeight() / 2 - 10, 22, 20);
}

void ModuleTab::mouseUp(const juce::MouseEvent& e)
{
    if (getLocalBounds().contains(e.getPosition()) && onSelect)
        onSelect();
}

//==============================================================================
TuneDisplay::TuneDisplay(APVTS& s, const dsp::Tune& t)
    : state(s)
    , tune(t)
    , styles{{"Hard", 0.0f, "Hard tune: snaps to each note instantly. The robotic stepping effect."},
             {"Pop", 12.0f, "Pop: tight and polished, a hint of the effect on fast notes (12 ms)."},
             {"Melodic", 35.0f, "Melodic: smooth, catchy correction for sung rap and R&B (35 ms)."},
             {"Natural", 120.0f, "Natural: gentle correction that keeps slides and vibrato (120 ms)."}}
{
    input.fill(-1.0f);
    output.fill(-1.0f);
    for (const auto& style : styles)
    {
        auto* b = styleButtons.add(new juce::TextButton(style.name));
        b->setTooltip(style.tip);
        const float speed = style.speedMs;
        b->onClick = [this, speed] {
            for (const auto& [id, value] : {std::pair<const char*, float>{pid::tuneSpeed, speed}, {pid::tuneAmount, 100.0f}})
                if (auto* p = state.getParameter(id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(value));
                    p->endChangeGesture();
                }
            repaint();
        };
        addAndMakeVisible(b);
    }
    setTooltip("Grey = what you sing, pink = what the stream hears. Lighter rows are the notes of the chosen key.");
}

juce::String TuneDisplay::noteName(int midi)
{
    static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

void TuneDisplay::tick()
{
    const auto& h = tune.getHistory();
    const int written = h.written.load(std::memory_order_acquire);
    if (written == lastWritten)
        return;
    lastWritten = written;
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < kShown; ++i)
    {
        const int k = written - kShown + i;
        input[(size_t)i] = k < 0 ? -1.0f : h.input[(size_t)(k % dsp::Tune::kHistory)].load(std::memory_order_relaxed);
        output[(size_t)i] = k < 0 ? -1.0f : h.output[(size_t)(k % dsp::Tune::kHistory)].load(std::memory_order_relaxed);
        if (output[(size_t)i] > 0.0f && i > kShown * 3 / 4)
        {
            sum += output[(size_t)i];
            ++count;
        }
    }
    // Follow the singer slowly so the graph doesn't jump around.
    if (count > 0)
        centre += (sum / (float)count - centre) * 0.15f;

    // Light up the style that matches the current speed.
    const float speed = state.getRawParameterValue(pid::tuneSpeed)->load();
    for (size_t i = 0; i < styles.size(); ++i)
        styleButtons[(int)i]->setToggleState(std::abs(speed - styles[i].speedMs) < 0.5f, juce::dontSendNotification);
    repaint();
}

void TuneDisplay::resized()
{
    auto top = getLocalBounds().reduced(8, 6).removeFromTop(24);
    for (int i = styleButtons.size() - 1; i >= 0; --i)
    {
        styleButtons[i]->setBounds(top.removeFromRight(74));
        top.removeFromRight(4);
    }
    plot = getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(30.0f).withTrimmedLeft(36.0f);
}

void TuneDisplay::paint(juce::Graphics& g)
{
    const bool on = state.getRawParameterValue(pid::tuneOn)->load() >= 0.5f;
    const int key = (int)state.getRawParameterValue(pid::tuneKey)->load();
    const int scale = juce::jlimit(0, (int)dsp::Tune::scaleMasks.size() - 1, (int)state.getRawParameterValue(pid::tuneScale)->load());
    const auto mask = dsp::Tune::scaleMasks[(size_t)scale];

    constexpr float span = 7.0f; // semitones above and below the centre
    auto yFor = [&](float midi) { return plot.getCentreY() - (midi - centre) / span * plot.getHeight() * 0.5f; };

    // Note rows.
    const int lowNote = (int)std::floor(centre - span), highNote = (int)std::ceil(centre + span);
    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = yFor((float)n);
        if (y < plot.getY() - 1.0f || y > plot.getBottom() + 1.0f)
            continue;
        const int pc = ((n - key) % 12 + 12) % 12;
        const bool inScale = (mask & (1 << pc)) != 0;
        const float rowH = plot.getHeight() / (2.0f * span);
        if (inScale)
        {
            g.setColour(theme::panelRaised.withAlpha(pc == 0 ? 0.9f : 0.55f));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - rowH * 0.5f, plot.getWidth(), rowH).getIntersection(plot));
        }
        g.setColour(pc == 0 ? theme::accent.withAlpha(0.8f) : (inScale ? theme::dimText : theme::faintText.withAlpha(0.6f)));
        g.setFont(theme::font(10.0f, pc == 0));
        g.drawText(noteName(n), juce::Rectangle<float>(plot.getX() - 36.0f, y - 6.0f, 32.0f, 12.0f), juce::Justification::centredRight);
    }
    g.setColour(theme::outline);
    g.drawRect(plot, 1.0f);

    // Pitch lines.
    auto drawLine = [&](const std::array<float, kShown>& v, juce::Colour colour, float thickness) {
        juce::Path p;
        bool drawing = false;
        for (int i = 0; i < kShown; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float)i / (float)(kShown - 1);
            if (v[(size_t)i] <= 0.0f)
            {
                drawing = false;
                continue;
            }
            const float y = juce::jlimit(plot.getY(), plot.getBottom(), yFor(v[(size_t)i]));
            if (!drawing)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
            drawing = true;
        }
        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());
    drawLine(input, theme::dimText.withAlpha(0.8f), 1.5f);
    if (on)
        drawLine(output, theme::accent, 2.4f);
    g.restoreState();

    // Current note, top left.
    auto header = getLocalBounds().reduced(10, 6).removeFromTop(24).toFloat();
    const float last = input[kShown - 1], lastOut = output[kShown - 1];
    g.setFont(theme::font(13.0f, true));
    if (!on)
    {
        g.setColour(theme::faintText);
        g.drawText("Tune is off: switch it on with the square on the tab", header, juce::Justification::centredLeft);
    }
    else if (last > 0.0f)
    {
        const int note = (int)std::lround(lastOut);
        const int cents = (int)std::lround((last - (float)note) * 100.0f);
        g.setColour(theme::text);
        g.drawText(noteName(note), header.removeFromLeft(44.0f), juce::Justification::centredLeft);
        g.setColour(theme::dimText);
        g.setFont(theme::font(12.0f));
        g.drawText("you sang " + juce::String(cents > 0 ? "+" : "") + juce::String(cents) + " cents", header,
                   juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(theme::faintText);
        g.drawText("Sing to see your pitch", header, juce::Justification::centredLeft);
    }
    g.setColour(theme::faintText);
    g.setFont(theme::font(10.0f));
    g.drawText("adds 20 ms delay while on", plot.reduced(6.0f, 4.0f), juce::Justification::bottomRight);
}

//==============================================================================
DisplayPanel::DisplayPanel(APVTS& s, SpectrumTap& t, std::function<double()> sr, const dsp::Tune& tune)
    : tap(t)
    , sampleRate(sr)
    , eqTab(s, pid::eqOn, "EQ", [this] { select(0); })
    , mbTab(s, pid::mbOn, "Multiband", [this] { select(1); })
    , tuneTab(s, pid::tuneOn, "Tune", [this] { select(2); })
    , eq(s, analyzer, sr)
    , multiband(s, analyzer)
    , tuneDisplay(s, tune)
    , eqFields(s, {{"1", "Low Cut", {{pid::eqLowCut, "Freq"}, {pid::eqLowCutSlope, "Slope"}, {pid::eqPlosive, "Pops"}}},
                   {"2", "Low Shelf", {{pid::eqLowFreq, "Freq"}, {pid::eqLowGain, "Gain"}}},
                   {"3", "Mid", {{pid::eqMidFreq, "Freq"}, {pid::eqMidGain, "Gain"}, {pid::eqMidQ, "Q"}}},
                   {"4", "Air", {{pid::eqHighFreq, "Freq"}, {pid::eqHighGain, "Gain"}}},
                   {"5", "Tame", {{pid::eqTameFreq, "Freq"}, {pid::eqTameThreshold, "Thresh"}, {pid::eqTameRange, "Max Cut"}}}})
    , mbFields(s, {{"", "Band Splits", {{pid::mbLowCross, "Low | Mid"}, {pid::mbHighCross, "Mid | High"}}},
                   {"", "Low Band", {{pid::mbLowThreshold, "Threshold"}, {pid::mbLowGain, "Gain"}}},
                   {"", "Mid Band", {{pid::mbMidThreshold, "Threshold"}, {pid::mbMidGain, "Gain"}}},
                   {"", "High Band", {{pid::mbHighThreshold, "Threshold"}, {pid::mbHighGain, "Gain"}}},
                   {"", "Dynamics", {{pid::mbRatio, "Ratio"}, {pid::mbAttack, "Attack"}, {pid::mbRelease, "Release"}}}})
    , tuneFields(s, {{"", "Key", {{pid::tuneKey, "Key"}, {pid::tuneScale, "Scale"}, {pid::tuneVoice, "Voice"}}},
                     {"", "Correction", {{pid::tuneSpeed, "Speed"}, {pid::tuneAmount, "Amount"}}},
                     {"", "Voice", {{pid::tuneFormant, "Formant"}, {pid::tuneTranspose, "Pitch"}}}})
{
    for (auto* c : std::initializer_list<juce::Component*>{&eqTab, &mbTab, &tuneTab, &eq, &multiband, &tuneDisplay, &eqFields,
                                                           &mbFields, &tuneFields})
        addAndMakeVisible(c);
    eqTab.setTooltip("Show the EQ");
    mbTab.setTooltip("Show the multiband compressor");
    tuneTab.setTooltip("Show Tune (pitch correction)");
    select(0);
    tap.setActive(true);
}

DisplayPanel::~DisplayPanel()
{
    tap.setActive(false);
}

void DisplayPanel::select(int tab)
{
    current = tab;
    eqTab.setSelected(tab == 0);
    mbTab.setSelected(tab == 1);
    tuneTab.setSelected(tab == 2);
    eq.setVisible(tab == 0);
    eqFields.setVisible(tab == 0);
    multiband.setVisible(tab == 1);
    mbFields.setVisible(tab == 1);
    tuneDisplay.setVisible(tab == 2);
    tuneFields.setVisible(tab == 2);
}

void DisplayPanel::tick(const dsp::VocalChain::Activity& activity)
{
    multiband.setReductions(activity.multibandReductionDb);
    eq.setTameReduction(activity.tameReductionDb);
    if (current == 2)
    {
        tuneDisplay.tick();
        return;
    }
    const bool changed = analyzer.update(tap, sampleRate ? sampleRate() : 48000.0);
    if (changed || current == 1)
        (current == 0 ? static_cast<juce::Component&>(eq) : multiband).repaint();
}

void DisplayPanel::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    g.setColour(theme::faintText);
    g.setFont(theme::font(10.5f));
    if (current != 2)
        g.drawText("OUTPUT SPECTRUM", getLocalBounds().removeFromTop(30).reduced(12, 0), juce::Justification::centredRight);
}

void DisplayPanel::resized()
{
    auto top = getLocalBounds().removeFromTop(30).reduced(6, 3);
    eqTab.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(4);
    mbTab.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(4);
    tuneTab.setBounds(top.removeFromLeft(90));

    auto body = getLocalBounds().withTrimmedTop(30).reduced(4, 2);
    const auto fieldsArea = body.removeFromBottom(fieldRowHeight).reduced(4, 4);
    eqFields.setBounds(fieldsArea);
    mbFields.setBounds(fieldsArea);
    tuneFields.setBounds(fieldsArea);
    eq.setBounds(body);
    multiband.setBounds(body);
    tuneDisplay.setBounds(body);
}

} // namespace stagemic
