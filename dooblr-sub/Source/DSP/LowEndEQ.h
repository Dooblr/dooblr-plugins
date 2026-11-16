#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    /**
        Three-band low-end EQ: a low shelf plus two peaking bands, focused on 20–250 Hz.
        At 0 dB on every band the process() path is a no-op (bit-identical). Gains are
        smoothed so a Fix (or a host automation jump) does not click.
    */
    struct LowEndEQSettings
    {
        float shelfDb = 0.0f;
        float shelfHz = 32.0f;
        float peakDb = 0.0f;
        float peakHz = 90.0f;
        float peakQ = 1.0f;
        float presenceDb = 0.0f;
        float presenceHz = 160.0f;
        float presenceQ = 1.0f;
    };

    class LowEndEQ
    {
    public:
        void prepare (double sampleRateToUse);
        void reset() noexcept;
        void setSettings (const LowEndEQSettings& s) noexcept { target = s; }

        void process (float& left, float& right) noexcept;
        bool isNeutral() const noexcept;

    private:
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;

            float tick (float x, float& z1, float& z2) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }

            void process (float& l, float& r) noexcept
            {
                l = tick (l, z1L, z2L);
                r = tick (r, z1R, z2R);
            }

            void reset() noexcept { z1L = z2L = z1R = z2R = 0.0f; }

            void identity() noexcept
            {
                b0 = 1.0f;
                b1 = b2 = a1 = a2 = 0.0f;
            }

            void setLowShelf (float hz, float gainDb, double sr) noexcept;
            void setPeak (float hz, float q, float gainDb, double sr) noexcept;
        };

        void tickSmoothers() noexcept;
        void updateCoeffs() noexcept;

        double sampleRate = 44100.0;
        LowEndEQSettings target, smoothed;
        Biquad shelf, peak, presence;
        int coeffHop = 0;
        float smoothA = 0.02f;
        bool engaged = false;
    };
}
