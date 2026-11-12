# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(DooblrThroatTests ThroatTests.cpp)

target_include_directories(DooblrThroatTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:DooblrThroat,INCLUDE_DIRECTORIES>)

target_compile_definitions(DooblrThroatTests
    PRIVATE
        $<TARGET_PROPERTY:DooblrThroat,COMPILE_DEFINITIONS>
        DOOBLRTHROAT_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(DooblrThroatTests
    PRIVATE
        DooblrThroat
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE AND DEFINED DOOBLRTHROAT_ONNXRUNTIME_ROOT)
    set_property(TARGET DooblrThroatTests APPEND PROPERTY
        BUILD_RPATH "${DOOBLRTHROAT_ONNXRUNTIME_ROOT}/lib")
endif()

add_test(NAME DooblrThroatTests COMMAND DooblrThroatTests)
