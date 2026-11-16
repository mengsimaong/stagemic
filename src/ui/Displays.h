// SPDX-License-Identifier: AGPL-3.0-or-later
// Top half of the StageMic window
#pragma once

#include "core/SpectrumTap.h"
#include "core/dsp/Tune.h"
#include "core/dsp/VocalChain.h"
#include "ui/Widgets.h"

#include <juce_dsp/juce_dsp.h>

namespace stagemic
{

// FFT analyzer fed from SpectrumTap
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();

    // Pulls new audio and updates the smoothed spectrum
    bool update(SpectrumTap& tap, double sampleRate);

    // Smoothed level in dB at a frequency
    float levelAt(float freq) const;

private:
    static constexpr int order = 11;
    static constexpr int size = 1 << order;

    juce::dsp::FFT fft{order};
    juce::dsp::WindowingFunction<float> window{(size_t)size, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> history = std::vector<float>((size_t)size, 0.0f);
    std::vector<float> incoming = std::vector<float>((size_t)SpectrumTap::capacity, 0.0f);
    std::vector<float> fftData = std::vector<float>((size_t)size * 2, 0.0f);
    std::vector<float> smoothedDb = std::vector<float>((size_t)size / 2, -100.0f);
    int historyPos = 0;
    int newSamples = 0;
    double sr = 48000.0;
};

// Shared frequency axis and grid drawing
class FrequencyGraph
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit FrequencyGraph(SpectrumAnalyzer& analyzer);

protected:
    juce::Rectangle<float> plotArea() const;
    float freqToX(float freq) const;
    float xToFreq(float x) const;
    void drawFrequencyGrid(juce::Graphics& g) const;
    void drawSpectrum(juce::Graphics& g) const;

    SpectrumAnalyzer& analyzer;
};

// EQ curve with seven draggable points over the spectrum
class EqDisplay final : public FrequencyGraph
{
public:
    EqDisplay(APVTS& state, SpectrumAnalyzer& analyzer, std::function<double()> sampleRate);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Where point `index` is drawn, for the tests
    juce::Point<float> getNodePosition(int index) const { return nodePosition(index); }
    static constexpr int numNodes = 7;
    // The selected point
    void setSelected(int index);
    int getSelected() const noexcept { return lastUsed; }
    std::function<void(int)> onSelect; // called when a point is grabbed on the graph
    // The gain the drawn curve shows at `freq`, for the tests
    float getDrawnResponseDb(float freq) const { return responseDb(freq); }

private:
    struct Node
    {
        const char* name;
        const char* freqId;
        const char* gainId; // nullptr = horizontal only
        const char* qId;    // nullptr = no width control
        bool tame = false;  // harshness tamer
    };

    float gainToY(float db) const;
    float yToGain(float y) const;
    float value(const char* id) const;
    void set(const char* id, float v);
    juce::Point<float> nodePosition(int index) const;
    int nodeAt(juce::Point<float> p) const;
    float responseDb(float freq) const;
    bool pointIsOn(int index) const;
    void beginGesture(int node);
    void endGesture(int node);

    APVTS& state;
    std::function<double()> sampleRate;
    std::array<Node, numNodes> nodes;
    int hovered = -1, dragging = -1;
    int lastUsed = -1;      // the point dragged last
    float tameCutDb = 0.0f; // live

public:
    // Live cut of the harshness tamer, shown as a dip in the curve
    void setTameReduction(float db)
    {
        if (std::abs(db - tameCutDb) > 0.05f)
        {
            tameCutDb = db;
            repaint();
        }
    }

private:
    static constexpr float rangeDb = 18.0f;
};

class MultibandDisplay final : public FrequencyGraph
{
public:
    MultibandDisplay(APVTS& state, SpectrumAnalyzer& analyzer);

    void setReductions(const float* db3);
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    enum Target
    {
        none = -1,
        lowSplit,
        highSplit,
        lowThreshold,
        midThreshold,
        highThreshold
    };

