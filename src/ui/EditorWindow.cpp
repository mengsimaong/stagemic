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

    centreWithSize(getWidth(), getHeight());
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
