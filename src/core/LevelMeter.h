// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cmath>

namespace stagemic
{

class LevelMeter
{
public:
    void pushPeak(float peak) noexcept
    {
        auto current = held.load(std::memory_order_relaxed);
        while (peak > current && !held.compare_exchange_weak(current, peak, std::memory_order_relaxed))
        {
        }
    }

    // Returns the highest peak since the last call and resets it
    float takePeak() noexcept { return held.exchange(0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> held{0.0f};
    static_assert(std::atomic<float>::is_always_lock_free);
};

} // namespace stagemic
