#pragma once

#include <juce_core/juce_core.h>

namespace dooblr::installer
{
    /** Where the installer keeps a GitHub token pasted into its sign-in panel: the macOS Keychain
        or the Windows Credential Manager, else (when that fails, or on other systems) a file only
        this user can read (userApplicationDataDirectory/dooblr/github-token, mode 0600).
        The token is never logged or shown. */
    class TokenStore
    {
    public:
        enum class Backend { system, file };

        TokenStore (Backend, juce::File fallbackFile, juce::String serviceName = "com.dooblr.installer.github-token");

        /** The system store with the default file as fallback; DOOBLR_INSTALLER_TOKEN_FILE makes it
            use only that file (tests and scripts). */
        static TokenStore defaultStore();
        static juce::File defaultFile();

        /** The saved token (empty when none); where = "macOS Keychain", "Windows Credential Manager" or the file. */
        juce::String load (juce::String& where) const;
        /** Saves (replaces) the token. */
        juce::Result save (const juce::String& token, juce::String& where) const;
        /** Removes a saved token from the system store and the file. */
        juce::Result forget() const;

        static juce::String systemStoreName();
        static bool looksLikeToken (const juce::String&);

    private:
        Backend backend;
        juce::File file;
        juce::String service;
    };
}
