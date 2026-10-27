#pragma once

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

        /** One wet sample for output time tOut (input sample number); newest is the last
            input sample number written to the delay buffers. */
        void processSample (std::int64_t tOut, std::int64_t newest, float ratio, float period,
                            bool voiced, bool formant, float& outL, float& outR) noexcept;

    private:
        float readAbs (const float* buf, double t) const noexcept;
        void addGrain (float period, float ratio, bool formant) noexcept;

        double sampleRate = 48000.0;
        const float* delayL = nullptr;
        const float* delayR = nullptr;
        int delaySize = 0;
        std::int64_t newestInput = 0, lastOut = -1;
        std::vector<float> olaL, olaR, olaW;
        int olaSize = 0, olaMask = 0, maxHalf = 0;
        bool marking = false;
        double synthMark = 0.0, analysisEpoch = 0.0;
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
                      bool nonRealtime, PitchTap* tap) noexcept;

        PitchEstimate getLastEstimate() const noexcept { return lastEstimate; }
        float getLastCorrectedHz() const noexcept { return lastCorrectedHz; }

    private:
        void analyseIfDue (const PitchSettings& s, bool hq) noexcept;
        float readDelay (const std::vector<float>& buf, int delay) const noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        int latency = 2048;
        int delaySize = 0, writePos = 0;
        int hop = 64, hopCounter = 0;
        std::vector<float> delayL, delayR, monoScratch;
        std::int64_t inputCount = 0; // input samples written so far (absolute time line)
        float voiceEnv = 0.0f;

        PitchDetector detector;
        PitchCorrector corrector;
        PitchShifter shifter;

        PitchEstimate lastEstimate;
        float lastCorrectedHz = 0.0f;
        float lastRatio = 1.0f;
        bool lastVoiced = false;
        float mixSmoothed = 1.0f;
    };
}
