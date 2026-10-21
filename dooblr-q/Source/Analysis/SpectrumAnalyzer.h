#pragma once

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

namespace dooblrq::analysis
{
    /**
        Single-producer / single-consumer sample FIFO between the audio thread and the
        analyzer. The audio thread mixes its channels to mono straight into preallocated
        storage; it never blocks or allocates. When the FIFO is full the newest samples
        that do not fit are dropped (and counted), so the analyzer simply sees a gap.
    */
    class AnalyzerFifo
    {
    public:
        static constexpr int capacity = 1 << 15;

        AnalyzerFifo();

        /** Audio thread. Averages the channels to mono and appends what fits. */
        void push (const float* const* channels, int numChannels, int numSamples) noexcept;

        /** Consumer thread. Reads up to maxSamples into dest; returns the number read. */
        int pop (float* dest, int maxSamples) noexcept;

        /** Consumer thread. Throws away everything waiting (e.g. stale audio on reopen). */
        void discardAll() noexcept;

        int getNumReady() const noexcept { return fifo.getNumReady(); }
        juce::int64 getDroppedSamples() const noexcept { return dropped.load (std::memory_order_relaxed); }

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<float> storage;
        std::atomic<juce::int64> dropped { 0 };
    };

    /**
        FFT spectrum analyzer with live ballistics and a long-term average that can be
        shown as a "frozen" overlay. Runs entirely on the consumer (message) thread; not
        thread-safe itself.

        Calibration: 4096-point FFT, periodic Hann window, 75% overlap (hop 1024).
        Levels are in dBFS per bin, scaled so a sine of peak amplitude A reads
        20*log10(A) at its bin (a full-scale sine reads 0 dBFS); a sine between bins
        reads up to 1.42 dB lower (Hann scalloping). Silence reads floorDb.

        Accumulation: a running power average (and per-bin peak hold) of every frame
        since the last clear(), so it represents "everything that has played" in that
        window, weighted equally. Freeze shows that average as an overlay; it keeps
        accumulating (and the overlay keeps updating) while frozen.
    */
    class SpectrumAnalyzer
    {
    public:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numBins = fftSize / 2 + 1;
        static constexpr int hopSize = fftSize / 4;
        static constexpr float floorDb = -120.0f;
        static constexpr float liveReleaseDbPerSecond = 30.0f;

        SpectrumAnalyzer();

        /** Resets all analysis if the rate differs from the current one. */
        void setSampleRate (double newSampleRate);
        double getSampleRate() const noexcept { return sampleRate; }
        double getBinFrequency (int bin) const noexcept { return bin * sampleRate / fftSize; }

        /** Drains the FIFO into the analyzer; returns the number of new FFT frames. */
        int pull (AnalyzerFifo& fifo);

        /** Feeds mono samples directly; returns the number of new FFT frames. */
        int pushSamples (const float* samples, int numSamples);

        /** Per-bin dBFS of the most recent frame (no smoothing). */
        const std::vector<float>& getLatestFrameDb() const noexcept { return latestDb; }
        /** Per-bin dBFS with display ballistics: instant attack, liveReleaseDbPerSecond decay. */
        const std::vector<float>& getLiveDb() const noexcept { return liveDb; }

        /** Per-bin power average since the last clear(). */
        const std::vector<float>& getAverageDb();
        /** Per-bin maximum since the last clear(). */
        const std::vector<float>& getPeakDb() const noexcept { return peakDb; }
        double getAccumulatedSeconds() const noexcept;
        juce::int64 getAccumulatedFrames() const noexcept { return framesAccumulated; }

        /** Shows the long-term average as an overlay. It is not a static snapshot: the
            average keeps accumulating live audio, so the overlay keeps updating. */
        void freeze() noexcept { frozen = true; }
        /** Hides the overlay (the average keeps running; clear() restarts it). */
        void unfreeze() noexcept { frozen = false; }
        bool isFrozen() const noexcept { return frozen; }

        /** Restarts the long-term average and peak hold. The Freeze state is kept. */
        void clear();

        /** Level for display at a frequency: the maximum of the bins within +/- span/2
            octaves, or linear interpolation between neighbouring bins where they are
            sparser than the span (low frequencies). */
        static float levelAt (const std::vector<float>& binDb, double frequency, double sampleRate,
                              double octaveSpan) noexcept;

    private:
        void processFrame();
        void resetAccumulation();
        void resetAll();

        juce::dsp::FFT fft { fftOrder };
        std::vector<float> window, input, fftData, scratch;
        std::vector<float> latestDb, liveDb, averageDb, peakDb;
        std::vector<double> powerSum;

        int inputFill = 0;
        juce::int64 framesAccumulated = 0;
        double sampleRate = 48000.0;
        bool frozen = false;
    };
}
