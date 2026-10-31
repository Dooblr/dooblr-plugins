#include "ResonanceEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrsoothe::dsp
{
    namespace
    {
        constexpr float powerFloor = 1.0e-14f; // -140 dB
        constexpr float hardCeilingDb = -24.0f;
        constexpr float gateLowDb = -100.0f, gateRangeDb = 20.0f;

        int orderOf (int size) noexcept
        {
            int order = 0;
            while ((1 << order) < size)
                ++order;
            return order;
        }

        float measureInverseScale (juce::dsp::FFT& fft)
        {
            const auto n = fft.getSize();
            std::vector<float> data ((size_t) n * 2, 0.0f);
            data[(size_t) n / 2] = 1.0f;
            fft.performRealOnlyForwardTransform (data.data(), true);
            fft.performRealOnlyInverseTransform (data.data());
            const auto v = data[(size_t) n / 2];
            return std::abs (v) > 1.0e-9f ? 1.0f / v : 1.0f;
        }

        inline float dbOf (float power) noexcept { return 10.0f * std::log10 (power + powerFloor); }
    }

    float DisplayFrame::pointFrequency (int point) noexcept
    {
        const auto t = (float) point / (float) (numPoints - 1);
        return minFreq * std::pow (maxFreq / minFreq, t);
    }

    ResonanceEngine::ResonanceEngine() = default;
    ResonanceEngine::~ResonanceEngine() = default;

    int ResonanceEngine::fftSizeFor (double rate, int q) noexcept
    {
        const int s = rate <= 50000.0 ? 1 : (rate <= 100000.0 ? 2 : 4);
        return (q == standard ? 1024 : 2048) * s;
    }

    void ResonanceEngine::prepare (double newSampleRate, int newQuality)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        scale = sampleRate <= 50000.0 ? 1 : (sampleRate <= 100000.0 ? 2 : 4);
        maxFftSize = 2048 * scale;
        const auto maxBins = (size_t) maxFftSize / 2 + 1;

        fftSmall = std::make_unique<juce::dsp::FFT> (orderOf (1024 * scale));
        fftLarge = std::make_unique<juce::dsp::FFT> (orderOf (2048 * scale));
        inverseScaleSmall = measureInverseScale (*fftSmall);
        inverseScaleLarge = measureInverseScale (*fftLarge);

        window.assign ((size_t) maxFftSize, 0.0f);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            inRing[(size_t) ch].assign ((size_t) maxFftSize, 0.0f);
            outAcc[(size_t) ch].assign ((size_t) maxFftSize, 0.0f);
            detRing[(size_t) ch].assign ((size_t) maxFftSize, 0.0f);
            spectrum[(size_t) ch].assign ((size_t) maxFftSize * 2, 0.0f);
            power[(size_t) ch].assign (maxBins, 0.0f);
            detPower[(size_t) ch].assign (maxBins, 0.0f);
            own[(size_t) ch].assign (maxBins, 0.0f);
            smoothed[(size_t) ch].assign (maxBins, 0.0f);
        }
        detSpectrum.assign ((size_t) maxFftSize * 2, 0.0f);
        for (auto* v : { &linkPower, &linked, &temp, &amount, &attackCoeff, &releaseCoeff, &region, &levelDb })
            v->assign (maxBins, 0.0f);
        prefixPower.assign (maxBins + 1, 0.0);

        setQuality (newQuality);
    }

    void ResonanceEngine::setQuality (int newQuality) noexcept
    {
        quality = juce::jlimit (0, 2, newQuality);
        fftSize = fftSizeFor (sampleRate, quality);
        hop = quality == ultra ? fftSize / 8 : fftSize / 4;
        mask = fftSize - 1;
        numBins = fftSize / 2 + 1;
        fft = quality == standard ? fftSmall.get() : fftLarge.get();
        inverseScale = quality == standard ? inverseScaleSmall : inverseScaleLarge;

        double windowSum = 0.0;
        for (int n = 0; n < fftSize; ++n)
        {
            window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);
            windowSum += window[(size_t) n];
        }
        // Hann analysis x Hann synthesis at a hop of N/4 or N/8 sums to (N / hop) * 3/8.
        olaNorm = 1.0f / ((float) fftSize / (float) hop * 0.375f);
        // Bin magnitude of a full-scale sine reads 1 (0 dB).
        specNorm = (float) (2.0 / windowSum);

        // Display grid: each point covers the bins between its neighbours' midpoints.
        const auto binHz = sampleRate / fftSize;
        for (int p = 0; p < DisplayFrame::numPoints; ++p)
        {
            const auto f = DisplayFrame::pointFrequency (p);
            const auto step = std::pow (DisplayFrame::maxFreq / DisplayFrame::minFreq, 0.5f / (DisplayFrame::numPoints - 1));
            const auto lo = (int) std::floor (f / step / binHz), hi = (int) std::ceil (f * step / binHz);
            const auto nearest = (int) std::lround (f / binHz);
            pointLo[(size_t) p] = juce::jlimit (1, numBins - 1, std::min (lo, nearest));
            pointHi[(size_t) p] = juce::jlimit (1, numBins - 1, std::max (hi, nearest));
        }

        tablesValid = false;
        reset();
    }

    void ResonanceEngine::reset() noexcept
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            std::fill (inRing[(size_t) ch].begin(), inRing[(size_t) ch].end(), 0.0f);
            std::fill (outAcc[(size_t) ch].begin(), outAcc[(size_t) ch].end(), 0.0f);
            std::fill (detRing[(size_t) ch].begin(), detRing[(size_t) ch].end(), 0.0f);
            std::fill (smoothed[(size_t) ch].begin(), smoothed[(size_t) ch].end(), 0.0f);
        }
        writePos = 0;
        hopCounter = 0;
    }

    void ResonanceEngine::updateTables() noexcept
    {
        const auto binHz = (float) (sampleRate / fftSize);
        const bool curveChanged = ! tablesValid || settings.bands != tableBands;
        const bool timeChanged = ! tablesValid || settings.attackMs != tableAttack || settings.releaseMs != tableRelease;
        const bool listenChanged = ! tablesValid || settings.listenBand != tableListen || curveChanged;

        if (curveChanged)
        {
            for (int k = 0; k < numBins; ++k)
                amount[(size_t) k] = k == 0 ? 0.0f : curveAmount (settings.bands, (float) k * binHz);
            tableBands = settings.bands;
        }

        if (timeChanged)
        {
            const auto hopSeconds = (float) hop / (float) sampleRate;
            for (int k = 0; k < numBins; ++k)
            {
                // Lows move more slowly than highs (a low partial needs longer to be resolved).
                const auto f = std::max (20.0f, (float) k * binHz);
                const auto tilt = juce::jlimit (0.6f, 1.8f, std::pow (1000.0f / f, 0.25f));
                const auto attack = std::max (1.0e-4f, settings.attackMs * 0.001f * tilt);
                const auto release = std::max (1.0e-4f, settings.releaseMs * 0.001f * tilt);
                attackCoeff[(size_t) k] = 1.0f - std::exp (-hopSeconds / attack);
                releaseCoeff[(size_t) k] = 1.0f - std::exp (-hopSeconds / release);
            }
            tableAttack = settings.attackMs;
            tableRelease = settings.releaseMs;
        }

        if (listenChanged)
        {
            const auto band = settings.listenBand;
            for (int k = 0; k < numBins; ++k)
                region[(size_t) k] = band >= 0 && band < params::numBands ? bandRegion (settings.bands[(size_t) band], (float) k * binHz) : 0.0f;
            tableListen = band;
        }

        tablesValid = true;
    }

    void ResonanceEngine::process (const float* const* in, const float* const* detector, int numDetectorChannels,
                                   float* const* wet, float* const* dry, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jlimit (1, maxChannels, numChannels);
        const bool useDetector = detector != nullptr && numDetectorChannels > 0;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& ring = inRing[(size_t) ch];
                auto& acc = outAcc[(size_t) ch];
                dry[ch][i] = ring[(size_t) writePos];
                ring[(size_t) writePos] = in[ch][i];
                wet[ch][i] = acc[(size_t) writePos];
                acc[(size_t) writePos] = 0.0f;

                if (useDetector)
                    detRing[(size_t) ch][(size_t) writePos] = detector[std::min (ch, numDetectorChannels - 1)][i];
            }

            writePos = (writePos + 1) & mask;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                processFrame (numChannels, useDetector);
            }
        }
    }

    void ResonanceEngine::analyse (const std::vector<float>& ring, std::vector<float>& spec, float* pow) noexcept
    {
        auto* x = spec.data();
        for (int n = 0; n < fftSize; ++n)
            x[n] = ring[(size_t) ((writePos + n) & mask)] * window[(size_t) n];
        std::fill (x + fftSize, x + 2 * fftSize, 0.0f);
        fft->performRealOnlyForwardTransform (x, true);

        const auto norm2 = specNorm * specNorm;
        for (int k = 0; k < numBins; ++k)
        {
            const auto re = x[2 * k], im = x[2 * k + 1];
            pow[k] = (re * re + im * im) * norm2;
        }
    }

    void ResonanceEngine::detect (const float* p, float* reduction) noexcept
    {
        // Prefix sums of power for the neighbourhood and envelope means.
        prefixPower[0] = 0.0;
        for (int k = 0; k < numBins; ++k)
            prefixPower[(size_t) k + 1] = prefixPower[(size_t) k] + (double) p[k];

        const auto sum = [this] (int lo, int hi) { return prefixPower[(size_t) hi + 1] - prefixPower[(size_t) lo]; };

        // Sharpness 0 .. 10: envelope window 3 octaves .. 1/4 octave wide.
        const auto widthOctaves = 3.0f * std::pow (1.0f / 12.0f, juce::jlimit (0.0f, 10.0f, settings.sharpness) / 10.0f);
        const auto ratio = std::exp2 (widthOctaves * 0.5f);
        const auto threshold = 1.0f + 1.2f * juce::jlimit (0.0f, 10.0f, settings.selectivity);
        const auto depth = juce::jlimit (0.0f, 1.0f, settings.depth);
        const auto last = numBins - 1;
        constexpr int peakHalf = 1, excludeHalf = 2, minHalf = 6;

        reduction[0] = 0.0f;

        for (int k = 1; k < numBins; ++k)
        {
            if (amount[(size_t) k] <= 0.0f || depth <= 0.0f)
            {
                reduction[k] = 0.0f;
                continue;
            }

            const auto pLo = std::max (1, k - peakHalf), pHi = std::min (last, k + peakHalf);
            const auto levelPower = sum (pLo, pHi) / (pHi - pLo + 1);
            const auto level = dbOf ((float) levelPower);

            auto lo = std::max (1, (int) ((float) k / ratio)), hi = std::min (last, (int) std::ceil ((float) k * ratio));
            lo = std::max (1, std::min (lo, k - minHalf));
            hi = std::min (last, std::max (hi, k + minHalf));
            const auto eLo = std::max (lo, k - excludeHalf), eHi = std::min (hi, k + excludeHalf);
            const auto count = (hi - lo + 1) - (eHi - eLo + 1);
            const auto envelopePower = count > 0 ? (sum (lo, hi) - sum (eLo, eHi)) / count : levelPower;
            const auto envelope = dbOf ((float) envelopePower);

            auto r = depth * std::max (0.0f, level - envelope - threshold);
            if (settings.hard)
                r += depth * 0.5f * std::max (0.0f, level - hardCeilingDb);

            const auto gate = juce::jlimit (0.0f, 1.0f, (level - gateLowDb) / gateRangeDb);
            reduction[k] = r * amount[(size_t) k] * gate;
        }
    }

    void ResonanceEngine::processFrame (int numChannels, bool useDetector) noexcept
    {
        updateTables();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            analyse (inRing[(size_t) ch], spectrum[(size_t) ch], power[(size_t) ch].data());
            if (useDetector)
                analyse (detRing[(size_t) ch], detSpectrum, detPower[(size_t) ch].data());
            else
                std::copy (power[(size_t) ch].begin(), power[(size_t) ch].begin() + numBins, detPower[(size_t) ch].begin());
        }

        const auto link = numChannels > 1 ? juce::jlimit (0.0f, 1.0f, settings.link) : 0.0f;

        if (link > 0.0f)
        {
            for (int k = 0; k < numBins; ++k)
                linkPower[(size_t) k] = 0.5f * (detPower[0][(size_t) k] + detPower[1][(size_t) k]);
            detect (linkPower.data(), linked.data());
        }

        const auto maxCut = std::max (0.0f, settings.maxCutDb);
        const auto focus = juce::jlimit (-1.0f, 1.0f, settings.focus);
        const bool listening = settings.listenBand >= 0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* r = own[(size_t) ch].data();

            if (link < 1.0f)
                detect (detPower[(size_t) ch].data(), r);
            else
                std::fill (r, r + numBins, 0.0f);

            const auto focusScale = numChannels < 2 ? 1.0f : (ch == 0 ? std::min (1.0f, 1.0f - focus) : std::min (1.0f, 1.0f + focus));

            for (int k = 0; k < numBins; ++k)
                r[k] = ((1.0f - link) * r[k] + link * linked[(size_t) k]) * focusScale;

            // Smooth across frequency (two 1-2-1 passes) so a cut spans the window's main lobe.
            for (int pass = 0; pass < 2; ++pass)
            {
                temp[0] = r[0];
                temp[(size_t) numBins - 1] = r[numBins - 1];
                for (int k = 1; k < numBins - 1; ++k)
                    temp[(size_t) k] = 0.25f * r[k - 1] + 0.5f * r[k] + 0.25f * r[k + 1];
                std::copy (temp.begin(), temp.begin() + numBins, r);
            }

            auto* s = smoothed[(size_t) ch].data();
            auto* x = spectrum[(size_t) ch].data();

            for (int k = 0; k < numBins; ++k)
            {
                const auto target = std::min (maxCut, r[k]);
                const auto coeff = target > s[k] ? attackCoeff[(size_t) k] : releaseCoeff[(size_t) k];
                s[k] += (target - s[k]) * coeff;
                if (s[k] < 1.0e-6f)
                    s[k] = 0.0f;

                const auto gain = std::exp (-s[k] * 0.11512925f); // 10^(-s / 20)
                const auto applied = listening ? (1.0f - gain) * region[(size_t) k] : gain;
                x[2 * k] *= applied;
                x[2 * k + 1] *= applied;
                r[k] = gain; // keep for the display
            }

            fft->performRealOnlyInverseTransform (x);

            auto& acc = outAcc[(size_t) ch];
            const auto outScale = olaNorm * inverseScale;
            for (int n = 0; n < fftSize; ++n)
                acc[(size_t) ((writePos + n) & mask)] += x[n] * window[(size_t) n] * outScale;
        }

        publishDisplay (numChannels);
    }

    void ResonanceEngine::publishDisplay (int numChannels) noexcept
    {
        auto& frame = display.writeSlot();
        const auto channelScale = 1.0f / (float) numChannels;

        for (int p = 0; p < DisplayFrame::numPoints; ++p)
        {
            float in = 0.0f, out = 0.0f, red = 0.0f;
            for (int k = pointLo[(size_t) p]; k <= pointHi[(size_t) p]; ++k)
            {
                float pin = 0.0f, pout = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto pw = power[(size_t) ch][(size_t) k];
                    const auto g = own[(size_t) ch][(size_t) k];
                    pin += pw;
                    pout += pw * g * g;
                    red = std::max (red, smoothed[(size_t) ch][(size_t) k]);
                }
                in = std::max (in, pin * channelScale);
                out = std::max (out, pout * channelScale);
            }
            frame.input[(size_t) p] = dbOf (in);
            frame.output[(size_t) p] = dbOf (out);
            frame.reduction[(size_t) p] = red;
        }

        frame.serial = ++displaySerial;
        display.publish();
    }
}
