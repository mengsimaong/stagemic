// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/dsp/DspUtils.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic::dsp
{

/** One built-in effect in the vocal chain.

    Real-time contract:
      prepare()           - may allocate; called before audio starts.
      reset/update/process- audio thread; must never allocate, lock or block.
    Blocks passed to process() are at most VocalChain::kSubBlock samples. */
class Module
{
public:
    virtual ~Module() = default;

    virtual void prepare(double sampleRate, int numChannels) = 0;

    /** Clears internal state (filters, envelopes, delay lines). */
    virtual void reset() noexcept = 0;

    /** Reads the current parameter values. snap = jump straight to them (no gliding). */
    virtual void update(bool snap) noexcept = 0;

    virtual void process(float* const* channels, int numChannels, int numSamples) noexcept = 0;

    /** Send effects (Chorus, Delay, Reverb) run in parallel with the dry voice:
        process() receives a copy of the voice and must replace it with the effect
        signal only ("100% wet"). The chain mixes the result back in. */
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
