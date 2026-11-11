cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-generate VERSION 0.3.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRGENERATE_BUILD_TESTS "Build the dooblr-generate tests" ON)
set(DOOBLRGENERATE_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRGENERATE_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRGENERATE_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrGenerate; the product / bundle / host name is "dooblr-generate".
# The model worker (worker/dooblr_generate_worker.py, our own code) is embedded as binary data
# and written to ~/Library/Application Support/dooblr/generate/runtime at run time.
# No model weights are ever part of the build.
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrGenerate
    PRODUCT_NAME                "dooblr-generate"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrgenerate"
    DESCRIPTION                 "Text-to-sample generator and one-shot sampler"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbgn
    FORMATS                     ${DOOBLRGENERATE_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

juce_add_binary_data(DooblrGenerateWorkerData
    SOURCES worker/dooblr_generate_worker.py)

target_sources(DooblrGenerate
    PRIVATE
        Source/Parameters.cpp
        Source/Engine/Sample.cpp
        Source/Engine/Sampler.cpp
        Source/Generation/Paths.cpp
        Source/Generation/ModelCatalog.cpp
        Source/Generation/History.cpp
        Source/Generation/SourceAudio.cpp
        Source/Generation/BeatSync.h
        Source/Generation/GenerationService.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/WaveformView.cpp
        Source/UI/HistoryList.cpp)

target_compile_definitions(DooblrGenerate
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrGenerate
    PRIVATE
        DooblrGenerateWorkerData
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrGenerate_VST3)
    get_target_property(dooblrgenerate_vst3_artefact DooblrGenerate_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrgenerate_vst3_artefact)
        add_custom_command(TARGET DooblrGenerate_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrgenerate_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrgenerate_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrGenerate_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrgenerate_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRGENERATE_BUILD_LOADCHECK "Build DooblrGenerateLoadCheck, the host-style load test" ON)

if(DOOBLRGENERATE_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRGENERATE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
