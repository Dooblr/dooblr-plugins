cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-preset-paths VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DOOBLRPRESETPATHS_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRPRESETPATHS_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_console_app(DooblrPresetPathTests
    PRODUCT_NAME "DooblrPresetPathTests"
    COMPANY_NAME "dooblr")

target_sources(DooblrPresetPathTests
    PRIVATE
        PresetPaths.cpp
        Tests/PresetPathTests.cpp)

target_include_directories(DooblrPresetPathTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(DooblrPresetPathTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(DooblrPresetPathTests
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME DooblrPresetPathTests COMMAND DooblrPresetPathTests)
