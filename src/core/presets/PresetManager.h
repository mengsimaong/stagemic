// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

class StageMicProcessor;

// Factory presets plus the user's own presets as files
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".stagemicpreset";

    explicit PresetManager(StageMicProcessor& processor, juce::File presetDirectory = defaultPresetDirectory());

    static juce::File defaultPresetDirectory();
    const juce::File& getPresetDirectory() const noexcept { return directory; }

    static juce::StringArray getFactoryPresetNames();
    // "Vocal", "Tune" or "Fun voices"
    static juce::String getFactoryPresetCategory(const juce::String& name);
    bool loadFactoryPreset(const juce::String& name);

    // User presets
    // Sorted by name
    juce::Array<juce::File> getUserPresetFiles() const;
    juce::File fileForName(const juce::String& name) const;

    // Saves the current sound under this name
    juce::File savePreset(const juce::String& name);

    bool loadPresetFile(const juce::File& file);
    bool deletePreset(const juce::File& file);

    // Writes the current sound to any location
    bool exportPreset(const juce::File& destination);

    // Copies a preset file into the preset folder and loads it
    juce::File importPreset(const juce::File& source);

    static std::unique_ptr<juce::XmlElement> readPresetFile(const juce::File& file);

private:
    StageMicProcessor& processor;
    juce::File directory;
};

} // namespace stagemic
