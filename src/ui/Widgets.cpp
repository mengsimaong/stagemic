// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Widgets.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace
{
void setupLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(theme::font(11.5f));
    l.setColour(juce::Label::textColourId, theme::dimText);
    l.setMinimumHorizontalScale(0.6f);
    l.setInterceptsMouseClicks(false, false);
}

double defaultOf(APVTS& state, const juce::String& id)
{
    auto* p = state.getParameter(id);
    return state.getParameterRange(id).convertFrom0to1(p->getDefaultValue());
}

juce::String tooltipFor(APVTS& state, const juce::String& id)
{
    return state.getParameter(id)->getName(64) + "  (double-click to reset)";
}
} // namespace

//==============================================================================
PowerButton::PowerButton()
    : juce::Button("power")
{
    setClickingTogglesState(true);
    setTooltip("Switch on/off");
}

void PowerButton::paintButton(juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced(2.0f).withSizeKeepingCentre(14.0f, 14.0f);
    if (getToggleState())
    {
        const auto tint = theme::tintOf(*this);
        g.setColour(tint.withAlpha(0.25f));
        g.fillRoundedRectangle(r.expanded(2.5f), 4.0f);
        g.setColour(tint);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRoundedRectangle(r.reduced(3.5f).withHeight(2.5f), 1.0f);
    }
    else
    {
        g.setColour(theme::background);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(over ? theme::dimText : theme::faintText);
        g.drawRoundedRectangle(r, 3.0f, 1.2f);
    }
}

//==============================================================================
SmallKnob::SmallKnob(APVTS& state, const juce::String& id, const juce::String& name)
    : attachment(state, id, slider)
{
    setupLabel(label, name);
    addAndMakeVisible(label);

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setDoubleClickReturnValue(true, defaultOf(state, id));
    slider.setTooltip(tooltipFor(state, id));
    addAndMakeVisible(slider);
}

void SmallKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(15));
    slider.setBounds(area);
}

//==============================================================================
ValueKnob::ValueKnob(APVTS& state, const juce::String& id, const juce::String& name)
    : attachment(state, id, slider)
{
    setupLabel(label, name);
    addAndMakeVisible(label);

    valueText.setJustificationType(juce::Justification::centred);
    valueText.setFont(theme::font(12.0f, true));
    valueText.setMinimumHorizontalScale(0.75f);
    valueText.setBorderSize({0, 1, 0, 1});
    valueText.setColour(juce::Label::textColourId, theme::text);
    valueText.setColour(juce::Label::backgroundWhenEditingColourId, theme::background);
    valueText.setColour(juce::Label::outlineWhenEditingColourId, theme::accent);
    valueText.setColour(juce::TextEditor::highlightColourId, theme::accent.withAlpha(0.4f));
    valueText.setEditable(true, true, false); // single click to type
    valueText.setTooltip("Click to type an exact value (e.g. 2.5k, -3, +4.5 dB)");
    valueText.onTextChange = [this] {
        slider.setValue(slider.getValueFromText(valueText.getText()), juce::sendNotificationSync);
        refreshText(); // show it in its proper format (clamped to the allowed range)
    };
    addAndMakeVisible(valueText);

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue(true, defaultOf(state, id));
    slider.setTooltip(tooltipFor(state, id));
    slider.onValueChange = [this] { refreshText(); };
    addAndMakeVisible(slider);

    refreshText();
}

void ValueKnob::refreshText()
{
    if (!valueText.isBeingEdited())
        valueText.setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification);
}

void ValueKnob::resized()
{
    // Same order everywhere: name, knob, value.
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(15));
    valueText.setBounds(area.removeFromBottom(17));
    const int size = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(size, size));
}

