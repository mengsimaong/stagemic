// SPDX-License-Identifier: AGPL-3.0-or-later
// The module panels in the lower half of the StageMic window.
#pragma once

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
    SmallKnob range, release;
    float gateGain = 1.0f;
};

class DeEsserStrip final : public Section
{
public:
    explicit DeEsserStrip(APVTS&);
    void updateActivity(const dsp::VocalChain::Activity&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    Fader threshold;
    ReductionMeter reduction{12.0f};
    SmallKnob freq, maxCut;
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
    SmallKnob ratio, attack, release, makeup;
};

class SaturationStrip final : public Section
{
public:
    explicit SaturationStrip(APVTS&);

private:
    void layoutContent(juce::Rectangle<int>) override;
    Fader drive, mix;
};

/** Compact row: title on the left, knobs to the right (Chorus, Delay, Reverb, Limiter). */
class EffectRow final : public Section
{
public:
    EffectRow(APVTS&, ModuleId module, std::initializer_list<std::pair<const char*, const char*>> knobs,
              bool showReduction = false);
    void updateActivity(const dsp::VocalChain::Activity&) override;

private:
    void layoutContent(juce::Rectangle<int>) override;
    ModuleId module;
    std::vector<std::unique_ptr<SmallKnob>> knobs;
    std::unique_ptr<ReductionMeter> reduction;
};

} // namespace stagemic
