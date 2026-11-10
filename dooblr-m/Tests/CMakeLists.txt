# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrMTests ModelTests.cpp)

target_include_directories(DooblrMTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrM,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrMTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrM,COMPILE_DEFINITIONS>
        DOOBLRM_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrMTests
    PRIVATE
        DooblrM
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrMTests COMMAND DooblrMTests)
