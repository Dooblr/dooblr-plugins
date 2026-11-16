#include "PresetPaths.h"

#include <algorithm>

namespace dooblr::preset_paths
{
    Platform nativePlatform()
    {
       #if JUCE_MAC
        return Platform::macos;
       #elif JUCE_WINDOWS
        return Platform::windows;
       #else
        return Platform::linux;
       #endif
    }

    namespace
    {
        juce::File envFile (const char* name)
        {
            const auto value = juce::SystemStats::getEnvironmentVariable (name, {});
            if (value.isEmpty())
                return {};
            return juce::File (value);
        }

        juce::File child (juce::File dir, std::initializer_list<const char*> parts)
        {
            for (auto* part : parts)
                dir = dir.getChildFile (part);
            return dir;
        }
    }

    juce::File nativeHome()
    {
        if (auto f = envFile (homeOverrideEnv); f != juce::File())
            return f;
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    juce::File nativeMusic()
    {
        if (auto f = envFile (musicOverrideEnv); f != juce::File())
            return f;
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    juce::File nativeProgramData()
    {
        if (auto f = envFile (programDataOverrideEnv); f != juce::File())
            return f;
        const auto value = juce::SystemStats::getEnvironmentVariable ("PROGRAMDATA", {});
        if (value.isNotEmpty())
            return juce::File (value);
        return juce::File ("C:\\ProgramData");
    }

    juce::File userDirectory (const juce::File& home, Platform platform, const juce::String& pluginName)
    {
        switch (platform)
        {
            case Platform::macos:
                return child (home, { "Library", "Audio", "Presets", vendor }).getChildFile (pluginName);
            case Platform::windows:
                return child (home, { "Documents", "VST3 Presets", vendor }).getChildFile (pluginName);
            case Platform::linux:
                return child (home, { ".vst3", "presets", vendor }).getChildFile (pluginName);
        }
        jassertfalse;
        return {};
    }

    juce::File factoryDirectory (Platform platform, const juce::File& programData, const juce::String& pluginName)
    {
        switch (platform)
        {
            case Platform::macos:
                return juce::File ("/Library/Audio/Presets").getChildFile (vendor).getChildFile (pluginName);
            case Platform::windows:
                return child (programData, { "VST3 Presets", vendor }).getChildFile (pluginName);
            case Platform::linux:
                return juce::File ("/usr/share/vst3/presets").getChildFile (vendor).getChildFile (pluginName);
        }
        jassertfalse;
        return {};
    }

    juce::File factoryDirectory (const juce::String& pluginName)
    {
        return factoryDirectory (nativePlatform(), nativeProgramData(), pluginName);
    }

    juce::File legacyUserDirectory (const juce::File& musicDir, const juce::String& pluginName)
    {
        return musicDir.getChildFile (pluginName).getChildFile ("Presets");
    }

    juce::File legacyUserDirectory (const juce::String& pluginName)
    {
        return legacyUserDirectory (nativeMusic(), pluginName);
    }

    CopyStats copyPresets (const juce::File& dest, const juce::Array<juce::File>& sources, const juce::String& glob)
    {
        CopyStats stats;
        dest.createDirectory();

        for (const auto& source : sources)
        {
            if (source == dest || ! source.isDirectory())
                continue;

            for (const auto& file : source.findChildFiles (juce::File::findFiles, false, glob))
            {
                const auto target = dest.getChildFile (file.getFileName());
                if (target.existsAsFile())
                {
                    ++stats.skippedExisting;
                    continue;
                }

                if (file.copyFileTo (target))
                    ++stats.copied;
                else
                    ++stats.failed;
            }
        }

        return stats;
    }

    CopyStats migrateUserPresets (const juce::String& pluginName, const juce::String& glob)
    {
        const auto dest = userDirectory (nativeHome(), nativePlatform(), pluginName);
        dest.createDirectory();
        return copyPresets (dest, { legacyUserDirectory (pluginName) }, glob);
    }

    juce::File userDirectory (const juce::String& pluginName)
    {
        migrateUserPresets (pluginName, "*");
        auto dir = userDirectory (nativeHome(), nativePlatform(), pluginName);
        dir.createDirectory();
        return dir;
    }

    juce::Array<juce::File> listUserPresets (const juce::String& pluginName, const juce::String& glob)
    {
        migrateUserPresets (pluginName, glob);

        juce::Array<juce::File> files;
        juce::StringArray names;

        const auto addUnique = [&] (const juce::File& dir)
        {
            if (! dir.isDirectory())
                return;

            for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, glob))
            {
                if (names.contains (file.getFileName()))
                    continue;
                names.add (file.getFileName());
                files.add (file);
            }
        };

        addUnique (userDirectory (nativeHome(), nativePlatform(), pluginName));
        addUnique (legacyUserDirectory (pluginName));

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
        });
        return files;
    }
}
