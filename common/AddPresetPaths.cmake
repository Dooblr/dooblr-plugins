# Compile the shared VST3 preset-path helper into a JUCE plug-in target.
# Usage, after juce_add_plugin(<Target> ...):
#   include("${CMAKE_CURRENT_SOURCE_DIR}/../common/AddPresetPaths.cmake")
#   dooblr_add_preset_paths(<Target>)

set(_dooblr_preset_paths_dir "${CMAKE_CURRENT_LIST_DIR}")

function(dooblr_add_preset_paths target)
    target_sources(${target} PRIVATE "${_dooblr_preset_paths_dir}/PresetPaths.cpp")
    target_include_directories(${target} PUBLIC "${_dooblr_preset_paths_dir}")
endfunction()
