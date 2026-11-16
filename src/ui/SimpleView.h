// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"
#include "ui/Displays.h"
#include "ui/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

// One module in Simple mode
class SimpleTile final : public juce::Component
{
public:
    struct Knob
    {
        const char* paramId;
        const char* label;
    };

    SimpleTile(APVTS&, const char* enableParam, juce::String title, std::vector<Knob> knobs, juce::String hint);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, hint;
    PowerButton power;
    APVTS::ButtonAttachment attachment;
    std::vector<std::unique_ptr<ValueKnob>> knobs;
};

class SimpleView final : public juce::Component
{
public:
    SimpleView(APVTS&, const dsp::Tune&);

    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    APVTS& state;
    SimpleTile cleanUp, tone, compress, deEss, warmth;
    SimpleTile reverb, echo, chorus;
    PowerButton tunePower;
    APVTS::ButtonAttachment tuneAttachment;
    TuneDisplay tuneDisplay;
    ChoiceToggle key, scale, voice;
    juce::Rectangle<int> voiceCard, effectsCard, tuneCard;
};

} // namespace stagemic
