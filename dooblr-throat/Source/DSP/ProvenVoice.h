#pragma once
// Pre-rendered cache for embedded third-party voices (espeak-ng, Piper+WORLD).
// Inference, WORLD analysis and any allocation or lock happen on a worker or
// the message thread. The audio thread only reads an immutable published
// cache and copies/resamples into the output buffer.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dooblrthroat::dsp
{
    enum class ProvenKind { None = 0, Espeak = 1, Piper = 2, Mbrola = 3 };

    struct ProvenStats
    {
        double synthSeconds = 0;
        double audioSeconds = 0;
        double inferSeconds = 0;
        int nativeRate = 0;
        int outputRate = 0;
    };

    struct ProvenClip
    {
        std::vector<float> samples;
        int rate = 48000;
        double refHz = 261.63;
    };

    struct ProvenCache
    {
        ProvenKind kind = ProvenKind::None;
        std::string text;
        double refHz = 261.63;
        int rate = 48000;
        double lengthScale = 1.0;
        ProvenClip speak;
        std::vector<ProvenClip> syllables;
        ProvenStats stats {};
    };

    class ProvenEngine
    {
    public:
        ProvenEngine();
        ~ProvenEngine();

        ProvenEngine (const ProvenEngine&) = delete;
        ProvenEngine& operator= (const ProvenEngine&) = delete;

        void setOutputRate (double fs) noexcept { outputRate_.store ((fs > 1000.0) ? fs : 48000.0, std::memory_order_release); }
        void setOffline (bool offline) noexcept { offline_.store (offline, std::memory_order_release); }
        bool offline() const noexcept { return offline_.load (std::memory_order_acquire); }

        // Message thread / worker: (re)build the speak clip and one clip per syllable
        // at a reference pitch. If wait is true (offline bounce / tests), block until
        // the cache is published or an error is set.
        void requestPrepare (ProvenKind kind, const std::string& phrase,
                             const std::vector<std::string>& syllables,
                             double lengthScale, bool wait);

        // Offline / tests: block until the latest request has been published (or failed).
        bool waitReady();

        // Tear down the worker and ONNX/espeak globals. Safe to call more than once.
        void shutdown();

        void clear() noexcept;

        // Audio thread: copy / resample the current clip. No allocation, no lock.
        bool render (float* out, size_t n, double gate) noexcept;

        void startSpeakPlayback (double midiHz) noexcept;
        void startSyllablePlayback (int index, double midiHz) noexcept;
        void stopPlayback() noexcept;
        void disengage() noexcept { engaged_.store (false, std::memory_order_release); }

        bool ready() const noexcept { return liveCache_.load (std::memory_order_acquire) != nullptr; }
        bool engaged() const noexcept { return engaged_.load (std::memory_order_acquire); }
        bool finished() const noexcept;
        int playingSyllable() const noexcept { return playSyl_.load (std::memory_order_acquire); }

        ProvenStats lastStats() const;
        std::string lastError() const;

    private:
        void workerLoop();
        void publishCache (std::unique_ptr<ProvenCache> next);
        std::unique_ptr<ProvenCache> buildCache (ProvenKind kind, const std::string& phrase,
                                                 const std::vector<std::string>& syllables,
                                                 double lengthScale, double outRate);
        void armPlayback (int mode, int syl, double midiHz) noexcept;

        std::atomic<double> outputRate_ { 48000.0 };
        std::atomic<bool> offline_ { false };
        std::atomic<bool> readyFlag_ { false };
        std::atomic<bool> engaged_ { false };
        std::atomic<int> playMode_ { 0 }; // 0 speak, 1 syllable
        std::atomic<int> playSyl_ { 0 };
        std::atomic<double> playPos_ { 0.0 };
        std::atomic<double> playRatio_ { 1.0 };
        std::atomic<int> inRender_ { 0 };
        std::atomic<const ProvenCache*> liveCache_ { nullptr };

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::condition_variable readyCv_;
        std::thread worker_;
        std::atomic<bool> stop_ { false };
        bool started_ = false;
        bool shutDown_ = false;
        bool pending_ = false;
        bool working_ = false;
        unsigned generation_ = 0;
        unsigned doneGeneration_ = 0;
        ProvenKind pendingKind_ = ProvenKind::None;
        std::string pendingPhrase_;
        std::vector<std::string> pendingSyllables_;
        double pendingLength_ = 1.0;

        std::unique_ptr<ProvenCache> ownedCache_;
        ProvenStats stats_ {};
        mutable std::string error_;
    };

    // Process-lifetime shutdown used from the engine destructor so Live can unload
    // the bundle without hitting ORT/espeak static-destructor races.
    void shutdownProvenGlobals();
}
