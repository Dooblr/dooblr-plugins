cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-throat VERSION 0.3.0 LANGUAGES C CXX)

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
    DESCRIPTION                 "Vocal instrument"
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
        Source/DSP/Klatt.cpp
        Source/DSP/ProvenVoice.cpp
        Source/DSP/ThroatEngine.cpp
        Source/DSP/Vowels.cpp
        Source/DSP/Phonetics/Inventory.cpp
        Source/DSP/Phonetics/English.cpp
        Source/DSP/Phonetics/Durations.cpp
        Source/DSP/Phonetics/Syllables.cpp
        Source/DSP/Phonetics/Coarticulation.cpp
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


# ---------------------------------------------------------------------------
# Third-party voices (personal-use licenses; see LICENSES.md)
# Large artefacts (ONNX Runtime, Piper model, espeak-ng source) are fetched
# with scripts/fetch-third-party.sh and checksums; they are not in git.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/piper-voices/en_US-lessac-low.onnx"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/libonnxruntime.dylib"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/espeak-ng/src/include/espeak-ng/speak_lib.h")
    execute_process(
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch-third-party.sh"
        RESULT_VARIABLE _fetch_rc)
    if(NOT _fetch_rc EQUAL 0)
        message(FATAL_ERROR "Failed to fetch third-party artefacts (see scripts/fetch-third-party.sh)")
    endif()
endif()

set(DOOBLRTHROAT_ESPEAK_BUILD "$ENV{HOME}/dooblr-build/espeak-ng-static" CACHE PATH "espeak-ng static build dir")
set(DOOBLRTHROAT_WORLD_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/World")
set(DOOBLRTHROAT_ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
set(DOOBLRTHROAT_PIPER_ONNX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/piper-voices/en_US-lessac-low.onnx")

if(NOT EXISTS "${DOOBLRTHROAT_ESPEAK_BUILD}/src/libespeak-ng/libespeak-ng.a")
    set(DOOBLRTHROAT_ESPEAK_BUILD "${CMAKE_BINARY_DIR}/_espeak-ng" CACHE PATH "espeak-ng static build dir" FORCE)
    execute_process(
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-espeak-ng.sh"
                "${DOOBLRTHROAT_ESPEAK_BUILD}"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/espeak-ng"
        RESULT_VARIABLE _espeak_rc)
    if(NOT _espeak_rc EQUAL 0)
        message(FATAL_ERROR "Failed to build static espeak-ng")
    endif()
endif()

# WORLD (BSD): compile the core only, skip examples/tests.
set(WORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DOOBLRTHROAT_WORLD_SRC}" "${CMAKE_BINARY_DIR}/_world" EXCLUDE_FROM_ALL)

# espeak-ng was pre-built as a static lib (klatt ON, pcaudio OFF).
add_library(espeak-ng-static STATIC IMPORTED)
set_target_properties(espeak-ng-static PROPERTIES
    IMPORTED_LOCATION "${DOOBLRTHROAT_ESPEAK_BUILD}/src/libespeak-ng/libespeak-ng.a")
add_library(espeak-ucd STATIC IMPORTED)
set_target_properties(espeak-ucd PROPERTIES
    IMPORTED_LOCATION "${DOOBLRTHROAT_ESPEAK_BUILD}/src/ucd-tools/libucd.a")
add_library(espeak-speechPlayer STATIC IMPORTED)
set_target_properties(espeak-speechPlayer PROPERTIES
    IMPORTED_LOCATION "${DOOBLRTHROAT_ESPEAK_BUILD}/src/speechPlayer/libspeechPlayer.a")

# ONNX Runtime (MIT) prebuilt dylib.
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${DOOBLRTHROAT_ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${DOOBLRTHROAT_ONNXRUNTIME_ROOT}/include")

target_include_directories(DooblrThroat PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/espeak-ng/src/include"
    "${DOOBLRTHROAT_WORLD_SRC}/src")

target_compile_definitions(DooblrThroat PRIVATE
    DOOBLRTHROAT_ESPEAK_DATA_PARENT="${DOOBLRTHROAT_ESPEAK_BUILD}"
    DOOBLRTHROAT_PIPER_ONNX="${DOOBLRTHROAT_PIPER_ONNX}")

# Tests use the fetched dylib via BUILD_RPATH. The VST3 binary gets
# @loader_path/../Frameworks after cmake/bundle-resources.sh copies it in.
if(APPLE)
    set(_ort_lib_dir "${DOOBLRTHROAT_ONNXRUNTIME_ROOT}/lib")
    set_property(TARGET DooblrThroat APPEND PROPERTY BUILD_RPATH "${_ort_lib_dir}")
endif()

target_link_libraries(DooblrThroat
    PRIVATE
        DooblrThroatData
        espeak-ng-static
        espeak-speechPlayer
        espeak-ucd
        world
        onnxruntime
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
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle-resources.sh"
                    "${dooblrthroat_vst3_artefact}"
                    "${DOOBLRTHROAT_ONNXRUNTIME_ROOT}/lib"
                    "${DOOBLRTHROAT_PIPER_ONNX}"
                    "${DOOBLRTHROAT_ESPEAK_BUILD}"
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign-bundle.sh" "${dooblrthroat_vst3_artefact}"
            VERBATIM)
        set_property(TARGET DooblrThroat_VST3 APPEND PROPERTY BUILD_RPATH "@loader_path/../Frameworks")
        set_property(TARGET DooblrThroat_VST3 APPEND PROPERTY INSTALL_RPATH "@loader_path/../Frameworks")
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
