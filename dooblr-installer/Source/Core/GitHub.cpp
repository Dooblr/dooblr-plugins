#include "GitHub.h"
#include "TokenStore.h"

#include <map>
#include <mutex>
#include <regex>
#include <string>

namespace dooblr::installer
{
    //==============================================================================
    // Redaction

    namespace
    {
        std::mutex secretsLock;
        juce::StringArray& secrets()
        {
            static juce::StringArray s;
            return s;
        }

        bool isSecretParameter (std::string name)
        {
            for (auto& c : name) c = (char) std::tolower ((unsigned char) c);
            static const std::regex exact (R"(^(sig|signature|jwt|key|api_key|apikey|auth|authorization|code|password|passwd|pwd|credential|credentials|x-amz-signature|x-amz-credential|x-amz-security-token|x-goog-signature|x-goog-credential)$)");
            return std::regex_match (name, exact)
                || name.find ("token") != std::string::npos
                || name.find ("secret") != std::string::npos
                || name.find ("signature") != std::string::npos;
        }

        std::string redactUrlStd (std::string s)
        {
            static const std::regex userinfo (R"(([A-Za-z][A-Za-z0-9+.\-]*://)[^/@\s?#]+@)");
            s = std::regex_replace (s, userinfo, "$1[redacted]@");

            const auto q = s.find ('?');
            if (q == std::string::npos)
                return s;

            auto head = s.substr (0, q + 1);
            auto rest = s.substr (q + 1);
            std::string fragment;
            if (const auto h = rest.find ('#'); h != std::string::npos)
            {
                fragment = rest.substr (h);
                rest = rest.substr (0, h);
            }

            std::string out;
            size_t start = 0;
            for (;;)
            {
                const auto amp = rest.find ('&', start);
                auto part = rest.substr (start, amp == std::string::npos ? std::string::npos : amp - start);
                const auto eq = part.find ('=');
                if (eq != std::string::npos && isSecretParameter (part.substr (0, eq)))
                    part = part.substr (0, eq) + "=[redacted]";
                out += part;
                if (amp == std::string::npos)
                    break;
                out += '&';
                start = amp + 1;
            }
            return head + out + fragment;
        }
    }

    void registerSecret (const juce::String& secret)
    {
        if (secret.length() < 4)
            return;
        std::scoped_lock l (secretsLock);
        secrets().addIfNotAlreadyThere (secret);
    }

    juce::String redactUrl (const juce::String& url)
    {
        return redactSecrets (url);
    }

