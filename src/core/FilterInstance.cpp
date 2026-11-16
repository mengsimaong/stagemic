// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/FilterInstance.h"

#include "ui/EditorWindow.h"

namespace stagemic
{

FilterInstance::FilterInstance(const Config& config)
    : processor(std::make_unique<StageMicProcessor>())
    , processedChannels(juce::jlimit(1, maxProcessedChannels, config.numHostChannels))
    , maxBlockSize(juce::jmax(1, config.maxBlockSize))
{
    processor->setHostDescription(config.hostDescription);

    const auto set = processedChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    processor->setBusesLayout(layout);

    processor->setRateAndBufferSizeDetails(config.sampleRate, maxBlockSize);
    processor->prepareToPlay(config.sampleRate, maxBlockSize);

    // Reserve MIDI storage up front so clear/processBlock never allocate
    midi.ensureSize(256);
}

FilterInstance::~FilterInstance()
{
    // The editor must go before the processor it points to
    window.reset();
    processor->releaseResources();
}

void FilterInstance::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (channels == nullptr || numFrames <= 0)
        return;

    int count = juce::jmin(numChannels, processedChannels);
    for (int ch = 0; ch < count; ++ch)
    {
        if (channels[ch] == nullptr)
        {
            count = ch;
            break;
        }
    }

    if (count <= 0)
        return;

    for (int offset = 0; offset < numFrames; offset += maxBlockSize)
    {
        const int n = juce::jmin(maxBlockSize, numFrames - offset);

        for (int ch = 0; ch < count; ++ch)
            chunkPointers[(size_t)ch] = channels[ch] + offset;

        // Points the buffer at OBS's memory
        hostView.setDataToReferTo(chunkPointers.data(), count, n);
        midi.clear();
        processor->processBlock(hostView, midi);
    }
}

std::string FilterInstance::getState()
{
    juce::MemoryBlock block;
    processor->getStateInformation(block);
    return block.toBase64Encoding().toStdString();
}

bool FilterInstance::setState(const std::string& base64)
{
    if (base64.empty())
        return false;

    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(juce::String(base64)))
        return false;

    // Validate before handing to the processor so callers learn about bad data
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int)block.getSize());
    if (xml == nullptr || !xml->hasTagName(StageMicProcessor::stateTag))
        return false;

    processor->setStateInformation(block.getData(), (int)block.getSize());
    return true;
}

void FilterInstance::showEditor(const juce::String& title, const std::function<void(void*)>& onWindowCreated)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (window == nullptr)
    {
        window = std::make_unique<EditorWindow>(title, *processor);
        if (onWindowCreated)
            if (auto* peer = window->getPeer())
                onWindowCreated(peer->getNativeHandle());
    }
    else
    {
        window->setName(title);
    }

    window->setVisible(true);
    window->toFront(true);
}

void FilterInstance::closeEditor()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (window != nullptr)
        window->setVisible(false);
}

bool FilterInstance::isEditorVisible() const
{
    return window != nullptr && window->isVisible();
}

} // namespace stagemic
