#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dooblrprophet::dsp
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float onePole (double sampleRate, double hz) { return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate)); }

        // Mutually prime-ish FDN line lengths (ms at scale 1) and input diffuser lengths (ms).
        constexpr float lineMs[Reverb::numLines] { 31.3f, 37.9f, 41.7f, 47.3f, 53.9f, 59.1f, 67.3f, 73.7f };
        constexpr float lineModHz[Reverb::numLines] { 0.83f, 1.07f, 0.71f, 1.19f, 0.93f, 0.61f, 1.13f, 0.77f };
        constexpr float diffMsL[Reverb::numDiffusers] { 4.77f, 3.59f, 12.73f, 9.31f };
        constexpr float diffMsR[Reverb::numDiffusers] { 5.21f, 3.17f, 11.83f, 8.69f };
        constexpr float maxScale = 2.0f, maxModMs = 2.0f, maxPreDelayMs = 250.0f;
    }

    //==============================================================================
    const std::array<EchoPreset, numEchoPresets>& echoPresets()
    {
        //  name                 sync div  time ms  fb     mix    ping   lowcut highcut drive wow ms wow Hz duck  spread
        static const std::array<EchoPreset, numEchoPresets> all { {
            { "Tape Slap",          false, 8,  110.0f, 0.12f, 0.25f, false, 120.0f, 5500.0f, 0.35f, 0.35f, 0.9f, 0.0f,  0.03f  },
            { "Dotted 8th Lead",    true,  6,  450.0f, 0.35f, 0.22f, false, 200.0f, 7000.0f, 0.10f, 0.05f, 0.5f, 0.3f,  0.0f   },
            { "Ping-Pong Pad",      true,  8,  500.0f, 0.45f, 0.30f, true,  150.0f, 6000.0f, 0.05f, 0.0f,  0.0f, 0.0f,  0.0f   },
            { "Analog BBD Echo",    false, 8,  320.0f, 0.45f, 0.25f, false, 180.0f, 2800.0f, 0.50f, 0.25f, 0.6f, 0.0f,  0.015f },
            { "Dub Feedback",       true,  9,  560.0f, 0.78f, 0.30f, false, 350.0f, 2200.0f, 0.60f, 0.15f, 0.3f, 0.0f,  0.02f  },
            { "Mono Bass Tight",    false, 8,   90.0f, 0.10f, 0.15f, false, 400.0f, 4500.0f, 0.10f, 0.0f,  0.0f, 0.5f,  0.0f   },
            { "Wide Stereo 1/4",    true,  8,  500.0f, 0.35f, 0.25f, false, 150.0f, 8000.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.08f  },
            { "Ducked Lead",        true,  5,  250.0f, 0.40f, 0.30f, false, 220.0f, 6500.0f, 0.10f, 0.05f, 0.4f, 0.85f, 0.01f  },
            { "Lo-Fi Wobble",       false, 8,  260.0f, 0.50f, 0.30f, false, 300.0f, 1800.0f, 0.70f, 1.6f,  3.2f, 0.0f,  0.01f  },
            { "Long Ambient",       true,  10, 1000.0f, 0.70f, 0.30f, true, 200.0f, 4000.0f, 0.15f, 0.2f,  0.25f, 0.2f, 0.0f   },
        } };
        return all;
    }

    const std::array<ReverbPreset, numReverbPresets>& reverbPresets()
    {
        //  name                  size   tone   mix    pre ms scale min/max  decay min/max  lowcut  mod ms mod Hz diff   width spring
        static const std::array<ReverbPreset, numReverbPresets> all { {
            { "Small Plate",        0.35f, 0.70f, 0.22f,  8.0f, 0.35f, 0.80f, 0.5f, 2.5f,  120.0f, 0.25f, 0.8f, 0.75f, 1.0f, 0.0f  },
            { "Bright Plate",       0.50f, 0.90f, 0.25f, 12.0f, 0.45f, 1.00f, 0.8f, 4.0f,  180.0f, 0.30f, 0.7f, 0.75f, 1.0f, 0.0f  },
            { "Vintage Spring-ish", 0.40f, 0.45f, 0.25f,  0.0f, 0.30f, 0.70f, 0.8f, 3.0f,  250.0f, 0.60f, 1.7f, 0.55f, 0.6f, 0.85f },
            { "Room",               0.35f, 0.55f, 0.20f,  6.0f, 0.25f, 0.60f, 0.3f, 1.5f,   90.0f, 0.15f, 0.5f, 0.60f, 0.9f, 0.0f  },
            { "Hall",               0.55f, 0.50f, 0.25f, 22.0f, 0.80f, 1.50f, 1.5f, 5.0f,  100.0f, 0.50f, 0.4f, 0.70f, 1.0f, 0.0f  },
            { "Big Hall",           0.70f, 0.45f, 0.30f, 35.0f, 1.00f, 2.00f, 3.0f, 10.0f, 110.0f, 0.70f, 0.3f, 0.72f, 1.0f, 0.0f  },
            { "Dark Pad Wash",      0.80f, 0.15f, 0.40f, 40.0f, 1.00f, 2.00f, 4.0f, 14.0f, 160.0f, 0.90f, 0.25f, 0.75f, 1.0f, 0.0f },
            { "Lead Ambience",      0.40f, 0.60f, 0.18f, 60.0f, 0.60f, 1.20f, 1.0f, 4.0f,  300.0f, 0.40f, 0.6f, 0.65f, 0.9f, 0.0f  },
            { "Unison Wide",        0.55f, 0.60f, 0.28f, 18.0f, 0.70f, 1.40f, 1.5f, 6.0f,  200.0f, 0.80f, 0.5f, 0.70f, 1.4f, 0.0f  },
            { "Short Tight",        0.20f, 0.60f, 0.15f,  3.0f, 0.25f, 0.50f, 0.2f, 1.0f,  150.0f, 0.10f, 0.9f, 0.60f, 0.8f, 0.0f  },
        } };
        return all;
    }

    const juce::StringArray& echoPresetNames()
    {
        static const juce::StringArray names = [] { juce::StringArray n; for (const auto& p : echoPresets()) n.add (p.name); return n; }();
        return names;
    }

    const juce::StringArray& reverbPresetNames()
    {
        static const juce::StringArray names = [] { juce::StringArray n; for (const auto& p : reverbPresets()) n.add (p.name); return n; }();
        return names;
    }

    const juce::StringArray& echoDivisionNames()
    {
        static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/2.", "1/1" };
        return names;
    }

    double echoDivisionQuarters (int index)
    {
        static constexpr double q[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
        return q[juce::jlimit (0, (int) std::size (q) - 1, index)];
    }

    double echoSeconds (const FxSettings& s, double bpm)
    {
        const auto seconds = s.echoSync ? echoDivisionQuarters (s.echoDivision) * 60.0 / juce::jlimit (20.0, 999.0, bpm)
                                        : (double) s.echoTimeMs * 0.001;
        return juce::jlimit (0.001, (double) maxEchoSeconds, seconds);
    }

    double reverbDecaySeconds (const FxSettings& s)
    {
        const auto& p = reverbPresets()[(size_t) juce::jlimit (0, numReverbPresets - 1, s.reverbPreset)];
        return p.decayMin * std::pow (p.decayMax / p.decayMin, juce::jlimit (0.0f, 1.0f, s.reverbSize));
    }

    //==============================================================================
    void Echo::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = (int) std::ceil ((maxEchoSeconds + 0.05) * sampleRate) + 8; // + the wow and smoothing margin
        bufferL.assign ((size_t) size, 0.0f);
        bufferR.assign ((size_t) size, 0.0f);
        gainStep = (float) (1.0 / (0.02 * sampleRate));
        timeCoef = (float) (1.0 - std::exp (-1.0 / (0.08 * sampleRate)));
        reset();
    }

    void Echo::reset()
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        write = 0;
        lpL = lpR = hpL = hpR = duckEnv = 0.0f;
        wowPhase = 0.0;
        gain = 0.0f;
        primed = false;
        cleared = true;
        clearPosition = 0;
    }

    float Echo::read (const std::vector<float>& buffer, float delaySamples) const noexcept
    {
        auto position = (float) write - delaySamples;
        if (position < 0.0f)
            position += (float) size;
        auto index = (int) position;
        const auto frac = position - (float) index;
        if (index >= size) index -= size;
        auto next = index + 1;
        if (next >= size) next = 0;
        return buffer[(size_t) index] + frac * (buffer[(size_t) next] - buffer[(size_t) index]);
    }

    void Echo::process (float* left, float* right, int numSamples, const FxSettings& s, double bpm) noexcept
    {
        if (size == 0)
            return;

        if (! s.echoOn && gain == 0.0f)
        {
            // Off: the signal passes untouched. The old repeats are cleared a slice per block.
            if (! cleared)
            {
                const auto n = std::min (size - clearPosition, 65536);
                std::fill_n (bufferL.begin() + clearPosition, n, 0.0f);
                std::fill_n (bufferR.begin() + clearPosition, n, 0.0f);
                clearPosition += n;
                if (clearPosition >= size)
                {
                    cleared = true;
                    clearPosition = 0;
                    lpL = lpR = hpL = hpR = duckEnv = 0.0f;
                    primed = false;
                }
            }
            return;
        }

        if (! cleared && clearPosition > 0)
        {
            // Switched back on part-way through clearing the old repeats: finish now.
            std::fill (bufferL.begin() + clearPosition, bufferL.end(), 0.0f);
            std::fill (bufferR.begin() + clearPosition, bufferR.end(), 0.0f);
            clearPosition = 0;
        }
        cleared = false;

        const auto& p = echoPresets()[(size_t) juce::jlimit (0, numEchoPresets - 1, s.echoPreset)];
        const auto target = juce::jlimit (1.0f, (float) size - 4.0f - p.wowMs * 0.001f * (float) sampleRate,
                                          (float) (echoSeconds (s, bpm) * sampleRate));
        if (! primed)
        {
            delay = target;
            primed = true;
        }

        const auto lowCoef = onePole (sampleRate, p.lowCutHz);
        const auto highCoef = onePole (sampleRate, std::min ((double) p.highCutHz, 0.45 * sampleRate));
        const auto k = 1.0f + 3.0f * p.drive;
        const auto feedback = juce::jlimit (0.0f, maxEchoFeedback, s.echoFeedback);
        const auto mix = juce::jlimit (0.0f, 1.0f, s.echoMix);
        const auto wowDepth = p.wowMs * 0.001f * (float) sampleRate;
        const auto wowStep = (double) p.wowHz / sampleRate;
        const auto attack = onePole (sampleRate, 1.0 / 0.005), release = onePole (sampleRate, 1.0 / 0.3);
        const auto gainTarget = s.echoOn ? 1.0f : 0.0f;
        const auto maxDelay = (float) size - 4.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inL = left[i], inR = right[i];

            delay += (target - delay) * timeCoef;
            const auto wow = wowDepth > 0.0f ? wowDepth * std::sin (twoPi * (float) wowPhase) : 0.0f;
            wowPhase += wowStep;
            if (wowPhase >= 1.0) wowPhase -= 1.0;

            const auto dL = juce::jlimit (1.0f, maxDelay, delay + wow);
            const auto dR = juce::jlimit (1.0f, maxDelay, (s.echoPingPong ? delay : delay * (1.0f + p.spread)) + wow);
            auto oL = read (bufferL, dL), oR = read (bufferR, dR);

            // Repeats: low cut, high cut, then soft saturation (never louder than what went in).
            hpL += (oL - hpL) * lowCoef;  oL -= hpL;
            hpR += (oR - hpR) * lowCoef;  oR -= hpR;
            lpL += (oL - lpL) * highCoef; oL = lpL;
            lpR += (oR - lpR) * highCoef; oR = lpR;
            if (p.drive > 0.0f)
            {
                oL = std::tanh (k * oL) / k;
                oR = std::tanh (k * oR) / k;
            }

            if (s.echoPingPong)
            {
                bufferL[(size_t) write] = 0.5f * (inL + inR) + feedback * oR;
                bufferR[(size_t) write] = feedback * oL;
            }
            else
            {
                bufferL[(size_t) write] = inL + feedback * oL;
                bufferR[(size_t) write] = inR + feedback * oR;
            }
            if (++write >= size) write = 0;

            const auto level = std::max (std::abs (inL), std::abs (inR));
            duckEnv += (level - duckEnv) * (level > duckEnv ? attack : release);
            const auto duck = 1.0f - p.duck * std::min (1.0f, duckEnv * 3.0f);

            if (gain != gainTarget)
                gain = gainTarget > gain ? std::min (gainTarget, gain + gainStep) : std::max (gainTarget, gain - gainStep);

            const auto amount = gain * mix;
            left[i] = inL + amount * (oL * duck - inL);
            right[i] = inR + amount * (oR * duck - inR);
        }
    }

    //==============================================================================
    void Reverb::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto lineSize = (int) std::ceil ((lineMs[numLines - 1] * maxScale + maxModMs + 1.0f) * 0.001 * sampleRate) + 4;
        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];
            line.size = lineSize;
            line.data.assign ((size_t) lineSize, 0.0f);
            const auto w = twoPi * lineModHz[i] / (float) sampleRate;
            line.rotC = std::cos (w);
            line.rotS = std::sin (w);
        }
        preSize = (int) std::ceil (maxPreDelayMs * 0.001 * sampleRate) + 2;
        preL.assign ((size_t) preSize, 0.0f);
        preR.assign ((size_t) preSize, 0.0f);
        for (int i = 0; i < numDiffusers; ++i)
        {
            diffL[(size_t) i].size = std::max (1, (int) std::round (diffMsL[i] * 0.001 * sampleRate));
            diffR[(size_t) i].size = std::max (1, (int) std::round (diffMsR[i] * 0.001 * sampleRate));
            diffL[(size_t) i].data.assign ((size_t) diffL[(size_t) i].size, 0.0f);
            diffR[(size_t) i].data.assign ((size_t) diffR[(size_t) i].size, 0.0f);
        }
        gainStep = (float) (1.0 / (0.02 * sampleRate));
        scaleCoef = (float) (1.0 - std::exp (-1.0 / (0.15 * sampleRate)));
        reset();
    }

    void Reverb::reset()
    {
        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];
            std::fill (line.data.begin(), line.data.end(), 0.0f);
            line.damp = 0.0f;
            // Spread the modulation phases so the lines never move together.
            line.cosine = std::cos ((float) i * 0.785f);
            line.sine = std::sin ((float) i * 0.785f);
        }
        lineWrite = preWrite = 0;
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        for (auto* set : { &diffL, &diffR })
            for (auto& a : *set) { std::fill (a.data.begin(), a.data.end(), 0.0f); a.pos = 0; }
        dispL.fill (0.0f);
        dispR.fill (0.0f);
        lowL = lowR = 0.0f;
        gain = 0.0f;
        primed = false;
        cleared = true;
    }

    void Reverb::process (float* left, float* right, int numSamples, const FxSettings& s) noexcept
    {
        if (lines[0].size == 0)
            return;

        if (! s.reverbOn && gain == 0.0f)
        {
            if (! cleared)
            {
                const auto keepGainStep = gainStep;
                reset();
                gainStep = keepGainStep;
            }
            return;
        }
        cleared = false;

        const auto& p = reverbPresets()[(size_t) juce::jlimit (0, numReverbPresets - 1, s.reverbPreset)];
        const auto size = juce::jlimit (0.0f, 1.0f, s.reverbSize);
        const auto targetScale = p.scaleMin + (p.scaleMax - p.scaleMin) * size;
        if (! primed)
        {
            scale = targetScale;
            primed = true;
        }

        const auto rt60 = (float) reverbDecaySeconds (s);
        const auto samplesPerMs = (float) (0.001 * sampleRate);
        const auto modDepth = std::min (maxModMs, p.modMs) * samplesPerMs;
        const auto modScale = p.modHz / 0.9f; // the line rates above average 0.9 Hz
        const auto dampHz = 1200.0 * std::pow (16000.0 / 1200.0, (double) juce::jlimit (0.0f, 1.0f, s.reverbTone));
        const auto dampCoef = onePole (sampleRate, std::min (dampHz, 0.45 * sampleRate));
        const auto lowCoef = onePole (sampleRate, p.lowCutHz);
        const auto preDelay = juce::jlimit (0, preSize - 1, (int) std::round (p.preDelayMs * samplesPerMs));
        const auto diffusion = p.diffusion;
        const auto dispersion = -0.65f;
        const auto mix = juce::jlimit (0.0f, 1.0f, s.reverbMix);
        const auto gainTarget = s.reverbOn ? 1.0f : 0.0f;
        const auto width = p.width;
        const auto lineSize = lines[0].size;

        // Per-line decay gains for this block's scale (RT60: -60 dB after rt60 seconds).
        std::array<float, numLines> decay {}, base {};
        for (int l = 0; l < numLines; ++l)
        {
            base[(size_t) l] = lineMs[l] * samplesPerMs;
            decay[(size_t) l] = std::pow (10.0f, -3.0f * (base[(size_t) l] * targetScale) / ((float) sampleRate * rt60));
        }

        // Modulation rates scale with the preset (re-derive the rotation per block; cheap).
        for (int l = 0; l < numLines; ++l)
        {
            auto& line = lines[(size_t) l];
            const auto w = twoPi * lineModHz[l] * modScale / (float) sampleRate;
            line.rotC = std::cos (w);
            line.rotS = std::sin (w);
            const auto norm = 1.0f / std::sqrt (line.cosine * line.cosine + line.sine * line.sine);
            line.cosine *= norm;
            line.sine *= norm;
        }

        const auto processAllpass = [diffusion] (Allpass& a, float x) noexcept
        {
            const auto delayed = a.data[(size_t) a.pos];
            const auto y = delayed - diffusion * x;
            a.data[(size_t) a.pos] = x + diffusion * y;
            if (++a.pos >= a.size) a.pos = 0;
            return y;
        };

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inL = left[i], inR = right[i];
            scale += (targetScale - scale) * scaleCoef;

            // Pre-delay, then input diffusion.
            preL[(size_t) preWrite] = inL;
            preR[(size_t) preWrite] = inR;
            auto readPre = preWrite - preDelay;
            if (readPre < 0) readPre += preSize;
            auto xL = preL[(size_t) readPre], xR = preR[(size_t) readPre];
            if (++preWrite >= preSize) preWrite = 0;

            for (int d = 0; d < numDiffusers; ++d)
            {
                xL = processAllpass (diffL[(size_t) d], xL);
                xR = processAllpass (diffR[(size_t) d], xR);
            }

            // The network: read each line (modulated), damp, decay, mix (Hadamard), write back.
            std::array<float, numLines> x {};
            for (int l = 0; l < numLines; ++l)
            {
                auto& line = lines[(size_t) l];
                const auto c = line.cosine * line.rotC - line.sine * line.rotS;
                line.sine = line.cosine * line.rotS + line.sine * line.rotC;
                line.cosine = c;

                const auto d = juce::jlimit (1.0f, (float) lineSize - 3.0f, base[(size_t) l] * scale + modDepth * (1.0f + line.sine));
                auto position = (float) lineWrite - d;
                if (position < 0.0f) position += (float) lineSize;
                auto index = (int) position;
                const auto frac = position - (float) index;
                if (index >= lineSize) index -= lineSize;
                const auto next = index + 1 >= lineSize ? 0 : index + 1;
                const auto v = line.data[(size_t) index] + frac * (line.data[(size_t) next] - line.data[(size_t) index]);
                line.damp += (v - line.damp) * dampCoef;
                x[(size_t) l] = v;
            }

            std::array<float, numLines> y {};
            for (int l = 0; l < numLines; ++l)
                y[(size_t) l] = lines[(size_t) l].damp * decay[(size_t) l];

            for (int h = 1; h < numLines; h <<= 1)
                for (int a = 0; a < numLines; a += h << 1)
                    for (int b = a; b < a + h; ++b)
                    {
                        const auto u = y[(size_t) b], w = y[(size_t) (b + h)];
                        y[(size_t) b] = u + w;
                        y[(size_t) (b + h)] = u - w;
                    }

            constexpr float hadamardNorm = 0.35355339f; // 1 / sqrt (8)
            for (int l = 0; l < numLines; ++l)
                lines[(size_t) l].data[(size_t) lineWrite] = y[(size_t) l] * hadamardNorm + ((l & 1) == 0 ? xL : xR) * 0.5f;
            if (++lineWrite >= lineSize) lineWrite = 0;

            auto wetL = 0.35f * (x[0] - x[2] + x[4] - x[6]);
            auto wetR = 0.35f * (x[1] - x[3] + x[5] - x[7]);

            if (p.spring > 0.0f)
            {
                // Spring-ish: a chain of first-order all-passes smears the highs into a chirp.
                auto sL = wetL, sR = wetR;
                for (int d = 0; d < numDispersers; ++d)
                {
                    const auto yl = dispersion * sL + dispL[(size_t) d];
                    dispL[(size_t) d] = sL - dispersion * yl;
                    sL = yl;
                    const auto yr = dispersion * sR + dispR[(size_t) d];
                    dispR[(size_t) d] = sR - dispersion * yr;
                    sR = yr;
                }
                wetL += p.spring * (sL - wetL);
                wetR += p.spring * (sR - wetR);
            }

            lowL += (wetL - lowL) * lowCoef; wetL -= lowL;
            lowR += (wetR - lowR) * lowCoef; wetR -= lowR;

            const auto mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;
            wetL = mid + side;
            wetR = mid - side;

            if (gain != gainTarget)
                gain = gainTarget > gain ? std::min (gainTarget, gain + gainStep) : std::max (gainTarget, gain - gainStep);

            const auto amount = gain * mix;
            left[i] = inL + amount * (wetL - inL);
            right[i] = inR + amount * (wetR - inR);
        }
    }

    //==============================================================================
    void FxChain::process (float* left, float* right, int numSamples, const FxSettings& s, double bpm) noexcept
    {
        if (echo.isIdle() && reverb.isIdle() && ! s.echoOn && ! s.reverbOn)
            return; // bypass: bit for bit

        const juce::ScopedNoDenormals noDenormals; // long, quiet tails stay out of the denormal range

        if (left != right)
        {
            echo.process (left, right, numSamples, s, bpm);
            reverb.process (left, right, numSamples, s);
            return;
        }

        // Mono bus: run in stereo on a copy, then sum back.
        constexpr int chunk = 256;
        float scratch[chunk];
        for (int start = 0; start < numSamples; start += chunk)
        {
            const auto n = std::min (chunk, numSamples - start);
            std::copy_n (left + start, n, scratch);
            echo.process (left + start, scratch, n, s, bpm);
            reverb.process (left + start, scratch, n, s);
            for (int i = 0; i < n; ++i)
                left[start + i] = 0.5f * (left[start + i] + scratch[i]);
        }
    }
}
