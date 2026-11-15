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
    , chainBar(p)
    , display(p.getParameters(), p.getSpectrumTap(), [&p] { return p.getPreparedSampleRate(); }, p.getChain().getTune())
    , gate(p.getParameters())
    , deEsser(p.getParameters(), p.getChain())
    , compressor(p.getParameters())
    , color(p.getParameters(),
            {{ModuleId::Saturation, "Warmth",
              {{pid::satModel, "Model", nullptr},
               {pid::satDrive, "Drive", nullptr},
               {pid::satMix, "Mix", nullptr},
               {pid::satCharacter, "Character", "Character: low = edgier, high = warmer"}}},
             {ModuleId::Exciter, "Air",
              {{pid::exFreq, "Freq", "Exciter: adds new harmonics above this frequency"},
               {pid::exAmount, "Amount", "How strongly the harmonics are generated"},
               {pid::exMix, "Mix", "How much of the new harmonics is added on top"}}}},
            "COLOR  (Warmth = saturation, Air = exciter)", 0, 2)
    , space(p.getParameters(),
            {{ModuleId::Chorus, "Chorus", {{pid::chRate, "Rate"}, {pid::chDepth, "Depth"}, {pid::chMix, "Level"}}},
             {ModuleId::Delay, "Delay",
              {{pid::dlyTime, "Time"}, {pid::dlyFeedback, "Feedback"}, {pid::dlyTone, "Tone"}, {pid::dlyMix, "Level"},
               {pid::dlyDuck, "Duck", "Duck: how much the echoes drop while you sing (they come back in the gaps)"}}},
             {ModuleId::Reverb, "Reverb",
              {{pid::revSize, "Size"}, {pid::revDamping, "Damping"}, {pid::revWidth, "Width"}, {pid::revMix, "Level"},
               {pid::revPreDelay, "Pre-Delay"}, {pid::revLowCut, "Low Cut"}, {pid::revHighCut, "High Cut"},
               {pid::revDuck, "Duck", "Duck: how much the reverb drops while you sing (it blooms in the gaps)"}}}},
            "SPACE  (parallel effects)", 2, 4)
    , limiter(p.getParameters(), ModuleId::Limiter, {{pid::limCeiling, "Ceiling"}, {pid::limRelease, "Release"}}, true)
    , master(p.getParameters())
    , simple(p.getParameters(), p.getChain().getTune())
    , voiceSetup(p)
{
    for (auto* c : std::initializer_list<juce::Component*>{&presetBar, &chainBar, &display, &gate, &deEsser, &compressor, &color,
                                                           &space, &limiter, &master})
        content.addAndMakeVisible(c);
    content.addChildComponent(simple);
    content.addAndMakeVisible(cat);
    content.addChildComponent(flash);
    master.onHearBefore = [this](bool on) { processor.setHearBefore(on); };
    presetBar.onModeChanged = [this](bool advanced) {
        processor.setAdvancedMode(advanced);
        applyMode(advanced);
    };
    content.addChildComponent(voiceSetup);
    presetBar.onSetUpVoice = [this] { voiceSetup.open(); };
    chainBar.onModuleClicked = [this](ModuleId id) { showModule(id); };
    addAndMakeVisible(content);
    // Apply the theme after every control exists, so all of them (including the
    // value boxes under knobs and faders) are rebuilt in StageMic's style.
    setLookAndFeel(&lookAndFeel);

    // Fixed design layout (see header): header, display + master column, strips + effect rows.
    constexpr int m = 10, gap = 6;
    presetBar.setBounds(m, 0, designWidth - 2 * m, 48);
    chainBar.setBounds(m, 48, designWidth - 2 * m, 34);
    constexpr int top = 88;
    master.setBounds(designWidth - m - 180, top, 180, bodyBottom - top);

    const int left = m, right = master.getX() - gap;
    display.setBounds(left, top, right - left, 250 + DisplayPanel::fieldRowHeight + 8);

    const int stripsTop = display.getBottom() + gap, stripsH = bodyBottom - stripsTop;
    int x = left;
    for (auto [c, w] : {std::pair<juce::Component*, int>{&gate, 118}, {&deEsser, 118}, {&compressor, 206}, {&color, 172}})
    {
        c->setBounds(x, stripsTop, w, stripsH);
        x += w + gap;
    }

    constexpr int limiterH = 84;
    space.setBounds(x, stripsTop, right - x, stripsH - limiterH - gap);
    limiter.setBounds(x, space.getBottom() + gap, right - x, limiterH);

    simple.setBounds(left, 56, right - left, bodyBottom - 56);
    cat.setBounds(0, bodyBottom, designWidth, footerHeight);
    content.setSize(designWidth, designHeight);
    flash.setBounds(0, 0, designWidth, designHeight);
    flash.toFront(false);
    voiceSetup.setBounds(0, 0, designWidth, designHeight);

    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)designWidth / designHeight);
    setResizeLimits(designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    applyMode(processor.isAdvancedMode());
    // The size they chose last time (the window then makes sure it fits the screen).
    const int width = processor.getEditorWidth() > 0 ? juce::jlimit(designWidth * 6 / 10, designWidth * 2, processor.getEditorWidth()) : designWidth;
    setSize(width, juce::roundToInt(width * (double)designHeight / designWidth));
    sizeRestored = true;

    // First time ever: start with "Set up my voice" (it fixes the gate for noisy
    // rooms and laptop fans, and the input level). Shown once; Cancel skips it.
    if (!processor.hasVoiceSetup() && !processor.wasVoiceSetupOffered())
    {
        processor.setVoiceSetupOffered();
        voiceSetup.openWelcome();
    }
    startTimerHz(StageMicEditor::refreshRateHz);
}

