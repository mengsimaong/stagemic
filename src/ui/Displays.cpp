// SPDX-License-Identifier: AGPL-3.0-or-later
// Display panel: tabs and the knob rows under the graphs
#include "ui/Displays.h"

#include "ui/GraphHelpers.h"
#include "ui/Theme.h"

namespace stagemic
{

using namespace graph;

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
            g.setColour(theme::tintOf(*this));
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
    theme::setTint(*this, theme::familyOfEnableParam(enableParam));
    power.setTooltip("Switch " + title + " on or off");
    power.onStateChange = [this] { repaint(); };
    addAndMakeVisible(power);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Click to show " + title + ". The square switches it on or off.");
}

void ModuleTab::setSelected(bool s)
{
    selected = s;
    repaint();
}

namespace
{
const juce::Colour cardColour = theme::panel;
}

void ModuleTab::paintTrack(juce::Graphics& g, juce::Rectangle<int> strip, juce::Rectangle<int>)
{
    const auto r = strip.toFloat();
    juce::Path p;
    p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.0f, 6.0f, true, true, false, false);
    g.setColour(theme::background);
    g.fillPath(p);
}

void ModuleTab::paint(juce::Graphics& g)
{
    const bool on = power.getToggleState();
    const auto t = getLocalBounds().toFloat().withTrimmedLeft(1.0f).withTrimmedRight(1.0f);
    juce::Path p;
    if (selected)
    {
        // Reaches 2 px into the card below, so there's no seam between them
        p.addRoundedRectangle(t.getX(), t.getY(), t.getWidth(), t.getHeight() + 2.0f, 7.0f, 7.0f, true, true, false, false);
        g.setColour(cardColour);
    }
    else
    {
        const auto u = t.withTrimmedTop(3.0f).withTrimmedBottom(2.0f);
        p.addRoundedRectangle(u.getX(), u.getY(), u.getWidth(), u.getHeight(), 7.0f, 7.0f, true, true, false, false);
        g.setColour(hover ? theme::panelRaised : theme::panelOff);
    }
    g.fillPath(p);

    // An effect that is on stays readable even when its tab isn't open
    g.setColour(selected || hover ? theme::text : (on ? theme::text.withAlpha(0.72f) : theme::dimText));
    g.setFont(theme::font(12.0f, true));
    g.drawFittedText(title.toUpperCase(), getLocalBounds().withTrimmedLeft(28).withTrimmedRight(4), juce::Justification::centredLeft, 1,
                     0.65f);
}

void ModuleTab::resized()
{
    power.setBounds(5, getHeight() / 2 - 10, 22, 20);
}

void ModuleTab::mouseUp(const juce::MouseEvent& e)
{
    if (getLocalBounds().contains(e.getPosition()) && onSelect)
        onSelect();
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
    , eqFields(s)
    , mbFields(s, {{"", "Band Splits", {{pid::mbLowCross, "Low | Mid"}, {pid::mbHighCross, "Mid | High"}}},
                   {"", "Low Band", {{pid::mbLowThreshold, "Threshold"}, {pid::mbLowGain, "Gain"}}},
                   {"", "Mid Band", {{pid::mbMidThreshold, "Threshold"}, {pid::mbMidGain, "Gain"}}},
                   {"", "High Band", {{pid::mbHighThreshold, "Threshold"}, {pid::mbHighGain, "Gain"}}},
                   {"", "Dynamics", {{pid::mbRatio, "Ratio"}, {pid::mbAttack, "Attack"}, {pid::mbRelease, "Release"}}}})
    , tuneFields(s, {{"", "Key", {{pid::tuneKey, "Key"}, {pid::tuneScale, "Scale"}, {pid::tuneVoice, "Voice"}}},
                     {"", "Correction", {{pid::tuneSpeed, "Speed"}, {pid::tuneAmount, "Amount"}}},
                     {"", "Voice", {{pid::tuneFormant, "Formant"}, {pid::tuneTranspose, "Pitch"}}}})
{
    for (auto* c : std::initializer_list<juce::Component*>{&eqTab, &mbTab, &tuneTab, &eq, &multiband, &tuneDisplay, &eqFields, &mbFields,
                                                           &tuneFields})
        addAndMakeVisible(c);
    eqTab.setTooltip("Show the EQ");
    mbTab.setTooltip("Show the multiband compressor");
    tuneTab.setTooltip("Show Tune (pitch correction)");
    for (auto* c : std::initializer_list<juce::Component*>{&eq, &eqFields})
        theme::setTint(*c, theme::family(ModuleId::Equalizer));
    for (auto* c : std::initializer_list<juce::Component*>{&multiband, &mbFields})
        theme::setTint(*c, theme::family(ModuleId::Multiband));
    theme::setTint(tuneFields, theme::family(ModuleId::Tune));
    // The graph and the bar under it share one selected point
    eq.onSelect = [this](int i) { eqFields.select(i); };
    eqFields.onSelect = [this](int i) { eq.setSelected(i); };
    eq.setSelected(eqFields.getSelected());
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
    setLive(live);
}

void DisplayPanel::setLive(bool shouldBeLive)
{
    live = shouldBeLive;
    // The spectrum is only needed on the EQ and Multiband tabs while they're
    tap.setActive(live && current != 2);
}

void DisplayPanel::tick(const dsp::VocalChain::Activity& activity)
{
    if (!live)
        return;
    multiband.setReductions(activity.multibandReductionDb);
    eq.setTameReduction(activity.tameReductionDb);
    if (current == 0)
        eqFields.refresh();
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
    {
        const auto strip = getLocalBounds().reduced(1).withBottom(eqTab.getBottom());
        ModuleTab::paintTrack(g, strip, {});
    }
    g.setColour(theme::faintText);
    g.setFont(theme::font(10.5f));
    if (current != 2)
        g.drawText("OUTPUT SPECTRUM", getLocalBounds().removeFromTop(30).reduced(12, 0), juce::Justification::centredRight);
}

void DisplayPanel::resized()
{
    auto top = getLocalBounds().removeFromTop(32).reduced(10, 5);
    eqTab.setBounds(top.removeFromLeft(84));
    top.removeFromLeft(4);
    mbTab.setBounds(top.removeFromLeft(128));
    top.removeFromLeft(4);
    tuneTab.setBounds(top.removeFromLeft(94));

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
