// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/ChainBar.h"

#include "ui/Theme.h"

namespace stagemic
{

namespace
{
bool isSendModule(ModuleId id)
{
    return id == ModuleId::Chorus || id == ModuleId::Delay || id == ModuleId::Reverb;
}

constexpr float arrowW = 12.0f;

juce::Colour familyColour(ModuleId id)
{
    return theme::family(id);
}

juce::String familyName(ModuleId id)
{
    switch (id)
    {
        case ModuleId::Gate:
        case ModuleId::Equalizer:
        case ModuleId::Leveler: return "clean-up";
        case ModuleId::Compressor:
        case ModuleId::Multiband: return "dynamics";
        case ModuleId::Saturation:
        case ModuleId::Exciter: return "color";
        case ModuleId::DeEsser: return "de-ess";
        case ModuleId::Chorus:
        case ModuleId::Delay:
        case ModuleId::Reverb: return "space (parallel)";
        case ModuleId::Limiter: return "safety";
        case ModuleId::Tune: return "pitch";
    }
    return {};
}
} // namespace

juce::String ChainBar::chipName(ModuleId id)
{
    switch (id)
    {
        case ModuleId::Gate: return "Gate";
        case ModuleId::Equalizer: return "EQ";
        case ModuleId::Leveler: return "Level";
        case ModuleId::DeEsser: return "De-Ess";
        case ModuleId::Compressor: return "Comp";
        case ModuleId::Multiband: return "Multiband";
        case ModuleId::Saturation: return "Warmth";
        case ModuleId::Exciter: return "Air";
        case ModuleId::Chorus: return "Chorus";
        case ModuleId::Delay: return "Delay";
        case ModuleId::Reverb: return "Reverb";
        case ModuleId::Limiter: return "Limiter";
        case ModuleId::Tune: return "Tune";
    }
    return {};
}

ChainBar::ChainBar(StageMicProcessor& p)
    : processor(p)
{
    resetButton.setTooltip("Put every module back in the recommended order");
    resetButton.setColour(juce::TextButton::buttonOnColourId, theme::accent);
    resetButton.onClick = [this] {
        processor.getChain().setOrder(dsp::VocalChain::defaultOrder());
        refresh();
    };
    addAndMakeVisible(resetButton);
    refresh();
}

juce::String ChainBar::getTooltip()
{
    if (hovered < 0)
        return "Signal chain, in processing order. Click a module to show it, drag to move it, "
               "click its square to switch it on or off. The Limiter always stays last.";
    const auto id = shown[(size_t)hovered];
    juce::String text = describe(id).displayName + juce::String(" (") + familyName(id) + ", "
                      + (enabled[(size_t)id] ? "on" : "off") + ")\nClick to show";
    if (!isLocked(hovered))
        text << ", drag to move";
    text << ", square = on/off";
    if (isLocked(hovered))
        text << "\nThe Limiter always stays last, so the stream can't clip.";
    return text;
}

void ChainBar::setActivity(const dsp::VocalChain::Activity& a)
{
    std::array<float, kNumModules> before{};
    for (int i = 0; i < kNumModules; ++i)
        before[(size_t)i] = activityOf((ModuleId)i);
    activity = a;
    for (int i = 0; i < kNumModules; ++i)
        if (std::abs(activityOf((ModuleId)i) - before[(size_t)i]) > 0.02f)
        {
            repaint(chipArea); // only when a bar visibly moves
            return;
        }
}

float ChainBar::activityOf(ModuleId id) const
{
    // 0..1: how hard the module is working right now (shown as a bar in its chip).
    switch (id)
    {
        case ModuleId::Gate: return juce::jlimit(0.0f, 1.0f, 1.0f - activity.gateGain);
        case ModuleId::Leveler: return juce::jlimit(0.0f, 1.0f, std::abs(activity.levelerGainDb) / 12.0f);
        case ModuleId::Compressor: return juce::jlimit(0.0f, 1.0f, activity.compressorReductionDb / 12.0f);
        case ModuleId::Multiband:
            return juce::jlimit(0.0f, 1.0f, juce::jmax(activity.multibandReductionDb[0], activity.multibandReductionDb[1],
                                                       activity.multibandReductionDb[2]) / 12.0f);
        case ModuleId::DeEsser: return juce::jlimit(0.0f, 1.0f, activity.deEsserReductionDb / 8.0f);
        case ModuleId::Equalizer: return juce::jlimit(0.0f, 1.0f, juce::jmax(activity.tameReductionDb / 8.0f, activity.plosiveAmount));
        case ModuleId::Limiter: return juce::jlimit(0.0f, 1.0f, activity.limiterReductionDb / 6.0f);
        default: return 0.0f;
    }
}

void ChainBar::refresh()
{
    const auto order = processor.getChain().getOrder();
    bool changed = order != shown;
    shown = order;

    const bool recommended = order == dsp::VocalChain::defaultOrder();
    resetButton.setToggleState(!recommended, juce::dontSendNotification);
    resetButton.setButtonText(recommended ? juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93 Recommended")) : "Recommended order");
    resetButton.setEnabled(!recommended);

    for (const auto& d : moduleDescriptors())
    {
        const bool on = processor.getParameters().getRawParameterValue(d.enableParam)->load() >= 0.5f;
        changed |= enabled[(size_t)d.id] != on;
        enabled[(size_t)d.id] = on;
    }

    if (changed)
        repaint();
}

void ChainBar::resized()
{
    auto area = getLocalBounds().reduced(0, 3);
    resetButton.setBounds(area.removeFromRight(136));
    area.removeFromRight(8);
    area.removeFromLeft(62); // "CHAIN" label
    chipArea = area;
}

juce::Rectangle<float> ChainBar::chipBounds(int index) const
{
    const float w = ((float)chipArea.getWidth() - arrowW * (kNumModules - 1)) / (float)kNumModules;
    return {(float)chipArea.getX() + (float)index * (w + arrowW), (float)chipArea.getY(), w, (float)chipArea.getHeight()};
}

juce::Rectangle<float> ChainBar::ledBounds(int index) const
{
    const auto r = chipBounds(index);
    return juce::Rectangle<float>(10.0f, 10.0f).withCentre({r.getX() + 10.0f, r.getCentreY() + 1.0f});
}

int ChainBar::indexAt(juce::Point<float> p) const
{
    for (int i = 0; i < kNumModules; ++i)
        if (chipBounds(i).contains(p))
            return i;
    return -1;
}

bool ChainBar::isLocked(int index) const
{
    return shown[(size_t)index] == ModuleId::Limiter;
}

int ChainBar::dropIndexFor(float x) const
{
    // Nearest slot, never past the Limiter.
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = 0; i < kNumModules; ++i)
    {
        if (isLocked(i))
            continue;
        const float d = std::abs(chipBounds(i).getCentreX() - x);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void ChainBar::paint(juce::Graphics& g)
{
    g.setColour(theme::dimText);
    g.setFont(theme::font(11.5f, true));
    g.drawText("CHAIN", getLocalBounds().removeFromLeft(56), juce::Justification::centredLeft);

    // Parallel effects next to each other share one bracket underneath.
    for (int i = 0; i < kNumModules;)
    {
        if (!isSendModule(shown[(size_t)i]))
        {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 < kNumModules && isSendModule(shown[(size_t)j + 1]))
            ++j;
        if (j > i)
        {
            const auto a = chipBounds(i), b = chipBounds(j);
            const auto bracket = juce::Rectangle<float>(a.getX() - 3.0f, a.getY() - 2.0f, b.getRight() - a.getX() + 6.0f, a.getHeight() + 4.0f);
            g.setColour(familyColour(ModuleId::Reverb).withAlpha(0.10f));
            g.fillRoundedRectangle(bracket, 7.0f);
            g.setColour(familyColour(ModuleId::Reverb).withAlpha(0.45f));
            g.drawRoundedRectangle(bracket, 7.0f, 1.0f);
        }
        i = j + 1;
    }

    for (int i = 0; i < kNumModules; ++i)
    {
        const auto id = shown[(size_t)i];
        auto r = chipBounds(i);
        const bool on = enabled[(size_t)id];
        const auto family = familyColour(id);

        if (i < kNumModules - 1)
        {
            const auto next = shown[(size_t)i + 1];
            const auto gap = juce::Rectangle<float>(r.getRight(), r.getY(), arrowW, r.getHeight());
            g.setColour(theme::faintText);
            g.setFont(theme::font(11.0f, true));
            g.drawText(isSendModule(id) && isSendModule(next) ? "+" : ">", gap, juce::Justification::centred);
        }

        if (i == dragging)
        {
            g.setColour(theme::outline);
            g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 1.0f);
            continue; // drawn under the mouse below
        }

        const bool hover = i == hovered;
        g.setColour(on ? theme::panelRaised.brighter(hover ? 0.08f : 0.0f) : theme::panelOff.brighter(hover ? 0.06f : 0.0f));
        g.fillRoundedRectangle(r, 5.0f);

        // Family stripe along the top.
        g.saveState();
        g.reduceClipRegion(r.withHeight(3.0f).toNearestInt());
        g.setColour(family.withAlpha(on ? 0.95f : 0.35f));
        g.fillRoundedRectangle(r, 5.0f);
        g.restoreState();

        g.setColour(on ? family.withAlpha(0.6f) : theme::outline);
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);

        // On/off square.
        const auto led = ledBounds(i);
        g.setColour(on ? family : theme::background);
        g.fillRoundedRectangle(led, 2.0f);
        g.setColour(on ? family : theme::faintText);
        g.drawRoundedRectangle(led, 2.0f, 1.0f);

        g.setColour(on ? theme::text : theme::faintText);
        g.setFont(theme::font(11.5f, on));
        g.drawFittedText(chipName(id) + (isLocked(i) ? " *" : ""), r.withTrimmedLeft(18.0f).toNearestInt().reduced(2, 0),
                         juce::Justification::centred, 1, 0.7f);

        // How hard it's working right now.
        const float amount = on ? activityOf(id) : 0.0f;
        if (amount > 0.01f)
        {
            const auto track = r.reduced(6.0f, 0.0f).removeFromBottom(4.0f).translated(0.0f, -2.0f);
            g.setColour(theme::reduction.withAlpha(0.9f));
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * amount), 1.5f);
        }
    }

    if (dragging >= 0)
    {
        // Where it will land, then the chip itself under the mouse.
        const auto target = chipBounds(dropAt);
        g.setColour(theme::accent);
        g.fillRoundedRectangle(target.withWidth(3.0f).translated(dropAt > dragging ? target.getWidth() - 1.0f : -2.0f, 0.0f), 1.5f);

        auto r = chipBounds(dragging).withCentre({dragX, (float)chipArea.getCentreY()});
        g.setColour(theme::accent.withAlpha(0.9f));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(theme::font(11.5f, true));
        g.drawFittedText(chipName(shown[(size_t)dragging]), r.toNearestInt().reduced(3, 0), juce::Justification::centred, 1, 0.7f);
    }
}

