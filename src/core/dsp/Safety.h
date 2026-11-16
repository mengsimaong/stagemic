// SPDX-License-Identifier: AGPL-3.0-or-later
// Guards around the whole chain
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace stagemic::dsp
{

// True for ordinary numbers
inline bool isNumber(float x) noexcept
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}

// Replaces anything that isn't a number with silence
inline int scrubNonFinite(float* const* ch, int numChannels, int n) noexcept
{
    int replaced = 0;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            if (!isNumber(ch[c][i]))
            {
                ch[c][i] = 0.0f;
                ++replaced;
            }
    return replaced;
}

// Last line of defence after Output gain
inline bool safetyLimit(float* const* ch, int numChannels, int n, float& envelope, float release) noexcept
{
    constexpr float ceiling = 0.999f; // just under 0 dBFS
    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = ch[c][i];
            if (!isNumber(x))
            {
                envelope = 0.0f;
                return false;
            }
            peak = std::fmax(peak, std::fabs(x));
        }
        envelope = std::fmax(peak, envelope * release);
        if (envelope > ceiling)
        {
            const float g = ceiling / envelope;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] *= g;
        }
    }
    return true;
}

} // namespace stagemic::dsp
