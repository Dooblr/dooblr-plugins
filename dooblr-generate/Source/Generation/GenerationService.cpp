#include "GenerationService.h"

#include "BinaryData.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace dooblrgenerate::gen
{
    /**
        The worker child process: stdout and stderr on one pipe that is read as data arrives
        (juce::ChildProcess reads with fread, which waits for a full buffer, so progress would
        only arrive at the end). The child gets its own process group, so cancelling kills it and
        anything it started; no file descriptors of the host leak into it.
    */
    class WorkerProcess
    {
    public:
        ~WorkerProcess()
        {
            if (pid > 0)
            {
                kill();
                int status = 0;
                ::waitpid (pid, &status, 0);
            }
            if (fd >= 0)
                ::close (fd);
        }

        bool start (const juce::StringArray& args, juce::String& error)
        {
            int fds[2];
            if (::pipe (fds) != 0)
            {
                error = "pipe() failed: " + juce::String (std::strerror (errno));
                return false;
            }

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init (&actions);
            posix_spawn_file_actions_addopen (&actions, 0, "/dev/null", O_RDONLY, 0);
            posix_spawn_file_actions_adddup2 (&actions, fds[1], 1);
            posix_spawn_file_actions_adddup2 (&actions, fds[1], 2);

            posix_spawnattr_t attr;
            posix_spawnattr_init (&attr);
            short flags = POSIX_SPAWN_SETPGROUP;
           #ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
            flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
           #else
            posix_spawn_file_actions_addclose (&actions, fds[0]);
           #endif
            posix_spawnattr_setflags (&attr, flags);
            posix_spawnattr_setpgroup (&attr, 0);

            std::vector<std::string> argStore, envStore;
            for (const auto& a : args)
                argStore.push_back (a.toStdString());
            for (char** e = environ; e != nullptr && *e != nullptr; ++e)
                if (std::strncmp (*e, "PYTHONUNBUFFERED=", 17) != 0)
                    envStore.emplace_back (*e);
            envStore.emplace_back ("PYTHONUNBUFFERED=1");

            std::vector<char*> argv, envp;
            for (auto& a : argStore) argv.push_back (a.data());
            argv.push_back (nullptr);
            for (auto& e : envStore) envp.push_back (e.data());
            envp.push_back (nullptr);

            const auto result = ::posix_spawn (&pid, argv[0], &actions, &attr, argv.data(), envp.data());
            posix_spawn_file_actions_destroy (&actions);
            posix_spawnattr_destroy (&attr);
            ::close (fds[1]);

            if (result != 0)
            {
                ::close (fds[0]);
                pid = -1;
                error = "Could not start " + args[0] + ": " + juce::String (std::strerror (result));
                return false;
            }

            fd = fds[0];
            return true;
        }

        /** Bytes read (> 0), or 0 at the end of output. */
        int read (char* buffer, int size)
        {
            for (;;)
            {
                const auto n = ::read (fd, buffer, (size_t) size);
                if (n < 0 && errno == EINTR)
                    continue;
                return n < 0 ? 0 : (int) n;
            }
        }

        void kill()
        {
            if (pid > 0)
            {
                ::kill (-pid, SIGKILL);
                ::kill (pid, SIGKILL);
            }
        }

        /** Waits for the exit; returns a description such as "exit code 6" or "killed by signal 6 (Abort trap: 6)". */
        juce::String wait (int& exitCode)
        {
            int status = 0;
            pid_t r;
            do { r = ::waitpid (pid, &status, 0); } while (r < 0 && errno == EINTR);
            pid = -1;
            exitCode = -1;

            if (r > 0 && WIFEXITED (status))
            {
                exitCode = WEXITSTATUS (status);
                return "exit code " + juce::String (exitCode);
            }
            if (r > 0 && WIFSIGNALED (status))
                return "killed by signal " + juce::String (WTERMSIG (status)) + " (" + juce::String (::strsignal (WTERMSIG (status))) + ")";
            return "unknown exit status";
        }

    private:
        pid_t pid = -1;
        int fd = -1;
    };

    juce::String toString (ModelState s)
    {
        switch (s)
        {
            case ModelState::notInstalled:   return "not installed";
            case ModelState::downloading:    return "downloading";
            case ModelState::ready:          return "ready";
            case ModelState::needsAccess:    return "needs access";
            case ModelState::runtimeMissing: return "runtime missing";
            case ModelState::error:          return "error";
        }
        return {};
    }

    GenerationService::GenerationService (Paths p, History& h, GeneratedCallback callback)
        : juce::Thread ("dooblr-generate worker"), paths (std::move (p)), history (h), onGenerated (std::move (callback))
    {
        startThread (juce::Thread::Priority::low);
    }

    GenerationService::~GenerationService()
    {
        signalThreadShouldExit();
        cancel();
        wake.signal();
        stopThread (10000);
    }

    juce::String GenerationService::makeReport (const juce::String& step, const juce::String& model, const juce::String& message)
    {
        juce::String r;
        r << "dooblr-generate " << JucePlugin_VersionString << "\n"
          << "OS: " << juce::SystemStats::getOperatingSystemName() << " (" << juce::SystemStats::getCpuModel() << ")\n"
          << "Step: " << step << "\n";
        if (model.isNotEmpty())
            r << "Model: " << model << "\n";
        r << "Time: " << juce::Time::getCurrentTime().toISO8601 (true) << "\n"
          << "Message: " << message.trim();
        return r;
    }

    //==============================================================================
    juce::var GenerationService::readSettings() const
    {
        auto v = juce::JSON::parse (paths.settings());
        return v.isObject() ? v : juce::var (new juce::DynamicObject());
    }

    void GenerationService::writeSettings (const juce::var& v) const
    {
        paths.root.createDirectory();
        paths.settings().replaceWithText (juce::JSON::toString (v), false, false, "\n");
    }

    juce::File GenerationService::modelFolder (const juce::String& modelId) const
    {
        const auto chosen = readSettings()["modelFolders"][juce::Identifier (modelId)].toString();
        if (chosen.isNotEmpty())
            return juce::File (chosen);
        return paths.models().getChildFile (modelId);
    }

    void GenerationService::setModelFolder (const juce::String& modelId, const juce::File& folder)
    {
        auto settings = readSettings();
        auto folders = settings["modelFolders"];
        if (! folders.isObject())
            folders = juce::var (new juce::DynamicObject());

        if (folder == juce::File())
            folders.getDynamicObject()->removeProperty (juce::Identifier (modelId));
        else
            folders.getDynamicObject()->setProperty (juce::Identifier (modelId), folder.getFullPathName());

        settings.getDynamicObject()->setProperty ("modelFolders", folders);
        writeSettings (settings);

        const juce::ScopedLock sl (lock);
        lastModelErrors.erase (modelId);
    }

    bool GenerationService::isSystemPython (const juce::File& python)
    {
        const auto path = python.getFullPathName();
        const auto real = python.getLinkedTarget().getFullPathName();
        for (const auto& p : { path, real })
            if (p.startsWith ("/usr/bin/") || p.startsWith ("/System/") || p.startsWith ("/Applications/Xcode")
                || p.startsWith ("/Library/Developer/CommandLineTools"))
                return true;
        return false;
    }

    void GenerationService::setPython (const juce::File& python)
    {
        auto settings = readSettings();
        if (python == juce::File())
            settings.getDynamicObject()->removeProperty ("python");
        else
            settings.getDynamicObject()->setProperty ("python", python.getFullPathName());
        writeSettings (settings);
    }

    bool GenerationService::saveToken (const juce::String& token)
    {
        const auto t = token.trim();
        if (t.isEmpty() || t.containsAnyOf (" \n\r\t"))
            return false;

        paths.root.createDirectory();
        const auto file = paths.tokenFile();
        file.deleteFile();
        file.create();
        ::chmod (file.getFullPathName().toRawUTF8(), 0600);
        const bool ok = file.replaceWithText (t, false, false, "\n");
        ::chmod (file.getFullPathName().toRawUTF8(), 0600);

        const juce::ScopedLock sl (lock);
        lastModelErrors.clear();
        return ok;
    }

    bool GenerationService::hasToken() const { return paths.tokenFile().existsAsFile() && paths.tokenFile().getSize() > 0; }
    void GenerationService::clearToken() { paths.tokenFile().deleteFile(); }

    juce::File GenerationService::findPython() const
    {
        // An interpreter chosen in settings.json is used exclusively (even if it is missing).
        const auto chosen = readSettings()["python"].toString();
        if (chosen.isNotEmpty())
            return juce::File::isAbsolutePath (chosen) && juce::File (chosen).existsAsFile() && ! isSystemPython (juce::File (chosen))
                     ? juce::File (chosen) : juce::File();

        juce::StringArray candidates;
        candidates.add (juce::SystemStats::getEnvironmentVariable ("DOOBLR_GENERATE_PYTHON", {}));
        candidates.add (paths.runtime().getChildFile ("venv/bin/python3").getFullPathName());
        candidates.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                            .getChildFile ("dooblr-build/generate-venv/bin/python3").getFullPathName());

        for (const auto& c : candidates)
            if (c.isNotEmpty() && juce::File::isAbsolutePath (c) && juce::File (c).existsAsFile() && ! isSystemPython (juce::File (c)))
                return juce::File (c);

        return {};
    }

    juce::File GenerationService::installWorker() const
    {
        const auto file = paths.worker();
        const juce::String script (BinaryData::dooblr_generate_worker_py, (size_t) BinaryData::dooblr_generate_worker_pySize);

        if (! file.existsAsFile() || file.loadFileAsString() != script)
        {
            paths.runtime().createDirectory();
            file.replaceWithText (script, false, false, "\n");
        }

        return file;
    }

    //==============================================================================
    ModelStatus GenerationService::modelStatus (const juce::String& modelId) const
    {
        ModelStatus s;
        const auto* model = ModelCatalog::find (modelId);

        if (model == nullptr)
        {
            s.state = ModelState::error;
            s.text = "Unknown model";
            s.details = makeReport ("model status", modelId, "unknown model id");
            return s;
        }

        {
            const juce::ScopedLock sl (lock);

            if (status.result == JobStatus::Result::running && status.kind == JobStatus::Kind::download && status.model == modelId)
            {
                s.state = ModelState::downloading;
                s.fraction = status.fraction;
                s.text = "Downloading " + juce::String (juce::roundToInt (status.fraction * 100.0f)) + "%";
                s.details = status.message;
                return s;
            }

            const auto it = lastModelErrors.find (modelId);
            if (it != lastModelErrors.end())
                return it->second;
        }

        if (! findPython().existsAsFile())
        {
            s.state = ModelState::runtimeMissing;
            s.text = "Python runtime not set up";
            s.details = makeReport ("runtime", modelId,
                "No Python runtime found. Run dooblr-generate/scripts/setup-runtime.sh (creates "
                + paths.runtime().getChildFile ("venv").getFullPathName() + "), or set DOOBLR_GENERATE_PYTHON.");
            return s;
        }

        if (model->isTest)
        {
            s.state = ModelState::ready;
            s.text = "Ready (test backend, not AI)";
            return s;
        }

        const auto folder = modelFolder (modelId);
        const auto missing = ModelCatalog::missingFiles (*model, folder);

        if (missing.isEmpty())
        {
            s.state = ModelState::ready;
            s.text = "Ready";
            s.details = folder.getFullPathName();
            return s;
        }

        if (model->gated && ! hasToken())
        {
            s.state = ModelState::needsAccess;
            s.text = "Needs Hugging Face access";
            s.details = makeReport ("model status", modelId,
                model->displayName + " is gated on Hugging Face. 1) Sign in at huggingface.co and accept the licence at "
                "https://huggingface.co/" + model->repo + "  2) Create a read-only token at https://huggingface.co/settings/tokens "
                "and paste it with Token...  3) Press Download. Or download " + model->requiredFiles.joinIntoString (", ")
                + " yourself and pick the folder with Folder...");
            return s;
        }

        s.state = ModelState::notInstalled;
        s.text = "Not installed";
        s.details = "Missing in " + folder.getFullPathName() + ": " + missing.joinIntoString (", ");
        return s;
    }

    JobStatus GenerationService::jobStatus() const
    {
        const juce::ScopedLock sl (lock);
        return status;
    }

    bool GenerationService::isBusy() const
    {
        const juce::ScopedLock sl (lock);
        return hasPending || status.result == JobStatus::Result::running;
    }

    bool GenerationService::waitUntilIdle (int timeoutMs) const
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (isBusy())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::Thread::sleep (5);
        }
        return true;
    }

    void GenerationService::setJob (std::function<void (JobStatus&)> change)
    {
        const juce::ScopedLock sl (lock);
        change (status);
    }

    bool GenerationService::startGenerate (const GenerateRequest& request)
    {
        if (request.prompt.trim().isEmpty() || ModelCatalog::find (request.model) == nullptr)
            return false;

        const juce::ScopedLock sl (lock);
        if (hasPending || status.result == JobStatus::Result::running)
            return false;

        pending = Job { JobStatus::Kind::generate, request, request.model };
        hasPending = true;
        cancelRequested = false;
        status = JobStatus { JobStatus::Kind::generate, JobStatus::Result::running, request.model, "starting", {}, 0.0f, {},
                             juce::Time::currentTimeMillis(), 0, status.serial + 1 };
        wake.signal();
        return true;
    }

    bool GenerationService::startDownload (const juce::String& modelId)
    {
        const auto* model = ModelCatalog::find (modelId);
        if (model == nullptr || model->isTest)
            return false;

        const juce::ScopedLock sl (lock);
        if (hasPending || status.result == JobStatus::Result::running)
            return false;

        pending = Job { JobStatus::Kind::download, {}, modelId };
        hasPending = true;
        cancelRequested = false;
        lastModelErrors.erase (modelId);
        status = JobStatus { JobStatus::Kind::download, JobStatus::Result::running, modelId, "starting", {}, 0.0f, {},
                             juce::Time::currentTimeMillis(), 0, status.serial + 1 };
        wake.signal();
        return true;
    }

    void GenerationService::cancel()
    {
        const juce::ScopedLock sl (lock);
        cancelRequested = true;
        if (runningProcess != nullptr)
            runningProcess->kill();
    }

    void GenerationService::run()
    {
        while (! threadShouldExit())
        {
            wake.wait (200);
            Job job;
            {
                const juce::ScopedLock sl (lock);
                if (! hasPending)
                    continue;
                job = pending;
                hasPending = false;
            }

            runJob (job);
        }
    }

    void GenerationService::handleEvent (const Job&, const juce::var& event, juce::var& done, juce::var& error)
    {
        const auto type = event["event"].toString();

        if (type == "progress")
            setJob ([&] (JobStatus& s)
            {
                s.stage = event["stage"].toString();
                s.fraction = (float) (double) event["fraction"];
                s.message = event["message"].toString();
            });
        else if (type == "done")
            done = event;
        else if (type == "error")
            error = event;
    }

    void GenerationService::runJob (const Job& job)
    {
        const auto isGenerate = job.kind == JobStatus::Kind::generate;
        const auto step = isGenerate ? juce::String ("generate") : juce::String ("download");

        auto finishFailed = [&] (const juce::String& message, const juce::String& code)
        {
            const auto report = makeReport (step, job.modelId, message);
            setJob ([&] (JobStatus& s)
            {
                s.result = JobStatus::Result::failed;
                s.error = report;
                s.finishedMs = juce::Time::currentTimeMillis();
            });

            if (! isGenerate || code == "access")
            {
                ModelStatus m;
                m.state = code == "access" ? ModelState::needsAccess : code == "runtime" ? ModelState::runtimeMissing
                        : code == "missing" ? ModelState::notInstalled : ModelState::error;
                m.text = code == "access" ? "Needs Hugging Face access" : code == "runtime" ? "Python runtime incomplete"
                       : code == "missing" ? "Not installed" : "Download failed";
                m.details = report;
                const juce::ScopedLock sl (lock);
                lastModelErrors[job.modelId] = m;
            }
        };

        const auto python = findPython();
        if (! python.existsAsFile())
            return finishFailed ("Python runtime not installed (the system Python is never used). Run dooblr-generate/scripts/setup-runtime.sh or set DOOBLR_GENERATE_PYTHON.", "runtime");

        if (const auto* model = ModelCatalog::find (job.modelId); isGenerate && model != nullptr && ! model->isTest)
        {
            const auto folder = modelFolder (job.modelId);
            const auto missing = ModelCatalog::missingFiles (*model, folder);
            if (! missing.isEmpty())
                return finishFailed (model->displayName + " is not installed or incomplete. Missing in "
                                     + folder.getFullPathName() + ": " + missing.joinIntoString (", "), "missing");
        }

        const auto worker = installWorker();
        juce::StringArray args { python.getFullPathName(), worker.getFullPathName() };
        HistoryEntry entry;
        juce::File outFile;

        if (isGenerate)
        {
            const auto& r = job.request;
            juce::String id;
            outFile = history.newWavFile (r.prompt, r.seed, id);
            paths.history().createDirectory();
            entry.id = id;
            entry.prompt = r.prompt.trim();
            entry.model = r.model;
            entry.seed = r.seed;
            entry.seconds = r.seconds;
            entry.wav = outFile;
            args.addArray ({ "generate", "--model", r.model, "--model-dir", modelFolder (r.model).getFullPathName(),
                             "--hf-home", paths.hfCache().getFullPathName(), "--prompt", entry.prompt,
                             "--seed", juce::String (r.seed), "--seconds", juce::String (r.seconds, 3),
                             "--out", outFile.getFullPathName() });
            if (r.steps > 0)
                args.addArray ({ "--steps", juce::String (r.steps) });
        }
        else
        {
            args.addArray ({ "download", "--model", job.modelId, "--model-dir", modelFolder (job.modelId).getFullPathName() });
            if (hasToken())
                args.addArray ({ "--token-file", paths.tokenFile().getFullPathName() });
        }

        WorkerProcess process;
        {
            const juce::ScopedLock sl (lock);
            if (cancelRequested)
            {
                status.result = JobStatus::Result::cancelled;
                status.finishedMs = juce::Time::currentTimeMillis();
                return;
            }
            juce::String startError;
            if (! process.start (args, startError))
            {
                status.result = JobStatus::Result::failed;
                status.error = makeReport (step, job.modelId, "Could not start the worker: " + startError);
                status.finishedMs = juce::Time::currentTimeMillis();
                return;
            }
            runningProcess = &process;
        }

        juce::var done, error;
        juce::StringArray otherOutput;
        juce::String pendingText;
        char buffer[4096];

        for (;;)
        {
            const auto n = process.read (buffer, (int) sizeof (buffer));
            if (n <= 0)
                break;

            pendingText += juce::String::fromUTF8 (buffer, n);

            for (int newline = pendingText.indexOfChar ('\n'); newline >= 0; newline = pendingText.indexOfChar ('\n'))
            {
                const auto line = pendingText.substring (0, newline).trim();
                pendingText = pendingText.substring (newline + 1);

                if (line.startsWithChar ('{'))
                {
                    const auto event = juce::JSON::parse (line);
                    if (event.isObject())
                    {
                        handleEvent (job, event, done, error);
                        continue;
                    }
                }

                if (line.isNotEmpty())
                {
                    otherOutput.add (line);
                    if (otherOutput.size() > 30)
                        otherOutput.remove (0);
                }
            }
        }

        int exitCode = -1;
        const auto exitText = process.wait (exitCode);
        bool wasCancelled;
        {
            const juce::ScopedLock sl (lock);
            runningProcess = nullptr;
            wasCancelled = cancelRequested;
        }

        if (wasCancelled)
        {
            outFile.deleteFile();
            setJob ([] (JobStatus& s) { s.result = JobStatus::Result::cancelled; s.stage = "cancelled"; s.finishedMs = juce::Time::currentTimeMillis(); });
            return;
        }

        if (error.isObject())
            return finishFailed (error["message"].toString() + "\n(worker " + exitText + ")", error["code"].toString());

        if (! done.isObject())
        {
            juce::String message = "The model worker stopped unexpectedly (" + exitText + ")."
                                   " The plug-in and the DAW are unaffected; try again.";
            if (! otherOutput.isEmpty())
                message << "\nLast output:\n" << otherOutput.joinIntoString ("\n");
            return finishFailed (message, "crash");
        }

        if (isGenerate)
        {
            entry.createdMs = juce::Time::currentTimeMillis();
            entry.generateSeconds = (double) done["generate_seconds"];
            entry.totalSeconds = (double) done["total_seconds"];
            entry.device = done["device"].toString();
            entry.isTest = (bool) done["is_test"];

            if (! outFile.existsAsFile())
                return finishFailed ("The worker reported success but wrote no file: " + outFile.getFullPathName(), "generate");

            history.add (entry);

            if (onGenerated)
                onGenerated (entry);
        }

        setJob ([] (JobStatus& s)
        {
            s.result = JobStatus::Result::succeeded;
            s.fraction = 1.0f;
            s.stage = "done";
            s.finishedMs = juce::Time::currentTimeMillis();
        });
    }
}
