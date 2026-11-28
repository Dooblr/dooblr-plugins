#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Bands.h"
#include "Biquad.h"
#include "Insights.h"
#include "KWeighting.h"
#include "LoudnessMath.h"
#include "Targets.h"
#include "Translation.h"
#include "TruePeak.h"

namespace dooblrmeter::dsp
{
    /** Everything measured over one 100 ms slot (sums, so slots can be merged and gated). */
    struct SlotData
    {
        double energy = 0.0;         // sum of K-weighted squares, channels summed (BS.1770, G = 1)
        double kL = 0.0, kR = 0.0;   // K-weighted squares per channel (balance)
        double sqL = 0.0, sqR = 0.0; // unweighted squares (RMS)
        double sumL = 0.0, sumR = 0.0; // sums (DC offset)
        float tp = 0.0f, sp = 0.0f;  // true peak / sample peak, linear
        int n = 0;
        int clips = 0;               // clip runs (3+ consecutive samples at |x| >= 0.999) starting here

        void merge (const SlotData& o) noexcept;
        void clear() noexcept { *this = SlotData(); }
    };

    /** A 4-second analysis segment (40 slots): spectrum, stereo and sibilance statistics. */
    struct SegmentData
    {
        std::array<double, bands::count> mid {}, side {}; // summed band mean-square per FFT frame
        int frames = 0;
        std::array<double, 4> lr {}, ll {}, rr {};        // full, <120 Hz, 120 Hz-2 kHz, >2 kHz
        double midLow = 0.0, sideLow = 0.0;
        int bursts = 0;
        long long samples = 0;
    };

    /** The data the editor draws, rebuilt by the analysis thread (never touched by the audio
        thread). Times are seconds on the active table's timeline. */
    struct Snapshot
    {
        // Live readouts (always the last few seconds, whatever the mode).
        float momentary = -150.0f, shortTerm = -150.0f, rmsShort = -150.0f, psr = 0.0f;
        float correlation = 1.0f, widthDb = -150.0f;
        std::array<float, 3> bandCorrelation { 1.0f, 1.0f, 1.0f };

        // Programme readouts (the active table: Live since Reset, or Song by position).
        LoudnessResult loudness;
        float truePeakMax = -150.0f, samplePeakMax = -150.0f, plr = 0.0f, medianPsr = 0.0f;
        float balanceDb = 0.0f, dcLeftDb = -150.0f, dcRightDb = -150.0f, monoLossDb = 0.0f;
        float sideToMidDb = -150.0f, sideLowToMidLowDb = -150.0f, correlationLong = 1.0f;
        std::array<float, 3> bandCorrelationLong { 1.0f, 1.0f, 1.0f };
        int clipRuns = 0, overRuns = 0;
        double analysedSeconds = 0.0, positionSeconds = 0.0;

        // Loudness history over the visible span.
        double historyT0 = 0.0, historyT1 = 30.0;
        std::vector<float> historyMomentary, historyShortTerm; // one value per point, -150 = gap
        std::vector<double> overTimes;                        // true peak over the ceiling

        // Spectrum.
        std::vector<float> liveSpectrum;      // dB, 1/3-octave band-power units at spectrumHz()
        bands::Levels ltasDb {}, curveDb {}, deviationDb {};
        bool ltasValid = false;

        // Vectorscope: interleaved L, R (most recent last).
        std::vector<float> scope;

        // Dynamics: short-term loudness histogram, 1 dB bins from -60 to 0 LUFS.
        std::array<int, 60> stHistogram {};

        std::array<TranslationResult, profiles::count> translation {};
        bool translationValid = false;
        std::vector<Finding> findings;

        bool songMode = false;
        double bpm = 0.0, beatsPerBar = 4.0;
        int droppedChunks = 0;
    };

    /** Frequencies of Snapshot::liveSpectrum points (log-spaced, 20 Hz to 20 kHz). */
    const std::vector<float>& spectrumHz();

    /**
        The meter engine. processAudio() runs on the audio thread and only does the exact,
        cheap part (K-weighting, true peak, peaks, sums per 100 ms slot), passing slot records
        and raw audio to the analysis side through lock-free FIFOs. pump() runs on the analysis
        thread (or directly from tests) and does everything else, then publishes a Snapshot.
    */
    class Analyzer
    {
    public:
        struct Settings
        {
            int target = 0, curve = 0, historySpan = 4, averaging = 1, sensitivity = 1;
            bool songMode = true, freeze = false;
            float customLufs = -14.0f, customCeiling = -1.0f;
        };

        Analyzer();
        ~Analyzer();

        // ---- audio thread ----
        void prepare (double sampleRate, int maxBlock, int numChannels);
        /** songSample: host timeline position of the first sample, or -1 when not playing. */
        void processAudio (const float* left, const float* right, int numSamples, juce::int64 songSample, bool nonRealtime) noexcept;