//==============================================================================
ChoiceToggle::ChoiceToggle(APVTS& state, const juce::String& id, const juce::String& name)
    : parameter(dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id)))
    , attachment(*state.getParameter(id), [this](float) { refresh(); }, nullptr)
{
    jassert(parameter != nullptr);
    setupLabel(label, name);
    addAndMakeVisible(label);

    const bool longList = parameter->choices.size() > 3;
    button.setTooltip(parameter->getName(64) + ": "
                      + (longList ? juce::String("click to choose") : parameter->choices.joinIntoString(" / ") + " (click to switch)"));
    button.onClick = [this, longList] {
        if (!longList)
        {
            const int next = (parameter->getIndex() + 1) % parameter->choices.size();
            attachment.setValueAsCompleteGesture((float)next);
            return;
        }
        // Many choices (key, scale): a menu instead of cycling through them all.
        juce::PopupMenu menu;
        for (int i = 0; i < parameter->choices.size(); ++i)
            menu.addItem(i + 1, parameter->choices[i], true, i == parameter->getIndex());
        juce::Component::SafePointer<ChoiceToggle> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&button), [safe](int result) {
            if (safe != nullptr && result > 0)
                safe->attachment.setValueAsCompleteGesture((float)(result - 1));
        });
    };
    addAndMakeVisible(button);
    attachment.sendInitialUpdate();
}

void ChoiceToggle::refresh()
{
    button.setButtonText(parameter->getCurrentChoiceName());
}

void ChoiceToggle::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(15)); // name on top, like the knobs
    button.setBounds(area.withSizeKeepingCentre(juce::jmin(area.getWidth(), 120), 24));
}

//==============================================================================
Fader::Fader(APVTS& state, const juce::String& id, const juce::String& name)
    : attachment(state, id, slider)
{
    setupLabel(label, name);
    addAndMakeVisible(label);

    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setDoubleClickReturnValue(true, defaultOf(state, id));
    slider.setTooltip(tooltipFor(state, id));
    addAndMakeVisible(slider);
}

void Fader::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(15));
    slider.setBounds(area);
}

//==============================================================================
MeterBar::MeterBar(juce::String text)
    : label(std::move(text))
{
}

void MeterBar::setLevel(float peak)
{
    level = juce::jmax(peak, level * 0.85f); // ~28 dB/s fall at 20 fps
    if (peak >= hold)
    {
        hold = peak;
        holdTicks = 30; // 1.5 s at 20 updates a second
    }
    else if (--holdTicks <= 0)
        hold = juce::jmax(level, hold * 0.92f);
    // Only redraw when the bar visibly moves (about 0.25 dB), not every frame in silence.
    auto toDb = [](float v) { return juce::Decibels::gainToDecibels(v, -60.0f); };
    if (std::abs(toDb(level) - toDb(paintedLevel)) > 0.25f || std::abs(toDb(hold) - toDb(paintedHold)) > 0.25f)
    {
        // Redraw only the slice between the old and the new level, and the two
        // places of the peak line: a few pixels instead of the whole bar.
        const auto area = barArea();
        auto slice = [&](float a, float b) {
            const float top = juce::jmin(a, b) - 3.0f, bottom = juce::jmax(a, b) + 3.0f;
            repaint(juce::Rectangle<float>(area.getX(), top, area.getWidth(), bottom - top).getSmallestIntegerContainer());
        };
        if (paintedLevel < 0.0f)
            repaint();
        else
        {
            slice(yFor(paintedLevel), yFor(level));
            slice(yFor(paintedHold), yFor(paintedHold));
            slice(yFor(hold), yFor(hold));
        }
        paintedLevel = level;
        paintedHold = hold;
    }
}

juce::Rectangle<float> MeterBar::barArea() const
{
    auto area = getLocalBounds().toFloat();
    if (label.isNotEmpty())
        area.removeFromBottom(16.0f);
    return area;
}

float MeterBar::yFor(float linear) const
{
    constexpr float minDb = -60.0f;
    const auto area = barArea();
    const float db = juce::Decibels::gainToDecibels(linear, minDb);
    return area.getBottom() - area.getHeight() * juce::jlimit(0.0f, 1.0f, (db - minDb) / -minDb);
}

