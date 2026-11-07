cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-distressor VERSION 0.1.2 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRDISTRESSOR_BUILD_TESTS "Build the dooblr-distressor tests" ON)
set(DOOBLRDISTRESSOR_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRDISTRESSOR_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRDISTRESSOR_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrDistressor; the product / bundle / host name is "dooblr-distressor".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrDistressor
    PRODUCT_NAME                "dooblr-distressor"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrdistressor"
    DESCRIPTION                 "EL8-style compressor"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbdt
    FORMATS                     ${DOOBLRDISTRESSOR_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(DooblrDistressor
    PRIVATE
        Source/DSP/DistressorEngine.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/Board.cpp
        Source/UI/CompressionDisplay.cpp)

target_compile_definitions(DooblrDistressor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrDistressor
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrDistressor_VST3)
    get_target_property(dooblrdistressor_vst3_artefact DooblrDistressor_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrdistressor_vst3_artefact)
        add_custom_command(TARGET DooblrDistressor_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrdistressor_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrdistressor_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrDistressor_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrdistressor_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRDISTRESSOR_BUILD_LOADCHECK "Build DooblrDistressorLoadCheck, the host-style load test" ON)

if(DOOBLRDISTRESSOR_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRDISTRESSOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
