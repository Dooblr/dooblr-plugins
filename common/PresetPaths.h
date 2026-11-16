#pragma once

#include <juce_core/juce_core.h>

/** VST3 user- and factory-preset locations for every dooblr plug-in.

    User (Steinberg VST3 convention, vendor "dooblr"):
      macOS:   ~/Library/Audio/Presets/dooblr/<plugin-name>/
      Windows: %USERPROFILE%\\Documents\\VST3 Presets\\dooblr\\<plugin-name>\\
      Linux:   ~/.vst3/presets/dooblr/<plugin-name>/

    Factory (when preset files are installed; most dooblr factory patches are compiled in):
      macOS:   /Library/Audio/Presets/dooblr/<plugin-name>/
      Windows: %PROGRAMDATA%\\VST3 Presets\\dooblr\\<plugin-name>\\
      Linux:   /usr/share/vst3/presets/dooblr/<plugin-name>/

    Until this helper, user presets lived in ~/Music/<plugin-name>/Presets. Those files are
    copied (never moved or deleted) into the new folder the first time a plug-in asks for it.

    Tests can redirect the native home / Music folders with DOOBLR_PRESET_HOME and
    DOOBLR_PRESET_MUSIC so they never touch the real user directories.
*/
namespace dooblr::preset_paths
{
    inline constexpr const char* vendor = "dooblr";
    inline constexpr const char* homeOverrideEnv = "DOOBLR_PRESET_HOME";
    inline constexpr const char* musicOverrideEnv = "DOOBLR_PRESET_MUSIC";
    inline constexpr const char* programDataOverrideEnv = "DOOBLR_PRESET_PROGRAMDATA";

    enum class Platform { macos, windows, linux };

    Platform nativePlatform();

    /** Home used by the native overloads: DOOBLR_PRESET_HOME, else the user's home. */
    juce::File nativeHome();
    /** Music folder used for the legacy location: DOOBLR_PRESET_MUSIC, else userMusicDirectory. */
    juce::File nativeMusic();
    /** Windows ProgramData (and tests): DOOBLR_PRESET_PROGRAMDATA, else PROGRAMDATA / C:\\ProgramData. */
    juce::File nativeProgramData();

    /** VST3 user-preset folder. Does not create it. */
    juce::File userDirectory (const juce::File& home, Platform platform, const juce::String& pluginName);

    /** Native user-preset folder: creates it and copies any leftover files from the legacy Music path. */
    juce::File userDirectory (const juce::String& pluginName);

    /** VST3 factory-preset folder. Does not create it. `programData` is used on Windows only. */
    juce::File factoryDirectory (Platform platform, const juce::File& programData, const juce::String& pluginName);
    juce::File factoryDirectory (const juce::String& pluginName);

    /** Previous user location: <musicDir>/<pluginName>/Presets */
    juce::File legacyUserDirectory (const juce::File& musicDir, const juce::String& pluginName);
    juce::File legacyUserDirectory (const juce::String& pluginName);

    struct CopyStats
    {
        int copied = 0;
        int skippedExisting = 0;
        int failed = 0;
    };

    /** Copy files matching `glob` (e.g. "*.dbass" or "*") from each source folder into dest.
        Creates dest. Does not overwrite existing dest files and does not delete the sources. */
    CopyStats copyPresets (const juce::File& dest, const juce::Array<juce::File>& sources, const juce::String& glob);

    /** Copy leftover files from the native legacy folder into the native user folder. Idempotent. */
    CopyStats migrateUserPresets (const juce::String& pluginName, const juce::String& glob);

    /** Migrates, then lists dest files, then leftover uniquely-named files still only in the
        legacy folder, sorted case-insensitively by the name without extension. */
    juce::Array<juce::File> listUserPresets (const juce::String& pluginName, const juce::String& glob);
}
