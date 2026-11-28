# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrMeterTests MeterTests.cpp)

target_include_directories(DooblrMeterTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrMeter,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrMeterTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrMeter,COMPILE_DEFINITIONS>
        DOOBLRMETER_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrMeterTests
    PRIVATE
        DooblrMeter
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrMeterTests COMMAND DooblrMeterTests)
