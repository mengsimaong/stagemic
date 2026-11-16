// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/StageMicProcessor.h"

#include "core/dsp/Safety.h"
#include "core/presets/PresetManager.h"
#include "ui/StageMicEditor.h"

#include <cmath>

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
    voiceAnalyzer.prepare(sampleRate);

    safetyEnvelope = 0.0f;
    beforeMix = 0.0f;
    beforeStep = (float)(1.0 / (0.015 * sampleRate)); // 15 ms fade
    sideEnergy[0] = sideEnergy[1] = 0.0f;
    oneSidedSamples = 0;
    mirrorFrom = -1;
    sideSmoothing = (float)std::exp(-(double)dsp::VocalChain::kSubBlock / (0.3 * sampleRate));
    safetyRelease = (float)std::exp(-1.0 / (0.15 * sampleRate)); // 150 ms
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
            return; // unity
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
    // REAL-TIME AUDIO THREAD: no allocation, no locks, no logging, no waiting
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

        dsp::scrubNonFinite(ptrs, numChannels, n);
        // Before / After
        const bool wantBefore = hearBefore.load(std::memory_order_relaxed);
        const bool mixBefore = wantBefore || beforeMix > 0.0f;
        if (mixBefore)
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::copy(dryCopy[c], ptrs[c], n);
        if (chain.anyModuleActive()) // never touch audio StageMic isn't processing
            fixOneSidedMic(ptrs, numChannels, n);
        voiceAnalyzer.push(ptrs, numChannels, n); // raw mic, before Input Gain
        applyGain(inGainSmooth, ptrs, numChannels, n);
        inPeak = juce::jmax(inPeak, peakOf(ptrs, numChannels, n));

        chain.process(ptrs, numChannels, n);

        applyGain(outGainSmooth, ptrs, numChannels, n);
        // Skipped when nothing is active, so bypass stays bit-exact
        const bool changesSound = chain.anyModuleActive() || inGainSmooth.isSmoothing() || outGainSmooth.isSmoothing() ||
                                  inGainSmooth.getCurrentValue() != 1.0f || outGainSmooth.getCurrentValue() != 1.0f;
        if (changesSound && !dsp::safetyLimit(ptrs, numChannels, n, safetyEnvelope, safetyRelease))
        {
            // Non-finite output: mute this block and reset the chain
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::clear(ptrs[c], n);
            chain.requestSnap();
        }
        if (mixBefore)
        {
            const float target = wantBefore ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                beforeMix = target > beforeMix ? juce::jmin(target, beforeMix + beforeStep) : juce::jmax(target, beforeMix - beforeStep);
                for (int c = 0; c < numChannels; ++c)
                    ptrs[c][i] += beforeMix * (dryCopy[c][i] - ptrs[c][i]);
            }
        }
        outPeak = juce::jmax(outPeak, peakOf(ptrs, numChannels, n));
        spectrum.push(ptrs, numChannels, n);
    }

    inputMeter.pushPeak(inPeak);
    outputMeter.pushPeak(outPeak);
}

void StageMicProcessor::fixOneSidedMic(float* const* ch, int numChannels, int n) noexcept
{
    if (numChannels != 2)
        return;
    for (int c = 0; c < 2; ++c)
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
            sum += ch[c][i] * ch[c][i];
        sideEnergy[c] = sideSmoothing * sideEnergy[c] + (1.0f - sideSmoothing) * sum / (float)n;
    }
    // One side has a voice and the other is near silent for a whole second
    const int loud = sideEnergy[0] >= sideEnergy[1] ? 0 : 1;
    const bool oneSided = sideEnergy[loud] > 1.0e-6f && sideEnergy[1 - loud] < 1.0e-8f && sideEnergy[1 - loud] < sideEnergy[loud] * 1.0e-4f;
    if (mirrorFrom >= 0)
    {
        if (sideEnergy[1 - mirrorFrom] > 1.0e-7f)
            mirrorFrom = -1, oneSidedSamples = 0;
    }
    else if (oneSided)
    {
        oneSidedSamples += n;
        if (oneSidedSamples > (int)preparedSampleRate.load())
            mirrorFrom = loud;
    }
    else
    {
        oneSidedSamples = 0;
    }
    if (mirrorFrom >= 0)
        juce::FloatVectorOperations::copy(ch[1 - mirrorFrom], ch[mirrorFrom], n);
}

