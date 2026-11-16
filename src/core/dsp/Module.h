// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/dsp/DspUtils.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic::dsp
{

// One built-in effect in the vocal chain
class Module
{
public:
    virtual ~Module() = default;

    virtual void prepare(double sampleRate, int numChannels) = 0;

    // Clears internal state
    virtual void reset() noexcept = 0;

    // Reads the current parameter values
    virtual void update(bool snap) noexcept = 0;

    virtual void process(float* const* channels, int numChannels, int numSamples) noexcept = 0;

    // Send effects run in parallel with the dry voice
    virtual bool isSend() const noexcept { return false; }

protected:
    static std::atomic<float>* param(juce::AudioProcessorValueTreeState& state, const char* id)
    {
        auto* p = state.getRawParameterValue(id);
        jassert(p != nullptr);
        return p;
    }
};

} // namespace stagemic::dsp
