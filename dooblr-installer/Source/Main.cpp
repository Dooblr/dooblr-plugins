// dooblr installer: a small window that installs and updates the dooblr plug-ins, plus a
// command line for scripts and tests:
//
//   dooblr-installer --list    [--manifest <url>] [--vst3-dir <dir>] [--m4l-dir <dir>]
//   dooblr-installer --install <id,id,...|all|updates> [same options]
//   dooblr-installer --manifest <url>          (opens the window on that list)
//
// The manifest URL defaults to the latest GitHub release (DOOBLRINSTALLER_MANIFEST_URL at build
// time); DOOBLR_MANIFEST_URL overrides it. --vst3-dir installs into (and reads only) that folder.

#include "UI/InstallerComponent.h"

#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <cstdio>
#endif

using namespace dooblr::installer;

namespace
{
    juce::String option (const juce::StringArray& args, const juce::String& name)
    {
        const auto i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : juce::String();
    }

    juce::URL toUrl (const juce::String& s)
    {
        if (s.contains ("://"))
            return juce::URL (s);
        return juce::URL (juce::File::getCurrentWorkingDirectory().getChildFile (s)); // a plain path
    }

    juce::URL manifestUrl (const juce::StringArray& args)
    {
        auto s = option (args, "--manifest");
        if (s.isEmpty()) s = juce::SystemStats::getEnvironmentVariable ("DOOBLR_MANIFEST_URL", {});
        if (s.isEmpty()) s = DOOBLRINSTALLER_MANIFEST_URL;
        return toUrl (s);
    }

    Locations locations (const juce::StringArray& args)
    {
        auto l = Locations::defaults();
        const auto v = option (args, "--vst3-dir");
        if (v.isNotEmpty()) { l.vst3Dir = juce::File::getCurrentWorkingDirectory().getChildFile (v); l.overridden = true; }
        const auto m = option (args, "--m4l-dir");
        if (m.isNotEmpty()) l.m4lDir = juce::File::getCurrentWorkingDirectory().getChildFile (m);
        return l;
    }

    void attachConsole()
    {
       #if JUCE_WINDOWS
        if (AttachConsole (ATTACH_PARENT_PROCESS))
        {
            FILE* f = nullptr;
            freopen_s (&f, "CONOUT$", "w", stdout);
            freopen_s (&f, "CONOUT$", "w", stderr);
        }
       #endif
    }

    int runCommandLine (const juce::StringArray& args)
    {
        attachConsole();

        if (args.contains ("--elevated-copy"))
        {
            const auto i = args.indexOf ("--elevated-copy");
            if (i + 3 >= args.size()) return 2;
            const auto r = replaceFolder (juce::File (args[i + 1].unquoted()), juce::File (args[i + 2].unquoted()), args[i + 3].unquoted());
            return r.wasOk() ? 0 : 1;
        }

        const auto url = manifestUrl (args);
        const auto loc = locations (args);
        juce::String text;
        Manifest m;
        auto r = fetchText (url, text);
        if (r.wasOk()) r = Manifest::parse (text, url, m);
        if (r.failed())
        {
            std::cerr << "error: " << r.getErrorMessage() << "\n  manifest: " << url.toString (false) << std::endl;
            return 3;
        }

        Receipts receipts;
        std::cout << "manifest " << url.toString (false) << " (" << m.items.size() << " items, generated " << m.generated << ")\n"
                  << "VST3 folder " << loc.vst3Dir.getFullPathName() << "\nM4L folder  " << loc.m4lDir.getFullPathName() << "\n";

        juce::StringArray wanted;
        for (auto& item : m.items)
        {
            const auto info = findInstalled (item, loc, receipts);
            const auto st = statusFor (item, info);
            std::cout << "  " << item.id.paddedRight (' ', 22) << " installed " << (info.present ? (info.version.isNotEmpty() ? info.version : "unknown") : "-").paddedRight (' ', 8)
                      << " available " << item.version.paddedRight (' ', 8) << " " << statusText (st) << "\n";
            if (st == Status::updateAvailable)
                wanted.add (item.id);
        }

        if (! args.contains ("--install"))
            return 0;

        const auto which = option (args, "--install");
        juce::StringArray ids;
        if (which == "updates")  ids = wanted;
        else if (which == "all") for (auto& item : m.items) { if (item.assetFor (currentPlatform()) != nullptr) ids.add (item.id); }
        else                     ids = juce::StringArray::fromTokens (which, ",", "");
        ids.trim();
        ids.removeEmptyStrings();

        if (ids.isEmpty()) { std::cout << "nothing to install" << std::endl; return 0; }

        const auto report = installItems (m, ids, loc, receipts,
                                          [] (const juce::String& line) { std::cout << line << std::endl; }, {});
        std::cout << report.done.size() << " installed, " << report.failed.size() << " failed" << std::endl;
        return report.failed.isEmpty() && ! report.cancelled ? 0 : 1;
    }
}

class DooblrInstallerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "dooblr installer"; }
    const juce::String getApplicationVersion() override { return DOOBLRINSTALLER_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = getCommandLineParameterArray();
        juce::ignoreUnused (commandLine);

        if (args.contains ("--list") || args.contains ("--install") || args.contains ("--elevated-copy"))
        {
            setApplicationReturnValue (runCommandLine (args));
            quit();
            return;
        }

        window = std::make_unique<Window> (manifestUrl (args), locations (args));
    }

    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window (juce::URL url, Locations loc)
            : DocumentWindow ("dooblr installer", dooblr::installer::ui::Palette::background, closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new dooblr::installer::ui::InstallerComponent (std::move (url), std::move (loc)), true);
            setResizable (true, false);
            setResizeLimits (720, 520, 1400, 1000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (DooblrInstallerApplication)
