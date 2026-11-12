// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

namespace stagemic
{

/** Hands a mono copy of the output from the audio thread to the UI's spectrum
    analyzer. Lock-free single-producer/single-consumer; costs nothing while no
    analyzer is open (the audio thread checks one flag). */
class SpectrumTap
{
public:
    static constexpr int capacity = 8192;

    /** UI: switch collection on while an analyzer is visible. */
    void setActive(bool shouldBeActive) noexcept { active.store(shouldBeActive, std::memory_order_relaxed); }

    /** Audio thread. Mixes up to two channels to mono. Drops samples if the UI falls behind. */
    void push(const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (!active.load(std::memory_order_relaxed) || numChannels <= 0)
            return;

        int start1, size1, start2, size2;
        fifo.prepareToWrite(numSamples, start1, size1, start2, size2);
        auto write = [&](int dest, int count, int srcOffset) {
            for (int i = 0; i < count; ++i)
            {
                float v = channels[0][srcOffset + i];
                if (numChannels > 1)
                    v = 0.5f * (v + channels[1][srcOffset + i]);
                buffer[(size_t)(dest + i)] = v;
            }
        };
        write(start1, size1, 0);
        write(start2, size2, size1);
        fifo.finishedWrite(size1 + size2);
    }

    /** UI thread. Copies up to maxSamples of the newest audio into dest; returns how many. */
    int pull(float* dest, int maxSamples) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(juce::jmin(maxSamples, fifo.getNumReady()), start1, size1, start2, size2);
        std::copy(buffer.begin() + start1, buffer.begin() + start1 + size1, dest);
        std::copy(buffer.begin() + start2, buffer.begin() + start2 + size2, dest + size1);
        fifo.finishedRead(size1 + size2);
        return size1 + size2;
    }

private:
    std::atomic<bool> active{false};
    juce::AbstractFifo fifo{capacity};
    std::array<float, capacity> buffer{};
};

} // namespace stagemic
