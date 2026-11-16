// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

class VoiceSetup final : public juce::Component
{
public:
    explicit VoiceSetup(StageMicProcessor&);

    void open();
    // The first-time welcome
    void openWelcome();
    void startMeasuring();
    void close();
    // Called by the window's timer while visible
    void tick(float inputPeak);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override {} // block clicks to the window underneath

    // Text summary of a result
    static juce::StringArray describe(const VoiceAnalyzer::Result&, float currentInputGainDb);

private:
    enum class Step
    {
        Intro,
        Measuring,
        Result
    };
    void setStep(Step);
    juce::Rectangle<int> card() const;

    StageMicProcessor& processor;
    Step step = Step::Intro;
    bool welcome = false;
    VoiceAnalyzer::Result result;
    juce::StringArray lines;
    float level = 0.0f; // live mic level for the little meter, 0..1
    juce::TextButton primary, secondary, tertiary;

    struct Use
    {
        const char* label;
        const char* preset;
    };
    static constexpr Use uses[3] = {{"Sing", "Clean Vocal"}, {"Talk & game", "Just Talking"}, {"ASMR (whisper)", "ASMR"}};
    juce::TextButton useButtons[3];
    int chosenUse = -1;
    void showUseChoice(bool);
};

} // namespace stagemic
