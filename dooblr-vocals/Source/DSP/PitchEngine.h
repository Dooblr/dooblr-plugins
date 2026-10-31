#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PitchTap.h"
#include "VocalSettings.h"

namespace dooblrvocals::dsp
{
    struct PitchEstimate
    {
        float hz = 0.0f;
        float confidence = 0.0f;
        bool voiced = false;
    };

    /**
        YIN pitch detector (de Cheveigné & Kawahara).
        analyse() is allocation-free after prepare().
    */
    class PitchDetector
    {
    public:
        void prepare (double sampleRate, int maxWindow);
        void reset() noexcept;

        /** x is window samples of a mono signal. */
        PitchEstimate analyse (const float* x, int window, float minHz, float maxHz,
                               float yinThreshold, bool hq) const noexcept;

        int recommendedWindow (float minHz) const noexcept;

    private:
        double sampleRate = 48000.0;
        mutable std::vector<float> diff, rawDiff;
    };

    /**
        Snaps a detected F0 onto a scale or the chromatic grid, with retune speed and
        a hold / humanize region around the target.
    */
    class PitchCorrector
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        /** Returns the target frequency (Hz). Unvoiced input returns 0. hopSamples is the
            detector hop, so retune speed is in real milliseconds. */
        float process (float detectedHz, bool voiced, const PitchSettings& s, int hopSamples) noexcept;

        float getSmoothedHz() const noexcept { return smoothedHz; }

        static float hzToMidi (float hz, float a4) noexcept;
        static float midiToHz (float midi, float a4) noexcept;
        static float snapMidi (float midi, int mask) noexcept;

