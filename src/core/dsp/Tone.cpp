// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Tone.h"

#include "core/Parameters.h"

namespace stagemic::dsp
{

//==============================================================================
Equalizer::Equalizer(juce::AudioProcessorValueTreeState& s)
    : lowCut(param(s, pid::eqLowCut))
    , lowFreq(param(s, pid::eqLowFreq))
    , lowGain(param(s, pid::eqLowGain))
    , midFreq(param(s, pid::eqMidFreq))
    , midGain(param(s, pid::eqMidGain))
    , midQ(param(s, pid::eqMidQ))
    , highFreq(param(s, pid::eqHighFreq))
    , highGain(param(s, pid::eqHighGain))
{
}

void Equalizer::prepare(double sampleRate, int)
{
    sr = sampleRate;
    update(true);
    reset();
}

void Equalizer::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
}

void Equalizer::update(bool snap) noexcept
{
    if (sLowCut.step(load(lowCut), snap) || snap)
        filters[LowCut].c = biquad::highpass(sr, sLowCut.current, 0.70710678);

    const bool lf = sLowFreq.step(load(lowFreq), snap);
    const bool lg = sLowGain.step(load(lowGain), snap);
    if (lf || lg || snap)
        filters[LowShelf].c = biquad::lowShelf(sr, sLowFreq.current, sLowGain.current);

    const bool mf = sMidFreq.step(load(midFreq), snap);
    const bool mg = sMidGain.step(load(midGain), snap);
    const bool mq = sMidQ.step(load(midQ), snap);
    if (mf || mg || mq || snap)
        filters[Mid].c = biquad::peak(sr, sMidFreq.current, std::max(0.1f, sMidQ.current), sMidGain.current);

    const bool hf = sHighFreq.step(load(highFreq), snap);
    const bool hg = sHighGain.step(load(highGain), snap);
    if (hf || hg || snap)
        filters[High].c = biquad::highShelf(sr, sHighFreq.current, sHighGain.current);
}

void Equalizer::process(float* const* ch, int numChannels, int n) noexcept
{
    for (int c = 0; c < numChannels; ++c)
    {
        float* x = ch[c];
        for (int i = 0; i < n; ++i)
        {
            float v = x[i];
            for (auto& f : filters)
                v = f.process(v, c);
            x[i] = v;
        }
    }
}

//==============================================================================
namespace
{
// Antiderivative of tanh: log(cosh(x)), written to stay accurate for large |x|.
inline double logCosh(double x) noexcept
{
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994531;
}
} // namespace

Saturation::Saturation(juce::AudioProcessorValueTreeState& s)
    : drive(param(s, pid::satDrive))
    , mix(param(s, pid::satMix))
{
}

void Saturation::prepare(double, int)
{
    update(true);
    reset();
}

void Saturation::reset() noexcept
{
    std::fill(std::begin(previous), std::end(previous), 0.0);
}

void Saturation::update(bool snap) noexcept
{
    driveDb.step(load(drive), snap);
    mixAmount.step(load(mix) * 0.01f, snap);
}

void Saturation::process(float* const* ch, int numChannels, int n) noexcept
{
    const double k = dbToGain(driveDb.current);
    // Partial level compensation: quiet parts stay roughly level, loud parts get denser.
    const double makeup = 1.0 / std::pow(k, 0.6);
    const float wet = mixAmount.current;

    for (int c = 0; c < numChannels; ++c)
    {
        float* x = ch[c];
        double prev = previous[c];

        for (int i = 0; i < n; ++i)
        {
            const double in = (double)x[i] * k;
            const double diff = in - prev;
            const double shaped = std::abs(diff) < 1.0e-5 ? std::tanh(0.5 * (in + prev))
                                                          : (logCosh(in) - logCosh(prev)) / diff;
            prev = in;

            const float sat = (float)(shaped * makeup);
            x[i] += wet * (sat - x[i]);
        }

        previous[c] = prev;
    }
}

} // namespace stagemic::dsp
