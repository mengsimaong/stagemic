# SPDX-License-Identifier: AGPL-3.0-or-later
# Developer tools (not shipped to users).

# stagemic_render: render a recording through every preset for listening tests.
juce_add_console_app(stagemic_render PRODUCT_NAME "stagemic_render")

set(_shared ${STAGEMIC_SHARED_SOURCES})
list(TRANSFORM _shared PREPEND "${PROJECT_SOURCE_DIR}/")

target_sources(stagemic_render PRIVATE ${_shared} RenderPresets.cpp)
stagemic_configure_juce_target(stagemic_render)
target_link_libraries(stagemic_render PRIVATE juce::juce_audio_formats)

# stagemic_bench: CPU cost per preset and abuse tests, driven like OBS drives the filter.
juce_add_console_app(stagemic_bench PRODUCT_NAME "stagemic_bench")
target_sources(stagemic_bench PRIVATE ${_shared} StressBench.cpp)
stagemic_configure_juce_target(stagemic_bench)
target_link_libraries(stagemic_bench PRIVATE juce::juce_audio_formats)

# stagemic_devtools: editor screenshots, artwork, and live window measurements.
juce_add_console_app(stagemic_devtools PRODUCT_NAME "stagemic_devtools")
target_sources(stagemic_devtools PRIVATE ${_shared} DevTools.cpp)
stagemic_configure_juce_target(stagemic_devtools)
target_compile_definitions(stagemic_devtools PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
