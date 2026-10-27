# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrVocalsTests VocalsTests.cpp)

target_include_directories(DooblrVocalsTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrVocals,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrVocalsTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrVocals,COMPILE_DEFINITIONS>
        DOOBLRVOCALS_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrVocalsTests
    PRIVATE
        DooblrVocals
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrVocalsTests COMMAND DooblrVocalsTests)
