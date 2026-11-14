# SPDX-License-Identifier: AGPL-3.0-or-later
# Automated tests: run the engine exactly as OBS would, but without OBS.

juce_add_console_app(stagemic_tests PRODUCT_NAME "stagemic_tests")

set(_shared ${STAGEMIC_SHARED_SOURCES})
list(TRANSFORM _shared PREPEND "${PROJECT_SOURCE_DIR}/")

target_sources(
  stagemic_tests
  PRIVATE
    ${_shared}
    AllocationGuard.cpp
    AllocationGuard.h
    TestMain.cpp
    FilterInstanceTests.cpp
    ChainTests.cpp
    TuneTests.cpp
    UiTests.cpp
    TestHelpers.h
)
stagemic_configure_juce_target(stagemic_tests)
target_compile_definitions(stagemic_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1) # for the live UI benchmark
target_include_directories(stagemic_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME stagemic_tests COMMAND stagemic_tests)
