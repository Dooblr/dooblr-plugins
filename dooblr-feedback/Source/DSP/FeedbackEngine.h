#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace dooblrfeedback::dsp
{
    inline constexpr int maxNotches = 24;

    /** One notch as the editor, presets, session state and tests see it. */
    struct Notch
    {
        int id = 0;                       // unique per engine session, > 0 while active
        bool active = false;
        bool fixed = false;               // held until cleared (ring-out or locked); floating ones release
        bool ringing = false;             // feedback is being pulled down at it right now
        float freq = 1000.0f;             // Hz
        float cutDb = 0.0f;               // current cut, positive dB
        float neededDb = 0.0f;            // the deepest cut it has needed so far
        float q = 20.0f;
        float levelDb = -140.0f;          // latest analysed peak level at the notch, dBFS
        int hits = 0;                     // detections (first + every re-ring after a quiet spell)
        juce::int64 detectedMs = 0;       // wall clock (ms since 1970) of the first detection
        juce::int64 detectedSample = -1;  // engine sample clock of the first detection (-1: loaded)

        // Engine bookkeeping (not saved)
        float quietSec = 0.0f;            // time since it last rang
        float graceSec = 0.0f;            // closed-loop wait after engaging (analysis window catching up)
        float history[4] {};
        int historyCount = 0;
    };

    using NotchArray = std::array<Notch, maxNotches>;

    /** One analysis frame for the graph: 256 log-spaced points, 20 Hz - 20 kHz, dBFS (a full-scale
        sine reads 0 dB). */
    struct DisplayFrame
    {
        static constexpr int numPoints = 256;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f;

        std::array<float, numPoints> input {};   // the mic, before the notches
        std::array<float, numPoints> output {};  // after the notches
        float binHz = 0.0f;
        juce::uint32 serial = 0;

        static float pointFrequency (int point) noexcept;
    };

    /** Lock-free triple buffer: the audio thread publishes, the message thread reads the newest. */
    class DisplayExchange
    {
    public:
        void publish (const DisplayFrame& frame) noexcept;
        /** Copies the newest frame into `out`; false when nothing new has arrived. */
        bool read (DisplayFrame& out) noexcept;

    private:
        static constexpr int freshBit = 4, indexMask = 3;
        std::array<DisplayFrame, 3> frames;
        std::atomic<int> middle { 1 };
        int writeIndex = 0, readIndex = 2;
    };

    /**
        The live feedback suppressor.

        Audio path (zero latency): up to 24 bell filters (Cytomic / Simper trapezoidal SVF, safe
        under modulation) applied in series to every channel. Cut, frequency and Q are smoothed
        (cut deepens with a 4 ms time constant, releases with 40 ms) and coefficients are refreshed
        every 16 samples, so moving notches do not zipper. With no notch active the audio passes
        bit-exact.

        Detection (beside the path, never delaying it): a mono sum of the processed output, or of
        the reference / sidechain signal, is analysed with a Hann-windowed FFT (4096 points at
        44.1 / 48 kHz, hop 256 = 5.3 ms). Feedback is told from programme material by:
          - prominence: the peak stands out from the bins around it (narrow);
          - persistence and stability: it stays put (vibrato or a gliding voice drifts away);
          - growth: a fast path confirms a peak that keeps rising (a howl building up);
          - no harmonics: a peak with partials at 2f / 3f, or that is itself a partial of f / 2,
            f / 3, is a musical note, not a feedback tone.
        A confirmed peak gets a notch (or widens / deepens a neighbouring one). Every notch is then
        controlled in a closed loop: while its frequency still rings and is not decaying it deepens
        (up to Max cut); once quiet, floating notches wait Release seconds and recover slowly.

        Threading: the notch model is guarded by a spin lock. The audio thread only try-locks (it
        skips one analysis hop when the editor holds the lock), the message thread locks briefly to
        read or edit notches. No allocation after prepare().
    */
    class FeedbackEngine
    {
    public:
        struct Settings
        {
            bool detect = true;          // Auto
            bool aggressive = false;     // Aggressive mode (Ring-out implies it)
            bool ringOut = false;        // new / re-ringing notches become fixed
            bool hold = false;           // floating notches never release
            float sensitivity = 0.5f;    // 0..1
            float maxCutDb = 18.0f;
            float baseQ = 20.0f;
            float releaseSec = 20.0f;
            float lowHz = 80.0f, highHz = 16000.0f;
        };

        /** Derived detection thresholds (public for the tests and the docs). */
        struct Thresholds
        {
            float prominenceDb, floorDb, persistSec, stableSec, growthDb, growthDbPerSec, harmonicDb, initialCutDb, deepenDbPerSec;
        };
        static Thresholds thresholdsFor (const Settings&) noexcept;

        static constexpr int controlInterval = 16;
        static constexpr float minQ = 4.0f;
        /** How fast a released (floating, quiet for the release time) notch fades out. */
        static constexpr float releaseDbPerSec = 6.0f;

        void prepare (double sampleRate, int maxBlockSize);
        /** Clears filter, analysis and tracker state (not the notches). */
        void reset() noexcept;
        void setSettings (const Settings& s) noexcept { settings = s; }
        const Settings& getSettings() const noexcept { return settings; }

        /** Processes `numChannels` channels in place. `detector`: mono detection source (the
            reference mic) or nullptr to detect on the processed output. `analyse`: false freezes
            detection and the notch model (bypass); the display keeps running. */
        void process (float* const* io, int numChannels, int numSamples, const float* detector, bool analyse) noexcept;

        // ---- message thread (brief lock) ----
        NotchArray getNotches() const;
        /** Replaces every notch (a loaded preset or session); ids are reassigned. */
        void setNotches (const NotchArray& notches);
        bool clearNotch (int notchId);
        void clearAll();
        bool setFixed (int notchId, bool fixed);
        /** Adds a notch directly (presets / tests). Returns its id, 0 when the bank is full. */
        int addNotch (float freq, float cutDb, float q, bool fixed, int hits = 1, juce::int64 detectedMs = 0);
        int countActive() const;
        bool wasBankFull() const noexcept { return bankFull.load(); }

        DisplayExchange& getDisplay() noexcept { return display; }
        double getSampleRate() const noexcept { return sampleRate; }
        int getFftSize() const noexcept { return fftSize; }
        int getHopSize() const noexcept { return hopSize; }
        float getBinHz() const noexcept { return (float) (sampleRate / fftSize); }
        /** Time for a new tone to (practically) fill the Hann-windowed analysis frame. */
        float windowFillSec() const noexcept { return 0.7f * (float) fftSize / (float) sampleRate; }
        juce::int64 getSampleClock() const noexcept { return clock.load (std::memory_order_relaxed); }
        /** Times stamped on new notches: wall-clock ms at sample `clockAtBase`. */
        void setWallClockBase (juce::int64 wallMs) noexcept;
        /** Number of analysis hops whose detection was skipped because the model was locked. */
        int getSkippedHops() const noexcept { return skippedHops.load(); }

        /** The response of one bell (analog prototype, matches the SVF), dB at `freq`. */
        static float notchResponseDb (float freq, float notchFreq, float cutDb, float q) noexcept;
        static float bankResponseDb (const NotchArray& notches, float freq) noexcept;

    private:
        struct Slot // the audio side of one notch
        {
            int id = 0;
            float cut = 0.0f, freq = 1000.0f, q = 20.0f;      // smoothed
            float targetCut = 0.0f, targetFreq = 1000.0f, targetQ = 20.0f;
            int targetId = 0;
            bool live = false;
            float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, m1 = 0.0f;
            std::array<float, 2> ic1 {}, ic2 {};               // per channel (stereo)
        };

        struct Tracker
        {
            bool used = false, seen = false;
            float freq = 0.0f, firstFreq = 0.0f, firstLevel = 0.0f, lastLevel = 0.0f;
            float windowLevel = 0.0f; // level once the analysis window had filled (windowFrames > 0)
            int frames = 0, missed = 0, windowFrames = 0;
        };

        struct Peak { float freq, level, prominence; };
        static constexpr int maxPeaks = 64;

        void updateControl() noexcept;
        void analyseHop (bool analyse) noexcept;
        void transform (const std::vector<float>& ring, std::vector<float>& db, std::vector<float>* power = nullptr) noexcept;
        void publishDisplay() noexcept;
        void detect (float hopSec) noexcept;
        void closedLoop (float hopSec, const Thresholds& t) noexcept;
        void engage (float freq, float level, const Thresholds& t) noexcept;
        void retrigger (Notch& n, const Thresholds& t) noexcept;
        void findPeaks (float floorDb) noexcept;
        float neighbourDb (float centreBin, float inner, float outer) const noexcept;
        const Peak* peakNear (float freq, float tolerance) const noexcept;
        /** True when the peak looks musical: its own overtone at 2f / 3f, or it is a partial of a
            series (a lower peak whose other partials are present too). */
        bool isMusical (float freq, float level, float harmonicDb, bool checkOvertones) const noexcept;
        juce::int64 wallMsNow() const noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        int fftOrder = 12, fftSize = 4096, hopSize = 256, numBins = 2049;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, fftData, detRing, inRing, outRing, detDb, detPower, inDb, outDb;
        float specNorm = 1.0f;
        int ringPos = 0, sampleInHop = 0, hopCount = 0;
        bool detectorIsOutput = true;

        std::array<Slot, maxNotches> slots;
        float cutAttack = 0.0f, cutRelease = 0.0f, glide = 0.0f;
        int seenVersion = -1;

        // The model (guarded by modelLock)
        mutable juce::SpinLock modelLock;
        NotchArray model;
        std::array<Tracker, 32> trackers;
        std::array<Peak, maxPeaks> peaks {};
        int numPeaks = 0;
        int nextId = 1;
        std::atomic<int> modelVersion { 0 };
        std::atomic<bool> bankFull { false };
        std::atomic<int> skippedHops { 0 };

        std::atomic<juce::int64> clock { 0 }, wallBaseMs { 0 }, clockAtBase { 0 };

        DisplayFrame frame;
        DisplayExchange display;
        std::array<float, DisplayFrame::numPoints> pointBin {}, pointWidth {};
    };
}
