// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/StageMicProcessor.h"

#include "core/presets/PresetManager.h"
#include "ui/StageMicEditor.h"

#ifndef STAGEMIC_VERSION
  #define STAGEMIC_VERSION "dev"
#endif

namespace stagemic
{

namespace
{
constexpr const char* paramsTag = "PARAMS";
}

StageMicProcessor::StageMicProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, paramsTag, createParameterLayout())
    , chain(parameters)
    , presetManager(std::make_unique<PresetManager>(*this))
{
    inGain = parameters.getRawParameterValue(pid::inGain);
    outGain = parameters.getRawParameterValue(pid::outGain);
}

StageMicProcessor::~StageMicProcessor() = default;

bool StageMicProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void StageMicProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = maximumExpectedSamplesPerBlock;

    chain.prepare(sampleRate, juce::jmax(1, getTotalNumInputChannels()));

    inGainSmooth.reset(sampleRate, 0.02);
    outGainSmooth.reset(sampleRate, 0.02);
    inGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inGain->load()));
    outGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outGain->load()));
}

float StageMicProcessor::peakOf(float* const* ch, int numChannels, int n) noexcept
{
    float peak = 0.0f;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            peak = juce::jmax(peak, std::abs(ch[c][i]));
    return peak;
}

namespace
{
void applyGain(juce::SmoothedValue<float>& gain, float* const* ch, int numChannels, int n) noexcept
{
    if (!gain.isSmoothing())
    {
        const float g = gain.getCurrentValue();
        if (g == 1.0f)
            return; // unity: leave the audio bit-exact
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::multiply(ch[c], g, n);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNextValue();
        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= g;
    }
}
} // namespace

void StageMicProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // REAL-TIME AUDIO THREAD: no allocation, no locks, no logging, no waiting.
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numChannels = juce::jmin(buffer.getNumChannels(), dsp::kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    inGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(inGain->load(std::memory_order_relaxed)));
    outGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(outGain->load(std::memory_order_relaxed)));

    float inPeak = 0.0f, outPeak = 0.0f;
    float* ptrs[dsp::kMaxChannels] = {};

    for (int offset = 0; offset < numSamples; offset += dsp::VocalChain::kSubBlock)
    {
        const int n = juce::jmin(dsp::VocalChain::kSubBlock, numSamples - offset);
        for (int c = 0; c < numChannels; ++c)
            ptrs[c] = buffer.getWritePointer(c, offset);

        applyGain(inGainSmooth, ptrs, numChannels, n);
        inPeak = juce::jmax(inPeak, peakOf(ptrs, numChannels, n));

        chain.process(ptrs, numChannels, n);

        applyGain(outGainSmooth, ptrs, numChannels, n);
        outPeak = juce::jmax(outPeak, peakOf(ptrs, numChannels, n));
        spectrum.push(ptrs, numChannels, n);
    }

    inputMeter.pushPeak(inPeak);
    outputMeter.pushPeak(outPeak);
}

juce::AudioProcessorEditor* StageMicProcessor::createEditor()
{
    return new StageMicEditor(*this);
}

//==============================================================================
// State = sound settings + which preset is selected. Stored by OBS in the scene
// collection (and by DAWs in their project).
void StageMicProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state(stateTag);
    state.setProperty("version", stateVersion, nullptr);
    state.setProperty("preset", currentPresetName, nullptr);
    state.setProperty("order", dsp::VocalChain::orderToString(chain.getOrder()), nullptr);
    state.appendChild(parameters.copyState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void StageMicProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateTag))
        return; // Unknown or corrupt data: keep current settings rather than crash.

    const auto state = juce::ValueTree::fromXml(*xml);
    const int version = state.getProperty("version", 0);

    // Version 1 (Phase 1) had no sound settings: it simply loads as the defaults.
    const auto params = version >= 2 ? state.getChildWithName(paramsTag) : juce::ValueTree();
    applySoundTree(params, state.getProperty("order").toString());

    if (state.hasProperty("preset"))
        currentPresetName = state.getProperty("preset").toString();

    chain.requestSnap(); // restoring a saved session: no fades, start exactly as saved
}

//==============================================================================
void StageMicProcessor::applySoundTree(const juce::ValueTree& params, const juce::String& order)
{
    // Start from defaults so anything the saved data doesn't mention is predictable.
    juce::ValueTree fresh(paramsTag);
    if (params.isValid())
        fresh.copyPropertiesAndChildrenFrom(params, nullptr);
    parameters.replaceState(fresh);

    const auto parsed = dsp::VocalChain::orderFromString(order);
    chain.setOrder(parsed.has_value() ? *parsed : dsp::VocalChain::defaultOrder());
}

std::unique_ptr<juce::XmlElement> StageMicProcessor::createPresetXml(const juce::String& presetName)
{
    juce::ValueTree preset(presetTag);
    preset.setProperty("name", presetName, nullptr);
    preset.setProperty("formatVersion", presetFormatVersion, nullptr);
    preset.setProperty("createdWith", juce::String("StageMic " STAGEMIC_VERSION), nullptr);
    preset.setProperty("order", dsp::VocalChain::orderToString(chain.getOrder()), nullptr);
    preset.appendChild(parameters.copyState(), nullptr);
    return preset.createXml();
}

bool StageMicProcessor::applyPresetXml(const juce::XmlElement& xml)
{
    if (!xml.hasTagName(presetTag))
        return false;

    const auto preset = juce::ValueTree::fromXml(xml);
    applySoundTree(preset.getChildWithName(paramsTag), preset.getProperty("order").toString());
    currentPresetName = preset.getProperty("name", "Untitled").toString();
    return true;
}

void StageMicProcessor::resetToDefaults()
{
    applySoundTree({}, {});
}

} // namespace stagemic
