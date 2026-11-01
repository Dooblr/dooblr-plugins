#include "Manifest.h"

namespace dooblr::installer
{
    int Version::compare (const juce::String& a, const juce::String& b)
    {
        if (a.trim().isEmpty() || b.trim().isEmpty())
            return (a.trim().isEmpty() ? 0 : 1) - (b.trim().isEmpty() ? 0 : 1);

        auto pa = juce::StringArray::fromTokens (a.trim(), ".", "");
        auto pb = juce::StringArray::fromTokens (b.trim(), ".", "");

        for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
        {
            const auto x = i < pa.size() ? pa[i].getIntValue() : 0;
            const auto y = i < pb.size() ? pb[i].getIntValue() : 0;

            if (x != y)
                return x < y ? -1 : 1;
        }

        return 0;
    }

    Platform currentPlatform() noexcept
    {
       #if JUCE_MAC
        return Platform::mac;
       #elif JUCE_WINDOWS
        return Platform::windows;
       #else
        return Platform::other;
       #endif
    }

    juce::String platformKey (Platform p)
    {
        switch (p)
        {
            case Platform::mac:     return "mac";
            case Platform::windows: return "windows";
            case Platform::other:   break;
        }
        return "other";
    }

    const Asset* Item::assetFor (Platform p) const
    {
        if (p == Platform::mac && mac.isValid())         return &mac;
        if (p == Platform::windows && windows.isValid()) return &windows;
        if (any.isValid())                               return &any;
        return nullptr;
    }

    const Item* Manifest::find (const juce::String& id) const
    {
        for (auto& i : items)
            if (i.id == id)
                return &i;
        return nullptr;
    }

    juce::URL resolveUrl (const juce::URL& base, const juce::String& s)
    {
        if (s.contains ("://"))
            return juce::URL (s);

        auto b = base.toString (false);
        return juce::URL (b.upToLastOccurrenceOf ("/", true, false) + s);
    }

    static Asset parseAsset (const juce::var& v, const juce::URL& base)
    {
        Asset a;
        if (! v.isObject())
            return a;

        const auto url = v.getProperty ("url", {}).toString();
        if (url.isNotEmpty())
            a.url = resolveUrl (base, url);
        a.sha256 = v.getProperty ("sha256", {}).toString().trim().toLowerCase();
        a.size = (juce::int64) v.getProperty ("size", 0);
        return a;
    }

    juce::Result Manifest::parse (const juce::String& json, const juce::URL& base, Manifest& out)
    {
        juce::var root;
        auto r = juce::JSON::parse (json, root);
        if (r.failed())
            return juce::Result::fail ("The manifest is not valid JSON: " + r.getErrorMessage());
        if (! root.isObject())
            return juce::Result::fail ("The manifest is not a JSON object.");

        Manifest m;
        m.schema = (int) root.getProperty ("schema", 0);
        if (m.schema != 1)
            return juce::Result::fail ("Unsupported manifest schema " + juce::String (m.schema) + " (this installer reads schema 1).");

        m.suite = root.getProperty ("suite", {}).toString();
        m.generated = root.getProperty ("generated", {}).toString();

        const auto inst = root.getProperty ("installer", {});
        m.installerVersion = inst.getProperty ("version", {}).toString();
        m.installerMac = parseAsset (inst.getProperty ("mac", {}), base);
        m.installerWindows = parseAsset (inst.getProperty ("windows", {}), base);

        const auto* items = root.getProperty ("items", {}).getArray();
        if (items == nullptr)
            return juce::Result::fail ("The manifest has no items.");

        for (auto& v : *items)
        {
            Item i;
            i.id = v.getProperty ("id", {}).toString();
            i.name = v.getProperty ("name", i.id).toString();
            i.version = v.getProperty ("version", {}).toString();
            i.description = v.getProperty ("description", {}).toString();
            i.bundle = v.getProperty ("bundle", {}).toString();
            i.parent = v.getProperty ("parent", {}).toString();
            i.optional = (bool) v.getProperty ("optional", false);
            const auto kind = v.getProperty ("kind", "vst3").toString();
            i.kind = kind == "m4l" ? Kind::m4l : Kind::vst3;
            i.mac = parseAsset (v.getProperty ("mac", {}), base);
            i.windows = parseAsset (v.getProperty ("windows", {}), base);
            i.any = parseAsset (v.getProperty ("any", {}), base);

            if (i.id.isEmpty() || i.version.isEmpty() || i.bundle.isEmpty())
                return juce::Result::fail ("A manifest item is missing its id, version or bundle.");
            if (i.bundle.containsAnyOf ("/\\") || i.bundle.startsWith ("."))
                return juce::Result::fail ("Bad bundle name in the manifest: " + i.bundle);
            if (i.kind == Kind::vst3 && ! i.bundle.endsWithIgnoreCase (".vst3"))
                return juce::Result::fail ("A plug-in bundle must end in .vst3: " + i.bundle);

            m.items.add (i);
        }

        out = std::move (m);
        return juce::Result::ok();
    }
}
