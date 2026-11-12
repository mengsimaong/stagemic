// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/Parameters.h"
#include "core/StageMicProcessor.h"

#include <cmath>
#include <vector>

namespace stagemic::test
{

inline void setParam(StageMicProcessor& p, const char* id, float value)
{
    auto* param = p.getParameters().getParameter(id);
    jassert(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

inline float getParam(StageMicProcessor& p, const char* id)
{
    return p.getParameters().getRawParameterValue(id)->load();
}

/** Every module off, gains at 0 dB: StageMic should be perfectly transparent. */
inline void allModulesOff(StageMicProcessor& p)
{
    for (const auto& d : moduleDescriptors())
        setParam(p, d.enableParam, 0.0f);
    setParam(p, pid::inGain, 0.0f);
    setParam(p, pid::outGain, 0.0f);
    p.getChain().requestSnap();
}

/** Only one module on (snapped, no fade). */
inline void onlyModule(StageMicProcessor& p, ModuleId id)
{
    allModulesOff(p);
    setParam(p, describe(id).enableParam, 1.0f);
    p.getChain().requestSnap();
}

/** A processor prepared like OBS would: 48 kHz stereo (or mono), 1024-sample max blocks. */
inline std::unique_ptr<StageMicProcessor> makeProcessor(double sampleRate = 48000.0, int channels = 2)
{
    auto p = std::make_unique<StageMicProcessor>();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    p->setBusesLayout(layout);
    p->setRateAndBufferSizeDetails(sampleRate, 1024);
    p->prepareToPlay(sampleRate, 1024);
    return p;
}

/** Processes a whole signal in OBS-sized blocks (480 frames). */
inline void processSignal(StageMicProcessor& p, juce::AudioBuffer<float>& signal, int blockSize = 480)
{
    juce::MidiBuffer midi;
    for (int start = 0; start < signal.getNumSamples(); start += blockSize)
    {
        const int n = juce::jmin(blockSize, signal.getNumSamples() - start);
        juce::AudioBuffer<float> view(signal.getArrayOfWritePointers(), signal.getNumChannels(), start, n);
        p.processBlock(view, midi);
    }
}

inline juce::AudioBuffer<float> sine(double freq, float amplitude, int samples, double sampleRate = 48000.0, int channels = 2)
{
    juce::AudioBuffer<float> b(channels, samples);
    for (int i = 0; i < samples; ++i)
    {
        const float v = amplitude * (float)std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate);
        for (int c = 0; c < channels; ++c)
            b.setSample(c, i, v);
    }
    return b;
}

inline float rmsDb(const juce::AudioBuffer<float>& b, int start, int length, int channel = 0)
{
    return juce::Decibels::gainToDecibels(b.getRMSLevel(channel, start, length), -200.0f);
}

inline bool allFinite(const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(c, i)))
                return false;
    return true;
}

} // namespace stagemic::test
