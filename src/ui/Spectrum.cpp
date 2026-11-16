// SPDX-License-Identifier: AGPL-3.0-or-later
// Live output spectrum, and the frequency axis the EQ and Multiband
#include "ui/Displays.h"

#include "ui/GraphHelpers.h"
#include "ui/Theme.h"

namespace stagemic
{

using namespace graph;

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer() = default;

bool SpectrumAnalyzer::update(SpectrumTap& tap, double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int got = tap.pull(incoming.data(), (int)incoming.size());
    for (int i = 0; i < got; ++i)
    {
        history[(size_t)historyPos] = incoming[(size_t)i];
        historyPos = (historyPos + 1) % size;
    }
    newSamples += got;

    if (newSamples < size / 4)
    {
        // Nothing new
        if (got == 0)
            for (auto& v : smoothedDb)
                v = juce::jmax(-100.0f, v - 1.5f);
        return got == 0;
    }
    newSamples = 0;

    for (int i = 0; i < size; ++i)
        fftData[(size_t)i] = history[(size_t)((historyPos + i) % size)];
    std::fill(fftData.begin() + size, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable(fftData.data(), (size_t)size);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    const float norm = 4.0f / (float)size; // sine amplitude A shows as ~A
    for (int b = 0; b < size / 2; ++b)
    {
        const float db = juce::Decibels::gainToDecibels(fftData[(size_t)b] * norm, -100.0f);
        auto& s = smoothedDb[(size_t)b];
        s = db > s ? db : s + 0.25f * (db - s); // fast rise, slow fall
    }
    return true;
}

float SpectrumAnalyzer::levelAt(float freq) const
{
    const float bin = freq * (float)size / (float)sr;
    const int i0 = juce::jlimit(1, size / 2 - 2, (int)bin);
    const float frac = juce::jlimit(0.0f, 1.0f, bin - (float)i0);
    // Average a few neighbouring bins at high frequencies so the line stays
    return smoothedDb[(size_t)i0] + frac * (smoothedDb[(size_t)i0 + 1] - smoothedDb[(size_t)i0]);
}

//==============================================================================
FrequencyGraph::FrequencyGraph(SpectrumAnalyzer& a)
    : analyzer(a)
{
}

juce::Rectangle<float> FrequencyGraph::plotArea() const
{
    return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedBottom(14.0f);
}

float FrequencyGraph::freqToX(float freq) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log(freq / minFreq) / std::log(maxFreq / minFreq);
}

float FrequencyGraph::xToFreq(float x) const
{
    const auto a = plotArea();
    return minFreq * std::pow(maxFreq / minFreq, juce::jlimit(0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

void FrequencyGraph::drawFrequencyGrid(juce::Graphics& g) const
{
    const auto a = plotArea();
    g.setFont(theme::font(10.0f));
    for (float f : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f})
    {
        const float x = freqToX(f);
        g.setColour(theme::outline.withAlpha(0.6f));
        g.drawVerticalLine((int)x, a.getY(), a.getBottom());
        g.setColour(theme::faintText);
        g.drawText(f >= 1000.0f ? juce::String((int)(f / 1000.0f)) + "k" : juce::String((int)f),
                   juce::Rectangle<float>(x - 20.0f, a.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
}

void FrequencyGraph::drawSpectrum(juce::Graphics& g) const
{
    const auto a = plotArea();
    constexpr float floorDb = -84.0f;
    juce::Path p;
    p.startNewSubPath(a.getX(), a.getBottom());
    for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
    {
        const float db = juce::jlimit(floorDb, 0.0f, analyzer.levelAt(xToFreq(x)) + 6.0f);
        p.lineTo(x, a.getBottom() - a.getHeight() * (db - floorDb) / -floorDb);
    }
    p.lineTo(a.getRight(), a.getBottom());
    p.closeSubPath();

    g.setGradientFill(juce::ColourGradient(theme::spectrum.withAlpha(0.45f), 0.0f, a.getY(), theme::spectrum.withAlpha(0.05f), 0.0f,
                                           a.getBottom(), false));
    g.fillPath(p);
    g.setColour(theme::spectrum.withAlpha(0.7f));
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

} // namespace stagemic
