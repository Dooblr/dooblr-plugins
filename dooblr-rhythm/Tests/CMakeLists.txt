# Console tests that exercise the real plugin processor, engine, DSP and editor.
add_executable(DooblrRhythmTests RhythmTests.cpp)

target_include_directories(DooblrRhythmTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrRhythm,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrRhythmTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrRhythm,COMPILE_DEFINITIONS>
        DOOBLRRHYTHM_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrRhythmTests
    PRIVATE
        DooblrRhythm
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrRhythmTests COMMAND DooblrRhythmTests)