    juce::String redactSecrets (const juce::String& text)
    {
        auto s = text;
        {
            std::scoped_lock l (secretsLock);
            for (auto& secret : secrets())
                s = s.replace (secret, "[redacted]");
        }

        std::string str = s.toStdString();
        static const std::regex tokens (R"(\b(gh[pousr]_[A-Za-z0-9]{16,}|github_pat_[A-Za-z0-9_]{20,}))");
        str = std::regex_replace (str, tokens, "[redacted-token]");
        static const std::regex bearer (R"((Authorization:\s*(Bearer|token|Basic)\s+|Bearer\s+)[^\s"']+)", std::regex::icase);
        str = std::regex_replace (str, bearer, "$1[redacted]");

        static const std::regex urls (R"([A-Za-z][A-Za-z0-9+.\-]*://[^\s"'<>]+)");
        std::string out;
        auto last = str.cbegin();
        for (std::sregex_iterator it (str.cbegin(), str.cend(), urls), end; it != end; ++it)
        {
            out.append (last, (*it)[0].first);
            out += redactUrlStd ((*it)[0].str());
            last = (*it)[0].second;
        }
        out.append (last, str.cend());
        return juce::String (out);
    }

    juce::String installerVersion()
    {
       #ifdef DOOBLRINSTALLER_VERSION
        return DOOBLRINSTALLER_VERSION;
       #else
        return "unknown";
       #endif
    }

    juce::String osDescription()
    {
        juce::String arch =
           #if JUCE_ARM
            "arm64";
           #elif JUCE_64BIT
            "x86_64";
           #else
            "x86";
           #endif
        return juce::SystemStats::getOperatingSystemName().replace ("Mac OSX", "macOS") + " (" + arch + ")";
    }

    juce::String httpStatusText (int status)
    {
        switch (status)
        {
            case 0:   return "no answer";
            case 200: return "OK";
            case 301: case 302: case 303: case 307: case 308: return "redirect";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 429: return "Too Many Requests";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            default:  return {};
        }
    }

    void ErrorInfo::setUrl (const juce::String& u)  { url = redactUrl (u); }

    juce::String ErrorInfo::shortText() const
    {
        juce::String s;
        s << message;
        if (details.isNotEmpty()) s << "\n" << details;
        if (step.isNotEmpty())    s << "\nStep: " << step;
        if (url.isNotEmpty())     s << "\nURL: " << url;
        if (httpStatus != 0)      s << "\nHTTP status: " << httpStatus << " " << httpStatusText (httpStatus);
        return redactSecrets (s);
    }

    juce::String ErrorInfo::copyText() const
    {
        juce::String s;
        s << "dooblr installer error details\n"
          << "Installer: " << installerVersion() << "\n"
          << "OS: " << osDescription() << "\n"
          << "Step: " << (step.isNotEmpty() ? step : juce::String ("-")) << "\n"
          << "URL: " << (url.isNotEmpty() ? url : juce::String ("-")) << "\n"
          << "HTTP status: " << (httpStatus != 0 ? juce::String (httpStatus) + " " + httpStatusText (httpStatus) : juce::String ("none (no HTTP answer)")) << "\n"
          << "Message: " << message << "\n";
        if (details.isNotEmpty())
            s << "Details: " << details << "\n";
        if (needsSignIn)
            s << "Sign-in needed: yes (a GitHub token with read access to the release)\n";
        s << "Time: " << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") << " local\n";
        return redactSecrets (s);
    }

    //==============================================================================
    GitHubConfig GitHubConfig::fromEnvironment()
    {
        GitHubConfig c;
        const auto api = juce::SystemStats::getEnvironmentVariable ("DOOBLR_GITHUB_API", {});
        if (api.isNotEmpty()) c.apiBase = api.trimCharactersAtEnd ("/");
        const auto hosts = juce::SystemStats::getEnvironmentVariable ("DOOBLR_GITHUB_WEB_HOSTS", {});
        if (hosts.isNotEmpty())
        {
            c.webHosts.addTokens (hosts, ",", "");
            c.webHosts.trim();
            c.webHosts.removeEmptyStrings();
        }
        return c;
    }

    juce::String ReleaseRef::releaseApiPath() const
    {
        return "/repos/" + owner + "/" + repo + (tag.isEmpty() ? juce::String ("/releases/latest") : "/releases/tags/" + tag);
    }

    namespace
    {
        juce::String hostAndPort (const juce::URL& u)
        {
            auto h = u.getDomain().toLowerCase();
            if (u.getPort() != 0) h << ":" << u.getPort();
            return h;
        }

        juce::String origin (const juce::URL& u)
        {
            return u.getScheme().toLowerCase() + "://" + hostAndPort (u);
        }

        juce::URL resolveLocation (const juce::URL& current, const juce::String& location)
        {
            if (location.contains ("://"))
                return juce::URL::createWithoutParsing (location);
            if (location.startsWith ("//"))
                return juce::URL::createWithoutParsing (current.getScheme() + ":" + location);
            if (location.startsWithChar ('/'))
                return juce::URL::createWithoutParsing (origin (current) + location);
            auto base = current.toString (false);
            return juce::URL::createWithoutParsing (base.upToLastOccurrenceOf ("/", true, false) + location);
        }
    }

    ReleaseRef parseReleaseUrl (const juce::URL& u, const GitHubConfig& c)
    {
        ReleaseRef r;
        const auto scheme = u.getScheme().toLowerCase();
        if (scheme != "https" && scheme != "http")
            return r;
        bool known = false;
        for (auto& h : c.webHosts)
            known = known || h.equalsIgnoreCase (hostAndPort (u)) || h.equalsIgnoreCase (u.getDomain());
        if (! known)
            return r;

        auto parts = juce::StringArray::fromTokens (u.getSubPath(), "/", "");
        parts.removeEmptyStrings();
        if (parts.size() == 6 && parts[2] == "releases" && parts[3] == "latest" && parts[4] == "download")
        {
            r.owner = parts[0]; r.repo = parts[1]; r.assetName = juce::URL::removeEscapeChars (parts[5]);
        }
        else if (parts.size() == 6 && parts[2] == "releases" && parts[3] == "download")
        {
            r.owner = parts[0]; r.repo = parts[1]; r.tag = parts[4]; r.assetName = juce::URL::removeEscapeChars (parts[5]);
        }
        return r;
    }

    //==============================================================================
    // Tokens

    namespace
    {
        juce::Array<TokenCandidate> envTokens (const TokenSources& s)
        {
            juce::Array<TokenCandidate> out;
            if (s.env)
                for (auto name : { "DOOBLR_GITHUB_TOKEN", "GITHUB_TOKEN", "GH_TOKEN" })
                    if (auto t = s.env (name).trim(); t.isNotEmpty())
                        out.add ({ t, name });
            return out;
        }

        juce::Array<TokenCandidate> savedTokens (const TokenSources& s)
        {
            juce::Array<TokenCandidate> out;
            if (s.saved)
            {
                juce::String where;
                if (auto t = s.saved (where).trim(); t.isNotEmpty())
                    out.add ({ t, "saved token (" + (where.isNotEmpty() ? where : juce::String ("installer")) + ")" });
            }
            return out;
        }

        juce::Array<TokenCandidate> ghTokens (const TokenSources& s)
        {
            juce::Array<TokenCandidate> out;
            if (s.ghCli)
                if (auto t = s.ghCli().trim(); t.isNotEmpty())
                    out.add ({ t, "gh auth token" });
            return out;
        }

        using Stage = juce::Array<TokenCandidate> (*) (const TokenSources&);
        constexpr Stage stages[] = { envTokens, savedTokens, ghTokens };
    }

    TokenSources TokenSources::system()
    {
        TokenSources s;
        s.env = [] (const juce::String& n) { return juce::SystemStats::getEnvironmentVariable (n, {}); };
        s.saved = [] (juce::String& where) { return TokenStore::defaultStore().load (where); };
        if (juce::SystemStats::getEnvironmentVariable ("DOOBLR_INSTALLER_NO_GH", {}) != "1")
            s.ghCli = [] { return ghCliToken(); };
        return s;
    }

    TokenSources TokenSources::none()  { return {}; }

    juce::Array<TokenCandidate> findTokens (const TokenSources& s)
    {
        juce::Array<TokenCandidate> out;
        for (auto stage : stages)
            for (auto& c : stage (s))
            {
                bool dup = false;
                for (auto& o : out) dup = dup || o.token == c.token;
                if (! dup) out.add (c);
            }
        return out;
    }

    juce::String ghCliToken()
    {
        juce::Array<juce::File> places;
       #if JUCE_WINDOWS
        const juce::String exe = "gh.exe";
        places.add (juce::File ("C:\\Program Files\\GitHub CLI\\gh.exe"));
        const auto sep = ";";
       #else
        const juce::String exe = "gh";
        places.add (juce::File ("/opt/homebrew/bin/gh"));
        places.add (juce::File ("/usr/local/bin/gh"));
        const auto sep = ":";
       #endif
        for (auto& dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), sep, ""))
            if (dir.isNotEmpty() && juce::File::isAbsolutePath (dir))
                places.add (juce::File (dir).getChildFile (exe));

        for (auto& gh : places)
        {
            if (! gh.existsAsFile())
                continue;
            juce::ChildProcess p;
            if (! p.start (juce::StringArray { gh.getFullPathName(), "auth", "token" }, juce::ChildProcess::wantStdOut))
                continue;
            const auto out = p.readAllProcessOutput().trim();
            p.waitForProcessToFinish (5000);
            if (p.getExitCode() == 0 && TokenStore::looksLikeToken (out))
                return out;
            return {};
        }
        return {};
    }

