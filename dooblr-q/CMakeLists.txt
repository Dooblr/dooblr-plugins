cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-q VERSION 0.9.4 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOOBLRQ_BUILD_TESTS "Build the dooblr-q smoke tests" ON)

# Plugin formats to build. Add AU on macOS later with -DDOOBLRQ_PLUGIN_FORMATS="VST3;AU".
set(DOOBLRQ_PLUGIN_FORMATS VST3 CACHE STRING "JUCE plugin formats to build")

# ---------------------------------------------------------------------------
# JUCE: pinned official release, fetched once into the build directory.
# To build against a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
set(DOOBLRQ_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRQ_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin. The CMake target is DooblrQ (no hyphen); the product, bundle and
# host-visible name is "dooblr-q".
# ---------------------------------------------------------------------------
juce_add_plugin(DooblrQ
    PRODUCT_NAME                "dooblr-q"
    COMPANY_NAME                "dooblr"
    BUNDLE_ID                   "com.dooblr.dooblrq"
    DESCRIPTION                 "Dynamic parametric equaliser"
    PLUGIN_MANUFACTURER_CODE    Dblr
    PLUGIN_CODE                 Dblq
    FORMATS                     ${DOOBLRQ_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx EQ Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(DooblrQ
    PRIVATE
        Source/DSP/SvfFilter.cpp
        Source/Analysis/SpectrumAnalyzer.cpp
        Source/Analysis/SpectrumDisplay.cpp
        Source/DSP/AnalogModels.cpp
        Source/DSP/DynamicEQBand.cpp
        Source/DSP/FirEqEngine.cpp
        Source/DSP/SoloStage.cpp
        Source/Parameters.cpp
        Source/UndoHistory.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/Theme.cpp
        Source/UI/ResponseCurveComponent.cpp
        Source/UI/RangeRing.cpp)

target_compile_definitions(DooblrQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DooblrQ
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Record the real VST3 artefact location per configuration so scripts/build.sh
# can report it without hard-coding JUCE's output layout.
if(TARGET DooblrQ_VST3)
    get_target_property(dooblrq_vst3_artefact DooblrQ_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

    # JUCE ad-hoc signs the bundle and only afterwards regenerates moduleinfo.json, which
    # leaves a stale signature whenever that file changes (e.g. a version bump). Re-sign
    # last so the bundle always passes `codesign --verify --deep --strict`.
    if(APPLE AND dooblrq_vst3_artefact)
        #
        # The build output must also not carry com.apple.quarantine: this project lives in
        # a File Provider (iCloud) synced ~/Documents, and the bundle folder in the build
        # tree picked up a quarantine flag. Copies (ditto, Finder) carry it along, and
        # Gatekeeper then refuses to let a host dlopen the plug-in ("library load disallowed
        # by system policy"), so DAWs report it as not loadable. Strip it before signing.
        add_custom_command(TARGET DooblrQ_VST3 POST_BUILD
            COMMAND sh -c "xattr -r -d com.apple.quarantine \"$1\" 2>/dev/null; exit 0" sh "${dooblrq_vst3_artefact}"
            COMMAND codesign --force --sign - "${dooblrq_vst3_artefact}"
            COMMAND codesign --verify --deep --strict "${dooblrq_vst3_artefact}"
            VERBATIM)
    endif()

    if(dooblrq_vst3_artefact)
        file(GENERATE
            OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrQ_VST3-$<CONFIG>.txt"
            CONTENT "${dooblrq_vst3_artefact}\n")
    endif()
endif()

# ---------------------------------------------------------------------------
# Load check tool (used by scripts/build.sh to load the installed bundle like a host)
# ---------------------------------------------------------------------------
option(DOOBLRQ_BUILD_LOADCHECK "Build DooblrQLoadCheck, the host-style load test" ON)

if(DOOBLRQ_BUILD_LOADCHECK AND APPLE)
    add_subdirectory(Tools/LoadCheck)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(DOOBLRQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
