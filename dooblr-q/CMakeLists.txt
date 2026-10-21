cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(OctaEQ VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OCTAEQ_BUILD_TESTS "Build the OctaEQ smoke tests" ON)

# Plugin formats to build. Add AU on macOS later with -DOCTAEQ_PLUGIN_FORMATS="VST3;AU".
set(OCTAEQ_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release, fetched once into the build directory.
# To build against a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
set(OCTAEQ_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${OCTAEQ_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(OctaEQ
    PRODUCT_NAME                "OctaEQ"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.octaeq"
    DESCRIPTION                 "Parametric equaliser"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Octq
    FORMATS                     ${OCTAEQ_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(OctaEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_compile_definitions(OctaEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OctaEQ
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Record the real VST3 artefact location per configuration so scripts/build.sh
# can report it without hard-coding JUCE's output layout.
if(TARGET OctaEQ_VST3)
    get_target_property(octaeq_vst3_artefact OctaEQ_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    if(octaeq_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/OctaEQ_VST3-$<CONFIG>.txt"
            CONTENT "${octaeq_vst3_artefact}\n")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(OCTAEQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
