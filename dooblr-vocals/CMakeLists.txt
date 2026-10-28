cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-vocals VERSION 0.2.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRVOCALS_BUILD_TESTS "Build the dooblr-vocals tests" ON)
set(DOOBLRVOCALS_PLUGIN_FORMATS "VST3;Standalone" CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release, fetched once into the build directory.
# To build against a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
set(DOOBLRVOCALS_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRVOCALS_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrVocals; the product / bundle / host name is "dooblr-vocals".
# Bundle ID is com.dooblr.vocals (suite owner), not the generic com.dooblr.dooblrvocals template.
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrVocals
    PRODUCT_NAME                "dooblr-vocals"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.vocals"
    DESCRIPTION                 "Real-time pitch correction and vocal chain"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbvx
    FORMATS                     ${DOOBLRVOCALS_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx PitchShift Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(DooblrVocals
    PRIVATE
        Source/DSP/Effects.cpp
        Source/DSP/MeterTap.cpp
        Source/DSP/PitchEngine.cpp
        Source/DSP/ProcessingChain.cpp
        Source/DSP/SpectrumAnalyzer.cpp
        Source/DSP/TimeFx.cpp
        Source/DSP/VocalEngine.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/RangeLearner.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/OutputVisuals.cpp
        Source/UI/PitchDisplay.cpp
        Source/UI/GateDisplay.cpp
        Source/UI/HarmonyDisplay.cpp
        Source/UI/FxDisplays.cpp)

target_include_directories(DooblrVocals PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(DooblrVocals
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrVocals
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrVocals_VST3)
    get_target_property(dooblrvocals_vst3_artefact DooblrVocals_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine, which a File Provider synced folder (Dropbox, iCloud) can add, and retries) so
    # `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrvocals_vst3_artefact)
        add_custom_command(TARGET DooblrVocals_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrvocals_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrvocals_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrVocals_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrvocals_vst3_artefact}\n")
    endif()
endif()

if(TARGET DooblrVocals_Standalone)
    get_target_property(dooblrvocals_standalone_artefact DooblrVocals_Standalone JUCE_PLUGIN_ARTEFACT_FILE)
    if(dooblrvocals_standalone_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrVocals_Standalone-$<CONFIG>.txt"
            CONTENT "${dooblrvocals_standalone_artefact}\n")
    endif()
endif()

option(DOOBLRVOCALS_BUILD_LOADCHECK "Build DooblrVocalsLoadCheck, the host-style load test" ON)

if(DOOBLRVOCALS_BUILD_LOADCHECK)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRVOCALS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
