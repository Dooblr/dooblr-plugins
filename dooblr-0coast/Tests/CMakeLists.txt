# Console tests that exercise the real plugin processor, DSP and editor.
add_executable(Dooblr0CoastTests CoastTests.cpp)

target_include_directories(Dooblr0CoastTests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        $<TARGET_PROPERTY:Dooblr0Coast,INCLUDE_DIRECTORIES>)

target_compile_definitions(Dooblr0CoastTests
    PRIVATE
        $<TARGET_PROPERTY:Dooblr0Coast,COMPILE_DEFINITIONS>
        DOOBLR0COAST_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(Dooblr0CoastTests
    PRIVATE
        Dooblr0Coast
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME Dooblr0CoastTests COMMAND Dooblr0CoastTests)
