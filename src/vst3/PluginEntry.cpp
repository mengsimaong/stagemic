// SPDX-License-Identifier: AGPL-3.0-or-later
// Entry point for the VST3 build: the same engine and editor as the OBS plugin.
#include "core/StageMicProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto* processor = new stagemic::StageMicProcessor();
    processor->setHostDescription(juce::String("VST3 (") + juce::PluginHostType().getHostDescription() + ")");
    return processor;
}
