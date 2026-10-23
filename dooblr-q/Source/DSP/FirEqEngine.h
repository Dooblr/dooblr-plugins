#pragma once

#include <array>
#include <complex>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "SvfFilter.h"

namespace dooblrq::dsp
{
    /** Per-band target used to build the FIR kernels (total gain = static + dynamic). */
    struct KernelBand
    {
        bool enabled = false;
        FilterType type = FilterType::bell;
        double frequency = 1000.0, q = 1.0, gainDb = 0.0;
        Slope slope = Slope::db12;
    };

    /**
        FIR equaliser used by the Natural Phase and Linear Phase modes.

        The kernel is designed from the analogue prototype of every band (so there is no
        bilinear "cramping" near Nyquist):
          - Linear Phase: the prototype magnitude with zero phase, made causal by a delay of
            half the kernel length and Hann-windowed. The impulse response is symmetric.
          - Natural Phase: the prototype's complex response (analogue magnitude AND phase),
            delayed by a short pre-delay that holds the small acausal part created by
            band-limiting, with a raised-cosine fade-in before and a fade-out at the end.
        Both channels use the same kernel, each with its own convolution state.

        Convolution is uniformly partitioned overlap-save (partition 256, FFT 512), which
        adds 256 samples of buffering. Total latency = kernel delay + 256.

        Real-time safe after prepare(): kernels are rebuilt on the calling (audio) thread
        into preallocated memory, and a new kernel replaces the old one at the next
        partition boundary with a 256-sample linear crossfade.
    */
    class FirEqEngine
    {
    public:
        enum class Mode { natural, linear };

        static constexpr int partitionSize = 256;
        static constexpr int maxChannels = 2;

        /** 1 up to 50 kHz, 2 up to 100 kHz, 4 above, so kernels keep their length in time. */
        static int rateFactor (double sampleRate) noexcept;
        static int kernelLength (Mode mode, double sampleRate) noexcept;
        static int kernelDelay (Mode mode, double sampleRate) noexcept;
        static int latencySamples (Mode mode, double sampleRate) noexcept { return kernelDelay (mode, sampleRate) + partitionSize; }

        FirEqEngine();
        ~FirEqEngine();

        /** Allocates for the longest kernel at this rate. Not real-time safe. */
        void prepare (double sampleRate);

        /** Selects a mode (no allocation) and clears all state. */
        void setMode (Mode newMode) noexcept;
        Mode getMode() const noexcept { return mode; }
        int getLatency() const noexcept { return latencySamples (mode, sampleRate); }

        void reset() noexcept;

        /** Builds the kernel for these bands. If immediate, it replaces the current kernel
            at once; otherwise it crossfades in at the next partition. */
        void setTarget (const std::array<KernelBand, 6>& bands, bool immediate) noexcept;

        /** Processes up to two channels in place. */
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        /** The designed kernel (time domain), for tests. */
        const std::vector<float>& getKernel() const noexcept { return kernelTime; }
        int getKernelLength() const noexcept { return kernelLength (mode, sampleRate); }

    private:
        using Complex = std::complex<float>;
        static constexpr int fftSize = partitionSize * 2;
        static constexpr int numBins = partitionSize + 1;

        void designKernel (const std::array<KernelBand, 6>& bands) noexcept;
        void partitionKernel (std::vector<Complex>& destination) noexcept;
        void processPartition (int numChannels) noexcept;
        void accumulate (const std::vector<Complex>& kernel, int channel, std::vector<float>& out) noexcept;

        double sampleRate = 48000.0;
        Mode mode = Mode::linear;
        int numPartitions = 1, maxPartitions = 1;

        std::unique_ptr<juce::dsp::FFT> partitionFft, naturalFft, linearFft;
        std::vector<float> designBuffer;

        std::vector<float> kernelTime;
        std::vector<Complex> current, next;                            // [partition][bin]
        std::array<std::vector<Complex>, maxChannels> history;         // input spectra ring
        std::array<std::vector<float>, maxChannels> inputWindow;       // 2 * partition
        std::array<std::vector<float>, maxChannels> inputBlock, outputBlock;
        std::vector<Complex> accumulator;
        std::vector<float> fftScratch, crossfadeScratch;

        // Per-band analogue response per kernel bin, recomputed only when that band changes
        // (a dynamic band redesigns every block; a steep cascade is up to 16 sections).
        std::array<std::vector<std::complex<double>>, 6> bandResponse;
        std::array<KernelBand, 6> cachedBands {};
        std::array<bool, 6> bandResponseValid {};
        void invalidateBandResponses() noexcept { bandResponseValid.fill (false); }

        int historyPosition = 0, blockPosition = 0;
        bool pendingCrossfade = false;
    };
}
