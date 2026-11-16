// SPDX-License-Identifier: AGPL-3.0-or-later
// Small reusable controls for the StageMic window
#pragma once

#include "core/Parameters.h"
#include "core/dsp/VocalChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

using APVTS = juce::AudioProcessorValueTreeState;

class PowerButton final : public juce::Button
{
public:
    PowerButton();
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

// Small rotary knob with its name above and value below
class SmallKnob final : public juce::Component
{
public:
    SmallKnob(APVTS& state, const juce::String& paramId, const juce::String& name);
    void resized() override;
    void setTooltip(const juce::String& text) { slider.setTooltip(text); }

private:
    juce::Label label;
    juce::Slider slider;
    APVTS::SliderAttachment attachment;
};

// Knob with its name on top and an editable value underneath
class ValueKnob final : public juce::Component
{
public:
    ValueKnob(APVTS& state, const juce::String& paramId, const juce::String& name);
    void resized() override;

private:
    void refreshText();

    juce::Label label, valueText;
    juce::Slider slider;
    APVTS::SliderAttachment attachment;
};

class ChoiceToggle final : public juce::Component
{
public:
    ChoiceToggle(APVTS& state, const juce::String& paramId, const juce::String& name);
    void resized() override;

private:
    void refresh();

    juce::Label label;
    juce::TextButton button;
    juce::AudioParameterChoice* parameter;
    juce::ParameterAttachment attachment;
};

// Vertical fader with its name above and value below
class Fader final : public juce::Component
{
public:
    Fader(APVTS& state, const juce::String& paramId, const juce::String& name);
    void resized() override;

private:
    juce::Label label;
    juce::Slider slider;
    APVTS::SliderAttachment attachment;
};

// Vertical level meter with peak hold
class MeterBar final : public juce::Component
{
public:
    explicit MeterBar(juce::String label = {});
    void setLevel(float linearPeak);
    void paint(juce::Graphics&) override;

private:
    juce::Rectangle<float> barArea() const;
    float yFor(float linear) const;

    juce::String label;
    float paintedLevel = -1.0f, paintedHold = -1.0f;
    float level = 0.0f, hold = 0.0f;
    int holdTicks = 0;
};

// Gain reduction bar that grows downward
class ReductionMeter final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit ReductionMeter(float maxDb = 24.0f);
    void setReduction(float db);
    void paint(juce::Graphics&) override;

private:
    float maxDb;
    float value = 0.0f;
};

class Section : public juce::Component
{
public:
    enum class Header
    {
        top, // title bar across the top
        left // title on the left
    };

    Section(APVTS& state, const char* enableParam, juce::String title, Header header = Header::top);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Called ~30 times a second with live meter values
    virtual void updateActivity(const dsp::VocalChain::Activity&) {}

protected:
    // Lay out controls inside the area below the header
    virtual void layoutContent(juce::Rectangle<int> area) = 0;
    bool isOn() const { return power.getToggleState(); }

    static constexpr int headerHeight = 28;
    static constexpr int sideHeaderWidth = 92;
    juce::String title;
    Header headerStyle;

private:
    PowerButton power;
    APVTS::ButtonAttachment attachment;
};

} // namespace stagemic