    private:
        double sampleRate = 48000.0;
        float smoothedHz = 0.0f;
        float holdHz = 0.0f;
        int holdSamples = 0;
    };

    /**
        Pitch-synchronous overlap-add (TD-PSOLA) shifter on an absolute time line.

        The engine writes input sample number t into a ring buffer (index t mod size) and asks
        for one wet sample per output time tOut = newest - latency. For each synthesis mark
        (spaced T / ratio apart, T the detected period) the shifter takes a Hann grain centred
        on the analysis epoch nearest the mark (epochs are spaced T apart, so every grain has
        the same phase) and overlap-adds it around the mark:
        - formant on: the grain is copied 1:1 (spectral envelope kept, pitch moves)
        - formant off: the grain is resampled by the ratio (envelope moves with the pitch)
        Where the grains' windows sum to less than 1 (voicing on / off) the dry input fills
        the rest, so voiced/unvoiced transitions cross-fade. process() does not allocate.
    */
    class PitchShifter
    {
    public:
        void prepare (double sampleRate, int maxGrainHalf);
        void reset() noexcept;

        void setDelayBuffers (const float* left, const float* right, int size) noexcept;

        /** Harmony voices: pitch-marked epochs (each grain centred on the waveform's peak, so
            large downward shifts read at the new pitch rather than as tremolo), a two-period
            analysis window for formant-preserving downshifts, and silence (not the dry input)
            where the grains do not cover the output. The lead keeps the default behaviour. */
        void setHarmonyVoice (bool on) noexcept { harmonyVoice = on; }

        /** One wet sample for output time tOut (input sample number); newest is the last
            input sample number written to the delay buffers.
            causal (Tracking, v0.2): grains are not shortened to fit the look-ahead; their
            pitch-synchronous epochs step back whole periods into the past instead, so the
            shifter works with a few samples of latency (voiced audio then lags by up to about
            one period). Ratios are limited to 0.25 .. 4 (harmony voices use +/- 2 octaves). */
        void processSample (std::int64_t tOut, std::int64_t newest, float ratio, float period,
                            bool voiced, bool formant, float& outL, float& outR, bool causal = false) noexcept;

    private:
        float readAbs (const float* buf, double t) const noexcept;
        void addGrain (float period, float ratio, bool formant, bool causal) noexcept;

        double sampleRate = 48000.0;
        const float* delayL = nullptr;
        const float* delayR = nullptr;
        int delaySize = 0;
        std::int64_t newestInput = 0, lastOut = -1;
        std::vector<float> olaL, olaR, olaW;
        int olaSize = 0, olaMask = 0, maxHalf = 0;
        bool marking = false, harmonyVoice = false;
        double synthMark = 0.0, analysisEpoch = 0.0;
    };

    /**
        v0.2 harmonizer: four extra PSOLA voices reading the pitch engine's delay buffers at
        the same output time as the lead, so every voice has exactly the lead's latency
        (only a voice's own Delay control moves it later, on purpose, for width).

        Each voice's pitch is the lead's output pitch (the corrected note, or the sung pitch
        when correction is off) moved by its interval: Scale mode counts steps through the
        key / scale (a third above E in C major is G), Fixed mode adds semitones. The voice is
        summed to mono and placed with an equal-power pan. Voices are gated with the detector's
        voicing (no harmonies on breaths or consonants). process() does not allocate.
    */
    class Harmonizer
    {
    public:
        void prepare (double sampleRate, int maxGrainHalf, const float* left, const float* right, int size);
        void reset() noexcept;

        /** Once per detector hop: new detected / lead pitch (Hz, 0 = unvoiced). */
        void setPitch (float detectedHz, float leadHz, bool isVoiced, const HarmonySettings& h, int mask, float a4) noexcept
        {
            setPitch (detectedHz, leadHz, isVoiced, h, mask, a4, leadHz);
        }

        /** leadNoteHz picks the lead note for Scale intervals; with Vibrato Respect it is the
            vibrato's corrected centre, so a wide vibrato does not flip the harmony's interval
            while leadHz (which carries the vibrato) still moves every voice with it. */
        void setPitch (float detectedHz, float leadHz, bool isVoiced, const HarmonySettings& h, int mask, float a4,
                       float leadNoteHz) noexcept;

        /** Adds the harmony voices for output time tOut to outL / outR. */
        void processSample (std::int64_t tOut, std::int64_t newest, float period, bool causal,
                            const HarmonySettings& h, float& outL, float& outR) noexcept;

        /** The interval (semitones) voice v uses for a lead note (rounded MIDI); for tests / UI. */
        static int intervalFor (const HarmonyVoice& v, int leadMidi, int mask) noexcept;
        float getVoiceRatio (int v) const noexcept { return ratios[(size_t) v]; }

    private:
        double sampleRate = 48000.0;
        std::array<PitchShifter, numHarmonyVoices> shifters;
        std::array<float, numHarmonyVoices> ratios {}, gains {}, panL {}, panR {}, delays {};
        std::array<std::vector<float>, numHarmonyVoices> lines;
        int lineSize = 0, linePos = 0;
        float env = 0.0f;
        bool voiced = false;
    };

    /**
        Vibrato Respect (v0.2.0): finds vibrato in a pitch track (MIDI notes, one reading per
        detector hop) and reports the slow centre it wobbles around.

        The track is smoothed (one-pole, about 20 Hz) and its turning points are found with a
        4 cent hysteresis. Every three alternating turning points give a midpoint
        (v0 + 2 v1 + v2) / 4, which cancels a sinusoid's swing and leaves its centre. A midpoint
        is good when both half-cycles are 59 .. 143 ms (3.5 .. 8.5 Hz), both swings are 6 .. 160
        cents, and the two half-cycles agree (period within 0.6 .. 1.67x, swing within 0.5 .. 2x).
        Vibrato is reported once a segment has two good midpoints (about 1.5 cycles), so a scoop,
        a single bend or a held note never counts. A jump of more than the vibrato depth plus
        0.6 semitones, a jump of 0.8 semitones within one reading, or more than 40 ms unvoiced
        starts a new segment, so a note change is never read as vibrato.

        push() and query() take absolute sample times: push readings in time order; query any time
        (the engine queries behind the newest reading when it has look-ahead). Allocation free.
    */
    class VibratoTracker
    {
    public:
        struct State
        {
            bool active = false;
            float centreMidi = 0.0f;
            float depthSemis = 0.0f;  // half the peak-to-peak swing
            float rateHz = 0.0f;
        };

        void prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
        void reset() noexcept;
        void push (std::int64_t t, float midi, bool voiced) noexcept;
        State query (std::int64_t t) const noexcept;

    private:
        struct Turn { std::int64_t t = 0; float v = 0.0f; bool peak = false; };
        struct Mid { std::int64_t t = 0; float v = 0.0f, depth = 0.0f, halfPeriod = 0.0f; int segment = 0; bool good = false; };
        struct Segment { std::int64_t start = 0; int id = 0; };
        static constexpr int ringSize = 16;

        void startSegment (std::int64_t t, float midi) noexcept;
        void addTurn (std::int64_t t, float v, bool peak) noexcept;
        const Mid* lastGoodMid() const noexcept;

        double rate = 48000.0;
        std::array<Turn, 2> turns {};
        int numTurns = 0;
        std::array<Mid, ringSize> mids {};
        int midHead = 0, midCount = 0;
        std::array<Segment, 8> segments {};
        int segHead = 0, segCount = 0, segmentId = 0;
        bool tracking = false;
        std::int64_t lastT = 0, lastVoicedT = 0;
        float y = 0.0f, lastMidi = 0.0f, cand = 0.0f, lo = 0.0f, hi = 0.0f;
        std::int64_t candT = 0;
        int dir = 0;
    };

    /**
        Real-time pitch correction: delay (reported as latency) -> YIN -> snap -> PSOLA -> mix.
        HQ uses a longer delay, a larger analysis window, a smaller hop and a stricter YIN threshold.
        When nonRealtime is set the HQ path is used even if the quality parameter is Live.
    */
    class PitchEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        int latencySamples (const PitchSettings& s, bool nonRealtime) const noexcept;
        int currentLatency() const noexcept { return latency; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples, const PitchSettings& s,
                      bool nonRealtime, PitchTap* tap) noexcept
        {
            process (buffer, numSamples, s, HarmonySettings {}, nonRealtime, tap);
        }

        void process (juce::AudioBuffer<float>& buffer, int numSamples, const PitchSettings& s,
                      const HarmonySettings& harmony, bool nonRealtime, PitchTap* tap) noexcept;

        ProcessMode currentMode() const noexcept { return mode; }
        const Harmonizer& getHarmonizer() const noexcept { return harmonizer; }

        PitchEstimate getLastEstimate() const noexcept { return lastEstimate; }
        float getLastCorrectedHz() const noexcept { return lastCorrectedHz; }

        /** Vibrato Respect: how much of the latest reading was treated as vibrato (0 .. 1), and
            the detected centre / depth / rate behind it. */
        float getVibratoWeight() const noexcept { return vibWeight; }
        VibratoTracker::State getVibratoState() const noexcept { return vibState; }

        /** Range learning: voiced / unvoiced readings while PitchSettings::learnRange is set. */
        void setLearnTap (PitchTap* t) noexcept { learnTap = t; }

    private:
        void analyseIfDue (const PitchSettings& s, const HarmonySettings& h) noexcept;
        void trackVibrato (const PitchSettings& s, int window, std::int64_t estimateTime, float threshold,
                           float minHz, float maxHz, float a4, float& correctorInputHz, float& vibratoSemis,
                           float respect) noexcept;
        float readDelay (const std::vector<float>& buf, int delay) const noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        int latency = 2048;
        int delaySize = 0, writePos = 0;
        int hop = 64, hopCounter = 0;
        PitchTap* learnTap = nullptr;
        std::vector<float> delayL, delayR, monoScratch;
        std::int64_t inputCount = 0; // input samples written so far (absolute time line)
        float voiceEnv = 0.0f;

        PitchDetector detector;
        PitchCorrector corrector;
        PitchShifter shifter;
        Harmonizer harmonizer;
        ProcessMode mode = ProcessMode::mix;

        PitchEstimate lastEstimate;
        float lastCorrectedHz = 0.0f;
        VibratoTracker vibrato;
        VibratoTracker::State vibState;
        float vibWeight = 0.0f, vibCentre = 0.0f, vibDepth = 0.0f, lastCentreHz = 0.0f;
        bool vibratoUsed = false;
        float lastRatio = 1.0f;
        bool lastVoiced = false;
        float mixSmoothed = 1.0f;
        int harmonyTail = 0;

        // Harmony Lo-Fi: band-limit -> sample-and-hold at a lower rate -> quantise. Only runs when
        // HarmonySettings::lofi > 0, so the default path is untouched.
        struct LoFi
        {
            float lp1L = 0.0f, lp1R = 0.0f, lp2L = 0.0f, lp2R = 0.0f;
            float holdL = 0.0f, holdR = 0.0f, phase = 1.0f;
        } lofiState;
    };
}
