// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/MasterPanel.h"

#include "ui/Theme.h"

namespace stagemic
{

MasterPanel::MasterPanel(APVTS& s)
    : inGain(s, pid::inGain, "Input")
    , outGain(s, pid::outGain, "Output")
{
    for (auto* c : std::initializer_list<juce::Component*>{&inMeter, &outMeter, &inGain, &outGain})
        addAndMakeVisible(c);
}

void MasterPanel::setLevels(float inPeak, float outPeak)
{
    inMeter.setLevel(inPeak);
    outMeter.setLevel(outPeak);

    // Numeric peak read-outs hold the highest value for ~2 s.
    const float inDb = juce::Decibels::gainToDecibels(inPeak, -100.0f);
    const float outDb = juce::Decibels::gainToDecibels(outPeak, -100.0f);
    if (--peakHoldTicks <= 0 || inDb > inPeakDb || outDb > outPeakDb)
    {
        if (peakHoldTicks <= 0)
        {
            inPeakDb = inDb;
            outPeakDb = outDb;
            peakHoldTicks = 60;
        }
        inPeakDb = juce::jmax(inPeakDb, inDb);
        outPeakDb = juce::jmax(outPeakDb, outDb);
        repaint(meterArea.getX() - 20, meterArea.getY() - 22, meterArea.getWidth() + 40, 22);
    }
}

void MasterPanel::setStatus(const juce::String& text)
{
    if (text != status)
    {
        status = text;
        repaint();
    }
}

void MasterPanel::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);

    g.setColour(theme::text);
    g.setFont(theme::font(12.0f, true));
    g.drawText("MASTER", getLocalBounds().removeFromTop(30).reduced(12, 0), juce::Justification::centredLeft);

    // Peak numbers above the meters
    auto drawPeak = [&](const juce::Component& meter, float db) {
        g.setColour(db >= 0.0f ? theme::meterRed : theme::text);
        g.setFont(theme::font(12.0f, true));
        g.drawText(db <= -99.0f ? "-inf" : juce::String(db, 1),
                   juce::Rectangle<int>(meter.getX() - 16, meter.getY() - 20, meter.getWidth() + 32, 18), juce::Justification::centred);
    };
    drawPeak(inMeter, inPeakDb);
    drawPeak(outMeter, outPeakDb);

    // dB scale between the meters
    const float top = (float)inMeter.getY() + 2.0f;
    const float bottom = (float)inMeter.getBottom() - 18.0f;
    const float cx = (float)(inMeter.getRight() + outMeter.getX()) * 0.5f;
    g.setFont(theme::font(10.0f));
    for (int db : {0, -6, -12, -18, -24, -36, -48, -60})
    {
        const float y = top + (bottom - top) * (float)db / -60.0f;
        g.setColour(theme::faintText);
        g.drawText(juce::String(db), juce::Rectangle<float>(cx - 14.0f, y - 6.0f, 28.0f, 12.0f), juce::Justification::centred);
    }

    g.setColour(theme::dimText);
    g.setFont(theme::font(11.0f));
    g.drawFittedText(status, getLocalBounds().removeFromBottom(40).reduced(10, 4), juce::Justification::centred, 2);
}

void MasterPanel::resized()
{
    auto area = getLocalBounds().reduced(12, 0);
    area.removeFromTop(58);
    area.removeFromBottom(40);
    auto knobs = area.removeFromBottom(84);
    area.removeFromBottom(10);

    meterArea = area;
    const int meterW = 30;
    const int gap = juce::jmax(40, area.getWidth() - 2 * meterW - 40);
    auto meters = area.withSizeKeepingCentre(2 * meterW + gap, area.getHeight());
    inMeter.setBounds(meters.removeFromLeft(meterW));
    outMeter.setBounds(meters.removeFromRight(meterW));

    inGain.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2).withSizeKeepingCentre(64, 78));
    outGain.setBounds(knobs.withSizeKeepingCentre(64, 78));
}

} // namespace stagemic
