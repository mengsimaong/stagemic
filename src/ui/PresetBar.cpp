// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PresetBar.h"
#include "ui/NeonCat.h"

#include "core/presets/PresetManager.h"
#include "ui/Theme.h"

namespace stagemic
{

PresetBar::PresetBar(StageMicProcessor& p)
    : processor(p)
{
    box.setTooltip("Choose a preset");
    box.onChange = [this] { choose(box.getSelectedItemIndex()); };
    prev.setTooltip("Previous preset");
    next.setTooltip("Next preset");
    saveButton.setTooltip("Save changes (factory presets are saved as a copy)");
    menuButton.setTooltip("Save As, Import, Export, Delete");
    prev.onClick = [this] { step(-1); };
    next.onClick = [this] { step(+1); };
    saveButton.onClick = [this] { save(); };
    menuButton.onClick = [this] { showMenu(); };
    voiceButton.setTooltip("Measure your mic and voice for about 11 seconds, then set Input Gain, Gate and De-Ess to match");
    for (auto* b : {&simpleButton, &advancedButton})
    {
        b->setClickingTogglesState(false);
        b->getProperties().set("bold", true);
        b->setConnectedEdges(b == &simpleButton ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        addAndMakeVisible(b);
    }
    simpleButton.setTooltip("Simple: just the controls that matter most");
    advancedButton.setTooltip("Advanced: every module, the EQ graph, the signal chain and all settings");
    simpleButton.onClick = [this] { setMode(false); if (onModeChanged) onModeChanged(false); };
    advancedButton.onClick = [this] { setMode(true); if (onModeChanged) onModeChanged(true); };
    voiceButton.getProperties().set("primary", true);
    voiceButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    voiceButton.onClick = [this] {
        if (onSetUpVoice)
            onSetUpVoice();
    };

    status.setFont(theme::font(12.0f));
    status.setJustificationType(juce::Justification::centredRight);

    for (auto* c : std::initializer_list<juce::Component*>{&box, &prev, &next, &saveButton, &menuButton, &voiceButton, &status})
        addAndMakeVisible(c);

    refresh();
}

void PresetBar::refresh()
{
    entries.clear();
    for (const auto& name : PresetManager::getFactoryPresetNames())
        entries.add({name, {}});
    for (const auto& file : processor.getPresetManager().getUserPresetFiles())
        entries.add({file.getFileNameWithoutExtension(), file});

    box.clear(juce::dontSendNotification);
    int i = 0;
    bool userHeading = false;
    juce::String category;
    for (const auto& e : entries)
    {
        if (e.file == juce::File())
        {
            const auto c = PresetManager::getFactoryPresetCategory(e.name);
            if (c != category)
            {
                if (category.isNotEmpty())
                    box.addSeparator();
                box.addSectionHeading(c);
                category = c;
            }
        }
        if (e.file != juce::File() && !userHeading)
        {
            box.addSeparator();
            box.addSectionHeading("My presets");
            userHeading = true;
        }
        box.addItem(e.name, ++i);
    }

    const int index = currentIndex();
    if (index >= 0)
        box.setSelectedItemIndex(index, juce::dontSendNotification);
    else
        box.setText(processor.getCurrentPresetName(), juce::dontSendNotification);
}

int PresetBar::currentIndex() const
{
    // Prefer the user's own preset if a factory preset has the same name.
    const auto name = processor.getCurrentPresetName();
    for (int i = entries.size(); --i >= 0;)
        if (entries[i].name == name)
            return i;
    return -1;
}

void PresetBar::choose(int index)
{
    if (!juce::isPositiveAndBelow(index, entries.size()))
        return;

    const auto e = entries[index];
    auto& presets = processor.getPresetManager();
    const bool ok = e.file == juce::File() ? presets.loadFactoryPreset(e.name) : presets.loadPresetFile(e.file);
    if (ok)
        showStatus("Loaded " + e.name);
    else
        showStatus("Couldn't load " + e.name + ". The file may be damaged.", true);

    refresh();
    if (onPresetLoaded)
        onPresetLoaded();
}

void PresetBar::step(int delta)
{
    if (entries.isEmpty())
        return;
    const int index = currentIndex();
    choose(index < 0 ? 0 : (index + delta + entries.size()) % entries.size());
}

void PresetBar::save()
{
    const int index = currentIndex();
    if (index < 0 || entries[index].file == juce::File())
    {
        saveAs(); // factory presets are read-only
        return;
    }
    const auto name = entries[index].name;
    if (processor.getPresetManager().savePreset(name).existsAsFile())
        showStatus("Saved " + name);
    else
        showStatus("Couldn't save. Check Documents\\StageMic\\Presets.", true);
    refresh();
}

void PresetBar::saveAs()
{
    const int index = currentIndex();
    const bool isUser = index >= 0 && entries[index].file != juce::File();
    auto* dialog = new juce::AlertWindow("Save preset", "Give this sound a name:", juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", isUser ? processor.getCurrentPresetName() + " 2" : "My " + processor.getCurrentPresetName());
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safe(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog](int result) {
                                if (safe == nullptr || result != 1)
                                    return;
                                const auto name = dialog->getTextEditorContents("name").trim();
                                if (name.isEmpty())
                                    return safe->showStatus("Please type a name.", true);
                                if (safe->processor.getPresetManager().fileForName(name).existsAsFile())
                                    return safe->confirmOverwrite(name);
                                if (safe->processor.getPresetManager().savePreset(name).existsAsFile())
                                    safe->showStatus("Saved " + name);
                                else
                                    safe->showStatus("Couldn't save the preset.", true);
                                safe->refresh();
                            }),
                            true);
}

void PresetBar::confirmOverwrite(const juce::String& name)
{
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Replace preset?")
                                     .withMessage("A preset called \"" + name + "\" already exists. Replace it?")
                                     .withButton("Replace", 1)
                                     .withButton("Cancel", 0)
                                     .withAssociatedComponent(this),
                                 [safe, name](int result) {
                                     if (safe == nullptr || result != 1)
                                         return;
                                     if (safe->processor.getPresetManager().savePreset(name).existsAsFile())
                                         safe->showStatus("Saved " + name);
                                     else
                                         safe->showStatus("Couldn't save the preset.", true);
                                     safe->refresh();
                                 });
}

