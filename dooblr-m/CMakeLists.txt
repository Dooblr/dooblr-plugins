cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-m VERSION 0.1.1 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRM_BUILD_TESTS "Build the dooblr-m tests" ON)
set(DOOBLRM_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRM_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRM_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrM; the product / bundle / host name is "dooblr-m".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrM
    PRODUCT_NAME                "dooblr-m"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrm"
    DESCRIPTION                 "All-purpose Model D-style monophonic synthesizer"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbmm
    FORMATS                     ${DOOBLRM_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(DooblrM
    PRIVATE
        Source/DSP/SynthEngine.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/BoardControls.cpp)

target_compile_definitions(DooblrM
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)

target_link_libraries(DooblrM
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrM_VST3)
    get_target_property(dooblrm_vst3_artefact DooblrM_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    if(APPLE AND dooblrm_vst3_artefact)
        add_custom_command(TARGET DooblrM_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrm_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrm_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrM_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrm_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRM_BUILD_LOADCHECK "Build DooblrMLoadCheck, the host-style load test" ON)

if(DOOBLRM_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
