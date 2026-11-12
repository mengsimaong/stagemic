// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

/** "Set up my voice": a short guided measurement shown over the window.

      1. Intro: what will happen (about 11 seconds).
      2. Quiet: 3 s of room noise.
      3. Voice: 8 s of singing/talking like on stream, with some "s" sounds.
      4. Result: what it found and what it will set. Apply / Try again / Cancel. */
class VoiceSetup final : public juce::Component
{
public:
    explicit VoiceSetup(StageMicProcessor&);

    void open();
    void startMeasuring();
    void close();
    /** Called by the window's timer while visible. */
    void tick(float inputPeak);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override {} // block clicks to the window underneath

    /** Text summary of a result (also used by the tests). */
    static juce::StringArray describe(const VoiceAnalyzer::Result&, float currentInputGainDb);

private:
    enum class Step { Intro, Measuring, Result };
    void setStep(Step);
    juce::Rectangle<int> card() const;

    StageMicProcessor& processor;
    Step step = Step::Intro;
    VoiceAnalyzer::Result result;
    juce::StringArray lines;
    float level = 0.0f; // live mic level for the little meter, 0..1
    juce::TextButton primary, secondary, tertiary;
};

} // namespace stagemic
