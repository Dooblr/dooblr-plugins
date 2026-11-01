# DooblrFeedbackLoadCheck: loads a dooblr-feedback.vst3 like a host (dlopen + JUCE VST3 hosting).
# scripts/build.sh runs it against the installed bundle as part of install verification.

juce_add_console_app(DooblrFeedbackLoadCheck)

target_sources(DooblrFeedbackLoadCheck PRIVATE LoadCheck.cpp)

target_compile_definitions(DooblrFeedbackLoadCheck
    PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(DooblrFeedbackLoadCheck
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_link_libraries(DooblrFeedbackLoadCheck PRIVATE ${CMAKE_DL_LIBS})

# Tell scripts/build.sh where the tool is for each configuration.
file(GENERATE
    OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrFeedbackLoadCheck-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:DooblrFeedbackLoadCheck>\n")
