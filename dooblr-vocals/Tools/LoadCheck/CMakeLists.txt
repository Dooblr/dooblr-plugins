# DooblrVocalsLoadCheck: loads a dooblr-vocals.vst3 like a host (dlopen + JUCE VST3 hosting).
# scripts/build.sh runs it against the built / installed bundle as part of verification.

juce_add_console_app(DooblrVocalsLoadCheck)

target_sources(DooblrVocalsLoadCheck PRIVATE LoadCheck.cpp)

target_compile_definitions(DooblrVocalsLoadCheck
    PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(DooblrVocalsLoadCheck
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_link_libraries(DooblrVocalsLoadCheck PRIVATE ${CMAKE_DL_LIBS})

file(GENERATE
    OUTPUT  "${CMAKE_BINARY_DIR}/artefact-paths/DooblrVocalsLoadCheck-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:DooblrVocalsLoadCheck>\n")
