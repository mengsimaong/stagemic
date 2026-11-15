// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/MasterPanel.h"

#include "ui/Theme.h"

namespace stagemic
{

MasterPanel::MasterPanel(APVTS& s)
    : levelPowerAttachment(s, pid::levOn, levelPower)
    , levelTarget(s, pid::levTarget, "Target")
    , levelRange(s, pid::levRange, "Range")
    , levelSpeed(s, pid::levSpeed, "Speed")
    , inGain(s, pid::inGain, "Input")
    , outGain(s, pid::outGain, "Output")
{
    for (auto* c : std::initializer_list<juce::Component*>{&levelPower, &levelTarget, &levelRange, &levelSpeed})
        theme::setTint(*c, theme::family(ModuleId::Leveler));
    levelPower.setTooltip("Auto Level: evens out quiet talking and loud singing");
    levelTarget.setTooltip("Target: the level Auto Level steers your voice toward");
    levelRange.setTooltip("Range: the most Auto Level may raise or lower your voice");
    levelSpeed.setTooltip("Speed: how quickly Auto Level reacts (it reacts faster to getting louder)");
    levelPower.onStateChange = [this] {
        for (auto* k : std::initializer_list<juce::Component*>{&levelTarget, &levelRange, &levelSpeed})
            k->setAlpha(levelPower.getToggleState() ? 1.0f : 0.4f);
        repaint(levelArea);
    };
    for (auto* c : std::initializer_list<juce::Component*>{&inMeter, &outMeter, &levelPower, &levelTarget, &levelRange,
                                                           &levelSpeed, &inGain, &outGain, &beforeButton})
        addAndMakeVisible(c);
    beforeButton.setTooltip("Hold to hear your mic without StageMic, let go to hear it with. (Your stream hears it too.)");
    beforeButton.onStateChange = [this] {
        const bool down = beforeButton.isDown();
        if (down != beforeDown)
        {
            beforeDown = down;
            beforeButton.setButtonText(down ? "Hearing: before" : "Hold: hear before");
            if (onHearBefore)
                onHearBefore(down);
        }
    };
}

void MasterPanel::setLevelerGain(float db)
{
    if (juce::roundToInt(db) != juce::roundToInt(levelerDb)) // the read-out shows whole dB
    {
        levelerDb = db;
        repaint(levelArea.withHeight(24));
    }
}

void MasterPanel::updateAdvice(float inPeak, float outPeak)
{
    // Plain-words advice from the last 4 seconds. Alarms (clipping, too loud) stay up
    // for 3 seconds so they can be read.
    recentIn[(size_t)adviceTick] = inPeak;
    recentOut[(size_t)adviceTick] = outPeak;
    adviceTick = (adviceTick + 1) % kAdviceTicks;
    float inMax = 0.0f, outMax = 0.0f;
    for (int i = 0; i < kAdviceTicks; ++i)
    {
        inMax = juce::jmax(inMax, recentIn[(size_t)i]);
        outMax = juce::jmax(outMax, recentOut[(size_t)i]);
    }

    juce::String text;
    juce::Colour colour;
    if (inPeak >= 0.989f) // -0.1 dBFS: the mic itself is overloading, before StageMic
    {
        text = "Mic is clipping: turn its gain down";
        alarmHold = 60;
    }
    else if (outPeak >= 0.97f)
    {
        text = "Too loud: turn Output down";
        alarmHold = 60;
    }
    if (text.isNotEmpty() || alarmHold > 0)
    {
        if (text.isEmpty())
        {
            --alarmHold;
            text = advice; // keep showing the alarm
        }
        colour = theme::meterRed;
    }
    else if (inMax < 1.0e-4f)
    {
        text = "No sound from the mic";
        colour = theme::dimText;
    }
    else if (outMax < 0.063f) // under -24 dBFS at its loudest
    {
        text = "Too quiet: try Set up my voice";
        colour = theme::reduction;
    }
    else
    {
        text = "Level is good";
        colour = theme::meterGreen.withAlpha(0.85f);
    }

    if (text != advice || colour != adviceColour)
    {
        advice = text;
        adviceColour = colour;
        repaint(adviceArea);
    }
}

void MasterPanel::setLevels(float inPeak, float outPeak)
{
    inMeter.setLevel(inPeak);
    outMeter.setLevel(outPeak);
    updateAdvice(inPeak, outPeak);

    // Numeric peak read-outs hold the highest value for ~2 s.
    const float inDb = juce::Decibels::gainToDecibels(inPeak, -100.0f);
    const float outDb = juce::Decibels::gainToDecibels(outPeak, -100.0f);
    if (--peakHoldTicks <= 0 || inDb > inPeakDb || outDb > outPeakDb)
    {
        if (peakHoldTicks <= 0)
        {
            inPeakDb = inDb;
            outPeakDb = outDb;
            peakHoldTicks = 40; // 2 s at 20 updates a second
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

    // Auto Level section: frame, title and what it's doing right now.
    const auto la = levelArea.toFloat();
    g.setColour(theme::panelRaised.withAlpha(0.6f));
    g.fillRoundedRectangle(la, 5.0f);
    const bool levelOn = levelPower.getToggleState();
    g.setColour(levelOn ? theme::text : theme::dimText);
    g.setFont(theme::font(11.5f, true));
    g.drawText("AUTO LEVEL", la.withHeight(24.0f).withTrimmedLeft(30.0f), juce::Justification::centredLeft);
    if (levelOn)
    {
        g.setColour(std::abs(levelerDb) < 0.05f ? theme::dimText : theme::reduction);
        g.setFont(theme::font(10.5f, false));
        // In words: which way it's turning the voice right now.
        const int amount = juce::roundToInt(std::abs(levelerDb));
        g.drawText(amount == 0 ? juce::String("steady") : (levelerDb > 0.0f ? "up " : "down ") + juce::String(amount) + " dB",
                   la.withHeight(24.0f).withTrimmedRight(8.0f), juce::Justification::centredRight);
    }

    g.setColour(adviceColour);
    g.setFont(theme::font(11.5f, true));
    g.drawFittedText(advice, adviceArea, juce::Justification::centred, 2, 0.85f);

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
    area.removeFromBottom(8);

    levelArea = area.removeFromBottom(108);
    levelPower.setBounds(levelArea.getX() + 4, levelArea.getY() + 2, 22, 20);
    auto levelKnobs = levelArea.withTrimmedTop(26).reduced(2, 2);
    const int lw = levelKnobs.getWidth() / 3;
    levelTarget.setBounds(levelKnobs.removeFromLeft(lw).withSizeKeepingCentre(juce::jmin(lw, 60), 76));
    levelRange.setBounds(levelKnobs.removeFromLeft(lw).withSizeKeepingCentre(juce::jmin(lw, 60), 76));
    levelSpeed.setBounds(levelKnobs.withSizeKeepingCentre(juce::jmin(lw, 60), 76));
    area.removeFromBottom(8);
    beforeButton.setBounds(area.removeFromBottom(28));
    area.removeFromBottom(4);
    adviceArea = area.removeFromBottom(30);
    area.removeFromBottom(2);

    meterArea = area;
    const int meterW = 30;
    const int gap = juce::jmax(40, area.getWidth() - 2 * meterW - 40);
    auto meters = area.withSizeKeepingCentre(2 * meterW + gap, area.getHeight());
    inMeter.setBounds(meters.removeFromLeft(meterW));
    outMeter.setBounds(meters.removeFromRight(meterW));

    inGain.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2).withSizeKeepingCentre(64, 78));
    outGain.setBounds(knobs.withSizeKeepingCentre(64, 78));
    levelPower.onStateChange();
}

} // namespace stagemic
