#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Curves.h"
#include "RoomModels.h"

namespace dooblrreverb::dsp
{
    /**
        The reverb: predelay, early reflections, input diffusion and a modulated 16-line feedback
        delay network, then stereo width and the Post EQ. Wet signal only (the processor mixes).

        Late reverb: two 8-line halves (left / right), each mixed by an 8x8 Hadamard matrix, joined
        by a rotation whose angle is the Stereo Width coupling (0 = two independent "multi-mono"
        reverbs, pi/4 = one fully coupled 16-line network). Every operation in the loop is
        orthogonal, so only the absorption filters set the decay.

        Frequency-dependent decay (the Decay Rate EQ, Brightness and the room model): the target
        RT60 is evaluated at 19 half-octave points from 31 Hz to 16 kHz; each line gets a broadband
        gain plus a 10-band octave graphic EQ (TPT bells) whose dB gains are proportional to the
        line's length, fitted by regularised least squares (Jot / Valimaki-Liski style). A guard
        on a fine frequency grid keeps every line's loop gain below unity at all frequencies.

        Parameters glide (Space over ~250 ms, so the room morphs without clicks; delay lengths
        change continuously through 3rd-order Lagrange reads). Zero latency. Real-time safe:
        everything is allocated in prepare().
    */
    class ReverbEngine
    {
    public:
        static constexpr int numLines = 16, halfLines = 8, numGeqBands = 10, numFitPoints = 19, numGuardPoints = 64,
                             numErTaps = 24, numDiffusers = 4, controlBlock = 32;

        struct Settings
        {
            float space = 45.0f, decayRate = 100.0f, brightness = 50.0f, character = 25.0f, distance = 30.0f,
                  width = 100.0f, predelayMs = 0.0f;
            DecayBands decay {};
            EqBands eq {};
        };

        ReverbEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;
        void setSettings (const Settings& s) noexcept { target = s; }

        /** Writes the wet signal for numSamples (any length; inputs and outputs must not alias). */
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

        double getSampleRate() const noexcept { return sampleRate; }
        /** The longest RT60 in use right now (any frequency), for the host's tail length. */
        double getLongestDecaySeconds() const noexcept { return longestDecay.load (std::memory_order_relaxed); }

        /** The decay time the Space readout shows: the room's time x Decay Rate. */
        static double decaySecondsFor (float space, float decayRate) noexcept;
        /** The RT60 the engine aims for at a frequency (Decay Rate EQ, Brightness and room included). */
        static double targetDecaySeconds (const Settings& s, double freq) noexcept;
        /** log2 of the decay multiplier from Brightness and the room model (no EQ bands). */
        static double toneLog2 (float space, float brightness, double freq) noexcept;

        /** Fit quality, for tests: the line response (dB per sample of delay) the absorption
            filters actually produce at a frequency for the current settings. */
        double absorptionDbPerSample (double freq) const noexcept;

    private:
        struct Smoothed
        {
            float space = 45.0f, decayRate = 100.0f, brightness = 50.0f, character = 25.0f, distance = 30.0f,
                  width = 100.0f, predelaySamples = 0.0f;
            std::array<float, numFitPoints> curveLog2 {};
        };

        struct EqState
        {
            float freqLog = 0.0f, gain = 0.0f, q = 0.71f, mix = 0.0f;
            params::EqShape shape = params::EqShape::bell;
            int slope = 1;
            std::array<SvfCoeffs, 4> coeffs {};
            std::array<std::array<Svf, 4>, 2> state {};
        };

        void control (int numSamples, bool snap) noexcept;
        void computeBandCurve() noexcept;
        static inline float readLagrange (const float* buffer, int mask, int writePos, float delay) noexcept;
        static inline float readLinear (const float* buffer, int mask, int writePos, float delay) noexcept;

        double sampleRate = 48000.0;
        Settings target;
        Smoothed now;
        bool needsSnap = true;
        DecayBands lastBands {};
        bool bandsValid = false;
        std::array<float, numFitPoints> bandCurveLog2 {};

        // Late network.
        std::array<std::vector<float>, numLines> lines;
        int lineMask = 0, linePos = 0;
        std::array<float, numLines> lineLength {}, lfoSin {}, lfoCos {}, lfoRotSin {}, lfoRotCos {};
        std::array<float, numLines> lineGain {};
        std::array<std::array<SvfCoeffs, numGeqBands>, numLines> geq {};
        std::array<std::array<Svf, numGeqBands>, numLines> geqState {};
        float modDepth = 0.0f, rotCos = 1.0f, rotSin = 0.0f, lateGain = 0.5f;
        std::array<float, numGeqBands> geqG {};
        std::array<double, numFitPoints> fitFreq {};
        std::array<std::array<double, numFitPoints>, numGeqBands + 1> solve {};   // regularised pseudo-inverse
        std::array<std::array<double, numGuardPoints>, numGeqBands + 1> guardBasis {}; // dB response per unit gain
        std::array<double, numGeqBands + 1> fitted {};                             // dB per sample of delay
        static constexpr double geqQ = 0.75;

        // Predelay + early reflections + diffusion.
        std::array<std::vector<float>, 2> pre;
        int preMask = 0, prePos = 0;
        std::array<std::array<float, numErTaps>, 2> erTime {}, erTapGain {};
        std::array<std::array<float, numErTaps>, 2> erOffset {}, erGain {};
        float lateOffset = 0.0f, erLowpassCoeff = 0.5f, erFeed = 0.35f, sideIn = 1.0f, sideOut = 1.0f;
        std::array<float, 2> erLowpass {};
        std::array<std::array<std::vector<float>, numDiffusers>, 2> diffusers;
        std::array<std::array<int, numDiffusers>, 2> diffuserPos {};
        float diffusion = 0.65f;
        SvfCoeffs inputTone;
        std::array<Svf, 2> inputToneState {};

        // Post EQ.
        std::array<EqState, params::numEqBands> eq {};

        std::atomic<double> longestDecay { 2.0 };
        int controlCountdown = 0;
    };
}
