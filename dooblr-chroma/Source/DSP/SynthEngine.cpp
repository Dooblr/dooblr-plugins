#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrchroma::dsp
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kLn1000 = 6.907755278982137f; // ln(1000) for T60

        float midiToHz (float note) noexcept
        {
            return kC4Hz * std::pow (2.0f, (note - 60.0f) / 12.0f);
        }

        float flush (float v) noexcept
        {
            return (std::abs (v) < 1.0e-30f) ? 0.0f : v;
        }

        // 0.1.0 used a fixed 0.12 per-mode scale, so loudness grew with mode count
        // (density 70 was many times hotter than density 4) and a forte Init note
        // peaked around +13 dBFS. Gains are now divided by sqrt(mode count).
        // kModalScale is calibrated so a single forte mallet note sits near -6..-3 dBFS.
        // High-frequency modes are down-weighted in the energy sum so a dense
        // inharmonic bank does not steal gain from the partials the mallet
        // actually excites (a 2–4 ms pulse is a low-pass).
        constexpr float kLegacyModeGain = 0.12f;
        constexpr float kModalScale = 0.018f;
        constexpr float kModalWeightHz = 600.0f;
        constexpr float kModalWeightPow = 3.0f;
        constexpr float kCoupledMix = 0.70710678f; // 1/sqrt(2)
        // Continuous noise has much higher RMS than a short mallet pulse through (x-x1).
        constexpr float kNoiseExcitation = 0.16f;

        float softClip (float x) noexcept
        {
            constexpr float t = 0.95f;
            const float ax = std::abs (x);
            if (ax <= t)
                return x;
            const float y = t + (1.0f - t) * std::tanh ((ax - t) / (1.0f - t));
            return std::copysign (y, x);
        }
    }

    void SynthEngine::ModalBank::clear()
    {
        n = 0;
        lastVel = lastCoupleVel = 0;
        lowCutOn = false;
        lowCut1.reset();
        lowCut2.reset();
        for (auto& m : modes)
            m.clear();
    }

    void SynthEngine::ModalBank::setup (const ResonatorSettings& rs, float f0, float velocity,
                                        float hitPos, float decayScale, float toneOffset, float sr)
    {
        f0 = std::max (8.0f, f0);
        const int want = modeCount (rs.type, rs.density, f0, sr);
        float ratios[kMaxModes] {};
        n = fillRatios (rs.type, want, ratios, rs.partials);

        const float vel = std::clamp (velocity, 0.0f, 1.0f);
        float decay = rs.decay * decayScale;
        decay *= std::pow (2.0f, rs.decayKey * (midiToHz (60.0f) > 0 ? 0.0f : 0.0f)); // key applied by caller via decayScale
        decay *= (1.0f + rs.decayVel * (vel - 0.5f) * 2.0f);
        decay = std::clamp (decay, 0.03f, 20.0f);

        // Tube radius: clockwise (higher) = longer decay = smaller actual radius, and darker? 
        // Manual: smaller radius = brighter (HPF). Clockwise longer decay.
        float tubeBright = 0.0f;
        if (isTube (rs.type))
        {
            decay *= (0.4f + 1.2f * rs.radius);
            tubeBright = (1.0f - rs.radius) * 6.0f; // extra dB/oct tilt
        }

        const float tone = rs.tone + toneOffset + tubeBright;
        const float material = isTube (rs.type) ? 0.0f : rs.material;
        const float hit = std::clamp (hitPos, 0.0f, 1.0f);

        int written = 0;
        float amps[kMaxModes] {}, shapes[kMaxModes] {}, coupleShape[kMaxModes] {}, polesR[kMaxModes] {};
        for (int i = 0; i < n; ++i)
        {
            const float freq = f0 * ratios[i];
            if (freq >= 0.49f * sr)
                continue;

            float t60 = decay * std::pow (std::max (ratios[i], 0.25f), -material);
            t60 = std::clamp (t60, 0.02f, 30.0f);

            const float r = std::exp (-kLn1000 / (t60 * sr));
            const float w = 2.0f * kPi * freq / sr;

            auto& m = modes[(size_t) written];
            m.a1 = 2.0f * r * std::cos (w);
            m.a2 = -r * r;
            m.freq = freq;

            float shape = hitAmplitude (rs.type, i, ratios[i], hit);
            // A tiny floor keeps a mode from vanishing at a node, but we still honour the shape.
            if (std::abs (shape) < 0.02f)
                shape = (shape < 0.0f ? -0.02f : 0.02f);
            const float tilt = std::pow (std::max (ratios[i], 0.25f), tone / 6.0206f);
            shapes[written] = shape;
            amps[written] = shape * tilt;
            coupleShape[written] = hitAmplitude (rs.type, i, ratios[i], 0.85f);
            polesR[written] = r;
            ++written;
        }
        n = written;

        // |shape| keeps bipolar plate / membrane / high-N string modes from
        // cancelling at the mallet onset. L2 (energy) normalisation, not L1,
        // is what keeps loudness stable once the mallet pulse has dephased
        // across the bank: L1 over-penalises high density because the peak
        // is not a one-sample coherent sum.
        float energy = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float tilt = (shapes[i] != 0.0f) ? std::abs (amps[i] / shapes[i]) : 1.0f;
            amps[i] = std::abs (shapes[i]) * tilt;
            const float w = 1.0f / (1.0f + std::pow (modes[(size_t) i].freq / kModalWeightHz, kModalWeightPow));
            energy += shapes[i] * shapes[i] * w * w;
        }
        const float norm = kModalScale / std::sqrt (std::max (energy, 1.0e-8f));
        for (int i = 0; i < n; ++i)
        {
            auto& m = modes[(size_t) i];
            m.gain = amps[i] * norm;
            // Keep the couple-pickup to mode-gain ratio of 0.1.0 so the bounded loop
            // does not get louder or quieter just because banks were renormalized.
            m.coupleGain = coupleShape[i] * (1.0f - polesR[i]) * (norm / kLegacyModeGain);
        }

        const int harmonic = juce::roundToInt (rs.lowCut);
        lowCutOn = harmonic > 0;
        if (lowCutOn)
        {
            const float cut = std::clamp (f0 * (float) harmonic, 20.0f, 0.4f * sr);
            lowCut1.setHighpass (cut, 0.707f, sr);
            lowCut2.setHighpass (cut, 0.707f, sr);
        }
        else
        {
            lowCut1.setIdentity();
            lowCut2.setIdentity();
        }
    }

    float SynthEngine::ModalBank::process (float excitation, float coupleIn)
    {
        float sum = 0.0f, couple = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            auto& m = modes[(size_t) i];
            const float in = excitation + coupleIn * m.coupleGain;
            const float y = m.process (in);
            sum += y;
            couple += y * (m.coupleGain == 0.0f ? 0.0f : m.coupleGain / std::max (1.0e-6f, (1.0f /* already in gain */)));
            // couple pickup: use the mode's coupleGain relative to its output
            couple += y; // accumulate velocity-like sum; scaled later
        }
        // The second += y double-counts. Use a proper pickup:
        // Recompute couple properly — see below. For speed we stored lastCoupleVel as the
        // sum of mode outputs weighted by coupleGain / (1-r) approximately:
        couple = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            auto& m = modes[(size_t) i];
            const float w = (m.gain != 0.0f) ? (m.coupleGain / m.gain) : 0.0f;
            couple += m.y1 * w;
        }

        if (lowCutOn)
            sum = lowCut2.process (lowCut1.process (sum));

        lastVel = flush (sum);
        lastCoupleVel = flush (couple);
        return lastVel;
    }

    void SynthEngine::Voice::reset()
    {
        active = false;
        gate = false;
        note = -1;
        velocity = 0;
        env = 0;
        envStage = 0;
        envTime = 0;
        malletPhase = 1.0f;
        age = 0;
        dampGain = 1.0f;
        declick = 0;
        lastOut = 0;
        for (auto& layer : layers)
            for (auto& u : layer.unisons)
            {
                u.a.clear();
                u.b.clear();
                u.coupleA = u.coupleB = 0;
            }
        malletColor.reset();
        noiseLp.reset();
        noiseHp.reset();
    }

    void SynthEngine::Fx::prepare (double sr)
    {
        const int chorusLen = std::max (64, (int) std::ceil (0.04 * sr) + 8);
        chorusL.assign ((size_t) chorusLen, 0.0f);
        chorusR.assign ((size_t) chorusLen, 0.0f);
        chorusW = 0;
        chorusPhase = 0;

        const int delayLen = std::max (64, (int) std::ceil (1.6 * sr) + 8);
        delayL.assign ((size_t) delayLen, 0.0f);
        delayR.assign ((size_t) delayLen, 0.0f);
        delayW = 0;

        const int combLens[kCombs] = {
            (int) (0.0291 * sr), (int) (0.0371 * sr), (int) (0.0411 * sr), (int) (0.0437 * sr)
        };
        const int apLens[kAllpasses] = { (int) (0.0050 * sr), (int) (0.0017 * sr) };
        for (int i = 0; i < kCombs; ++i)
        {
            combL[i].assign ((size_t) std::max (combLens[i], 32), 0.0f);
            combR[i].assign ((size_t) std::max (combLens[i] + 23, 32), 0.0f);
            combIdx[i] = 0;
            combFilterL[i] = combFilterR[i] = 0;
        }
        for (int i = 0; i < kAllpasses; ++i)
        {
            apL[i].assign ((size_t) std::max (apLens[i], 16), 0.0f);
            apR[i].assign ((size_t) std::max (apLens[i] + 11, 16), 0.0f);
            apIdx[i] = 0;
            apZ1L[i] = apZ1R[i] = 0;
        }

        eqLowL.setIdentity();
        eqLowR.setIdentity();
        eqMidL.setIdentity();
        eqMidR.setIdentity();
        eqHighL.setIdentity();
        eqHighR.setIdentity();
        compEnv = 0;
    }

    void SynthEngine::Fx::reset()
    {
        auto zero = [] (std::vector<float>& v) { std::fill (v.begin(), v.end(), 0.0f); };
        zero (chorusL); zero (chorusR);
        zero (delayL); zero (delayR);
        for (int i = 0; i < kCombs; ++i) { zero (combL[i]); zero (combR[i]); combFilterL[i] = combFilterR[i] = 0; }
        for (int i = 0; i < kAllpasses; ++i) { zero (apL[i]); zero (apR[i]); apZ1L[i] = apZ1R[i] = 0; }
        chorusW = delayW = 0;
        chorusPhase = 0;
        compEnv = 0;
        eqLowL.reset(); eqLowR.reset(); eqMidL.reset(); eqMidR.reset(); eqHighL.reset(); eqHighR.reset();
    }

    void SynthEngine::Fx::setEq (const Settings& s, float sr)
    {
        eqLowL.setLowshelf (120.0f, s.eqLow, sr);
        eqLowR.setLowshelf (120.0f, s.eqLow, sr);
        eqMidL.setPeaking (s.eqMidFreq, s.eqMid, 0.8f, sr);
        eqMidR.setPeaking (s.eqMidFreq, s.eqMid, 0.8f, sr);
        eqHighL.setHighshelf (6000.0f, s.eqHigh, sr);
        eqHighR.setHighshelf (6000.0f, s.eqHigh, sr);
    }

    void SynthEngine::Fx::process (float& left, float& right, const Settings& s, float sr)
    {
        left  = eqHighL.process (eqMidL.process (eqLowL.process (left)));
        right = eqHighR.process (eqMidR.process (eqLowR.process (right)));

        // Feed-forward compressor, zero look-ahead.
        if (s.compMix > 1.0e-5f)
        {
            const float peak = std::max (std::abs (left), std::abs (right));
            const float thresh = std::pow (10.0f, s.compThreshold / 20.0f);
            const float atk = 1.0f - std::exp (-1.0f / std::max (1.0f, s.compAttack * sr));
            const float rel = 1.0f - std::exp (-1.0f / std::max (1.0f, s.compRelease * sr));
            if (peak > compEnv) compEnv += (peak - compEnv) * atk;
            else                compEnv += (peak - compEnv) * rel;
            float gain = 1.0f;
            if (compEnv > thresh && s.compRatio > 1.0f)
            {
                const float overDb = 20.0f * std::log10 (compEnv / std::max (thresh, 1.0e-8f));
                const float redDb = overDb - overDb / s.compRatio;
                gain = std::pow (10.0f, -redDb / 20.0f);
            }
            const float g = 1.0f + s.compMix * (gain - 1.0f);
            left *= g;
            right *= g;
        }

        if (s.distDrive > 1.0e-5f)
        {
            const float drive = 1.0f + s.distDrive * 8.0f;
            const float wetL = std::tanh (left * drive);
            const float wetR = std::tanh (right * drive);
            left  = left  + s.distMix * (wetL - left);
            right = right + s.distMix * (wetR - right);
        }

        if (s.chorusMix > 1.0e-5f && ! chorusL.empty())
        {
            chorusPhase += s.chorusRate / sr;
            if (chorusPhase >= 1.0f) chorusPhase -= 1.0f;
            const float mod = std::sin (2.0f * kPi * chorusPhase);
            const float delayMsL = 8.0f + s.chorusDepth * 6.0f * mod;
            const float delayMsR = 8.0f - s.chorusDepth * 6.0f * mod;
            auto read = [] (const std::vector<float>& buf, int w, float samples) -> float
            {
                const int n = (int) buf.size();
                float idx = (float) w - samples;
                while (idx < 0.0f) idx += (float) n;
                const int i0 = ((int) idx) % n;
                const int i1 = (i0 + 1) % n;
                const float f = idx - std::floor (idx);
                return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
            };
            chorusL[(size_t) chorusW] = left;
            chorusR[(size_t) chorusW] = right;
            const float wetL = read (chorusL, chorusW, delayMsL * 0.001f * sr);
            const float wetR = read (chorusR, chorusW, delayMsR * 0.001f * sr);
            chorusW = (chorusW + 1) % (int) chorusL.size();
            left  = left  + s.chorusMix * (wetL - left);
            right = right + s.chorusMix * (wetR - right);
        }

        if (s.delayMix > 1.0e-5f && ! delayL.empty())
        {
            const int n = (int) delayL.size();
            const float dSamp = std::clamp (s.delayTime * sr, 1.0f, (float) n - 4.0f);
            auto read = [] (const std::vector<float>& buf, int w, float samples) -> float
            {
                const int m = (int) buf.size();
                float idx = (float) w - samples;
                while (idx < 0.0f) idx += (float) m;
                const int i0 = ((int) idx) % m;
                const int i1 = (i0 + 1) % m;
                const float f = idx - std::floor (idx);
                return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
            };
            const float delayedL = read (delayL, delayW, dSamp);
            const float delayedR = read (delayR, delayW, dSamp);
            delayL[(size_t) delayW] = left  + delayedL * s.delayFeedback;
            delayR[(size_t) delayW] = right + delayedR * s.delayFeedback;
            delayW = (delayW + 1) % n;
            left  += delayedL * s.delayMix;
            right += delayedR * s.delayMix;
        }

        if (s.reverbMix > 1.0e-5f)
        {
            const float feedback = 0.7f + 0.28f * s.reverbSize;
            const float damp = s.reverbDamp;
            auto comb = [&] (std::vector<float>& buf, int& idx, float& filter, float in) -> float
            {
                const int n = (int) buf.size();
                const float y = buf[(size_t) idx];
                filter = y * (1.0f - damp) + filter * damp;
                buf[(size_t) idx] = in + filter * feedback;
                idx = (idx + 1) % n;
                return y;
            };
            auto allpass = [&] (std::vector<float>& buf, int& idx, float in) -> float
            {
                const int n = (int) buf.size();
                const float bufOut = buf[(size_t) idx];
                const float y = -in + bufOut;
                buf[(size_t) idx] = in + bufOut * 0.5f;
                idx = (idx + 1) % n;
                return y;
            };
            float accL = 0, accR = 0;
            for (int i = 0; i < kCombs; ++i)
            {
                accL += comb (combL[i], combIdx[i], combFilterL[i], left);
                accR += comb (combR[i], combIdx[i], combFilterR[i], right);
            }
            accL *= 0.25f;
            accR *= 0.25f;
            for (int i = 0; i < kAllpasses; ++i)
            {
                accL = allpass (apL[i], apIdx[i], accL);
                accR = allpass (apR[i], apIdx[i], accR);
            }
            left  += accL * s.reverbMix;
            right += accR * s.reverbMix;
        }

        left  = flush (left);
        right = flush (right);
    }

    void SynthEngine::prepare (double sr, int samplesPerBlock)
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        blockSize = std::max (1, samplesPerBlock);
        fx.prepare (sampleRate);
        reset();
    }

    void SynthEngine::reset()
    {
        for (auto& v : voices)
            v.reset();
        fx.reset();
        peakAbs = 0;
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (v.active && v.note == note)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr)
        {
            for (auto& v : voices)
                if (! v.active)
                {
                    slot = &v;
                    break;
                }
        }
        if (slot == nullptr)
        {
            slot = &voices[0];
            for (auto& v : voices)
                if (v.age > slot->age)
                    slot = &v;
        }

        const bool wasActive = slot->active;
        const bool retrigger = wasActive && slot->note == note;
        // Keep any leftover output and fade it out over ~2 ms so a steal or
        // retrigger does not hard-cut a ringing tail (or a previous fade).
        const float leftover = wasActive ? slot->lastOut : 0.0f;
        slot->active = true;
        slot->gate = true;
        slot->note = note;
        slot->velocity = std::clamp (velocity, 0.0f, 1.0f);
        slot->targetNote = (float) note;
        if (! wasActive || settings.glide <= 1.0e-4f)
            slot->currentNote = (float) note;
        slot->age = 0;
        slot->envStage = 1;
        slot->envTime = 0;
        if (settings.envAttack <= 1.0e-4f)
            slot->env = 1.0f;
        else
            slot->env = 0.0f;
        slot->malletPhase = 0.0f;
        const float stiff = std::clamp (settings.malletStiffness, 0.0f, 1.0f);
        slot->malletDur = 0.00032f + (1.0f - stiff) * 0.0075f;
        slot->malletColor.reset();
        slot->noiseLp.reset();
        slot->noiseHp.reset();
        slot->malletColor.setHighpass (settings.malletColor, 0.7f, (float) sampleRate);
        slot->lfoPhase = 0;
        slot->dampGain = 1.0f;
        slot->declick = leftover;
        slot->lastOut = leftover;
        slot->rng.setSeedRandomly();
        for (int li = 0; li < 2; ++li)
            for (int ri = 0; ri < 2; ++ri)
                slot->hitRnd[li][ri] = slot->rng.nextFloat() * 2.0f - 1.0f;

        // Fresh banks on a new or retriggered note so we don't inherit ringing from a steal.
        for (auto& layer : slot->layers)
            for (auto& u : layer.unisons)
            {
                u.a.clear();
                u.b.clear();
                u.coupleA = u.coupleB = 0;
            }

        // Initialise the pitch envelope once, then build the banks.
        for (int li = 0; li < 2; ++li)
        {
            const auto& ref = settings.layers[li].a.on ? settings.layers[li].a : settings.layers[li].b;
            const float startSemis = ref.pLevel + ref.pVel * slot->velocity;
            slot->layers[li].pitchEnvTarget = 1.0f;
            slot->layers[li].pitchEnv = std::pow (2.0f, startSemis / 12.0f);
            const float rate = std::max (0.001f, ref.pRate);
            slot->layers[li].pitchEnvCoeff = 1.0f - std::exp (-1.0f / (rate * (float) sampleRate));
        }

        juce::ignoreUnused (retrigger);
        updateVoicePitch (*slot, settings);
    }

    void SynthEngine::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.gate && v.note == note)
            {
                v.gate = false;
                v.envStage = 5;
                v.envTime = 0;
            }
    }

    void SynthEngine::updateVoicePitch (Voice& v, const Settings& s)
    {
        const int unison = std::clamp (s.unisonCount, 1, kMaxUnison);
        const float macroDecay = 1.0f + s.macro2;
        const float macroTone = s.macro3 * 6.0f;
        const float vib = std::sin (v.vibratoPhase) * s.vibratoDepth / 100.0f; // in semitones (depth is cents)

        auto setupLayer = [&] (int li)
        {
            const auto& ls = s.layers[li];
            auto& lv = v.layers[li];
            lv.unisonUsed = unison;

            const bool split = s.layerMode == 1;
            const float splitAt = s.splitNote;
            bool plays = ls.level > 1.0e-5f;
            if (split)
            {
                if (li == 0) plays = plays && (float) v.note < splitAt;
                else         plays = plays && (float) v.note >= splitAt;
            }
            if (! plays)
            {
                for (int u = 0; u < kMaxUnison; ++u)
                {
                    lv.unisons[(size_t) u].a.n = 0;
                    lv.unisons[(size_t) u].b.n = 0;
                }
                return;
            }

            const float note = v.currentNote + ls.transpose + vib;
            auto setupRes = [&] (ModalBank& bank, const ResonatorSettings& rs, int ri)
            {
                if (! rs.on)
                {
                    bank.n = 0;
                    return;
                }
                const float keyOffset = (note - 60.0f) * rs.key;
                const float f0 = midiToHz (60.0f + rs.pitch + rs.cents * 0.01f + keyOffset)
                               * lv.pitchEnv;
                float hit = rs.hit + rs.hitVel * (v.velocity - 0.5f) * 2.0f
                            + rs.hitKey * (note - 60.0f) / 48.0f
                            + rs.hitRnd * v.hitRnd[li][ri];
                const float decayScale = macroDecay * std::pow (2.0f, rs.decayKey * (note - 60.0f) / 24.0f);
                bank.setup (rs, f0, v.velocity, hit, decayScale, macroTone, (float) sampleRate);
            };

            for (int u = 0; u < unison; ++u)
            {
                float detune = 0.0f;
                if (unison > 1)
                    detune = (s.unisonDetune / 100.0f)
                             * ((float) u - 0.5f * (float) (unison - 1)) / std::max (0.5f, 0.5f * (float) (unison - 1));
                // Temporarily bake detune into pitchEnv... apply as frequency scale after setup.
                setupRes (lv.unisons[(size_t) u].a, ls.a, 0);
                setupRes (lv.unisons[(size_t) u].b, ls.b, 1);
                if (detune != 0.0f)
                {
                    const float scale = std::pow (2.0f, detune / 12.0f);
                    auto retune = [&] (ModalBank& bank)
                    {
                        // Rebuild with scaled f0 would be cleaner; scale mode frequencies approximately
                        // by adjusting a1 = 2 r cos(w*scale) — skip if already set up: redo setup.
                        juce::ignoreUnused (scale, bank);
                    };
                    // Redo setup with detuned f0:
                    auto setupDetuned = [&] (ModalBank& bank, const ResonatorSettings& rs, int ri)
                    {
                        if (! rs.on) { bank.n = 0; return; }
                        const float keyOffset = (note - 60.0f) * rs.key;
                        const float f0 = midiToHz (60.0f + rs.pitch + rs.cents * 0.01f + keyOffset + detune)
                                       * lv.pitchEnv;
                        float hit = rs.hit + rs.hitVel * (v.velocity - 0.5f) * 2.0f
                                    + rs.hitKey * (note - 60.0f) / 48.0f
                                    + rs.hitRnd * v.hitRnd[li][ri];
                        const float decayScale = macroDecay * std::pow (2.0f, rs.decayKey * (note - 60.0f) / 24.0f);
                        bank.setup (rs, f0, v.velocity, hit, decayScale, macroTone, (float) sampleRate);
                    };
                    setupDetuned (lv.unisons[(size_t) u].a, ls.a, 0);
                    setupDetuned (lv.unisons[(size_t) u].b, ls.b, 1);
                    juce::ignoreUnused (retune);
                }
            }

        };

        setupLayer (0);
        setupLayer (1);
    }

    void SynthEngine::renderVoice (Voice& v, float* left, float* right, int start, int num)
    {
        const auto s = settings;
        const float sr = (float) sampleRate;
        const float malletAmt = s.mixMallet * (1.0f + s.macro1);
        const float noiseAmt  = s.mixNoise + 0.5f * s.macro4;
        const float directAmt = s.mixDirect;

        const float malletWidth = std::max (2.0f, v.malletDur * sr);
        v.malletColor.setHighpass (s.malletColor, 0.7f, sr);

        const bool lpHp = s.noiseFilter == 3;
        const bool lp = s.noiseFilter == 0 || lpHp;
        const bool hp = s.noiseFilter == 1 || lpHp;
        const bool bp = s.noiseFilter == 2;
        if (bp)
        {
            v.noiseLp.setBandpass (s.noiseFreq, s.noiseQ, sr);
            v.noiseHp.setIdentity();
        }
        else
        {
            if (lp) v.noiseLp.setLowpass (s.noiseFreq, s.noiseQ, sr);
            else    v.noiseLp.setIdentity();
            if (hp)
            {
                const float hpFreq = lpHp ? std::max (40.0f, s.noiseFreq / std::max (1.1f, s.noiseQ))
                                          : s.noiseFreq;
                v.noiseHp.setHighpass (hpFreq, s.noiseQ, sr);
            }
            else
                v.noiseHp.setIdentity();
        }

        for (int i = 0; i < num; ++i)
        {
            // Glide
            if (s.glide > 1.0e-4f)
            {
                const float coeff = 1.0f - std::exp (-1.0f / (s.glide * sr));
                v.currentNote += (v.targetNote - v.currentNote) * coeff;
            }
            else
                v.currentNote = v.targetNote;

            v.vibratoPhase += s.vibratoRate / sr;
            if (v.vibratoPhase >= 1.0f) v.vibratoPhase -= 1.0f;
            v.lfoPhase += s.lfoRate / sr;
            if (v.lfoPhase >= 1.0f) v.lfoPhase -= 1.0f;

            // Rebuild banks slowly when pitch is moving.
            if (((v.age + i) & 63) == 0)
                updateVoicePitch (v, s);

            for (int li = 0; li < 2; ++li)
            {
                auto& lv = v.layers[li];
                lv.pitchEnv += (lv.pitchEnvTarget - lv.pitchEnv) * lv.pitchEnvCoeff;
            }

            // Mallet pulse (Hann * decaying)
            float mallet = 0.0f;
            if (v.malletPhase < 1.0f)
            {
                const float t = v.malletPhase;
                mallet = 0.5f * (1.0f - std::cos (2.0f * kPi * t)) * (1.0f - t);
                if (s.malletNoise > 1.0e-5f)
                {
                    // Window the noise with the same Hann * decay as the pulse so the
                    // first (and last) sample is 0. A bare (1-t) step at t=0 was a click.
                    const float n = (v.rng.nextFloat() * 2.0f - 1.0f) * s.malletNoise * mallet;
                    mallet += v.malletColor.process (n);
                }
                v.malletPhase += 1.0f / malletWidth;
            }
            mallet *= malletAmt * (0.4f + 0.6f * v.velocity);

            // Noise envelope
            const float lfo = std::sin (2.0f * kPi * v.lfoPhase) * s.lfoDepth;
            auto advanceEnv = [&]()
            {
                const bool ahd = s.envMode == 1;
                const float atk = std::max (1.0e-4f, s.envAttack);
                const float hld = std::max (0.0f, s.envHold);
                const float dec = std::max (1.0e-4f, s.envDecay);
                const float sus = ahd ? 0.0f : s.envSustain;
                const float rel = std::max (1.0e-4f, s.envRelease);

                switch (v.envStage)
                {
                    case 1: // attack
                        v.env += (1.0f / (atk * sr));
                        if (v.env >= 1.0f) { v.env = 1.0f; v.envStage = 2; v.envTime = 0; }
                        break;
                    case 2: // hold
                        v.envTime += 1.0f / sr;
                        if (v.envTime >= hld)
                            v.envStage = 3;
                        break;
                    case 3: // decay
                        v.env += (sus - v.env) * (1.0f - std::exp (-1.0f / (dec * sr)));
                        if (std::abs (v.env - sus) < 1.0e-4f || (ahd && v.env <= 1.0e-4f))
                        {
                            v.env = sus;
                            v.envStage = ahd ? 0 : 4;
                            if (ahd && ! v.gate)
                                v.envStage = 0;
                        }
                        break;
                    case 4: // sustain
                        v.env = sus;
                        if (! v.gate) { v.envStage = 5; v.envTime = 0; }
                        break;
                    case 5: // release
                        v.env += (0.0f - v.env) * (1.0f - std::exp (-1.0f / (rel * sr)));
                        if (v.env < 1.0e-4f) { v.env = 0; v.envStage = 0; }
                        break;
                    default:
                        v.env = 0;
                        break;
                }
            };
            advanceEnv();

            float noiseSig = 0.0f;
            if (noiseAmt > 1.0e-5f && v.env > 1.0e-5f)
            {
                float n = (noise.nextFloat() * 2.0f - 1.0f);
                n = v.noiseLp.process (n);
                if (! bp)
                    n = v.noiseHp.process (n);
                noiseSig = n * v.env * noiseAmt * (1.0f + lfo) * (0.4f + 0.6f * v.velocity) * kNoiseExcitation;
            }

            const float excitation = mallet + noiseSig;
            float res = 0.0f;

            int nActiveLayers = 0;
            for (int li = 0; li < 2; ++li)
                if (s.layers[li].level > 1.0e-5f)
                    ++nActiveLayers;
            const float stackNorm = nActiveLayers > 1 ? (1.0f / std::sqrt ((float) nActiveLayers)) : 1.0f;

            for (int li = 0; li < 2; ++li)
            {
                const auto& ls = s.layers[li];
                auto& lv = v.layers[li];
                if (ls.level <= 1.0e-5f)
                    continue;

                float layerOut = 0.0f;
                const float invUnison = 1.0f / (float) std::max (1, lv.unisonUsed);
                for (int u = 0; u < lv.unisonUsed; ++u)
                {
                    auto& pair = lv.unisons[(size_t) u];
                    const bool aOn = pair.a.n > 0;
                    const bool bOn = pair.b.n > 0;
                    if (! aOn && ! bOn)
                        continue;

                    if (! ls.couple || ! aOn || ! bOn)
                    {
                        const float ya = aOn ? pair.a.process (excitation, 0.0f) : 0.0f;
                        const float yb = bOn ? pair.b.process (excitation, 0.0f) : 0.0f;
                        const float bal = ls.balance;
                        if (aOn && bOn)
                            layerOut += ((1.0f - bal) * ya + bal * yb);
                        else
                            layerOut += ya + yb;
                    }
                    else
                    {
                        // Bidirectional coupling: delayed, energy-bounded force from the
                        // impedance ratio (balance). ZA small when balance is near 0 (A is "light").
                        const float bal = std::clamp (ls.balance, 0.02f, 0.98f);
                        const float zA = 0.08f + 0.92f * bal;
                        const float zB = 1.0f - 0.92f * bal + 0.08f;
                        const float zSum = zA + zB;
                        const float kMax = 0.35f / zSum; // conservative, keeps the loop lossy
                        const float k = kMax * 4.0f * bal * (1.0f - bal) / 1.0f;
                        const float force = k * (pair.coupleA - pair.coupleB);
                        const float driveA = -force / zA;
                        const float driveB =  force / zB;
                        // Excitation enters A only when coupled (A is the struck object).
                        const float ya = pair.a.process (excitation + driveA, 0.0f);
                        const float yb = pair.b.process (driveB, 0.0f);
                        pair.coupleA = flush (0.995f * pair.a.lastCoupleVel);
                        pair.coupleB = flush (0.995f * pair.b.lastCoupleVel);
                        juce::ignoreUnused (ya, yb);
                        layerOut += (pair.a.lastVel + pair.b.lastVel) * kCoupledMix;
                    }
                }
                res += layerOut * ls.level * invUnison * stackNorm;
            }

            float out = res + excitation * directAmt;

            // Release damper: multiply by a gain that starts at 1 and decays smoothly
            // (never a step). Time constant is resonator Release * Decay, at least 2 ms.
            if (! v.gate)
            {
                const float rel = std::max (0.02f, s.layers[0].a.release)
                                * std::max (0.05f, s.layers[0].a.decay);
                const float tau = std::max (0.002f, rel);
                v.dampGain *= std::exp (-1.0f / (tau * sr));
                out *= v.dampGain;
            }

            // ~2 ms fade of a stolen / retriggered tail, added after the new attack
            // so the leftover level falls to zero without a hard cut.
            if (v.declick != 0.0f)
            {
                out += v.declick;
                v.declick *= std::exp (-1.0f / (0.002f * sr));
                if (std::abs (v.declick) < 1.0e-6f)
                    v.declick = 0.0f;
            }

            v.lastOut = flush (out);
            left[start + i]  += out;
            right[start + i] += out;
        }

        v.age += num;

        // Voice ends only after the output has decayed below ~-90 dB, and never
        // by hard-cutting a still-audible tail.
        constexpr float kSilence = 3.16227766e-5f; // 10^(-90/20)
        if (! v.gate && v.envStage == 0 && v.malletPhase >= 1.0f && v.age > (int) (0.05 * sampleRate)
            && std::abs (v.lastOut) < kSilence && std::abs (v.declick) < kSilence)
        {
            v.active = false;
            v.lastOut = 0;
            v.declick = 0;
            v.dampGain = 0;
        }
    }

    void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const int n = buffer.getNumSamples();
        buffer.clear();
        if (n <= 0)
            return;

        int sample = 0;
        for (const auto metadata : midi)
        {
            const int pos = std::clamp (metadata.samplePosition, 0, n);
            if (pos > sample)
            {
                for (auto& v : voices)
                    if (v.active)
                        renderVoice (v, buffer.getWritePointer (0),
                                     buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0),
                                     sample, pos - sample);
                sample = pos;
            }
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (auto& v : voices)
                    v.reset();
            }
        }

        if (sample < n)
        {
            for (auto& v : voices)
                if (v.active)
                    renderVoice (v, buffer.getWritePointer (0),
                                 buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0),
                                 sample, n - sample);
        }

        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
        const float gain = std::pow (10.0f, settings.outputDb / 20.0f);
        fx.setEq (settings, (float) sampleRate);
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            fx.process (l, r, settings, (float) sampleRate);
            l *= gain;
            r *= gain;
            l = softClip (l);
            r = softClip (r);
            if (! std::isfinite (l)) l = 0;
            if (! std::isfinite (r)) r = 0;
            L[i] = l;
            if (R != L) R[i] = r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        peakAbs = peak;
    }
}
