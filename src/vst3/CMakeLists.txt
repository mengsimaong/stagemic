# SPDX-License-Identifier: AGPL-3.0-or-later
# StageMic as a VST3 plugin, for testing the engine in a DAW (Reaper, etc.).

juce_add_plugin(
  StageMic_VST3
  PRODUCT_NAME "StageMic"
  COMPANY_NAME "StageMic"
  BUNDLE_ID "org.stagemic.vst3"
  PLUGIN_MANUFACTURER_CODE Stgm
  PLUGIN_CODE Smic
  FORMATS VST3
  VST3_CATEGORIES Fx Tools
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VERSION ${PROJECT_VERSION}
)

set(_shared ${STAGEMIC_SHARED_SOURCES})
list(TRANSFORM _shared PREPEND "${PROJECT_SOURCE_DIR}/")

target_sources(StageMic_VST3 PRIVATE ${_shared} PluginEntry.cpp)
stagemic_configure_juce_target(StageMic_VST3)
