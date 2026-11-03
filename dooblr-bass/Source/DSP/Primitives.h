#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace dooblrbass::dsp
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

    inline float softClipSlope (float x) noexcept
    {
        if (x > 3.0f || x < -3.0f) return 0.0f;
        const auto x2 = x * x;
        const auto a = 9.0f - x2, b = 27.0f + 9.0f * x2;
        return 9.0f * a * a / (b * b);
    }

    /** Saturator with a level: L * softClip (x / L); level <= 0 means linear. */
    inline float saturate (float x, float level) noexcept { return level > 0.0f ? level * softClip (x / level) : x; }

    inline float semitonesToHz (float midiNote) noexcept { return 440.0f * std::exp2 ((midiNote - 69.0f) * (1.0f / 12.0f)); }

    /** Small deterministic PRNG (xorshift32): noise, S&H. Real-time safe. */
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
        t is the time since the discontinuity in samples, in [0, 1). */
    inline float blepNow (float t) noexcept    { const auto u = 1.0f - t; return -0.5f * u * u; }
    inline float blepBefore (float t) noexcept { return 0.5f * t * t; }
    inline float blampNow (float t) noexcept   { const auto u = 1.0f - t; return u * u * u * (1.0f / 6.0f); }
    inline float blampBefore (float t) noexcept { return t * t * t * (1.0f / 6.0f); }

    namespace model
    {
        /** Analog VCO ramp core. f_eff = 1 / (1/f_ideal + t_r); ~2 us is about 17 cents
            flat at 5 kHz, the classic analog high-frequency droop from a finite reset. */
        inline constexpr float rampResetSeconds = 2.0e-6f;

        /** Early-effect bow on the charging transistor: the saw is slightly concave,
            phase' = phase + k * phase * (phase - 1). Audio-rate only; the comparator (pulse)
            and triangle fold still use linear phase. */
        inline constexpr float earlyEffect = 0.11f;

        /** Cubic warp of the expo converter (cents-scale): a hair sharp in the bass,
            a hair flat on top. Kept tiny so 1 V/oct tracking stays within a few cents. */
        inline constexpr float expoCubic = 0.35f;

        /** RC on the expo-converter base (pitch CV after glide, cutoff CV, VCA control). */
        inline constexpr float pitchExpoSeconds = 3.5e-4f;
        inline constexpr float cutoffExpoSeconds = 1.2e-3f;
        inline constexpr float vcaExpoSeconds = 4.0e-4f;

        /** Virtual-earth mixer: each channel through a resistor into an inverting summer
            that saturates as the summing node is pulled off 0 V. Drive lowers the headroom
            (more current into the node). Overload is the filter output fed back into the same
            node, the classic “everything up” path into the ladder. */
        inline constexpr float mixerHeadroom = 1.55f;
        inline float mixerGain (float drive) noexcept { return 0.85f + 3.4f * std::clamp (drive, 0.0f, 1.0f); }
        inline float virtualEarth (float sum, float drive) noexcept
        {
            return saturate (sum * mixerGain (drive), mixerHeadroom);
        }

        /** OTA VCA: Iout ~= Iabc * tanh(Vin / (2 Vt)). Closed, a little current still leaks
            (analog VCAs never fully shut). Headroom sets how hard the pair folds. */
        inline constexpr float vcaBleed = 0.0018f;
        inline constexpr float vcaHeadroom = 1.35f;

        /** 4-pole transistor ladder. Linear core H(s) = 1 / ((1 + s/wc)^4 + k), self-oscillation
            from k = 4, DC gain 1 / (1 + k) — no bass-loss compensation, that dip is the circuit.
            Each stage is a transistor pair (tanh) around a TPT integrator; the input pair is the
            differential cell that subtracts the resonating current; the output pair is slightly
            biased so even harmonics appear. Feedback is solved zero-delay (Newton). */
        struct LadderModel
        {
            float kMax;           // feedback at full resonance (self-oscillation from k = 4)
            float resoTaper;      // k = kMax * reso^taper
            float inputLevel;     // input differential pair
            float resonanceLevel; // tanh-like resonance cell (sets self-osc amplitude)
            float stageLevel;     // per-stage transistor pair
            float outputLevel;    // output pair clip
            float outputBias;     // DC offset on the output pair -> even harmonics
        };

        inline constexpr LadderModel transistorLadder { 4.25f, 1.15f, 1.55f, 1.15f, 0.72f, 2.2f, 0.18f };

        inline float ladderK (float resonance) noexcept
        {
            return transistorLadder.kMax * std::pow (std::clamp (resonance, 0.0f, 1.0f), transistorLadder.resoTaper);
        }

        inline constexpr int newtonIterations = 2;
        inline constexpr float thermalNoise = 1.5e-5f;
        inline constexpr int oversample = 2;

        /** Expo converter: Hz from a MIDI-note CV, with the cubic warp of a real pair. */
        inline float expoHz (float midiNote) noexcept
        {
            const auto t = (midiNote - 48.0f) * (1.0f / 48.0f);
            return semitonesToHz (midiNote + expoCubic * t * t * t);
        }
    }

    /** One-pole CV lag (the capacitor on an expo base or an OTA control pin). */
    struct CvLag
    {
        float z = 0.0f;
        bool primed = false;

        void reset (float x = 0.0f) noexcept { z = x; primed = false; }

        float process (float x, float coef) noexcept
        {
            if (! primed)
            {
                z = x;
                primed = true;
            }
            else
            {
                z += (x - z) * coef;
            }
            return z;
        }
    };

    /** OTA VCA: control-current lag, residual bleed, tanh pair. */
    class OtaVca
    {
    public:
        void reset() noexcept { lag.reset(); }

        float process (float x, float control, float coef) noexcept
        {
            const auto gm = std::max (0.0f, lag.process (control, coef)) + model::vcaBleed;
            return saturate (x * gm, model::vcaHeadroom);
        }

    private:
        CvLag lag;
    };

    /** Waveform weights: triangle, saw and pulse are summed. */
    struct WaveWeights
    {
        float tri = 0.0f, saw = 1.0f, pulse = 0.0f;

        static WaveWeights forSaw() noexcept      { return { 0.0f, 1.0f, 0.0f }; }
        static WaveWeights forSquare() noexcept   { return { 0.0f, 0.0f, 1.0f }; }
        static WaveWeights forTriangle() noexcept { return { 0.5f, 0.0f, 0.0f }; } // analog triangle is quieter than saw
    };

    /**
        Ramp-core analog VCO: a rising saw with a finite reset (see model::rampResetSeconds);
        the charging transistor's Early effect bows the ramp; the triangle is a fold of the
        linear phase; the pulse is a comparator on that phase. Steps use polyBLEP, slope
        changes polyBLAMP. The output is one sample late so corrections can reach the sample
        before each event.
    */
    class Oscillator
    {
    public:
        void reset (float startPhase = 0.0f) noexcept { phase = startPhase; held = 0.0f; }

        float process (float inc, float pulseWidth, const WaveWeights& w, float& wrapT) noexcept
        {
            inc = std::clamp (inc, 1.0e-7f, 0.45f);
            const auto pw = std::clamp (pulseWidth, 0.02f, 0.98f);
            wrapT = -1.0f;
            float now = 0.0f, before = 0.0f;

            const auto previous = phase;
            auto next = phase + inc;
            constexpr float triPeak = 0.5f;
            const auto triSlopeChange = 2.0f / (triPeak * (1.0f - triPeak)) * inc * w.tri;

            if (previous < pw && next >= pw)
                addEvents ((next - pw) / inc, -2.0f * w.pulse, 0.0f, now, before);

            if (previous < triPeak && next >= triPeak)
                addEvents ((next - triPeak) / inc, 0.0f, -triSlopeChange, now, before);

            if (next >= 1.0f)
            {
                next -= 1.0f;
                const auto t = next / inc;
                addEvents (t, 2.0f * w.pulse - 2.0f * w.saw, triSlopeChange, now, before);
                wrapT = t;

                if (next >= pw)
                    addEvents ((next - pw) / inc, -2.0f * w.pulse, 0.0f, now, before);
                if (next >= triPeak)
                    addEvents ((next - triPeak) / inc, 0.0f, -triSlopeChange, now, before);
            }

            phase = next;
            const auto current = naive (phase, pw, w) + now;
            const auto out = held + before;
            held = current;
            return out;
        }

        float getPhase() const noexcept { return phase; }

        static float naive (float p, float pw, const WaveWeights& w) noexcept
        {
            constexpr float a = 0.5f;
            const auto bowed = p + model::earlyEffect * p * (p - 1.0f);
            const auto tri = p < a ? -1.0f + 2.0f * p / a : 1.0f - 2.0f * (p - a) / (1.0f - a);
            const auto saw = 2.0f * bowed - 1.0f;
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

        float phase = 0.0f, held = 0.0f;
    };

    /** Square one or two octaves below its parent: a flip-flop toggled on each parent wrap. */
    class SubOscillator
    {
    public:
        void reset() noexcept { high = true; held = 0.0f; divide = false; }

        /** parentWrapT: time since the parent's wrap in samples, or < 0 for none.
            twoOctaves: toggle only on every other parent wrap. */
        float process (float parentWrapT, bool twoOctaves) noexcept
        {
            float now = 0.0f, before = 0.0f;
            if (parentWrapT >= 0.0f)
            {
                if (twoOctaves)
                {
                    divide = ! divide;
                    if (! divide)
                    {
                        const auto current = (high ? 1.0f : -1.0f);
                        const auto out = held;
                        held = current;
                        return out;
                    }
                }

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
        bool high = true, divide = false;
        float held = 0.0f;
    };

    /**
        4-pole transistor ladder (the Moog-style differential-pair cascade), zero-delay.
        Linear core H(s) = 1 / ((1 + s/wc)^4 + k). Circuit pieces: input pair, four tanh
        stages, saturating resonance cell solved with Newton from the linear guess, biased
        output pair. No DC-gain compensation — bass loss at high k is the ladder. Oversample
        this (model::oversample).
    */
    class LadderFilter
    {
    public:
        void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.0f; }

        void prepare() noexcept
        {
            const auto& m = model::transistorLadder;
            outputNorm = 1.0f / softClipSlope (m.outputBias);
            outputOffset = softClip (m.outputBias);
        }

        /** w: pi * fc / fs (fs = the rate the filter runs at). k: feedback, 0..kMax. */
        void setCoefficients (float w, float k) noexcept
        {
            w = std::clamp (w, 1.0e-5f, 1.2f);
            const auto g = std::tan (w);
            const auto Gi = g / (1.0f + g);
            for (int i = 0; i < 4; ++i)
                G[i] = Gi;
            feedback = std::max (0.0f, k);
            Gp = G[0] * G[1] * G[2] * G[3];
        }

        float process (float x) noexcept
        {
            const auto& mdl = model::transistorLadder;
            // Input differential pair: the summing node of the ladder (virtual earth of the
            // four transistor stages looking back at the mixer).
            const auto in = saturate (x, mdl.inputLevel);
            const auto S = G[1] * G[2] * G[3] * (1.0f - G[0]) * s[0] + G[2] * G[3] * (1.0f - G[1]) * s[1]
                         + G[3] * (1.0f - G[2]) * s[2] + (1.0f - G[3]) * s[3];

            auto y = (Gp * in + S) / (1.0f + feedback * Gp);
            const auto level = mdl.resonanceLevel;
            for (int n = 0; n < model::newtonIterations; ++n)
            {
                const auto f = y - Gp * (in - feedback * saturate (y, level)) - S;
                const auto slope = 1.0f + Gp * feedback * softClipSlope (y / level);
                y -= f / std::max (slope, 1.0e-6f);
            }

            // Resonating current back into the input pair, then four transistor-pair stages.
            auto u = in - feedback * saturate (y, level);
            for (int i = 0; i < 4; ++i)
            {
                const auto pair = saturate (u, mdl.stageLevel);
                const auto v = (pair - s[i]) * G[i];
                const auto out = v + s[i];
                s[i] = out + v;
                u = out;
            }

            const auto L = mdl.outputLevel;
            return L * (softClip (u / L + mdl.outputBias) - outputOffset) * outputNorm;
        }

    private:
        float s[4] {}, G[4] { 0.5f, 0.5f, 0.5f, 0.5f };
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
                    const auto n = 2 * i + 1;
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
        static constexpr int numSide = (numTaps - 1) / 4 + 1;

        void push (float v) noexcept
        {
            history[pos] = v;
            history[pos + numTaps] = v;
            pos = pos + 1 == numTaps ? 0 : pos + 1;
        }

        float history[2 * numTaps] {};
        int pos = 0;
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
            attackCoef = coef (attack, 1.466f);
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
                    level += (sustain - level) * decayCoef;
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
