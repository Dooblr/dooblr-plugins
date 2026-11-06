# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrGenerateTests GenerateTests.cpp)

target_include_directories(DooblrGenerateTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrGenerate,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrGenerateTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrGenerate,COMPILE_DEFINITIONS>
        DOOBLRGENERATE_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrGenerateTests
    PRIVATE
        DooblrGenerate
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrGenerateTests COMMAND DooblrGenerateTests)

target_compile_definitions(DooblrGenerateTests
    PRIVATE
        DOOBLRGENERATE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
