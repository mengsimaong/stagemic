// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/VoiceSetup.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace
{
juce::String db(float v, int decimals = 1)
{
    return (v > 0.0f ? "+" : "") + juce::String(v, decimals) + " dB";
}
} // namespace

VoiceSetup::VoiceSetup(StageMicProcessor& p)
    : processor(p)
{
    for (auto* b : {&primary, &secondary, &tertiary})
        addAndMakeVisible(b);
    primary.getProperties().set("primary", true);
    primary.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setStep(Step::Intro);
}

void VoiceSetup::open()
{
    setStep(Step::Intro);
    setVisible(true);
    toFront(true);
}

void VoiceSetup::startMeasuring()
{
    processor.getVoiceAnalyzer().start();
    setStep(Step::Measuring);
}

void VoiceSetup::openWelcome()
{
    open();
    welcome = true;
    setStep(Step::Intro);
}

void VoiceSetup::close()
{
    welcome = false;
    processor.getVoiceAnalyzer().cancel();
    setVisible(false);
}

void VoiceSetup::setStep(Step s)
{
    step = s;
    tertiary.setVisible(false);
    primary.setVisible(true);
    secondary.setVisible(true);

    switch (step)
    {
        case Step::Intro:
            primary.setButtonText("Start");
            primary.onClick = [this] { startMeasuring(); };
            secondary.setButtonText(welcome ? "Skip for now" : "Cancel");
            secondary.onClick = [this] { close(); };
            break;

        case Step::Measuring:
            primary.setVisible(false);
            secondary.setButtonText("Stop");
            secondary.onClick = [this] { close(); };
            break;

        case Step::Result:
            if (result.heardVoice)
            {
                primary.setButtonText("Apply");
                primary.onClick = [this] {
                    processor.applyVoiceSetup(result);
                    close();
                };
            }
            else
                primary.setVisible(false);
            secondary.setButtonText("Try again");
            secondary.onClick = [this] { startMeasuring(); };
            tertiary.setVisible(true);
            tertiary.setButtonText("Cancel");
            tertiary.onClick = [this] { close(); };
            break;
    }
    resized();
    repaint();
}

void VoiceSetup::tick(float inputPeak)
{
    level = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(inputPeak, -60.0f) + 60.0f) / 60.0f);
    if (step == Step::Measuring && processor.getVoiceAnalyzer().getPhase() == VoiceAnalyzer::Phase::Done)
    {
        result = VoiceAnalyzer::evaluate(processor.getVoiceAnalyzer().getMeasurements());
        const float currentGain = processor.getParameters().getRawParameterValue(pid::inGain)->load();
        lines = describe(result, currentGain);
        setStep(Step::Result);
    }
    repaint();
}

juce::StringArray VoiceSetup::describe(const VoiceAnalyzer::Result& r, float currentInputGainDb)
{
    juce::StringArray out;
    if (!r.heardVoice)
    {
        out.add("I couldn't hear a voice.");
        out.add("Check that OBS is using the right microphone, that it isn't muted,");
        out.add("and sing or talk during the second part. Then try again.");
        return out;
    }
    out.add("Input Gain:  " + db(currentInputGainDb) + "  ->  " + db(r.inputGainDb));
    out.add("Gate opens at:  " + db(r.gateThresholdDb, 0) + (r.gateRatio > 2.0f ? "  (firmer, to hide the room noise)" : ""));
    out.add(r.sibilantHz > 0.0f ? "Your \"s\" sounds sit around " + juce::String(r.sibilantHz / 1000.0f, 1) + " kHz (De-Ess set there)"
                                : "Not enough \"s\" sounds to tune the De-Ess (it stays as it is)");
    out.add("Room noise:  " + db(r.noiseDb, 0) + "   Voice:  " + db(r.voiceDb, 0)
            + (r.noisy ? "   (noisy room)" : "   (good)"));
    if (r.clipped)
        out.add("! Your mic clipped. Turn the gain knob on your audio interface down a bit and try again.");
    if (r.veryQuiet)
        out.add("! Your mic is very quiet. Turn the gain knob on your audio interface up and try again.");
    if (r.noisy && !r.clipped)
        out.add("! Lots of background noise (fan, PC, aircon?). The gate will help, but less noise is better.");
    out.add("These mic settings now stay the same when you switch presets.");
    return out;
}

juce::Rectangle<int> VoiceSetup::card() const
{
    return getLocalBounds().withSizeKeepingCentre(560, 330);
}

