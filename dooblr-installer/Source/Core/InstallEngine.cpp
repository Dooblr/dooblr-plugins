#include "InstallEngine.h"

#include <juce_cryptography/juce_cryptography.h>

#include <regex>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
#endif

namespace dooblr::installer
{
    //==========================================================================
    juce::File Locations::systemVst3Dir()
    {
       #if JUCE_MAC
        return juce::File ("/Library/Audio/Plug-Ins/VST3");
       #elif JUCE_WINDOWS
        auto common = juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", {});
        if (common.isEmpty())
            common = "C:\\Program Files\\Common Files";
        return juce::File (common).getChildFile ("VST3");
       #else
        return juce::File ("/usr/lib/vst3");
       #endif
    }

    juce::File Locations::userVst3Dir()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3");
       #elif JUCE_WINDOWS
        auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        return juce::File (local).getChildFile ("Programs\\Common\\VST3");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3");
       #endif
    }

    juce::File Locations::defaultM4LDir()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect");
       #else
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect");
       #endif
    }

    Locations Locations::defaults()
    {
        Locations l;
        l.vst3Dir = systemVst3Dir();
        l.m4lDir = defaultM4LDir();

        // Like the suite's build scripts: if dooblr plug-ins live only in the user folder, keep them there.
        const auto sys = systemVst3Dir().findChildFiles (juce::File::findDirectories, false, "dooblr-*.vst3");
        const auto usr = userVst3Dir().findChildFiles (juce::File::findDirectories, false, "dooblr-*.vst3");
        if (sys.isEmpty() && ! usr.isEmpty())
            l.vst3Dir = userVst3Dir();

        const auto v = juce::SystemStats::getEnvironmentVariable ("DOOBLR_VST3_DIR", {});
        if (v.isNotEmpty()) { l.vst3Dir = juce::File (v); l.overridden = true; }
        const auto m = juce::SystemStats::getEnvironmentVariable ("DOOBLR_M4L_DIR", {});
        if (m.isNotEmpty()) l.m4lDir = juce::File (m);
        return l;
    }

    juce::String statusText (Status s)
    {
        switch (s)
        {
            case Status::notInstalled:    return "Install";
            case Status::updateAvailable: return "Update";
            case Status::upToDate:        return "Up to date";
            case Status::newerInstalled:  return "Newer installed";
            case Status::unavailable:     return "Not for this computer";
        }
        return {};
    }

    //==========================================================================
    juce::String readBundleVersion (const juce::File& bundle)
    {
        const auto plist = bundle.getChildFile ("Contents/Info.plist");
        if (plist.existsAsFile())
        {
            // A simple scan is enough for the XML plists JUCE writes.
            const auto text = plist.loadFileAsString();
            const auto key = text.fromFirstOccurrenceOf ("<key>CFBundleShortVersionString</key>", false, false);
            const auto v = key.fromFirstOccurrenceOf ("<string>", false, false).upToFirstOccurrenceOf ("</string>", false, false).trim();
            if (key.isNotEmpty() && v.isNotEmpty() && v.length() < 32)
                return v;
        }

        const auto info = bundle.getChildFile ("Contents/Resources/moduleinfo.json");
        if (info.existsAsFile())
        {
            // moduleinfo.json is JSON5-ish (trailing commas), so read the first "Version" directly.
            const auto text = info.loadFileAsString();
            const auto after = text.fromFirstOccurrenceOf ("\"Version\"", false, false).fromFirstOccurrenceOf (":", false, false);
            const auto v = after.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false).trim();
            if (v.isNotEmpty() && v.length() < 32)
                return v;
        }

        return {};
    }

    //==========================================================================
    juce::File Receipts::defaultFile()
    {
        const auto e = juce::SystemStats::getEnvironmentVariable ("DOOBLR_INSTALLER_RECEIPTS", {});
        if (e.isNotEmpty())
            return juce::File (e);
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("dooblr").getChildFile ("installer-receipts.json");
    }

    Receipts::Receipts (juce::File f) : file (std::move (f)) {}

    juce::String Receipts::versionFor (const juce::String& id, const juce::File& at) const
    {
        const auto root = juce::JSON::parse (file);
        const auto r = root.getProperty (id, {});
        if (r.isObject() && juce::File (r.getProperty ("path", {}).toString()) == at)
            return r.getProperty ("version", {}).toString();
        return {};
    }

    void Receipts::record (const juce::String& id, const juce::String& version, const juce::File& at)
    {
        auto root = juce::JSON::parse (file);
        if (! root.isObject())
            root = juce::var (new juce::DynamicObject());

        auto* o = new juce::DynamicObject();
        o->setProperty ("version", version);
        o->setProperty ("path", at.getFullPathName());
        o->setProperty ("installed", juce::Time::getCurrentTime().toISO8601 (true));
        root.getDynamicObject()->setProperty (id, juce::var (o));

        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (root));
    }

    //==========================================================================
    juce::String readM4LVersion (const juce::File& folder)
    {
        // The devices keep their version in their script: VERSION = '0.3.0' (chords) or UI_VERSION = "0.3.2" (prophet panel).
        static const std::regex re (R"((?:^|[^A-Za-z_])(?:UI_)?VERSION\s*=\s*['"]([0-9]+(?:\.[0-9]+)*)['"])");
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.js"))
        {
            const auto text = f.loadFileAsString().toStdString();
            std::smatch m;
            if (std::regex_search (text, m, re))
                return juce::String (m[1].str());
        }
        return {};
    }

    InstalledInfo findInstalled (const Item& item, const Locations& loc, const Receipts& receipts)
    {
        juce::Array<juce::File> dirs;
        if (item.kind == Kind::m4l)
            dirs.add (loc.m4lDir);
        else
        {
            dirs.add (loc.vst3Dir);
            if (! loc.overridden)
                for (auto d : { Locations::systemVst3Dir(), Locations::userVst3Dir() })
                    dirs.addIfNotAlreadyThere (d);
        }

        for (auto& d : dirs)
        {
            const auto f = d.getChildFile (item.bundle);
            if (! f.exists())
                continue;

            InstalledInfo info;
            info.present = true;
            info.where = f;
            if (item.kind == Kind::vst3)
                info.version = readBundleVersion (f);
            if (info.version.isEmpty())
                info.version = receipts.versionFor (item.id, f);
            if (info.version.isEmpty() && item.kind == Kind::m4l)
                info.version = readM4LVersion (f);
            return info;
        }

        return {};
    }

    Status statusFor (const Item& item, const InstalledInfo& info, Platform p)
    {
        if (item.assetFor (p) == nullptr)
            return Status::unavailable;
        if (! info.present)
            return Status::notInstalled;
        if (info.version.isEmpty())
            return Status::updateAvailable; // present, version unknown: offer to replace it

        const auto c = Version::compare (info.version, item.version);
        return c < 0 ? Status::updateAvailable : (c == 0 ? Status::upToDate : Status::newerInstalled);
    }

    //==========================================================================
    juce::String sha256OfFile (const juce::File& f)
    {
        return juce::SHA256 (f).toHexString().toLowerCase();
    }

    juce::Result fetchText (const juce::URL& url, juce::String& out)
    {
        ErrorInfo err;
        auto r = httpGet (url, {}, {}, err);
        if (! r.ok())
            return juce::Result::fail (err.message + (err.url.isNotEmpty() && ! err.message.contains (err.url) ? " (" + err.url + ")" : juce::String()));
        out = r.stream->readEntireStreamAsString();
        return juce::Result::ok();
    }

    juce::Result verify (const juce::File& f, const Asset& a)
    {
        if (a.size > 0 && f.getSize() != a.size)
            return juce::Result::fail (f.getFileName() + ": size " + juce::String (f.getSize()) + " bytes, expected " + juce::String (a.size));

        const auto h = sha256OfFile (f);
        if (h != a.sha256)
            return juce::Result::fail (f.getFileName() + ": checksum mismatch (sha256 " + h.substring (0, 12) + "..., expected "
                                       + a.sha256.substring (0, 12) + "...). The download is damaged or was changed; nothing was installed.");
        return juce::Result::ok();
    }

    juce::Result downloadAndVerify (const Asset& a, const juce::File& dest, const Progress& progress)
    {
        return downloadAndVerify (a, dest, progress, nullptr, nullptr);
    }

    juce::Result downloadAndVerify (const Asset& a, const juce::File& dest, const Progress& progress,
                                    const Session* session, ErrorInfo* errorOut)
    {
        ErrorInfo err;
        const auto viaApi = session != nullptr && session->authenticated() && a.apiUrl.isNotEmpty();
        err.setUrl (viaApi ? a.apiUrl : a.url.toString (true));
        auto fail = [&] (const juce::String& message)
        {
            if (err.message.isEmpty()) err.message = message;
            if (errorOut != nullptr) *errorOut = err;
            return juce::Result::fail (message);
        };

        auto response = openAsset (a, session, err);
        if (! response.ok())
        {
            auto message = err.message;
            if (err.httpStatus == 404 && ! viaApi)
                message << " The file is not there (or the release is private).";
            else if ((err.httpStatus == 401 || err.httpStatus == 403) && viaApi)
                message << " GitHub refused the token (" << session->tokenSource << ").";
            err.message = message;
            return fail (message + " (" + err.url + ")");
        }

        auto& in = response.stream;
        auto total = response.totalLength;
        if (total <= 0)
            total = a.size;

        dest.deleteFile();
        dest.getParentDirectory().createDirectory();
        {
            juce::FileOutputStream out (dest);
            if (! out.openedOk())
                return fail ("Cannot write " + dest.getFullPathName());

            juce::HeapBlock<char> buf (65536);
            juce::int64 done = 0;
            for (;;)
            {
                const auto n = in->read (buf, 65536);
                if (n <= 0)
                    break;
                out.write (buf, (size_t) n);
                done += n;
                if (progress && ! progress (done, total))
                {
                    out.flush();
                    dest.deleteFile();
                    return fail ("Cancelled");
                }
            }
            out.flush();
        }

        auto r = verify (dest, a);
        if (r.failed())
        {
            dest.deleteFile();
            err.httpStatus = response.status;
            return fail (r.getErrorMessage());
        }
        return r;
    }

    juce::Result extractItem (const juce::File& zipFile, const Item& item, const juce::File& destDir, juce::File& extracted)
    {
        juce::ZipFile zip (zipFile);
        if (zip.getNumEntries() == 0)
            return juce::Result::fail (zipFile.getFileName() + " is not a valid zip.");

        // Every entry must sit inside item.bundle/ (no stray files, no "..", no absolute paths).
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');
            if (! (name == item.bundle || name.startsWith (item.bundle + "/")) || name.contains ("../") || name.startsWith ("/"))
                return juce::Result::fail (zipFile.getFileName() + " has an unexpected entry: " + name);
        }

        destDir.deleteRecursively();
        destDir.createDirectory();
        auto r = zip.uncompressTo (destDir, true);
        if (r.failed())
            return juce::Result::fail ("Unzip failed: " + r.getErrorMessage());

        extracted = destDir.getChildFile (item.bundle);
        if (! extracted.isDirectory())
            return juce::Result::fail (zipFile.getFileName() + " does not contain " + item.bundle);

       #if JUCE_MAC
        // ZipFile does not restore Unix modes; plug-in binaries must be executable.
        for (auto& f : extracted.getChildFile ("Contents/MacOS").findChildFiles (juce::File::findFiles, false))
            f.setExecutePermission (true);
       #endif

        return juce::Result::ok();
    }

    //==========================================================================
    juce::Result replaceFolder (const juce::File& src, const juce::File& dstDir, const juce::String& name)
    {
        if (! dstDir.createDirectory())
            return juce::Result::fail ("Cannot create " + dstDir.getFullPathName());

        const auto target = dstDir.getChildFile (name);
        if (target.exists() && ! target.deleteRecursively())
            return juce::Result::fail ("Cannot remove the old " + target.getFullPathName() + " (is a DAW using it?)");

        if (! src.copyDirectoryTo (target))
            return juce::Result::fail ("Cannot copy to " + target.getFullPathName());

        return juce::Result::ok();
    }

    static bool canWriteTo (const juce::File& dir)
    {
        auto d = dir;
        while (! d.exists() && d != d.getParentDirectory())
            d = d.getParentDirectory();
        if (! d.hasWriteAccess())
            return false;

        const auto probe = d.getNonexistentChildFile (".dooblr-write-test", "");
        const auto ok = probe.create().wasOk();
        probe.deleteFile();
        return ok;
    }

   #if JUCE_MAC
    static juce::String shQuote (const juce::String& s) { return "'" + s.replace ("'", "'\\''") + "'"; }

    static juce::Result runElevatedMac (const juce::Array<Staged>& items)
    {
        juce::StringArray cmds;
        for (auto& s : items)
        {
            const auto target = s.targetDir.getChildFile (s.item.bundle);
            cmds.add ("/bin/mkdir -p " + shQuote (s.targetDir.getFullPathName()));
            cmds.add ("/bin/rm -rf " + shQuote (target.getFullPathName()));
            cmds.add ("/usr/bin/ditto " + shQuote (s.source.getFullPathName()) + " " + shQuote (target.getFullPathName()));
        }

        const auto shell = cmds.joinIntoString (" && ");
        const auto script = "do shell script \"" + shell.replace ("\\", "\\\\").replace ("\"", "\\\"")
                          + "\" with prompt \"dooblr installer wants to install plug-ins into the system VST3 folder.\" with administrator privileges";

        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/osascript", "-e", script }, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
            return juce::Result::fail ("Could not ask for administrator rights.");

        const auto output = p.readAllProcessOutput();
        p.waitForProcessToFinish (-1);
        if (p.getExitCode() != 0)
            return juce::Result::fail (output.contains ("-128") ? "Cancelled at the administrator prompt; nothing was installed."
                                                                 : "Install with administrator rights failed: " + output.trim());
        return juce::Result::ok();
    }
   #endif

   #if JUCE_WINDOWS
    static juce::Result runElevatedWindows (const juce::Array<Staged>& items)
    {
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (auto& s : items)
        {
            const auto params = "--elevated-copy \"" + s.source.getFullPathName() + "\" \"" + s.targetDir.getFullPathName()
                              + "\" \"" + s.item.bundle + "\"";

            SHELLEXECUTEINFOW info {};
            info.cbSize = sizeof (info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS;
            info.lpVerb = L"runas";
            const auto exe = self.getFullPathName();
            info.lpFile = exe.toWideCharPointer();
            info.lpParameters = params.toWideCharPointer();
            info.nShow = SW_HIDE;

            if (! ShellExecuteExW (&info) || info.hProcess == nullptr)
                return juce::Result::fail ("Cancelled at the Windows permission prompt; " + s.item.name + " was not installed.");

            WaitForSingleObject (info.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess (info.hProcess, &code);
            CloseHandle (info.hProcess);
            if (code != 0)
                return juce::Result::fail ("Installing " + s.item.name + " with administrator rights failed.");
        }
        return juce::Result::ok();
    }
   #endif

    juce::Result placeStaged (const juce::Array<Staged>& staged, Receipts& receipts)
    {
        juce::Array<Staged> elevated;
        for (auto& s : staged)
        {
            if (canWriteTo (s.targetDir))
            {
                auto r = replaceFolder (s.source, s.targetDir, s.item.bundle);
                if (r.failed())
                    return r;
            }
            else
                elevated.add (s);
        }

        if (! elevated.isEmpty())
        {
           #if JUCE_MAC
            auto r = runElevatedMac (elevated);
           #elif JUCE_WINDOWS
            auto r = runElevatedWindows (elevated);
           #else
            auto r = juce::Result::fail ("No write access to " + elevated.getFirst().targetDir.getFullPathName());
           #endif
            if (r.failed())
                return r;
        }

        for (auto& s : staged)
        {
            const auto at = s.targetDir.getChildFile (s.item.bundle);
            if (! at.isDirectory())
                return juce::Result::fail (s.item.name + " is not in " + s.targetDir.getFullPathName() + " after installing.");
            receipts.record (s.item.id, s.item.version, at);
        }
        return juce::Result::ok();
    }

    //==========================================================================
    InstallReport installItems (const Manifest& m, const juce::StringArray& ids, const Locations& loc, Receipts& receipts,
                                const std::function<void (const juce::String&)>& log,
                                const std::function<bool (int, int, double)>& progress,
                                const Session* session)
    {
        InstallReport report;
        auto addFailure = [&report] (const juce::String& line, const juce::String& step, ErrorInfo e = {})
        {
            report.failed.add (line);
            if (e.step.isEmpty()) e.step = step;
            if (e.message.isEmpty()) e.message = line;
            report.errors.add (e);
        };
        const auto say = [&log] (const juce::String& s) { if (log) log (s); };
        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("dooblr-installer-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        work.createDirectory();

        juce::Array<Staged> staged;
        for (int i = 0; i < ids.size(); ++i)
        {
            const auto* item = m.find (ids[i]);
            if (item == nullptr) { addFailure (ids[i] + ": not in the manifest", "Choosing " + ids[i]); say (report.failed[report.failed.size() - 1]); continue; }

            const auto* asset = item->assetFor (currentPlatform());
            if (asset == nullptr) { addFailure (item->name + ": no download for this computer", "Choosing " + item->name); say (report.failed[report.failed.size() - 1]); continue; }

            say ("Downloading " + item->name + " " + item->version + "...");
            const auto zipName = juce::File::createLegalFileName (asset->url.getFileName());
            const auto zip = work.getChildFile (zipName.endsWithIgnoreCase (".zip") ? zipName : item->id + ".zip");
            ErrorInfo downloadError;
            auto r = downloadAndVerify (*asset, zip, [&] (juce::int64 done, juce::int64 total)
            {
                return ! progress || progress (i, ids.size(), total > 0 ? 0.9 * (double) done / (double) total : 0.0);
            }, session, &downloadError);

            if (r.failed())
            {
                if (r.getErrorMessage() == "Cancelled") { report.cancelled = true; break; }
                downloadError.step = "Downloading " + item->name + " " + item->version;
                downloadError.message = item->name + ": " + downloadError.message;
                addFailure (item->name + ": " + r.getErrorMessage(), downloadError.step, downloadError);
                say (report.failed[report.failed.size() - 1]);
                continue;
            }

            say ("Checked " + item->name + " (sha256 OK)");
            juce::File extracted;
            r = extractItem (zip, *item, work.getChildFile (item->id), extracted);
            if (r.failed()) { addFailure (item->name + ": " + r.getErrorMessage(), "Unzipping " + item->name); say (report.failed[report.failed.size() - 1]); continue; }

            staged.add ({ *item, extracted, item->kind == Kind::m4l ? loc.m4lDir : loc.vst3Dir });
            if (progress && ! progress (i, ids.size(), 1.0)) { report.cancelled = true; break; }
        }

        if (! report.cancelled && ! staged.isEmpty())
        {
            say ("Installing...");
            auto r = placeStaged (staged, receipts);
            if (r.failed())
            {
                for (auto& s : staged)
                    addFailure (s.item.name + ": " + r.getErrorMessage(), "Installing " + s.item.name + " into " + s.targetDir.getFullPathName());
                say (r.getErrorMessage());
            }
            else
            {
                for (auto& s : staged)
                {
                    const auto at = s.targetDir.getChildFile (s.item.bundle);
                    report.done.add (s.item.name + " " + s.item.version + " -> " + at.getFullPathName());
                    say ("Installed " + report.done[report.done.size() - 1]);
                }
            }
        }

        work.deleteRecursively();
        return report;
    }

    juce::String InstallReport::copyText() const
    {
        juce::String s;
        for (auto& e : errors)
            s << e.copyText() << "\n";
        if (s.isEmpty())
            s << "dooblr installer " << installerVersion() << " on " << osDescription() << "\n"
              << done.size() << " installed, " << failed.size() << " failed" << (cancelled ? ", cancelled" : "") << "\n";
        return redactSecrets (s);
    }
}