    juce::Rectangle<float> graphArea() const;
    float dbToY(float db) const;
    float yToDb(float y) const;
    float bandLeft(int band) const;
    float bandRight(int band) const;
    Target targetAt(juce::Point<float> p) const;
    const char* paramFor(Target t) const;

    APVTS& state;
    float reductionDb[3]{};
    Target hovered = none, dragging = none;
};

class FieldRow final : public juce::Component
{
public:
    struct Group
    {
        juce::String badge; // number shown in a circle, matching the graph
        juce::String title;
        std::vector<std::pair<const char*, juce::String>> fields; // param id, label
    };

    FieldRow(APVTS& state, std::vector<Group> groups);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::vector<Group> groups;
    std::vector<std::unique_ptr<juce::Component>> fields;
    std::vector<juce::Rectangle<int>> groupAreas;
};

class EqBandBar final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit EqBandBar(APVTS& state);
    void select(int index);
    int getSelected() const noexcept { return selected; }
    // Call regularly
    void refresh();
    std::function<void(int)> onSelect; // called when a chip is clicked

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct Band
    {
        juce::String name;
        const char* freqId;
        const char* gainId; // nullptr = none
        bool tame;
    };
    juce::String readOut(int index, int line) const;
    int chipAt(juce::Point<int>) const;

    APVTS& state;
    std::vector<Band> bands;
    std::vector<std::unique_ptr<FieldRow>> rows;
    std::vector<juce::Rectangle<int>> chips;
    PowerButton pointPower[7];
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> pointAttachments;
    std::array<float, 7> shownFreq{}, shownGain{}; // what the chips last drew
    int selected = 3, hover = -1;
};

class ModuleTab final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    ModuleTab(APVTS& state, const char* enableParam, juce::String title, std::function<void()> onSelect);
    void setSelected(bool);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override
    {
        hover = true;
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover = false;
        repaint();
    }

    // Draws the darker strip behind the tabs
    static void paintTrack(juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> content);

private:
    juce::String title;
    std::function<void()> onSelect;
    PowerButton power;
    APVTS::ButtonAttachment attachment;
    bool selected = false, hover = false;
};

// EQ / Multiband display with tabs
class TuneDisplay final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    TuneDisplay(APVTS& state, const dsp::Tune& tune);
    void tick();
    // Light drawing for Simple mode
    void setLight(bool shouldBeLight) noexcept { light = shouldBeLight; }
    void paint(juce::Graphics&) override;
    void resized() override;

    static juce::String noteName(int midi);

private:
    struct Style
    {
        const char* name;
        float speedMs;
        const char* tip;
    };
    static constexpr int kShown = 640; // about 3.4 s
    APVTS& state;
    const dsp::Tune& tune;
    std::array<float, kShown> input{}, output{};
    int lastWritten = -1;
    float centre = 64.0f;
    bool light = false;
    int lightSkip = 0;
    juce::OwnedArray<juce::TextButton> styleButtons;
    std::vector<Style> styles;
    juce::Rectangle<float> plot;
};

class DisplayPanel final : public juce::Component
{
public:
    static constexpr int fieldRowHeight = 96;
    DisplayPanel(APVTS& state, SpectrumTap& tap, std::function<double()> sampleRate, const dsp::Tune& tune);
    ~DisplayPanel() override;

    void tick(const dsp::VocalChain::Activity&);
    // 0 = EQ, 1 = Multiband, 2 = Tune
    void select(int tab);
    // On screen or not
    void setLive(bool);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SpectrumTap& tap;
    std::function<double()> sampleRate;
    SpectrumAnalyzer analyzer;
    ModuleTab eqTab, mbTab, tuneTab;
    EqDisplay eq;
    MultibandDisplay multiband;
    TuneDisplay tuneDisplay;
    EqBandBar eqFields;
    FieldRow mbFields, tuneFields;
    int current = 0;
    bool live = true;
};

} // namespace stagemic
