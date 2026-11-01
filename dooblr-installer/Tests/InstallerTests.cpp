// Console tests for the dooblr installer engine and window. Everything is written under a temp
// folder; the real plug-in folders are only read. DOOBLRINSTALLER_SNAPSHOT=<prefix> also renders
// the window pages to <prefix>-<page>.png.

#include "UI/InstallerComponent.h"

#include <cstdlib>
#include <iostream>

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
#endif

using namespace dooblr::installer;

namespace
{
    int checks = 0, failures = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) { ++failures; std::cout << "  [FAIL] " << what << std::endl; }
    }

    void note (const juce::String& s) { std::cout << "    " << s << std::endl; }

    juce::File tempRoot()
    {
        static const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("dooblr-installer-tests-" + juce::String (juce::Time::currentTimeMillis()));
        return root;
    }

    juce::String plist (const juce::String& version)
    {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>\n"
               "<key>CFBundleName</key><string>x</string>\n"
               "<key>CFBundleShortVersionString</key><string>" + version + "</string>\n"
               "<key>CFBundleVersion</key><string>" + version + "</string>\n</dict></plist>\n";
    }

    /** A fake .vst3 bundle (Info.plist, an "executable", moduleinfo.json). */
    juce::File makeBundle (const juce::File& dir, const juce::String& name, const juce::String& version, bool withPlist = true)
    {
        const auto b = dir.getChildFile (name + ".vst3");
        b.getChildFile ("Contents/MacOS").createDirectory();
        b.getChildFile ("Contents/Resources").createDirectory();
        if (withPlist)
            b.getChildFile ("Contents/Info.plist").replaceWithText (plist (version));
        b.getChildFile ("Contents/MacOS/" + name).replaceWithText ("binary " + version);
        b.getChildFile ("Contents/Resources/moduleinfo.json")
            .replaceWithText ("{\n  \"Name\": \"" + name + "\",\n  \"Version\": \"" + version + "\",\n  \"Factory Info\": {\n    \"Flags\": {\n      \"Unicode\": true,\n    },\n  },\n}\n");
        return b;
    }

    /** Zips folder (keeping its name as the top entry), plus optional extra entries. */
    juce::File zipFolder (const juce::File& folder, const juce::File& zip, const juce::StringArray& extraEntries = {})
    {
        juce::ZipFile::Builder builder;
        for (auto& f : folder.findChildFiles (juce::File::findFiles, true))
            builder.addFile (f, 9, folder.getFileName() + "/" + f.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
        for (auto& e : extraEntries)
        {
            const auto t = tempRoot().getChildFile ("extra.txt");
            t.replaceWithText ("x");
            builder.addFile (t, 9, e);
        }
        zip.deleteFile();
        juce::FileOutputStream out (zip);
        builder.writeToStream (out, nullptr);
        return zip;
    }

    juce::var assetVar (const juce::File& zip, const juce::String& shaOverride = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("url", zip.getFileName());
        o->setProperty ("sha256", shaOverride.isNotEmpty() ? shaOverride : sha256OfFile (zip));
        o->setProperty ("size", zip.getSize());
        return juce::var (o);
    }

    juce::var itemVar (const juce::String& id, const juce::String& version, const juce::String& bundle, const juce::String& kind,
                       const juce::var& asset, const juce::String& parent = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("name", id);
        o->setProperty ("version", version);
        o->setProperty ("description", "test item");
        o->setProperty ("bundle", bundle);
        o->setProperty ("kind", kind);
        if (parent.isNotEmpty()) { o->setProperty ("parent", parent); o->setProperty ("optional", true); }
        o->setProperty (kind == "m4l" ? "any" : platformKey (currentPlatform()), asset);
        return juce::var (o);
    }

    juce::File writeManifest (const juce::File& dir, const juce::Array<juce::var>& items)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("schema", 1);
        o->setProperty ("suite", "dooblr");
        o->setProperty ("generated", "test");
        juce::Array<juce::var> a (items);
        o->setProperty ("items", a);
        const auto f = dir.getChildFile ("dooblr-manifest.json");
        f.replaceWithText (juce::JSON::toString (juce::var (o)));
        return f;
    }

    juce::Result loadManifest (const juce::File& f, Manifest& m)
    {
        const juce::URL url (f);
        juce::String text;
        auto r = fetchText (url, text);
        return r.failed() ? r : Manifest::parse (text, url, m);
    }

    //==========================================================================
    void testVersions()
    {
        std::cout << "versions" << std::endl;
        check (Version::compare ("0.9.5", "0.9.5") == 0, "equal");
        check (Version::compare ("0.9.5", "0.10.0") < 0, "0.9.5 < 0.10.0 (numeric, not text)");
        check (Version::compare ("1.0", "0.99.9") > 0, "1.0 > 0.99.9");
        check (Version::compare ("0.3", "0.3.0") == 0, "missing part is 0");
        check (Version::compare ("", "0.1.0") < 0, "empty is oldest");
        check (Version::compare ("0.3.6", "0.3.5") > 0, "patch");
    }

    void testManifest()
    {
        std::cout << "manifest" << std::endl;
        const juce::URL base ("https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json");
        check (resolveUrl (base, "dooblr-q-0.9.5-mac.zip").toString (false)
                   == "https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-q-0.9.5-mac.zip", "relative asset URL");
        check (resolveUrl (base, "https://example.com/x.zip").toString (false) == "https://example.com/x.zip", "absolute asset URL");

        const juce::String good = R"({"schema":1,"suite":"dooblr","generated":"t","installer":{"version":"0.2.0","mac":{"url":"i.dmg","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":5}},
            "items":[{"id":"dooblr-q","name":"dooblr-q","kind":"vst3","version":"0.9.5","bundle":"dooblr-q.vst3",
                      "mac":{"url":"q.zip","sha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","size":10}},
                     {"id":"p-m4l","kind":"m4l","version":"0.3.2","bundle":"dooblr-prophet-panel","parent":"dooblr-prophet","optional":true,
                      "any":{"url":"p.zip","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}}]})";
        Manifest m;
        auto r = Manifest::parse (good, base, m);
        check (r.wasOk(), "parses: " + r.getErrorMessage());
        check (m.items.size() == 2 && m.installerVersion == "0.2.0", "items and installer version");
        check (m.items[0].mac.sha256 == juce::String::repeatedString ("a", 64), "sha256 lower-cased");
        check (m.items[0].assetFor (Platform::mac) != nullptr && m.items[0].assetFor (Platform::windows) == nullptr, "per-platform asset");
        check (m.items[1].assetFor (Platform::windows) != nullptr && m.items[1].optional && m.items[1].parent == "dooblr-prophet", "any-platform add-on");

        check (Manifest::parse ("{\"schema\":2,\"items\":[]}", base, m).failed(), "rejects an unknown schema");
        check (Manifest::parse ("not json", base, m).failed(), "rejects bad JSON");
        check (Manifest::parse (R"({"schema":1,"items":[{"id":"x","version":"1","bundle":"../evil.vst3"}]})", base, m).failed(), "rejects a path in bundle");
        check (Manifest::parse (R"({"schema":1,"items":[{"id":"x","version":"1","bundle":"x.app"}]})", base, m).failed(), "rejects a non-.vst3 plug-in");
    }

    void testSha256AndVersions()
    {
        std::cout << "sha256 and installed versions" << std::endl;
        const auto f = tempRoot().getChildFile ("abc.txt");
        f.replaceWithText ("abc");
        note ("sha256(abc.txt) = " + sha256OfFile (f) + ", size " + juce::String (f.getSize()));
        check (sha256OfFile (f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(\"abc\") test vector");

        const auto dir = tempRoot().getChildFile ("versions");
        check (readBundleVersion (makeBundle (dir, "a", "1.2.3")) == "1.2.3", "Info.plist version");
        check (readBundleVersion (makeBundle (dir, "b", "0.4.1", false)) == "0.4.1", "moduleinfo.json version (JSON5 trailing commas)");
        check (readBundleVersion (dir.getChildFile ("missing.vst3")).isEmpty(), "no bundle, no version");

        // The real plug-ins on this computer (read only).
        for (auto d : { Locations::systemVst3Dir(), Locations::userVst3Dir() })
            for (auto& b : d.findChildFiles (juce::File::findDirectories, false, "dooblr-*.vst3"))
            {
                const auto v = readBundleVersion (b);
                note (b.getFullPathName() + ": " + (v.isNotEmpty() ? v : "(no version)"));
                check (v.isNotEmpty(), "reads the installed version of " + b.getFileName());
            }
    }

    void testInstallFlow()
    {
        std::cout << "install, update, checksum rejection (temp folders)" << std::endl;
        const auto root = tempRoot().getChildFile ("flow");
        const auto release = root.getChildFile ("release");
        const auto vst3 = root.getChildFile ("VST3 test folder");
        const auto m4l = root.getChildFile ("User Library/dooblr");
        release.createDirectory();

        Locations loc;
        loc.vst3Dir = vst3;
        loc.m4lDir = m4l;
        loc.overridden = true;
        Receipts receipts (root.getChildFile ("receipts.json"));

        // Release 1: one plug-in and one M4L device folder.
        const auto src = root.getChildFile ("src1");
        const auto zipA = zipFolder (makeBundle (src, "dooblr-test", "0.2.0"), release.getChildFile ("dooblr-test-0.2.0.zip"));
        const auto dev = src.getChildFile ("dooblr-device");
        dev.createDirectory();
        dev.getChildFile ("dooblr-device.amxd").replaceWithText ("ampf device");
        dev.getChildFile ("dooblr-device-ui.js").replaceWithText ("// js");
        const auto zipM = zipFolder (dev, release.getChildFile ("dooblr-device-0.1.0.zip"));
        note ("device zip: " + juce::String (juce::ZipFile (zipM).getNumEntries()) + " entries, " + juce::String (zipM.getSize()) + " bytes");

        auto mf = writeManifest (release, { itemVar ("dooblr-test", "0.2.0", "dooblr-test.vst3", "vst3", assetVar (zipA)),
                                            itemVar ("dooblr-device", "0.1.0", "dooblr-device", "m4l", assetVar (zipM), "dooblr-test") });
        Manifest m;
        auto r = loadManifest (mf, m);
        check (r.wasOk(), "loads a file:// manifest: " + r.getErrorMessage());
        if (r.failed()) return;
        check (m.items[0].assetFor (currentPlatform())->url.isLocalFile(), "relative zip resolves to a local file");

        const auto* item = m.find ("dooblr-test");
        check (statusFor (*item, findInstalled (*item, loc, receipts)) == Status::notInstalled, "status before: Install");

        juce::StringArray lines;
        auto report = installItems (m, { "dooblr-test", "dooblr-device" }, loc, receipts, [&] (const juce::String& s) { lines.add (s); }, {});
        check (report.failed.isEmpty() && report.done.size() == 2, "installs both: " + report.failed.joinIntoString ("; "));
        const auto installed = vst3.getChildFile ("dooblr-test.vst3");
        check (readBundleVersion (installed) == "0.2.0", "installed bundle has version 0.2.0");
        check (installed.getChildFile ("Contents/MacOS/dooblr-test").loadFileAsString() == "binary 0.2.0", "binary content intact");
       #if JUCE_MAC
        check (access (installed.getChildFile ("Contents/MacOS/dooblr-test").getFullPathName().toRawUTF8(), X_OK) == 0, "binary is executable after unzip");
       #endif
        check (m4l.getChildFile ("dooblr-device/dooblr-device-ui.js").existsAsFile(), "M4L device folder installed with its js");
        check (statusFor (*item, findInstalled (*item, loc, receipts)) == Status::upToDate, "status after: Up to date");
        check (findInstalled (*m.find ("dooblr-device"), loc, receipts).version == "0.1.0", "M4L version from the receipt");
        check (lines.joinIntoString ("\n").contains ("sha256 OK"), "log reports the checksum");

        // Release 2: an update replaces the old bundle (no leftovers).
        installed.getChildFile ("Contents/Resources/old-only.txt").replaceWithText ("from 0.2.0");
        const auto src2 = root.getChildFile ("src2");
        const auto zipB = zipFolder (makeBundle (src2, "dooblr-test", "0.3.0"), release.getChildFile ("dooblr-test-0.3.0.zip"));
        mf = writeManifest (release, { itemVar ("dooblr-test", "0.3.0", "dooblr-test.vst3", "vst3", assetVar (zipB)) });
        r = loadManifest (mf, m);
        item = m.find ("dooblr-test");
        check (statusFor (*item, findInstalled (*item, loc, receipts)) == Status::updateAvailable, "status with a newer release: Update");
        report = installItems (m, { "dooblr-test" }, loc, receipts, {}, {});
        check (report.failed.isEmpty() && readBundleVersion (installed) == "0.3.0", "update installs 0.3.0");
        check (! installed.getChildFile ("Contents/Resources/old-only.txt").exists(), "update replaces the old bundle completely");

        // Corrupted download: one byte changed, same size -> rejected, installed copy untouched.
        const auto src3 = root.getChildFile ("src3");
        const auto zipC = zipFolder (makeBundle (src3, "dooblr-test", "0.4.0"), release.getChildFile ("dooblr-test-0.4.0.zip"));
        const auto goodSha = sha256OfFile (zipC);
        const auto goodSize = zipC.getSize();
        {
            juce::MemoryBlock mb;
            zipC.loadFileAsData (mb);
            static_cast<char*> (mb.getData())[mb.getSize() / 2] ^= 0x5a;
            zipC.replaceWithData (mb.getData(), mb.getSize());
        }
        mf = writeManifest (release, { itemVar ("dooblr-test", "0.4.0", "dooblr-test.vst3", "vst3", assetVar (zipC, goodSha)) });
        r = loadManifest (mf, m);
        check (zipC.getSize() == goodSize, "corrupted file keeps its size (so only the hash can catch it)");
        lines.clear();
        report = installItems (m, { "dooblr-test" }, loc, receipts, [&] (const juce::String& s) { lines.add (s); }, {});
        check (report.done.isEmpty() && report.failed.size() == 1 && report.failed[0].contains ("checksum mismatch"),
               "corrupted zip rejected by sha256: " + report.failed.joinIntoString ("; "));
        check (readBundleVersion (installed) == "0.3.0", "installed 0.3.0 untouched after the rejected download");
        note ("rejected: " + report.failed.joinIntoString ("; "));

        // Truncated download -> size mismatch.
        {
            juce::MemoryBlock mb;
            zipC.loadFileAsData (mb);
            zipC.replaceWithData (mb.getData(), mb.getSize() - 100);
        }
        report = installItems (m, { "dooblr-test" }, loc, receipts, {}, {});
        check (report.failed.size() == 1 && report.failed[0].contains ("size"), "truncated zip rejected by size");

        // A zip with an entry outside the bundle is refused even with a matching hash.
        const auto src4 = root.getChildFile ("src4");
        const auto zipD = zipFolder (makeBundle (src4, "dooblr-test", "0.5.0"), release.getChildFile ("dooblr-test-0.5.0.zip"), { "../escape.txt" });
        mf = writeManifest (release, { itemVar ("dooblr-test", "0.5.0", "dooblr-test.vst3", "vst3", assetVar (zipD)) });
        r = loadManifest (mf, m);
        report = installItems (m, { "dooblr-test" }, loc, receipts, {}, {});
        check (report.failed.size() == 1 && report.failed[0].contains ("unexpected entry"), "zip with a stray entry rejected");
        check (! root.getChildFile ("escape.txt").exists() && readBundleVersion (installed) == "0.3.0", "nothing written outside, 0.3.0 kept");

        // Missing download.
        release.getChildFile ("dooblr-test-0.5.0.zip").deleteFile();
        report = installItems (m, { "dooblr-test" }, loc, receipts, {}, {});
        check (report.failed.size() == 1 && report.failed[0].contains ("not found"), "missing zip reported");

        // Cancelling from the progress callback stops before anything is placed.
        mf = writeManifest (release, { itemVar ("dooblr-test", "0.2.0", "dooblr-test.vst3", "vst3", assetVar (zipA)) });
        r = loadManifest (mf, m);
        report = installItems (m, { "dooblr-test" }, loc, receipts, {}, [] (int, int, double) { return false; });
        check (report.cancelled && report.done.isEmpty() && readBundleVersion (installed) == "0.3.0", "cancel installs nothing");
    }

    //==========================================================================
    /** The real suite list, with a temp folder holding some older copies. */
    void testWindow()
    {
        std::cout << "window" << std::endl;
        const auto root = tempRoot().getChildFile ("ui");
        const auto vst3 = root.getChildFile ("VST3");
        const auto m4l = root.getChildFile ("User Library/dooblr");
        makeBundle (vst3, "dooblr-q", "0.9.5");
        makeBundle (vst3, "dooblr-kick", "0.3.4");
        makeBundle (vst3, "dooblr-prophet", "0.3.1");
        makeBundle (vst3, "dooblr-soothe", "0.1.2");
        m4l.getChildFile ("dooblr-chords").createDirectory();
        m4l.getChildFile ("dooblr-prophet").createDirectory();
        m4l.getChildFile ("dooblr-prophet/dooblr-prophet-ui.js").replaceWithText ("// panel\nvar UI_VERSION = \"0.3.2\"; // version\n");

        const auto sha = juce::String::repeatedString ("0", 64);
        juce::Array<juce::var> items;
        auto add = [&] (juce::String id, juce::String v, juce::String desc, juce::String kind, juce::String bundle, juce::String parent = {})
        {
            auto* a = new juce::DynamicObject();
            a->setProperty ("url", id + ".zip");
            a->setProperty ("sha256", sha);
            auto it = itemVar (id, v, bundle, kind, juce::var (a), parent);
            it.getDynamicObject()->setProperty ("description", desc);
            items.add (it);
        };
        add ("dooblr-q", "0.9.5", "Dynamic parametric EQ", "vst3", "dooblr-q.vst3");
        add ("dooblr-kick", "0.3.6", "Layered kick drum designer (instrument)", "vst3", "dooblr-kick.vst3");
        add ("dooblr-vocals", "0.2.0", "Pitch correction and vocal chain", "vst3", "dooblr-vocals.vst3");
        add ("dooblr-prophet", "0.3.1", "Analog-modelled polyphonic synth (instrument)", "vst3", "dooblr-prophet.vst3");
        add ("dooblr-prophet-panel", "0.3.2", "Max for Live panel for dooblr-prophet (optional)", "m4l", "dooblr-prophet", "dooblr-prophet");
        add ("dooblr-soothe", "0.1.3", "Dynamic resonance reducer", "vst3", "dooblr-soothe.vst3");
        add ("dooblr-feedback", "0.1.1", "Automatic live feedback suppressor", "vst3", "dooblr-feedback.vst3");
        add ("dooblr-chords", "0.3.0", "Chord progression generator (Max for Live MIDI effect)", "m4l", "dooblr-chords");
        const auto mf = writeManifest (root, items);

        Manifest m;
        auto r = loadManifest (mf, m);
        check (r.wasOk(), "suite manifest loads");

        const auto receiptsPath = root.getChildFile ("receipts.json").getFullPathName();
       #if JUCE_WINDOWS
        _putenv_s ("DOOBLR_INSTALLER_RECEIPTS", receiptsPath.toRawUTF8());
       #else
        setenv ("DOOBLR_INSTALLER_RECEIPTS", receiptsPath.toRawUTF8(), 1);
       #endif
        Locations loc;
        loc.vst3Dir = vst3;
        loc.m4lDir = m4l;
        loc.overridden = true;

        ui::InstallerComponent c (juce::URL (mf), loc, false);
        c.setSize (820, 600);
        check (c.getPage() == ui::InstallerComponent::Page::welcome, "starts on Welcome");
        c.setManifest (m);
        check (c.numRows() == 8, "one row per item");
        check (c.rowStatus ("dooblr-q") == "Up to date", "q 0.9.5 installed: Up to date");
        check (c.rowStatus ("dooblr-kick") == "Update", "kick 0.3.4 -> 0.3.6: Update");
        check (c.rowStatus ("dooblr-vocals") == "Install", "vocals missing: Install");
        check (c.rowStatus ("dooblr-chords") == "Update", "chords folder with unknown version: Update");
        check (c.rowStatus ("dooblr-prophet-panel") == "Up to date", "prophet panel version read from UI_VERSION in its script: Up to date");
        check (readM4LVersion (m4l.getChildFile ("dooblr-prophet")) == "0.3.2", "readM4LVersion finds UI_VERSION");
        const auto sel = c.getSelectedIds();
        check (sel.contains ("dooblr-kick") && sel.contains ("dooblr-soothe") && ! sel.contains ("dooblr-q") && ! sel.contains ("dooblr-vocals"),
               "default selection = the updates: " + sel.joinIntoString (","));
        note ("status line: " + c.getStatusLine());
        check (c.getStatusLine().contains ("8 items") && c.getStatusLine().contains ("3 updates"), "status line counts");

        const auto snap = juce::SystemStats::getEnvironmentVariable ("DOOBLRINSTALLER_SNAPSHOT", {});
        const auto save = [&] (const juce::String& name)
        {
            if (snap.isEmpty()) return;
            const auto f = juce::File (snap + "-" + name + ".png");
            f.getParentDirectory().createDirectory();
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), out);
            note ("snapshot " + f.getFullPathName());
        };

        save ("welcome");
        c.setSelected ("dooblr-vocals", true);
        c.setSelected ("dooblr-prophet-panel", true);
        c.showPage (ui::InstallerComponent::Page::choose);
        check (c.getSelectedIds().size() == 5, "select two more");
        save ("choose");

        c.showPage (ui::InstallerComponent::Page::progress);
        c.setProgress (0.45, "Downloading dooblr-kick 0.3.6...");
        c.setProgress (-1, "Checked dooblr-kick (sha256 OK)");
        c.setProgress (-1, "Downloading dooblr-soothe 0.1.3...");
        save ("progress");

        InstallReport rep;
        rep.done.add ("dooblr-kick 0.3.6 -> /Library/Audio/Plug-Ins/VST3/dooblr-kick.vst3");
        rep.done.add ("dooblr-soothe 0.1.3 -> /Library/Audio/Plug-Ins/VST3/dooblr-soothe.vst3");
        rep.failed.add ("dooblr-vocals: dooblr-vocals-0.2.0-mac.zip: checksum mismatch (sha256 3f1c..., expected 0000...). The download is damaged or was changed; nothing was installed.");
        c.showReport (rep);
        check (c.getPage() == ui::InstallerComponent::Page::done, "report shows Done");
        save ("done");

        ui::InstallerComponent e (juce::URL ("https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json"), loc, false);
        e.setSize (820, 600);
        e.setFetchError ("Could not download https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json (HTTP 404: not found, or the release is private)");
        if (snap.isNotEmpty())
        {
            const auto f = juce::File (snap + "-error.png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (e.createComponentSnapshot (e.getLocalBounds(), true, 2.0f), out);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    tempRoot().createDirectory();

    testVersions();
    testManifest();
    testSha256AndVersions();
    testInstallFlow();
    testWindow();

    tempRoot().deleteRecursively();
    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