void PresetBar::importPreset()
{
    const auto downloads = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads");
    chooser = std::make_unique<juce::FileChooser>(
        "Import a StageMic preset", downloads.isDirectory() ? downloads : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        juce::String("*") + PresetManager::fileExtension);
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser& fc) {
                             if (safe == nullptr || fc.getResult() == juce::File())
                                 return;
                             const auto imported = safe->processor.getPresetManager().importPreset(fc.getResult());
                             if (imported.existsAsFile())
                                 safe->showStatus("Imported " + imported.getFileNameWithoutExtension());
                             else
                                 safe->showStatus(fc.getResult().getFileName() + " isn't a StageMic preset.", true);
                             safe->refresh();
                             if (safe->onPresetLoaded)
                                 safe->onPresetLoaded();
                         });
}

void PresetBar::exportPreset()
{
    const auto suggested = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                               .getChildFile(juce::File::createLegalFileName(processor.getCurrentPresetName()) + PresetManager::fileExtension);
    chooser = std::make_unique<juce::FileChooser>("Export this sound as a preset file", suggested,
                                                  juce::String("*") + PresetManager::fileExtension);
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe](const juce::FileChooser& fc) {
                             if (safe == nullptr || fc.getResult() == juce::File())
                                 return;
                             if (safe->processor.getPresetManager().exportPreset(fc.getResult()))
                                 safe->showStatus("Exported " + fc.getResult().getFileName());
                             else
                                 safe->showStatus("Couldn't write " + fc.getResult().getFullPathName(), true);
                         });
}

void PresetBar::deleteCurrent()
{
    const int index = currentIndex();
    if (index < 0 || entries[index].file == juce::File())
        return showStatus("Factory presets can't be deleted.", true);

    const auto file = entries[index].file;
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle("Delete preset?")
                                     .withMessage("Delete \"" + file.getFileNameWithoutExtension() + "\"? This can't be undone.")
                                     .withButton("Delete", 1)
                                     .withButton("Cancel", 0)
                                     .withAssociatedComponent(this),
                                 [safe, file](int result) {
                                     if (safe == nullptr || result != 1)
                                         return;
                                     if (safe->processor.getPresetManager().deletePreset(file))
                                         safe->showStatus("Deleted " + file.getFileNameWithoutExtension());
                                     else
                                         safe->showStatus("Couldn't delete the preset.", true);
                                     safe->refresh();
                                 });
}

