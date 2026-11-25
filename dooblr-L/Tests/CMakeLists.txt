# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrLTests LimiterTests.cpp)

target_include_directories(DooblrLTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrL,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrLTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrL,COMPILE_DEFINITIONS>
        DOOBLRL_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrLTests
    PRIVATE
        DooblrL
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrLTests COMMAND DooblrLTests)
