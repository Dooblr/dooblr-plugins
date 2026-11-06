#pragma once

#include "Paths.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace dooblrgenerate::gen
{
    /** One past generation: a WAV in the history folder plus how it was made. */
    struct HistoryEntry
    {
        juce::String id;            // unique, also the WAV's file stem
        juce::String prompt, model;
        juce::int64 seed = 0;
        double seconds = 0.0;       // requested length
        juce::int64 createdMs = 0;  // ms since epoch
        double generateSeconds = 0.0, totalSeconds = 0.0;
        juce::String device;
        bool isTest = false;        // test backend, not AI
        juce::File wav;

        juce::var toVar() const;
        static HistoryEntry fromVar (const juce::var&, const juce::File& historyFolder);
    };

    /**
        The history of generations, shared by every instance on the machine
        (history/history.json, newest first). Thread-safe. Entries whose WAV is missing are
        dropped on load; WAVs are never deleted by the plug-in.
    */
    class History
    {
    public:
        explicit History (Paths paths);

        void reload();
        bool add (const HistoryEntry&);
        std::vector<HistoryEntry> entries() const;
        int size() const;
        bool find (const juce::String& id, HistoryEntry& out) const;

        /** A new WAV path in the history folder for a prompt and seed. */
        juce::File newWavFile (const juce::String& prompt, juce::int64 seed, juce::String& idOut) const;

        static constexpr int maxEntries = 500;

    private:
        bool save() const;

        Paths paths;
        mutable juce::CriticalSection lock;
        std::vector<HistoryEntry> list;
    };
}
