// SPDX-License-Identifier: AGPL-3.0-or-later
// StageMic's look: dark, compact channel strip with a pink accent.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic::theme
{
inline const juce::Colour background{0xff101116};
inline const juce::Colour panel{0xff1a1c23};
inline const juce::Colour panelRaised{0xff22252e};
inline const juce::Colour panelOff{0xff16171d};
inline const juce::Colour outline{0xff2b2e38};
inline const juce::Colour text{0xffe9e9f0};
inline const juce::Colour dimText{0xff8b8e9a};
inline const juce::Colour faintText{0xff5d606b};
inline const juce::Colour accent{0xffff4f9a};
inline const juce::Colour accentSoft{0xffff8cbf};
inline const juce::Colour spectrum{0xff5f7bb0};
inline const juce::Colour meterGreen{0xff3ddc97};
inline const juce::Colour meterYellow{0xfff5d547};
inline const juce::Colour meterRed{0xffff5470};
inline const juce::Colour reduction{0xffffa64d};

inline juce::FontOptions font(float size, bool bold = false)
{
    return juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain);
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme({background, panel, panel, outline, text, accent, juce::Colours::white, accent, text});
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.4f));
        setColour(juce::ComboBox::backgroundColourId, panelRaised);
        setColour(juce::ComboBox::outlineColourId, outline);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, dimText);
        setColour(juce::TextButton::buttonColourId, panelRaised);
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::PopupMenu::backgroundColourId, panelRaised);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.35f));
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::TooltipWindow::backgroundColourId, panelRaised);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, outline);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::TextEditor::backgroundColourId, panelRaised);
        setColour(juce::TextEditor::outlineColourId, outline);
    }

    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(2.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = start + pos * (end - start);
        const float thickness = juce::jmax(2.5f, radius * 0.14f);
        const auto arcRadius = radius - thickness * 0.5f;
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto zeroAngle = bipolar ? start + (float)slider.valueToProportionOfLength(0.0) * (end - start) : start;

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
        g.setColour(outline);
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin(zeroAngle, angle),
                            juce::jmax(zeroAngle, angle), true);
        g.setColour(accent.withAlpha(slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto capRadius = radius - thickness * 1.9f;
        g.setGradientFill(juce::ColourGradient(panelRaised.brighter(0.25f), centre.x, centre.y - capRadius, panelRaised,
                                               centre.x, centre.y + capRadius, false));
        g.fillEllipse(juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(centre));

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.2f, -capRadius + 1.5f, 2.4f, capRadius * 0.6f, 1.2f);
        g.setColour(text);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            // Slim track; filled from the left, or from 0 for +/- controls; round thumb.
            const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
            const auto track = bounds.withSizeKeepingCentre(bounds.getWidth(), 4.0f);
            g.setColour(background);
            g.fillRoundedRectangle(track, 2.0f);

            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            const float from = bipolar ? (float)x + (float)w * (float)slider.valueToProportionOfLength(0.0) : (float)x;
            g.setColour(accent.withAlpha(slider.isEnabled() ? 0.85f : 0.35f));
            g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(from, sliderPos), track.getY(), std::abs(sliderPos - from), 4.0f), 2.0f);

            const auto thumb = juce::Rectangle<float>(11.0f, 11.0f).withCentre({sliderPos, track.getCentreY()});
            g.setColour(juce::Colour(0xffd9dae2));
            g.fillEllipse(thumb);
            g.setColour(accent);
            g.drawEllipse(thumb.reduced(0.5f), 1.5f);
            return;
        }

        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, slider);
            return;
        }

        const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
        const float cx = bounds.getCentreX();
        const auto track = juce::Rectangle<float>(cx - 2.0f, bounds.getY(), 4.0f, bounds.getHeight());

        // Scale ticks
        g.setColour(outline);
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = bounds.getY() + bounds.getHeight() * (float)i / 10.0f;
            const float len = i % 5 == 0 ? 9.0f : 5.0f;
            g.drawHorizontalLine((int)ty, cx - 8.0f - len, cx - 8.0f);
            g.drawHorizontalLine((int)ty, cx + 8.0f, cx + 8.0f + len);
        }

        g.setColour(background);
        g.fillRoundedRectangle(track, 2.0f);
        g.setColour(accent.withAlpha(0.75f));
        g.fillRoundedRectangle(track.withTop(sliderPos), 2.0f);

        // Fader cap
        const auto cap = juce::Rectangle<float>(26.0f, 14.0f).withCentre({cx, sliderPos});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd9dae2), cap.getX(), cap.getY(), juce::Colour(0xff8f929e),
                                               cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 3.0f);
        g.setColour(background.withAlpha(0.6f));
        g.drawRoundedRectangle(cap, 3.0f, 1.0f);
        g.setColour(accent);
        g.fillRect(cap.withSizeKeepingCentre(cap.getWidth() - 8.0f, 2.0f));
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout(slider);
        if (slider.getSliderStyle() == juce::Slider::LinearVertical)
            layout.sliderBounds = layout.sliderBounds.reduced(0, 8); // room for the fader cap
        return layout;
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox(slider);
        l->setFont(font(12.0f));
        l->setColour(juce::Label::textColourId, text);
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineWhenEditingColourId, accent);
        l->setColour(juce::Label::backgroundWhenEditingColourId, background);
        l->setMinimumHorizontalScale(0.75f);
        return l;
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? panelRaised.brighter(0.15f) : (over ? panelRaised.brighter(0.07f) : panelRaised));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(outline);
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(13.0f); }
};
} // namespace stagemic::theme
