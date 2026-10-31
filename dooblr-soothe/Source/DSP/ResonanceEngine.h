#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "DepthCurve.h"

namespace dooblrsoothe::dsp
{
    /** One analysis frame for the editor, on a fixed log-frequency grid (20 Hz .. 20 kHz). */
    struct DisplayFrame
    {
        static constexpr int numPoints = 256;
        static constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
        static float pointFrequency (int point) noexcept;

        std::array<float, numPoints> input {};     // dBFS (a full-scale sine reads 0 dB)
        std::array<float, numPoints> output {};    // dBFS after the reduction (before mix / trim)
        std::array<float, numPoints> reduction {}; // dB of cut, >= 0 (the larger of the channels)
        int serial = 0;
    };

    /** Lock-free single-producer / single-consumer hand-off of the newest DisplayFrame
        (a triple buffer: the audio thread never waits and never allocates). */
    class DisplayExchange
    {
    public:
        DisplayFrame& writeSlot() noexcept { return slots[(size_t) writeIndex]; }
        void publish() noexcept
        {
            const auto previous = state.exchange (writeIndex | freshBit, std::memory_order_acq_rel);
            writeIndex = previous & indexMask;
        }
        /** The newest frame, or nullptr if nothing new arrived since the last call. */
        const DisplayFrame* read() noexcept
        {
            if ((state.load (std::memory_order_acquire) & freshBit) == 0)
                return nullptr;
            const auto previous = state.exchange (readIndex, std::memory_order_acq_rel);
            readIndex = previous & indexMask;
            return &slots[(size_t) readIndex];
        }

    private:
        static constexpr int freshBit = 4, indexMask = 3;
        std::array<DisplayFrame, 3> slots;
        std::atomic<int> state { 1 };
        int writeIndex = 0, readIndex = 2;
    };

    /**
        The resonance reducer: a short-time Fourier transform (periodic Hann analysis and
        synthesis windows, overlap-add) that applies a time-varying, real, per-bin gain.

        Per frame and bin, the detector compares the level of the bin's neighbourhood (3 bins)
        with a spectral envelope: the mean power of a log-frequency window around it (Sharpness
        narrows the window from 3 octaves to a quarter octave) with the bin's own neighbourhood
        left out, so a peak does not raise its own reference. What sticks out by more than the
        Selectivity threshold is the excess; the reduction is Depth x excess, weighted by the
        depth curve, smoothed across frequency, limited to Max Cut, and given per-bin attack /
        release (slower in the lows, faster in the highs). Hard mode adds a level-dependent
        part: bins above an absolute level are also pulled down (2:1 at full depth), so loud
        passages are treated harder than quiet ones. Soft mode is purely relative (the same
        treatment at any level).

        The latency is exactly the FFT size (prepare / setQuality report it). The engine also
        returns the input delayed by the same amount (the dry path for mix, delta and bypass).
        Everything is allocated in prepare(); process() and setQuality() do not allocate.
    */
    class ResonanceEngine
    {
    public:
        static constexpr int maxChannels = 2;
        enum Quality { standard = 0, high = 1, ultra = 2 };

        struct Settings
        {
            float depth = 0.35f;       // 0..1
            float sharpness = 4.0f;    // 0..10
            float selectivity = 3.0f;  // 0..10
            float attackMs = 6.0f, releaseMs = 80.0f;
            bool hard = false;
            float link = 1.0f;         // 0..1
            float focus = 0.0f;        // -1 (channel 1 / mid only) .. +1 (channel 2 / side only)
            float maxCutDb = 24.0f;
            int listenBand = -1;       // >= 0: output only what this band's region removes
            Bands bands {};
        };

        ResonanceEngine();
        ~ResonanceEngine();

        /** Allocates for the sample rate (message thread). */
        void prepare (double sampleRate, int quality);
        /** Switches FFT size / overlap and clears the state; no allocation (audio thread safe). */
        void setQuality (int quality) noexcept;
        void reset() noexcept;

        int getQuality() const noexcept { return quality; }
        int getFftSize() const noexcept { return fftSize; }
        int getHopSize() const noexcept { return hop; }
        int getLatencySamples() const noexcept { return fftSize; }
        static int fftSizeFor (double sampleRate, int quality) noexcept;

        void setSettings (const Settings& s) noexcept { settings = s; }

        /** in / wet / dry: numChannels (1 or 2) channels of numSamples. detector: the sidechain
            channels (numDetectorChannels 1 or 2) or nullptr to detect on the input itself.
            wet = processed input, dry = the input delayed by getLatencySamples(). */
        void process (const float* const* in, const float* const* detector, int numDetectorChannels, float* const* wet,
                      float* const* dry, int numChannels, int numSamples) noexcept;

        DisplayExchange& getDisplay() noexcept { return display; }

        /** Smoothed reduction (dB, >= 0) of the latest frame, per bin; for tests. */
        const float* getReduction (int channel) const noexcept { return smoothed[(size_t) channel].data(); }
        int getNumBins() const noexcept { return fftSize / 2 + 1; }
        double getSampleRate() const noexcept { return sampleRate; }

    private:
        void processFrame (int numChannels, bool useDetector) noexcept;
        void detect (const float* power, float* reduction) noexcept;
        void updateTables() noexcept;
        void publishDisplay (int numChannels) noexcept;
        void analyse (const std::vector<float>& ring, std::vector<float>& spectrum, float* power) noexcept;

        double sampleRate = 48000.0;
        int scale = 1, quality = high, fftSize = 2048, hop = 512, mask = 2047, numBins = 1025, maxFftSize = 2048;
        int writePos = 0, hopCounter = 0;
        float olaNorm = 1.0f, specNorm = 1.0f;

        std::unique_ptr<juce::dsp::FFT> fftSmall, fftLarge;
        juce::dsp::FFT* fft = nullptr;
        float inverseScaleSmall = 1.0f, inverseScaleLarge = 1.0f, inverseScale = 1.0f;

        std::vector<float> window;
        std::array<std::vector<float>, maxChannels> inRing, outAcc, detRing, spectrum, power, detPower, own, smoothed;
        std::vector<float> detSpectrum, linkPower, linked, temp, amount, attackCoeff, releaseCoeff, region, levelDb;
        std::vector<double> prefixPower;

        Settings settings;
        Bands tableBands {};
        float tableAttack = -1.0f, tableRelease = -1.0f;
        int tableListen = -2;
        bool tablesValid = false;

        std::array<int, DisplayFrame::numPoints> pointLo {}, pointHi {};
        DisplayExchange display;
        int displaySerial = 0;
    };
}
