cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-throat VERSION 0.2.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRTHROAT_BUILD_TESTS "Build the dooblr-throat tests" ON)
set(DOOBLRTHROAT_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release. scripts/build.sh points FetchContent at the suite's shared
# checkout (~/dooblr-build/_deps/juce-src) with -DFETCHCONTENT_SOURCE_DIR_JUCE when it exists.
# ---------------------------------------------------------------------------
set(DOOBLRTHROAT_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRTHROAT_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrThroat; the product / bundle / host name is "dooblr-throat".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrThroat
    PRODUCT_NAME                "dooblr-throat"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.throat"
    DESCRIPTION                 "Physical-model vocal instrument"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dbth
    FORMATS                     ${DOOBLRTHROAT_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(DooblrThroat
    PRIVATE
        Source/DSP/Airway.cpp
        Source/DSP/Anatomy.cpp
        Source/DSP/Articulation.cpp
        Source/DSP/Decimator.cpp
        Source/DSP/Geom.cpp
        Source/DSP/Glottis.cpp
        Source/DSP/HumanAirway.cpp
        Source/DSP/HumanVoice.cpp
        Source/DSP/ThroatEngine.cpp
        Source/DSP/Vowels.cpp
        Source/DSP/Phonetics/Inventory.cpp
        Source/DSP/Phonetics/English.cpp
        Source/DSP/Phonetics/Durations.cpp
        Source/DSP/Phonetics/Syllables.cpp
        Source/DSP/Phonetics/Planner.cpp
        Source/DSP/Phonetics/Phrase.cpp
        Source/DSP/Phonetics/Cmudict.cpp
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ControlPanel.cpp
        Source/UI/OutputMeter.cpp
        Source/UI/VowelPad.cpp)

juce_add_binary_data(DooblrThroatData
    SOURCES
        Resources/cmudict.txt.gz)

target_compile_definitions(DooblrThroat
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrThroat
    PRIVATE
        DooblrThroatData
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(TARGET DooblrThroat_VST3)
    get_target_property(dooblrthroat_vst3_artefact DooblrThroat_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature. Re-sign last (cmake/sign-bundle.sh strips Finder info and
    # quarantine and retries) so `codesign --verify --deep --strict` passes.
    if(APPLE AND dooblrthroat_vst3_artefact)
        add_custom_command(TARGET DooblrThroat_VST3 POST_BUILD
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrthroat_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrthroat_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrThroat_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrthroat_vst3_artefact}\n")
    endif()
endif()

option(DOOBLRTHROAT_BUILD_LOADCHECK "Build DooblrThroatLoadCheck, the host-style load test" ON)

if(DOOBLRTHROAT_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

if(DOOBLRTHROAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
