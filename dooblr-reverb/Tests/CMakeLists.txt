# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrReverbTests ReverbTests.cpp)

target_include_directories(DooblrReverbTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrReverb,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrReverbTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrReverb,COMPILE_DEFINITIONS>
        DOOBLRREVERB_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrReverbTests
    PRIVATE
        DooblrReverb
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrReverbTests COMMAND DooblrReverbTests)
