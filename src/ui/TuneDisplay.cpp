// SPDX-License-Identifier: AGPL-3.0-or-later
// Tune's pitch graph
#include "ui/Displays.h"

#include "ui/GraphHelpers.h"
#include "ui/Theme.h"

namespace stagemic
{

using namespace graph;

//==============================================================================
TuneDisplay::TuneDisplay(APVTS& s, const dsp::Tune& t)
    : state(s)
    , tune(t)
    , styles{{"Hard", 0.0f, "Hard tune: snaps to each note instantly. The robotic stepping effect."},
             {"Pop", 12.0f, "Pop: tight and polished, a hint of the effect on fast notes (12 ms)."},
             {"Melodic", 35.0f, "Melodic: smooth, catchy correction for sung rap and R&B (35 ms)."},
             {"Natural", 120.0f, "Natural: gentle correction that keeps slides and vibrato (120 ms)."}}
{
    input.fill(-1.0f);
    output.fill(-1.0f);
    for (const auto& style : styles)
    {
        auto* b = styleButtons.add(new juce::TextButton(style.name));
        b->setTooltip(style.tip);
        const float speed = style.speedMs;
        b->onClick = [this, speed] {
            for (const auto& [id, value] : {std::pair<const char*, float>{pid::tuneSpeed, speed}, {pid::tuneAmount, 100.0f}})
                if (auto* p = state.getParameter(id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(value));
                    p->endChangeGesture();
                }
            repaint();
        };
        addAndMakeVisible(b);
    }
    theme::setTint(*this, theme::family(ModuleId::Tune));
    setTooltip("Grey = what you sing, blue = what the stream hears. Lighter rows are the notes of the chosen key.");
}

juce::String TuneDisplay::noteName(int midi)
{
    static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

void TuneDisplay::tick()
{
    const auto& h = tune.getHistory();
    const int written = h.written.load(std::memory_order_acquire);
    if (written == lastWritten)
        return; // Tune is off or silent
    if (light && (++lightSkip & 1) != 0)
        return; // Simple mode
    lastWritten = written;
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < kShown; ++i)
    {
        const int k = written - kShown + i;
        input[(size_t)i] = k < 0 ? -1.0f : h.input[(size_t)(k % dsp::Tune::kHistory)].load(std::memory_order_relaxed);
        output[(size_t)i] = k < 0 ? -1.0f : h.output[(size_t)(k % dsp::Tune::kHistory)].load(std::memory_order_relaxed);
        if (output[(size_t)i] > 0.0f && i > kShown * 3 / 4)
        {
            sum += output[(size_t)i];
            ++count;
        }
    }
    // Follow the singer slowly so the graph doesn't jump around
    if (count > 0)
        centre += (sum / (float)count - centre) * 0.15f;

    // Light up the style that matches the current speed
    const float speed = state.getRawParameterValue(pid::tuneSpeed)->load();
    for (size_t i = 0; i < styles.size(); ++i)
        styleButtons[(int)i]->setToggleState(std::abs(speed - styles[i].speedMs) < 0.5f, juce::dontSendNotification);
    // Redraw only what changes
    repaint(plot.getSmallestIntegerContainer().expanded(2));
    repaint(getLocalBounds().withHeight(36).withRight(styleButtons.isEmpty() ? getWidth() : styleButtons.getFirst()->getX()));
}

void TuneDisplay::resized()
{
    auto top = getLocalBounds().reduced(8, 6).removeFromTop(24);
    for (int i = styleButtons.size() - 1; i >= 0; --i)
    {
        styleButtons[i]->setBounds(top.removeFromRight(74));
        top.removeFromRight(4);
    }
    plot = getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(30.0f).withTrimmedLeft(36.0f);
}

void TuneDisplay::paint(juce::Graphics& g)
{
    const bool on = state.getRawParameterValue(pid::tuneOn)->load() >= 0.5f;
    const int key = (int)state.getRawParameterValue(pid::tuneKey)->load();
    const int scale = juce::jlimit(0, (int)dsp::Tune::scaleMasks.size() - 1, (int)state.getRawParameterValue(pid::tuneScale)->load());
    const auto mask = dsp::Tune::scaleMasks[(size_t)scale];

    constexpr float span = 7.0f; // semitones above and below the centre
    auto yFor = [&](float midi) { return plot.getCentreY() - (midi - centre) / span * plot.getHeight() * 0.5f; };

    // Note rows
    const int lowNote = (int)std::floor(centre - span), highNote = (int)std::ceil(centre + span);
    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = yFor((float)n);
        if (y < plot.getY() - 1.0f || y > plot.getBottom() + 1.0f)
            continue;
        const int pc = ((n - key) % 12 + 12) % 12;
        const bool inScale = (mask & (1 << pc)) != 0;
        const float rowH = plot.getHeight() / (2.0f * span);
        if (inScale)
        {
            g.setColour(theme::panelRaised.withAlpha(pc == 0 ? 0.9f : 0.55f));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - rowH * 0.5f, plot.getWidth(), rowH).getIntersection(plot));
        }
        g.setColour(pc == 0 ? theme::family(ModuleId::Tune).withAlpha(0.9f)
                            : (inScale ? theme::dimText : theme::faintText.withAlpha(0.6f)));
        g.setFont(theme::font(10.0f, pc == 0));
        g.drawText(noteName(n), juce::Rectangle<float>(plot.getX() - 36.0f, y - 6.0f, 32.0f, 12.0f), juce::Justification::centredRight);
    }
    g.setColour(theme::outline);
    g.drawRect(plot, 1.0f);

    // Pitch lines
    auto drawLine = [&](const std::array<float, kShown>& v, juce::Colour colour, float thickness) {
        juce::Path p;
        bool drawing = false;
        const int stride = light ? 4 : 2; // every 2nd point is plenty for the eye
        for (int i = 0; i < kShown; i += stride)
        {
            const float x = plot.getX() + plot.getWidth() * (float)i / (float)(kShown - 1);
            if (v[(size_t)i] <= 0.0f)
            {
                drawing = false;
                continue;
            }
            const float y = juce::jlimit(plot.getY(), plot.getBottom(), yFor(v[(size_t)i]));
            if (!drawing)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
            drawing = true;
        }
        g.setColour(colour);
        g.strokePath(p, light ? juce::PathStrokeType(thickness, juce::PathStrokeType::beveled, juce::PathStrokeType::butt)
                              : juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());
    drawLine(input, theme::dimText.withAlpha(0.8f), 1.5f);
    if (on)
        drawLine(output, theme::family(ModuleId::Tune), 2.4f);
    g.restoreState();

    // Current note, top left
    auto header = getLocalBounds().reduced(10, 6).removeFromTop(24).toFloat();
    const float last = input[kShown - 1], lastOut = output[kShown - 1];
    g.setFont(theme::font(13.0f, true));
    if (!on)
    {
        g.setColour(theme::faintText);
        g.drawText("Tune is off", header, juce::Justification::centredLeft);
        // Say what it does and how to start, in the middle of the empty graph
        g.setColour(theme::dimText);
        g.setFont(theme::font(14.0f, true));
        g.drawFittedText(
            "Tune corrects your pitch while you sing.\nSwitch it on with the square next to Tune,\nthen pick your song's key and a style.",
            plot.reduced(16.0f).toNearestInt(), juce::Justification::centred, 3);
    }
    else if (last > 0.0f)
    {
        const int note = (int)std::lround(lastOut);
        const int cents = (int)std::lround((last - (float)note) * 100.0f);
        g.setColour(theme::text);
        g.drawText(noteName(note), header.removeFromLeft(44.0f), juce::Justification::centredLeft);
        g.setColour(theme::dimText);
        g.setFont(theme::font(12.0f));
        g.drawText("you sang " + juce::String(cents > 0 ? "+" : "") + juce::String(cents) + " cents", header,
                   juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(theme::faintText);
        g.drawText("Sing to see your pitch", header, juce::Justification::centredLeft);
    }
    g.setColour(theme::faintText);
    g.setFont(theme::font(10.0f));
    g.drawText("adds 20 ms delay while on", plot.reduced(6.0f, 4.0f), juce::Justification::bottomRight);
}

} // namespace stagemic
