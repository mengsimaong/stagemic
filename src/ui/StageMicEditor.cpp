// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/StageMicEditor.h"

#ifndef STAGEMIC_VERSION
  #define STAGEMIC_VERSION "dev"
#endif

namespace stagemic
{

StageMicEditor::StageMicEditor(StageMicProcessor& p)
    : juce::AudioProcessorEditor(p)
    , processor(p)
    , presetBar(p)
    , display(p.getParameters(), p.getSpectrumTap(), [&p] { return p.getPreparedSampleRate(); })
    , gate(p.getParameters())
    , deEsser(p.getParameters())
    , compressor(p.getParameters())
    , saturation(p.getParameters())
    , chorus(p.getParameters(), ModuleId::Chorus, {{pid::chRate, "Rate"}, {pid::chDepth, "Depth"}, {pid::chMix, "Mix"}})
    , delay(p.getParameters(), ModuleId::Delay,
            {{pid::dlyTime, "Time"}, {pid::dlyFeedback, "Feedback"}, {pid::dlyTone, "Tone"}, {pid::dlyMix, "Mix"}})
    , reverb(p.getParameters(), ModuleId::Reverb,
             {{pid::revSize, "Size"}, {pid::revDamping, "Damping"}, {pid::revWidth, "Width"}, {pid::revMix, "Mix"}})
    , limiter(p.getParameters(), ModuleId::Limiter, {{pid::limCeiling, "Ceiling"}, {pid::limRelease, "Release"}}, true)
    , master(p.getParameters())
{
    for (auto* c : std::initializer_list<juce::Component*>{&presetBar, &display, &gate, &deEsser, &compressor, &saturation,
                                                           &chorus, &delay, &reverb, &limiter, &master})
        content.addAndMakeVisible(c);
    addAndMakeVisible(content);
    // Apply the theme after every control exists, so all of them (including the
    // value boxes under knobs and faders) are rebuilt in StageMic's style.
    setLookAndFeel(&lookAndFeel);

    // Fixed design layout (see header): header, display + master column, strips + effect rows.
    constexpr int m = 10, gap = 6;
    presetBar.setBounds(m, 0, designWidth - 2 * m, 48);
    master.setBounds(designWidth - m - 200, 54, 200, designHeight - 54 - m);

    const int left = m, right = master.getX() - gap;
    display.setBounds(left, 54, right - left, 250 + DisplayPanel::fieldRowHeight + 8);

    const int stripsTop = display.getBottom() + gap, stripsH = designHeight - m - stripsTop;
    int x = left;
    for (auto [c, w] : {std::pair<juce::Component*, int>{&gate, 100}, {&deEsser, 112}, {&compressor, 208}, {&saturation, 104}})
    {
        c->setBounds(x, stripsTop, w, stripsH);
        x += w + gap;
    }

    const int rowH = (stripsH - 3 * gap) / 4;
    int y = stripsTop;
    for (auto* row : {&chorus, &delay, &reverb, &limiter})
    {
        row->setBounds(x, y, right - x, rowH);
        y += rowH + gap;
    }

    content.setSize(designWidth, designHeight);

    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)designWidth / designHeight);
    setResizeLimits(designWidth * 7 / 10, designHeight * 7 / 10, designWidth * 2, designHeight * 2);
    setSize(designWidth, designHeight);
    startTimerHz(30);
}

StageMicEditor::~StageMicEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void StageMicEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
}

void StageMicEditor::resized()
{
    const float scale = (float)getWidth() / (float)designWidth;
    content.setTransform(juce::AffineTransform::scale(scale));
}

void StageMicEditor::refreshFromProcessor(bool evenIfHidden)
{
    // Always drain the meters so a stale peak doesn't appear when the window reopens.
    const auto inPeak = processor.getInputMeter().takePeak();
    const auto outPeak = processor.getOutputMeter().takePeak();

    if (!isShowing() && !evenIfHidden)
        return;

    const auto activity = processor.getChain().getActivity();
    master.setLevels(inPeak, outPeak);
    master.setStatus(juce::String(processor.getPreparedSampleRate() / 1000.0, 1) + " kHz  |  latency 0 ms\nStageMic v" STAGEMIC_VERSION);
    display.tick(activity);
    for (auto* s : std::initializer_list<Section*>{&gate, &deEsser, &compressor, &saturation, &chorus, &delay, &reverb, &limiter})
        s->updateActivity(activity);
}

} // namespace stagemic
