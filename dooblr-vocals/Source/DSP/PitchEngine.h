#pragma once

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
        mutable std::vector<float> diff;
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
        Formant-aware TD-PSOLA shifter (formant on) or resampled-grain shifter (formant off).
        Reads from a stereo delay line; process() does not allocate.
    */
    class PitchShifter
    {
    public:
        void prepare (double sampleRate, int delaySize);
        void reset() noexcept;

        void setDelayBuffers (const float* left, const float* right, int size, int writePos) noexcept;

        /** Writes n wet samples corresponding to delay[writePos - latency]. */
        void process (float* outL, float* outR, int n, int latency, const float* ratios,
                      const char* voiced, const float* periods, bool formant) noexcept;

    private:
        float readAt (const float* buf, float delay) const noexcept;
        void addGrain (int latency, float period, float ratio, bool formant) noexcept;

        double sampleRate = 48000.0;
        const float* delayL = nullptr;
        const float* delayR = nullptr;
        int delaySize = 0, writePos = 0;
        std::vector<float> olaL, olaR;
        int olaWrite = 0, olaSize = 0;
        float synthPhase = 0.0f;
        float lastPeriod = 100.0f;
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
        std::vector<float> delayL, delayR, monoScratch, ratioScratch, wetL, wetR;
        std::vector<char> voicedScratch;
        std::vector<float> periodScratch;
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
