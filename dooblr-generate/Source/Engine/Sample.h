#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace dooblrgenerate::engine
{
    /** One loaded generation: the audio plus where it came from. Immutable once published. */
    struct Sample
    {
        juce::AudioBuffer<float> audio;   // 1 or 2 channels
        double sampleRate = 44100.0;
        juce::File file;
        juce::String historyId, prompt, model;
        juce::int64 seed = 0;
        bool isTest = false;              // made by the test backend, not by a model

        double lengthSeconds() const { return sampleRate > 0 ? audio.getNumSamples() / sampleRate : 0.0; }

        /** Loads a WAV/AIFF file (max 60 s). Returns nullptr and sets error on failure. */
        static std::shared_ptr<Sample> load (const juce::File& file, juce::String& error);
    };

    /**
        Hands the current Sample to the audio thread without locks or waits on that thread.

        Writers (message thread, worker threads) serialise on a CriticalSection and keep every
        published Sample alive in a list. The audio thread calls acquire() at the start of each
        block: it reads the published pointer, advertises it as in use, and re-checks that it is
        still the published one (retrying if a writer swapped it in between). A writer frees only
        samples that are neither published nor advertised as in use, so the audio thread never
        touches a freed sample and never allocates, frees or blocks.
    */
    class SampleSlot
    {
    public:
        ~SampleSlot();

        /** Writers: publishes a sample (nullptr clears) and frees retired ones. */
        void publish (std::shared_ptr<const Sample> sample);
        /** Writers / UI: the published sample (shared ownership). */
        std::shared_ptr<const Sample> current() const;

        /** Audio thread only: the sample to play this block (may be nullptr). Lock-free. */
        const Sample* acquire() noexcept;

        /** For tests: how many samples are being kept alive. */
        int numAlive() const;
        /** Writers: frees retired samples (also done on every publish). */
        void collect();

    private:
        mutable juce::CriticalSection writeLock;
        std::vector<std::shared_ptr<const Sample>> alive;
        std::shared_ptr<const Sample> published;
        std::atomic<const Sample*> publishedRaw { nullptr };
        std::atomic<const Sample*> inUse { nullptr };
    };
}
