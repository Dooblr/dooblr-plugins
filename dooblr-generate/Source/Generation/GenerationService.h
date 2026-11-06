#pragma once

#include "History.h"
#include "ModelCatalog.h"
#include "Paths.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>

namespace dooblrgenerate::gen
{
    class WorkerProcess;

    enum class ModelState { notInstalled, downloading, ready, needsAccess, runtimeMissing, error };

    struct ModelStatus
    {
        ModelState state = ModelState::notInstalled;
        float fraction = 0.0f;       // downloading
        juce::String text;           // one line for the status label
        juce::String details;        // copyable detail (errors, what to do)
    };

    struct GenerateRequest
    {
        juce::String prompt, model;
        juce::int64 seed = 0;
        double seconds = 2.0;
        int steps = 0;                    // 0: the model's default
    };

    struct JobStatus
    {
        enum class Kind { none, generate, download };
        enum class Result { none, running, succeeded, failed, cancelled };

        Kind kind = Kind::none;
        Result result = Result::none;
        juce::String model, stage, message;
        float fraction = 0.0f;
        juce::String error;               // copyable report when failed
        juce::int64 startedMs = 0, finishedMs = 0;
        int serial = 0;                   // increments with every job
    };

    /**
        Runs model work in a separate worker process (worker/dooblr_generate_worker.py, embedded
        in the plug-in and written to runtime/), one job at a time, on its own thread. The DAW
        process never loads a model: if the worker crashes, hangs or runs out of memory, the job
        fails with a copyable error and the plug-in keeps running. Nothing here touches the audio
        thread.
    */
    class GenerationService : private juce::Thread
    {
    public:
        using GeneratedCallback = std::function<void (const HistoryEntry&)>;

        GenerationService (Paths paths, History& history, GeneratedCallback onGenerated);
        ~GenerationService() override;

        /** Starts a job; false (and nothing happens) if one is already running or the request is invalid. */
        bool startGenerate (const GenerateRequest&);
        bool startDownload (const juce::String& modelId);
        /** Kills the running worker (the job ends as cancelled). */
        void cancel();
        bool isBusy() const;
        /** Waits for the running job to end (tests). */
        bool waitUntilIdle (int timeoutMs) const;

        JobStatus jobStatus() const;
        ModelStatus modelStatus (const juce::String& modelId) const;

        juce::File modelFolder (const juce::String& modelId) const;
        /** Uses a folder the user picked (a manual download); empty resets to the default folder. */
        void setModelFolder (const juce::String& modelId, const juce::File& folder);

        /** Stores a Hugging Face token in tokenFile() (owner-only permissions). */
        bool saveToken (const juce::String& token);
        bool hasToken() const;
        void clearToken();

        /** The Python interpreter the worker runs with (invalid File: none found): settings.json
            "python" if set (exclusively), else $DOOBLR_GENERATE_PYTHON, runtime/venv/bin/python3,
            ~/dooblr-build/generate-venv/bin/python3. */
        juce::File findPython() const;
        /** Writes the embedded worker script to runtime/ if it differs; returns the file. */
        juce::File installWorker() const;

        /** One copyable line block describing the last failure (version, OS, step, message). */
        static juce::String makeReport (const juce::String& step, const juce::String& model, const juce::String& message);

        const Paths& getPaths() const noexcept { return paths; }
        /** True for interpreters the worker must never use: the macOS / Xcode / Command Line
            Tools system Python (no torch, and not ours to load models into). */
        static bool isSystemPython (const juce::File&);
        /** Sets settings.json "python" (empty removes it). */
        void setPython (const juce::File&);

    private:
        struct Job
        {
            JobStatus::Kind kind = JobStatus::Kind::none;
            GenerateRequest request;
            juce::String modelId;
        };

        void run() override;
        void runJob (const Job&);
        void handleEvent (const Job&, const juce::var& event, juce::var& done, juce::var& error);
        void setJob (std::function<void (JobStatus&)> change);
        juce::var readSettings() const;
        void writeSettings (const juce::var&) const;

        Paths paths;
        History& history;
        GeneratedCallback onGenerated;

        mutable juce::CriticalSection lock;
        JobStatus status;
        Job pending;
        bool hasPending = false;
        bool cancelRequested = false;
        WorkerProcess* runningProcess = nullptr;
        std::map<juce::String, ModelStatus> lastModelErrors;
        juce::WaitableEvent wake;
    };

    juce::String toString (ModelState);
}
