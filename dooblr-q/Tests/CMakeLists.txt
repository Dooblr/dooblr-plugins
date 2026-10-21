# Console smoke tests that exercise the real plugin processor and editor.
#
# The tests link the plugin's shared-code target (OctaEQ), which already contains
# the compiled JUCE modules and the processor/editor sources. JUCE's own format
# wrappers link the same way: they borrow the shared target's include directories
# and compile definitions rather than recompiling the modules.

add_executable(OctaEQTests PluginSmokeTests.cpp)

target_include_directories(OctaEQTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:OctaEQ,INCLUDE_DIRECTORIES>)

target_compile_definitions(OctaEQTests
    PRIVATE
        $<TARGET_PROPERTY:OctaEQ,COMPILE_DEFINITIONS>)

target_link_libraries(OctaEQTests
    PRIVATE
        OctaEQ
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME OctaEQSmokeTests COMMAND OctaEQTests)
