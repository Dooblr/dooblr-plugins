#pragma once

#include "GitHub.h"
#include "Manifest.h"

#include <functional>
#include <map>

namespace dooblr::installer
{
    /** Where things get installed. */
    struct Locations
    {
        juce::File vst3Dir;     // the VST3 folder to install into
        juce::File m4lDir;      // the folder Max for Live devices go into
        bool overridden = false; // set by --vst3-dir / DOOBLR_VST3_DIR (tests): only that folder is read

        /** Mac: /Library/Audio/Plug-Ins/VST3. Windows: C:\Program Files\Common Files\VST3. */
        static juce::File systemVst3Dir();
        /** Mac: ~/Library/Audio/Plug-Ins/VST3. Windows: %LOCALAPPDATA%\Programs\Common\VST3. */
        static juce::File userVst3Dir();
        /** Ableton's User Library (Mac ~/Music/..., Windows Documents\...) + "/Presets/MIDI Effects/Max MIDI Effect",
            the same folder the dooblr-chords and dooblr-prophet build scripts install into. */
        static juce::File defaultM4LDir();

        /** Defaults (system VST3 folder, unless dooblr plug-ins are only in the user folder),
            then the environment overrides DOOBLR_VST3_DIR / DOOBLR_M4L_DIR. */
        static Locations defaults();
    };

    enum class Status { notInstalled, updateAvailable, upToDate, newerInstalled, unavailable };
    juce::String statusText (Status);

    struct InstalledInfo
    {
        bool present = false;
        juce::String version;   // empty when present but unknown
        juce::File where;
    };

    /** Reads a .vst3 bundle's version: Contents/Info.plist (CFBundleShortVersionString), else
        Contents/Resources/moduleinfo.json ("Version"). Empty when neither has one. */
    juce::String readBundleVersion (const juce::File& bundle);
    /** Version written in a Max for Live device folder's .js (VERSION / UI_VERSION = "x.y.z"), or empty. */
    juce::String readM4LVersion (const juce::File& folder);

    /** Installer receipts (what this installer put where, with which version). */
    class Receipts
    {
    public:
        /** userApplicationDataDirectory/dooblr/installer-receipts.json, or DOOBLR_INSTALLER_RECEIPTS. */
        static juce::File defaultFile();
        explicit Receipts (juce::File f = defaultFile());
        juce::String versionFor (const juce::String& id, const juce::File& installedAt) const;
        void record (const juce::String& id, const juce::String& version, const juce::File& installedAt);
    private:
        juce::File file;
    };

    InstalledInfo findInstalled (const Item&, const Locations&, const Receipts&);
    Status statusFor (const Item&, const InstalledInfo&, Platform = currentPlatform());

    juce::String sha256OfFile (const juce::File&);

    /** Progress: (bytes or steps done, total; 0 when unknown). Return false to cancel. */
    using Progress = std::function<bool (juce::int64, juce::int64)>;

    /** Fetches a URL (https or file://) into memory, for the manifest. */
    juce::Result fetchText (const juce::URL&, juce::String& out);

    /** Downloads an asset to dest, then checks its size and sha256. A mismatch deletes the
        download and fails. */
    juce::Result downloadAndVerify (const Asset&, const juce::File& dest, const Progress& = {});
    /** Same, through the GitHub API with the session's token when the asset has an apiUrl (private
        release). error (when given) gets the step, redacted URL and HTTP status of a failure. */
    juce::Result downloadAndVerify (const Asset&, const juce::File& dest, const Progress&, const Session*, ErrorInfo* error);

    /** Checks a file against an asset's sha256 (and size when known). */
    juce::Result verify (const juce::File&, const Asset&);

    /** Unzips into destDir and returns the item's top-level folder (the bundle / device folder).
        Rejects zips whose top entry is not exactly item.bundle. Mac: restores the executable bit. */
    juce::Result extractItem (const juce::File& zip, const Item&, const juce::File& destDir, juce::File& extracted);

    /** One staged (downloaded, verified, unzipped) item waiting to be put in place. */
    struct Staged
    {
        Item item;
        juce::File source;     // the extracted bundle / folder
        juce::File targetDir;  // where it goes
    };

    /** Puts the staged items in place, replacing older copies. Writable folders are written
        directly; otherwise one admin prompt (macOS authorization via osascript, Windows UAC)
        does all the copies. Records receipts. */
    juce::Result placeStaged (const juce::Array<Staged>&, Receipts&);

    /** Copies src (a folder) to dstDir/name, replacing it. Used by the elevated helper on Windows. */
    juce::Result replaceFolder (const juce::File& src, const juce::File& dstDir, const juce::String& name);

    /** Per-item outcome lines for the UI and the command line. */
    struct InstallReport
    {
        juce::StringArray done, failed;
        juce::Array<ErrorInfo> errors;   // one per failure, for "Copy details" and the command line
        bool cancelled = false;

        /** Every error's copyText, or a short summary when nothing failed. */
        juce::String copyText() const;
    };

    /** Downloads, verifies, unzips and installs the given items (by id). log gets one line per step. */
    InstallReport installItems (const Manifest&, const juce::StringArray& ids, const Locations&, Receipts&,
                                const std::function<void (const juce::String& line)>& log,
                                const std::function<bool (int itemIndex, int itemCount, double itemProgress)>& progress,
                                const Session* session = nullptr);
}
