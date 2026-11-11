#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include <juce_core/juce_core.h>

namespace dooblrm::dsp
{
    inline constexpr float pi = 3.14159265358979323846f;
    inline constexpr float twoPi = 6.28318530717958647692f;

    /** Padé tanh; good to a few percent on |x| < 3, hard-clips beyond. Cheaper than std::tanh
        and denormal-safe once the argument is finite. */
    inline float tanhApprox (float x) noexcept
    {
        x = juce::jlimit (-4.0f, 4.0f, x);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float flushDenormal (float x) noexcept
    {
        return std::abs (x) < 1.0e-20f ? 0.0f : x;
    }

    inline float midiToHz (float midi) noexcept
    {
        return 440.0f * std::exp2 ((midi - 69.0f) / 12.0f);
    }

    /** xorshift32. Deterministic, no heap. */
    struct Rng
    {
        std::uint32_t state = 0xA341316Cu;

        void seed (std::uint32_t s) noexcept { state = s ? s : 0xA341316Cu; }

        std::uint32_t next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float bipolar() noexcept
        {
            return (float) ((int) (next() >> 8) - 8388608) * (1.0f / 8388608.0f);
        }

        float unipolar() noexcept { return (next() >> 8) * (1.0f / 16777215.0f); }
    };

    /** White and pink (Paul Kellet's 7-pole economical filter). */
    struct Noise
    {
        Rng rng;
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

        void seed (std::uint32_t s) noexcept { rng.seed (s); }

        float white() noexcept { return rng.bipolar(); }

        float pink() noexcept
        {
            const auto w = rng.bipolar();
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f;
            b5 = -0.7616f * b5 - w * 0.0168980f;
            const auto p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            return p * 0.11f;
        }
    };

    /**
        Closed-form band-limited impulse train (Stilson/Smith, "Alias-Free Digital Synthesis
        of Classic Analog Waveforms"):

            y(n) = sin(π M φ) / (P sin(π φ))     φ = n/P ∈ [0,1),  P = Fs / f
            M    = largest odd integer ≤ P

        Saw / pulse / triangle are leaky integrals of BLIT / bipolar BLIT, as in that paper.
        When fewer than three harmonics fit below Nyquist the oscillator falls back to a sine
        (the remaining in-band partial).
    */
    struct BlitOsc
    {
        float phase = 0.0f;     // 0..1
        float sawState = 0.0f;
        float pulseState = 0.0f;
        float triState = 0.0f;
        float leak = 0.995f;
        float lastFreq = 440.0f;

        void reset() noexcept
        {
            // Free-running: do not touch phase. Clear integrators only.
            sawState = pulseState = triState = 0.0f;
        }

        void prepare (double sampleRate) noexcept
        {
            // Leaky integrator forgets DC / roundoff on a ~0.3 s horizon (Stilson/Smith §4.3).
            leak = (float) std::exp (-3.0 / sampleRate);
        }

        static float blitAt (float phase, float period) noexcept
        {
            const auto m = 2 * (int) std::floor (period * 0.5f) + 1;
            const auto s = std::sin (pi * phase);
            if (std::abs (s) < 1.0e-6f)
                return (float) m / period;
            return std::sin (pi * (float) m * phase) / (period * s);
        }

        float process (float freqHz, float sampleRate, int wave) noexcept
        {
            freqHz = juce::jlimit (0.05f, sampleRate * 0.45f, freqHz);
            lastFreq = freqHz;
            const auto period = sampleRate / freqHz;
            const auto inc = freqHz / sampleRate;
            const auto m = 2 * (int) std::floor (period * 0.5f) + 1;

            if (m < 3)
            {
                // Top octave: only the fundamental is in band.
                phase += inc;
                if (phase >= 1.0f)
                    phase -= 1.0f;
                const auto s = std::sin (twoPi * phase);
                sawState = pulseState = triState = s;
                return s;
            }

            const auto blit = blitAt (phase, (float) period);
            const auto dc = 1.0f / (float) period;
            sawState = leak * (sawState + blit - dc);

            constexpr float duties[] { 0.5f, 0.5f, 0.5f, 0.5f, 0.25f, 0.10f };
            const auto duty = duties[juce::jlimit (0, 5, wave)];
            auto phase2 = phase - duty;
            if (phase2 < 0.0f)
                phase2 += 1.0f;
            const auto blit2 = blitAt (phase2, (float) period);
            pulseState = leak * (pulseState + blit - blit2);

            const auto scale = 2.0f * inc / juce::jmax (0.02f, duty * (1.0f - duty));
            triState = leak * (triState + scale * pulseState);

            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            const auto saw = juce::jlimit (-1.2f, 1.2f, sawState * 2.0f);
            const auto pulse = juce::jlimit (-1.2f, 1.2f, pulseState);
            const auto tri = juce::jlimit (-1.2f, 1.2f, triState);

            switch (wave)
            {
                case 0: return tri;
                case 1: return juce::jlimit (-1.2f, 1.2f, 0.65f * saw + 0.35f * tri); // shark / tri-saw
                case 2: return saw;
                case 3: return pulse; // 50 %
                case 4: return pulse; // wide
                case 5: return pulse; // narrow
                default: return saw;
            }
        }
    };

    /**
        Dedicated Osc 3 LFO: naive bipolar copies of the Osc 3 waves (aliasing is irrelevant
        below 30 Hz). Frequency is one-pole smoothed so rate changes do not click. Phase is
        free-running unless retrigger() is called. Pulse-wide / pulse-narrow become
        sample-and-hold and bipolar random (PWM is not a useful LFO shape).
    */
    struct LfoOsc
    {
        float phase = 0.0f;
        float hzSmoothed = 1.0f;
        float last = 0.0f;
        float held = 0.0f;
        bool heldValid = false;
        Rng rng { 0x51F00Du };

        void retrigger() noexcept { phase = 0.0f; heldValid = false; }

        float process (float targetHz, float sampleRate, int wave) noexcept
        {
            targetHz = juce::jlimit (0.01f, sampleRate * 0.25f, targetHz);
            const auto coeff = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
            hzSmoothed += (targetHz - hzSmoothed) * coeff;
            hzSmoothed = juce::jlimit (0.01f, sampleRate * 0.25f, hzSmoothed);

            const auto inc = hzSmoothed / (float) sampleRate;
            phase += inc;
            bool wrapped = false;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                wrapped = true;
            }

            last = shape (phase, wave, wrapped);
            return last;
        }

        float shape (float p, int wave, bool wrapped) noexcept
        {
            const auto tri = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            const auto sawUp = 2.0f * p - 1.0f;
            const auto sawDown = 1.0f - 2.0f * p;
            const auto square = p < 0.5f ? 1.0f : -1.0f;

            switch (wave)
            {
                case 0: return tri;
                case 1: return sawDown; // shark slot → falling saw
                case 2: return sawUp;
                case 3: return square;
                case 4: // wide-pulse slot → sample & hold
                    if (wrapped || ! heldValid)
                    {
                        held = rng.bipolar();
                        heldValid = true;
                    }
                    return held;
                case 5: return rng.bipolar(); // narrow-pulse slot → random
                default: return tri;
            }
        }
    };

