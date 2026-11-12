// SPDX-License-Identifier: AGPL-3.0-or-later
// Small reusable controls for the StageMic window.
#pragma once

#include "core/Parameters.h"
#include "core/dsp/VocalChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

using APVTS = juce::AudioProcessorValueTreeState;

/** Square LED-style on/off switch. */
class PowerButton final : public juce::Button
{
public:
    PowerButton();
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

/** Small rotary knob with its name above and value below. */
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

/** Knob with an editable value on top and its name underneath:
        2.50 kHz     <- click and type (e.g. "2.5k", "-3", "+4.5 dB")
          (knob)     <- drag up/down; double-click resets
          Freq
*/
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

/** Multiple-choice setting: name below, a button showing the current choice.
    Each click moves to the next choice (e.g. Tube -> Tape -> Console -> Tube). */
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

/** Vertical fader with its name above and value below. */
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

/** Vertical level meter (dBFS) with peak hold. */
class MeterBar final : public juce::Component
{
public:
    explicit MeterBar(juce::String label = {});
    void setLevel(float linearPeak);
    void paint(juce::Graphics&) override;

private:
    juce::String label;
    float level = 0.0f, hold = 0.0f;
    int holdTicks = 0;
};

/** Gain reduction bar that grows downward (0 to -maxDb). */
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

/** A module panel: header with power switch and title, dimmed when off. */
class Section : public juce::Component
{
public:
    enum class Header
    {
        top,  // title bar across the top (strips)
        left  // title on the left (compact rows)
    };

    Section(APVTS& state, const char* enableParam, juce::String title, Header header = Header::top);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Called ~30 times a second with live meter values. */
    virtual void updateActivity(const dsp::VocalChain::Activity&) {}

protected:
    /** Lay out controls inside the area below the header. */
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
