#pragma once

#include "Manifest.h"

#include <functional>
#include <memory>

// Downloads from the GitHub release, with a GitHub token when the repository is private, and
// the error details the window and the command line show (and let you copy).
//
// The anonymous release URL is always tried first. When GitHub answers 401/403/404 the release is
// read through the REST API with a token: GET /repos/<owner>/<repo>/releases/latest (or
// /releases/tags/<tag>), then each file (the manifest, the zips) by name from its asset API URL
// with "Accept: application/octet-stream". Redirects are followed by hand so the Authorization
// header only goes to the API host, never to the storage host GitHub redirects to.

namespace dooblr::installer
{
    //==============================================================================
    /** What went wrong, in a form the window shows in selectable text and copies to the clipboard. */
    struct ErrorInfo
    {
        juce::String step;        // "Getting the plug-in list", "Downloading dooblr-q 0.9.5", ...
        juce::String url;         // always stored redacted (see setUrl)
        int httpStatus = 0;       // 0 = no HTTP answer (no connection, or a local file)
        juce::String message;     // what happened, in plain words
        juce::String details;     // extra lines (which token source was tried, ...), never a secret
        bool needsSignIn = false; // the release is private and no token (or no working token) was found

        void setUrl (const juce::String& u);
        bool isEmpty() const { return message.isEmpty(); }

        /** The full report for "Copy details" and the command line: installer version, OS, step,
            URL (redacted), HTTP status, message, details and time. Secrets are always removed. */
        juce::String copyText() const;
        /** Step, message, URL and status for the window's error box (also redacted). */
        juce::String shortText() const;
    };

    /** Removes secrets from a URL: user:password@, and the values of query parameters that carry
        credentials (token, access_token, sig, signature, jwt, X-Amz-Signature, ...). */
    juce::String redactUrl (const juce::String& url);
    /** Removes GitHub tokens (ghp_..., github_pat_..., ...), "Bearer ..." values, every URL's
        secrets (redactUrl) and every token registered with registerSecret from any text. */
    juce::String redactSecrets (const juce::String& text);
    /** Remembers a token in use so redactSecrets removes it even if it has no known prefix. */
    void registerSecret (const juce::String& secret);

    juce::String installerVersion();
    /** "macOS 15.1 (arm64)" style text for the error details. */
    juce::String osDescription();
    juce::String httpStatusText (int status);

    //==============================================================================
    struct GitHubConfig
    {
        juce::String apiBase { "https://api.github.com" };
        juce::StringArray webHosts { "github.com" };    // hosts whose /<owner>/<repo>/releases/... URLs are GitHub releases

        /** Defaults, plus DOOBLR_GITHUB_API and DOOBLR_GITHUB_WEB_HOSTS (comma separated) for tests. */
        static GitHubConfig fromEnvironment();
    };

    /** "https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json". */
    struct ReleaseRef
    {
        juce::String owner, repo, tag, assetName;   // tag empty = latest
        bool isValid() const { return owner.isNotEmpty() && repo.isNotEmpty() && assetName.isNotEmpty(); }
        juce::String releaseApiPath() const;         // "/repos/<o>/<r>/releases/latest" or "/releases/tags/<t>"
    };
    ReleaseRef parseReleaseUrl (const juce::URL&, const GitHubConfig&);

    //==============================================================================
    struct TokenCandidate
    {
        juce::String token;
        juce::String source;   // "DOOBLR_GITHUB_TOKEN", "saved token (macOS Keychain)", "gh auth token"; never the token
    };

    /** Where tokens come from, in this order: the environment (DOOBLR_GITHUB_TOKEN, then
        GITHUB_TOKEN, then GH_TOKEN), the token saved by the installer, then `gh auth token`.
        Each part can be replaced (tests use fakes so no real token is ever read). */
    struct TokenSources
    {
        std::function<juce::String (const juce::String& name)> env;
        std::function<juce::String (juce::String& whereSaved)> saved;
        std::function<juce::String()> ghCli;

        static TokenSources system();
        static TokenSources none();
    };

    /** Every token found, in lookup order, without duplicates. */
    juce::Array<TokenCandidate> findTokens (const TokenSources&);

    /** `gh auth token` from the GitHub CLI when it is installed (PATH, /opt/homebrew/bin,
        /usr/local/bin, Program Files); empty otherwise. */
    juce::String ghCliToken();

    /** The token that worked for the manifest, used for the downloads. Kept in memory only. */
    struct Session
    {
        juce::String token, tokenSource, apiBase;
        bool authenticated() const { return token.isNotEmpty(); }
    };

    //==============================================================================
    /** One GET with redirects followed by hand. bearerToken (when set) is sent only to the first
        URL's origin (scheme, host and port); a redirect to another origin gets no Authorization. */
    struct HttpResponse
    {
        std::unique_ptr<juce::InputStream> stream;   // set on 2xx (or a local file)
        int status = 0;
        juce::int64 totalLength = -1;
        juce::String finalUrl;
        bool ok() const { return stream != nullptr; }
    };
    HttpResponse httpGet (const juce::URL&, const juce::String& bearerToken, const juce::String& accept,
                          ErrorInfo& error, int maxRedirects = 8, int timeoutMs = 20000);

    /** Reads the manifest: anonymously first; on 401/403/404 from a GitHub release URL through the
        API with each token found until one works. Fills each asset's apiUrl when the API was used.
        On failure error says what happened; error.needsSignIn when a (working) token is needed. */
    bool fetchManifest (const juce::URL&, Manifest&, Session&, ErrorInfo& error,
                        const TokenSources& = TokenSources::system(),
                        const GitHubConfig& = GitHubConfig::fromEnvironment());

    /** Opens an asset for download: through its API URL with the session's token when it has one,
        otherwise its public URL. */
    HttpResponse openAsset (const Asset&, const Session*, ErrorInfo& error);
}
