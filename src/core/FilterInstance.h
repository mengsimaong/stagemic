// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace stagemic
{

class EditorWindow;

// One StageMic filter as seen by a host that delivers planar float audio
class FilterInstance
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        int numHostChannels = 2; // channels OBS hands us
        int maxBlockSize = 1024; // OBS's AUDIO_OUTPUT_FRAMES
        juce::String hostDescription;
    };

    explicit FilterInstance(const Config& config);
    ~FilterInstance();

    //==========================================================================
    // Processes audio in place
    void process(float* const* channels, int numChannels, int numFrames) noexcept;

    //==========================================================================
    // Whole-filter state as a base64 string, suitable for OBS settings
    std::string getState();
    // Restores state from getState
    bool setState(const std::string& base64);

    //==========================================================================
    void showEditor(const juce::String& title, const std::function<void(void* nativeHandle)>& onWindowCreated = {});
    void closeEditor();
    bool isEditorVisible() const;

    StageMicProcessor& getProcessor() noexcept { return *processor; }
    int getProcessedChannelCount() const noexcept { return processedChannels; }
    int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    std::unique_ptr<StageMicProcessor> processor;
    std::unique_ptr<EditorWindow> window;

    // Pre-sized so process never allocates
    static constexpr int maxProcessedChannels = 2;
    std::array<float*, maxProcessedChannels> chunkPointers{};
    juce::AudioBuffer<float> hostView;
    juce::MidiBuffer midi;

    int processedChannels = 2;
    int maxBlockSize = 1024;

    JUCE_DECLARE_NON_COPYABLE(FilterInstance)
};

} // namespace stagemic
