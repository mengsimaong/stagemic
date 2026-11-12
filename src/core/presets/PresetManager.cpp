// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/presets/PresetManager.h"

#include "core/Parameters.h"
#include "core/StageMicProcessor.h"

#include <vector>

namespace stagemic
{

namespace
{
struct Setting
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    std::vector<Setting> settings; // anything not listed uses its default
};

// clang-format off
const std::vector<FactoryPreset> factoryPresets = {
    {"Clean Vocal", {}},

    {"Just Talking", {
        {pid::eqLowCut, 90.0f}, {pid::eqHighGain, 1.0f},
        {pid::compThreshold, -22.0f}, {pid::compRatio, 2.5f}, {pid::compMakeup, 5.0f},
        {pid::revOn, 0.0f},
    }},

    {"Karaoke Star", {
        {pid::eqTameRange, 4.0f}, {pid::eqHighGain, 3.0f}, {pid::eqMidFreq, 3000.0f}, {pid::eqMidGain, 1.5f},
        {pid::compRatio, 4.0f}, {pid::compThreshold, -22.0f}, {pid::compMakeup, 6.0f},
        {pid::satOn, 1.0f}, {pid::satDrive, 5.0f}, {pid::satMix, 25.0f},
        {pid::dlyOn, 1.0f}, {pid::dlyTime, 280.0f}, {pid::dlyFeedback, 25.0f}, {pid::dlyMix, 14.0f},
        {pid::revSize, 70.0f}, {pid::revMix, 26.0f},
    }},

    {"Bright Pop", {
        {pid::eqTameRange, 4.0f}, {pid::eqLowCut, 100.0f}, {pid::eqHighGain, 4.5f}, {pid::eqHighFreq, 10000.0f},
        {pid::dsThreshold, -32.0f}, {pid::dsRange, 10.0f},
        {pid::compRatio, 4.0f}, {pid::compAttack, 4.0f}, {pid::compMakeup, 6.0f},
        {pid::chOn, 1.0f}, {pid::chDepth, 25.0f}, {pid::chMix, 15.0f},
        {pid::revSize, 50.0f}, {pid::revMix, 18.0f},
    }},

    {"Warm Ballad", {
        {pid::eqLowGain, 2.0f}, {pid::eqLowFreq, 180.0f}, {pid::eqHighGain, 1.0f},
        {pid::compRatio, 3.0f}, {pid::compRelease, 200.0f},
        {pid::satOn, 1.0f}, {pid::satDrive, 6.0f}, {pid::satMix, 25.0f},
        {pid::dlyOn, 1.0f}, {pid::dlyTime, 450.0f}, {pid::dlyFeedback, 35.0f}, {pid::dlyTone, 3500.0f}, {pid::dlyMix, 12.0f},
        {pid::revSize, 85.0f}, {pid::revDamping, 55.0f}, {pid::revMix, 30.0f},
    }},

    {"Radio Voice", {
        {pid::eqLowCut, 280.0f}, {pid::eqMidFreq, 1800.0f}, {pid::eqMidGain, 7.0f}, {pid::eqMidQ, 1.2f},
        {pid::eqHighFreq, 5000.0f}, {pid::eqHighGain, -8.0f},
        {pid::compRatio, 8.0f}, {pid::compThreshold, -26.0f}, {pid::compMakeup, 8.0f},
        {pid::satOn, 1.0f}, {pid::satDrive, 14.0f}, {pid::satMix, 70.0f},
        {pid::revOn, 0.0f},
    }},

    // Tune styles. Chromatic works with any song; set the song's key for even cleaner notes.
    {"Hard Tune", {
        {pid::eqTameRange, 4.0f}, {pid::tuneOn, 1.0f}, {pid::tuneSpeed, 0.0f},
        {pid::eqLowCut, 110.0f}, {pid::eqHighGain, 4.0f}, {pid::eqMidFreq, 3000.0f}, {pid::eqMidGain, 1.0f},
        {pid::compRatio, 4.0f}, {pid::compThreshold, -24.0f}, {pid::compMakeup, 7.0f},
        {pid::dsThreshold, -32.0f},
        {pid::chOn, 1.0f}, {pid::chDepth, 20.0f}, {pid::chMix, 12.0f},
        {pid::dlyOn, 1.0f}, {pid::dlyTime, 230.0f}, {pid::dlyFeedback, 28.0f}, {pid::dlyMix, 12.0f},
        {pid::revSize, 60.0f}, {pid::revMix, 20.0f},
    }},

    {"Pop Tune", {
        {pid::eqTameRange, 4.0f}, {pid::tuneOn, 1.0f}, {pid::tuneSpeed, 12.0f},
        {pid::eqLowCut, 100.0f}, {pid::eqHighGain, 4.5f}, {pid::eqHighFreq, 10000.0f},
        {pid::dsThreshold, -32.0f}, {pid::dsRange, 10.0f},
        {pid::compRatio, 4.0f}, {pid::compAttack, 4.0f}, {pid::compMakeup, 6.0f},
        {pid::exOn, 1.0f},
        {pid::chOn, 1.0f}, {pid::chDepth, 25.0f}, {pid::chMix, 14.0f},
        {pid::revSize, 55.0f}, {pid::revMix, 20.0f},
    }},

    {"Melodic Tune", {
        {pid::eqTameRange, 4.0f}, {pid::tuneOn, 1.0f}, {pid::tuneSpeed, 35.0f},
        {pid::eqLowCut, 100.0f}, {pid::eqLowGain, 1.5f}, {pid::eqHighGain, 3.0f},
        {pid::compRatio, 4.0f}, {pid::compThreshold, -24.0f}, {pid::compMakeup, 7.0f},
        {pid::satOn, 1.0f}, {pid::satDrive, 6.0f}, {pid::satMix, 30.0f},
        {pid::dlyOn, 1.0f}, {pid::dlyTime, 330.0f}, {pid::dlyFeedback, 30.0f}, {pid::dlyTone, 4000.0f}, {pid::dlyMix, 15.0f},
        {pid::revSize, 75.0f}, {pid::revMix, 22.0f},
    }},
};
// clang-format on

const FactoryPreset* findFactory(const juce::String& name)
{
    for (const auto& p : factoryPresets)
        if (name == p.name)
            return &p;
    return nullptr;
}
} // namespace

