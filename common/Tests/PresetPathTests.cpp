/*
    dooblr shared VST3 preset-path tests.

    Path construction is mocked with fake home directories (spaces and unicode) for macOS
    and Windows. Migration copies files in a temp tree; the real Music / Library folders
    are never read or written (DOOBLR_PRESET_HOME / DOOBLR_PRESET_MUSIC).
*/

#include "PresetPaths.h"

#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <string>

#if JUCE_WINDOWS
 #include <stdlib.h>
#else
 #include <unistd.h>
#endif

namespace
{
    using namespace dooblr::preset_paths;

    int passed = 0, failed = 0;

    void check (bool ok, const std::string& what)
    {
        if (ok) { ++passed; return; }
        ++failed;
        std::cout << "    FAILED: " << what << "\n";
    }

    void setEnv (const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
       #else
        setenv (name, value.toRawUTF8(), 1);
       #endif
    }

    void clearEnv (const char* name)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, "");
       #else
        unsetenv (name);
       #endif
    }

    struct EnvGuard
    {
        EnvGuard() = default;
        ~EnvGuard()
        {
            clearEnv (homeOverrideEnv);
            clearEnv (musicOverrideEnv);
            clearEnv (programDataOverrideEnv);
        }
        EnvGuard (const EnvGuard&) = delete;
        EnvGuard& operator= (const EnvGuard&) = delete;
    };

    juce::File uniqueTemp (const juce::String& name)
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("dooblr-preset-path-tests")
                             .getChildFile (name + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    void testMacUserPath()
    {
        const auto home = juce::File ("/Users/Dan Feinstein");
        const auto got = userDirectory (home, Platform::macos, "dooblr-bass");
        const auto expected = home.getChildFile ("Library").getChildFile ("Audio").getChildFile ("Presets")
                                  .getChildFile ("dooblr").getChildFile ("dooblr-bass");
        check (got == expected, "macOS user presets: ~/Library/Audio/Presets/dooblr/<plugin>");
        check (got.getFileName() == "dooblr-bass", "macOS folder is the plugin name");
    }

    void testWindowsUserPath()
    {
        const auto home = juce::File ("/Users/Dan Feinstein");
        const auto got = userDirectory (home, Platform::windows, "dooblr-kick");
        const auto expected = home.getChildFile ("Documents").getChildFile ("VST3 Presets")
                                  .getChildFile ("dooblr").getChildFile ("dooblr-kick");
        check (got == expected, "Windows user presets: Documents/VST3 Presets/dooblr/<plugin>");
    }

    void testLinuxUserPath()
    {
        const auto home = juce::File (juce::String ("/home/") + juce::String::fromUTF8 ("Jos\xc3\xa9 P\xc3\xa9rez"));
        const auto got = userDirectory (home, Platform::linux, "dooblr-vocals");
        const auto expected = home.getChildFile (".vst3").getChildFile ("presets")
                                  .getChildFile ("dooblr").getChildFile ("dooblr-vocals");
        check (got == expected, "Linux user presets: ~/.vst3/presets/dooblr/<plugin>");
    }

    void testUnicodeAndSpaces()
    {
        const auto jose = juce::String::fromUTF8 ("Jos\xc3\xa9");
        const auto home = juce::File ("/Users/Dan Feinstein/" + jose + "'s Studio");
        const auto mac = userDirectory (home, Platform::macos, "dooblr-soothe");
        check (mac.getFullPathName().contains ("Dan Feinstein"), "mac path keeps spaces in the home");
        check (mac.getFullPathName().contains (jose), "mac path keeps unicode in the home");
        check (mac.getChildFile ("Warm Patch.dsoothe").getFileNameWithoutExtension() == "Warm Patch",
               "preset file names may contain spaces");

        const auto win = userDirectory (home, Platform::windows, "dooblr-feedback");
        check (win.getFullPathName().contains ("VST3 Presets"), "Windows folder name has a space");
        check (win.getFullPathName().contains (jose), "Windows path keeps unicode in the home");
    }

    void testFactoryPaths()
    {
        const auto programData = juce::File ("C:/ProgramData");
        const auto mac = factoryDirectory (Platform::macos, programData, "dooblr-prophet");
        check (mac == juce::File ("/Library/Audio/Presets").getChildFile ("dooblr").getChildFile ("dooblr-prophet"),
               "macOS factory: /Library/Audio/Presets/dooblr/<plugin>");

        const auto win = factoryDirectory (Platform::windows, programData, "dooblr-prophet");
        check (win == programData.getChildFile ("VST3 Presets").getChildFile ("dooblr").getChildFile ("dooblr-prophet"),
               "Windows factory: ProgramData/VST3 Presets/dooblr/<plugin>");

        const auto linux = factoryDirectory (Platform::linux, programData, "dooblr-prophet");
        check (linux == juce::File ("/usr/share/vst3/presets").getChildFile ("dooblr").getChildFile ("dooblr-prophet"),
               "Linux factory: /usr/share/vst3/presets/dooblr/<plugin>");
    }

    void testLegacyPath()
    {
        const auto music = juce::File ("/Users/Dan Feinstein/Music");
        const auto got = legacyUserDirectory (music, "dooblr-m");
        check (got == music.getChildFile ("dooblr-m").getChildFile ("Presets"),
               "legacy user presets: <Music>/dooblr-<plugin>/Presets");
    }

    void testCopyAndMigrate()
    {
        const auto root = uniqueTemp ("migrate");
        const auto oldDir = legacyUserDirectory (root.getChildFile ("Music"), "dooblr-bass");
        const auto newDir = userDirectory (root, Platform::macos, "dooblr-bass");

        check (oldDir.createDirectory(), "create legacy folder (parents included)");
        check (oldDir.getChildFile ("Warm Patch.dbass").replaceWithText ("warm"), "write space-named preset");
        check (oldDir.getChildFile ("日本語.dbass").replaceWithText ("jp"), "write unicode-named preset");
        check (oldDir.getChildFile ("notes.txt").replaceWithText ("ignore-me"), "write a non-preset file");

        const auto stats = copyPresets (newDir, { oldDir }, "*.dbass");
        check (stats.copied == 2 && stats.failed == 0, "copied two .dbass files, not the txt");
        check (newDir.getChildFile ("Warm Patch.dbass").loadFileAsString() == "warm", "space-named file arrived");
        check (newDir.getChildFile ("日本語.dbass").loadFileAsString() == "jp", "unicode-named file arrived");
        check (! newDir.getChildFile ("notes.txt").existsAsFile(), "glob skipped the txt");
        check (oldDir.getChildFile ("Warm Patch.dbass").existsAsFile()
               && oldDir.getChildFile ("日本語.dbass").existsAsFile(),
               "legacy files were copied, not moved");

        newDir.getChildFile ("Warm Patch.dbass").replaceWithText ("newer");
        const auto again = copyPresets (newDir, { oldDir }, "*.dbass");
        check (again.copied == 0 && again.skippedExisting == 2, "second pass does not overwrite");
        check (newDir.getChildFile ("Warm Patch.dbass").loadFileAsString() == "newer",
               "existing dest file is left alone");

        const auto emptyDest = root.getChildFile ("empty-dest");
        const auto emptyStats = copyPresets (emptyDest, { root.getChildFile ("no-such-folder") }, "*");
        check (emptyDest.isDirectory(), "dest folder is created even when there is nothing to copy");
        check (emptyStats.copied == 0 && emptyStats.failed == 0, "missing source is a no-op, not a failure");

        root.deleteRecursively();
    }

    void testListPrefersNewThenLeftovers()
    {
        EnvGuard guard;
        const auto root = uniqueTemp ("list");
        setEnv (homeOverrideEnv, root.getFullPathName());
        setEnv (musicOverrideEnv, root.getChildFile ("Music").getFullPathName());

        const auto oldDir = legacyUserDirectory ("dooblr-throat");
        oldDir.createDirectory();
        oldDir.getChildFile ("Alpha.dthroat").replaceWithText ("old-alpha");
        oldDir.getChildFile ("OnlyOld.dthroat").replaceWithText ("only-old");

        const auto files = listUserPresets ("dooblr-throat", "*.dthroat");
        const auto dest = userDirectory (nativeHome(), nativePlatform(), "dooblr-throat");
        check (dest.getChildFile ("Alpha.dthroat").existsAsFile(), "listUserPresets migrates into the new folder");
        check (oldDir.getChildFile ("Alpha.dthroat").existsAsFile(), "legacy Alpha is still there");
        check (files.size() == 2, "two unique user presets after migrate");

        bool sawDestAlpha = false, sawOldOnly = false;
        for (const auto& f : files)
        {
            if (f.getFileName() == "Alpha.dthroat")
            {
                check (f == dest.getChildFile ("Alpha.dthroat"), "duplicate name prefers the new folder");
                sawDestAlpha = true;
            }
            if (f.getFileName() == "OnlyOld.dthroat")
                sawOldOnly = f.existsAsFile();
        }
        check (sawDestAlpha && sawOldOnly, "new copy and leftover unique old name both show up");
        check (files[0].getFileNameWithoutExtension() == "Alpha", "list is sorted by name");

        root.deleteRecursively();
    }

    void testNativeOverrideCreatesFolder()
    {
        EnvGuard guard;
        const auto root = uniqueTemp ("native");
        setEnv (homeOverrideEnv, root.getFullPathName());
        setEnv (musicOverrideEnv, root.getChildFile ("Music").getFullPathName());

        const auto dir = userDirectory ("dooblr-sub");
        const auto expected = userDirectory (root, nativePlatform(), "dooblr-sub");
        check (dir == expected, "native userDirectory uses the override home");
        check (dir.isDirectory(), "native userDirectory creates the folder");

        check (factoryDirectory (Platform::windows, juce::File ("D:/ProgramData"), "dooblr-q")
                   == juce::File ("D:/ProgramData").getChildFile ("VST3 Presets").getChildFile ("dooblr").getChildFile ("dooblr-q"),
               "factory Windows path uses the supplied ProgramData");

        root.deleteRecursively();
    }

    void testNativePlatformMatchesHost()
    {
       #if JUCE_MAC
        check (nativePlatform() == Platform::macos, "host platform is macOS");
       #elif JUCE_WINDOWS
        check (nativePlatform() == Platform::windows, "host platform is Windows");
       #else
        check (nativePlatform() == Platform::linux, "host platform is Linux");
       #endif
    }

    struct Case { const char* name; void (*run)(); };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const Case cases[] {
        { "macOS user path",                    testMacUserPath },
        { "Windows user path",                  testWindowsUserPath },
        { "Linux user path",                    testLinuxUserPath },
        { "unicode and spaces",                 testUnicodeAndSpaces },
        { "factory paths",                      testFactoryPaths },
        { "legacy Music path",                  testLegacyPath },
        { "copy / migrate (no overwrite)",      testCopyAndMigrate },
        { "list prefers new, keeps leftovers",  testListPrefersNewThenLeftovers },
        { "native override creates folder",     testNativeOverrideCreatesFolder },
        { "native platform",                    testNativePlatformMatchesHost },
    };

    int failedTests = 0;
    for (const auto& c : cases)
    {
        const auto beforeFail = failed, beforePass = passed;
        std::cout << c.name << "\n";
        c.run();
        const auto bad = failed > beforeFail;
        std::cout << "  [" << (bad ? "FAIL" : "PASS") << "] "
                  << (passed - beforePass) << " checks";
        if (bad)
            std::cout << ", " << (failed - beforeFail) << " failed";
        std::cout << "\n";
        failedTests += bad ? 1 : 0;
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << (int) std::size (cases) << " tests (" << failedTests << " failed)\n";
    return failed == 0 ? 0 : 1;
}