    /**
        Huovilainen 2004 non-linear Moog ladder (DAFx-04): four Euler stages with embedded
        tanh, resonance feedback, half-sample delay compensation by averaging, 2× oversampling.
        Resonance r is scaled so emphasis = 1 sits just past self-oscillation (loop gain > 4).
    */
    struct LadderFilter
    {
        float y1 = 0, y2 = 0, y3 = 0, y4 = 0, y4z = 0;
        float g = 0;
        float resonance = 0;
        float sampleRate = 48000.0f;

        void prepare (double sr) noexcept
        {
            sampleRate = (float) sr;
            reset();
        }

        void reset() noexcept { y1 = y2 = y3 = y4 = y4z = 0; }

        void set (float cutoffHz, float emphasis) noexcept
        {
            const auto fs2 = sampleRate * 2.0f;
            cutoffHz = juce::jlimit (8.0f, fs2 * 0.24f, cutoffHz);
            // Scaled impulse-invariant one-pole: g = 1 - exp(-2π Fc / Fs)  (paper eq. 21)
            g = 1.0f - std::exp (-twoPi * cutoffHz / fs2);
            // Paper r ∈ (0, 1]; analog self-osc needs loop gain 4. A little extra so it
            // starts from thermal noise at the top of the knob.
            resonance = 4.15f * juce::jlimit (0.0f, 1.0f, emphasis);
        }

        float tick (float x) noexcept
        {
            const auto fb = 0.5f * (y4 + y4z); // half-sample delay (paper §5.3)
            y4z = y4;
            const auto input = tanhApprox (x - resonance * fb);
            y1 = flushDenormal (y1 + g * (tanhApprox (input) - tanhApprox (y1)));
            y2 = flushDenormal (y2 + g * (tanhApprox (y1) - tanhApprox (y2)));
            y3 = flushDenormal (y3 + g * (tanhApprox (y2) - tanhApprox (y3)));
            y4 = flushDenormal (y4 + g * (tanhApprox (y3) - tanhApprox (y4)));
            return y4;
        }

