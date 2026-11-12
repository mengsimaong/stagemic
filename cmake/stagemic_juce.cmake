# Common JUCE settings shared by every StageMic target.
# SPDX-License-Identifier: AGPL-3.0-or-later

include_guard(GLOBAL)

function(stagemic_configure_juce_target target)
  target_link_libraries(
    ${target}
    PRIVATE
      juce::juce_audio_processors
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_gui_extra
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
  )

  target_compile_definitions(
    ${target}
    PRIVATE
      # Nothing that phones home or needs extra system libraries.
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      JUCE_VST3_CAN_REPLACE_VST2=0
      # Hosting third-party plugins comes in phase 3.
      JUCE_PLUGINHOST_VST3=0
      JUCE_PLUGINHOST_AU=0
      JUCE_PLUGINHOST_LADSPA=0
      JUCE_PLUGINHOST_LV2=0
      JUCE_WIN_PER_MONITOR_DPI_AWARE=1
      JUCE_STRICT_REFCOUNTEDPOINTER=1
      STAGEMIC_VERSION="${PROJECT_VERSION}"
  )

  # JUCE's own sources trigger warnings under OBS's strict warning set;
  # that is upstream noise, not something we can fix.
  if(MSVC)
    target_compile_options(${target} PRIVATE /bigobj)
  else()
    target_compile_options(${target} PRIVATE -Wno-unused-parameter -Wno-extra)
  endif()

  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
endfunction()
