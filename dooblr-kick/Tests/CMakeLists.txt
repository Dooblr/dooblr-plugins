# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrKickTests KickTests.cpp)

target_include_directories(DooblrKickTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrKick,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrKickTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrKick,COMPILE_DEFINITIONS>
        DOOBLRKICK_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrKickTests
    PRIVATE
        DooblrKick
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrKickTests COMMAND DooblrKickTests)
