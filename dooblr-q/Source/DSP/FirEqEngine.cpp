#include "FirEqEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dooblrq::dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        int log2Of (int value) noexcept
        {
            int order = 0;
            while ((1 << order) < value)
                ++order;
            return order;
        }
    }

    int FirEqEngine::rateFactor (double rate) noexcept
    {
        return rate <= 50000.0 ? 1 : (rate <= 100000.0 ? 2 : 4);
    }

    int FirEqEngine::kernelLength (Mode m, double rate) noexcept
    {
        return (m == Mode::linear ? 8192 : 4096) * rateFactor (rate);
    }

    int FirEqEngine::kernelDelay (Mode m, double rate) noexcept
    {
        return m == Mode::linear ? kernelLength (m, rate) / 2 : 768 * rateFactor (rate);
    }

    FirEqEngine::FirEqEngine() = default;
    FirEqEngine::~FirEqEngine() = default;

    void FirEqEngine::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        const auto longest = kernelLength (Mode::linear, sampleRate);
        maxPartitions = longest / partitionSize;

        partitionFft = std::make_unique<juce::dsp::FFT> (log2Of (fftSize));
        naturalFft   = std::make_unique<juce::dsp::FFT> (log2Of (kernelLength (Mode::natural, sampleRate)));
        linearFft    = std::make_unique<juce::dsp::FFT> (log2Of (longest));

        designBuffer.assign ((size_t) longest * 2, 0.0f);
        for (auto& r : bandResponse)
            r.assign ((size_t) longest / 2 + 1, {});
        invalidateBandResponses();
        accumulator.assign ((size_t) numBins, {});
        fftScratch.assign ((size_t) fftSize * 2, 0.0f);
        crossfadeScratch.assign ((size_t) partitionSize, 0.0f);
        kernelTime.assign ((size_t) longest, 0.0f);
        current.assign ((size_t) (maxPartitions * numBins), {});
        next.assign ((size_t) (maxPartitions * numBins), {});

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const auto c = (size_t) ch;
            history[c].assign ((size_t) (maxPartitions * numBins), {});
            inputWindow[c].assign ((size_t) fftSize, 0.0f);
            inputBlock[c].assign ((size_t) partitionSize, 0.0f);
            outputBlock[c].assign ((size_t) partitionSize, 0.0f);
        }

        setMode (mode);
    }

    void FirEqEngine::setMode (Mode newMode) noexcept
    {
        mode = newMode;
        invalidateBandResponses(); // the bin spacing depends on the kernel length
        numPartitions = std::min (maxPartitions, kernelLength (mode, sampleRate) / partitionSize);

        std::fill (current.begin(), current.end(), Complex {});
        std::fill (kernelTime.begin(), kernelTime.end(), 0.0f);

        reset();
    }

    void FirEqEngine::reset() noexcept
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const auto c = (size_t) ch;
            std::fill (history[c].begin(), history[c].end(), Complex {});
            std::fill (inputWindow[c].begin(), inputWindow[c].end(), 0.0f);
            std::fill (inputBlock[c].begin(), inputBlock[c].end(), 0.0f);
            std::fill (outputBlock[c].begin(), outputBlock[c].end(), 0.0f);
        }

        historyPosition = 0;
        blockPosition = 0;

        if (pendingCrossfade)
        {
            std::swap (current, next);
            pendingCrossfade = false;
        }
    }

    void FirEqEngine::designKernel (const std::array<KernelBand, 6>& bands) noexcept
    {
        const auto n = kernelLength (mode, sampleRate);
        const auto delay = kernelDelay (mode, sampleRate);
        auto& fft = mode == Mode::linear ? *linearFft : *naturalFft;
        const auto sameBand = [] (const KernelBand& a, const KernelBand& b)
        {
            return a.enabled == b.enabled && a.type == b.type && juce::exactlyEqual (a.frequency, b.frequency)
                   && juce::exactlyEqual (a.q, b.q) && juce::exactlyEqual (a.gainDb, b.gainDb) && a.slope == b.slope;
        };

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto& band = bands[b];

            if (! band.enabled || (bandResponseValid[b] && sameBand (band, cachedBands[b])))
                continue;

            const auto prototype = makeBandPrototype (band.type, band.slope, band.q, band.gainDb);
            auto& response = bandResponse[b];

            analogResponse (prototype, band.frequency, sampleRate, n, n / 2 + 1, response.data());

            cachedBands[b] = band;
            bandResponseValid[b] = true;
        }

        std::fill (designBuffer.begin(), designBuffer.begin() + 2 * n, 0.0f);

        for (int bin = 0; bin <= n / 2; ++bin)
        {
            std::complex<double> h (1.0, 0.0);

            for (size_t b = 0; b < bands.size(); ++b)
                if (bands[b].enabled)
                    h *= bandResponse[b][(size_t) bin];

            if (mode == Mode::linear)
                h = std::abs (h);                 // zero phase
            else if (bin == 0 || bin == n / 2)
                h = h.real();                     // DC and Nyquist bins of a real signal are real

            designBuffer[(size_t) (2 * bin)]     = (float) h.real();
            designBuffer[(size_t) (2 * bin + 1)] = (float) h.imag();
        }

        fft.performRealOnlyInverseTransform (designBuffer.data());

        // Rotate the (circular) impulse response so time zero sits at the delay, then window.
        const auto fadeOut = n / 4;
        auto& kernel = kernelTime;

        for (int i = 0; i < n; ++i)
        {
            double w = 1.0;

            if (mode == Mode::linear)
                w = 0.5 - 0.5 * std::cos (2.0 * pi * i / n);
            else if (i < delay)
                w = 0.5 - 0.5 * std::cos (pi * i / delay);
            else if (i >= n - fadeOut)
                w = 0.5 + 0.5 * std::cos (pi * (i - (n - fadeOut)) / fadeOut);

            kernel[(size_t) i] = (float) w * designBuffer[(size_t) ((i - delay + n) % n)];
        }
    }

    void FirEqEngine::partitionKernel (std::vector<Complex>& destination) noexcept
    {
        const auto& kernel = kernelTime;

        for (int p = 0; p < numPartitions; ++p)
        {
            std::fill (fftScratch.begin(), fftScratch.end(), 0.0f);
            std::copy (kernel.begin() + p * partitionSize, kernel.begin() + (p + 1) * partitionSize, fftScratch.begin());
            partitionFft->performRealOnlyForwardTransform (fftScratch.data(), true);

            auto* bins = destination.data() + p * numBins;
            for (int j = 0; j < numBins; ++j)
                bins[j] = { fftScratch[(size_t) (2 * j)], fftScratch[(size_t) (2 * j + 1)] };
        }
    }

    void FirEqEngine::setTarget (const std::array<KernelBand, 6>& bands, bool immediate) noexcept
    {
        designKernel (bands);
        partitionKernel (immediate ? current : next);
        pendingCrossfade = ! immediate;
    }

    void FirEqEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = channels[ch][i];
                inputBlock[(size_t) ch][(size_t) blockPosition] = sample;
                sample = outputBlock[(size_t) ch][(size_t) blockPosition];
            }

            if (++blockPosition == partitionSize)
            {
                processPartition (numChannels);
                blockPosition = 0;
            }
        }
    }

    void FirEqEngine::accumulate (const std::vector<Complex>& kernel, int channel, std::vector<float>& out) noexcept
    {
        std::fill (accumulator.begin(), accumulator.end(), Complex {});
        const auto& spectra = history[(size_t) channel];

        for (int p = 0; p < numPartitions; ++p)
        {
            const auto slot = (historyPosition - p + numPartitions) % numPartitions;
            const auto* x = spectra.data() + slot * numBins;
            const auto* h = kernel.data() + p * numBins;

            for (int j = 0; j < numBins; ++j)
                accumulator[(size_t) j] += x[j] * h[j];
        }

        for (int j = 0; j < numBins; ++j)
        {
            fftScratch[(size_t) (2 * j)]     = accumulator[(size_t) j].real();
            fftScratch[(size_t) (2 * j + 1)] = accumulator[(size_t) j].imag();
        }

        std::fill (fftScratch.begin() + 2 * numBins, fftScratch.end(), 0.0f);
        partitionFft->performRealOnlyInverseTransform (fftScratch.data());

        // Overlap-save: the second half is free of circular wrap-around.
        std::copy (fftScratch.begin() + partitionSize, fftScratch.begin() + fftSize, out.begin());
    }

    void FirEqEngine::processPartition (int numChannels) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = (size_t) ch;
            auto& window = inputWindow[c];
            std::copy (window.begin() + partitionSize, window.end(), window.begin());
            std::copy (inputBlock[c].begin(), inputBlock[c].end(), window.begin() + partitionSize);

            std::fill (fftScratch.begin(), fftScratch.end(), 0.0f);
            std::copy (window.begin(), window.end(), fftScratch.begin());
            partitionFft->performRealOnlyForwardTransform (fftScratch.data(), true);

            auto* slot = history[c].data() + historyPosition * numBins;
            for (int j = 0; j < numBins; ++j)
                slot[j] = { fftScratch[(size_t) (2 * j)], fftScratch[(size_t) (2 * j + 1)] };

            accumulate (current, ch, outputBlock[c]);

            if (pendingCrossfade)
            {
                accumulate (next, ch, crossfadeScratch);

                for (int i = 0; i < partitionSize; ++i)
                {
                    const auto t = ((float) i + 0.5f) / (float) partitionSize;
                    auto& y = outputBlock[c][(size_t) i];
                    y += t * (crossfadeScratch[(size_t) i] - y);
                }
            }
        }

        if (pendingCrossfade)
        {
            std::swap (current, next);
            pendingCrossfade = false;
        }

        historyPosition = (historyPosition + 1) % numPartitions;
    }
}
