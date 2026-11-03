# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrBassTests BassTests.cpp)

target_include_directories(DooblrBassTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrBass,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrBassTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrBass,COMPILE_DEFINITIONS>
        DOOBLRBASS_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrBassTests
    PRIVATE
        DooblrBass
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrBassTests COMMAND DooblrBassTests)