juce::AudioProcessorEditor* StageMicProcessor::createEditor()
{
    return new StageMicEditor(*this);
}

//==============================================================================
// State = sound settings + which preset is selected
void StageMicProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state(stateTag);
    state.setProperty("version", stateVersion, nullptr);
    state.setProperty("preset", currentPresetName, nullptr);
    state.setProperty("order", dsp::VocalChain::orderToString(chain.getOrder()), nullptr);
    state.setProperty("voiceSetup", voiceSetupDone, nullptr);
    state.setProperty("voiceSetupOffered", voiceSetupOffered, nullptr);
    state.setProperty("advanced", advancedMode, nullptr);
    if (editorWidth > 0)
        state.setProperty("editorWidth", editorWidth, nullptr);
    state.appendChild(parameters.copyState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void StageMicProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateTag))
        return; // Unknown or corrupt data

    const auto state = juce::ValueTree::fromXml(*xml);
    const int version = state.getProperty("version", 0);

    // Version 1 had no sound settings
    const auto params = version >= 2 ? state.getChildWithName(paramsTag) : juce::ValueTree();

    // v2 setups on the old default order move to the new one
    auto order = state.getProperty("order").toString();
    if (version < 3 && order == dsp::VocalChain::legacyDefaultOrderString())
        order = {};
    applySoundTree(params, order);

    voiceSetupDone = (bool)state.getProperty("voiceSetup", false);
    voiceSetupOffered = (bool)state.getProperty("voiceSetupOffered", false);
    advancedMode = (bool)state.getProperty("advanced", false);
    editorWidth = (int)state.getProperty("editorWidth", 0);
    if (state.hasProperty("preset"))
        currentPresetName = state.getProperty("preset").toString();

    chain.requestSnap(); // restoring a saved session
}

//==============================================================================
void StageMicProcessor::applySoundTree(const juce::ValueTree& params, const juce::String& order)
{
    // Start from defaults
    juce::ValueTree fresh(paramsTag);
    if (params.isValid())
        fresh.copyPropertiesAndChildrenFrom(params, nullptr);
    // A damaged or hand-edited file may hold values that aren't numbers
    for (int i = fresh.getNumChildren(); --i >= 0;)
    {
        const auto child = fresh.getChild(i);
        if (child.hasProperty("value") && !std::isfinite((double)child.getProperty("value")))
            fresh.removeChild(i, nullptr);
    }
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
    const auto mic = captureMicSettings();
    applySoundTree(preset.getChildWithName(paramsTag), preset.getProperty("order").toString());
    restoreMicSettings(mic);
    currentPresetName = preset.getProperty("name", "Untitled").toString();
    return true;
}

void StageMicProcessor::resetToDefaults()
{
    const auto mic = captureMicSettings();
    applySoundTree({}, {});
    restoreMicSettings(mic);
}

std::array<float, 4> StageMicProcessor::captureMicSettings()
{
    std::array<float, 4> values{};
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = parameters.getRawParameterValue(micSettings[i])->load();
    return values;
}

void StageMicProcessor::restoreMicSettings(const std::array<float, 4>& values)
{
    if (!voiceSetupDone)
        return;
    for (size_t i = 0; i < values.size(); ++i)
        setParameterValue(micSettings[i], values[i]);
}

void StageMicProcessor::setParameterValue(const char* id, float value)
{
    if (auto* p = parameters.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void StageMicProcessor::applyVoiceSetup(const VoiceAnalyzer::Result& r)
{
    if (!r.heardVoice)
        return;
    setParameterValue(pid::inGain, r.inputGainDb);
    setParameterValue(pid::gateThreshold, r.gateThresholdDb);
    setParameterValue(pid::gateRatio, r.gateRatio);
    if (r.sibilantHz > 0.0f)
        setParameterValue(pid::dsFreq, r.sibilantHz);
    voiceSetupDone = true;
}

} // namespace stagemic
