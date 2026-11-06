# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrDistressorTests DistressorTests.cpp)

target_include_directories(DooblrDistressorTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrDistressor,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrDistressorTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrDistressor,COMPILE_DEFINITIONS>
        DOOBLRDISTRESSOR_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrDistressorTests
    PRIVATE
        DooblrDistressor
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrDistressorTests COMMAND DooblrDistressorTests)