PresetManager::PresetManager(StageMicProcessor& p, juce::File presetDirectory)
    : processor(p)
    , directory(std::move(presetDirectory))
{
}

juce::File PresetManager::defaultPresetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("StageMic").getChildFile("Presets");
}

//==============================================================================
juce::StringArray PresetManager::getFactoryPresetNames()
{
    juce::StringArray names;
    for (const auto& p : factoryPresets)
        names.add(p.name);
    return names;
}

bool PresetManager::loadFactoryPreset(const juce::String& name)
{
    const auto* preset = findFactory(name);
    if (preset == nullptr)
        return false;

    processor.resetToDefaults();

    auto& params = processor.getParameters();
    for (const auto& s : preset->settings)
        if (auto* p = params.getParameter(s.id))
            p->setValueNotifyingHost(p->convertTo0to1(s.value));

    processor.setCurrentPresetName(name);
    return true;
}

//==============================================================================
juce::Array<juce::File> PresetManager::getUserPresetFiles() const
{
    auto files = directory.findChildFiles(juce::File::findFiles, false, juce::String("*") + fileExtension);
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

juce::File PresetManager::fileForName(const juce::String& name) const
{
    auto safe = juce::File::createLegalFileName(name.trim());
    if (safe.isEmpty())
        safe = "Untitled";
    return directory.getChildFile(safe + fileExtension);
}

juce::File PresetManager::savePreset(const juce::String& name)
{
    const auto cleanName = name.trim().isEmpty() ? juce::String("Untitled") : name.trim();
    if (!directory.createDirectory())
        return {};

    const auto file = fileForName(cleanName);
    auto xml = processor.createPresetXml(cleanName);
    if (xml == nullptr || !xml->writeTo(file))
        return {};

    processor.setCurrentPresetName(cleanName);
    return file;
}

std::unique_ptr<juce::XmlElement> PresetManager::readPresetFile(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
        return nullptr;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(StageMicProcessor::presetTag))
        return nullptr;
    return xml;
}

bool PresetManager::loadPresetFile(const juce::File& file)
{
    auto xml = readPresetFile(file);
    return xml != nullptr && processor.applyPresetXml(*xml);
}

bool PresetManager::deletePreset(const juce::File& file)
{
    if (file.getParentDirectory() != directory || !file.hasFileExtension(fileExtension))
        return false; // only ever delete our own preset files
    return file.deleteFile();
}

bool PresetManager::exportPreset(const juce::File& destination)
{
    auto target = destination.hasFileExtension(fileExtension) ? destination : destination.withFileExtension(fileExtension);
    auto xml = processor.createPresetXml(processor.getCurrentPresetName());
    return xml != nullptr && xml->writeTo(target);
}

juce::File PresetManager::importPreset(const juce::File& source)
{
    auto xml = readPresetFile(source);
    if (xml == nullptr || !directory.createDirectory())
        return {};

    auto name = xml->getStringAttribute("name").trim();
    if (name.isEmpty())
        name = source.getFileNameWithoutExtension();

    // Never silently overwrite one of the user's presets: "Name (2)", "Name (3)", ...
    auto target = fileForName(name);
    for (int n = 2; target.exists() && target != source; ++n)
        target = fileForName(name + " (" + juce::String(n) + ")");

    if (target != source)
    {
        xml->setAttribute("name", target.getFileNameWithoutExtension());
        if (!xml->writeTo(target))
            return {};
    }

    return processor.applyPresetXml(*xml) ? target : juce::File();
}

} // namespace stagemic
