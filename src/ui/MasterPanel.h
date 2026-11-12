// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Widgets.h"

namespace stagemic
{

/** Right-hand column: big IN/OUT meters, input/output gain, status. */
class MasterPanel final : public juce::Component
{
public:
    explicit MasterPanel(APVTS& state);

    void setLevels(float inPeak, float outPeak);
    void setStatus(const juce::String& text);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MeterBar inMeter{"IN"}, outMeter{"OUT"};
    SmallKnob inGain, outGain;
    juce::String status;
    float inPeakDb = -100.0f, outPeakDb = -100.0f;
    int peakHoldTicks = 0;
    juce::Rectangle<int> meterArea;
};

} // namespace stagemic
