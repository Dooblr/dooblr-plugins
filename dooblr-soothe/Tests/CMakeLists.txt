# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrSootheTests SootheTests.cpp)

target_include_directories(DooblrSootheTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrSoothe,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrSootheTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrSoothe,COMPILE_DEFINITIONS>
        DOOBLRSOOTHE_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrSootheTests
    PRIVATE
        DooblrSoothe
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrSootheTests COMMAND DooblrSootheTests)
