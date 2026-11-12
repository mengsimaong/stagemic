// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/SimpleView.h"

#include "ui/Theme.h"

namespace stagemic
{

//==============================================================================
SimpleTile::SimpleTile(APVTS& s, const char* enableParam, juce::String t, std::vector<Knob> k, juce::String h)
    : title(std::move(t))
    , hint(std::move(h))
    , attachment(s, enableParam, power)
{
    power.setTooltip("Switch " + title + " on or off");
    power.onStateChange = [this] { repaint(); };
    addAndMakeVisible(power);
    for (const auto& knob : k)
    {
        knobs.push_back(std::make_unique<ValueKnob>(s, knob.paramId, knob.label));
        addAndMakeVisible(*knobs.back());
    }
}

void SimpleTile::paint(juce::Graphics& g)
{
    const bool on = power.getToggleState();
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(on ? theme::panelRaised.withAlpha(0.55f) : theme::panelOff);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(r, 8.0f, 1.0f);

    g.setColour(on ? theme::text : theme::dimText);
    g.setFont(theme::font(13.0f, true));
    g.drawText(title.toUpperCase(), getLocalBounds().removeFromTop(36).withTrimmedLeft(38), juce::Justification::centredLeft);

    g.setColour(theme::faintText);
    g.setFont(theme::font(11.0f));
    g.drawFittedText(hint, getLocalBounds().removeFromBottom(30).reduced(10, 2), juce::Justification::centred, 2, 0.9f);
}

void SimpleTile::resized()
{
    power.setBounds(10, 8, 22, 20);
    auto area = getLocalBounds().withTrimmedTop(36).withTrimmedBottom(30).reduced(6, 0);
    if (knobs.empty())
        return;
    // Same knob size everywhere, however big the tile is.
    const int w = area.getWidth() / (int)knobs.size();
    for (auto& k : knobs)
        k->setBounds(area.removeFromLeft(w).withSizeKeepingCentre(juce::jmin(w - 4, 120), juce::jmin(area.getHeight(), 132)));
}

//==============================================================================
SimpleView::SimpleView(APVTS& s, const dsp::Tune& tune)
    : state(s)
    , cleanUp(s, pid::gateOn, "Clean-up", {{pid::gateThreshold, "Noise gate"}}, "Silences room noise between phrases")
    , tone(s, pid::eqOn, "Tone", {{pid::eqLowGain, "Body"}, {pid::eqHighGain, "Brightness"}}, "Fuller or thinner, darker or brighter")
    , compress(s, pid::compOn, "Compress", {{pid::compThreshold, "Threshold"}}, "Lower = more even, louder quiet parts")
    , deEss(s, pid::dsOn, "De-Ess", {{pid::dsRange, "Max cut"}}, "Softens sharp \"s\" sounds")
    , warmth(s, pid::satOn, "Warmth", {{pid::satDrive, "Drive"}}, "Analog-style richness")
    , reverb(s, pid::revOn, "Reverb", {{pid::revMix, "Amount"}, {pid::revSize, "Size"}}, "Room or hall around the voice")
    , echo(s, pid::dlyOn, "Echo", {{pid::dlyMix, "Amount"}, {pid::dlyTime, "Time"}}, "Repeats of the voice")
    , chorus(s, pid::chOn, "Chorus", {{pid::chMix, "Amount"}}, "Wider, shimmering voice")
    , tuneAttachment(s, pid::tuneOn, tunePower)
    , tuneDisplay(s, tune)
    , key(s, pid::tuneKey, "Key")
    , scale(s, pid::tuneScale, "Scale")
    , voice(s, pid::tuneVoice, "Voice")
{
    for (auto* c : std::initializer_list<juce::Component*>{&cleanUp, &tone, &compress, &deEss, &warmth, &reverb, &echo, &chorus,
                                                           &tunePower, &tuneDisplay, &key, &scale, &voice})
        addAndMakeVisible(c);
    tunePower.setTooltip("Switch Tune (pitch correction) on or off");
    tunePower.onStateChange = [this] { repaint(); };
}

void SimpleView::tick()
{
    tuneDisplay.tick();
}

void SimpleView::paint(juce::Graphics& g)
{
    auto card = [&](juce::Rectangle<int> r, const juce::String& name) {
        g.setColour(theme::panel);
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        g.setColour(theme::outline);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 8.0f, 1.0f);
        g.setColour(theme::dimText);
        g.setFont(theme::font(12.0f, true));
        g.drawText(name, r.removeFromTop(28).withTrimmedLeft(name == "TUNE" ? 44 : 14), juce::Justification::centredLeft);
    };
    card(voiceCard, "VOICE");
    card(effectsCard, "EFFECTS");
    card(tuneCard, "TUNE");
}

void SimpleView::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 8;

    voiceCard = area.removeFromTop(218);
    area.removeFromTop(gap);
    auto lower = area;
    effectsCard = lower.removeFromLeft(lower.getWidth() * 42 / 100);
    lower.removeFromLeft(gap);
    tuneCard = lower;

    // Voice: five tiles in a row (Tone is wider: two knobs).
    {
        auto row = voiceCard.withTrimmedTop(28).reduced(10, 0).withTrimmedBottom(10);
        const int unit = (row.getWidth() - 4 * gap) / 6;
        for (auto [tile, units] : {std::pair<SimpleTile*, int>{&cleanUp, 1}, {&tone, 2}, {&compress, 1}, {&deEss, 1}, {&warmth, 1}})
        {
            tile->setBounds(row.removeFromLeft(unit * units + (units - 1) * gap));
            row.removeFromLeft(gap);
        }
    }
    // Effects: Reverb and Echo (two knobs each) above Chorus.
    {
        auto col = effectsCard.withTrimmedTop(28).reduced(10, 0).withTrimmedBottom(10);
        const int h = (col.getHeight() - gap) / 2;
        auto top = col.removeFromTop(h);
        col.removeFromTop(gap);
        reverb.setBounds(top.removeFromLeft((top.getWidth() - gap) / 2));
        top.removeFromLeft(gap);
        echo.setBounds(top);
        chorus.setBounds(col);
    }
    // Tune: on/off, the pitch graph with the style buttons, then Key / Scale / Voice.
    {
        tunePower.setBounds(tuneCard.getX() + 14, tuneCard.getY() + 5, 22, 20);
        auto inner = tuneCard.withTrimmedTop(28).reduced(8, 0).withTrimmedBottom(8);
        auto controls = inner.removeFromBottom(64);
        tuneDisplay.setBounds(inner);
        const int w = controls.getWidth() / 3;
        for (auto* c : {static_cast<juce::Component*>(&key), static_cast<juce::Component*>(&scale), static_cast<juce::Component*>(&voice)})
            c->setBounds(controls.removeFromLeft(w).reduced(6, 4));
    }
}

} // namespace stagemic
