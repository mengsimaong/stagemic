// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/LevelMeter.h"
#include "core/Parameters.h"
#include "core/SpectrumTap.h"
#include "core/VoiceAnalyzer.h"
#include "core/dsp/VocalChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

class PresetManager;

/** The StageMic audio engine.

    A standard JUCE AudioProcessor with no knowledge of OBS, so the exact same
    engine runs inside OBS (via FilterInstance) and as a VST3.

    Signal flow: input gain -> built-in chain (VocalChain) -> output gain.
    Everything is zero-latency. */
class StageMicProcessor final : public juce::AudioProcessor
{
public:
    StageMicProcessor();
    ~StageMicProcessor() override;

    //==========================================================================
    // juce::AudioProcessor
    const juce::String getName() const override { return "Paopoa StageMic"; }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    // StageMic-specific
    static constexpr int stateVersion = 3;
    static constexpr const char* stateTag = "StageMicState";
    static constexpr const char* presetTag = "StageMicPreset";
    static constexpr int presetFormatVersion = 1;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    dsp::VocalChain& getChain() noexcept { return chain; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

    /** Sound settings only (parameters + module order) as a preset document. */
    std::unique_ptr<juce::XmlElement> createPresetXml(const juce::String& presetName);
    /** Applies a preset document. Missing settings fall back to defaults. Returns false for non-presets. */
    bool applyPresetXml(const juce::XmlElement& xml);

    /** Sets every setting back to its default (the "Clean Vocal" sound). */
    void resetToDefaults();

    /** Name of the last loaded/saved preset. Message thread only. */
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void setCurrentPresetName(const juce::String& name) { currentPresetName = name; }

    SpectrumTap& getSpectrumTap() noexcept { return spectrum; }

    /** "Set up my voice". Once applied, the mic settings it chose (input gain, gate
        threshold, de-esser frequency) stay as they are when presets change. */
    VoiceAnalyzer& getVoiceAnalyzer() noexcept { return voiceAnalyzer; }
    void applyVoiceSetup(const VoiceAnalyzer::Result&);
    bool hasVoiceSetup() const noexcept { return voiceSetupDone; }

    /** Window layout: Simple (default) or Advanced. Saved with the OBS scene. Message thread. */
    bool isAdvancedMode() const noexcept { return advancedMode; }
    void setAdvancedMode(bool on) { advancedMode = on; }
    /** The window width the user last chose (0 = never resized), saved with the settings. */
    int getEditorWidth() const noexcept { return editorWidth; }
    void setEditorWidth(int width) noexcept { editorWidth = width; }
    void clearVoiceSetup() { voiceSetupDone = false; }
    /** "Set up my voice" opens by itself the first time the window opens (once). */
    bool wasVoiceSetupOffered() const noexcept { return voiceSetupOffered; }
    void setVoiceSetupOffered() noexcept { voiceSetupOffered = true; }
    /** The settings kept across presets once the voice is set up. */
    static constexpr const char* micSettings[] = {pid::inGain, pid::gateThreshold, pid::gateRatio, pid::dsFreq};
    LevelMeter& getInputMeter() noexcept { return inputMeter; }
    LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Free text shown in the editor, e.g. "OBS Studio 31.1.1". Set once, before the editor opens. */
    void setHostDescription(const juce::String& text) { hostDescription = text; }
    const juce::String& getHostDescription() const noexcept { return hostDescription; }

    double getPreparedSampleRate() const noexcept { return preparedSampleRate.load(); }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize.load(); }

private:
    void applySoundTree(const juce::ValueTree& params, const juce::String& order);
    std::array<float, 4> captureMicSettings();
    void restoreMicSettings(const std::array<float, 4>&);
    void setParameterValue(const char* id, float value);
    static float peakOf(float* const* ch, int numChannels, int n) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    dsp::VocalChain chain;
    std::unique_ptr<PresetManager> presetManager;

    std::atomic<float>* inGain = nullptr;
    std::atomic<float>* outGain = nullptr;
    juce::SmoothedValue<float> inGainSmooth, outGainSmooth;
    float safetyEnvelope = 0.0f, safetyRelease = 0.9999f; // the last-line safety limiter
    // One-sided mic fix: many audio interfaces put the mic on the left channel only.
    void fixOneSidedMic(float* const* ch, int numChannels, int n) noexcept;
    float sideEnergy[2] = {}, sideSmoothing = 0.99f;
    int oneSidedSamples = 0, mirrorFrom = -1;
public:
    /** Which channel is being copied to the other (-1 = none), for the tests and the UI. */
    int getMirroredChannel() const noexcept { return mirrorFrom; }
private:

    LevelMeter inputMeter;
    LevelMeter outputMeter;
    SpectrumTap spectrum;
    VoiceAnalyzer voiceAnalyzer;
    bool voiceSetupDone = false;
    bool voiceSetupOffered = false;
    bool advancedMode = false;
    int editorWidth = 0;
    juce::String hostDescription{"Unknown host"};
    juce::String currentPresetName{"Clean Vocal"};
    std::atomic<double> preparedSampleRate{0.0};
    std::atomic<int> preparedBlockSize{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageMicProcessor)
};

} // namespace stagemic
