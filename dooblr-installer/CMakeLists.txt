cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helper tools.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(dooblr-installer VERSION 0.1.5 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows: link the C runtime statically so the .exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DOOBLRINSTALLER_BUILD_TESTS "Build the dooblr-installer tests" ON)

# Where the installer looks for the suite manifest. Repoint this (or pass --manifest <url>,
# or set DOOBLR_MANIFEST_URL) when the releases move, e.g. to a public repo or another host.
set(DOOBLRINSTALLER_MANIFEST_URL
    "https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json"
    CACHE STRING "Default manifest URL")

set(DOOBLRINSTALLER_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${DOOBLRINSTALLER_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# The download / verify / install engine and the window, compiled into both the app and the
# tests (JUCE modules are interface libraries, so they are linked per executable, not via a
# static library, which would compile them twice).
set(DOOBLRINSTALLER_CORE_SOURCES
    Source/Core/Manifest.cpp
    Source/Core/InstallEngine.cpp
    Source/Core/GitHub.cpp
    Source/Core/TokenStore.cpp
    Source/UI/Theme.cpp
    Source/UI/ProgressRing.cpp
    Source/UI/InstallerComponent.cpp)
if(APPLE)
    list(APPEND DOOBLRINSTALLER_CORE_SOURCES Source/Core/Clipboard.mm)
else()
    list(APPEND DOOBLRINSTALLER_CORE_SOURCES Source/Core/Clipboard.cpp)
endif()

function(dooblr_installer_setup target)
    target_sources(${target} PRIVATE ${DOOBLRINSTALLER_CORE_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        DOOBLRINSTALLER_VERSION="${PROJECT_VERSION}"
        DOOBLRINSTALLER_MANIFEST_URL="${DOOBLRINSTALLER_MANIFEST_URL}")
    target_link_libraries(${target} PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if(WIN32)
        target_link_libraries(${target} PRIVATE shell32 advapi32)   # advapi32: Credential Manager (saved GitHub token)
    elseif(APPLE)
        target_link_libraries(${target} PRIVATE "-framework Security")   # Keychain (saved GitHub token)
    endif()
endfunction()

juce_add_gui_app(DooblrInstaller
    PRODUCT_NAME   "dooblr installer"
    COMPANY_NAME   "dooblr"
    BUNDLE_ID      "com.dooblr.installer"
    VERSION        ${PROJECT_VERSION})


target_sources(DooblrInstaller PRIVATE Source/Main.cpp)
dooblr_installer_setup(DooblrInstaller)

if(DOOBLRINSTALLER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(DooblrInstallerTests PRODUCT_NAME "DooblrInstallerTests")
    target_sources(DooblrInstallerTests PRIVATE Tests/InstallerTests.cpp)
    dooblr_installer_setup(DooblrInstallerTests)
    # The tests spin the message loop while a background fetch runs (runDispatchLoopUntil).
    target_compile_definitions(DooblrInstallerTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    add_test(NAME DooblrInstallerTests COMMAND DooblrInstallerTests)
endif()
