#include "NoiseReducer.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr float tinyPower = 1.0e-20f;
        constexpr float fadeSeconds = 0.005f;
        constexpr float openMs = 5.0f, closeMs = 60.0f;
        constexpr float autoSmoothSeconds = 0.05f;   // power smoothing before the minimum search
        constexpr float autoWindowSeconds = 1.5f;    // minimum over the last ~1.5 s
        constexpr float autoBias = 2.0f;             // the minimum of a smoothed PSD sits below its mean

        int rateFactorOrder (double sampleRate) noexcept
        {
            return sampleRate >= 176000.0 ? 2 : (sampleRate >= 88000.0 ? 1 : 0);
        }
    }

    int NoiseReducer::fftSizeFor (ProcessMode mode, double rate) noexcept
    {
        const auto base = mode == ProcessMode::tracking ? 8 : (mode == ProcessMode::hq ? 11 : 10);
        return 1 << (base + rateFactorOrder (rate));
    }

    void NoiseReducer::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        baseOrder = 8 + rateFactorOrder (sampleRate);
        maxOrder = baseOrder + 3;
        for (int i = 0; i < 4; ++i)
            ffts[(size_t) i] = std::make_unique<juce::dsp::FFT> (baseOrder + i);

        const auto maxSize = (size_t) 1 << maxOrder;
        window.assign (maxSize, 0.0f);
        fftData.assign (maxSize * 2, 0.0f);
        power.assign (maxSize / 2 + 1, 0.0f);
        gain.assign (maxSize / 2 + 1, 1.0f);
        smoothGain.assign (maxSize / 2 + 1, 1.0f);
        noise.assign (maxSize / 2 + 1, 0.0f);
        frameScratch.assign (maxSize / 2 + 1, 0.0f);
        smoothPower.assign (maxSize / 2 + 1, 0.0f);
        subMin.assign (maxSize / 2 + 1, 0.0f);
        minRing.assign ((size_t) autoSubWindows * (maxSize / 2 + 1), 0.0f);
        learnSum.assign ((size_t) profileSize, 0.0f);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            inFrame[(size_t) ch].assign (maxSize, 0.0f);
            outAccum[(size_t) ch].assign (maxSize, 0.0f);
            inFifo[(size_t) ch].assign (maxSize, 0.0f);
            outFifo[(size_t) ch].assign (maxSize, 0.0f);
            spectrum[(size_t) ch].assign (maxSize * 2, 0.0f);
        }

        fadeStep = 1.0f / std::max (1.0f, fadeSeconds * (float) sampleRate);
        running = false;
        order = 0;
        phase = Phase::steady;
        fade = 1.0f;
        primed = false;
        reset();
    }

    void NoiseReducer::reset()
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            std::fill (inFrame[(size_t) ch].begin(), inFrame[(size_t) ch].end(), 0.0f);
            std::fill (outAccum[(size_t) ch].begin(), outAccum[(size_t) ch].end(), 0.0f);
            std::fill (inFifo[(size_t) ch].begin(), inFifo[(size_t) ch].end(), 0.0f);
            std::fill (outFifo[(size_t) ch].begin(), outFifo[(size_t) ch].end(), 0.0f);
        }
        std::fill (smoothGain.begin(), smoothGain.end(), 1.0f);
        std::fill (noise.begin(), noise.end(), 0.0f);
        noiseSeeded = false;
        pos = 0;
        reductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void NoiseReducer::configure (int fftOrder) noexcept
    {
        order = std::clamp (fftOrder, baseOrder, maxOrder);
        size = 1 << order;
        hop = size / 4;
        bins = size / 2 + 1;

        // Periodic sqrt-Hann: analysis x synthesis = Hann, which sums to 2 at 75 % overlap.
        windowPower = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            const auto hann = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
            window[(size_t) i] = std::sqrt (hann);
            windowPower += hann;
        }

        reset();
    }

    float NoiseReducer::floorAt (int bin) const noexcept
    {
        // Learned grid -> this FFT's bin (linear interpolation in frequency).
        const auto x = (float) bin / (float) (bins - 1) * (float) (profileSize - 1);
        const auto i = std::min ((int) x, profileSize - 2);
        const auto t = x - (float) i;
        const auto a = profile[(size_t) i].load (std::memory_order_relaxed);
        const auto b = profile[(size_t) i + 1].load (std::memory_order_relaxed);
        return a + (b - a) * t;
    }

    void NoiseReducer::copyProfile (std::vector<float>& destination) const
    {
        destination.resize ((size_t) profileSize);
        for (int i = 0; i < profileSize; ++i)
            destination[(size_t) i] = profile[(size_t) i].load (std::memory_order_relaxed);
    }

    void NoiseReducer::setProfile (const std::vector<float>& source)
    {
        if ((int) source.size() != profileSize)
            return;
        for (int i = 0; i < profileSize; ++i)
            pendingProfile[(size_t) i].store (std::isfinite (source[(size_t) i]) ? std::max (0.0f, source[(size_t) i]) : 0.0f,
                                              std::memory_order_relaxed);
        pendingValid.store (true, std::memory_order_release);
    }

    void NoiseReducer::process (juce::AudioBuffer<float>& buffer, int numSamples, const NoiseReducerSettings& s, ProcessMode mode) noexcept
    {
        if (pendingValid.exchange (false, std::memory_order_acquire))
        {
            for (int i = 0; i < profileSize; ++i)
                profile[(size_t) i].store (pendingProfile[(size_t) i].load (std::memory_order_relaxed), std::memory_order_relaxed);
            profileValid.store (true, std::memory_order_release);
            noiseSeeded = false;
        }

        const auto targetOrder = s.enabled ? juce::roundToInt (std::log2 ((double) fftSizeFor (mode, sampleRate))) : 0;

        if (! primed)
        {
            // First block: start in the right state with no fade.
            primed = true;
            running = s.enabled;
            if (running)
                configure (targetOrder);
            phase = Phase::steady;
            fade = 1.0f;
        }

        const auto wantRunning = s.enabled;
        if (phase == Phase::steady && (wantRunning != running || (running && targetOrder != order)))
            phase = Phase::fadingOut;

        if (! running && phase == Phase::steady)
            return; // off: the buffer is not touched (bit-identical)

        const auto numChannels = std::min (buffer.getNumChannels(), maxChannels);

        for (int n = 0; n < numSamples; ++n)
        {
            if (running)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& sample = buffer.getWritePointer (ch)[n];
                    inFifo[(size_t) ch][(size_t) pos] = sample;
                    sample = outFifo[(size_t) ch][(size_t) pos];
                }
                for (int ch = numChannels; ch < maxChannels; ++ch)
                    inFifo[(size_t) ch][(size_t) pos] = 0.0f;

                if (++pos >= hop)
                {
                    pos = 0;
                    processFrame (s);
                }
            }

            if (phase != Phase::steady)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.getWritePointer (ch)[n] *= fade;

                if (phase == Phase::fadingOut)
                {
                    fade -= fadeStep;
                    if (fade <= 0.0f)
                    {
                        fade = 0.0f;
                        running = wantRunning;
                        if (running)
                            configure (targetOrder);
                        phase = Phase::fadingIn;
                    }
                }
                else
                {
                    fade += fadeStep;
                    if (fade >= 1.0f)
                    {
                        fade = 1.0f;
                        phase = Phase::steady;
                        if (! running)
                            return;
                    }
                }
            }
        }
    }

    void NoiseReducer::processFrame (const NoiseReducerSettings& s) noexcept
    {
        auto& fft = *ffts[(size_t) (order - baseOrder)];
        const auto hopSeconds = (float) hop / (float) sampleRate;

        // 1. Analysis: shift each channel's frame by a hop, window, FFT; power = max over channels.
        std::fill (power.begin(), power.begin() + bins, 0.0f);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            auto& frame = inFrame[(size_t) ch];
            std::copy (frame.begin() + hop, frame.begin() + size, frame.begin());
            std::copy (inFifo[(size_t) ch].begin(), inFifo[(size_t) ch].begin() + hop, frame.begin() + (size - hop));

            auto& spec = spectrum[(size_t) ch];
            for (int i = 0; i < size; ++i)
                spec[(size_t) i] = frame[(size_t) i] * window[(size_t) i];
            std::fill (spec.begin() + size, spec.begin() + 2 * size, 0.0f);
            fft.performRealOnlyForwardTransform (spec.data(), false);

            for (int k = 0; k < bins; ++k)
            {
                const auto re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
                power[(size_t) k] = std::max (power[(size_t) k], (re * re + im * im) / windowPower);
            }
        }

        // 2. Learn: average the PSD on the fixed grid while Learn is on; store it when Learn ends.
        if (s.learn)
        {
            if (! wasLearning)
            {
                std::fill (learnSum.begin(), learnSum.end(), 0.0f);
                learnFrames = 0;
            }
            for (int g = 0; g < profileSize; ++g)
            {
                const auto x = (float) g / (float) (profileSize - 1) * (float) (bins - 1);
                const auto i = std::min ((int) x, bins - 2);
                const auto t = x - (float) i;
                learnSum[(size_t) g] += power[(size_t) i] + (power[(size_t) i + 1] - power[(size_t) i]) * t;
            }
            ++learnFrames;
            learnedSeconds.store ((float) learnFrames * hopSeconds, std::memory_order_relaxed);
        }
        else if (wasLearning && learnFrames > 0)
        {
            for (int g = 0; g < profileSize; ++g)
                profile[(size_t) g].store (learnSum[(size_t) g] / (float) learnFrames, std::memory_order_relaxed);
            profileValid.store (true, std::memory_order_release);
            noiseSeeded = false;
        }
        wasLearning = s.learn;

        // 3. Noise floor for this frame.
        const auto learned = s.mode == 0 && hasProfile();
        if (s.mode != lastMode)
        {
            noiseSeeded = false;
            lastMode = s.mode;
        }

        if (learned)
        {
            for (int k = 0; k < bins; ++k)
                noise[(size_t) k] = floorAt (k);
        }
        else
        {
            // Minimum statistics: smooth each bin's power, keep the minimum over ~1.5 s in
            // autoSubWindows sub-windows, noise = that minimum x a bias. Follows a changing
            // noise floor within ~1.5 s either way; a vocal's gaps and syllable changes keep its
            // own partials above the minimum.
            const auto smooth = std::exp (-hopSeconds / autoSmoothSeconds);
            // Longer hops average fewer frames in the 50 ms smoothing, so the minimum sits further
            // below the mean: scale the bias with the hop (2.0 at the Tracking hop, ~2.8 Mix, ~3.4 HQ).
            const auto bias = autoBias * std::pow (std::max (1.0f, hopSeconds / (64.0f / 48000.0f)), 0.25f);
            const auto subFrames = std::max (1, juce::roundToInt (autoWindowSeconds / (float) autoSubWindows / hopSeconds));
            if (! noiseSeeded)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const auto seed = hasProfile() ? floorAt (k) / autoBias : power[(size_t) k];
                    smoothPower[(size_t) k] = seed;
                    subMin[(size_t) k] = seed;
                    for (int w = 0; w < autoSubWindows; ++w)
                        minRing[(size_t) (w * bins + k)] = seed;
                }
                subCount = 0;
                ringPos = 0;
                noiseSeeded = true;
            }
            for (int k = 0; k < bins; ++k)
            {
                auto& sp = smoothPower[(size_t) k];
                sp = power[(size_t) k] + smooth * (sp - power[(size_t) k]);
                subMin[(size_t) k] = std::min (subMin[(size_t) k], sp);
            }
            if (++subCount >= subFrames)
            {
                subCount = 0;
                std::copy (subMin.begin(), subMin.begin() + bins, minRing.begin() + ringPos * bins);
                ringPos = (ringPos + 1) % autoSubWindows;
                std::copy (smoothPower.begin(), smoothPower.begin() + bins, subMin.begin());
            }
            for (int k = 0; k < bins; ++k)
            {
                auto m = subMin[(size_t) k];
                for (int w = 0; w < autoSubWindows; ++w)
                    m = std::min (m, minRing[(size_t) (w * bins + k)]);
                noise[(size_t) k] = m * bias;
            }
        }

        // 4. Gains: Wiener-style subtraction against the floor, then smoothed over frequency and time.
        const auto sens = std::clamp (s.sensitivity, 0.0f, 100.0f) / 100.0f;
        const auto offset = std::pow (10.0f, (12.0f - 18.0f * sens) / 10.0f);
        const auto minGain = std::pow (10.0f, -std::clamp (s.amountDb, 0.0f, maxAmountDb) / 20.0f);
        for (int k = 0; k < bins; ++k)
        {
            const auto p = std::max (power[(size_t) k], tinyPower);
            frameScratch[(size_t) k] = std::clamp (1.0f - offset * noise[(size_t) k] / p, minGain, 1.0f);
        }
        for (int k = 0; k < bins; ++k)
        {
            const auto lo = frameScratch[(size_t) std::max (0, k - 1)], hi = frameScratch[(size_t) std::min (bins - 1, k + 1)];
            gain[(size_t) k] = 0.25f * lo + 0.5f * frameScratch[(size_t) k] + 0.25f * hi;
        }
        const auto openCoeff = 1.0f - std::exp (-hopSeconds / (openMs * 0.001f));
        const auto closeCoeff = 1.0f - std::exp (-hopSeconds / (closeMs * 0.001f));
        double sumIn = 0.0, sumOut = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            auto& g = smoothGain[(size_t) k];
            const auto target = gain[(size_t) k];
            g += (target > g ? openCoeff : closeCoeff) * (target - g);
            sumIn += power[(size_t) k];
            sumOut += power[(size_t) k] * g * g;
        }
        reductionDb.store (sumIn > 1.0e-12 ? (float) (10.0 * std::log10 (std::max (sumOut, 1.0e-30) / sumIn)) : 0.0f,
                           std::memory_order_relaxed);

        // 5. Synthesis: apply, inverse FFT, window, overlap-add; emit the next hop.
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            auto& spec = spectrum[(size_t) ch];
            for (int k = 0; k < size; ++k)
            {
                const auto g = smoothGain[(size_t) (k < bins ? k : size - k)]; // mirrored negative frequencies
                spec[(size_t) (2 * k)] *= g;
                spec[(size_t) (2 * k + 1)] *= g;
            }
            fft.performRealOnlyInverseTransform (spec.data());

            auto& acc = outAccum[(size_t) ch];
            for (int i = 0; i < size; ++i)
                acc[(size_t) i] += spec[(size_t) i] * window[(size_t) i] * 0.5f;

            std::copy (acc.begin(), acc.begin() + hop, outFifo[(size_t) ch].begin());
            std::copy (acc.begin() + hop, acc.begin() + size, acc.begin());
            std::fill (acc.begin() + (size - hop), acc.begin() + size, 0.0f);
        }
    }
}
