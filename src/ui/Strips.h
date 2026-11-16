// SPDX-License-Identifier: AGPL-3.0-or-later
// The module panels in the lower half of the StageMic window
#pragma once

#include "ui/Displays.h"
#include "ui/Widgets.h"

#include <memory>
#include <vector>

namespace stagemic
{

class GateStrip final : public Section
{
public:
    explicit GateStrip(APVTS&);
    void updateActivity(const dsp::VocalChain::Activity&) override;
    void paintOverChildren(juce::Graphics&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    Fader threshold;
    SmallKnob range, release, ratio;
    float gateGain = 1.0f;
};

class DeEsserStrip final : public Section
{
public:
    DeEsserStrip(APVTS&, dsp::VocalChain&);
    ~DeEsserStrip() override;
    void updateActivity(const dsp::VocalChain::Activity&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    dsp::VocalChain& chain;
    Fader threshold;
    ReductionMeter reduction{12.0f};
    SmallKnob freq, maxCut;
    ChoiceToggle mode;
    juce::TextButton autoButton{"Auto"}, listenButton{"Listen"};
    APVTS::ButtonAttachment autoAttachment;
};

class CompressorStrip final : public Section
{
public:
    explicit CompressorStrip(APVTS&);
    void updateActivity(const dsp::VocalChain::Activity&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    Fader threshold;
    ReductionMeter reduction{24.0f};
    SmallKnob ratio, attack, release, makeup, mix;
};

class EffectRow final : public Section
{
public:
    EffectRow(APVTS&, ModuleId module, std::initializer_list<std::pair<const char*, const char*>> knobs, bool showReduction = false);
    void updateActivity(const dsp::VocalChain::Activity&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    ModuleId module;
    std::vector<std::unique_ptr<SmallKnob>> knobs;
    std::unique_ptr<ReductionMeter> reduction;
};

// Several modules sharing one panel, one tab each
class TabbedModules final : public juce::Component
{
public:
    struct Control
    {
        const char* paramId;
        const char* label;
        const char* tooltip = nullptr;
    };
    struct Tab
    {
        ModuleId module;
        const char* title;
        std::vector<Control> controls;
    };

    TabbedModules(APVTS&, std::vector<Tab> tabs, juce::String footer, int initialTab = 0, int maxColumns = 4);
    void paint(juce::Graphics&) override;
    void resized() override;
    void select(int index);

private:
    struct Page
    {
        std::unique_ptr<ModuleTab> tab;
        std::vector<std::unique_ptr<juce::Component>> controls;
    };

    std::vector<Page> pages;
    juce::String footer;
    int current = 0;
    int maxColumns = 4;
};

} // namespace stagemic