void MeterBar::paint(juce::Graphics& g)
{
    constexpr float minDb = -60.0f;
    auto area = getLocalBounds().toFloat();

    if (label.isNotEmpty())
    {
        g.setColour(theme::dimText);
        g.setFont(theme::font(11.0f, true));
        g.drawText(label, area.removeFromBottom(16.0f), juce::Justification::centred);
    }

    g.setColour(theme::background);
    g.fillRoundedRectangle(area, 3.0f);

    auto toY = [&](float linear) {
        const float db = juce::Decibels::gainToDecibels(linear, minDb);
        return area.getBottom() - area.getHeight() * juce::jlimit(0.0f, 1.0f, (db - minDb) / -minDb);
    };

    const auto bar = area.reduced(2.0f);
    const float top = toY(level);
    if (top < bar.getBottom())
    {
        juce::ColourGradient grad(theme::meterRed, 0.0f, toY(1.0f), theme::meterGreen, 0.0f, toY(juce::Decibels::decibelsToGain(-18.0f)), false);
        grad.addColour(0.15, theme::meterYellow);
        g.setGradientFill(grad);
        g.fillRect(bar.withTop(top));
    }

    if (hold > 0.0f)
    {
        g.setColour(hold >= 1.0f ? theme::meterRed : theme::text.withAlpha(0.8f));
        g.fillRect(bar.getX(), toY(hold) - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
ReductionMeter::ReductionMeter(float max)
    : maxDb(max)
{
    setTooltip("Gain reduction: how much this module is turning the voice down right now");
}

void ReductionMeter::setReduction(float db)
{
    const float next = juce::jlimit(0.0f, maxDb, db);
    if (std::abs(next - value) > 0.05f)
    {
        value = next;
        repaint();
    }
}

void ReductionMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(theme::background);
    g.fillRoundedRectangle(area, 2.0f);
    const auto bar = area.reduced(1.5f);
    g.setColour(theme::reduction);
    g.fillRect(bar.withHeight(bar.getHeight() * value / maxDb));
}

//==============================================================================
Section::Section(APVTS& state, const char* enableParam, juce::String t, Header h)
    : title(std::move(t))
    , headerStyle(h)
    , attachment(state, enableParam, power)
{
    theme::setTint(*this, theme::familyOfEnableParam(enableParam));
    power.setTooltip("Switch " + title + " on or off");
    power.onStateChange = [this] {
        const bool on = isOn();
        for (auto* child : getChildren())
            if (child != &power)
                child->setAlpha(on ? 1.0f : 0.4f);
        repaint();
    };
    addAndMakeVisible(power);
}

void Section::paint(juce::Graphics& g)
{
    const bool on = isOn();
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(on ? theme::panel : theme::panelOff);
    g.fillRoundedRectangle(r, 6.0f);

    const bool top = headerStyle == Header::top;
    auto header = top ? r.withHeight((float)headerHeight) : r.withWidth((float)sideHeaderWidth);
    g.setColour(on ? theme::panelRaised : theme::panelOff.brighter(0.04f));
    g.fillRoundedRectangle(header, 6.0f);
    if (top)
        g.fillRect(header.withTop(header.getCentreY()));
    else
        g.fillRect(header.withLeft(header.getCentreX()));

    g.setColour(theme::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);

    g.setColour(on ? theme::text : theme::dimText);
    g.setFont(theme::font(12.0f, true));
    const auto titleArea = top ? header.withTrimmedLeft(30.0f).withTrimmedRight(8.0f)
                               : header.withTrimmedLeft(30.0f).withTrimmedRight(4.0f);
    g.drawFittedText(title.toUpperCase(), titleArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
}

void Section::resized()
{
    if (headerStyle == Header::top)
    {
        power.setBounds(6, 4, 22, headerHeight - 8);
        layoutContent(getLocalBounds().withTrimmedTop(headerHeight).reduced(6, 6));
    }
    else
    {
        power.setBounds(6, getHeight() / 2 - 10, 22, 20);
        layoutContent(getLocalBounds().withTrimmedLeft(sideHeaderWidth).reduced(6, 4));
    }
    power.onStateChange();
}

} // namespace stagemic
