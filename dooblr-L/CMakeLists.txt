cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-L VERSION 0.1.3 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRL_BUILD_TESTS "Build the dooblr-L tests" ON)
set(DOOBLRL_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRL_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRL_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrL; the product / bundle / host name is "dooblr-L".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrL
    PRODUCT_NAME                "dooblr-L"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrl"
    DESCRIPTION                 "Mastering and bus limiter"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbll
    FORMATS                     ${DOOBLRL_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/AddPresetPaths.cmake")
dooblr_add_preset_paths(DooblrL)

target_sources(DooblrL
    PRIVATE
        Source/DSP/HistoryTap.cpp
        Source/DSP/LearnSetup.cpp
        Source/DSP/LimiterEngine.cpp
        Source/DSP/Loudness.cpp
        Source/DSP/TruePeak.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/Meters.cpp)

target_compile_definitions(DooblrL
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrL
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrL_VST3)
    get_target_property(dooblrl_vst3_artefact DooblrL_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrl_vst3_artefact)
        add_custom_command(TARGET DooblrL_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrl_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrl_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrL_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrl_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRL_BUILD_LOADCHECK "Build DooblrLLoadCheck, the host-style load test" ON)

if(DOOBLRL_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
