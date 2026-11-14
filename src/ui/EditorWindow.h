// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace stagemic
{

/** Top-level desktop window that holds the StageMic editor.

    Closing the window only hides it; the editor is kept so reopening is
    instant. It is destroyed together with its FilterInstance. */
class EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow(const juce::String& title, juce::AudioProcessor& processor);
    ~EditorWindow() override;

    void closeButtonPressed() override;

private:
    void fitOnScreen();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
};

} // namespace stagemic