void PresetBar::showMenu()
{
    const int index = currentIndex();
    const bool isUser = index >= 0 && entries[index].file != juce::File();

    juce::PopupMenu menu;
    menu.addItem(1, "Save As...");
    menu.addItem(2, "Import Preset...");
    menu.addItem(3, "Export Preset...");
    menu.addSeparator();
    menu.addItem(4, "Delete This Preset", isUser);
    menu.addItem(5, "Open Preset Folder");

    juce::Component::SafePointer<PresetBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton), [safe](int result) {
        if (safe == nullptr)
            return;
        switch (result)
        {
            case 1: safe->saveAs(); break;
            case 2: safe->importPreset(); break;
            case 3: safe->exportPreset(); break;
            case 4: safe->deleteCurrent(); break;
            case 5:
            {
                const auto dir = safe->processor.getPresetManager().getPresetDirectory();
                dir.createDirectory();
                dir.startAsProcess();
                break;
            }
            default: break;
        }
    });
}

void PresetBar::showStatus(const juce::String& text, bool error)
{
    status.setColour(juce::Label::textColourId, error ? theme::meterRed : theme::dimText);
    status.setText(text, juce::dontSendNotification);
    statusTicks = 6;
    statusTimer.startTimer(1000);
}

void PresetBar::StatusTimer::timerCallback()
{
    if (--bar.statusTicks <= 0)
    {
        bar.status.setText({}, juce::dontSendNotification);
        stopTimer();
    }
}

void PresetBar::setMode(bool advanced)
{
    simpleButton.setToggleState(!advanced, juce::dontSendNotification);
    advancedButton.setToggleState(advanced, juce::dontSendNotification);
    simpleButton.getProperties().set("primary", !advanced);
    advancedButton.getProperties().set("primary", advanced);
    for (auto* b : {&simpleButton, &advancedButton})
    {
        b->setColour(juce::TextButton::textColourOffId, b->getToggleState() ? juce::Colours::white : theme::dimText);
        b->repaint();
    }
}

void PresetBar::paint(juce::Graphics& g)
{
    auto logo = getLocalBounds().removeFromLeft(196).withTrimmedLeft(2).reduced(0, 4);
    {
        // The neon head is drawn once into a picture (the glow is costly to draw).
        const auto headArea = logo.removeFromLeft(38).toFloat().reduced(1.0f, 2.0f);
        if (logoImage.isNull())
        {
            constexpr float oversample = 3.0f;
            logoImage = juce::Image(juce::Image::ARGB, (int)(headArea.getWidth() * oversample), (int)(headArea.getHeight() * oversample), true);
            juce::Graphics lg(logoImage);
            neoncat::paintHead(lg, logoImage.getBounds().toFloat(), theme::accent);
        }
        g.drawImage(logoImage, headArea);
    }
    logo.removeFromLeft(4);
    g.setColour(theme::accentSoft);
    g.setFont(theme::font(11.0f, true).withKerningFactor(0.25f));
    g.drawText("PAOPOA", logo.removeFromTop(13), juce::Justification::bottomLeft);
    g.setColour(theme::accent);
    g.setFont(theme::font(24.0f, true));
    g.drawText("StageMic", logo, juce::Justification::centredLeft);
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced(0, 7);
    area.removeFromLeft(200);
    prev.setBounds(area.removeFromLeft(30));
    area.removeFromLeft(4);
    box.setBounds(area.removeFromLeft(286));
    area.removeFromLeft(4);
    next.setBounds(area.removeFromLeft(30));
    area.removeFromLeft(10);
    saveButton.setBounds(area.removeFromLeft(64));
    area.removeFromLeft(4);
    menuButton.setBounds(area.removeFromLeft(36));
    area.removeFromLeft(12);
    voiceButton.setBounds(area.removeFromRight(150));
    area.removeFromRight(12);
    advancedButton.setBounds(area.removeFromRight(86));
    simpleButton.setBounds(area.removeFromRight(70));
    area.removeFromRight(10);
    status.setBounds(area);
}

} // namespace stagemic
