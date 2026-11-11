#include "ToneShift.h"

#include <algorithm>
#include <cmath>

namespace dooblrsub::dsp
{
    namespace
    {
        constexpr float pi = 3.14159265f;
    }

    void ToneShift::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * pi * (float) i / (float) fftSize);

        inputFifo.assign ((size_t) fftSize, 0.0f);
        outputFifo.assign ((size_t) fftSize, 0.0f);
        fftWork.assign ((size_t) fftSize * 2, 0.0f);
        cepWork.assign ((size_t) fftSize * 2, 0.0f);
        synthWork.assign ((size_t) fftSize * 2, 0.0f);
        mag.assign ((size_t) (fftSize / 2 + 1), 0.0f);
        env.assign (mag.size(), 0.0f);
        magOut.assign (mag.size(), 0.0f);
        dryDelay.assign ((size_t) latencySamples, 0.0f);
        transDelay.assign ((size_t) latencySamples, 0.0f);
        modalDelay.assign ((size_t) latencySamples, 0.0f);
        reset();
    }

    void ToneShift::reset()
    {
        std::fill (inputFifo.begin(), inputFifo.end(), 0.0f);
        std::fill (outputFifo.begin(), outputFifo.end(), 0.0f);
        std::fill (fftWork.begin(), fftWork.end(), 0.0f);
        std::fill (cepWork.begin(), cepWork.end(), 0.0f);
        std::fill (synthWork.begin(), synthWork.end(), 0.0f);
        std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
        std::fill (transDelay.begin(), transDelay.end(), 0.0f);
        std::fill (modalDelay.begin(), modalDelay.end(), 0.0f);
        fifoIndex = hopCount = delayWrite = 0;
        fastEnv = slowEnv = 0.0f;
        bodyEnv = bodyPrev = modalPhase = modalHz = 0.0f;
        zcCount = 0;
    }

    void ToneShift::splitTransient (float x, float& transient, float& body) noexcept
    {
        const auto ax = std::abs (x);
        const auto atkFast = 1.0f - std::exp (-1.0f / (0.0008f * (float) sampleRate));
        const auto relFast = 1.0f - std::exp (-1.0f / (0.022f * (float) sampleRate));
        const auto atkSlow = 1.0f - std::exp (-1.0f / (0.014f * (float) sampleRate));
        const auto relSlow = 1.0f - std::exp (-1.0f / (0.090f * (float) sampleRate));

        if (ax > fastEnv) fastEnv += atkFast * (ax - fastEnv);
        else              fastEnv += relFast * (ax - fastEnv);
        if (ax > slowEnv) slowEnv += atkSlow * (ax - slowEnv);
        else              slowEnv += relSlow * (ax - slowEnv);

        const auto amount = juce::jlimit (0.0f, 1.0f, settings.transientPreserve);
        auto t = (fastEnv - 0.85f * slowEnv) / (slowEnv + 1.0e-4f);
        t = juce::jlimit (0.0f, 1.0f, t);
        t *= t;
        transient = x * t * amount;
        body = x - transient;
    }

    void ToneShift::cepstralEnvelope (const float* magIn, float* envOut) noexcept
    {
        const auto bins = fftSize / 2 + 1;
        std::fill (cepWork.begin(), cepWork.end(), 0.0f);
        for (int i = 0; i < bins; ++i)
            cepWork[(size_t) i] = std::log (std::max (1.0e-8f, magIn[i]));
        for (int i = 1; i < fftSize / 2; ++i)
            cepWork[(size_t) (fftSize - i)] = cepWork[(size_t) i];

        fft->performRealOnlyInverseTransform (cepWork.data());

        const auto lift = 48;
        for (int i = lift + 1; i < fftSize - lift; ++i)
            cepWork[(size_t) i] = 0.0f;

        std::fill (cepWork.begin() + fftSize, cepWork.end(), 0.0f);
        fft->performRealOnlyForwardTransform (cepWork.data(), true);

        const auto scale = 1.0f / (float) fftSize;
        for (int i = 0; i < bins; ++i)
            envOut[i] = std::exp (cepWork[(size_t) (2 * i)] * scale);
    }

    void ToneShift::warpSpectrum (const float* magIn, const float* envIn, float* out, float ratio) const noexcept
    {
        const auto bins = fftSize / 2 + 1;
        std::fill (out, out + bins, 0.0f);
        out[0] = magIn[0];
        const auto r = juce::jlimit (0.25f, 4.0f, ratio);

        // Scatter each source bin onto dest = src * ratio so modal peaks move by that
        // factor whether we shift up or down. Residual * envelope (same warp) is the
        // source-filter path; for a single resonance it equals mag(src).
        for (int s = 1; s < bins - 1; ++s)
        {
            const auto envS = std::max (1.0e-8f, envIn[s]);
            const auto residual = magIn[s] / envS;
            const auto magS = residual * envS;
            const auto dest = (float) s * r;
            const auto i = (int) std::floor (dest);
            const auto f = dest - (float) i;
            if (i >= 1 && i < bins - 1)
            {
                out[i] += magS * (1.0f - f);
                if (i + 1 < bins - 1)
                    out[i + 1] += magS * f;
            }
        }
        out[bins - 1] = magIn[bins - 1];
    }

    void ToneShift::processFrame()
    {
        const auto bins = fftSize / 2 + 1;
        std::fill (fftWork.begin(), fftWork.end(), 0.0f);

        for (int i = 0; i < fftSize; ++i)
        {
            const auto idx = (fifoIndex + i) % fftSize;
            fftWork[(size_t) i] = inputFifo[(size_t) idx] * window[(size_t) i];
        }

        fft->performRealOnlyForwardTransform (fftWork.data(), true);

        for (int k = 0; k < bins; ++k)
        {
            const auto re = fftWork[(size_t) (2 * k)];
            const auto im = fftWork[(size_t) (2 * k + 1)];
            mag[(size_t) k] = std::sqrt (re * re + im * im);
        }

        cepstralEnvelope (mag.data(), env.data());

        const auto ratio = std::pow (2.0f, settings.cents / 1200.0f);
        warpSpectrum (mag.data(), env.data(), magOut.data(), ratio);

        std::fill (synthWork.begin(), synthWork.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            const auto src = juce::jlimit (0, bins - 1,
                                           (int) std::lround ((float) k / juce::jmax (0.25f, ratio)));
            const auto re = fftWork[(size_t) (2 * src)];
            const auto im = fftWork[(size_t) (2 * src + 1)];
            const auto ph = std::atan2 (im, re);
            const auto m = magOut[(size_t) k];
            synthWork[(size_t) (2 * k)]     = m * std::cos (ph);
            synthWork[(size_t) (2 * k + 1)] = m * std::sin (ph);
        }

        fft->performRealOnlyInverseTransform (synthWork.data());

        const auto bodyGain = juce::jlimit (0.0f, 1.0f, settings.bodyMix);
        const auto scale = bodyGain * (2.0f / 3.0f) / (float) fftSize;

        for (int i = 0; i < fftSize; ++i)
        {
            const auto idx = (fifoIndex + i) % fftSize;
            outputFifo[(size_t) idx] += synthWork[(size_t) i] * window[(size_t) i] * scale;
        }
    }

    void ToneShift::process (const float* in, float* out, int numSamples)
    {
        if (inputFifo.empty() || numSamples <= 0)
            return;

        const auto mix = juce::jlimit (0.0f, 1.0f, settings.mix);
        const auto delaySize = (int) dryDelay.size();

        for (int n = 0; n < numSamples; ++n)
        {
            float transient = 0.0f, body = 0.0f;
            splitTransient (in[n], transient, body);

            const auto delayedDry = dryDelay[(size_t) delayWrite];
            const auto delayedT = transDelay[(size_t) delayWrite];
            const auto delayedModal = modalDelay[(size_t) delayWrite];
            dryDelay[(size_t) delayWrite] = in[n];
            transDelay[(size_t) delayWrite] = transient;

            inputFifo[(size_t) fifoIndex] = body;
            const auto stftY = outputFifo[(size_t) fifoIndex];
            outputFifo[(size_t) fifoIndex] = 0.0f;
            fifoIndex = (fifoIndex + 1) % fftSize;

            if (++hopCount >= hopSize)
            {
                hopCount = 0;
                processFrame();
            }

            // Modal body: follow the body's envelope and zero-crossings, then resynthesise
            // a sine at the scaled frequency. Duration is unchanged; this is the resonator
            // model of the drum body. STFT warp supplies broadband residual.
            // ~18 Hz follower on |body| so the envelope does not track individual
            // cycles (that would AM-modulate the resonator at 2*f0 and smear the centroid).
            const auto ax = std::abs (body);
            const auto envC = 1.0f - std::exp (-6.28318530718f * 18.0f / (float) sampleRate);
            bodyEnv += envC * (ax - bodyEnv);

            if (bodyPrev <= 0.0f && body > 0.0f && zcCount > 8)
            {
                const auto hz = (float) sampleRate / (float) zcCount;
                if (hz > 30.0f && hz < 4000.0f)
                    modalHz = modalHz < 1.0f ? hz : modalHz + 0.25f * (hz - modalHz);
                zcCount = 0;
            }
            ++zcCount;
            bodyPrev = body;

            const auto ratio = std::pow (2.0f, settings.cents / 1200.0f);
            const auto oscHz = modalHz * juce::jlimit (0.25f, 4.0f, ratio);
            modalPhase += oscHz / (float) sampleRate;
            modalPhase -= std::floor (modalPhase);
            const auto modal = bodyEnv * std::sin (2.0f * pi * modalPhase);
            modalDelay[(size_t) delayWrite] = modal;
            delayWrite = (delayWrite + 1) % delaySize;
            const auto bodyGain = juce::jlimit (0.0f, 1.0f, settings.bodyMix);
            const auto residual = 0.18f * bodyEnv * std::tanh (stftY);
            const auto bodyY = bodyGain * (delayedModal + residual);

            const auto shifted = delayedT + bodyY;
            out[n] = (1.0f - mix) * delayedDry + mix * shifted;
        }
    }
}
