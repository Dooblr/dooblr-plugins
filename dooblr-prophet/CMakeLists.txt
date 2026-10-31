cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-prophet VERSION 0.3.1 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRPROPHET_BUILD_TESTS "Build the dooblr-prophet tests" ON)
set(DOOBLRPROPHET_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRPROPHET_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRPROPHET_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrProphet; the product / bundle / host name is "dooblr-prophet".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrProphet
    PRODUCT_NAME                "dooblr-prophet"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrprophet"
    DESCRIPTION                 "Analog-style polyphonic synthesizer"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbp5
    FORMATS                     ${DOOBLRPROPHET_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(DooblrProphet
    PRIVATE
        Source/DSP/SynthEngine.cpp
        Source/DSP/Effects.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/ControlRules.cpp
        Source/Link/Osc.cpp
        Source/Link/RemoteLink.cpp
        Source/Randomiser.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/Displays.cpp
        Source/UI/Layout.cpp)

target_compile_definitions(DooblrProphet
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrProphet
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrProphet_VST3)
    get_target_property(dooblrprophet_vst3_artefact DooblrProphet_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrprophet_vst3_artefact)
        add_custom_command(TARGET DooblrProphet_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrprophet_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrprophet_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrProphet_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrprophet_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRPROPHET_BUILD_LOADCHECK "Build DooblrProphetLoadCheck, the host-style load test" ON)

if(DOOBLRPROPHET_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRPROPHET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
