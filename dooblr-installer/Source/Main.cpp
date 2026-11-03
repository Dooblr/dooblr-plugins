// dooblr installer: a small window that installs and updates the dooblr plug-ins, plus a
// command line for scripts and tests:
//
//   dooblr-installer --list    [--manifest <url>] [--vst3-dir <dir>] [--m4l-dir <dir>]
//   dooblr-installer --install <id,id,...|all|updates> [same options]
//   dooblr-installer --manifest <url>          (opens the window on that list)
//   dooblr-installer --save-token              (reads a GitHub token from stdin and saves it)
//   dooblr-installer --forget-token
//   --no-auth: never look for a GitHub token (anonymous downloads only)
//
// The manifest URL defaults to the latest GitHub release (DOOBLRINSTALLER_MANIFEST_URL at build
// time); DOOBLR_MANIFEST_URL overrides it. --vst3-dir installs into (and reads only) that folder.

#include "UI/InstallerComponent.h"

#include <iostream>
#include <string>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <cstdio>
#else
 #include <termios.h>
 #include <unistd.h>
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
            freopen_s (&f, "CONIN$", "r", stdin);
        }
       #endif
    }

    /** One line from stdin without echoing it when stdin is a terminal. */
    juce::String readSecretLine()
    {
       #if JUCE_WINDOWS
        HANDLE in = GetStdHandle (STD_INPUT_HANDLE);
        DWORD mode = 0;
        const bool console = GetConsoleMode (in, &mode) != 0;
        if (console) SetConsoleMode (in, mode & ~(DWORD) ENABLE_ECHO_INPUT);
       #else
        termios old {};
        const bool console = isatty (STDIN_FILENO) && tcgetattr (STDIN_FILENO, &old) == 0;
        if (console)
        {
            auto quiet = old;
            quiet.c_lflag &= ~(tcflag_t) ECHO;
            tcsetattr (STDIN_FILENO, TCSANOW, &quiet);
        }
       #endif
        std::string line;
        std::getline (std::cin, line);
       #if JUCE_WINDOWS
        if (console) SetConsoleMode (in, mode);
       #else
        if (console) tcsetattr (STDIN_FILENO, TCSANOW, &old);
       #endif
        if (console) std::cerr << std::endl;
        return juce::String (line).trim();
    }

    void printSignInHelp()
    {
        std::cerr << "\nThe dooblr releases are in a private GitHub repository for now, so a GitHub token is needed:\n"
                     "  1. Create a fine-grained token at https://github.com/settings/personal-access-tokens/new\n"
                     "     Resource owner: Dooblr. Repository access: Only select repositories > dooblr-plugins.\n"
                     "     Permissions: Contents: Read-only.\n"
                     "  2. Save it:  \"dooblr installer\" --save-token   (paste it and press Return; it is not shown)\n"
                     "     or set DOOBLR_GITHUB_TOKEN for this shell, or sign in with the GitHub CLI (gh auth login).\n";
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

        if (args.contains ("--save-token"))
        {
            std::cerr << "Paste the GitHub token and press Return: " << std::flush;
            const auto token = readSecretLine();
            juce::String where;
            const auto r = TokenStore::defaultStore().save (token, where);
            if (r.failed()) { std::cerr << "error: " << r.getErrorMessage() << std::endl; return 1; }
            std::cout << "Saved the token in the " << where << "." << std::endl;
            return 0;
        }

        if (args.contains ("--forget-token"))
        {
            const auto r = TokenStore::defaultStore().forget();
            std::cout << (r.wasOk() ? "Removed the saved token." : r.getErrorMessage()) << std::endl;
            return r.wasOk() ? 0 : 1;
        }

        const auto url = manifestUrl (args);
        const auto loc = locations (args);
        Manifest m;
        Session session;
        ErrorInfo err;
        if (! fetchManifest (url, m, session, err, args.contains ("--no-auth") ? TokenSources::none() : TokenSources::system()))
        {
            std::cerr << "error: " << err.message << "\n\n" << err.copyText() << "Manifest: " << redactUrl (url.toString (true)) << std::endl;
            if (err.needsSignIn)
            {
                printSignInHelp();
                return 4;
            }
            return 3;
        }

        Receipts receipts;
        std::cout << "manifest " << url.toString (false) << " (" << m.items.size() << " items, generated " << m.generated << ")\n"
                  << (session.authenticated() ? "signed in to GitHub with the " + session.tokenSource + "\n" : juce::String())
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
                                          [] (const juce::String& line) { std::cout << redactSecrets (line) << std::endl; }, {}, &session);
        std::cout << report.done.size() << " installed, " << report.failed.size() << " failed" << std::endl;
        for (auto& e : report.errors)
            std::cerr << "\n" << e.copyText();
        return report.failed.isEmpty() && ! report.cancelled ? 0 : 1;
    }
}

class DooblrInstallerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "dooblr installer"; }
    const juce::String getApplicationVersion() override { return DOOBLRINSTALLER_VERSION; }
    // Command-line runs (--list, --install, ...) always run in their own process, even while an
    // installer window is open; otherwise JUCE would hand the arguments to the open window and
    // exit 0 without doing anything. Only plain window launches stay single-instance.
    static bool isCommandLineRun()
    {
        const auto args = getCommandLineParameterArray();
        for (auto* flag : { "--list", "--install", "--elevated-copy", "--save-token", "--forget-token" })
            if (args.contains (flag))
                return true;
        return false;
    }

    bool moreThanOneInstanceAllowed() override          { return isCommandLineRun(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = getCommandLineParameterArray();
        juce::ignoreUnused (commandLine);

        if (isCommandLineRun())
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