void VoiceSetup::resized()
{
    auto buttons = card().reduced(24).removeFromBottom(34);
    primary.setBounds(buttons.removeFromRight(120));
    buttons.removeFromRight(8);
    secondary.setBounds(buttons.removeFromRight(110));
    buttons.removeFromRight(8);
    if (tertiary.isVisible())
        tertiary.setBounds(buttons.removeFromRight(100));
}

void VoiceSetup::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    const auto c = card();
    g.setColour(theme::panel);
    g.fillRoundedRectangle(c.toFloat(), 12.0f);
    g.setColour(theme::accent.withAlpha(0.6f));
    g.drawRoundedRectangle(c.toFloat().reduced(0.5f), 12.0f, 1.0f);

    auto area = c.reduced(24);
    g.setColour(theme::text);
    g.setFont(theme::font(20.0f, true));
    g.drawText(welcome && step == Step::Intro ? "Welcome! Let's set up your mic" : "Set up my voice", area.removeFromTop(30),
               juce::Justification::centredLeft);
    area.removeFromTop(8);
    area.removeFromBottom(46);

    auto bodyText = [&](const juce::String& text, float size = 14.0f, juce::Colour colour = theme::text) {
        g.setColour(colour);
        g.setFont(theme::font(size));
        g.drawFittedText(text, area.removeFromTop((int)(size * 1.6f)), juce::Justification::centredLeft, 2, 0.9f);
    };

    if (step == Step::Intro)
    {
        bodyText("Takes about 11 seconds. Use the mic and position you stream with.", 14.0f, theme::dimText);
        area.removeFromTop(6);
        bodyText("1.  Stay quiet for 3 seconds (it listens to the room).");
        bodyText("2.  Then sing or talk like on stream for 8 seconds,");
        {
            const auto full = area;
            area.removeFromLeft(22);
            bodyText("with some \"s\" sounds: \"she sells sea shells\".");
            area = full.withTop(area.getY());
        }
        area.removeFromTop(8);
        bodyText("It sets Input Gain, the Gate and the De-Ess frequency for your mic.", 13.0f, theme::dimText);
        bodyText("Your preset's sound stays the same.", 13.0f, theme::dimText);
        return;
    }

    if (step == Step::Measuring)
    {
        const auto& analyzer = processor.getVoiceAnalyzer();
        const auto phase = analyzer.getPhase();
        const bool quiet = phase != VoiceAnalyzer::Phase::Voice;
        const float progress = analyzer.getProgress();
        const double seconds = quiet ? VoiceAnalyzer::silenceSeconds : VoiceAnalyzer::voiceSeconds;
        const int left = juce::jmax(1, (int)std::ceil((1.0f - progress) * (float)seconds));

        area.removeFromTop(10);
        g.setColour(quiet ? theme::dimText : theme::accent);
        g.setFont(theme::font(26.0f, true));
        g.drawText(quiet ? "Shh... stay quiet" : "Now sing or talk!", area.removeFromTop(40), juce::Justification::centred);
        bodyText(quiet ? "Listening to the room noise..." : "Like on stream. Include some \"s\" sounds.", 14.0f, theme::dimText);
        area.removeFromTop(4);
        g.setColour(theme::text);
        g.setFont(theme::font(16.0f, true));
        g.drawText(juce::String(left) + " s", area.removeFromTop(24), juce::Justification::centred);
        area.removeFromTop(10);

        auto bar = area.removeFromTop(10).reduced(40, 0).toFloat();
        g.setColour(theme::background);
        g.fillRoundedRectangle(bar, 4.0f);
        g.setColour(quiet ? theme::dimText : theme::accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * progress), 4.0f);

        area.removeFromTop(12);
        auto meter = area.removeFromTop(6).reduced(40, 0).toFloat();
        g.setColour(theme::background);
        g.fillRoundedRectangle(meter, 3.0f);
        g.setColour(level > 0.95f ? theme::meterRed : theme::meterGreen);
        g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * level), 3.0f);
        g.setColour(theme::faintText);
        g.setFont(theme::font(11.0f));
        g.drawText("mic level", area.removeFromTop(16).reduced(40, 0), juce::Justification::centredLeft);
        return;
    }

    for (const auto& line : lines)
    {
        const bool warning = line.startsWith("!");
        bodyText(warning ? line.substring(2) : line, 13.5f,
                 warning ? theme::reduction : (line.startsWith("These") ? theme::dimText : theme::text));
    }
}

} // namespace stagemic
