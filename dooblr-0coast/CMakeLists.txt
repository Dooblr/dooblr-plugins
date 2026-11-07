cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-0coast VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLR0COAST_BUILD_TESTS "Build the dooblr-0coast tests" ON)
set(DOOBLR0COAST_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLR0COAST_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLR0COAST_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is Dooblr0Coast; the product / bundle / host name is "dooblr-0coast".
# ---------------------------------------------------------------------------
juce_add_plugin(Dooblr0Coast
    PRODUCT_NAME                "dooblr-0coast"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblr0coast"
    DESCRIPTION                 "No-coast monophonic synth voice"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbnc
    FORMATS                     ${DOOBLR0COAST_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(Dooblr0Coast
    PRIVATE
        Source/DSP/SynthEngine.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/PatchView.cpp)

target_compile_definitions(Dooblr0Coast
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Dooblr0Coast
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET Dooblr0Coast_VST3)
    get_target_property(dooblr0coast_vst3_artefact Dooblr0Coast_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblr0coast_vst3_artefact)
        add_custom_command(TARGET Dooblr0Coast_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblr0coast_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblr0coast_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/Dooblr0Coast_VST3-$<CONFIG>.txt"
            CONTENT "${dooblr0coast_vst3_artefact}\n")
    endif()
endif()

option(DOOBLR0COAST_BUILD_LOADCHECK "Build Dooblr0CoastLoadCheck, the host-style load test" ON)

if(DOOBLR0COAST_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLR0COAST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
