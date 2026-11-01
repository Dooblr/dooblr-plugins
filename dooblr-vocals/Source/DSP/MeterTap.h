#pragma once

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrvocals::dsp
{
    struct MeterFrame
    {
        float dynamicsInDb = -120.0f;
        float compressorInDb = -120.0f;
        float dynamicsOutDb = -120.0f;
        float compressorGrDb = 0.0f;
        float limiterInDb = -120.0f;
        float limiterOutDb = -120.0f;
        float limiterGrDb = 0.0f;
        float gateInDb = -120.0f;
        float gateOutDb = -120.0f;
        float gateGrDb = 0.0f;
        float outputDb = -120.0f;
        float deEssBandDb = -120.0f; // de-esser side-chain band peak
        float deEssGrDb = 0.0f;      // de-esser cut (<= 0)
    };

    struct MeterBlock
    {
        float dynamicsIn = 0, compressorIn = 0, dynamicsOut = 0, compressorGrDb = 0;
        float limiterIn = 0, limiterOut = 0, limiterGrDb = 0;
        float gateIn = 0, gateOut = 0, gateGrDb = 0, output = 0;
        float deEssBand = 0, deEssGrDb = 0;
    };

    class MeterTap
    {
    public:
        static constexpr int spectrumCapacity = 1 << 15;
        static constexpr int frameCapacity = 1 << 11;

        MeterTap();

        void prepare (double sampleRate) noexcept;

        void pushSpectrum (const float* left, const float* right, int numSamples) noexcept;
        void addBlock (const MeterBlock& block, int numSamples) noexcept;

        int popSpectrum (float* destination, int maxSamples) noexcept;
        int popFrames (MeterFrame* destination, int maxFrames) noexcept;
        void discardAll() noexcept;

        double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
        int getSamplesPerFrame() const noexcept { return samplesPerFrame.load (std::memory_order_relaxed); }
        double getFramesPerSecond() const noexcept { return getSampleRate() / (double) getSamplesPerFrame(); }

        juce::int64 getFramesWritten() const noexcept { return framesWritten.load (std::memory_order_relaxed); }
        juce::int64 getSpectrumSamplesWritten() const noexcept { return spectrumWritten.load (std::memory_order_relaxed); }

    private:
        juce::AbstractFifo spectrumFifo { spectrumCapacity }, frameFifo { frameCapacity };
        std::vector<float> spectrum;
        std::vector<MeterFrame> frames;

        MeterBlock accumulated;
        int accumulatedSamples = 0;
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<int> samplesPerFrame { 256 };
        std::atomic<juce::int64> framesWritten { 0 }, spectrumWritten { 0 };
    };
}