    //==============================================================================
    // HTTP

    HttpResponse httpGet (const juce::URL& start, const juce::String& bearerToken, const juce::String& accept,
                          ErrorInfo& error, int maxRedirects, int timeoutMs)
    {
        HttpResponse r;
        if (start.isLocalFile())
        {
            const auto f = start.getLocalFile();
            r.finalUrl = start.toString (false);
            if (! f.existsAsFile())
            {
                error.setUrl (start.toString (false));
                error.message = "File not found: " + f.getFullPathName();
                return r;
            }
            r.totalLength = f.getSize();
            r.stream = f.createInputStream();
            if (r.stream == nullptr) { error.setUrl (start.toString (false)); error.message = "Cannot read " + f.getFullPathName(); }
            return r;
        }

        if (bearerToken.isNotEmpty())
            registerSecret (bearerToken);

        const auto authOrigin = origin (start);
        auto current = start;
        for (int hop = 0; hop <= maxRedirects; ++hop)
        {
            const bool sendAuth = bearerToken.isNotEmpty() && origin (current) == authOrigin;
            juce::String headers;
            headers << "User-Agent: dooblr-installer/" << installerVersion() << "\r\n";
            if (accept.isNotEmpty())
                headers << "Accept: " << accept << "\r\n";
            if (sendAuth)
                headers << "Authorization: Bearer " << bearerToken << "\r\n"
                        << "X-GitHub-Api-Version: 2022-11-28\r\n";

            int status = 0;
            juce::StringPairArray responseHeaders;
            auto s = current.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                    .withExtraHeaders (headers)
                                                    .withConnectionTimeoutMs (timeoutMs)
                                                    .withNumRedirectsToFollow (0)
                                                    .withStatusCode (&status)
                                                    .withResponseHeaders (&responseHeaders));
            r.status = status;
            r.finalUrl = current.toString (true);

            if (status >= 300 && status < 400 && status != 304)
            {
                const auto location = responseHeaders.getValue ("Location", {});
                if (location.isEmpty())
                {
                    error.setUrl (r.finalUrl);
                    error.httpStatus = status;
                    error.message = "The server sent a redirect without a Location.";
                    return r;
                }
                current = resolveLocation (current, location.trim());
                continue;
            }

            if (s != nullptr && (status == 0 || (status >= 200 && status < 300)))
            {
                r.totalLength = s->getTotalLength();
                r.stream = std::move (s);
                return r;
            }

            error.setUrl (r.finalUrl);
            error.httpStatus = status;
            if (status == 0)
                error.message = "No connection to " + current.getDomain() + " (offline, blocked by a firewall or proxy, or the address is wrong).";
            else
                error.message = "The server answered HTTP " + juce::String (status) + " " + httpStatusText (status) + ".";
            return r;
        }