        // ---- analysis thread / tests ----
        /** Consumes everything queued and, when due (or forced), rebuilds the snapshot. */
        void pump (bool forceFullUpdate = false);
        void setSettings (const Settings& s) noexcept;
        void setTempo (double bpm, double beatsPerBar) noexcept;

        // ---- any thread ----
        std::shared_ptr<const Snapshot> getSnapshot() const;
        void requestReset() noexcept { resetRequested.store (true); }
        /** True while nothing has been written by the analysis side since construction. */
        double getSampleRate() const noexcept { return sampleRate; }
        int getSlotLength() const noexcept { return slotLength; }
        int droppedChunkCount() const noexcept { return droppedChunks.load(); }

        /** When the background thread is not running, processAudio never waits for FIFO space. */
        void setConsumerRunning (bool running) noexcept { consumerRunning.store (running); }

        // ---- persistence (message thread) ----
        juce::MemoryBlock saveAnalysis() const;
        void loadAnalysis (const juce::MemoryBlock&);

        static constexpr int slotsPerSegment = 40;
        static constexpr size_t maxSlots = 72000; // 2 hours per table

    private:
        struct SlotRecord { int table; juce::int64 index; SlotData data; };
        struct Chunk
        {
            static constexpr int capacity = 256;
            juce::int64 liveSample = 0, songSample = -1;
            int n = 0;
            float l[capacity], r[capacity];
        };
        struct Table
        {
            std::vector<SlotData> slots;
            std::vector<SegmentData> segments;
            void clear() { slots.clear(); segments.clear(); }
            SlotData* slot (juce::int64 index);
            SegmentData* segment (juce::int64 index);
        };

        void pushRecord (int table, juce::int64 index, const SlotData&, bool nonRealtime) noexcept;
        void pushChunk (bool nonRealtime) noexcept;
        void consumeRecord (const SlotRecord&);
        void consumeChunk (const Chunk&);
        void analyseFrame (juce::int64 liveSegment, juce::int64 songSegment);
        void rebuildSnapshot (bool full);
        void clearAll();

        // Audio side --------------------------------------------------------------------------
        double sampleRate = 48000.0;
        int slotLength = 4800, channels = 2;
        std::array<KWeighting, 2> kw;
        TruePeak truePeak;
        std::array<int, 2> clipRun {};
        SlotData liveAcc, songAcc;
        juce::int64 liveSampleCount = 0, liveSlotIndex = 0, songSlot = -1;
        Chunk pending;
        juce::AbstractFifo recordFifo { 8192 }, chunkFifo { 2048 };
        std::vector<SlotRecord> records;
        std::vector<Chunk> chunks;
        std::atomic<int> droppedChunks { 0 }, droppedRecords { 0 };
        std::atomic<bool> consumerRunning { false }, resetRequested { false };

        // Analysis side -----------------------------------------------------------------------
        mutable juce::CriticalSection tableLock;
        Table liveTable, songTable;
        juce::int64 liveBase = -1;          // live slot index of the live table's slot 0
        juce::int64 pendingLiveCount = -1;  // after a load: slots already in the live table
        std::vector<SlotData> recentLive;   // last 30 live slots (momentary / short-term)
        std::atomic<bool> settingsChanged { true };
        juce::SpinLock settingsLock;
        Settings settings;
        std::atomic<double> tempoBpm { 0.0 }, tempoBeats { 4.0 };

        static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, hop = fftSize / 4;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, ringMid, ringSide, fftBuffer;
        std::vector<double> livePower;      // averaged power per bin
        double livePowerFrames = 0.0;
        int ringWrite = 0, samplesSinceFrame = 0;
        std::array<std::pair<int, int>, bands::count> bandBins {};
        double binScale = 1.0;

        std::array<Biquad, 2> lowL, lowR, highL, highR; // 120 Hz LR4, 2 kHz LR4
        std::array<Biquad, 3> sib;                       // 5-10 kHz band on the mid
        double sibFast = 0.0, sibSlow = 0.0, sibFastCoefA = 0.0, sibFastCoefR = 0.0, sibSlowCoef = 0.0;
        int sibHold = 0;
        std::array<double, 4> corrLR {}, corrLL {}, corrRR {};
        double corrCoef = 0.0, liveMid = 0.0, liveSide = 0.0;
        std::vector<float> scopeRing;
        int scopeWrite = 0, scopeDecimate = 0;
        juce::int64 lastSongSlotWritten = -1, lastLiveSegment = -1;

        double lastFullUpdate = 0.0;
        mutable juce::SpinLock snapshotLock;
        std::shared_ptr<const Snapshot> snapshot;

        JUCE_DECLARE_NON_COPYABLE (Analyzer)
    };
}
