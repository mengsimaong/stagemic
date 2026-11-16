// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Widgets.h"

namespace stagemic
{

class MasterPanel final : public juce::Component
{
public:
    explicit MasterPanel(APVTS& state);

    void setLevels(float inPeak, float outPeak);
    void setLevelerGain(float db);
    void setStatus(const juce::String& text);
    std::function<void(bool)> onHearBefore;
    // The plain-words level advice currently shown
    const juce::String& getAdvice() const noexcept { return advice; }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MeterBar inMeter{"IN"}, outMeter{"OUT"};
    PowerButton levelPower;
    APVTS::ButtonAttachment levelPowerAttachment;
    SmallKnob levelTarget, levelRange, levelSpeed;
    float levelerDb = 0.0f;
    juce::Rectangle<int> levelArea;
    SmallKnob inGain, outGain;
    juce::String status;
    float inPeakDb = -100.0f, outPeakDb = -100.0f;
    int peakHoldTicks = 0;
    juce::Rectangle<int> meterArea;

    // Before / After, and plain-words level advice under the meters
    juce::TextButton beforeButton{"Hold: hear before"};
    bool beforeDown = false;
    static constexpr int kAdviceTicks = 80; // 4 s at 20 updates a second
    std::array<float, kAdviceTicks> recentIn{}, recentOut{};
    int adviceTick = 0, alarmHold = 0;
    juce::String advice;
    juce::Colour adviceColour;
    juce::Rectangle<int> adviceArea;
    void updateAdvice(float inPeak, float outPeak);
};

} // namespace stagemic
