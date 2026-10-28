#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace dooblrvocals::dsp
{
    /** Where Key Follow listens. Auto: the sidechain when it carries signal, else the vocal. */
    enum class KeySource { automatic = 0, sidechain, vocal };

    /** Key index 0..23: 0..11 = C..B major, 12..23 = C..B minor; -1 = none yet. */
    inline constexpr int keyTonic (int key) noexcept { return key < 0 ? 0 : key % 12; }
    inline constexpr bool keyIsMinor (int key) noexcept { return key >= 12; }

    /**
        One analysis channel: a 12-bin chromagram of a (downsampled) signal.

        Mono sum -> 6th-order Butterworth low-pass at 1.7 kHz -> decimate to about 5.3 kHz ->
        4096-point Hann FFT every 1024 decimated samples (about 0.19 s) -> spectral peaks between
        55 Hz and 1.7 kHz folded into pitch classes (weighted by how close each peak is to a tempered
        pitch at the current A4) -> per-frame normalised -> two exponential averages: a slow one
        (the key estimate, time constant tau) and a fast one (tau / 4, used to confirm a change). Frames below -60 dBFS are skipped (they carry no key evidence).
        Everything is allocated in prepare(); push() is real-time safe.
    */
    class ChromaAccumulator
    {
    public:
        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, hopSize = 1024;

        void prepare (double sampleRate, float a4Hz = 440.0f);
        void reset() noexcept;
        void setA4 (float a4Hz) noexcept;

        /** Adds n samples (one or two channels). Returns the number of new frames analysed. */
        int push (const float* left, const float* right, int n, double tauSeconds, double fastTauSeconds) noexcept;

        const std::array<float, 12>& getChroma() const noexcept { return chroma; }
        const std::array<float, 12>& getFastChroma() const noexcept { return fastChroma; }
        /** Seconds of voiced (non-silent) analysis behind the average, capped at 60. */
        double getEvidenceSeconds() const noexcept { return evidence; }
        /** Seconds since the last voiced frame (large when never voiced). */
        double getSecondsSinceSignal() const noexcept { return sinceSignal; }
        double getHopSeconds() const noexcept { return hopSeconds; }
        int getDecimation() const noexcept { return decimation; }

    private:
        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float process (float x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        void analyseFrame (double tauSeconds, double fastTauSeconds) noexcept;
        void rebuildBinMap() noexcept;

        double rate = 48000.0, decimatedRate = 48000.0 / 9.0, hopSeconds = 0.19;
        float a4 = 440.0f;
        int decimation = 9, phase = 0, write = 0, sinceHop = 0, filled = 0;
        std::array<Biquad, 3> lowpass;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> ring, window, work, binWeight;
        std::vector<int> binClass;
        int lowBin = 1, highBin = 2;
        std::array<float, 12> chroma {}, fastChroma {};
        double evidence = 0.0, sinceSignal = 1.0e9;
    };

    /**
        Key Follow's detector: chromagrams of the sidechain and of the dry vocal, Krumhansl-Schmuckler
        correlation over 24 keys, and hysteresis so a passing chord never flips the key.

        Smoothing: an 8 s exponential chroma average decides; a 2 s one must agree before a change
        accumulates. Hysteresis is a CUSUM: a challenger key earns (r[challenger] - r[current] - margin)
        per second only while it is the best key on both averages, loses `margin` per second otherwise,
        and takes over when its credit reaches the hold threshold. Sensitivity 0..1 scales the margin
        (0.06 .. 0.02 correlation) and the threshold (0.5 .. 0.2 correlation-seconds). The first key is
        adopted after 6 s of evidence when both averages agree. The vocal is weaker evidence
        (monophonic), so it uses twice the smoothing and 1.5x margin, threshold and evidence. When the
        chosen source is silent the current key is held. The detector never writes parameters; the
        processor applies the key.

        Audio thread: process(). Any thread: the atomic getters.
    */
    class KeyDetector
    {
    public:
        struct Settings
        {
            float sensitivity = 0.5f;           // 0 = steady .. 1 = responsive
            KeySource source = KeySource::automatic;
            float a4Hz = 440.0f;
        };

        void prepare (double sampleRate);
        void reset() noexcept;

        /** Sidechain may be null / 0 channels (bus disabled). The vocal is the dry main input. */
        void process (const float* const* sidechain, int sidechainChannels,
                      const float* const* vocal, int vocalChannels, int numSamples,
                      const Settings& settings) noexcept;

        /** Current decision (0..23), or -1 before anything was detected. */
        int getKey() const noexcept { return key.load (std::memory_order_relaxed); }
        /** Best-correlating key right now (may differ from getKey() while the hysteresis holds). */
        int getCandidate() const noexcept { return candidate.load (std::memory_order_relaxed); }
        /** 0..1: correlation of the smoothed chroma with the current key's profile. */
        float getConfidence() const noexcept { return confidence.load (std::memory_order_relaxed); }
        /** 0 = listening to nothing (holding), 1 = sidechain, 2 = vocal. */
        int getActiveSource() const noexcept { return activeSource.load (std::memory_order_relaxed); }
        /** True while the sidechain carried signal in the last few seconds. */
        bool hasSidechainSignal() const noexcept { return sidechainLive.load (std::memory_order_relaxed); }
        /** Increments every time the decided key changes. */
        int getChangeCount() const noexcept { return changes.load (std::memory_order_relaxed); }

        /** Pearson correlation of a chroma vector with each of the 24 key profiles. */
        static void correlate (const std::array<float, 12>& chroma, std::array<float, 24>& out) noexcept;
        static juce::String keyName (int key);

        static constexpr double sidechainHoldoffSeconds = 4.0; // Auto falls back to the vocal after this

    private:
        void decide (const ChromaAccumulator& from, bool fromVocal, float sensitivity, double seconds) noexcept;

        ChromaAccumulator sidechainChroma, vocalChroma;
        int current = -1, lastSource = 0;
        std::array<float, 24> credit {};
        double lastA4 = 440.0;
        std::atomic<int> key { -1 }, candidate { -1 }, activeSource { 0 }, changes { 0 };
        std::atomic<float> confidence { 0.0f };
        std::atomic<bool> sidechainLive { false };
    };
}
