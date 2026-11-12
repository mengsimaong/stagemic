// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Strips.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace
{
constexpr int knobW = 58;
constexpr int knobH = 72;

/** Places items centred in a grid inside area. */
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

/** Fader on the left, a single column of knobs on the right. */
void faderWithKnobColumn(juce::Rectangle<int> area, juce::Component& fader, juce::Component* meter,
                         std::initializer_list<juce::Component*> knobs)
{
    auto column = area.removeFromRight(knobW + 2);
    if (meter != nullptr)
        meter->setBounds(area.removeFromRight(8).withTrimmedTop(23).withTrimmedBottom(26));
    fader.setBounds(area);
    const int used = (int)knobs.size() * knobH;
    placeGrid(column.withSizeKeepingCentre(column.getWidth(), juce::jmin(column.getHeight(), used + 20)), knobs, 1);
}
} // namespace

//==============================================================================
GateStrip::GateStrip(APVTS& s)
    : Section(s, pid::gateOn, "Gate")
    , threshold(s, pid::gateThreshold, "Threshold")
    , range(s, pid::gateRange, "Range")
    , release(s, pid::gateRelease, "Release")
    , ratio(s, pid::gateRatio, "Ratio")
{
    for (auto* c : std::initializer_list<juce::Component*>{&threshold, &range, &release, &ratio})
        addAndMakeVisible(c);
}

void GateStrip::layoutContent(juce::Rectangle<int> area)
{
    faderWithKnobColumn(area, threshold, nullptr, {&range, &release, &ratio});
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
    // Small LED in the header: lit while the voice passes through (not being turned down).
    const auto led = juce::Rectangle<float>(8.0f, 8.0f).withCentre({(float)getWidth() - 14.0f, headerHeight * 0.5f});
    const bool open = isOn() && gateGain > 0.7f;
    g.setColour(open ? theme::meterGreen : theme::background);
    g.fillEllipse(led);
    g.setColour(theme::outline);
    g.drawEllipse(led, 1.0f);
}

//==============================================================================
DeEsserStrip::DeEsserStrip(APVTS& s, dsp::VocalChain& c)
    : Section(s, pid::dsOn, "De-Ess")
    , chain(c)
    , threshold(s, pid::dsThreshold, "Threshold")
    , freq(s, pid::dsFreq, "Freq")
    , maxCut(s, pid::dsRange, "Max Cut")
    , mode(s, pid::dsMode, "Mode")
    , autoAttachment(s, pid::dsAuto, autoButton)
{
    autoButton.setClickingTogglesState(true);
    autoButton.setTooltip("Auto: find the \"s\" frequency automatically (shown on the button)");
    listenButton.setClickingTogglesState(true);
    listenButton.setTooltip("Listen: hear only what the de-esser hears, to find the right frequency. "
                            "Remember to switch it off again!");
    listenButton.onClick = [this] { chain.setDeEsserListen(listenButton.getToggleState()); };
    for (auto* b : {&autoButton, &listenButton})
        b->setColour(juce::TextButton::buttonOnColourId, theme::accent);

    for (auto* comp : std::initializer_list<juce::Component*>{&threshold, &reduction, &freq, &maxCut, &mode, &autoButton, &listenButton})
        addAndMakeVisible(comp);
}

DeEsserStrip::~DeEsserStrip()
{
    chain.setDeEsserListen(false); // never leave Listen on when the window closes
}

void DeEsserStrip::layoutContent(juce::Rectangle<int> area)
{
    auto buttons = area.removeFromBottom(22);
    autoButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(1, 0));
    listenButton.setBounds(buttons.reduced(1, 0));
    area.removeFromBottom(4);
    faderWithKnobColumn(area, threshold, &reduction, {&freq, &maxCut, &mode});
}

void DeEsserStrip::updateActivity(const dsp::VocalChain::Activity& a)
{
    reduction.setReduction(a.deEsserReductionDb);
    const auto text = autoButton.getToggleState() ? "Auto " + juce::String(a.deEsserFrequency / 1000.0f, 1) + "k"
                                                  : juce::String("Auto");
    if (autoButton.getButtonText() != text)
        autoButton.setButtonText(text);
    if (listenButton.getToggleState() != chain.getDeEsserListen())
        listenButton.setToggleState(chain.getDeEsserListen(), juce::dontSendNotification);
}

