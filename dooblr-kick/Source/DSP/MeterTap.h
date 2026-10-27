#pragma once

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrkick::dsp
{
    /** One meter reading: the peaks over about 5 ms of output (dBFS) and the deepest gain
        reduction in that time (dB, <= 0). */
    struct MeterFrame
    {
        float dynamicsInDb = -120.0f;   // after the EQ, before the transient shaper
        float compressorInDb = -120.0f; // after the shaper (the compressor's detector input)
        float dynamicsOutDb = -120.0f;  // after the compressor (makeup included)
        float compressorGrDb = 0.0f;    // compressor gain reduction, makeup excluded
        float limiterInDb = -120.0f;    // into the clipper / limiter
        float limiterOutDb = -120.0f;   // after the clipper / limiter, before the output gain
        float limiterGrDb = 0.0f;       // clipper / limiter gain reduction
        float driveInDb = -120.0f;      // Drive send bus, into the shaper
        float driveOutDb = -120.0f;     // Drive send bus, after tone / output / mix
        float dryDb = -120.0f;          // summed layers before the chain (the dry signal)
        float wetDb = -120.0f;          // after the chain (the wet signal)
        float outputDb = -120.0f;       // after dry / wet and the output gain
    };

    /** Linear peaks of one engine sub-block, accumulated into MeterFrames. */
    struct MeterBlock
    {
        float dynamicsIn = 0, compressorIn = 0, dynamicsOut = 0, compressorGrDb = 0;
        float limiterIn = 0, limiterOut = 0, limiterGrDb = 0;
        float driveIn = 0, driveOut = 0, dry = 0, wet = 0, output = 0;
    };

    /**
        Audio thread -> editor feed for the EQ spectrum and the dynamics / limiter views.

        Two single-producer / single-consumer lock-free FIFOs (juce::AbstractFifo) with storage
        allocated in the constructor: post-EQ mono samples for the spectrum analyser, and
        MeterFrames. The audio thread never blocks or allocates; when a FIFO is full (no editor
        is reading) new data is dropped. The consumer (the editor's vblank) is one thread.
    */
    class MeterTap
    {
    public:
        static constexpr int spectrumCapacity = 1 << 15;
        static constexpr int frameCapacity = 1 << 11;

        MeterTap();

        /** Not concurrent with the audio thread (prepareToPlay). */
        void prepare (double sampleRate) noexcept;

        // Audio thread ---------------------------------------------------------
        void pushSpectrum (const float* left, const float* right, int numSamples) noexcept;
        void addBlock (const MeterBlock& block, int numSamples) noexcept;

        // Consumer -------------------------------------------------------------
        int popSpectrum (float* destination, int maxSamples) noexcept;
        int popFrames (MeterFrame* destination, int maxFrames) noexcept;
        void discardAll() noexcept;

        double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
        /** Samples per MeterFrame (a multiple of 32, about 5 ms). */
        int getSamplesPerFrame() const noexcept { return samplesPerFrame.load (std::memory_order_relaxed); }
        double getFramesPerSecond() const noexcept { return getSampleRate() / (double) getSamplesPerFrame(); }

        /** Totals since construction (tests / diagnostics). */
        juce::int64 getFramesWritten() const noexcept { return framesWritten.load (std::memory_order_relaxed); }
        juce::int64 getSpectrumSamplesWritten() const noexcept { return spectrumWritten.load (std::memory_order_relaxed); }

    private:
        juce::AbstractFifo spectrumFifo { spectrumCapacity }, frameFifo { frameCapacity };
        std::vector<float> spectrum;
        std::vector<MeterFrame> frames;

        MeterBlock accumulated; // audio thread only
        int accumulatedSamples = 0;
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<int> samplesPerFrame { 256 };
        std::atomic<juce::int64> framesWritten { 0 }, spectrumWritten { 0 };
    };
}
