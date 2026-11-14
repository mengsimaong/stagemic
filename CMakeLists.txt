# StageMic - one-window karaoke/vocal chain for OBS Studio.
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# What gets built (each can be switched off):
#   stagemic          - the OBS Studio plugin (stagemic.dll on Windows)
#   StageMic_VST3     - the same audio engine as a VST3, for testing in a DAW
#   stagemic_tests    - automated tests that run without OBS

cmake_minimum_required(VERSION 3.28...3.31)

# Reads name/version from buildspec.json and sets up OBS's CMake helpers.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(STAGEMIC_BUILD_OBS "Build the OBS Studio plugin" ON)
option(STAGEMIC_BUILD_VST3 "Build the VST3 plugin (for testing in a DAW)" ON)
option(STAGEMIC_BUILD_TESTS "Build the automated tests" ON)

# JUCE's own sources compile inside our targets, so upstream warnings must not
# fail the build. (Pass -DCMAKE_COMPILE_WARNING_AS_ERROR=ON to be strict.)
if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
endif()

# The OBS template defaults to C++17; JUCE and our code use C++20.
include(compilerconfig)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# Finding / downloading OBS is only needed when building the OBS plugin.
if(STAGEMIC_BUILD_OBS)
  set(ENABLE_FRONTEND_API ON)
  include(defaults)
  include(helpers)
endif()

# ---------------------------------------------------------------------------
# JUCE (AGPL-3.0 / commercial). Downloaded automatically at configure time.
# To use a local copy instead: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
set(STAGEMIC_JUCE_VERSION 9.0.5)
include(FetchContent)
FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG ${STAGEMIC_JUCE_VERSION}
  GIT_SHALLOW TRUE
  GIT_PROGRESS FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(juce)

include(cmake/stagemic_juce.cmake)

# ---------------------------------------------------------------------------
# Shared engine + editor sources (compiled into every target below).
# ---------------------------------------------------------------------------
set(
  STAGEMIC_SHARED_SOURCES
  src/core/StageMicProcessor.cpp
  src/core/StageMicProcessor.h
  src/core/FilterInstance.cpp
  src/core/FilterInstance.h
  src/core/LevelMeter.h
  src/core/Parameters.cpp
  src/core/Parameters.h
  src/core/dsp/DspUtils.h
  src/core/dsp/Safety.h
  src/core/dsp/Module.h
  src/core/dsp/Dynamics.cpp
  src/core/dsp/Dynamics.h
  src/core/dsp/Tone.cpp
  src/core/dsp/Tone.h
  src/core/dsp/Tune.cpp
  src/core/dsp/Tune.h
  src/core/dsp/Effects.cpp
  src/core/dsp/Effects.h
  src/core/dsp/VocalChain.cpp
  src/core/dsp/VocalChain.h
  src/core/presets/PresetManager.cpp
  src/core/presets/PresetManager.h
  src/core/SpectrumTap.h
  src/core/VoiceAnalyzer.h
  src/ui/VoiceSetup.cpp
  src/ui/VoiceSetup.h
  src/ui/Theme.h
  src/ui/Widgets.cpp
  src/ui/Widgets.h
  src/ui/Strips.cpp
  src/ui/Strips.h
  src/ui/Displays.cpp
  src/ui/Displays.h
  src/ui/MasterPanel.cpp
  src/ui/MasterPanel.h
  src/ui/PresetBar.cpp
  src/ui/PresetBar.h
  src/ui/ChainBar.cpp
  src/ui/NeonCat.cpp
  src/ui/NeonCat.h
  src/ui/SimpleView.cpp
  src/ui/SimpleView.h
  src/ui/ChainBar.h
  src/ui/StageMicEditor.cpp
  src/ui/StageMicEditor.h
  src/ui/EditorWindow.cpp
  src/ui/EditorWindow.h
)

# ---------------------------------------------------------------------------
# OBS plugin. Defined here (not in a subfolder) because the OBS template's
# helpers expect the plugin target and data/ folder at the top level.
# ---------------------------------------------------------------------------
if(STAGEMIC_BUILD_OBS)
  add_library(${CMAKE_PROJECT_NAME} MODULE)

  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)

  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      ${STAGEMIC_SHARED_SOURCES}
      src/obs/plugin-main.cpp
      src/obs/StageMicFilter.cpp
      src/obs/StageMicFilter.h
      src/obs/JuceRuntime.cpp
      src/obs/JuceRuntime.h
      src/obs/ObsWindowOwner.cpp
      src/obs/ObsWindowOwner.h
  )

  target_compile_definitions(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      STAGEMIC_HOST_OBS=1
  )
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)
  stagemic_configure_juce_target(${CMAKE_PROJECT_NAME})

  set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
endif()

if(STAGEMIC_BUILD_VST3)
  add_subdirectory(src/vst3)
endif()

if(STAGEMIC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tools)
endif()