//==============================================================================
CompressorStrip::CompressorStrip(APVTS& s)
    : Section(s, pid::compOn, "Compressor")
    , threshold(s, pid::compThreshold, "Threshold")
    , ratio(s, pid::compRatio, "Ratio")
    , attack(s, pid::compAttack, "Attack")
    , release(s, pid::compRelease, "Release")
    , makeup(s, pid::compMakeup, "Makeup")
    , mix(s, pid::compMix, "Mix")
{
    mix.setTooltip("Mix: below 100% blends in the uncompressed voice (parallel compression)");
    for (auto* c : std::initializer_list<juce::Component*>{&threshold, &reduction, &ratio, &attack, &release, &makeup, &mix})
        addAndMakeVisible(c);
}

void CompressorStrip::layoutContent(juce::Rectangle<int> area)
{
    auto left = area.removeFromLeft(76);
    reduction.setBounds(left.removeFromRight(10).withTrimmedTop(23).withTrimmedBottom(26));
    threshold.setBounds(left);
    area.removeFromLeft(4);
    placeGrid(area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), knobH * 3 + 30)),
              {&ratio, &attack, &release, &makeup, &mix}, 2);
}

void CompressorStrip::updateActivity(const dsp::VocalChain::Activity& a)
{
    reduction.setReduction(a.compressorReductionDb);
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

//==============================================================================
TabbedModules::TabbedModules(APVTS& s, std::vector<Tab> tabs, juce::String footerText, int initialTab, int columns)
    : footer(std::move(footerText))
    , current(initialTab)
    , maxColumns(columns)
{
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        const auto& d = describe(tabs[i].module);
        Page page;
        page.tab = std::make_unique<ModuleTab>(s, d.enableParam, tabs[i].title, [this, i] { select((int)i); });
        page.tab->setTooltip("Show " + juce::String(d.displayName) + " (the square switches it on or off)");
        addAndMakeVisible(*page.tab);

        for (const auto& c : tabs[i].controls)
        {
            std::unique_ptr<juce::Component> control;
            if (dynamic_cast<juce::AudioParameterChoice*>(s.getParameter(c.paramId)) != nullptr)
                control = std::make_unique<ChoiceToggle>(s, c.paramId, c.label);
            else
            {
                auto knob = std::make_unique<SmallKnob>(s, c.paramId, c.label);
                if (c.tooltip != nullptr)
                    knob->setTooltip(c.tooltip);
                control = std::move(knob);
            }
            addChildComponent(*control);
            page.controls.push_back(std::move(control));
        }
        pages.push_back(std::move(page));
    }

    select(current);
}

void TabbedModules::select(int index)
{
    current = juce::jlimit(0, (int)pages.size() - 1, index);
    for (int i = 0; i < (int)pages.size(); ++i)
    {
        pages[(size_t)i].tab->setSelected(i == current);
        for (auto& c : pages[(size_t)i].controls)
            c->setVisible(i == current);
    }
    resized();
}

void TabbedModules::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);

    if (!pages.empty())
        ModuleTab::paintTrack(g, getLocalBounds().reduced(1).withBottom(pages.front().tab->getBottom()), {});

    g.setColour(theme::faintText);
    g.setFont(theme::font(10.5f));
    g.drawFittedText(footer, getLocalBounds().removeFromBottom(18).reduced(8, 0), juce::Justification::centredRight, 1, 0.7f);
}

void TabbedModules::resized()
{
    if (pages.empty())
        return;

    auto area = getLocalBounds().reduced(6, 4);
    auto tabs = area.removeFromTop(32).reduced(3, 3);
    const int tabW = tabs.getWidth() / (int)pages.size();
    for (auto& p : pages)
        p.tab->setBounds(tabs.removeFromLeft(tabW).reduced(1, 0));

    area.removeFromTop(4);
    area.removeFromBottom(16);
    auto& controls = pages[(size_t)current].controls;
    if (controls.empty())
        return;
    const int columns = juce::jmin(maxColumns, (int)controls.size());
    const int rows = ((int)controls.size() + columns - 1) / columns;
    const auto grid = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), columns * (knobW + 10)),
                                                 juce::jmin(area.getHeight(), rows * (knobH + 8)));
    const int cellW = grid.getWidth() / columns, cellH = grid.getHeight() / rows;
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setBounds(juce::Rectangle<int>(grid.getX() + (int)(i % (size_t)columns) * cellW,
                                                    grid.getY() + (int)(i / (size_t)columns) * cellH, cellW, cellH)
                                   .withSizeKeepingCentre(juce::jmin(cellW, knobW + 8), knobH));
}

} // namespace stagemic
