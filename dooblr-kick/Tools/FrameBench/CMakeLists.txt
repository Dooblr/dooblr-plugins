# DooblrKickFrameBench: opens the real editor in a window and measures frame times while a
# knob and an envelope point are dragged continuously (see FrameBench.cpp). Not installed.

add_executable(DooblrKickFrameBench FrameBench.cpp)

target_include_directories(DooblrKickFrameBench
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrKick,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrKickFrameBench
    PRIVATE
        $<TARGET_PROPERTY:DooblrKick,COMPILE_DEFINITIONS>)

target_link_libraries(DooblrKickFrameBench
    PRIVATE
        DooblrKick
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
