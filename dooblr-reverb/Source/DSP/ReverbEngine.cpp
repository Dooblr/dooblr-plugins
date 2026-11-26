#include "ReverbEngine.h"

#include <cmath>

namespace dooblrreverb::dsp
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double ln10Over40 = 0.05756462732485115; // ln (10) / 40: dB -> ln (A) for the bells

        // Line lengths as a fraction of the room size; even entries feed the left half, odd the right.
        constexpr float lineRatio[ReverbEngine::numLines] { 1.000f, 0.953f, 0.897f, 0.861f, 0.812f, 0.776f, 0.739f, 0.703f,
                                                            0.671f, 0.643f, 0.611f, 0.589f, 0.561f, 0.538f, 0.513f, 0.491f };
        constexpr float inSign[ReverbEngine::halfLines]  { 1, -1, 1, 1, -1, 1, -1, -1 };
        constexpr float outSign[ReverbEngine::halfLines] { 1, 1, -1, 1, -1, -1, 1, -1 };
        constexpr float diffuserMs[2][ReverbEngine::numDiffusers] { { 3.17f, 4.71f, 7.43f, 11.27f }, { 3.53f, 5.13f, 6.89f, 10.61f } };

        int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

        inline void hadamard8 (float* x) noexcept
        {
            for (int h = 1; h < 8; h <<= 1)
                for (int i = 0; i < 8; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const auto a = x[j], b = x[j + h];
                        x[j] = a + b;
                        x[j + h] = a - b;
                    }
            constexpr float norm = 0.35355339059327373f; // 1 / sqrt (8)
            for (int i = 0; i < 8; ++i)
                x[i] *= norm;
        }

        float onePole (double seconds, double dt) { return seconds <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-dt / seconds)); }
    }

    ReverbEngine::ReverbEngine()
    {
        prepare (48000.0, 512);
    }

    double ReverbEngine::decaySecondsFor (float space, float decayRate) noexcept
    {
        return params::spaceToSeconds (space) * juce::jlimit (50.0f, 200.0f, decayRate) * 0.01;
    }

    double ReverbEngine::toneLog2 (float space, float brightness, double f) noexcept
    {
        const auto room = roomAt (space);
        const auto b = juce::jlimit (0.0, 1.0, brightness * 0.01);
        // Brightness: high-frequency absorption (a shelf whose corner rises with Brightness) and,
        // in darker settings, a slightly longer low end.
        const auto hfCorner = 1200.0 * std::pow (5.0, b);
        const auto hf = -2.2 * std::pow (1.0 - b, 1.2) / (1.0 + (hfCorner / f) * (hfCorner / f));
        const auto lf = 0.3 * (1.0 - b) / (1.0 + (f / 250.0) * (f / 250.0));
        // The room: bass ratio and air absorption.
        const auto bass = room.bassLog2 / (1.0 + (f / 300.0) * (f / 300.0));
        const auto air = room.airLog2 / (1.0 + (8000.0 / f) * (8000.0 / f));
        return hf + lf + bass + air;
    }

    double ReverbEngine::targetDecaySeconds (const Settings& s, double f) noexcept
    {
        const auto log2m = juce::jlimit (-4.0, 2.5, decayCurveLog2 (s.decay, f) + toneLog2 (s.space, s.brightness, f));
        return juce::jlimit (0.05, 40.0, decaySecondsFor (s.space, s.decayRate) * std::exp2 (log2m));
    }

    //==============================================================================
    void ReverbEngine::prepare (double newRate, int)
    {
        sampleRate = std::max (8000.0, newRate);
        const auto msToSamples = sampleRate * 0.001;

        // Longest line: the largest room (110 ms) plus the deepest modulation, with headroom.
        const auto lineSize = nextPow2 ((int) std::ceil ((115.0 + 2.0) * msToSamples) + 8);
        lineMask = lineSize - 1;
        for (auto& l : lines)
            l.assign ((size_t) lineSize, 0.0f);

        // Predelay buffer: 500 ms + the longest reflection span + the late onset offset.
        const auto preSize = nextPow2 ((int) std::ceil ((500.0 + 125.0 + 90.0) * msToSamples) + 8);
        preMask = preSize - 1;
        for (auto& p : pre)
            p.assign ((size_t) preSize, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
            for (int d = 0; d < numDiffusers; ++d)
                diffusers[(size_t) ch][(size_t) d].assign ((size_t) std::max (1, (int) std::round (diffuserMs[ch][d] * msToSamples)), 0.0f);

        // Early reflection pattern (fixed seed: the same room every time). Times are unsorted so
        // that a lower density drops taps evenly across the span; density rises with time.
        juce::Random rng (0x5eed2026);
        for (int ch = 0; ch < 2; ++ch)
        {
            double energy = 0.0;
            for (int j = 0; j < numErTaps; ++j)
            {
                const auto u = juce::jlimit (0.02f, 1.0f, ((float) j + 0.5f + 0.9f * (rng.nextFloat() - 0.5f)) / (float) numErTaps);
                const auto t = std::pow (u, 0.6f);
                const auto sign = rng.nextBool() ? 1.0f : -1.0f;
                erTime[(size_t) ch][(size_t) j] = t;
                erTapGain[(size_t) ch][(size_t) j] = sign * (1.0f - 0.7f * t) * (0.6f + 0.4f * rng.nextFloat());
                energy += erTapGain[(size_t) ch][(size_t) j] * erTapGain[(size_t) ch][(size_t) j];
            }
            // Shuffle the order (keeping pairs) so dropping the last indices thins the whole span.
            for (int j = numErTaps - 1; j > 0; --j)
            {
                const auto k = rng.nextInt (j + 1);
                std::swap (erTime[(size_t) ch][(size_t) j], erTime[(size_t) ch][(size_t) k]);
                std::swap (erTapGain[(size_t) ch][(size_t) j], erTapGain[(size_t) ch][(size_t) k]);
            }
            const auto norm = (float) (1.0 / std::sqrt (std::max (1.0e-9, energy)));
            for (auto& g : erTapGain[(size_t) ch])
                g *= norm;
        }

        // Graphic EQ bands (octaves from 31.25 Hz) and the least-squares fit.
        for (int m = 0; m < numGeqBands; ++m)
        {
            const auto f = std::min (31.25 * std::pow (2.0, m), sampleRate * 0.45);
            geqG[(size_t) m] = (float) std::tan (pi * f / sampleRate);
        }
        for (int k = 0; k < numFitPoints; ++k)
            fitFreq[(size_t) k] = std::min (31.25 * std::pow (2.0, k * 0.5), sampleRate * 0.45);

        // B: response (dB) at each fit point of each band at a 1 dB prototype gain, plus the
        // broadband gain column. Solve (B'B + lambda D) x = B' t once: solve = (B'B + lambda D)^-1 B'.
        constexpr int nu = numGeqBands + 1;
        double B[numFitPoints][nu] {};
        const auto bandResponse = [this] (int m, double f)
        {
            const auto fc = std::min (31.25 * std::pow (2.0, m), sampleRate * 0.45);
            return svfMagnitudeDb (makeSvf (SvfType::bell, fc, sampleRate, geqQ, 1.0), f, sampleRate);
        };
        for (int k = 0; k < numFitPoints; ++k)
        {
            for (int m = 0; m < numGeqBands; ++m)
                B[k][m] = bandResponse (m, fitFreq[(size_t) k]);
            B[k][numGeqBands] = 1.0;
        }

        double N[nu][2 * nu] {};
        for (int i = 0; i < nu; ++i)
        {
            for (int j = 0; j < nu; ++j)
            {
                double sum = 0.0;
                for (int k = 0; k < numFitPoints; ++k)
                    sum += B[k][i] * B[k][j];
                N[i][j] = sum + (i == j && i < numGeqBands ? 0.02 : 0.0);
            }
            N[i][nu + i] = 1.0;
        }
        for (int c = 0; c < nu; ++c) // Gauss-Jordan with partial pivoting
        {
            int pivot = c;
            for (int r = c + 1; r < nu; ++r)
                if (std::abs (N[r][c]) > std::abs (N[pivot][c]))
                    pivot = r;
            for (int j = 0; j < 2 * nu; ++j)
                std::swap (N[c][j], N[pivot][j]);
            const auto d = N[c][c];
            for (int j = 0; j < 2 * nu; ++j)
                N[c][j] /= d;
            for (int r = 0; r < nu; ++r)
                if (r != c)
                {
                    const auto f = N[r][c];
                    for (int j = 0; j < 2 * nu; ++j)
                        N[r][j] -= f * N[c][j];
                }
        }
        for (int i = 0; i < nu; ++i)
            for (int k = 0; k < numFitPoints; ++k)
            {
                double sum = 0.0;
                for (int j = 0; j < nu; ++j)
                    sum += N[i][nu + j] * B[k][j];
                solve[(size_t) i][(size_t) k] = sum;
            }

        // Guard grid (20 Hz .. Nyquist): the linearised response of every band, so the loop gain
        // can be checked everywhere, not only at the fit points.
        for (int p = 0; p < numGuardPoints; ++p)
        {
            const auto f = 20.0 * std::pow ((sampleRate * 0.499) / 20.0, p / (double) (numGuardPoints - 1));
            for (int m = 0; m < numGeqBands; ++m)
                guardBasis[(size_t) m][(size_t) p] = bandResponse (m, f);
            guardBasis[(size_t) numGeqBands][(size_t) p] = 1.0;
        }

        for (auto& e : eq)
            e = EqState {};

        bandsValid = false;
        reset();
    }

    void ReverbEngine::reset() noexcept
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        for (auto& p : pre)
            std::fill (p.begin(), p.end(), 0.0f);
        for (auto& ch : diffusers)
            for (auto& d : ch)
                std::fill (d.begin(), d.end(), 0.0f);
        for (auto& ch : diffuserPos)
            ch.fill (0);
        for (auto& lineState : geqState)
            for (auto& s : lineState)
                s.reset();
        for (auto& s : inputToneState)
            s.reset();
        for (auto& e : eq)
            for (auto& ch : e.state)
                for (auto& s : ch)
                    s.reset();
        erLowpass.fill (0.0f);
        linePos = prePos = 0;
        for (int i = 0; i < numLines; ++i)
        {
            // Spread the LFO phases so the lines never move together.
            const auto phase = 2.0 * pi * (double) i / (double) numLines * 2.618;
            lfoSin[(size_t) i] = (float) std::sin (phase);
            lfoCos[(size_t) i] = (float) std::cos (phase);
        }
        needsSnap = true;
        controlCountdown = 0;
    }

    //==============================================================================
    void ReverbEngine::computeBandCurve() noexcept
    {
        if (bandsValid && target.decay == lastBands)
            return;
        lastBands = target.decay;
        bandsValid = true;
        for (int k = 0; k < numFitPoints; ++k)
            bandCurveLog2[(size_t) k] = (float) decayCurveLog2 (lastBands, fitFreq[(size_t) k]);
    }

    void ReverbEngine::control (int numSamples, bool snap) noexcept
    {
        const auto dt = numSamples / sampleRate;
        const auto glide = [snap] (float& value, float goal, float coeff) { value = snap ? goal : value + coeff * (goal - value); };

        computeBandCurve();
        glide (now.space, target.space, onePole (0.25, dt));
        glide (now.decayRate, target.decayRate, onePole (0.08, dt));
        glide (now.brightness, target.brightness, onePole (0.05, dt));
        glide (now.character, target.character, onePole (0.05, dt));
        glide (now.distance, target.distance, onePole (0.05, dt));
        glide (now.width, target.width, onePole (0.03, dt));
        glide (now.predelaySamples, (float) (juce::jlimit (0.0f, params::maxPredelayMs, target.predelayMs) * sampleRate * 0.001), onePole (0.1, dt));
        const auto curveCoeff = onePole (0.04, dt);
        for (int k = 0; k < numFitPoints; ++k)
            glide (now.curveLog2[(size_t) k], bandCurveLog2[(size_t) k], curveCoeff);

        const auto room = roomAt (now.space);
        const auto c = juce::jlimit (0.0f, 1.0f, now.character * 0.01f);
        const auto d = juce::jlimit (0.0f, 1.0f, now.distance * 0.01f);
        const auto b = juce::jlimit (0.0f, 1.0f, now.brightness * 0.01f);
        const auto w = juce::jlimit (0.0f, 1.5f, now.width * 0.01f);
        const auto msToSamples = (float) (sampleRate * 0.001);

        // --- Late network: lengths, modulation, absorption ------------------------------
        const auto size = room.sizeMs * msToSamples;
        double meanLength = 0.0;
        for (int i = 0; i < numLines; ++i)
        {
            const auto ratio = lineRatio[(i % 2) * 8 + i / 2]; // interleave so each half spans the range
            lineLength[(size_t) i] = std::max (8.0f, size * ratio);
            meanLength += lineLength[(size_t) i];
        }
        meanLength /= numLines;

        const auto depthMs = c <= 0.5f ? 0.15f * (c / 0.5f) * (c / 0.5f) : 0.15f + 1.05f * ((c - 0.5f) / 0.5f) * ((c - 0.5f) / 0.5f);
        modDepth = depthMs * msToSamples;
        for (int i = 0; i < numLines; ++i)
        {
            const auto rate = (0.07 + 0.045 * i) * (1.0 + 2.0 * c);
            const auto inc = 2.0 * pi * rate / sampleRate;
            lfoRotSin[(size_t) i] = (float) std::sin (inc);
            lfoRotCos[(size_t) i] = (float) std::cos (inc);
            // Keep the phasor on the unit circle.
            const auto mag = std::sqrt (lfoSin[(size_t) i] * lfoSin[(size_t) i] + lfoCos[(size_t) i] * lfoCos[(size_t) i]);
            if (mag > 0.0f) { lfoSin[(size_t) i] /= mag; lfoCos[(size_t) i] /= mag; }
        }

        const auto t0 = decaySecondsFor (now.space, now.decayRate);
        double targetDb[numFitPoints];
        double longest = 0.0;
        for (int k = 0; k < numFitPoints; ++k)
        {
            const auto f = fitFreq[(size_t) k];
            const auto log2m = juce::jlimit (-4.0, 2.5, (double) now.curveLog2[(size_t) k] + toneLog2 (now.space, now.brightness, f));
            const auto t60 = juce::jlimit (0.05, 40.0, t0 * std::exp2 (log2m));
            longest = std::max (longest, t60);
            targetDb[k] = -60.0 / (sampleRate * t60); // dB per sample of delay
        }
        longestDecay.store (longest, std::memory_order_relaxed);

        for (int u = 0; u <= numGeqBands; ++u)
        {
            double sum = 0.0;
            for (int k = 0; k < numFitPoints; ++k)
                sum += solve[(size_t) u][(size_t) k] * targetDb[k];
            fitted[(size_t) u] = sum;
        }

        // Guard: the loop gain must stay below unity everywhere (the fit can overshoot between
        // points); a 40 s RT60 is the longest allowed at any frequency.
        double worst = -1.0e9;
        for (int p = 0; p < numGuardPoints; ++p)
        {
            double sum = 0.0;
            for (int u = 0; u <= numGeqBands; ++u)
                sum += guardBasis[(size_t) u][(size_t) p] * fitted[(size_t) u];
            worst = std::max (worst, sum);
        }
        const auto ceiling = -60.0 / (sampleRate * 40.0);
        if (worst > ceiling)
            fitted[(size_t) numGeqBands] -= worst - ceiling;

        for (int i = 0; i < numLines; ++i)
        {
            const auto L = (double) lineLength[(size_t) i];
            lineGain[(size_t) i] = (float) std::pow (10.0, L * fitted[(size_t) numGeqBands] / 20.0);
            for (int m = 0; m < numGeqBands; ++m)
            {
                auto& cf = geq[(size_t) i][(size_t) m];
                const auto A = std::exp (L * fitted[(size_t) m] * ln10Over40);
                const auto k = 1.0 / (geqQ * A);
                cf.g = geqG[(size_t) m];
                cf.k = (float) k;
                cf.m0 = 1.0f;
                cf.m1 = (float) (k * (A * A - 1.0));
                cf.m2 = 0.0f;
                cf.update();
            }
        }

        // Level: the network's energy grows with RT60 / line length; normalise most of that away
        // (longer rooms still sound a little louder, as they should).
        const auto energy = 0.5 * std::pow (t0 / 1.2, 0.3);
        lateGain = (float) std::sqrt (13.8 * meanLength / (sampleRate * t0) * energy) * (0.8f + 0.4f * d);

        // Width: coupling between the halves, then the output side level.
        const auto coupling = w <= 0.5f ? 1.0f : (w <= 1.0f ? 1.0f - (w - 0.5f) / 0.5f : 0.0f);
        rotCos = std::cos (coupling * (float) pi * 0.25f);
        rotSin = std::sin (coupling * (float) pi * 0.25f);
        sideOut = w <= 0.5f ? w / 0.5f : (w <= 1.0f ? 1.0f : 1.0f + (w - 1.0f) * 2.0f);
        sideIn = std::min (1.0f, w / 0.5f);

        // --- Early reflections and diffusion (Distance and Character) ---------------------
        const auto erSpan = room.erSpanMs * msToSamples;
        const auto erLevel = room.erLevel * (1.25f - 1.0f * d) * (0.65f + 0.7f * std::min (1.0f, c / 0.5f));
        const auto active = room.erDensity * (float) numErTaps;
        for (int ch = 0; ch < 2; ++ch)
            for (int j = 0; j < numErTaps; ++j)
            {
                erOffset[(size_t) ch][(size_t) j] = now.predelaySamples + 1.0f + erTime[(size_t) ch][(size_t) j] * erSpan;
                const auto fade = juce::jlimit (0.0f, 1.0f, active - (float) j);
                erGain[(size_t) ch][(size_t) j] = erTapGain[(size_t) ch][(size_t) j] * fade * erLevel;
            }
        lateOffset = now.predelaySamples + 1.0f + (0.1f + 0.6f * d) * erSpan;
        const auto erCutoff = (3000.0 + 13000.0 * (1.0 - d)) * (0.4 + 0.6 * b);
        erLowpassCoeff = (float) (1.0 - std::exp (-2.0 * pi * std::min (erCutoff, sampleRate * 0.45) / sampleRate));
        diffusion = juce::jlimit (0.3f, 0.82f, room.diffusion + 0.12f * (d - 0.3f) - 0.2f * std::max (0.0f, c - 0.25f));
        erFeed = 0.35f;

        const auto toneHz = std::min (1800.0 * std::pow (11.1, (double) b), sampleRate * 0.45);
        inputTone = makeSvf (SvfType::lowPass, toneHz, sampleRate, 0.6, 0.0);

        // --- Post EQ -----------------------------------------------------------------------
        const auto eqCoeff = onePole (0.03, dt), mixCoeff = onePole (0.02, dt);
        for (int bnd = 0; bnd < params::numEqBands; ++bnd)
        {
            const auto& t = target.eq[(size_t) bnd];
            auto& e = eq[(size_t) bnd];
            const auto goalLog = std::log (juce::jlimit (params::minFreq, params::maxFreq, t.freq));
            const auto structural = e.shape != t.shape || e.slope != t.slope;
            if (structural)
            {
                e.shape = t.shape;
                e.slope = t.slope;
                for (auto& chState : e.state)
                    for (auto& s : chState)
                        s.reset();
            }
            const auto snapBand = snap || structural || e.mix <= 1.0e-4f;
            glide (e.freqLog, goalLog, snapBand ? 1.0f : eqCoeff);
            glide (e.gain, t.gain, snapBand ? 1.0f : eqCoeff);
            glide (e.q, t.q, snapBand ? 1.0f : eqCoeff);
            glide (e.mix, t.active ? 1.0f : 0.0f, mixCoeff);
            if (e.mix < 1.0e-4f && ! t.active)
                e.mix = 0.0f;
            if (e.mix > 0.0f)
            {
                EqBand live { true, e.shape, std::exp (e.freqLog), e.gain, e.q, e.slope };
                for (int s = 0; s < eqSections (live); ++s)
                    e.coeffs[(size_t) s] = eqSectionCoeffs (live, s, sampleRate);
            }
        }
    }

    //==============================================================================
    inline float ReverbEngine::readLagrange (const float* buffer, int mask, int writePos, float delay) noexcept
    {
        // Third-order Lagrange around the read point (passive for a fraction in [0, 1)).
        const auto pos = (float) writePos - delay;
        const auto base = (int) std::floor (pos);
        const auto f = pos - (float) base;
        const auto xm1 = buffer[(base - 1) & mask], x0 = buffer[base & mask], x1 = buffer[(base + 1) & mask], x2 = buffer[(base + 2) & mask];
        const auto fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
        return -f * fm1 * fm2 * (1.0f / 6.0f) * xm1 + fp1 * fm1 * fm2 * 0.5f * x0 - fp1 * f * fm2 * 0.5f * x1 + fp1 * f * fm1 * (1.0f / 6.0f) * x2;
    }

    inline float ReverbEngine::readLinear (const float* buffer, int mask, int writePos, float delay) noexcept
    {
        const auto pos = (float) writePos - delay;
        const auto base = (int) std::floor (pos);
        const auto f = pos - (float) base;
        const auto a = buffer[base & mask], b = buffer[(base + 1) & mask];
        return a + f * (b - a);
    }

    void ReverbEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
    {
        float* lineData[numLines];
        for (int i = 0; i < numLines; ++i)
            lineData[i] = lines[(size_t) i].data();
        float* preData[2] { pre[0].data(), pre[1].data() };
        constexpr float inNorm = 0.35355339059327373f;

        for (int n = 0; n < numSamples; ++n)
        {
            if (controlCountdown <= 0)
            {
                control (controlBlock, needsSnap);
                needsSnap = false;
                controlCountdown = controlBlock;
            }
            --controlCountdown;

            // Input (side narrowed when Width is under 50 %), into the predelay line.
            {
                const auto l = inL[n], r = inR[n];
                const auto m = 0.5f * (l + r), s = 0.5f * (l - r) * sideIn;
                preData[0][prePos] = m + s;
                preData[1][prePos] = m - s;
            }

            // Early reflections: taps on the predelay line, then a gentle low-pass.
            float er[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                float sum = 0.0f;
                const auto& offsets = erOffset[(size_t) ch];
                const auto& gains = erGain[(size_t) ch];
                for (int j = 0; j < numErTaps; ++j)
                    if (gains[(size_t) j] != 0.0f)
                        sum += gains[(size_t) j] * readLinear (preData[ch], preMask, prePos + 1, offsets[(size_t) j]);
                erLowpass[(size_t) ch] += erLowpassCoeff * (sum - erLowpass[(size_t) ch]);
                er[ch] = erLowpass[(size_t) ch];
            }

            // Late input: the delayed signal (later with Distance), toned, blended with the
            // reflections and smeared by the diffusers.
            float u[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto x = readLinear (preData[ch], preMask, prePos + 1, lateOffset);
                x = inputToneState[(size_t) ch].process (x, inputTone);
                x = (1.0f - erFeed) * x + erFeed * er[ch];
                for (int dIdx = 0; dIdx < numDiffusers; ++dIdx)
                {
                    auto& buf = diffusers[(size_t) ch][(size_t) dIdx];
                    auto& pos = diffuserPos[(size_t) ch][(size_t) dIdx];
                    const auto delayed = buf[(size_t) pos];
                    const auto v = x + diffusion * delayed;
                    x = delayed - diffusion * v;
                    buf[(size_t) pos] = v;
                    if (++pos >= (int) buf.size()) pos = 0;
                }
                u[ch] = x * inNorm;
            }
            prePos = (prePos + 1) & preMask;

            // Late network.
            float y[numLines];
            for (int i = 0; i < numLines; ++i)
            {
                // Advance the line's LFO (a rotating phasor).
                const auto s = lfoSin[(size_t) i], c = lfoCos[(size_t) i];
                lfoSin[(size_t) i] = s * lfoRotCos[(size_t) i] + c * lfoRotSin[(size_t) i];
                lfoCos[(size_t) i] = c * lfoRotCos[(size_t) i] - s * lfoRotSin[(size_t) i];

                auto v = readLagrange (lineData[i], lineMask, linePos, lineLength[(size_t) i] + modDepth * (1.0f + s));
                v *= lineGain[(size_t) i];
                auto& states = geqState[(size_t) i];
                const auto& coeffs = geq[(size_t) i];
                for (int m = 0; m < numGeqBands; ++m)
                    v = states[(size_t) m].process (v, coeffs[(size_t) m]);
                y[i] = v;
            }

            float lateL = 0.0f, lateR = 0.0f;
            for (int i = 0; i < halfLines; ++i)
            {
                lateL += outSign[i] * y[i];
                lateR += outSign[i] * y[i + halfLines];
            }

            hadamard8 (y);
            hadamard8 (y + halfLines);
            for (int i = 0; i < halfLines; ++i)
            {
                const auto a = y[i], b = y[i + halfLines];
                lineData[i][linePos] = rotCos * a + rotSin * b + inSign[i] * u[0];
                lineData[i + halfLines][linePos] = -rotSin * a + rotCos * b + inSign[(i + 3) % halfLines] * u[1];
            }
            linePos = (linePos + 1) & lineMask;

            // Mix late + early, then width (mid / side).
            const auto wl = lateGain * lateL * inNorm + er[0];
            const auto wr = lateGain * lateR * inNorm + er[1];
            const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * sideOut;
            float out[2] { mid + side, mid - side };

            // Post EQ.
            for (auto& e : eq)
            {
                if (e.mix <= 0.0f)
                    continue;
                const auto sections = params::eqShapeIsCut (e.shape) ? params::slopeSections (e.slope) : 1;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto v = out[ch];
                    for (int s = 0; s < sections; ++s)
                        v = e.state[(size_t) ch][(size_t) s].process (v, e.coeffs[(size_t) s]);
                    out[ch] += e.mix * (v - out[ch]);
                }
            }

            outL[n] = out[0];
            outR[n] = out[1];
        }
    }

    double ReverbEngine::absorptionDbPerSample (double freq) const noexcept
    {
        double sum = fitted[(size_t) numGeqBands];
        for (int m = 0; m < numGeqBands; ++m)
        {
            const auto fc = std::min (31.25 * std::pow (2.0, m), sampleRate * 0.45);
            sum += fitted[(size_t) m] * svfMagnitudeDb (makeSvf (SvfType::bell, fc, sampleRate, geqQ, 1.0), freq, sampleRate);
        }
        return sum;
    }
}
