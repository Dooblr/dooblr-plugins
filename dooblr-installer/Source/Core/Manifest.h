#pragma once

#include <juce_core/juce_core.h>

namespace dooblr::installer
{
    /** Dotted numeric versions ("0.9.5", "0.10.0"); a missing part counts as 0. */
    struct Version
    {
        /** < 0 when a is older than b, 0 when equal, > 0 when newer. Empty sorts oldest. */
        static int compare (const juce::String& a, const juce::String& b);
    };

    enum class Platform { mac, windows, other };
    Platform currentPlatform() noexcept;
    juce::String platformKey (Platform);

    /** One downloadable zip. */
    struct Asset
    {
        juce::URL url;
        juce::String sha256;   // lowercase hex
        juce::int64 size = 0;  // bytes, 0 = unknown
        bool isValid() const   { return url.toString (false).isNotEmpty() && sha256.length() == 64; }
    };

    enum class Kind { vst3, m4l };

    /** A plug-in (a .vst3 bundle) or a Max for Live device folder. */
    struct Item
    {
        juce::String id, name, version, description;
        juce::String bundle;   // "dooblr-q.vst3" for a plug-in; the device folder name for M4L
        juce::String parent;   // the plug-in an optional add-on belongs to (the prophet panel)
        Kind kind = Kind::vst3;
        bool optional = false;
        Asset mac, windows, any;

        /** The asset for this platform (or the platform-independent one), nullptr when none. */
        const Asset* assetFor (Platform) const;
    };

    struct Manifest
    {
        int schema = 0;
        juce::String suite, generated;
        juce::String installerVersion;
        Asset installerMac, installerWindows;
        juce::Array<Item> items;

        const Item* find (const juce::String& id) const;

        /** Parses manifest JSON. Relative asset URLs resolve against baseUrl (the manifest's own
            URL), so a manifest and its zips can move together to any host or file:// folder. */
        static juce::Result parse (const juce::String& json, const juce::URL& baseUrl, Manifest& out);
    };

    /** "dooblr-q-0.9.5-mac.zip" against "https://host/a/dooblr-manifest.json" -> "https://host/a/dooblr-q-0.9.5-mac.zip". */
    juce::URL resolveUrl (const juce::URL& base, const juce::String& relativeOrAbsolute);
}
