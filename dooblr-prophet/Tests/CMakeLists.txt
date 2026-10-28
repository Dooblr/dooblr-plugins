# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrProphetTests ProphetTests.cpp)

target_include_directories(DooblrProphetTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrProphet,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrProphetTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrProphet,COMPILE_DEFINITIONS>
        DOOBLRPROPHET_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrProphetTests
    PRIVATE
        DooblrProphet
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrProphetTests COMMAND DooblrProphetTests)
