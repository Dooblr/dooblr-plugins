#pragma once

#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace dooblrvocals::dsp
{
    /**
        Consumer side of the EQ spectrum (runs on the message thread at display rate).

        4096-point FFT, periodic Hann window, a new frame every 1024 samples. Each bin is
        calibrated to dBFS of a sine at that frequency. Ballistics: instant attack, 30 dB/s
        release (time based, so the display keeps falling when the host stops processing).
        Optional peak hold keeps the per-bin maximum until cleared.
        Everything is allocated in the constructor.
    */
    class SpectrumAnalyzer
    {
    public:
        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, numBins = fftSize / 2 + 1, hopSize = 1024;
        static constexpr float floorDb = -120.0f, releaseDbPerSecond = 30.0f;

        SpectrumAnalyzer();

        void setSampleRate (double newRate) noexcept { sampleRate = newRate; }
        double getSampleRate() const noexcept { return sampleRate; }

        /** Adds samples; returns the number of new FFT frames analysed. */
        int push (const float* samples, int numSamples) noexcept;
        /** Releases the live curve by elapsed time. */
        void decay (double seconds) noexcept;
        void reset() noexcept;
        void clearPeakHold() noexcept;

        const std::vector<float>& getLiveDb() const noexcept { return live; }
        const std::vector<float>& getPeakDb() const noexcept { return peak; }
        /** True when nothing is above the floor (no need to repaint). */
        bool isSilent() const noexcept { return maxLiveDb <= floorDb + 0.5f; }
        int getFramesAnalysed() const noexcept { return framesAnalysed; }

        /** Level at a frequency from a per-bin dB curve: the loudest bin within +/- half a
            column (octaveSpan wide), or interpolated between bins when the span is narrower. */
        float levelAt (const std::vector<float>& binsDb, float frequency, float octaveSpan) const noexcept;

    private:
        void analyse() noexcept;

        juce::dsp::FFT fft { fftOrder };
        std::vector<float> window, history, fftData, live, peak;
        int writePosition = 0, samplesSinceFrame = 0, filled = 0, framesAnalysed = 0;
        float maxLiveDb = floorDb, scale = 1.0f;
        double sampleRate = 48000.0;
    };
}
