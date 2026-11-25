#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dooblrl::dsp
{
    /** ITU-R BS.1770-4 / EBU R128 K-weighted loudness and EBU Tech 3342 LRA.

        Momentary (400 ms), short-term (3 s), gated integrated, and loudness range.
        Coefficients: published 48 kHz biquads, bilinear-warped at other rates.
        Call reset() from the meter Reset control (and on prepare).
    */
    class LoudnessMeter
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        void process (const float* left, const float* right, int numSamples, int numChannels) noexcept;

        float momentaryLufs() const noexcept { return momentary; }
        float shortTermLufs() const noexcept { return shortTerm; }
        float integratedLufs() const noexcept { return integrated; }
        float loudnessRange() const noexcept { return lra; }
        bool hasIntegrated() const noexcept { return haveIntegrated; }
        bool shortTermReady() const noexcept { return processedSamples >= shortTermSamples && shortTermSamples > 0; }

    private:
        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            float z1 = 0, z2 = 0;

            float process (float x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }

            void reset() noexcept { z1 = z2 = 0; }
        };

        struct Channel
        {
            Biquad shelf, rlb;
        };

        void design (double sampleRate);
        void finishBlock() noexcept;
        static float meanSquareToLufs (double sumMs) noexcept;

        Channel ch[2];
        double sampleRate = 48000.0;
        int momentarySamples = 0, shortTermSamples = 0, gateSamples = 0;
        int momWrite = 0, stWrite = 0, gateCount = 0;
        std::vector<float> momEnergy, stEnergy;
        double momAccum = 0.0, stAccum = 0.0, gateAccum = 0.0;
        std::vector<double> gatedBlocks;      // 400 ms blocks that passed the absolute gate (ring)
        std::vector<float> shortTermHistory;  // 3 s windows for LRA (ring)
        int gatedCount = 0, gatedWrite = 0, stHistCount = 0, stHistWrite = 0;

        int processedSamples = 0;
        float momentary = -70.0f, shortTerm = -70.0f, integrated = -70.0f, lra = 0.0f;
        bool haveIntegrated = false;
    };
}
