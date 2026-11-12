# SPDX-License-Identifier: AGPL-3.0-or-later
# Developer tools (not shipped to users).

# stagemic_render: render a recording through every preset for listening tests.
juce_add_console_app(stagemic_render PRODUCT_NAME "stagemic_render")

set(_shared ${STAGEMIC_SHARED_SOURCES})
list(TRANSFORM _shared PREPEND "${PROJECT_SOURCE_DIR}/")

target_sources(stagemic_render PRIVATE ${_shared} RenderPresets.cpp)
stagemic_configure_juce_target(stagemic_render)
target_link_libraries(stagemic_render PRIVATE juce::juce_audio_formats)
