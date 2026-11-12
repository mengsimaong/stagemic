// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

class StageMicProcessor;

/** Factory presets plus the user's own presets as files.

    User presets live in Documents/StageMic/Presets as *.stagemicpreset (XML),
    so they're easy to find, back up and share. Message thread only. */
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".stagemicpreset";

    explicit PresetManager(StageMicProcessor& processor, juce::File presetDirectory = defaultPresetDirectory());

    static juce::File defaultPresetDirectory();
    const juce::File& getPresetDirectory() const noexcept { return directory; }

    // Factory presets ---------------------------------------------------------
    static juce::StringArray getFactoryPresetNames();
    /** "Vocal", "Tune" or "Fun voices" (for the preset menu's sections). */
    static juce::String getFactoryPresetCategory(const juce::String& name);
    bool loadFactoryPreset(const juce::String& name);

    // User presets -------------------------------------------------------------
    /** Sorted by name. */
    juce::Array<juce::File> getUserPresetFiles() const;
    juce::File fileForName(const juce::String& name) const;

    /** Saves the current sound under this name (overwrites a preset with the same name).
        Returns the file, or an empty File on failure. */
    juce::File savePreset(const juce::String& name);

    bool loadPresetFile(const juce::File& file);
    bool deletePreset(const juce::File& file);

    /** Writes the current sound to any location (for sharing). */
    bool exportPreset(const juce::File& destination);

    /** Copies a preset file into the preset folder and loads it.
        Returns the new file, or an empty File if it isn't a valid StageMic preset. */
    juce::File importPreset(const juce::File& source);

    /** Reads a preset file without applying it; nullptr if it isn't a valid preset. */
    static std::unique_ptr<juce::XmlElement> readPresetFile(const juce::File& file);

private:
    StageMicProcessor& processor;
    juce::File directory;
};

} // namespace stagemic
