#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iterator>

#include "AnalogModel.h"

namespace dooblrprophet::dsp
{
    inline constexpr float pi = 3.14159265358979f;

    /** Cheap, bounded tanh-like saturator (exact limits of +-1, smooth, odd). */
    inline float softClip (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float semitonesToHz (float midiNote) noexcept { return 440.0f * std::exp2 ((midiNote - 69.0f) * (1.0f / 12.0f)); }

    /** Small deterministic PRNG (xorshift32): noise, S&H, voice drift. Real-time safe. */
    struct Random
    {
        uint32_t state = 0x9e3779b9u;
        void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9e3779b9u; }
        uint32_t nextInt() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        /** Uniform in [-1, 1). */
        float bipolar() noexcept { return (float) (nextInt() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    };

    /** Band-limited step / ramp residuals for a 2-sample kernel (polyBLEP / polyBLAMP).
        t is the time since the discontinuity in samples, in [0, 1). The "now" residual is added
        to the sample after the event, the "before" residual to the sample before it. */
    inline float blepNow (float t) noexcept    { const auto u = 1.0f - t; return -0.5f * u * u; }
    inline float blepBefore (float t) noexcept { return 0.5f * t * t; }
    inline float blampNow (float t) noexcept   { const auto u = 1.0f - t; return u * u * u * (1.0f / 6.0f); }
    inline float blampBefore (float t) noexcept { return t * t * t * (1.0f / 6.0f); }

    /** Waveform weights for one oscillator: triangle, saw and pulse are summed. */
    struct WaveWeights
    {
        float tri = 0.0f, saw = 1.0f, pulse = 0.0f;

        /** Continuous shape: 0 triangle, 0.5 saw, 1 pulse, crossfading in between. */
        static WaveWeights fromShape (float shape) noexcept
        {
            shape = std::clamp (shape, 0.0f, 1.0f);
            if (shape < 0.5f)
                return { 1.0f - 2.0f * shape, 2.0f * shape, 0.0f };
            return { 0.0f, 2.0f - 2.0f * shape, 2.0f * shape - 1.0f };
        }

        /** Switched waves (any combination), mixed at equal power before the triangle scaling. */
        static WaveWeights fromSwitches (bool saw, bool tri, bool pulse) noexcept
        {
            const int count = (saw ? 1 : 0) + (tri ? 1 : 0) + (pulse ? 1 : 0);
            const auto w = count > 0 ? 1.0f / std::sqrt ((float) count) : 0.0f;
            // CEM3340 style: the triangle is half the saw's amplitude (analog-reference.md).
            return { tri ? w * model::switchedTriangleLevel : 0.0f, saw ? w : 0.0f, pulse ? w : 0.0f };
        }
    };

    /**
        CEM3340-style ramp-core oscillator (docs/analog-reference.md, "Oscillator" and
        "Blueprint"): the core is a rising ramp (saw) with a reset; the triangle is a fold of the
        ramp with a slightly off-centre peak and a small glitch at each reset; the pulse is a
        comparator on the ramp. Steps use polyBLEP, slope changes polyBLAMP. The output is one
        sample late so corrections can reach the sample before each event. Hard sync resets the
        slave at a sub-sample time, delayed by the finite reset time, with a matching BLEP.
        The caller supplies the increment, already including HF flattening and PWM leak.
    */
    class Oscillator
    {
    public:
        void reset (float startPhase = 0.0f) noexcept { phase = startPhase; held = 0.0f; pendingSync = -1.0f; }

        /** sampleRate: the rate this oscillator runs at (oversampled). triangleSkew: -1..1 per voice. */
        void configure (double sampleRate, float peakPosition) noexcept
        {
            resetSamples = (float) (model::syncResetSeconds * sampleRate);
            glitchArea = model::triangleGlitchHeight * resetSamples;
            triPeak = std::clamp (peakPosition, 0.4f, 0.6f);
        }

        /** Renders one sample. inc: cycles per sample (clamped). syncT: time since the master's
            wrap in samples, in [0, 1), or < 0 for none. wrapT receives the time since this
            oscillator's own wrap or reset (< 0 if none), for the sub oscillator. */
        float process (float inc, float pulseWidth, const WaveWeights& w, float syncT, float& wrapT) noexcept
        {
            inc = std::clamp (inc, 1.0e-7f, 0.45f);
            const auto pw = std::clamp (pulseWidth, 0.02f, 0.98f);
            wrapT = -1.0f;
            float now = 0.0f, before = 0.0f;

            // Finite reset: the slave restarts resetSamples after the master's edge.
            auto resetT = -1.0f;
            if (pendingSync >= 0.0f)
            {
                resetT = pendingSync;
                pendingSync = -1.0f;
            }
            if (syncT >= 0.0f)
            {
                const auto delayed = syncT - resetSamples;
                if (delayed >= 0.0f)
                    resetT = std::max (resetT, delayed);
                else
                    pendingSync = delayed + 1.0f; // lands in the next sample
            }

            if (resetT >= 0.0f)
            {
                resetT = std::min (resetT, 0.999999f);
                auto atSync = phase + inc * (1.0f - resetT);
                atSync -= std::floor (atSync);
                const auto step = naive (0.0f, pw, w, triPeak) - naive (atSync, pw, w, triPeak);
                phase = inc * resetT;
                now += step * blepNow (resetT);
                before += step * blepBefore (resetT);
                addGlitch (resetT, w, now, before);
                wrapT = resetT;
            }
            else
            {
                const auto previous = phase;
                auto next = phase + inc;
                const auto triSlopeChange = 2.0f / (triPeak * (1.0f - triPeak)) * inc * w.tri;

                if (previous < pw && next >= pw) // comparator: falling pulse edge
                    addEvents ((next - pw) / inc, -2.0f * w.pulse, 0.0f, now, before);

                if (previous < triPeak && next >= triPeak) // triangle peak
                    addEvents ((next - triPeak) / inc, 0.0f, -triSlopeChange, now, before);

                if (next >= 1.0f)
                {
                    next -= 1.0f;
                    const auto t = next / inc;
                    addEvents (t, 2.0f * w.pulse - 2.0f * w.saw, triSlopeChange, now, before);
                    addGlitch (t, w, now, before);
                    wrapT = t;

                    if (next >= pw) // tiny pulse widths at high pitch: the edge after the wrap
                        addEvents ((next - pw) / inc, -2.0f * w.pulse, 0.0f, now, before);
                    if (next >= triPeak)
                        addEvents ((next - triPeak) / inc, 0.0f, -triSlopeChange, now, before);
                }

                phase = next;
            }

            const auto current = naive (phase, pw, w, triPeak) + now;
            const auto out = held + before;
            held = current;
            return out;
        }

        float getPhase() const noexcept { return phase; }

        /** Naive (non-band-limited) value; also used by the editor's waveform display.
            Ramp -1..1; triangle folded from the ramp with its peak at peakPosition;
            pulse high while the ramp is below the width threshold. */
        static float naive (float p, float pw, const WaveWeights& w, float peakPosition = 0.5f) noexcept
        {
            const auto a = peakPosition;
            const auto tri = p < a ? -1.0f + 2.0f * p / a : 1.0f - 2.0f * (p - a) / (1.0f - a);
            const auto saw = 2.0f * p - 1.0f;
            const auto pulse = p < pw ? 1.0f : -1.0f;
            return w.tri * tri + w.saw * saw + w.pulse * pulse;
        }

    private:
        static void addEvents (float t, float step, float slope, float& now, float& before) noexcept
        {
            t = std::clamp (t, 0.0f, 0.999999f);
            now += step * blepNow (t) + slope * blampNow (t);
            before += step * blepBefore (t) + slope * blampBefore (t);
        }

        /** Reset glitch on the triangle: an impulse of the glitch's area, split linearly
            between the two samples around the event. */
        void addGlitch (float t, const WaveWeights& w, float& now, float& before) const noexcept
        {
            const auto area = glitchArea * w.tri;
            now += area * (1.0f - t);
            before += area * t;
        }

        float phase = 0.0f, held = 0.0f, pendingSync = -1.0f;
        float resetSamples = 0.02f, glitchArea = 0.012f, triPeak = 0.5f;
    };

    /** Square one octave below its parent: a flip-flop toggled on each parent wrap, with polyBLEP. */
    class SubOscillator
    {
    public:
        void reset() noexcept { high = true; held = 0.0f; }

        float process (float parentWrapT) noexcept
        {
            float now = 0.0f, before = 0.0f;
            if (parentWrapT >= 0.0f)
            {
                high = ! high;
                const auto step = high ? 2.0f : -2.0f;
                const auto t = std::clamp (parentWrapT, 0.0f, 0.999999f);
                now = step * blepNow (t);
                before = step * blepBefore (t);
            }
            const auto current = (high ? 1.0f : -1.0f) + now;
            const auto out = held + before;
            held = current;
            return out;
        }

    private:
        bool high = true;
        float held = 0.0f;
    };

    /** Derivative of softClip. */
    inline float softClipSlope (float x) noexcept
    {
        if (x > 3.0f || x < -3.0f) return 0.0f;
        const auto x2 = x * x;
        const auto a = 9.0f - x2, b = 27.0f + 9.0f * x2;
        return 9.0f * a * a / (b * b);
    }

    /** Saturator with a level: L * softClip (x / L); level <= 0 means linear. */
    inline float saturate (float x, float level) noexcept { return level > 0.0f ? level * softClip (x / level) : x; }

    /**
        4-pole OTA ladder (docs/analog-reference.md, "Filter CEM3320" and "Blueprint / Filter"):
        four TPT one-poles with global feedback, solved zero-delay. Linear core:
            H(s) = 1 / ((1 + s/wc)^4 + k), self-oscillation at k = 4, DC gain 1 / (1 + k).
        With per-stage cutoffs G1..G4 the blueprint's closed form generalises to
            y4 = (Gp x + S) / (1 + k Gp),  Gp = G1 G2 G3 G4,
            S  = G2G3G4 (1-G1) s1 + G3G4 (1-G2) s2 + G4 (1-G3) s3 + (1-G4) s4
        (identical to the blueprint when all G are equal). Nonlinear parts: soft clip at the
        input, a tanh-like resonance cell in the feedback (solved with Newton iterations
        starting from the linear estimate), optional per-stage saturation (SSM model) and a
        biased output clip. Run it oversampled (>= 2x).
    */
    class LadderFilter
    {
    public:
        void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.0f; }

        void setModel (const model::LadderModel& m) noexcept
        {
            mdl = m;
            outputNorm = 1.0f / softClipSlope (m.outputBias);
            outputOffset = softClip (m.outputBias);
        }

        /** Relative per-stage cutoff errors (e.g. +-0.02). */
        void setStageMismatch (const float* mismatch) noexcept
        {
            for (int i = 0; i < 4; ++i)
                stageError[i] = mismatch[i];
        }

        /** w: pi * fc / fs (fs = the rate the filter runs at). k: feedback, 0..kMax. */
        void setCoefficients (float w, float k) noexcept
        {
            const auto g = std::tan (w);
            for (int i = 0; i < 4; ++i)
            {
                // tan (w (1 + m)) by the tangent addition rule with tan (w m) ~ w m.
                const auto d = w * stageError[i];
                const auto gi = (g + d) / (1.0f - g * d);
                G[i] = gi / (1.0f + gi);
            }
            feedback = k;
            Gp = G[0] * G[1] * G[2] * G[3];
        }

        float process (float x) noexcept
        {
            const auto in = saturate (x, mdl.inputLevel);
            const auto S = G[1] * G[2] * G[3] * (1.0f - G[0]) * s[0] + G[2] * G[3] * (1.0f - G[1]) * s[1]
                         + G[3] * (1.0f - G[2]) * s[2] + (1.0f - G[3]) * s[3];

            // Linear zero-delay estimate, then Newton on y = Gp (in - k R(y)) + S.
            auto y = (Gp * in + S) / (1.0f + feedback * Gp);
            const auto level = mdl.resonanceLevel;
            for (int n = 0; n < model::newtonIterations; ++n)
            {
                const auto f = y - Gp * (in - feedback * saturate (y, level)) - S;
                const auto slope = 1.0f + Gp * feedback * softClipSlope (y / level);
                y -= f / slope;
            }

            auto u = in - feedback * saturate (y, level);
            for (int i = 0; i < 4; ++i)
            {
                const auto v = (saturate (u, mdl.stageLevel) - s[i]) * G[i];
                const auto out = v + s[i];
                s[i] = out + v;
                u = out;
            }

            // Biased output clip, normalised to unity small-signal gain and zero offset.
            const auto L = mdl.outputLevel;
            return L * (softClip (u / L + mdl.outputBias) - outputOffset) * outputNorm;
        }

    private:
        model::LadderModel mdl = model::cem3320;
        float s[4] {}, G[4] { 0.5f, 0.5f, 0.5f, 0.5f }, stageError[4] {};
        float Gp = 0.0625f, feedback = 0.0f, outputNorm = 1.0f, outputOffset = 0.0f;
    };

    /**
        Decimate-by-2 half-band FIR (Blackman-windowed sinc, 47 taps, only the non-zero taps
        are evaluated). Feed two input samples, get one output.
    */
    class HalfbandDecimator
    {
    public:
        static constexpr int numTaps = 47;

        HalfbandDecimator() { reset(); }

        void reset() noexcept { std::fill (std::begin (history), std::end (history), 0.0f); pos = 0; }

        float process (float a, float b) noexcept
        {
            push (a);
            push (b);
            const auto& c = coefficients();
            // Newest sample at history[pos + numTaps - 1].
            const auto* x = history + pos;
            auto sum = 0.5f * x[centre];
            for (int i = 0; i < numSide; ++i)
                sum += c.side[i] * (x[centre - (2 * i + 1)] + x[centre + (2 * i + 1)]);
            return sum;
        }

        struct Table { float side[(numTaps - 1) / 4 + 1] {}; };

        /** Computed once; call from prepare() so the first call is not on the audio thread. */
        static const Table& coefficients()
        {
            static const Table table = []
            {
                Table t;
                const auto M = (numTaps - 1) / 2;
                double raw[numSide] {}, sum = 0.5;
                for (int i = 0; i < numSide; ++i)
                {
                    const auto n = 2 * i + 1; // odd offset from the centre
                    const auto sinc = std::sin (0.5 * 3.14159265358979 * n) / (3.14159265358979 * n);
                    const auto k = M + n;
                    const auto win = 0.42 - 0.5 * std::cos (2.0 * 3.14159265358979 * k / (numTaps - 1))
                                   + 0.08 * std::cos (4.0 * 3.14159265358979 * k / (numTaps - 1));
                    raw[i] = sinc * win;
                    sum += 2.0 * raw[i];
                }
                for (int i = 0; i < numSide; ++i)
                    t.side[i] = (float) (raw[i] / sum);
                return t;
            }();
            return table;
        }

    private:
        static constexpr int centre = (numTaps - 1) / 2;
        static constexpr int numSide = (numTaps - 1) / 4 + 1; // odd offsets 1, 3, ... up to centre

        void push (float v) noexcept
        {
            history[pos] = v;
            history[pos + numTaps] = v;
            pos = pos + 1 == numTaps ? 0 : pos + 1;
        }

        float history[2 * numTaps] {};
        int pos = 0;
    };

    /** 2-pole state-variable high-pass (TPT form), resonance up to a strong peak. Linear, stable. */
    class HighPassFilter
    {
    public:
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        void setCoefficients (float g, float resonance) noexcept
        {
            k = 2.0f - 1.88f * std::clamp (resonance, 0.0f, 1.0f);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v0 - k * v1 - v2;
        }

    private:
        float ic1 = 0.0f, ic2 = 0.0f, k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    };

    /**
        ADSR with analog RC-style curves: the attack charges toward an overshoot target (so it
        is slightly convex and reaches 1 in the attack time), decay and release are exponential
        and reach ~2 % of their distance in the set time. Retriggering starts the attack from the
        current level (no click).
    */
    class Envelope
    {
    public:
        enum class Stage { idle, attack, decay, sustain, release };

        void setTimes (double sampleRate, float attack, float decay, float sustainLevel, float release) noexcept
        {
            const auto coef = [sampleRate] (float seconds, float timeConstants)
            {
                const auto tau = std::max (1.0e-4, (double) seconds / timeConstants);
                return (float) (1.0 - std::exp (-1.0 / (tau * sampleRate)));
            };
            attackCoef = coef (attack, 1.466f); // reaches 1 of a 1.3 target after 1.466 tau
            decayCoef = coef (decay, 4.0f);
            releaseCoef = coef (release, 4.0f);
            sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
        }

        void noteOn() noexcept { stage = Stage::attack; }
        void noteOff() noexcept { if (stage != Stage::idle) stage = Stage::release; }
        void reset() noexcept { stage = Stage::idle; level = 0.0f; }

        float next() noexcept
        {
            switch (stage)
            {
                case Stage::idle: return 0.0f;
                case Stage::attack:
                    level += (1.3f - level) * attackCoef;
                    if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                    break;
                case Stage::decay:
                    level += (sustain - level) * decayCoef;
                    if (std::abs (level - sustain) < 1.0e-4f) { level = sustain; stage = Stage::sustain; }
                    break;
                case Stage::sustain:
                    level += (sustain - level) * decayCoef; // follows sustain changes smoothly
                    break;
                case Stage::release:
                    level -= level * releaseCoef;
                    if (level < 1.0e-5f) { level = 0.0f; stage = Stage::idle; }
                    break;
            }
            return level;
        }

        Stage getStage() const noexcept { return stage; }
        float getLevel() const noexcept { return level; }
        bool isActive() const noexcept { return stage != Stage::idle; }

    private:
        Stage stage = Stage::idle;
        float level = 0.0f, attackCoef = 0.01f, decayCoef = 0.001f, releaseCoef = 0.001f, sustain = 1.0f;
    };
}