StageMicEditor::~StageMicEditor()
{
    stopTimer();
    processor.setHearBefore(false); // never leave "before" on when the window goes
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
    if (sizeRestored)
        processor.setEditorWidth(getWidth());
}

void StageMicEditor::applyMode(bool advanced)
{
    presetBar.setMode(advanced);
    for (auto* c : std::initializer_list<juce::Component*>{&chainBar, &display, &gate, &deEsser, &compressor, &color, &space, &limiter})
        c->setVisible(advanced);
    simple.setVisible(!advanced);
    display.setLive(advanced);
    // Simple mode has no chain bar: the meters start right under the header.
    const int top = advanced ? 88 : 56;
    master.setBounds(master.getX(), top, master.getWidth(), bodyBottom - top);
}

void StageMicEditor::showModule(ModuleId id)
{
    juce::Component* panel = nullptr;
    switch (id)
    {
        case ModuleId::Gate: panel = &gate; break;
        case ModuleId::Equalizer: display.select(0); panel = &display; break;
        case ModuleId::Multiband: display.select(1); panel = &display; break;
        case ModuleId::Leveler: panel = &master; break;
        case ModuleId::DeEsser: panel = &deEsser; break;
        case ModuleId::Compressor: panel = &compressor; break;
        case ModuleId::Saturation: color.select(0); panel = &color; break;
        case ModuleId::Exciter: color.select(1); panel = &color; break;
        case ModuleId::Chorus: space.select(0); panel = &space; break;
        case ModuleId::Delay: space.select(1); panel = &space; break;
        case ModuleId::Reverb: space.select(2); panel = &space; break;
        case ModuleId::Limiter: panel = &limiter; break;
        case ModuleId::Tune: display.select(2); panel = &display; break;
    }
    if (panel != nullptr)
        flash.show(panel->getBounds());
}

void StageMicEditor::refreshFromProcessor(bool evenIfHidden)
{
    // Always drain the meters so a stale peak doesn't appear when the window reopens.
    const auto inPeak = processor.getInputMeter().takePeak();
    const auto outPeak = processor.getOutputMeter().takePeak();

    // Window closed or minimised: stop the spectrum's audio tap too, so a hidden
    // window costs the audio thread nothing.
    const bool showing = isShowing() || evenIfHidden;
    if (showing != wasShowing)
    {
        wasShowing = showing;
        display.setLive(showing && processor.isAdvancedMode());
    }
    if (!showing)
        return;

    if (voiceSetup.isVisible())
        voiceSetup.tick(inPeak);

    const auto activity = processor.getChain().getActivity();
    chainBar.refresh();
    chainBar.setActivity(activity);
    flash.step();
    master.setLevels(inPeak, outPeak);
    master.setLevelerGain(activity.levelerGainDb);
    // Saturation and Exciter oversample, which delays only their effect by about 0.1 ms.
    const auto& chain = processor.getChain();
    const bool oversampling = chain.isModuleActive(ModuleId::Saturation) || chain.isModuleActive(ModuleId::Exciter);
    const bool tuning = chain.isModuleActive(ModuleId::Tune);
    master.setStatus(juce::String(processor.getPreparedSampleRate() / 1000.0, 1) + " kHz  |  latency "
                     + (tuning ? (oversampling ? "20.1 ms" : "20 ms") : (oversampling ? "0.1 ms" : "0 ms"))
                     + "\nPaopoa StageMic v" STAGEMIC_VERSION);
    if (simple.isVisible())
        simple.tick();
    cat.advance();
    display.tick(activity);
    for (auto* s : std::initializer_list<Section*>{&gate, &deEsser, &compressor, &limiter})
        s->updateActivity(activity);
}

} // namespace stagemic
