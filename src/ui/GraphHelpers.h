// SPDX-License-Identifier: AGPL-3.0-or-later
// Small helpers shared by the graph components
#pragma once

#include "core/dsp/DspUtils.h"
#include "ui/Widgets.h"

#include <complex>

namespace stagemic::graph
{

inline constexpr float minFreq = 20.0f, maxFreq = 20000.0f;

inline juce::String formatFreq(float f)
{
    return f >= 1000.0f ? juce::String(f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz";
}

inline float biquadMagnitudeDb(const dsp::BiquadCoeffs& c, double freq, double sampleRate)
{
    const double w = 2.0 * dsp::kPi * freq / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const auto num = (double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2;
    const auto den = 1.0 + (double)c.a1 * z1 + (double)c.a2 * z2;
    return (float)(20.0 * std::log10(std::max(1.0e-9, std::abs(num / den))));
}

inline void beginParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
            p->beginChangeGesture();
}

inline void endParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
            p->endChangeGesture();
}

inline void resetParam(APVTS& s, const char* id)
{
    if (id != nullptr)
        if (auto* p = s.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            p->endChangeGesture();
        }
}
} // namespace stagemic::graph
