cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-kick VERSION 0.3.7 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRKICK_BUILD_TESTS "Build the dooblr-kick tests" ON)
set(DOOBLRKICK_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release, fetched once into the build directory.
# To build against a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
set(DOOBLRKICK_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRKICK_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrKick; the product / bundle / host name is "dooblr-kick".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrKick
    PRODUCT_NAME                "dooblr-kick"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrkick"
    DESCRIPTION                 "Kick drum designer"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbkk
    FORMATS                     ${DOOBLRKICK_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(DooblrKick
    PRIVATE
        Source/DSP/Envelope.cpp
        Source/DSP/Effects.cpp
        Source/DSP/KickVoice.cpp
        Source/DSP/KickEngine.cpp
        Source/DSP/ProcessingChain.cpp
        Source/DSP/SamplePlayer.cpp
        Source/DSP/MeterTap.cpp
        Source/DSP/SpectrumAnalyzer.cpp
        Source/Samples/SampleBank.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/EnvelopeEditor.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/WaveformView.cpp
        Source/UI/OutputVisuals.cpp
        Source/UI/ProcessingVisuals.cpp
        Source/UI/Switches.cpp)

# Factory samples (CC0, see Resources/Samples/LICENSES.md), embedded in the binary.
file(GLOB dooblrkick_sample_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Samples/*.wav")
juce_add_binary_data(DooblrKickSamples SOURCES ${dooblrkick_sample_files})
set_target_properties(DooblrKickSamples PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(DooblrKick
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrKick
    PRIVATE
        DooblrKickSamples
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrKick_VST3)
    get_target_property(dooblrkick_vst3_artefact DooblrKick_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine, which a synced folder (iCloud ~/Documents before, Dropbox now) can add, and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrkick_vst3_artefact)
        add_custom_command(TARGET DooblrKick_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrkick_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrkick_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrKick_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrkick_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRKICK_BUILD_LOADCHECK "Build DooblrKickLoadCheck, the host-style load test" ON)

if(DOOBLRKICK_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

option(DOOBLRKICK_BUILD_FRAMEBENCH "Build DooblrKickFrameBench, the editor frame-time bench" ON)

if(DOOBLRKICK_BUILD_FRAMEBENCH AND APPLE)
    add_subdirectory(Tools/FrameBench)
endif()

if(DOOBLRKICK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
