// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/StageMicProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stagemic
{

/** Header: logo, preset browser (< name >), Save, and a menu for
    Save As / Import / Export / Delete / open the preset folder. */
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar(StageMicProcessor&);

    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

    /** Called after any preset is loaded (the module order may have changed). */
    std::function<void()> onPresetLoaded;
    std::function<void()> onSetUpVoice;
    /** Simple / Advanced switch. */
    std::function<void(bool advanced)> onModeChanged;
    void setMode(bool advanced);

private:
    struct Entry
    {
        juce::String name;
        juce::File file; // empty for factory presets
    };

    void choose(int index);
    void step(int delta);
    void save();
    void saveAs();
    void confirmOverwrite(const juce::String& name);
    void importPreset();
    void exportPreset();
    void deleteCurrent();
    void showMenu();
    void showStatus(const juce::String&, bool error = false);
    int currentIndex() const;

    StageMicProcessor& processor;
    juce::Array<Entry> entries;
    juce::ComboBox box;
    juce::TextButton prev{"<"}, next{">"}, saveButton{"Save"}, menuButton{"..."}, voiceButton{"Set up my voice"};
    juce::TextButton simpleButton{"SIMPLE"}, advancedButton{"ADVANCED"};
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    int statusTicks = 0;

    class StatusTimer : public juce::Timer
    {
    public:
        explicit StatusTimer(PresetBar& b) : bar(b) {}
        void timerCallback() override;
        PresetBar& bar;
    } statusTimer{*this};
};

} // namespace stagemic