        error.setUrl (current.toString (true));
        error.message = "Too many redirects.";
        return r;
    }

    namespace
    {
        void attachApiUrls (Manifest& m, const std::map<juce::String, juce::String>& byName)
        {
            auto attach = [&byName] (Asset& a)
            {
                const auto name = juce::URL::removeEscapeChars (a.fileName());
                if (auto it = byName.find (name); name.isNotEmpty() && it != byName.end())
                    a.apiUrl = it->second;
            };
            attach (m.installerMac);
            attach (m.installerWindows);
            for (auto& item : m.items)
            {
                attach (item.mac);
                attach (item.windows);
                attach (item.any);
            }
        }
    }

    bool fetchManifest (const juce::URL& url, Manifest& m, Session& session, ErrorInfo& error,
                        const TokenSources& sources, const GitHubConfig& config)
    {
        session = {};
        error = {};
        const juce::String step = "Getting the plug-in list (the manifest)";

        ErrorInfo anon;
        anon.step = step;
        {
            auto r = httpGet (url, {}, {}, anon);
            if (r.ok())
            {
                const auto text = r.stream->readEntireStreamAsString();
                const auto parsed = Manifest::parse (text, url, m);
                if (parsed.wasOk())
                    return true;
                error.step = step;
                error.setUrl (url.toString (true));
                error.httpStatus = r.status;
                error.message = parsed.getErrorMessage();
                return false;
            }
        }

        const auto ref = parseReleaseUrl (url, config);
        const auto st = anon.httpStatus;
        if (! ref.isValid() || ! (st == 401 || st == 403 || st == 404))
        {
            error = anon;
            if (st == 404)
                error.message << " The file is not there (or the release is private).";
            return false;
        }

        const auto repoName = ref.owner + "/" + ref.repo;
        const auto apiUrl = config.apiBase + ref.releaseApiPath();
        juce::StringArray refused;
        juce::Array<juce::String> triedTokens;
        int lastStatus = 0;

        for (auto stage : stages)
        {
            for (auto& c : stage (sources))   // later sources (gh auth token) are only asked when the earlier ones failed
            {
                if (triedTokens.contains (c.token))
                    continue;
                triedTokens.add (c.token);
                registerSecret (c.token);

                ErrorInfo e;
                e.step = "Reading the release from the GitHub API";
                auto r = httpGet (juce::URL::createWithoutParsing (apiUrl), c.token, "application/vnd.github+json", e);
                if (! r.ok())
                {
                    if (e.httpStatus == 401 || e.httpStatus == 403 || e.httpStatus == 404)
                    {
                        refused.add (c.source + " (HTTP " + juce::String (e.httpStatus) + " " + httpStatusText (e.httpStatus) + ")");
                        lastStatus = e.httpStatus;
                        continue;
                    }
                    error = e;
                    error.details = "Token from " + c.source + ".";
                    return false;
                }

                const auto json = juce::JSON::parse (r.stream->readEntireStreamAsString());
                std::map<juce::String, juce::String> byName;
                if (auto* assets = json.getProperty ("assets", {}).getArray())
                    for (auto& a : *assets)
                        if (a.getProperty ("name", {}).toString().isNotEmpty() && a.getProperty ("url", {}).toString().isNotEmpty())
                            byName[a.getProperty ("name", {}).toString()] = a.getProperty ("url", {}).toString();

                const auto tagName = json.getProperty ("tag_name", {}).toString();
                const auto it = byName.find (ref.assetName);
                if (it == byName.end())
                {
                    error = e;
                    error.setUrl (apiUrl);
                    error.httpStatus = r.status;
                    error.message = "The GitHub release " + (tagName.isNotEmpty() ? tagName + " " : juce::String()) + "of " + repoName + " has no file named " + ref.assetName + ".";
                    error.details = "Token from " + c.source + ".";
                    return false;
                }

                ErrorInfo e2;
                e2.step = "Downloading the plug-in list through the GitHub API";
                auto mr = httpGet (juce::URL::createWithoutParsing (it->second), c.token, "application/octet-stream", e2);
                if (! mr.ok())
                {
                    error = e2;
                    error.details = "Token from " + c.source + ".";
                    return false;
                }

                const auto text = mr.stream->readEntireStreamAsString();
                const auto parsed = Manifest::parse (text, url, m);
                if (parsed.failed())
                {
                    error = e2;
                    error.setUrl (it->second);
                    error.httpStatus = mr.status;
                    error.message = parsed.getErrorMessage();
                    return false;
                }

                attachApiUrls (m, byName);
                session.token = c.token;
                session.tokenSource = c.source;
                session.apiBase = config.apiBase;
                return true;
            }
        }

        error = anon;
        error.needsSignIn = true;
        if (refused.isEmpty())
        {
            error.message = "GitHub answered " + juce::String (st) + " " + httpStatusText (st) + " for the plug-in list. The dooblr releases are in a private GitHub repository ("
                          + repoName + "), so the installer needs a GitHub token to download them.";
            error.details = "No token found (looked in DOOBLR_GITHUB_TOKEN, GITHUB_TOKEN, GH_TOKEN, the token saved by the installer, and `gh auth token`).";
        }
        else
        {
            error.step = "Signing in to GitHub";
            error.setUrl (apiUrl);
            error.httpStatus = lastStatus;
            error.message = "GitHub did not accept the token for " + repoName + ". It may have expired or been revoked, or it does not include this repository with Contents: Read-only.";
            error.details = "Tried: " + refused.joinIntoString ("; ") + ".";
        }
        return false;
    }

    HttpResponse openAsset (const Asset& a, const Session* session, ErrorInfo& error)
    {
        if (session != nullptr && session->authenticated() && a.apiUrl.isNotEmpty())
            return httpGet (juce::URL::createWithoutParsing (a.apiUrl), session->token, "application/octet-stream", error);
        return httpGet (a.url, {}, {}, error);
    }

    //==============================================================================
    // Getting a token

    juce::URL tokenCreationUrl (const juce::String& owner, const juce::String& repo)
    {
        return juce::URL ("https://github.com/settings/personal-access-tokens/new")
            .withParameter ("name", "dooblr installer")
            .withParameter ("description", "Lets the dooblr installer download the plug-ins from the private " + owner + "/" + repo
                                           + " releases. Repository access: Only select repositories > " + repo + ". Contents: Read-only.")
            .withParameter ("target_name", owner)
            .withParameter ("expires_in", juce::String (tokenExpiryDays))
            .withParameter ("contents", "read");
    }

    bool looksLikeGitHubToken (const juce::String& text)
    {
        const auto t = text.trim();
        if (t.length() < 30 || t.length() > 255)
            return false;
        static const std::regex re (R"(^(github_pat_[A-Za-z0-9_]{22,}|ghp_[A-Za-z0-9]{30,})$)");
        return std::regex_match (t.toStdString(), re);
    }

    bool validateToken (const juce::String& tokenIn, const juce::String& owner, const juce::String& repo,
                        ErrorInfo& error, const GitHubConfig& config)
    {
        error = {};
        const auto token = tokenIn.trim();
        const auto repoName = owner + "/" + repo;
        const auto apiUrl = config.apiBase + "/repos/" + owner + "/" + repo;
        error.step = "Checking the GitHub token";
        if (token.isEmpty())
        {
            error.setUrl (apiUrl);
            error.message = "No token to check.";
            error.needsSignIn = true;
            return false;
        }
        registerSecret (token);
        auto r = httpGet (juce::URL::createWithoutParsing (apiUrl), token, "application/vnd.github+json", error);
        if (r.ok())
        {
            const auto json = juce::JSON::parse (r.stream->readEntireStreamAsString());
            const auto name = json.getProperty ("full_name", {}).toString();
            if (name.isEmpty() || name.equalsIgnoreCase (repoName))
            {
                // The repository is visible (that needs only Metadata); the releases need Contents: Read-only.
                ErrorInfo re;
                const auto relUrl = apiUrl + "/releases/latest";
                auto rr = httpGet (juce::URL::createWithoutParsing (relUrl), token, "application/vnd.github+json", re);
                if (rr.ok())
                {
                    error = {};
                    return true;
                }
                error = re;
                error.step = "Checking the GitHub token";
                if (re.httpStatus == 401 || re.httpStatus == 403 || re.httpStatus == 404)
                {
                    error.needsSignIn = true;
                    error.message = "The token can see " + repoName + " but cannot read its releases (HTTP " + juce::String (re.httpStatus)
                                    + "). On GitHub, edit the token: Permissions > Contents: Read-only.";
                }
                return false;
            }
            error.setUrl (apiUrl);
            error.httpStatus = r.status;
            error.message = "GitHub answered for " + name + " instead of " + repoName + ".";
            return false;
        }
        const auto st = error.httpStatus;
        error.step = "Checking the GitHub token";
        if (st == 401)
        {
            error.needsSignIn = true;
            error.message = "GitHub did not accept this token (HTTP 401). It may be incomplete, expired or revoked. Copy it again from GitHub, or make a new one.";
        }
        else if (st == 403 || st == 404)
        {
            error.needsSignIn = true;
            error.message = "The token works, but it cannot read " + repoName + " (HTTP " + juce::String (st) + "). On GitHub, edit the token: "
                            "Resource owner " + owner + ", Repository access > Only select repositories > " + repo + ", Permissions > Contents: Read-only.";
        }
        else if (st != 0)
            error.message = "GitHub answered HTTP " + juce::String (st) + " " + httpStatusText (st) + " while checking the token. Try again in a minute.";
        // st == 0: httpGet's "No connection to ..." message stays.
        return false;
    }
}
