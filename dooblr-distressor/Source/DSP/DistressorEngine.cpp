#include "DistressorEngine.h"

namespace dooblrdistressor::dsp
{
    namespace
    {
        constexpr float pi = juce::MathConstants<float>::pi;

        float mapKnob (float knob, float tmin, float tmax) noexcept
        {
            const auto n = juce::jlimit (0.0f, 1.0f, knob / 10.0f);
            return tmin * std::pow (tmax / tmin, n);
        }
    }

    bool DistressorEngine::usesFastMinAttack (int ratio) noexcept
    {
        return ratio == (int) Ratio::r2 || ratio == (int) Ratio::r3 || ratio == (int) Ratio::r4;
    }

    float DistressorEngine::knobToNorm (float knob) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, knob / 10.0f);
    }

    float DistressorEngine::attackSeconds (float knob, int ratio) noexcept
    {
        return mapKnob (knob, usesFastMinAttack (ratio) ? attackMinFastSec : attackMinSec, attackMaxSec);
    }

    float DistressorEngine::releaseSeconds (float knob, int ratio) noexcept
    {
        juce::ignoreUnused (ratio);
        return mapKnob (knob, releaseMinSec, releaseMaxSec);
    }

    DistressorEngine::Curve DistressorEngine::curveFor (int ratio, float attackKnob, float releaseKnob) noexcept
    {
        Curve c;
        const auto atk = knobToNorm (attackKnob);
        const auto rel = knobToNorm (releaseKnob);

        switch ((Ratio) juce::jlimit (0, (int) Ratio::numRatios - 1, ratio))
        {
            case Ratio::r1:
                c = { 120.0f, 0.0f, 1.0f, 0.0f, false, false, false, false, false };
                break;
            case Ratio::r2:
                // Long parabolic knee: +15 dB typical, up to ~30 dB with slower attack/release.
                c = { -2.0f, 15.0f + 15.0f * (0.55f * atk + 0.45f * rel), 2.0f, 0.0f, true, false, false, false, false };
                break;
            case Ratio::r3:
                c = { -6.0f, 12.0f + 4.0f * atk, 3.0f, 0.0f, true, false, false, false, false };
                break;
            case Ratio::r4:
                c = { -10.0f, 8.0f, 4.0f, 0.0f, false, false, false, false, false };
                break;
            case Ratio::r6:
                c = { -14.0f, 6.0f, 6.0f, 0.0f, false, true, false, false, false };
                break;
            case Ratio::r10:
                c = { -18.0f, 4.0f, 10.0f, 0.0f, false, false, false, true, false };
                break;
            case Ratio::r20:
                c = { -8.0f, 1.2f, 20.0f, 0.0f, false, false, true, false, false };
                break;
            case Ratio::nuke:
                c = { -16.0f, 0.6f, 100.0f, -2.5f, false, false, true, false, true };
                break;
            case Ratio::numRatios:
                break;
        }
        return c;
    }

    float DistressorEngine::staticReductionDb (float detectorDb, int ratio, float attackKnob, float releaseKnob) noexcept
    {
        const auto c = curveFor (ratio, attackKnob, releaseKnob);
        if (c.ratio <= 1.0001f)
            return 0.0f;

        const auto over = detectorDb - c.thresholdDb;
        auto ratioNow = c.ratio;

        if (c.risingRatio && over > c.kneeDb * 0.5f)
        {
            const auto extra = juce::jlimit (0.0f, 1.0f, (over - c.kneeDb * 0.5f) / 18.0f);
            ratioNow = c.ratio + extra * extra * (40.0f - c.ratio);
        }

        const auto slope = 1.0f / ratioNow - 1.0f;
        const auto knee = juce::jmax (0.0f, c.kneeDb);

        float gr = 0.0f;
        if (! (knee > 0.0f))
        {
            gr = over > 0.0f ? slope * over : 0.0f;
        }
        else if (c.parabolic)
        {
            // Gentle x^2 onset that meets the final ratio at the end of the knee.
            if (over <= 0.0f)
                gr = 0.0f;
            else if (over >= knee)
                gr = slope * over;
            else
            {
                const auto t = over / knee;
                gr = slope * over * t;
            }
        }
        else
        {
            if (2.0f * over > knee)
                gr = slope * over;
            else if (2.0f * over > -knee)
                gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else
                gr = 0.0f;
        }

        if (c.brickwall && over > 0.0f)
            gr = juce::jmin (gr, -over + 0.3f); // keep output within ~1 dB past the threshold

        return juce::jmin (0.0f, gr);
    }

    void DistressorEngine::designAudioHp (Biquad& hp2, OnePoleHp& hp1, double sampleRate) noexcept
    {
        // 2nd-order Butterworth high-pass + 1-pole, tuned so the cascade is about -3 dB at 60 Hz
        // and -12 dB at 30 Hz, with an 18 dB/oct stopband (see design notes).
        constexpr float fc2 = 57.2f, q = 0.70710678f, fc1 = 15.5f;
        const auto w0 = 2.0f * pi * fc2 / (float) sampleRate;
        const auto cosw = std::cos (w0);
        const auto sinw = std::sin (w0);
        const auto alpha = sinw / (2.0f * q);
        const auto a0 = 1.0f + alpha;
        hp2.b0 = (1.0f + cosw) * 0.5f / a0;
        hp2.b1 = -(1.0f + cosw) / a0;
        hp2.b2 = (1.0f + cosw) * 0.5f / a0;
        hp2.a1 = (-2.0f * cosw) / a0;
        hp2.a2 = (1.0f - alpha) / a0;
        hp1.setCutoff (fc1, sampleRate);
    }

    void DistressorEngine::designPeak (Biquad& eq, float hz, float gainDb, float q, double sampleRate) noexcept
    {
        const auto a = std::pow (10.0f, gainDb / 40.0f);
        const auto w0 = 2.0f * pi * hz / (float) sampleRate;
        const auto cosw = std::cos (w0);
        const auto sinw = std::sin (w0);
        const auto alpha = sinw / (2.0f * q);
        const auto a0 = 1.0f + alpha / a;
        eq.b0 = (1.0f + alpha * a) / a0;
        eq.b1 = (-2.0f * cosw) / a0;
        eq.b2 = (1.0f - alpha * a) / a0;
        eq.a1 = (-2.0f * cosw) / a0;
        eq.a2 = (1.0f - alpha / a) / a0;
    }

    float DistressorEngine::audioHpMagnitudeDb (float hz, double sampleRate) noexcept
    {
        Biquad hp2;
        OnePoleHp hp1;
        designAudioHp (hp2, hp1, sampleRate);

        const auto w = 2.0 * juce::MathConstants<double>::pi * (double) hz / sampleRate;
        const auto zre = std::cos (w);
        const auto zim = std::sin (w);
        // H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
        const auto numRe = (double) hp2.b0 + (double) hp2.b1 * zre + (double) hp2.b2 * (zre * zre - zim * zim);
        const auto numIm = -(double) hp2.b1 * zim - (double) hp2.b2 * 2.0 * zre * zim;
        const auto denRe = 1.0 + (double) hp2.a1 * zre + (double) hp2.a2 * (zre * zre - zim * zim);
        const auto denIm = -(double) hp2.a1 * zim - (double) hp2.a2 * 2.0 * zre * zim;
        const auto den2 = denRe * denRe + denIm * denIm;
        const auto h2re = (numRe * denRe + numIm * denIm) / den2;
        const auto h2im = (numIm * denRe - numRe * denIm) / den2;
        const auto mag2 = std::sqrt (h2re * h2re + h2im * h2im);

        const auto a = (double) hp1.a;
        // one-pole HP: y = x - lp, lp = a*lp + (1-a)*x  => H = (1-a)(1-z^-1) / (1 - a z^-1) wait
        // z = a z + (1-a) x; y = x - z
        // z = (1-a) x / (1 - a z^-1)
        // H = 1 - (1-a)/(1 - a z^-1) = (1 - a z^-1 - 1 + a) / (1 - a z^-1) = a (1 - z^-1) / (1 - a z^-1)? 
        // 1 - (1-a)/(1-a z^-1) = [(1-a z^-1) - (1-a)] / den = (1 - a z^-1 - 1 + a) / den = a (1 - z^-1) / den
        // At DC that's 0. At Nyquist z=-1: a(1-(-1))/(1-a(-1)) = 2a/(1+a)
        // Empirical process of a sine is the ground truth; this closed form:
        const auto hp1numRe = a * (1.0 - zre);
        const auto hp1numIm = a * zim; // a (0 - (-zim)) for (1 - z^-1) imag part = zim
        // (1 - z^{-1}) = 1 - zre + j zim
        const auto n1re = a * (1.0 - zre);
        const auto n1im = a * zim;
        const auto d1re = 1.0 - a * zre;
        const auto d1im = a * zim;
        const auto d12 = d1re * d1re + d1im * d1im;
        const auto h1re = (n1re * d1re + n1im * d1im) / d12;
        const auto h1im = (n1im * d1re - n1re * d1im) / d12;
        juce::ignoreUnused (hp1numRe, hp1numIm);
        const auto mag1 = std::sqrt (h1re * h1re + h1im * h1im);
        const auto mag = mag1 * mag2;
        return mag > 1.0e-12 ? (float) (20.0 * std::log10 (mag)) : -240.0f;
    }

    float DistressorEngine::smallSignalGain (DistMode mode) noexcept
    {
        juce::ignoreUnused (mode);
        return 1.0f; // saturate() is unity at the origin
    }

    float DistressorEngine::saturate (float x, DistMode mode) noexcept
    {
        float even = 0.0035f, odd = 0.0020f, drive = 0.28f;
        if (mode == DistMode::dist2) { even = 0.28f; odd = 0.004f; drive = 0.32f; }
        if (mode == DistMode::dist3) { even = 0.02f; odd = 0.28f; drive = 1.55f; }

        auto y = x + even * x * x - odd * x * x * x;
        y = std::tanh (drive * y) / drive;
        return y;
    }

    void DistressorEngine::prepare (double newRate, int) noexcept
    {
        sampleRate = newRate > 1.0 ? newRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void DistressorEngine::reset() noexcept
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            audioHp2[ch].reset();
            audioHp1[ch].reset();
            detHp[ch].reset();
            detPeak[ch].reset();
            envDb[ch] = 0.0f;
            optoSlow[ch] = 0.0f;
            lastGrDb[ch] = 0.0f;
            inPeak[ch] = 0.0f;
            outPeak[ch] = 0.0f;
        }
        displayedGrDb = 0.0f;
        thdBallistics = 0.0f;
        clipLatch = false;
        meterGr = meterThd = 0.0f;
        meterLed1 = meterLed3 = false;
        updateCoefficients();
    }

    void DistressorEngine::updateCoefficients() noexcept
    {
        designAudioHp (audioHp2[0], audioHp1[0], sampleRate);
        audioHp2[1] = audioHp2[0];
        audioHp2[1].reset();
        audioHp1[1].setCutoff (15.5f, sampleRate);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            detHp[ch].setCutoff (100.0f, sampleRate);
            designPeak (detPeak[ch], 6000.0f, 9.0f, 0.8f, sampleRate);
        }
    }

    float DistressorEngine::detectorFilter (float x, int channel) noexcept
    {
        // Always run so the filters stay warm; only the selected stages feed the detector.
        const auto hp = detHp[channel].process (x);
        auto y = settings.detHp ? hp : x;
        const auto peaked = detPeak[channel].process (y);
        return settings.detBand ? peaked : y;
    }

    float DistressorEngine::waveshape (float x, DistMode mode, float& thdAcc, float& thdRef) noexcept
    {
        const auto y = saturate (x, mode);
        const auto linear = x * smallSignalGain (mode);
        const auto err = y - linear;
        thdAcc += err * err;
        thdRef += linear * linear;
        return y;
    }

    void DistressorEngine::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jlimit (1, maxChannels, numChannels);
        const auto s = settings;
        const auto ratio = juce::jlimit (0, (int) Ratio::numRatios - 1, s.ratio);
        const auto curve = curveFor (ratio, s.attackKnob, s.releaseKnob);
        const auto atkSec = attackSeconds (s.attackKnob, ratio);
        auto relSec = releaseSeconds (s.releaseKnob, ratio);
        const auto inGain = dbToGain (inputGainDb (s.inputKnob) + headroomDriveDb (s.headroomDb));
        const auto outGain = dbToGain (outputGainDb (s.outputKnob) - headroomDriveDb (s.headroomDb) + curve.makeupDb);
        const auto mix = juce::jlimit (0.0f, 1.0f, s.mix);
        const auto dt = 1.0f / (float) sampleRate;
        const auto atkCoef = 1.0f - std::exp (-dt / juce::jmax (1.0e-7f, atkSec));

        const auto thresholdShift = (s.inputKnob - 5.0f) * 1.2f; // input also drives into the threshold

        float thdAcc = 0.0f, thdRef = 0.0f;
        float blockGr = 0.0f;
        bool clipped = false;

        for (int i = 0; i < numSamples; ++i)
        {
            float dry[maxChannels] {};
            float driven[maxChannels] {};
            float det[maxChannels] {};

            for (int ch = 0; ch < numChannels; ++ch)
            {
                dry[ch] = io[ch][i];
                inPeak[ch] = std::max (inPeak[ch], std::abs (dry[ch]));
                auto x = saturate (dry[ch] * inGain, DistMode::off); // nonlinear input stage (always)
                if (s.audioHp)
                    x = audioHp1[ch].process (audioHp2[ch].process (x));
                driven[ch] = x;
                det[ch] = detectorFilter (x, ch);
            }

            // Stereo link: average of the two detectors. Mono + link is the hardware "dead patch":
            // the unused second unit contributes silence, so the summed detector is half as hot.
            float detLinked = 0.0f;
            if (s.stereoLink)
            {
                if (numChannels == 1)
                    detLinked = std::abs (det[0]) * 0.5f;
                else
                    detLinked = 0.5f * (std::abs (det[0]) + std::abs (det[1]));
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto level = s.stereoLink ? detLinked : std::abs (det[ch]);
                const auto levelDb = gainToDb (level) + thresholdShift;
                const auto target = staticReductionDb (levelDb, ratio, s.attackKnob, s.releaseKnob);

                auto& env = envDb[ch];
                float releaseNow = relSec;

                if (curve.opto)
                {
                    // Program-dependent photocell: a brief hit barely lights it; a long sustain
                    // charges over ~1 s and then recovers over many seconds, stretching release
                    // up to ~20 s.
                    const auto tau = target < -0.5f ? 0.85f : 8.0f;
                    const auto charge = 1.0f - std::exp (-dt / tau);
                    const auto want = target < -0.5f ? juce::jmin (1.0f, -target / 12.0f) : 0.0f;
                    optoSlow[ch] += (want - optoSlow[ch]) * charge;
                    releaseNow = relSec + optoSlow[ch] * (optoReleaseMaxSec - relSec);
                }

                if (target < env)
                {
                    env += (target - env) * atkCoef;
                }
                else if (curve.logRelease)
                {
                    // Logarithmic: remaining GR decays proportionally (fast at first, then slower).
                    const auto relCoef = std::exp (-dt / juce::jmax (1.0e-4f, releaseNow));
                    env *= relCoef;
                    if (env > -0.02f) env = 0.0f;
                }
                else
                {
                    const auto relCoef = 1.0f - std::exp (-dt / juce::jmax (1.0e-4f, releaseNow));
                    env += (target - env) * relCoef;
                }

                lastGrDb[ch] = env;
                blockGr = std::min (blockGr, env);

                auto y = driven[ch] * dbToGain (env);
                y = waveshape (y, s.dist, thdAcc, thdRef);
                y *= outGain;
                if (std::abs (y) > 0.99f)
                    clipped = true;

                const auto wet = y;
                const auto out = s.bypass ? dry[ch] : mix * wet + (1.0f - mix) * dry[ch];
                io[ch][i] = out;
                outPeak[ch] = std::max (outPeak[ch], std::abs (out));
            }
        }

        displayedGrDb = -blockGr;
        const auto thdInst = (thdRef > 1.0e-12f) ? std::sqrt (thdAcc / thdRef) : 0.0f;
        const auto thdCoef = 1.0f - std::exp (-(float) numSamples / ((float) sampleRate * 0.03f));
        thdBallistics += (thdInst - thdBallistics) * thdCoef;
        if (clipped) clipLatch = true;
        // Clip latch clears when the block is well under redline.
        if (! clipped && thdBallistics < thdLed3 * 0.5f)
            clipLatch = false;

        meterGr = displayedGrDb;
        meterThd = thdBallistics;
        meterLed1 = thdBallistics >= thdLed1;
        meterLed3 = thdBallistics >= thdLed3 || clipLatch;
    }

    Meters DistressorEngine::takeMeters() noexcept
    {
        Meters m;
        m.gainReductionDb = meterGr;
        m.inputPeakL = inPeak[0];
        m.inputPeakR = inPeak[1];
        m.outputPeakL = outPeak[0];
        m.outputPeakR = outPeak[1];
        m.thd = meterThd;
        m.led1pct = meterLed1;
        m.led3pct = meterLed3;
        inPeak[0] = inPeak[1] = 0.0f;
        outPeak[0] = outPeak[1] = 0.0f;
        return m;
    }
}