        float process (float x) noexcept
        {
            // Zero-order-hold 2× upsample, average downsample. The paper asks for oversampling
            // because Euler + tanh generate harmonics; 2× is the amount they plot.
            const auto a = tick (x);
            const auto b = tick (x);
            return 0.5f * (a + b);
        }

        /** Linear-core DC gain 1/(1+k), k = resonance. Used by tests. */
        float dcGain() const noexcept { return 1.0f / (1.0f + resonance); }
    };

    /**
        Attack / decay / sustain contour as on the Model D (service manual, board 2).
        There is no separate release knob: the Decay switch makes release = decay; off, the
        note dumps in a couple of milliseconds. Attack charges toward 1.3 so it reaches 1
        in finite time (analog current source into a capacitor with a clamp).
    */
    struct Contour
    {
        enum class Stage { idle, attack, decay, sustain, release };

        Stage stage = Stage::idle;
        float value = 0.0f;
        float sampleRate = 48000.0f;

        void prepare (double sr) noexcept { sampleRate = (float) sr; }
        void reset() noexcept { stage = Stage::idle; value = 0.0f; }

        void gate (bool on, bool retrigger) noexcept
        {
            if (on)
            {
                if (retrigger || stage == Stage::idle || stage == Stage::release)
                    stage = Stage::attack;
            }
            else if (stage != Stage::idle)
            {
                stage = Stage::release;
            }
        }

        float process (float attack, float decay, float sustain, bool decaySwitch) noexcept
        {
            attack = juce::jmax (0.0005f, attack);
            decay = juce::jmax (0.0005f, decay);
            sustain = juce::jlimit (0.0f, 1.0f, sustain);
            const auto release = decaySwitch ? decay : 0.003f;

            const auto coeff = [this] (float seconds, float distance)
            {
                // Coefficient so the analog RC covers `distance` of the remaining gap in `seconds`.
                const auto t = juce::jmax (1.0f, seconds * sampleRate);
                return 1.0f - std::exp (-distance / t);
            };

            switch (stage)
            {
                case Stage::idle:
                    value = 0.0f;
                    break;
                case Stage::attack:
                    value += (1.3f - value) * coeff (attack, 1.467f);
                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        stage = Stage::decay;
                    }
                    break;
                case Stage::decay:
                    value += (sustain - value) * coeff (decay, 4.0f);
                    if (std::abs (value - sustain) < 0.001f)
                    {
                        value = sustain;
                        stage = Stage::sustain;
                    }
                    break;
                case Stage::sustain:
                    value = sustain;
                    break;
                case Stage::release:
                    value += (0.0f - value) * coeff (release, 4.0f);
                    if (value < 1.0e-4f)
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                    }
                    break;
            }
            return value;
        }
    };

    /** Slow random walk in cents. Independent per oscillator (they are non-synchronous). */
    struct Drift
    {
        float value = 0.0f;
        Rng rng;

        void seed (std::uint32_t s) noexcept { rng.seed (s); }

        float process() noexcept
        {
            value += rng.bipolar() * 0.00025f;
            value *= 0.99997f;
            value = juce::jlimit (-0.12f, 0.12f, value); // about ±12 cents
            return value;
        }
    };

    /** One-pole CV lag used for glide (the sample-hold glide on board 2). */
    struct Glide
    {
        float current = 60.0f;

        void jump (float target) noexcept { current = target; }

        float process (float target, float timeSeconds, float sampleRate) noexcept
        {
            if (timeSeconds <= 1.0e-4f)
            {
                current = target;
                return current;
            }
            const auto coeff = 1.0f - std::exp (-1.0f / (timeSeconds * sampleRate));
            current += (target - current) * coeff;
            return current;
        }
    };

    /** Lock-free ring of recent output samples for the live scope. */
    class ScopeTap
    {
    public:
        static constexpr int capacity = 2048;

        void push (float x) noexcept
        {
            const auto i = write.fetch_add (1, std::memory_order_relaxed);
            buffer[(size_t) i & (capacity - 1)] = x;
        }

        void copyLatest (float* dest, int n) const noexcept
        {
            const auto end = write.load (std::memory_order_relaxed);
            n = juce::jmin (n, capacity);
            for (int i = 0; i < n; ++i)
                dest[i] = buffer[(size_t) (end - n + i) & (capacity - 1)];
        }

    private:
        float buffer[capacity] {};
        std::atomic<int> write { 0 };
    };
}
