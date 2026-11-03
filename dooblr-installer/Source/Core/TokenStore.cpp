#include "TokenStore.h"

#if JUCE_MAC
 #include <Security/Security.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <wincred.h>
#endif

#if ! JUCE_WINDOWS
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

#include <string>

namespace dooblr::installer
{
    namespace
    {
        constexpr const char* accountName = "github";

       #if JUCE_MAC
        struct CFHolder
        {
            explicit CFHolder (CFTypeRef r = nullptr) : ref (r) {}
            ~CFHolder() { if (ref != nullptr) CFRelease (ref); }
            CFHolder (const CFHolder&) = delete;
            CFHolder& operator= (const CFHolder&) = delete;
            CFTypeRef ref;
        };

        CFStringRef cfString (const juce::String& s)
        {
            return CFStringCreateWithCString (kCFAllocatorDefault, s.toRawUTF8(), kCFStringEncodingUTF8);
        }

        CFMutableDictionaryRef baseQuery (CFStringRef service, CFStringRef account)
        {
            auto q = CFDictionaryCreateMutable (kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue (q, kSecClass, kSecClassGenericPassword);
            CFDictionarySetValue (q, kSecAttrService, service);
            CFDictionarySetValue (q, kSecAttrAccount, account);
            return q;
        }

        juce::String systemLoad (const juce::String& serviceName)
        {
            CFHolder service (cfString (serviceName)), account (cfString (accountName));
            CFHolder query (baseQuery ((CFStringRef) service.ref, (CFStringRef) account.ref));
            CFDictionarySetValue ((CFMutableDictionaryRef) query.ref, kSecReturnData, kCFBooleanTrue);
            CFDictionarySetValue ((CFMutableDictionaryRef) query.ref, kSecMatchLimit, kSecMatchLimitOne);

            CFTypeRef result = nullptr;
            const auto status = SecItemCopyMatching ((CFDictionaryRef) query.ref, &result);
            CFHolder holder (result);
            if (status != errSecSuccess || result == nullptr || CFGetTypeID (result) != CFDataGetTypeID())
                return {};
            auto data = (CFDataRef) result;
            return juce::String::fromUTF8 ((const char*) CFDataGetBytePtr (data), (int) CFDataGetLength (data));
        }

        bool systemForget (const juce::String& serviceName)
        {
            CFHolder service (cfString (serviceName)), account (cfString (accountName));
            CFHolder query (baseQuery ((CFStringRef) service.ref, (CFStringRef) account.ref));
            const auto status = SecItemDelete ((CFDictionaryRef) query.ref);
            return status == errSecSuccess || status == errSecItemNotFound;
        }

        juce::Result systemSave (const juce::String& serviceName, const juce::String& token)
        {
            systemForget (serviceName);
            CFHolder service (cfString (serviceName)), account (cfString (accountName));
            CFHolder label (cfString ("dooblr installer GitHub token"));
            CFHolder data (CFDataCreate (kCFAllocatorDefault, (const UInt8*) token.toRawUTF8(), (CFIndex) token.getNumBytesAsUTF8()));
            CFHolder query (baseQuery ((CFStringRef) service.ref, (CFStringRef) account.ref));
            CFDictionarySetValue ((CFMutableDictionaryRef) query.ref, kSecAttrLabel, label.ref);
            CFDictionarySetValue ((CFMutableDictionaryRef) query.ref, kSecValueData, data.ref);
            const auto status = SecItemAdd ((CFDictionaryRef) query.ref, nullptr);
            if (status == errSecSuccess)
                return juce::Result::ok();
            return juce::Result::fail ("The macOS Keychain refused the token (OSStatus " + juce::String ((int) status) + ")");
        }
       #elif JUCE_WINDOWS
        std::wstring wide (const juce::String& s) { return std::wstring (s.toWideCharPointer()); }

        juce::String systemLoad (const juce::String& serviceName)
        {
            PCREDENTIALW cred = nullptr;
            auto target = wide (serviceName);
            if (! CredReadW (target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr)
                return {};
            auto s = juce::String::fromUTF8 ((const char*) cred->CredentialBlob, (int) cred->CredentialBlobSize);
            CredFree (cred);
            return s;
        }

        bool systemForget (const juce::String& serviceName)
        {
            auto target = wide (serviceName);
            return CredDeleteW (target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
        }

        juce::Result systemSave (const juce::String& serviceName, const juce::String& token)
        {
            auto target = wide (serviceName);
            auto user = wide (accountName);
            std::string blob (token.toRawUTF8());
            CREDENTIALW c {};
            c.Type = CRED_TYPE_GENERIC;
            c.TargetName = target.data();
            c.UserName = user.data();
            c.CredentialBlobSize = (DWORD) blob.size();
            c.CredentialBlob = (LPBYTE) blob.data();
            c.Persist = CRED_PERSIST_LOCAL_MACHINE;
            if (CredWriteW (&c, 0))
                return juce::Result::ok();
            return juce::Result::fail ("Windows Credential Manager refused the token (error " + juce::String ((int) GetLastError()) + ")");
        }
       #else
        juce::String systemLoad (const juce::String&)                         { return {}; }
        bool systemForget (const juce::String&)                               { return true; }
        juce::Result systemSave (const juce::String&, const juce::String&)    { return juce::Result::fail ("no system credential store"); }
       #endif

        juce::Result writePrivateFile (const juce::File& f, const juce::String& text)
        {
            if (! f.getParentDirectory().createDirectory())
                return juce::Result::fail ("Cannot create " + f.getParentDirectory().getFullPathName());
           #if JUCE_WINDOWS
            // %APPDATA% is readable by this user only.
            return f.replaceWithText (text) ? juce::Result::ok() : juce::Result::fail ("Cannot write " + f.getFullPathName());
           #else
            const auto path = f.getFullPathName();
            const int fd = ::open (path.toRawUTF8(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd < 0)
                return juce::Result::fail ("Cannot write " + path);
            ::fchmod (fd, S_IRUSR | S_IWUSR);   // also when the file already existed with other permissions
            const auto bytes = std::string (text.toRawUTF8());
            const auto written = ::write (fd, bytes.data(), bytes.size());
            ::close (fd);
            return written == (ssize_t) bytes.size() ? juce::Result::ok() : juce::Result::fail ("Cannot write " + path);
           #endif
        }
    }

    TokenStore::TokenStore (Backend b, juce::File f, juce::String s)
        : backend (b), file (std::move (f)), service (std::move (s)) {}

    juce::File TokenStore::defaultFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("dooblr").getChildFile ("github-token");
    }

    TokenStore TokenStore::defaultStore()
    {
        const auto forced = juce::SystemStats::getEnvironmentVariable ("DOOBLR_INSTALLER_TOKEN_FILE", {});
        if (forced.isNotEmpty())
            return TokenStore (Backend::file, juce::File (forced));
        return TokenStore (Backend::system, defaultFile());
    }

    juce::String TokenStore::systemStoreName()
    {
       #if JUCE_MAC
        return "macOS Keychain";
       #elif JUCE_WINDOWS
        return "Windows Credential Manager";
       #else
        return "settings file";
       #endif
    }

    bool TokenStore::looksLikeToken (const juce::String& t)
    {
        return t.length() >= 20 && t.length() <= 255
            && t.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    }

    juce::String TokenStore::load (juce::String& where) const
    {
        if (backend == Backend::system)
        {
            auto t = systemLoad (service).trim();
            if (t.isNotEmpty()) { where = systemStoreName(); return t; }
        }
        if (file.existsAsFile())
        {
            auto t = file.loadFileAsString().trim();
            if (t.isNotEmpty()) { where = "settings file " + file.getFullPathName(); return t; }
        }
        return {};
    }

    juce::Result TokenStore::save (const juce::String& tokenIn, juce::String& where) const
    {
        const auto token = tokenIn.trim();
        if (! looksLikeToken (token))
            return juce::Result::fail ("That does not look like a GitHub token (it should be one word of letters, digits and _ , like github_pat_... or ghp_...).");

        if (backend == Backend::system)
        {
            auto r = systemSave (service, token);
            if (r.wasOk())
            {
                file.deleteFile();   // no stale copy in the fallback file
                where = systemStoreName();
                return r;
            }
        }

        auto r = writePrivateFile (file, token);
        if (r.wasOk())
            where = "settings file " + file.getFullPathName();
        return r;
    }

    juce::Result TokenStore::forget() const
    {
        bool ok = true;
        if (backend == Backend::system)
            ok = systemForget (service);
        if (file.existsAsFile())
            ok = file.deleteFile() && ok;
        return ok ? juce::Result::ok() : juce::Result::fail ("Could not remove the saved token");
    }
}
