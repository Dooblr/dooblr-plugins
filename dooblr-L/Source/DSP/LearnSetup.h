#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "LimiterEngine.h"

namespace dooblrl::dsp
{
    /** Features extracted from a captured buffer (deterministic, CPU / SIMD). */
    struct LearnFeatures
    {
        float crestDb = 0, peakDb = -120, rmsDb = -120;
        float onsetRate = 0, onsetStrength = 0;
        float lowEnergy = 0, stereoCorr = 0;
        float momentaryLufs = -70, shortTermLufs = -70, integratedLufs = -70;
    };

    /** Settings Learn will write, plus the scored loudness of the winning render. */
    struct LearnResult
    {
        Settings settings;
        float targetLufs = -70;
        float score = -1.0e9f;
        bool valid = false;
    };

    /**
        Listening auto-setup: the audio thread only copies into a lock-free FIFO.
        A background thread (or a synchronous test entry) analyses the capture and
        searches limiter settings. No GPU path — juce::FloatVectorOperations on CPU.
    */
    class LearnSetup final : public juce::Thread
    {
    public:
        static constexpr int fifoCapacity = 1 << 17;
        static constexpr float defaultSeconds = 20.0f;
        static constexpr float minSeconds = 10.0f;
        static constexpr float maxSeconds = 30.0f;

        LearnSetup();
        ~LearnSetup() override;

        void prepare (double sampleRate) noexcept;

        /** Message thread. */
        void start (const Settings& current, float seconds = defaultSeconds);
        void cancel();
        bool isListening() const noexcept { return state.load (std::memory_order_acquire) == listening; }
        bool isBusy() const noexcept { const auto s = state.load (std::memory_order_acquire); return s == listening || s == analysing; }
        float progress() const noexcept { return progressNorm.load (std::memory_order_relaxed); }
        LearnResult takeResult(); // valid once, then cleared
        bool hasResult() const noexcept { return resultReady.load (std::memory_order_acquire); }

        // Audio thread: never blocks, never allocates. Drops if the FIFO is full.
        void pushAudio (const float* left, const float* right, int numSamples, int numChannels) noexcept;

        /** Deterministic analysis + search on an already-captured buffer (tests / worker). */
        static LearnResult analyseAndSearch (const float* left, const float* right, int numSamples,
                                             int numChannels, double sampleRate, const Settings& base,
                                             const std::atomic<bool>* cancel = nullptr);

        static LearnFeatures analyse (const float* left, const float* right, int numSamples,
                                      int numChannels, double sampleRate);

    private:
        enum { idle = 0, listening, analysing, done };

        void run() override;

        juce::AbstractFifo fifo { fifoCapacity };
        std::vector<float> ringL, ringR;
        std::vector<float> captureL, captureR;

        std::atomic<int> state { idle };
        std::atomic<float> progressNorm { 0 };
        std::atomic<bool> cancelFlag { false };
        std::atomic<bool> resultReady { false };
        std::atomic<int> capturedSamples { 0 };
        std::atomic<int> targetSamples { 0 };
        std::atomic<double> sampleRate { 48000.0 };

        LearnResult pending;
        Settings baseSettings;
        juce::CriticalSection resultLock;
    };
}
