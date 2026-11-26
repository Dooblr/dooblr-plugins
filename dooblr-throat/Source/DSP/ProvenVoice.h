#pragma once
// Pre-rendered cache for embedded third-party voices (espeak-ng, Piper+WORLD).
// Inference, WORLD analysis and any allocation or lock happen on a worker or
// the message thread. The audio thread only reads an immutable published
// cache/play clip and copies into the output buffer (with a sample loop
// while the key is held). Playback is at the note's F0: WORLD resynthesizes
// on the worker with the stored spectral envelope so formants stay put.

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

    // WORLD features for one utterance, packed spectrogram/aperiodicity.
    struct WorldFeatures
    {
        int fs = 0;
        int f0n = 0;
        int fft = 0;
        int bins = 0;
        double framePeriod = 5.0;
        std::vector<double> temporal;
        std::vector<double> f0;
        std::vector<double> sp;
        std::vector<double> ap;
        bool valid() const noexcept { return f0n > 2 && bins > 0 && fs > 0 && ! sp.empty(); }
    };

    struct ProvenClip
    {
        std::vector<float> samples;
        int rate = 48000;
        double refHz = 261.63;
        int loopStart = 0;
        int loopEnd = 0; // exclusive; loop if loopEnd > loopStart + 32
        WorldFeatures world;
    };

    struct ProvenPlay
    {
        std::vector<float> samples;
        int rate = 48000;
        int loopStart = 0;
        int loopEnd = 0;
        int midi = -1;
        int mode = 0; // 0 speak, 1 syllable
        int syl = 0;
        double hz = 261.63;
        unsigned cacheGen = 0; // which phrase cache this clip was made from
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
        unsigned gen = 0;
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

        // 0 Auto, 1 Bright, 2 Deep. Applied at the next note synthesis.
        void setVoice (int voice) noexcept { voice_.store (voice, std::memory_order_release); }
        // 0 darker .. 0.5 neutral .. 1 brighter. Applied at the next note synthesis.
        void setFormant (double amount) noexcept { formant_.store (amount, std::memory_order_release); }
        void setSustaining (bool on) noexcept { sustaining_.store (on, std::memory_order_release); }
        // Vowel Loop (0.4.1): while sustaining, loop the vowel (Sing: the syllable's
        // vowel; Speak: the line's last vowel). Off: every clip plays once.
        void setVowelLoop (bool on) noexcept { vowelLoop_.store (on, std::memory_order_release); }

        // Message thread / worker: (re)build the speak clip and one clip per syllable
        // as WORLD features at a reference pitch. If wait is true (offline bounce / tests),
        // block until the cache is published or an error is set.
        void requestPrepare (ProvenKind kind, const std::string& phrase,
                             const std::vector<std::string>& syllables,
                             double lengthScale, bool wait);

        // Offline / tests: block until the latest request has been published (or failed).
        bool waitReady();

        // Offline / tests: block until a play clip exists for the last armed note.
        bool waitNoteReady();

        // Tear down the worker and ONNX/espeak globals. Safe to call more than once.
        void shutdown();

        void clear() noexcept;

        // Audio thread: copy the current play clip. No allocation, no lock.
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
        void publishPlay (std::unique_ptr<ProvenPlay> next);
        std::unique_ptr<ProvenCache> buildCache (ProvenKind kind, const std::string& phrase,
                                                 const std::vector<std::string>& syllables,
                                                 double lengthScale, double outRate);
        void armPlayback (int mode, int syl, double midiHz) noexcept;
        bool playMatchesRequest (const ProvenPlay* play) const noexcept;
        std::unique_ptr<ProvenPlay> synthesizeNote (const ProvenCache& cache, int mode, int syl,
                                                    double hz, double formant, int voice) const;

        std::atomic<double> outputRate_ { 48000.0 };
        std::atomic<bool> offline_ { false };
        std::atomic<bool> readyFlag_ { false };
        std::atomic<bool> engaged_ { false };
        std::atomic<int> playMode_ { 0 }; // 0 speak, 1 syllable
        std::atomic<int> playSyl_ { 0 };
        std::atomic<double> playPos_ { 0.0 };
        std::atomic<double> playRatio_ { 1.0 };
        std::atomic<double> playHz_ { 261.626 };
        std::atomic<int> inRender_ { 0 };
        std::atomic<const ProvenCache*> liveCache_ { nullptr };
        std::atomic<const ProvenPlay*> livePlay_ { nullptr };
        std::atomic<bool> sustaining_ { false };
        std::atomic<int> voice_ { 0 };
        std::atomic<double> formant_ { 0.5 };
        std::atomic<unsigned> noteGen_ { 0 };
        std::atomic<unsigned> noteDone_ { 0 };
        std::atomic<bool> vowelLoop_ { true };
        std::atomic<bool> preparing_ { false };
        std::atomic<unsigned> liveCacheGen_ { 0 };
        std::atomic<unsigned> reqCacheGen_ { 0 };

        // Audio-thread-only declick state (no atomics needed: render() and
        // armPlayback() both run on the audio thread).
        static constexpr int kTailMax = 512;
        float tail_[kTailMax] {};
        int tailLen_ = 0;
        int tailPos_ = 0;
        float lastGate_ = 0.0f;
        bool inXfade_ = false;
        bool lastLooping_ = false;
        void captureTail (const ProvenPlay* play) noexcept;
        void mixTail (float* out, size_t n) noexcept;

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
        std::unique_ptr<ProvenPlay> ownedPlay_;
        ProvenStats stats_ {};
        mutable std::string error_;
    };

    // Process-lifetime shutdown used from the engine destructor so Live can unload
    // the bundle without hitting ORT/espeak static-destructor races.
    void shutdownProvenGlobals();
}
