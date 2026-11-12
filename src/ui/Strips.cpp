// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Strips.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace
{
constexpr int knobW = 58;
constexpr int knobH = 72;

/** Places knobs centred in a grid inside area. */
void placeGrid(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int columns)
{
    const int rows = ((int)items.size() + columns - 1) / columns;
    const int cellW = area.getWidth() / columns;
    const int cellH = area.getHeight() / juce::jmax(1, rows);
    int i = 0;
    for (auto* c : items)
    {
        const auto cell = juce::Rectangle<int>(area.getX() + (i % columns) * cellW, area.getY() + (i / columns) * cellH, cellW, cellH);
        c->setBounds(cell.withSizeKeepingCentre(juce::jmin(knobW + 8, cellW), juce::jmin(knobH, cellH)));
        ++i;
    }
}
} // namespace

//==============================================================================
GateStrip::GateStrip(APVTS& s)
    : Section(s, pid::gateOn, "Gate")
    , threshold(s, pid::gateThreshold, "Threshold")
    , range(s, pid::gateRange, "Range")
    , release(s, pid::gateRelease, "Release")
{
    for (auto* c : std::initializer_list<juce::Component*>{&threshold, &range, &release})
        addAndMakeVisible(c);
}

void GateStrip::layoutContent(juce::Rectangle<int> area)
{
    auto knobs = area.removeFromBottom(knobH * 2 + 4);
    threshold.setBounds(area);
    placeGrid(knobs, {&range, &release}, 1);
}

void GateStrip::updateActivity(const dsp::VocalChain::Activity& a)
{
    if (std::abs(a.gateGain - gateGain) > 0.01f)
    {
        gateGain = a.gateGain;
        repaint(getWidth() - 24, 0, 24, headerHeight);
    }
}

void GateStrip::paintOverChildren(juce::Graphics& g)
{
    // Small LED in the header: lit while the gate lets the voice through.
    const auto led = juce::Rectangle<float>(8.0f, 8.0f).withCentre({(float)getWidth() - 14.0f, headerHeight * 0.5f});
    const bool open = isOn() && gateGain > 0.5f;
    g.setColour(open ? theme::meterGreen : theme::background);
    g.fillEllipse(led);
    g.setColour(theme::outline);
    g.drawEllipse(led, 1.0f);
}

//==============================================================================
DeEsserStrip::DeEsserStrip(APVTS& s)
    : Section(s, pid::dsOn, "De-Ess")
    , threshold(s, pid::dsThreshold, "Threshold")
    , freq(s, pid::dsFreq, "Freq")
    , maxCut(s, pid::dsRange, "Max Cut")
{
    for (auto* c : std::initializer_list<juce::Component*>{&threshold, &reduction, &freq, &maxCut})
        addAndMakeVisible(c);
}

void DeEsserStrip::layoutContent(juce::Rectangle<int> area)
{
    auto knobs = area.removeFromBottom(knobH * 2 + 4);
    reduction.setBounds(area.removeFromRight(8).withTrimmedTop(23).withTrimmedBottom(26));
    threshold.setBounds(area);
    placeGrid(knobs, {&freq, &maxCut}, 1);
}

void DeEsserStrip::updateActivity(const dsp::VocalChain::Activity& a)
{
    reduction.setReduction(a.deEsserReductionDb);
}

//==============================================================================
CompressorStrip::CompressorStrip(APVTS& s)
    : Section(s, pid::compOn, "Compressor")
    , threshold(s, pid::compThreshold, "Threshold")
    , ratio(s, pid::compRatio, "Ratio")
    , attack(s, pid::compAttack, "Attack")
    , release(s, pid::compRelease, "Release")
    , makeup(s, pid::compMakeup, "Makeup")
{
    for (auto* c : std::initializer_list<juce::Component*>{&threshold, &reduction, &ratio, &attack, &release, &makeup})
        addAndMakeVisible(c);
}

void CompressorStrip::layoutContent(juce::Rectangle<int> area)
{
    auto left = area.removeFromLeft(80);
    reduction.setBounds(left.removeFromRight(10).withTrimmedTop(23).withTrimmedBottom(26));
    threshold.setBounds(left);
    area.removeFromLeft(6);
    placeGrid(area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), knobH * 2 + 40)),
              {&ratio, &attack, &release, &makeup}, 2);
}

void CompressorStrip::updateActivity(const dsp::VocalChain::Activity& a)
{
    reduction.setReduction(a.compressorReductionDb);
}

//==============================================================================
SaturationStrip::SaturationStrip(APVTS& s)
    : Section(s, pid::satOn, "Saturation")
    , drive(s, pid::satDrive, "Drive")
    , mix(s, pid::satMix, "Mix")
{
    addAndMakeVisible(drive);
    addAndMakeVisible(mix);
}

void SaturationStrip::layoutContent(juce::Rectangle<int> area)
{
    drive.setBounds(area.removeFromLeft(area.getWidth() / 2));
    mix.setBounds(area);
}

//==============================================================================
EffectRow::EffectRow(APVTS& s, ModuleId m, std::initializer_list<std::pair<const char*, const char*>> list, bool showReduction)
    : Section(s, describe(m).enableParam, describe(m).displayName, Header::left)
    , module(m)
{
    for (const auto& [id, name] : list)
    {
        knobs.push_back(std::make_unique<SmallKnob>(s, id, name));
        addAndMakeVisible(*knobs.back());
    }
    if (showReduction)
    {
        reduction = std::make_unique<ReductionMeter>(12.0f);
        addAndMakeVisible(*reduction);
    }
}

void EffectRow::layoutContent(juce::Rectangle<int> area)
{
    if (reduction != nullptr)
        reduction->setBounds(area.removeFromRight(8).reduced(0, 6));
    for (auto& k : knobs)
        k->setBounds(area.removeFromLeft(knobW).withSizeKeepingCentre(knobW, juce::jmin(knobH, area.getHeight())));
}

void EffectRow::updateActivity(const dsp::VocalChain::Activity& a)
{
    if (reduction != nullptr && module == ModuleId::Limiter)
        reduction->setReduction(a.limiterReductionDb);
}

} // namespace stagemic
