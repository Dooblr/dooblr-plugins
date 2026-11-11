// Console tests for the dooblr installer engine and window. Everything is written under a temp
// folder; the real plug-in folders are only read. DOOBLRINSTALLER_SNAPSHOT=<prefix> also renders
// the window pages to <prefix>-<page>.png.

#include "UI/InstallerComponent.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <vector>

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
 #include <sys/stat.h>
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


    void setEnv (const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());   // "" removes it
       #else
        if (value.isEmpty()) unsetenv (name);
        else                 setenv (name, value.toRawUTF8(), 1);
       #endif
    }

    //==========================================================================
    /** A tiny HTTP/1.1 server on 127.0.0.1 for the GitHub tests. Records every request's headers. */
    struct MockRequest
    {
        juce::String method, path;
        juce::StringPairArray headers;   // case-insensitive keys
    };

    struct MockResponse
    {
        int status = 200;
        juce::StringPairArray headers;
        juce::MemoryBlock body;

        static MockResponse text (int status, const juce::String& t)
        {
            MockResponse r;
            r.status = status;
            r.body.append (t.toRawUTF8(), t.getNumBytesAsUTF8());
            return r;
        }
    };

    class MockServer : private juce::Thread
    {
    public:
        explicit MockServer (std::function<MockResponse (const MockRequest&)> h) : juce::Thread ("mock http"), handler (std::move (h)) {}

        ~MockServer() override
        {
            signalThreadShouldExit();
            { juce::StreamingSocket wake; wake.connect ("127.0.0.1", port, 300); }
            stopThread (3000);
            listener.close();
        }

        bool start()
        {
            for (int i = 0; i < 40; ++i)
            {
                const int p = 20000 + juce::Random::getSystemRandom().nextInt (30000);
                if (listener.createListener (p, "127.0.0.1")) { port = p; startThread(); return true; }
            }
            return false;
        }

        int getPort() const { return port; }

        std::vector<MockRequest> requests() const
        {
            std::lock_guard<std::mutex> l (lock);
            return seen;
        }

        int count (const juce::String& pathStart) const
        {
            int n = 0;
            for (auto& r : requests()) n += r.path.startsWith (pathStart) ? 1 : 0;
            return n;
        }

    private:
        void run() override
        {
            while (! threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
                if (c == nullptr || threadShouldExit()) break;
                serve (*c);
            }
        }

        void serve (juce::StreamingSocket& c)
        {
            juce::MemoryBlock in;
            char buf[4096];
            for (int guard = 0; guard < 200; ++guard)
            {
                const auto s = juce::String::fromUTF8 ((const char*) in.getData(), (int) in.getSize());
                if (s.contains ("\r\n\r\n")) break;
                if (c.waitUntilReady (true, 2000) != 1) return;
                const int n = c.read (buf, sizeof (buf), false);
                if (n <= 0) return;
                in.append (buf, (size_t) n);
            }
            const auto head = juce::String::fromUTF8 ((const char*) in.getData(), (int) in.getSize()).upToFirstOccurrenceOf ("\r\n\r\n", false, false);
            auto lines = juce::StringArray::fromLines (head);
            MockRequest req;
            req.method = lines[0].upToFirstOccurrenceOf (" ", false, false);
            req.path = lines[0].fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);
            for (int i = 1; i < lines.size(); ++i)
                if (lines[i].contains (":"))
                    req.headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim(), lines[i].fromFirstOccurrenceOf (":", false, false).trim());
            { std::lock_guard<std::mutex> l (lock); seen.push_back (req); }

            const auto res = handler (req);
            juce::String h;
            h << "HTTP/1.1 " << res.status << " " << (res.status == 200 ? "OK" : res.status == 302 ? "Found" : "Error") << "\r\n"
              << "Content-Length: " << (int) res.body.getSize() << "\r\nConnection: close\r\n";
            for (auto& k : res.headers.getAllKeys())
                h << k << ": " << res.headers[k] << "\r\n";
            h << "\r\n";
            c.write (h.toRawUTF8(), (int) h.getNumBytesAsUTF8());
            if (res.body.getSize() > 0)
                c.write (res.body.getData(), (int) res.body.getSize());
            c.close();
        }

        std::function<MockResponse (const MockRequest&)> handler;
        juce::StreamingSocket listener;
        int port = 0;
        mutable std::mutex lock;
        std::vector<MockRequest> seen;
    };

    const juce::String goodToken = "github_pat_TESTONLY0good0token0abcdefghijklmnop";
    const juce::String badToken  = "github_pat_TESTONLY0bad00token0abcdefghijklmnop";
    const juce::String noRepoToken = "github_pat_TESTONLY0norepo0token0abcdefghijklm";       // valid, but dooblr-plugins not selected
    const juce::String noContentsToken = "github_pat_TESTONLY0nocont0token0abcdefghijklm";   // repo selected, Contents not granted

    /** A fake GitHub: "web" + API on 127.0.0.1:A (the anonymous release URL 404s, like a private
        repo); asset downloads redirect to an "objects" host on localhost:B (another origin). */
    struct FakeGitHub
    {
        juce::String manifestText;
        juce::MemoryBlock zipBytes;
        juce::String zipName;
        std::atomic<bool> publicRelease { false }, corruptZip { false };

        std::unique_ptr<MockServer> objects, api;

        bool start()
        {
            objects = std::make_unique<MockServer> ([this] (const MockRequest& r)
            {
                if (r.path.startsWith ("/objects/1?")) return MockResponse::text (200, manifestText);
                if (r.path.startsWith ("/objects/2?"))
                {
                    MockResponse res;
                    res.body = zipBytes;
                    if (corruptZip.load() && res.body.getSize() > 10) static_cast<char*> (res.body.getData())[res.body.getSize() / 2] ^= 0x5a;
                    return res;
                }
                return MockResponse::text (404, "not found");
            });
            if (! objects->start()) return false;

            api = std::make_unique<MockServer> ([this] (const MockRequest& r)
            {
                const auto base = "http://127.0.0.1:" + juce::String (api->getPort());
                const bool authed = r.headers["Authorization"] == "Bearer " + goodToken;
                if (r.path == "/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json")
                    return publicRelease.load() ? MockResponse::text (200, manifestText) : MockResponse::text (404, "Not Found");
                const auto auth = r.headers["Authorization"];
                if (r.path == "/repos/Dooblr/dooblr-plugins")
                {
                    if (authed || auth == "Bearer " + noContentsToken)
                        return MockResponse::text (200, "{\"full_name\":\"Dooblr/dooblr-plugins\",\"private\":true}");
                    if (auth.isEmpty() || auth == "Bearer " + noRepoToken)
                        return MockResponse::text (404, "{\"message\":\"Not Found\"}");
                    return MockResponse::text (401, "{\"message\":\"Bad credentials\"}");
                }
                if (r.path == "/repos/Dooblr/dooblr-plugins/releases/latest" && auth == "Bearer " + noContentsToken)
                    return MockResponse::text (403, "{\"message\":\"Resource not accessible by personal access token\"}");
                if (r.path == "/repos/Dooblr/dooblr-plugins/releases/latest")
                {
                    if (! authed) return MockResponse::text (r.headers.containsKey ("Authorization") ? 401 : 404, "{\"message\":\"Bad credentials\"}");
                    return MockResponse::text (200, "{\"tag_name\":\"v9.9.9\",\"assets\":["
                        "{\"name\":\"dooblr-manifest.json\",\"url\":\"" + base + "/repos/Dooblr/dooblr-plugins/releases/assets/1\"},"
                        "{\"name\":\"" + zipName + "\",\"url\":\"" + base + "/repos/Dooblr/dooblr-plugins/releases/assets/2\"}]}");
                }
                if (r.path.startsWith ("/repos/Dooblr/dooblr-plugins/releases/assets/"))
                {
                    if (! authed) return MockResponse::text (404, "{\"message\":\"Not Found\"}");
                    if (r.headers["Accept"] != "application/octet-stream") return MockResponse::text (200, "{\"json\":\"metadata, not the file\"}");
                    MockResponse res;
                    res.status = 302;
                    res.headers.set ("Location", "http://localhost:" + juce::String (objects->getPort()) + "/objects/"
                                                 + r.path.fromLastOccurrenceOf ("/", false, false) + "?X-Amz-Signature=SECRETSIG123&jwt=SECRETJWT456&response-content-type=x");
                    return res;
                }
                return MockResponse::text (404, "unknown");
            });
            return api->start();
        }

        juce::String apiBase() const { return "http://127.0.0.1:" + juce::String (api->getPort()); }
        juce::URL manifestUrl() const { return juce::URL (apiBase() + "/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json"); }
        GitHubConfig config() const
        {
            GitHubConfig c;
            c.apiBase = apiBase();
            c.webHosts = { "127.0.0.1:" + juce::String (api->getPort()) };
            return c;
        }
    };

    /** Fake token sources that count how often each one was asked. */
    struct FakeTokens
    {
        std::map<juce::String, juce::String> env;
        juce::String saved, gh;
        std::shared_ptr<std::atomic<int>> envAsked = std::make_shared<std::atomic<int>> (0),
                                          savedAsked = std::make_shared<std::atomic<int>> (0),
                                          ghAsked = std::make_shared<std::atomic<int>> (0);

        TokenSources sources() const
        {
            TokenSources s;
            s.env = [e = env, n = envAsked] (const juce::String& k) { ++*n; auto it = e.find (k); return it == e.end() ? juce::String() : it->second; };
            s.saved = [v = saved, n = savedAsked] (juce::String& where) { ++*n; where = "test store"; return v; };
            s.ghCli = [v = gh, n = ghAsked] { ++*n; return v; };
            return s;
        }
    };

    const auto noProgress = [] (int, int, double) { return true; };
    const auto noLog = [] (const juce::String&) {};

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
    void testTokensAndRedaction()
    {
        std::cout << "GitHub token lookup order, token store, redaction, error details" << std::endl;

        FakeTokens f;
        f.env = { { "DOOBLR_GITHUB_TOKEN", "tok_dooblr_aaaaaaaaaaaaaaaaaaaa" }, { "GITHUB_TOKEN", "tok_github_bbbbbbbbbbbbbbbbbbbb" },
                  { "GH_TOKEN", "tok_gh_cccccccccccccccccccccc" } };
        f.saved = "tok_saved_dddddddddddddddddddd";
        f.gh = "tok_ghcli_eeeeeeeeeeeeeeeeeeee";
        auto all = findTokens (f.sources());
        juce::StringArray order;
        for (auto& c : all) order.add (c.source);
        note ("order: " + order.joinIntoString (" | "));
        check (all.size() == 5 && all[0].token.startsWith ("tok_dooblr") && all[1].token.startsWith ("tok_github") && all[2].token.startsWith ("tok_gh_")
                   && all[3].token.startsWith ("tok_saved") && all[4].token.startsWith ("tok_ghcli"),
               "order: DOOBLR_GITHUB_TOKEN, GITHUB_TOKEN, GH_TOKEN, saved, gh auth token");
        check (all.size() == 5 && all[0].source == "DOOBLR_GITHUB_TOKEN" && all[3].source.containsIgnoreCase ("saved") && all[3].source.contains ("test store")
                   && all[4].source.contains ("gh"),
               "each candidate names its source");
        check (! order.joinIntoString ("").contains ("tok_"), "source names never contain the token");

        FakeTokens g;
        g.env = { { "GH_TOKEN", "tok_same_ffffffffffffffffffff" } };
        g.gh = "tok_same_ffffffffffffffffffff";
        check (findTokens (g.sources()).size() == 1, "the same token from two places is tried once");
        check (findTokens (TokenSources::none()).isEmpty(), "no sources: no tokens");
        FakeTokens h;
        h.env = { { "GITHUB_TOKEN", "  tok_trim_gggggggggggggggggggg \n" } };
        const auto trimmed = findTokens (h.sources());
        check (trimmed.size() == 1 && trimmed[0].token == "tok_trim_gggggggggggggggggggg", "tokens are trimmed");

        // Token store, file backend (temp file).
        const auto file = tempRoot().getChildFile ("token-store/github-token");
        TokenStore store (TokenStore::Backend::file, file);
        juce::String where;
        check (store.save ("not a token!", where).failed(), "save rejects text that is not a token");
        check (store.save (goodToken, where).wasOk() && file.existsAsFile(), "save writes the file");
        check (store.load (where) == goodToken && where.contains (file.getFileName()), "load returns it (" + where + ")");
       #if JUCE_MAC || JUCE_LINUX
        struct stat st {};
        check (stat (file.getFullPathName().toRawUTF8(), &st) == 0 && (st.st_mode & 0777) == 0600, "token file is user-only (0600)");
       #endif
        check (store.save (badToken, where).wasOk() && store.load (where) == badToken, "save replaces the token");
        check (store.forget().wasOk() && ! file.exists() && store.load (where).isEmpty(), "forget removes it");
        check (TokenStore::looksLikeToken (goodToken) && TokenStore::looksLikeToken ("ghp_" + juce::String::repeatedString ("A", 36))
                   && ! TokenStore::looksLikeToken ("short") && ! TokenStore::looksLikeToken ("has space in it 1234567890"),
               "looksLikeToken");

        if (juce::SystemStats::getEnvironmentVariable ("DOOBLRINSTALLER_TEST_KEYCHAIN", {}) == "1")
        {
            // Opt-in: its own service name, so the real saved token is never read or changed.
            TokenStore sys (TokenStore::Backend::system, file, "com.dooblr.installer.selftest");
            const juce::String dummy = "dooblr_selftest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
            check (sys.save (dummy, where).wasOk(), "system store save (" + TokenStore::systemStoreName() + ")");
            check (sys.load (where) == dummy, "system store load");
            check (sys.forget().wasOk() && sys.load (where).isEmpty(), "system store forget");
        }

        // Redaction
        const auto ru = redactUrl ("https://user:hunter2@objects.example.com/x/y.zip?a=1&X-Amz-Signature=SECRETSIG&sig=S1G&jwt=J.W.T&token=TOK&response-content-type=zip#frag");
        note ("redacted URL: " + ru);
        check (ru.contains ("a=1") && ru.contains ("response-content-type=zip") && ru.contains ("objects.example.com/x/y.zip"), "redactUrl keeps the address");
        check (! ru.contains ("hunter2") && ! ru.contains ("SECRETSIG") && ! ru.contains ("S1G") && ! ru.contains ("J.W.T") && ! ru.contains ("TOK"), "redactUrl removes secrets");
        const auto rs = redactSecrets ("Authorization: Bearer abc.def-123 token ghp_" + juce::String::repeatedString ("Z", 36) + " and " + goodToken
                                       + " at https://x.test/a?token=QQQ");
        note ("redacted text: " + rs);
        check (! rs.contains ("abc.def-123") && ! rs.contains ("ZZZZZZZZ") && ! rs.contains (goodToken) && ! rs.contains ("QQQ"), "redactSecrets removes tokens");
        registerSecret ("plain-secret-value-1234567890");
        check (! redactSecrets ("x plain-secret-value-1234567890 y").contains ("plain-secret"), "registered secrets are removed");

        ErrorInfo e;
        e.step = "Getting the plug-in list";
        e.setUrl ("https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json?token=" + goodToken);
        e.httpStatus = 404;
        e.message = "GitHub answered HTTP 404 with " + goodToken;
        e.details = "Tried: DOOBLR_GITHUB_TOKEN (HTTP 401)";
        const auto ct = e.copyText();
        note ("copy text:\n" + ct);
        check (ct.contains ("Installer: " + installerVersion()) && ct.contains ("OS: ") && ct.contains ("Step: Getting the plug-in list")
                   && ct.contains ("URL: https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json")
                   && ct.contains ("HTTP status: 404") && ct.contains ("Message: ") && ct.contains ("Tried: DOOBLR_GITHUB_TOKEN"),
               "copy text has version, OS, step, URL, HTTP status, message, details");
        check (! ct.contains (goodToken) && ! e.shortText().contains (goodToken) && ! e.url.contains (goodToken), "copy text never contains a token");
        check (installerVersion() == "0.1.5", "installer version 0.1.5: " + installerVersion());

        // "Get access on GitHub": the pre-filled token page (only the parameters GitHub documents).
        {
            const auto u = tokenCreationUrl();
            const juce::URL back (u.toString (true));   // what the browser gets, parsed again
            const auto names = back.getParameterNames();
            const auto values = back.getParameterValues();
            const auto param = [&] (const juce::String& n) { const auto i = names.indexOf (n); return i >= 0 ? values[i] : juce::String ("<missing>"); };
            note ("token page: " + u.toString (true));
            check (u.toString (false) == "https://github.com/settings/personal-access-tokens/new", "token page address: " + u.toString (false));
            check (names.size() == 5 && param ("name") == "dooblr installer" && param ("target_name") == "Dooblr"
                       && param ("expires_in") == "366" && param ("contents") == "read",
                   "token page pre-fills name, owner Dooblr, 366 days, Contents read: " + names.joinIntoString (","));
            check (param ("name").length() <= 40 && param ("description").length() <= 1024
                       && param ("description").contains ("Only select repositories > dooblr-plugins"),
                   "token page name <= 40, description <= 1024 and says which repository to pick");
            check (! u.toString (true).contains (" "), "token page address is encoded");
            const auto other = juce::URL (tokenCreationUrl ("someone", "other-repo").toString (true));
            check (other.getParameterValues()[other.getParameterNames().indexOf ("target_name")] == "someone"
                       && other.getParameterValues()[other.getParameterNames().indexOf ("description")].contains ("other-repo"),
                   "token page follows the release's owner and repository");
        }

        // Noticing a copied token.
        check (looksLikeGitHubToken (goodToken) && looksLikeGitHubToken ("  " + goodToken + "\n")
                   && looksLikeGitHubToken ("ghp_" + juce::String::repeatedString ("aB3", 12)),
               "copied github_pat_ / ghp_ tokens are noticed (trimmed)");
        check (! looksLikeGitHubToken ({}) && ! looksLikeGitHubToken ("hello") && ! looksLikeGitHubToken ("github_pat_short")
                   && ! looksLikeGitHubToken ("ghp_abc") && ! looksLikeGitHubToken ("Bearer " + goodToken)
                   && ! looksLikeGitHubToken (goodToken + " and more text") && ! looksLikeGitHubToken ("https://x.com/?t=" + goodToken)
                   && ! looksLikeGitHubToken ("gho_" + juce::String::repeatedString ("aB3", 12))
                   && ! looksLikeGitHubToken ("github_pat_" + juce::String::repeatedString ("a", 300))
                   && ! looksLikeGitHubToken ("ghp_" + juce::String::repeatedString ("aB-", 12)),
               "other clipboard text is not taken for a token");

        const juce::URL latest ("https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json");
        const auto ref = parseReleaseUrl (latest, GitHubConfig());
        check (ref.isValid() && ref.owner == "Dooblr" && ref.repo == "dooblr-plugins" && ref.tag.isEmpty() && ref.assetName == "dooblr-manifest.json"
                   && ref.releaseApiPath() == "/repos/Dooblr/dooblr-plugins/releases/latest",
               "release URL (latest) parsed");
        const auto tagged = parseReleaseUrl (juce::URL ("https://github.com/Dooblr/dooblr-plugins/releases/download/v0.1.2/x.zip"), GitHubConfig());
        check (tagged.isValid() && tagged.tag == "v0.1.2" && tagged.releaseApiPath() == "/repos/Dooblr/dooblr-plugins/releases/tags/v0.1.2", "release URL (tag) parsed");
        check (! parseReleaseUrl (juce::URL ("https://example.com/Dooblr/dooblr-plugins/releases/latest/download/x.json"), GitHubConfig()).isValid(),
               "other hosts are not GitHub releases");
    }

    //==========================================================================
    void snapshot (juce::Component& c, const juce::String& name);

    bool waitFor (const std::function<bool()>& done)
    {
        for (int i = 0; i < 300 && ! done(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        return done();
    }

    void testPrivateRelease()
    {
        std::cout << "private release through the GitHub API (local mock server)" << std::endl;
        const auto root = tempRoot().getChildFile ("private");
        const auto src = root.getChildFile ("src");
        const auto zip = zipFolder (makeBundle (src, "dooblr-test", "0.5.0"), root.getChildFile ("dooblr-test-0.5.0.zip"));

        FakeGitHub gh;
        gh.zipName = zip.getFileName();
        zip.loadFileAsData (gh.zipBytes);
        {
            juce::Array<juce::var> items;
            items.add (itemVar ("dooblr-test", "0.5.0", "dooblr-test.vst3", "vst3", assetVar (zip)));
            gh.manifestText = writeManifest (root, items).loadFileAsString();
        }
        if (! gh.start()) { check (false, "mock servers start"); return; }
        const auto url = gh.manifestUrl();
        const auto cfg = gh.config();

        // 1. No token: the 404 becomes "sign in".
        {
            Manifest m; Session s; ErrorInfo e;
            const auto ok = fetchManifest (url, m, s, e, TokenSources::none(), cfg);
            note ("no token: " + e.shortText().replace ("\n", " / "));
            check (! ok && e.needsSignIn && e.httpStatus == 404, "404 without a token -> needs sign-in");
            check (e.url.contains ("/releases/latest/download/dooblr-manifest.json") && e.step.isNotEmpty() && e.message.containsIgnoreCase ("private"),
                   "the error names the URL and the step, and says the release is private");
            check (gh.api->count ("/repos/") == 0, "no API call without a token");
        }

        // 2. A wrong token: refused, still sign-in; the token never appears.
        {
            FakeTokens f;
            f.env = { { "DOOBLR_GITHUB_TOKEN", badToken } };
            Manifest m; Session s; ErrorInfo e;
            const auto ok = fetchManifest (url, m, s, e, f.sources(), cfg);
            note ("bad token: " + e.copyText().replace ("\n", " / "));
            check (! ok && e.needsSignIn && e.details.contains ("DOOBLR_GITHUB_TOKEN") && e.details.contains ("401"), "refused token -> sign-in; details name the source tried");
            check (! e.copyText().contains (badToken) && ! s.authenticated(), "the refused token is not in the error details");
        }

        // 3. Env token refused, saved token works; gh is never asked.
        const auto objectsBefore = gh.objects->requests().size();
        FakeTokens f;
        f.env = { { "DOOBLR_GITHUB_TOKEN", badToken } };
        f.saved = goodToken;
        f.gh = "tok_should_never_be_asked_zzzzzzzz";
        Manifest m; Session s; ErrorInfo e;
        const auto ok = fetchManifest (url, m, s, e, f.sources(), cfg);
        check (ok && m.items.size() == 1, "saved token: manifest read through the API: " + e.message);
        check (s.authenticated() && s.tokenSource.containsIgnoreCase ("saved") && *f.ghAsked == 0, "session uses the saved token; gh auth token not asked");
        const auto* asset = m.items.isEmpty() ? nullptr : m.items.getReference (0).assetFor (currentPlatform());
        check (asset != nullptr && asset->apiUrl.contains ("/releases/assets/2"), "zip asset gets its API download URL");

        bool sawApiAuth = false, sawOctet = false;
        for (auto& r : gh.api->requests())
            if (r.path.startsWith ("/repos/Dooblr/dooblr-plugins/releases/assets/1"))
            {
                sawApiAuth = sawApiAuth || r.headers["Authorization"] == "Bearer " + goodToken;
                sawOctet = sawOctet || r.headers["Accept"] == "application/octet-stream";
            }
        check (sawApiAuth && sawOctet, "asset request to the API: Bearer token + Accept: application/octet-stream");
        check (gh.objects->requests().size() > objectsBefore, "the API redirect was followed to the objects host");

        // 4. Install with the session: zip downloaded via the API, sha256 checked.
        Locations loc;
        loc.vst3Dir = root.getChildFile ("VST3");
        loc.m4lDir = root.getChildFile ("m4l");
        loc.overridden = true;
        Receipts receipts (root.getChildFile ("receipts.json"));
        auto rep = installItems (m, { "dooblr-test" }, loc, receipts, noLog, noProgress, &s);
        check (rep.failed.isEmpty() && rep.done.size() == 1 && loc.vst3Dir.getChildFile ("dooblr-test.vst3/Contents/Info.plist").existsAsFile(),
               "installs from the private release: " + rep.failed.joinIntoString ("; "));
        check (gh.api->count ("/repos/Dooblr/dooblr-plugins/releases/assets/2") >= 1 && gh.objects->count ("/objects/2") >= 1, "zip came through the API asset URL");

        gh.corruptZip = true;
        loc.vst3Dir.getChildFile ("dooblr-test.vst3").deleteRecursively();
        auto bad = installItems (m, { "dooblr-test" }, loc, receipts, noLog, noProgress, &s);
        check (bad.failed.size() == 1 && bad.failed[0].containsIgnoreCase ("checksum") && ! loc.vst3Dir.getChildFile ("dooblr-test.vst3").exists(),
               "a damaged private download is rejected (sha256): " + bad.failed.joinIntoString ("; "));
        check (bad.errors.size() == 1 && bad.errors[0].step.isNotEmpty() && bad.copyText().containsIgnoreCase ("checksum") && ! bad.copyText().contains (goodToken),
               "install failure has copyable details without the token");
        note ("install failure copy text: " + bad.copyText().replace ("\n", " / "));
        gh.corruptZip = false;

        // The Authorization header never left the API origin.
        bool leaked = false;
        for (auto& r : gh.objects->requests())
            leaked = leaked || r.headers.containsKey ("Authorization") || r.headers.getAllValues().joinIntoString (" ").contains (goodToken);
        check (! leaked && gh.objects->requests().size() >= 3, "no Authorization header sent to the redirected (objects) host ("
                                                                 + juce::String ((int) gh.objects->requests().size()) + " requests)");

        // 5. A public release never needs a token (no API call, no token lookup).
        gh.publicRelease = true;
        FakeTokens p;
        p.env = { { "DOOBLR_GITHUB_TOKEN", goodToken } };
        const auto apiBefore = gh.api->count ("/repos/");
        Manifest pm; Session ps; ErrorInfo pe;
        check (fetchManifest (url, pm, ps, pe, p.sources(), cfg) && ! ps.authenticated() && gh.api->count ("/repos/") == apiBefore
                   && *p.envAsked == 0 && *p.ghAsked == 0,
               "public release: read anonymously, no token looked up");
        gh.publicRelease = false;

        // 6. The window: 404 -> sign-in panel -> Save and retry -> signed in.
        setEnv ("DOOBLR_GITHUB_API", cfg.apiBase);
        setEnv ("DOOBLR_GITHUB_WEB_HOSTS", cfg.webHosts[0]);
        {
            const auto tokenFile = root.getChildFile ("ui-token");
            ui::InstallerComponent c (url, loc, false);
            c.setSize (820, 600);
            c.setTokenSources (TokenSources::none());
            c.setTokenStore (TokenStore (TokenStore::Backend::file, tokenFile));
            juce::String clip;
            c.setClipboardSink ([&clip] (const juce::String& t) { clip = t; });
            c.refetch();
            check (waitFor ([&] { return ! c.getFetchError().isEmpty(); }), "window: fetch finished");
            check (c.isSignInVisible() && c.getFetchError().needsSignIn && c.getFetchError().httpStatus == 404, "window: 404 shows the sign-in panel");
            check (c.isTokenFieldMasked(), "window: token field is masked");
            check (c.isErrorTextSelectable() && c.isCopyVisible() && c.getErrorBoxText().contains ("404"), "window: error is selectable text with Copy details");
            c.clickCopy();
            check (clip.contains ("HTTP status: 404") && clip.contains ("Installer: ") && clip.contains ("/releases/latest/download/dooblr-manifest.json"),
                   "window: Copy details copies the full details");
            const auto checksBefore = gh.api->count ("/repos/Dooblr/dooblr-plugins");
            check (c.saveTokenAndRetry ("nope").failed() && c.isSignInVisible() && gh.api->count ("/repos/Dooblr/dooblr-plugins") == checksBefore,
                   "window: a non-token is refused without asking GitHub");
            check (c.saveTokenAndRetry (badToken).wasOk(), "window: bad token goes to GitHub for a check");
            check (waitFor ([&] { return ! c.isCheckingToken(); }) && c.isSignInVisible() && c.isTokenNoteBad() && ! tokenFile.existsAsFile()
                       && c.getFetchError().httpStatus == 401,
                   "window: refused token is not saved, sign-in stays: " + c.getTokenNote());
            check (c.saveTokenAndRetry (goodToken).wasOk(), "window: good token checked");
            check (waitFor ([&] { return c.getSession().authenticated(); }), "window: signed in after Save and retry");
            check (c.getFetchError().isEmpty() && ! c.isSignInVisible() && c.numRows() == 1 && c.getStatusLine().contains ("signed in"),
                   "window: list shown, panel hidden: " + c.getStatusLine());
            juce::String w;
            check (TokenStore (TokenStore::Backend::file, tokenFile).load (w) == goodToken, "window: token kept in the store");
        }

        // 7. Checking a token before it is saved (GET /repos/Dooblr/dooblr-plugins, then its releases).
        {
            ErrorInfo ve;
            check (validateToken (goodToken, "Dooblr", "dooblr-plugins", ve, cfg) && ve.isEmpty(), "validate: a good token is accepted");
            check (! validateToken (badToken, "Dooblr", "dooblr-plugins", ve, cfg) && ve.httpStatus == 401 && ve.needsSignIn
                       && ve.message.contains ("did not accept"),
                   "validate: bad token -> 401, plain words: " + ve.message);
            check (! validateToken (noRepoToken, "Dooblr", "dooblr-plugins", ve, cfg) && ve.httpStatus == 404
                       && ve.message.contains ("Only select repositories > dooblr-plugins"),
                   "validate: repository not selected -> 404 says what to pick: " + ve.message);
            check (! validateToken (noContentsToken, "Dooblr", "dooblr-plugins", ve, cfg) && ve.httpStatus == 403
                       && ve.message.contains ("Contents: Read-only"),
                   "validate: no Contents permission -> says to grant Contents: Read-only: " + ve.message);
            const auto ct = ve.copyText() + ve.shortText() + ve.url;
            check (! ct.contains (noContentsToken) && ! ct.contains ("TESTONLY"), "validate: error text never contains the token");
            int deadPort = 0;
            for (int i = 0; i < 20 && deadPort == 0; ++i)
            {
                juce::StreamingSocket probe;
                const int probePort = 20000 + juce::Random::getSystemRandom().nextInt (30000);
                if (probe.createListener (probePort, "127.0.0.1")) { deadPort = probePort; probe.close(); }
            }
            GitHubConfig offline;
            offline.apiBase = "http://127.0.0.1:" + juce::String (deadPort);
            check (! validateToken (goodToken, "Dooblr", "dooblr-plugins", ve, offline) && ve.httpStatus == 0 && ve.message.contains ("No connection"),
                   "validate: offline -> a connection error, not a token error: " + ve.message);
        }

        // 8. "Get access on GitHub" -> browser opens the pre-filled page -> clipboard watched -> "Use the copied token".
        {
            const auto tokenFile = root.getChildFile ("ui-token-clip");
            ui::InstallerComponent c (url, loc, false);
            c.setSize (820, 600);
            c.setTokenSources (TokenSources::none());
            c.setTokenStore (TokenStore (TokenStore::Backend::file, tokenFile));
            juce::Array<juce::URL> opened;
            c.setBrowserOpener ([&opened] (const juce::URL& u) { opened.add (u); return true; });
            juce::String board = "some text copied earlier";
            int reads = 0;
            c.setClipboardSource ([&] { ++reads; return board; });
            c.setClipboardSink ([&board] (const juce::String& t) { board = t; });
            c.refetch();
            check (waitFor ([&] { return ! c.getFetchError().isEmpty(); }) && c.isSignInVisible(), "clipboard: sign-in panel shown");
            check (! c.isWatchingClipboard() && reads == 0 && ! c.isUseCopiedVisible(), "clipboard: not read before Get access is clicked");

            c.clickGetAccess();
            check (opened.size() == 1 && opened[0].toString (true) == tokenCreationUrl().toString (true), "clipboard: Get access opens the pre-filled page");
            check (c.isWatchingClipboard() && reads == 1 && ! c.isUseCopiedVisible(), "clipboard: watched after the click; other text ignored");
            check (waitFor ([&] { return reads >= 2; }), "clipboard: read again by the 1 s timer");

            board = badToken;
            c.pollClipboard();
            check (c.isUseCopiedVisible() && ! tokenFile.existsAsFile(), "clipboard: a copied token is offered (not saved yet)");
            c.clickUseCopied();
            check (waitFor ([&] { return ! c.isCheckingToken(); }) && c.isTokenNoteBad() && ! tokenFile.existsAsFile() && c.isSignInVisible()
                       && c.getFetchError().httpStatus == 401 && c.isCopyVisible() && c.getErrorBoxText().contains ("Checking the GitHub token"),
                   "clipboard: refused token -> not saved, red note, copyable error: " + c.getTokenNote());
            c.pollClipboard();
            check (! c.isUseCopiedVisible(), "clipboard: a refused token is not offered again");
            juce::String copied;
            c.setClipboardSink ([&copied] (const juce::String& t) { copied = t; });
            c.clickCopy();
            check (copied.contains ("Checking the GitHub token") && copied.contains ("401") && ! copied.contains (badToken) && ! copied.contains ("TESTONLY"),
                   "clipboard: Copy details has the check failure, no token");
            c.setClipboardSink ([&board] (const juce::String& t) { board = t; });

            board = noRepoToken;
            c.pollClipboard();
            c.clickUseCopied();
            check (waitFor ([&] { return ! c.isCheckingToken(); }) && c.getFetchError().httpStatus == 404 && ! tokenFile.existsAsFile()
                       && c.getErrorBoxText().contains ("Only select repositories"),
                   "clipboard: token without the repository -> says to select dooblr-plugins");

            board = goodToken;
            c.pollClipboard();
            check (c.isUseCopiedVisible(), "clipboard: the new token is offered");
            snapshot (c, "signin-offer");
            c.clickUseCopied();
            check (waitFor ([&] { return c.getSession().authenticated(); }) && c.numRows() == 1 && ! c.isSignInVisible(),
                   "clipboard: accepted token -> signed in, list shown");
            juce::String w;
            check (TokenStore (TokenStore::Backend::file, tokenFile).load (w) == goodToken, "clipboard: accepted token saved");
            check (board.isEmpty(), "clipboard: the token is cleared from the clipboard once saved");
            check (! c.isWatchingClipboard(), "clipboard: watching stops once signed in");
            const auto seen = c.getTokenNote() + c.getStatusLine() + c.getErrorBoxText() + c.getCopyText();
            for (auto& t : { goodToken, badToken, noRepoToken })
                check (! seen.contains (t), "clipboard: no token in notes, status, error text or copy text");
        }

        // The token never reaches the window's text even when an error message would carry it.
        {
            ErrorInfo leak;
            leak.step = "Checking the GitHub token";
            leak.message = "GitHub said: bad token " + goodToken;
            leak.setUrl (cfg.apiBase + "/repos/Dooblr/dooblr-plugins?access_token=" + goodToken);
            registerSecret (goodToken);
            check (! leak.copyText().contains (goodToken) && ! redactSecrets (leak.message).contains (goodToken), "a token inside an error is redacted");
        }

        setEnv ("DOOBLR_GITHUB_API", {});
        setEnv ("DOOBLR_GITHUB_WEB_HOSTS", {});
    }

    //==========================================================================
    void snapshot (juce::Component& c, const juce::String& name)
    {
        const auto snap = juce::SystemStats::getEnvironmentVariable ("DOOBLRINSTALLER_SNAPSHOT", {});
        if (snap.isEmpty()) return;
        const auto f = juce::File (snap + "-" + name + ".png");
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), out);
        note ("snapshot " + f.getFullPathName());
    }

    /** Install button -> worker -> wheel -> summary, for real (file:// zips into temp folders). */
    void testUiInstall()
    {
        std::cout << "window install (temp folders)" << std::endl;
        const auto root = tempRoot().getChildFile ("uiflow");
        const auto release = root.getChildFile ("release");
        const auto vst3 = root.getChildFile ("VST3");
        release.createDirectory();
        setEnv ("DOOBLR_INSTALLER_RECEIPTS", root.getChildFile ("receipts.json").getFullPathName());

        makeBundle (vst3, "dooblr-old", "0.1.0");
        makeBundle (vst3, "dooblr-same", "1.0.0");
        const auto zNew  = zipFolder (makeBundle (root.getChildFile ("s1"), "dooblr-new", "0.2.0"), release.getChildFile ("new.zip"));
        const auto zOld  = zipFolder (makeBundle (root.getChildFile ("s2"), "dooblr-old", "0.2.0"), release.getChildFile ("old.zip"));
        const auto zSame = zipFolder (makeBundle (root.getChildFile ("s3"), "dooblr-same", "1.0.0"), release.getChildFile ("same.zip"));
        const auto zBad  = zipFolder (makeBundle (root.getChildFile ("s4"), "dooblr-bad", "0.1.0"), release.getChildFile ("bad.zip"));
        const auto mf = writeManifest (release, { itemVar ("dooblr-new", "0.2.0", "dooblr-new.vst3", "vst3", assetVar (zNew)),
                                                  itemVar ("dooblr-old", "0.2.0", "dooblr-old.vst3", "vst3", assetVar (zOld)),
                                                  itemVar ("dooblr-same", "1.0.0", "dooblr-same.vst3", "vst3", assetVar (zSame)),
                                                  itemVar ("dooblr-bad", "0.1.0", "dooblr-bad.vst3", "vst3",
                                                           assetVar (zBad, juce::String::repeatedString ("0", 64))) });
        Manifest m;
        if (! loadManifest (mf, m).wasOk()) { check (false, "ui install manifest loads"); return; }
        Locations loc;
        loc.vst3Dir = vst3;
        loc.m4lDir = root.getChildFile ("User Library/dooblr");
        loc.overridden = true;

        ui::InstallerComponent c (juce::URL (mf), loc, false);
        c.setSize (820, 600);
        c.setManifest (m);
        c.showPage (ui::InstallerComponent::Page::choose);
        c.clickSelectAll();
        check (c.getSelectedIds().size() == 4 && c.getInstallButtonText() == "Install and update 4", "All: 4 (new, update, reinstall, bad)");
        c.clickInstall();
        check (c.getPage() == ui::InstallerComponent::Page::progress && c.isRingVisible() && ! c.isLogVisible(), "Install: the wheel");
        check (waitFor ([&] { return c.getPage() == ui::InstallerComponent::Page::done; }), "install finished");
        note ("summary: " + c.getSummaryText());
        check (c.getSummaryText() == juce::String::fromUTF8 ("1 installed  \xc2\xb7  1 updated  \xc2\xb7  1 reinstalled  \xc2\xb7  1 failed"),
               "real install summary: " + c.getSummaryText());
        check (readBundleVersion (vst3.getChildFile ("dooblr-new.vst3")) == "0.2.0" && readBundleVersion (vst3.getChildFile ("dooblr-old.vst3")) == "0.2.0"
               && ! vst3.getChildFile ("dooblr-bad.vst3").exists(), "files: new and updated in place, the damaged one not installed");
        check (std::abs (c.getProgressFraction() - 1.0) < 1.0e-9 && c.isDetailsSelectable() && c.getDetailsText().contains ("dooblr-bad"),
               "wheel full, the failure selectable");
        juce::String clip;
        c.setClipboardSink ([&clip] (const juce::String& t) { clip = t; });
        c.clickCopy();
        check (clip.contains ("Log:") && clip.contains ("Downloading dooblr-new") && clip.contains ("Result: "), "Copy details: result and step log");
    }

    void testErrorWindows()
    {
        std::cout << "sign-in panel and copyable errors (window)" << std::endl;
        Locations loc;
        loc.vst3Dir = tempRoot().getChildFile ("errwin/VST3");
        loc.m4lDir = tempRoot().getChildFile ("errwin/m4l");
        loc.overridden = true;
        const juce::URL real ("https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json");

        // The real release URL, anonymously (opt-in network test). While the repo is private this is a 404.
        ErrorInfo signIn;
        if (juce::SystemStats::getEnvironmentVariable ("DOOBLRINSTALLER_NETWORK_TESTS", {}) == "1")
        {
            Manifest m; Session s;
            const auto ok = fetchManifest (real, m, s, signIn, TokenSources::none(), GitHubConfig());
            note ("real anonymous fetch: " + (ok ? juce::String ("OK (release is public)") : signIn.shortText().replace ("\n", " / ")));
            check (ok || (signIn.needsSignIn && (signIn.httpStatus == 404 || signIn.httpStatus == 401 || signIn.httpStatus == 403)),
                   "real release URL: works, or the private 404 asks for sign-in");
        }
        if (signIn.isEmpty() || ! signIn.needsSignIn)
        {
            Manifest m; Session s;
            // Same wording as a real private 404, produced by a local server.
            FakeGitHub gh;
            if (gh.start())
            {
                fetchManifest (gh.manifestUrl(), m, s, signIn, TokenSources::none(), gh.config());
                signIn.setUrl (real.toString (true));
            }
        }

        ui::InstallerComponent c (real, loc, false);
        c.setSize (820, 600);
        juce::String clip;
        c.setClipboardSink ([&clip] (const juce::String& t) { clip = t; });
        c.setFetchError (signIn);
        check (c.isSignInVisible() && c.isTokenFieldMasked() && c.isErrorTextSelectable() && c.isCopyVisible(), "sign-in panel, masked field, selectable error, Copy");
        snapshot (c, "signin");

        // A connection error (nothing listens on this port): no sign-in, but selectable + Copy.
        int deadPort = 0;
        for (int i = 0; i < 20 && deadPort == 0; ++i)
        {
            juce::StreamingSocket probe;
            const int p = 20000 + juce::Random::getSystemRandom().nextInt (30000);
            if (probe.createListener (p, "127.0.0.1")) { deadPort = p; probe.close(); }
        }
        const juce::URL dead ("http://127.0.0.1:" + juce::String (deadPort) + "/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json?token=SHOULDNOTSHOW");
        ErrorInfo ce;
        const auto resp = httpGet (dead, {}, {}, ce, 2, 3000);
        check (! resp.ok() && ce.message.isNotEmpty() && ! ce.needsSignIn, "connection refused gives an error: " + ce.message);
        ce.step = "Getting the plug-in list (the manifest)";
        ui::InstallerComponent d (dead, loc, false);
        d.setSize (820, 600);
        d.setClipboardSink ([&clip] (const juce::String& t) { clip = t; });
        d.setFetchError (ce);
        check (! d.isSignInVisible() && d.isCopyVisible() && d.isErrorTextSelectable(), "connection error: no sign-in panel, Copy shown");
        d.clickCopy();
        note ("copied:\n" + clip);
        check (clip.contains ("Installer: ") && clip.contains ("Step: ") && clip.contains ("127.0.0.1") && ! clip.contains ("SHOULDNOTSHOW"),
               "copied connection error has the details, redacted");
        snapshot (d, "error-copy");

        // Done page: failures are selectable and copyable.
        InstallReport rep;
        rep.done.add ("dooblr-kick 0.3.6");
        rep.failed.add ("dooblr-vocals: checksum mismatch");
        ErrorInfo ie;
        ie.step = "Downloading dooblr-vocals 0.2.0";
        ie.message = "checksum mismatch (sha256 3f1c..., expected 0000...)";
        ie.setUrl ("https://api.github.com/repos/Dooblr/dooblr-plugins/releases/assets/7");
        rep.errors.add (ie);
        c.showReport (rep);
        c.clickCopy();
        check (clip.contains ("Downloading dooblr-vocals") && clip.contains ("checksum mismatch") && clip.contains ("Log:") && c.isCopyVisible(),
               "done page: Copy details has the failures and the log");
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
        add ("dooblr-titan", "0.1.0", "MIDI clip tightener and variation writer (Max for Live MIDI effect)", "m4l", "dooblr-titan");
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
        check (c.numRows() == 9, "one row per item");
        check (c.rowStatus ("dooblr-q") == "Up to date", "q 0.9.5 installed: Up to date");
        check (c.rowStatus ("dooblr-kick") == "Update", "kick 0.3.4 -> 0.3.6: Update");
        check (c.rowStatus ("dooblr-vocals") == "Install", "vocals missing: Install");
        check (c.rowStatus ("dooblr-titan") == "Install", "titan missing: Install");
        check (c.rowStatus ("dooblr-chords") == "Update", "chords folder with unknown version: Update");
        check (c.rowStatus ("dooblr-prophet-panel") == "Up to date", "prophet panel version read from UI_VERSION in its script: Up to date");
        check (readM4LVersion (m4l.getChildFile ("dooblr-prophet")) == "0.3.2", "readM4LVersion finds UI_VERSION");
        const auto sel = c.getSelectedIds();
        check (sel.contains ("dooblr-kick") && sel.contains ("dooblr-soothe") && ! sel.contains ("dooblr-q") && ! sel.contains ("dooblr-vocals"),
               "default selection = the updates: " + sel.joinIntoString (","));
        note ("status line: " + c.getStatusLine());
        check (c.getStatusLine().contains ("9 items") && c.getStatusLine().contains ("3 updates"), "status line counts");

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
        c.showPage (ui::InstallerComponent::Page::choose);
        check (c.getInstallButtonText() == "Update 3" && c.isInstallEnabled() && c.getSelectionSummary() == "3 of 9 selected",
               "default: the 3 updates, button \"" + c.getInstallButtonText() + "\", " + c.getSelectionSummary());

        // All / None (0.1.3 bug: All ticked only Install/Update rows and unticked the rest, so on a computer where
        // everything is up to date it did nothing; and reading the list again threw the choice away).
        c.clickSelectNone();
        check (c.getSelectedIds().isEmpty() && ! c.isInstallEnabled() && c.getInstallButtonText() == "Install"
               && c.getSelectionSummary() == "0 of 9 selected", "None unticks everything and disables Install");
        c.clickSelectAll();
        check (c.getSelectedIds().size() == 9, "All ticks every item (new, updates, up to date): " + c.getSelectedIds().joinIntoString (","));
        check (c.isInstallEnabled() && c.getInstallButtonText() == "Install and update 9" && c.getSelectionSummary() == "9 of 9 selected",
               "All: button \"" + c.getInstallButtonText() + "\", " + c.getSelectionSummary());
        c.clickRow ("dooblr-q");
        check (c.getSelectedIds().size() == 8 && ! c.getSelectedIds().contains ("dooblr-q") && c.getSelectionSummary() == "8 of 9 selected",
               "a row tick still works after All");
        c.clickRow ("dooblr-q");
        check (c.getSelectedIds().contains ("dooblr-q") && c.getSelectedIds().size() == 9, "and ticks again");
        c.clickSelectNone();
        check (c.getSelectedIds().isEmpty() && ! c.isInstallEnabled(), "None after All clears every row");
        c.clickRow ("dooblr-vocals");
        check (c.getSelectedIds() == juce::StringArray { "dooblr-vocals" } && c.getInstallButtonText() == "Install 1", "one tick after None: Install 1");
        c.clickSelectUpdates();
        const auto ups = c.getSelectedIds();
        check (ups.size() == 3 && ups.contains ("dooblr-kick") && ups.contains ("dooblr-soothe") && ups.contains ("dooblr-chords")
               && c.getInstallButtonText() == "Update 3", "Updates ticks only the 3 updates: " + ups.joinIntoString (","));

        // The choice survives reading the list again (after sign-in or Check again).
        c.clickRow ("dooblr-vocals");
        c.setManifest (m);
        check (c.getSelectedIds().size() == 4 && c.getSelectedIds().contains ("dooblr-vocals") && c.getInstallButtonText() == "Install and update 4",
               "the selection survives a refresh: " + c.getSelectedIds().joinIntoString (","));
        c.clickSelectNone();
        c.setManifest (m);
        check (c.getSelectedIds().isEmpty() && ! c.isInstallEnabled(), "None survives a refresh");
        c.clickSelectAll();
        c.setManifest (m);
        check (c.getSelectedIds().size() == 9, "All survives a refresh");

        // A computer where everything is up to date (where All/None did nothing in 0.1.3).
        {
            const auto root2 = root.getChildFile ("uptodate");
            const auto v2 = root2.getChildFile ("VST3");
            juce::Array<juce::var> items2;
            for (auto [id, v] : { std::pair<const char*, const char*> { "dooblr-q", "0.9.5" }, { "dooblr-kick", "0.3.6" }, { "dooblr-soothe", "0.1.3" } })
            {
                makeBundle (v2, id, v);
                auto* a = new juce::DynamicObject();
                a->setProperty ("url", juce::String (id) + ".zip");
                a->setProperty ("sha256", sha);
                items2.add (itemVar (id, v, juce::String (id) + ".vst3", "vst3", juce::var (a)));
            }
            Manifest m2;
            check (loadManifest (writeManifest (root2, items2), m2).wasOk(), "up-to-date manifest loads");
            Locations loc2 = loc;
            loc2.vst3Dir = v2;
            ui::InstallerComponent u (juce::URL (mf), loc2, false);
            u.setSize (820, 600);
            u.setManifest (m2);
            u.showPage (ui::InstallerComponent::Page::choose);
            check (u.getSelectedIds().isEmpty() && ! u.isInstallEnabled() && u.rowStatus ("dooblr-kick") == "Up to date",
                   "all up to date: nothing ticked by default");
            u.clickSelectAll();
            check (u.getSelectedIds().size() == 3 && u.isInstallEnabled() && u.getInstallButtonText() == "Install 3",
                   "all up to date: All ticks all 3 (reinstall), button \"" + u.getInstallButtonText() + "\"");
            u.clickSelectNone();
            check (u.getSelectedIds().isEmpty() && ! u.isInstallEnabled(), "all up to date: None unticks them");
        }

        c.clickSelectNone();
        c.clickSelectUpdates();
        c.clickRow ("dooblr-vocals");
        c.clickRow ("dooblr-prophet-panel");
        check (c.getSelectedIds().size() == 5, "select two more");
        save ("choose");
        save ("chooser");

        // Progress: one wheel, the current item and step, no list of lines.
        c.beginProgress (c.getSelectedIds());
        check (c.getPage() == ui::InstallerComponent::Page::progress && c.isRingVisible() && ! c.isLogVisible(), "progress page: the wheel, no log list");
        c.setProgress (-1, "Downloading dooblr-kick 0.3.6...");
        c.setItemProgress (0, 5, 1.0);
        c.setProgress (-1, "Checked dooblr-kick (sha256 OK)");
        c.setProgress (-1, "Downloading dooblr-vocals 0.2.0...");
        c.setItemProgress (1, 5, 0.4);
        note ("progress: " + c.getProgressItem() + " / " + c.getProgressStep() + " / " + juce::String (c.getProgressFraction()));
        check (c.getProgressItem() == "dooblr-vocals 0.2.0" && c.getProgressStep() == "Downloading and checking"
               && std::abs (c.getProgressFraction() - 0.28) < 1.0e-6, "the wheel shows the overall progress and the current item");
        save ("progress");
        c.setProgress (-1, "Installing...");
        check (c.getProgressItem() == "Putting the plug-ins in place", "last step shown");

        InstallReport rep;
        rep.done.add ("dooblr-kick 0.3.6 -> /Library/Audio/Plug-Ins/VST3/dooblr-kick.vst3");
        rep.done.add ("dooblr-soothe 0.1.3 -> /Library/Audio/Plug-Ins/VST3/dooblr-soothe.vst3");
        rep.done.add ("dooblr-prophet-panel 0.3.2 -> " + m4l.getChildFile ("dooblr-prophet").getFullPathName());
        rep.failed.add ("dooblr-vocals: dooblr-vocals-0.2.0-mac.zip: checksum mismatch (sha256 3f1c..., expected 0000...). The download is damaged or was changed; nothing was installed.");
        c.showReport (rep);
        check (c.getPage() == ui::InstallerComponent::Page::done, "report shows Done");
        note ("summary: " + c.getSummaryText());
        check (c.getSummaryText().contains ("2 updated") && c.getSummaryText().contains ("1 reinstalled") && c.getSummaryText().contains ("1 failed")
               && ! c.getSummaryText().contains ("installed ·"), "done summary counts updated / reinstalled / failed");
        check (c.isDetailsSelectable() && c.getDetailsText().contains ("checksum mismatch") && c.isCopyVisible(), "failures: selectable text and Copy");
        save ("done");

        InstallReport ok;
        ok.done.add ("dooblr-vocals 0.2.0 -> /Library/Audio/Plug-Ins/VST3/dooblr-vocals.vst3");
        c.beginProgress ({ "dooblr-vocals" });
        c.showReport (ok);
        check (c.getSummaryText() == "1 installed" && ! c.isDetailsSelectable(), "all good: \"1 installed\", no details box");
        save ("done-ok");

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
    testTokensAndRedaction();
    testPrivateRelease();
    testUiInstall();
    testErrorWindows();

    tempRoot().deleteRecursively();
    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
