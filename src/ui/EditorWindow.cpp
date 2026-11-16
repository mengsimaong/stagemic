// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/EditorWindow.h"

namespace stagemic
{

EditorWindow::EditorWindow(const juce::String& title, juce::AudioProcessor& processor)
    : juce::DocumentWindow(title, juce::Colour(0xff15161c), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton, true)
{
    setUsingNativeTitleBar(true);

    if (auto* editor = processor.createEditorAndMakeActive())
    {
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
    }

    fitOnScreen();
}

void EditorWindow::fitOnScreen()
{
    // Laptops often have little room
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForPoint(juce::Desktop::getInstance().getMainMouseSource().getScreenPosition());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display == nullptr)
    {
        centreWithSize(getWidth(), getHeight());
        return;
    }

    constexpr int titleBar = 40, margin = 8;
    const auto area = display->userBounds.toNearestInt().reduced(margin);
    if (auto* content = getContentComponent())
    {
        const double fit =
            juce::jmin((double)area.getWidth() / content->getWidth(), (double)(area.getHeight() - titleBar) / content->getHeight());
        if (fit < 1.0)
            content->setSize(juce::roundToInt(content->getWidth() * fit), juce::roundToInt(content->getHeight() * fit));
    }
    setBounds(getBounds().withCentre(area.getCentre().translated(0, titleBar / 2)).constrainedWithin(area));
}

EditorWindow::~EditorWindow()
{
    clearContentComponent(); // deletes the editor before the window
}

void EditorWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace stagemic
