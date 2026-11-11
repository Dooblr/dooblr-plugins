# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrSubTests SubTests.cpp)

target_include_directories(DooblrSubTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrSub,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrSubTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrSub,COMPILE_DEFINITIONS>
        DOOBLRSUB_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrSubTests
    PRIVATE
        DooblrSub
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrSubTests COMMAND DooblrSubTests)
