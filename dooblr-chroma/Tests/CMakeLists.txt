# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrChromaTests ChromaTests.cpp)

target_include_directories(DooblrChromaTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrChroma,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrChromaTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrChroma,COMPILE_DEFINITIONS>
        DOOBLRCHROMA_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrChromaTests
    PRIVATE
        DooblrChroma
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrChromaTests COMMAND DooblrChromaTests)
