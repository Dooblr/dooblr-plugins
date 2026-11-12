#include "SynthKit.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace dooblrrhythm::dsp
{
    namespace
    {
        constexpr float twoPi = 6.28318530718f;

        uint32_t lcg (uint32_t& s) noexcept
        {
            s = s * 1664525u + 1013904223u;
            return s;
        }

        float noiseSample (uint32_t& s) noexcept
        {
            return (float) (int32_t) lcg (s) * (1.0f / 2147483648.0f);
        }

        float dbToGain (float db) noexcept
        {
            return db <= -59.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
        }

        struct Spec
        {
            float f0 = 180.0f;
            float inharm = 0.04f;
            int modes = 4;
            float noiseAmt = 0.25f;
            float noiseDecay = 0.0025f;
            float bodyDecay = 0.0012f;
            float noiseHp = 0.05f;
            float noiseLp = 0.35f;
            float metal = 0.0f;
        };

        Spec specFor (int inst, Articulation artic) noexcept
        {
            Spec s;
            switch ((Instrument) inst)
            {
                case Instrument::clave:      s = { 2400.f, 0.18f, 4, 0.15f, 0.008f, 0.0045f, 0.2f, 0.9f, 0.7f }; break;
                case Instrument::cowbell:    s = { 560.f,  0.22f, 5, 0.12f, 0.004f, 0.0009f, 0.15f, 0.7f, 0.85f }; break;
                case Instrument::gankogui:   s = { 420.f,  0.28f, 6, 0.10f, 0.003f, 0.0007f, 0.12f, 0.65f, 0.9f }; break;
                case Instrument::agogo:      s = { 680.f,  0.24f, 5, 0.10f, 0.004f, 0.0008f, 0.18f, 0.75f, 0.88f }; break;
                case Instrument::quinto:     s = { 220.f,  0.03f, 5, 0.28f, 0.0035f, 0.0014f, 0.04f, 0.4f, 0.05f }; break;
                case Instrument::conga:      s = { 160.f,  0.025f, 5, 0.26f, 0.003f, 0.0011f, 0.03f, 0.35f, 0.04f }; break;
                case Instrument::tumba:      s = { 95.f,   0.02f, 5, 0.22f, 0.0025f, 0.0008f, 0.02f, 0.28f, 0.03f }; break;
                case Instrument::bongos:     s = { 310.f,  0.04f, 4, 0.3f,  0.004f, 0.0018f, 0.05f, 0.5f, 0.06f }; break;
                case Instrument::timbales:   s = { 280.f,  0.08f, 5, 0.18f, 0.003f, 0.0010f, 0.08f, 0.55f, 0.35f }; break;
                case Instrument::guiro:      s = { 1400.f, 0.4f,  3, 0.85f, 0.0018f, 0.006f, 0.15f, 0.8f, 0.1f }; break;
                case Instrument::shakers:    s = { 900.f,  0.5f,  2, 0.95f, 0.0022f, 0.008f, 0.2f, 0.85f, 0.0f }; break;
                case Instrument::maracas:    s = { 1100.f, 0.45f, 2, 0.9f,  0.0028f, 0.007f, 0.22f, 0.88f, 0.0f }; break;
                case Instrument::shekere:    s = { 700.f,  0.35f, 3, 0.7f,  0.0020f, 0.0025f, 0.1f, 0.6f, 0.15f }; break;
                case Instrument::cabasa:     s = { 1600.f, 0.5f,  2, 0.92f, 0.0035f, 0.009f, 0.25f, 0.9f, 0.0f }; break;
                case Instrument::ganza:      s = { 1300.f, 0.48f, 2, 0.88f, 0.0024f, 0.0075f, 0.18f, 0.82f, 0.0f }; break;
                case Instrument::djembe:     s = { 140.f,  0.03f, 5, 0.32f, 0.0032f, 0.0012f, 0.03f, 0.38f, 0.05f }; break;
                case Instrument::talkingDrum:s = { 170.f,  0.05f, 5, 0.24f, 0.0028f, 0.0010f, 0.04f, 0.4f, 0.08f }; break;
                case Instrument::kenkeni:    s = { 210.f,  0.06f, 4, 0.2f,  0.0022f, 0.0009f, 0.05f, 0.4f, 0.25f }; break;
                case Instrument::sangban:    s = { 130.f,  0.05f, 4, 0.18f, 0.0020f, 0.00075f, 0.04f, 0.35f, 0.22f }; break;
                case Instrument::dundunba:   s = { 72.f,   0.04f, 5, 0.16f, 0.0018f, 0.00055f, 0.02f, 0.25f, 0.2f }; break;
                case Instrument::udu:        s = { 85.f,   0.08f, 4, 0.35f, 0.0025f, 0.0007f, 0.02f, 0.3f, 0.1f }; break;
            }
            juce::ignoreUnused (inst);

            switch (artic)
            {
                case Articulation::slap:
                case Articulation::tip:
                    s.noiseAmt = juce::jmin (1.0f, s.noiseAmt * 1.8f);
                    s.f0 *= 1.15f;
                    s.bodyDecay *= 1.4f;
                    s.noiseDecay *= 1.2f;
                    break;
                case Articulation::mute:
                case Articulation::heel:
                case Articulation::toe:
                    s.f0 *= 0.85f;
                    s.bodyDecay *= 2.2f;
                    s.noiseAmt *= 0.7f;
                    break;
                case Articulation::bass:
                    s.f0 *= 0.55f;
                    s.bodyDecay *= 0.7f;
                    s.noiseAmt *= 0.6f;
                    break;
                case Articulation::cascara:
                case Articulation::bell:
                    s.metal = juce::jmax (s.metal, 0.7f);
                    s.f0 *= 1.35f;
                    s.inharm = juce::jmax (s.inharm, 0.2f);
                    break;
                case Articulation::scratch:
                case Articulation::shake:
                    s.noiseAmt = 1.0f;
                    s.modes = juce::jmin (s.modes, 2);
                    s.bodyDecay *= 3.0f;
                    break;
                case Articulation::open:
                case Articulation::mouth:
                case Articulation::tone:
                case Articulation::count:
                    break;
            }
            return s;
        }
    }

    void SynthKit::prepare (double sr, int)
    {
        sampleRate = sr > 1.0 ? sr : 44100.0;
        reset();
    }

    void SynthKit::reset() noexcept
    {
        for (auto& v : voices)
            v = {};
        steal = 0;
    }

    int SynthKit::activeVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.active ? 1 : 0;
        return n;
    }

    void SynthKit::noteOn (int instrument, Articulation artic, float velocity, uint32_t rrSeed) noexcept
    {
        instrument = juce::jlimit (0, numInstruments - 1, instrument);
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
        {
            slot = &voices[(size_t) steal];
            steal = (steal + 1) % maxVoices;
        }
        setupVoice (*slot, instrument, artic, juce::jlimit (0.0f, 1.0f, velocity), rrSeed);
    }

    void SynthKit::setupVoice (Voice& v, int instrument, Articulation artic, float velocity, uint32_t rrSeed) noexcept
    {
        const auto& ctrl = settings.instruments[(size_t) instrument];
        if (ctrl.mute || (settings.anySolo && ! ctrl.solo))
        {
            v.active = false;
            return;
        }

        const int bucket = juce::jlimit (0, 3, (int) artic % 4);
        const Sample* replacement = replacements[instrument][bucket].acquire();

        auto spec = specFor (instrument, artic);
        uint32_t seed = rrSeed ? rrSeed : 1u;
        // Real round-robin: randomised modal tuning, strike position and noise seed.
        const float detune = 1.0f + (noiseSample (seed) * 0.006f);          // ±0.6 %
        const float strike = 0.5f + noiseSample (seed) * 0.35f;              // mode mix
        const float velBright = 0.7f + velocity * 0.6f;
        spec.f0 *= detune * std::pow (2.0f, ctrl.pitch / 12.0f);
        spec.bodyDecay *= juce::jmap (ctrl.decay, 0.0f, 100.0f, 2.4f, 0.45f);
        spec.noiseDecay *= juce::jmap (ctrl.decay, 0.0f, 100.0f, 2.0f, 0.55f);
        spec.noiseAmt *= 0.45f + velocity * 0.9f;

        v = {};
        v.active = true;
        v.instrument = instrument;
        v.noiseSeed = seed ^ 0xA341316Cu;
        v.env = 1.0f;
        v.envDecay = spec.bodyDecay * (float) (44100.0 / sampleRate);
        v.noise = spec.noiseAmt * velocity;
        v.noiseDecay = spec.noiseDecay * (float) (44100.0 / sampleRate);
        v.noiseHp = spec.noiseHp;
        v.noiseLp = spec.noiseLp;
        v.numModes = spec.modes;
        const float pan = juce::jlimit (-1.0f, 1.0f, ctrl.pan * 0.01f);
        const float g = dbToGain (ctrl.volumeDb) * (0.25f + 0.75f * velocity);
        v.gainL = g * (1.0f - juce::jmax (0.0f, pan));
        v.gainR = g * (1.0f + juce::jmin (0.0f, pan));

        const float ratios[] { 1.0f, 1.5f + spec.inharm, 2.0f + spec.inharm * 2, 2.8f + spec.inharm * 3,
                               3.6f + spec.inharm * 4, 4.7f + spec.inharm * 5 };
        for (int m = 0; m < v.numModes; ++m)
        {
            v.modeFreq[m] = spec.f0 * ratios[m] * velBright * (m == 0 ? 1.0f : (0.7f + strike * 0.5f));
            v.modePhase[m] = (noiseSample (seed) * 0.5f + 0.5f) * twoPi;
            const float modeW = (m == 0) ? (1.0f - 0.4f * strike) : (0.35f + 0.4f * strike) / (float) m;
            v.modeAmp[m] = modeW * (0.5f + velocity * 0.5f) * (spec.metal > 0.5f ? 0.9f : 0.7f);
            v.modeDecay[m] = spec.bodyDecay * (1.0f + 0.35f * (float) m) * (float) (44100.0 / sampleRate);
        }

        if (replacement != nullptr && replacement->audio.getNumSamples() > 0)
        {
            v.sample = replacement;
            v.samplePos = 0.0f;
            v.sampleInc = (float) (replacement->sampleRate / sampleRate);
            v.sampleGain = g;
            // Still run a little of the synth under the sample so decay/pitch stay live.
            for (int m = 0; m < v.numModes; ++m)
                v.modeAmp[m] *= 0.15f;
            v.noise *= 0.2f;
        }
    }

    void SynthKit::renderVoice (Voice& v, float* left, float* right, int n) noexcept
    {
        const float srInv = (float) (1.0 / sampleRate);
        uint32_t seed = v.noiseSeed;
        float hp = 0.0f, lp = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float body = 0.0f;
            bool live = false;
            for (int m = 0; m < v.numModes; ++m)
            {
                if (v.modeAmp[m] < 1.0e-5f)
                    continue;
                live = true;
                v.modePhase[m] += v.modeFreq[m] * twoPi * srInv;
                if (v.modePhase[m] > twoPi) v.modePhase[m] -= twoPi;
                body += std::sin (v.modePhase[m]) * v.modeAmp[m];
                v.modeAmp[m] *= (1.0f - v.modeDecay[m]);
            }

            float nz = 0.0f;
            if (v.noise > 1.0e-5f)
            {
                live = true;
                const float white = noiseSample (seed);
                hp += v.noiseHp * (white - hp);
                const float high = white - hp;
                lp += v.noiseLp * (high - lp);
                nz = lp * v.noise;
                v.noise *= (1.0f - v.noiseDecay);
            }

            float samp = 0.0f;
            if (v.sample != nullptr)
            {
                const int len = v.sample->audio.getNumSamples();
                const int pos = (int) v.samplePos;
                if (pos < len)
                {
                    live = true;
                    const float frac = v.samplePos - (float) pos;
                    const int chs = v.sample->audio.getNumChannels();
                    auto at = [&] (int c, int p) { return v.sample->audio.getSample (c, juce::jlimit (0, len - 1, p)); };
                    const float a = at (0, pos);
                    const float b = at (0, pos + 1);
                    samp = a + (b - a) * frac;
                    if (chs > 1)
                    {
                        const float ar = at (1, pos), br = at (1, pos + 1);
                        samp = 0.5f * (samp + ar + (br - ar) * frac);
                    }
                    samp *= v.sampleGain;
                    v.samplePos += v.sampleInc;
                }
                else
                {
                    v.sample = nullptr;
                }
            }

            const float s = body + nz + samp;
            left[i]  += s * v.gainL;
            right[i] += s * v.gainR;
            if (! live)
            {
                v.active = false;
                v.noiseSeed = seed;
                return;
            }
        }
        v.noiseSeed = seed;
        juce::ignoreUnused (hp);
    }

    void SynthKit::render (float* left, float* right, int n) noexcept
    {
        if (left == nullptr)
            return;
        if (right == nullptr)
            right = left;

        juce::FloatVectorOperations::clear (left, n);
        if (right != left)
            juce::FloatVectorOperations::clear (right, n);

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, left, right, n);

        const float g = settings.outputGain;
        if (std::abs (g - 1.0f) > 1.0e-6f)
        {
            juce::FloatVectorOperations::multiply (left, g, n);
            if (right != left)
                juce::FloatVectorOperations::multiply (right, g, n);
        }

        float pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pkL = juce::jmax (pkL, std::abs (left[i]));
            pkR = juce::jmax (pkR, std::abs (right[i]));
        }
        peak.push (pkL, pkR);
    }
}
