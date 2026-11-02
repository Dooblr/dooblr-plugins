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
        bool merging = false;             // folded into a neighbour: fading out (crossfade), then freed
        float anchorHz = 0.0f;            // where the howl was last confirmed (0: the notch's own centre)
        float offPeakHz = 0.0f;           // a steady tonal peak inside the notch's span, off its centre
        float offPeakSec = 0.0f;
        float catchDb = -200.0f;   // detector level when the notch was created (bookkeeping, not saved)

        // Auto-recover (0.1.2, bookkeeping, not saved): a quiet floating notch steps back toward
        // 0 dB in small stages, probing after each one; see FeedbackEngine::recover().
        bool recovering = false;
        float stageStartDb = 0.0f;        // cut when the current stage began (restored + margin on a re-onset)
        float stageTargetDb = 0.0f;       // cut this stage glides to
        float stageDwellSec = 0.0f;       // time spent probing at the stage target
        float stageBaseDb = -200.0f;      // smoothed detector level at the notch when the stage began
        float probeDb = -200.0f;          // smoothed (about 0.45 s) detector level at the notch while recovering
        int probeHops = 0;                // consecutive hops the probe has read a re-onset
        float ringSec = 0.0f;             // how long the ringing test has fired without a break
        float recoverScale = 1.0f;        // 1, doubled after every re-onset (cap 8): slower next time
        int reonsets = 0;                 // re-onsets caught while recovering
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

        Audio path (zero latency): up to 24 bands, each a parametric peaking bell in series with a
        narrow notch (Cytomic / Simper trapezoidal SVF, safe under modulation). The first
        bellShareDb of the cut is the bell (Bell Q); anything deeper is the notch, and a full cut
        is still a zero on the unit circle. Cut, frequency and Q are smoothed (cut deepens with a
        4 ms time constant, releases with 40 ms) and coefficients are refreshed every 16 samples,
        so moving bands do not zipper. With no band active the audio passes bit-exact.

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
            bool hold = false;           // HOLD: floating bands never recover
            float sensitivity = 0.5f;    // 0..1
            float maxCutDb = 18.0f;
            float baseQ = 20.0f;
            float bellQ = 4.0f;          // parametric bell (the first bellShareDb of each cut)
            float releaseSec = 20.0f;    // Recover after: quiet time before the first stage
            int recoverSpeed = 1;        // 0 Slow, 1 Normal, 2 Fast
            float lowHz = 80.0f, highHz = 20000.0f;

            // Safety duck (last resort): see processSafety().
            bool safety = true;              // on / off
            bool safetyAlways = false;       // false: only while the detector reports a runaway
            float safetyThresholdDb = -6.0f; // peak dBFS, after the notches
            float safetyDepthDb = 12.0f;     // positive dB; >= fullDuckDb mutes
            float safetyReleaseSec = 1.5f;
        };

        /** Cut at (or beyond) which a notch is a true notch: a zero on the unit circle, infinite
            attenuation at its centre. Max cut "-inf" is stored as this value. */
        static constexpr float fullCutDb = 120.0f;
        /** Up to this cut a notch is the usual constant-Q bell; deeper cuts keep this bell's pole
            bandwidth and only move the zeros towards the unit circle (so the notch does not widen). */
        static constexpr float bellCapDb = 30.0f;
        /** The first this many dB of a band are a parametric peaking bell. The rest is the narrow notch. */
        static constexpr float bellShareDb = 12.0f;
        static constexpr float defaultBellQ = 4.0f;
        static bool isFullCut (float cutDb) noexcept { return cutDb >= fullCutDb - 0.5f; }
        /** Safety depth at (or beyond) which the duck mutes. */
        static constexpr float fullDuckDb = 100.0f;
        static constexpr float safetyAttackSec = 0.005f, safetyHoldSec = 0.08f, safetyHintSec = 0.5f;

        /** Safety duck state for the editor (lock-free). */
        struct SafetyState
        {
            bool active = false;        // reducing gain right now
            float reductionDb = 0.0f;   // current gain reduction, positive dB (>= fullDuckDb: muted)
            int events = 0;             // ducks since prepare / reset of the counter
            juce::int64 lastEventMs = 0;
            float totalSec = 0.0f;      // time spent ducking
        };
        SafetyState getSafety() const noexcept;
        void resetSafetyCount() noexcept { safetyEvents.store (0); safetyTotalSamples.store (0); safetyLastMs.store (0); }

        /** Derived detection thresholds (public for the tests and the docs). */
        struct Thresholds
        {
            float prominenceDb, seedProminenceDb, floorDb, persistSec, stableSec, growthSec, growthDb, growthDbPerSec, harmonicDb, initialCutDb, deepenDbPerSec;
        };
        static Thresholds thresholdsFor (const Settings&) noexcept;

        static constexpr int controlInterval = 16;
        static constexpr float minQ = 4.0f;
        static constexpr float mergeFadeDbPerSec = 60.0f;
        /** Auto-recover stages: each one lowers the cut by 25 % of it (3 - 6 dB; a cut of 3.5 dB or
            less goes to 0), glides there at recoverGlideDbPerSec[speed] and then probes for
            recoverDwellSec[speed]. Cuts deeper than bellCapDb (a true notch) first glide to bellCapDb
            at recoverDeepDbPerSec (that part is inaudible). Both are slowed by the notch's recoverScale.
            A re-onset restores the cut the stage started from plus recoverMarginDb. */
        static constexpr float recoverGlideDbPerSec[3] { 0.5f, 1.0f, 2.0f };
        static constexpr float recoverDwellSec[3] { 8.0f, 4.0f, 2.0f };
        static constexpr float recoverDeepDbPerSec = 30.0f, recoverMarginDb = 3.0f, recoverMaxScale = 8.0f;
        /** A level rise at the notch this much beyond what the removed cut explains is a re-onset. */
        static constexpr float reonsetRiseDb = 5.0f;
        static constexpr float probeSmoothing = 0.012f;  // per hop (about 5 ms): a 0.45 s average
        static constexpr int probeConfirmHops = 4;
        static constexpr float ringConfirmSec = 0.15f;   // a ring while recovering must last this long
        /** The cut a stage starting at cutDb glides to. */
        static float recoverStageTarget (float cutDb) noexcept;
        /** Seconds a quiet floating notch at cutDb takes to recover fully (Recover after included),
            with recoverScale 1 and no re-onset. */
        static float expectedRecoverySec (float cutDb, float recoverAfterSec, int speed) noexcept;
        /** Safety duck release: the gain comes back in safetyStages equal dB steps (each at
            Depth / Recover dB per second), probing safetyDwellFraction * Recover between steps.
            A re-trigger during the release multiplies the next release time by 1.5 (cap 4); it
            returns to 1 after safetyCalmSec without a duck. */
        static constexpr int safetyStages = 3;
        static constexpr float safetyDwellFraction = 0.3f, safetyMaxScale = 4.0f, safetyCalmSec = 10.0f;
        /** The seed (runaway) confirmation: rise since the window filled, over at least fastSec. */
        static constexpr float fastSec = 0.08f, fastRiseDb = 9.0f, fastDbPerSec = 60.0f;
        static constexpr float strongDipDb = 1.5f; // a dip this deep restarts the strong-run growth window

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
        int getMergeCount() const noexcept { return mergeCount.load(); } // notches folded into a neighbour so far
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

        /** The response of one bell or notch (analog prototype, matches the SVF), dB at `freq`. */
        static float notchResponseDb (float freq, float notchFreq, float cutDb, float q) noexcept;
        /** Bell (min(cut, bellShareDb) at bellQ) plus the narrow notch (the rest, at notchQ). */
        static float bandResponseDb (float freq, float centreHz, float cutDb, float notchQ, float bellQ) noexcept;
        static float bankResponseDb (const NotchArray& notches, float freq, float bellQ = defaultBellQ) noexcept;

        /** Test / diagnostic hook: counts, per analysis hop, what happened to the strongest peak
            within `toleranceHz` of `freq` (0 = off). Message thread sets it before processing. */
        struct WatchStats
        {
            int hops = 0, noPeak = 0, belowFloor = 0, lowProminence = 0, outOfRange = 0, covered = 0, candidate = 0;
            int confirmed = 0, musicalOvertone = 0, musicalAround = 0, musicalBase = 0, engaged = 0;
            float maxProminence = -100.0f, maxLevel = -200.0f;
            std::array<float, 256> traceLevel {}, traceProminence {}, traceFreq {};
        };
        void setWatch (float freq, float toleranceHz) noexcept { watchHz = freq; watchTolHz = toleranceHz; watch = {}; }
        const WatchStats& getWatch() const noexcept { return watch; }

    private:
        struct Filter // one SVF, either the parametric bell or the narrow notch
        {
            bool live = false;
            float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, m1 = 0.0f;
            std::array<float, 2> ic1 {}, ic2 {};               // per channel (stereo)
        };

        struct Slot // the audio side of one band: a bell and a notch at the same frequency
        {
            int id = 0;
            float cut = 0.0f, freq = 1000.0f, q = 20.0f, bellQ = defaultBellQ; // smoothed
            float targetCut = 0.0f, targetFreq = 1000.0f, targetQ = 20.0f, targetBellQ = defaultBellQ;
            int targetId = 0;
            Filter bell, notch;
        };

        struct Tracker
        {
            bool used = false, seen = false;
            float freq = 0.0f, firstFreq = 0.0f, firstLevel = 0.0f, lastLevel = 0.0f;
            float windowLevel = 0.0f; // level once the analysis window had filled (windowFrames > 0)
            int frames = 0, missed = 0, windowFrames = 0;
            // Frames at full prominence (the stable path counts these; more than 2 weak frames in a
            // row restart it), and the largest frame-to-frame fall since the window filled.
            float prominence = 0.0f;
            int strongFrames = 0, weakRun = 0;
            float maxDropDb = 0.0f, windowProminence = 0.0f;
            // The growth window of the current strong run (a seed that turns strong starts it then,
            // so a slow howl rising out of a weak seed is judged from its strong start).
            int strongWindowFrames = 0;
            float strongWindowLevel = 0.0f;
        };

        struct Peak { float freq, level, prominence; };
        static constexpr int maxPeaks = 64;

        void updateControl() noexcept;
        void designCut (Filter& filter, float freq, float q, float cutDb, bool fullNotch) noexcept;
        void analyseHop (bool analyse) noexcept;
        void processSafety (float* const* io, int channels, int start, int len, bool analyse) noexcept;
        void armSafety() noexcept { safetyHint = (int) (safetyHintSec * (float) sampleRate); }
        void transform (const std::vector<float>& ring, std::vector<float>& db, std::vector<float>* power = nullptr) noexcept;
        void publishDisplay() noexcept;
        void detect (float hopSec) noexcept;
        void closedLoop (float hopSec, const Thresholds& t) noexcept;
        void engage (float freq, float level, const Thresholds& t, float extraCutDb = 0.0f) noexcept;
        void retrigger (Notch& n, const Thresholds& t, float freq = 0.0f) noexcept;
        void consolidate() noexcept;
        void recover (Notch& n, float hopSec, float peak, float prominence, const Thresholds& t) noexcept;
        void reonset (Notch& n) noexcept;
        static void startStage (Notch& n) noexcept;
        float refineBin (int k, float quadraticBin) const noexcept;
        /** Half-width (Hz) within which a detection is folded into notch n instead of adding one. */
        static float mergeHz (const Notch& n, float binHz) noexcept;
        void findPeaks (float floorDb) noexcept;
        float neighbourDb (float centreBin, float inner, float outer) const noexcept;
        float localFloorDb (int k, float level) const noexcept;
        const Peak* peakNear (float freq, float tolerance) const noexcept;
        /** True when the peak looks musical: its own overtone at 2f / 3f, or it is a partial of a
            series (a lower peak whose other partials are present too). */
        bool isMusical (float freq, float level, float harmonicDb, bool checkOvertones) const noexcept { return musicalReason (freq, level, harmonicDb, checkOvertones) != 0; }
        /** 0: not musical; 1: its own 2f; 2: partials of a lower series around it; 3: a prominent base plus a partial. */
        int musicalReason (float freq, float level, float harmonicDb, bool checkOvertones) const noexcept;
        void updateWatch (const Thresholds& t, float lowHz, float highHz) noexcept;
        juce::int64 wallMsNow() const noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        int fftOrder = 12, fftSize = 4096, hopSize = 256, numBins = 2049;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, fftData, detRing, inRing, outRing, detDb, detPower, inDb, outDb;
        std::vector<float> detRe, detIm, prevRe, prevIm; // detector spectrum, this hop and the last (phase-vocoder refinement)
        bool havePrevSpectrum = false;
        std::atomic<int> mergeCount { 0 };
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

        // Safety duck (audio thread) and what the editor reads.
        float safetyGain = 1.0f, safetyEnv = 0.0f, safetyEnvRelease = 0.0f, safetyAttack = 0.0f;
        int safetyHold = 0, safetyHint = 0, safetyLate = 0;
        bool safetyDucking = false;
        int safetyStage = 0, safetyDwell = 0, safetyCalm = 0;   // staged release (0: not releasing)
        float safetyStageGain = 1.0f, safetyStartDb = 0.0f, safetyScale = 1.0f;
        std::atomic<bool> safetyActive { false };
        std::atomic<float> safetyReductionDb { 0.0f };
        std::atomic<int> safetyEvents { 0 };
        std::atomic<juce::int64> safetyLastMs { 0 }, safetyTotalSamples { 0 };

        float watchHz = 0.0f, watchTolHz = 0.0f;
        WatchStats watch;

        DisplayFrame frame;
        DisplayExchange display;
        std::array<float, DisplayFrame::numPoints> pointBin {}, pointWidth {};
    };
}
