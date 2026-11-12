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

/** One StageMic filter as seen by a host that delivers planar float audio (OBS).

    Contains no OBS code, so it can be tested without OBS.

    Threading contract:
      - process()                    : audio thread only. Never allocates or locks.
      - getState()/setState()        : any non-audio thread.
      - showEditor()/closeEditor()   : JUCE message thread only.
      - destructor                   : JUCE message thread (closes the editor window).
*/
class FilterInstance
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        int numHostChannels = 2;  // channels OBS hands us (1-8)
        int maxBlockSize = 1024;  // OBS's AUDIO_OUTPUT_FRAMES
        juce::String hostDescription;
    };

    explicit FilterInstance(const Config& config);
    ~FilterInstance();

    //==========================================================================
    /** Processes audio in place.
        Only the first one or two channels are processed (mono/stereo mic chain);
        any extra surround channels are left untouched. Blocks larger than
        maxBlockSize are split into chunks. */
    void process(float* const* channels, int numChannels, int numFrames) noexcept;

    //==========================================================================
    /** Whole-filter state as a base64 string, suitable for OBS settings. */
    std::string getState();
    /** Restores state from getState(). Returns false (and changes nothing) for bad data. */
    bool setState(const std::string& base64);

    //==========================================================================
    /** Opens (or brings to front) the editor window.
        onWindowCreated receives the native window handle so the host can make
        the window "owned" by its main window. */
    void showEditor(const juce::String& title, const std::function<void(void* nativeHandle)>& onWindowCreated = {});
    void closeEditor();
    bool isEditorVisible() const;

    StageMicProcessor& getProcessor() noexcept { return *processor; }
    int getProcessedChannelCount() const noexcept { return processedChannels; }
    int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    std::unique_ptr<StageMicProcessor> processor;
    std::unique_ptr<EditorWindow> window;

    // Pre-sized so process() never allocates.
    static constexpr int maxProcessedChannels = 2;
    std::array<float*, maxProcessedChannels> chunkPointers{};
    juce::AudioBuffer<float> hostView;
    juce::MidiBuffer midi;

    int processedChannels = 2;
    int maxBlockSize = 1024;

    JUCE_DECLARE_NON_COPYABLE(FilterInstance)
};

} // namespace stagemic
