// SPDX-License-Identifier: AGPL-3.0-or-later
// Guards around the whole chain: nothing that isn't a number may get in or
// out, and nothing louder than full scale may reach the stream.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace stagemic::dsp
{

/** True for ordinary numbers; false for NaN and infinity. Looks at the bits,
    so it works whatever floating-point shortcuts the compiler is told to take
    (comparisons with NaN are always false, which hides it from max()/abs()). */
inline bool isNumber(float x) noexcept
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/** Replaces anything that isn't a number (e.g. from a broken driver or another
    filter) with silence, so it can never get stuck inside the reverb, echo or
    filters and break the sound until OBS restarts. Returns how many it replaced. */
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

/** Last line of defence after Output gain: a tiny safety limiter that turns
    the voice down (smoothly, no distortion) only if it would pass full scale,
    e.g. Output turned way up with the Limiter off. `envelope` is its state,
    `release` the per-sample release factor. Returns false, leaving the block
    as it is, if the chain produced something that isn't a number. */
inline bool safetyLimit(float* const* ch, int numChannels, int n, float& envelope, float release) noexcept
{
    constexpr float ceiling = 0.999f; // just under 0 dBFS (-0.01 dB)
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
