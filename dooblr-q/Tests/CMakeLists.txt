# Console smoke tests that exercise the real plugin processor and editor.
#
# The tests link the plugin's shared-code target (DooblrQ), which already contains
# the compiled JUCE modules and the processor/editor sources. JUCE's own format
# wrappers link the same way: they borrow the shared target's include directories
# and compile definitions rather than recompiling the modules.

add_executable(DooblrQTests PluginSmokeTests.cpp)

target_include_directories(DooblrQTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrQ,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrQTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrQ,COMPILE_DEFINITIONS>)

target_link_libraries(DooblrQTests
    PRIVATE
        DooblrQ
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME DooblrQSmokeTests COMMAND DooblrQTests)
