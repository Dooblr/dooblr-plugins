# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrFeedbackTests FeedbackTests.cpp)

target_include_directories(DooblrFeedbackTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrFeedback,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrFeedbackTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrFeedback,COMPILE_DEFINITIONS>
        DOOBLRFEEDBACK_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrFeedbackTests
    PRIVATE
        DooblrFeedback
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrFeedbackTests COMMAND DooblrFeedbackTests)