void ChainBar::mouseMove(const juce::MouseEvent& e)
{
    const int i = indexAt(e.position);
    if (i != hovered)
    {
        hovered = i;
        repaint(chipArea);
    }
    const bool overLed = i >= 0 && ledBounds(i).expanded(3.0f).contains(e.position);
    setMouseCursor(i < 0 ? juce::MouseCursor::NormalCursor
                         : (overLed ? juce::MouseCursor::PointingHandCursor
                                    : (isLocked(i) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor)));
}

void ChainBar::mouseDown(const juce::MouseEvent& e)
{
    pressed = indexAt(e.position);
    movedEnough = false;
    if (pressed < 0)
        return;

    // The square switches the module on or off.
    if (ledBounds(pressed).expanded(3.0f).contains(e.position))
    {
        if (auto* p = processor.getParameters().getParameter(describe(shown[(size_t)pressed]).enableParam))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
        pressed = -1;
        refresh();
        return;
    }

    if (!isLocked(pressed))
    {
        dragging = pressed;
        dropAt = pressed;
        dragX = e.position.x;
    }
}

void ChainBar::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 4)
        movedEnough = true;
    if (dragging < 0 || !movedEnough)
        return;
    dragX = juce::jlimit((float)chipArea.getX(), (float)chipArea.getRight(), e.position.x);
    dropAt = dropIndexFor(dragX);
    repaint();
}

void ChainBar::mouseUp(const juce::MouseEvent&)
{
    if (!movedEnough && pressed >= 0)
    {
        // A click, not a drag: show that module.
        const auto id = shown[(size_t)pressed];
        dragging = dropAt = pressed = -1;
        repaint();
        if (onModuleClicked)
            onModuleClicked(id);
        return;
    }

    if (dragging >= 0 && dropAt >= 0 && dropAt != dragging)
    {
        auto order = shown;
        if (dropAt > dragging)
            std::rotate(order.begin() + dragging, order.begin() + dragging + 1, order.begin() + dropAt + 1);
        else
            std::rotate(order.begin() + dropAt, order.begin() + dragging, order.begin() + dragging + 1);
        processor.getChain().setOrder(order);
    }
    dragging = dropAt = pressed = -1;
    refresh();
    repaint();
}

} // namespace stagemic
