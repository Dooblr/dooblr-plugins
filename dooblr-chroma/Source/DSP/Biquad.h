#pragma once

#include <cmath>
#include <algorithm>

namespace dooblrchroma::dsp
{
    /** A reusable, non-nested transposes-form-II biquad. Coefficients follow the
        Audio EQ Cookbook (RBJ) convention: y = b0 x + z1; z1 = b1 x - a1 y + z2; ... */
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() noexcept { z1 = z2 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setIdentity() noexcept
        {
            b0 = 1.0f;
            b1 = b2 = a1 = a2 = 0.0f;
        }

        void setLowpass (float freq, float q, float sampleRate) noexcept
        {
            setRBJ (freq, q, sampleRate, 0);
        }

        void setHighpass (float freq, float q, float sampleRate) noexcept
        {
            setRBJ (freq, q, sampleRate, 1);
        }

        void setBandpass (float freq, float q, float sampleRate) noexcept
        {
            setRBJ (freq, q, sampleRate, 2);
        }

        void setLowshelf (float freq, float gainDb, float sampleRate) noexcept
        {
            setShelf (freq, gainDb, sampleRate, true);
        }

        void setHighshelf (float freq, float gainDb, float sampleRate) noexcept
        {
            setShelf (freq, gainDb, sampleRate, false);
        }

        void setPeaking (float freq, float gainDb, float q, float sampleRate) noexcept
        {
            freq = std::clamp (freq, 10.0f, 0.45f * sampleRate);
            q = std::max (0.1f, q);
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w = 2.0f * 3.14159265358979323846f * freq / sampleRate;
            const float cs = std::cos (w), sn = std::sin (w);
            const float alpha = sn / (2.0f * q);
            const float b0n = 1.0f + alpha * A;
            const float b1n = -2.0f * cs;
            const float b2n = 1.0f - alpha * A;
            const float a0n = 1.0f + alpha / A;
            const float a1n = -2.0f * cs;
            const float a2n = 1.0f - alpha / A;
            b0 = b0n / a0n;
            b1 = b1n / a0n;
            b2 = b2n / a0n;
            a1 = a1n / a0n;
            a2 = a2n / a0n;
        }

    private:
        void setRBJ (float freq, float q, float sampleRate, int kind) noexcept
        {
            freq = std::clamp (freq, 10.0f, 0.45f * sampleRate);
            q = std::max (0.1f, q);
            const float w = 2.0f * 3.14159265358979323846f * freq / sampleRate;
            const float cs = std::cos (w), sn = std::sin (w);
            const float alpha = sn / (2.0f * q);
            float b0n = 0, b1n = 0, b2n = 0, a0n = 1, a1n = 0, a2n = 0;

            if (kind == 0) // LP
            {
                b0n = (1.0f - cs) * 0.5f;
                b1n = 1.0f - cs;
                b2n = (1.0f - cs) * 0.5f;
                a0n = 1.0f + alpha;
                a1n = -2.0f * cs;
                a2n = 1.0f - alpha;
            }
            else if (kind == 1) // HP
            {
                b0n = (1.0f + cs) * 0.5f;
                b1n = -(1.0f + cs);
                b2n = (1.0f + cs) * 0.5f;
                a0n = 1.0f + alpha;
                a1n = -2.0f * cs;
                a2n = 1.0f - alpha;
            }
            else // BP (constant skirt, 0 dB peak)
            {
                b0n = alpha;
                b1n = 0.0f;
                b2n = -alpha;
                a0n = 1.0f + alpha;
                a1n = -2.0f * cs;
                a2n = 1.0f - alpha;
            }

            b0 = b0n / a0n;
            b1 = b1n / a0n;
            b2 = b2n / a0n;
            a1 = a1n / a0n;
            a2 = a2n / a0n;
        }

        void setShelf (float freq, float gainDb, float sampleRate, bool low) noexcept
        {
            freq = std::clamp (freq, 10.0f, 0.45f * sampleRate);
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w = 2.0f * 3.14159265358979323846f * freq / sampleRate;
            const float cs = std::cos (w), sn = std::sin (w);
            const float beta = std::sqrt (A) * sn; // S = 1
            float b0n, b1n, b2n, a0n, a1n, a2n;

            if (low)
            {
                b0n =    A * ((A + 1.0f) - (A - 1.0f) * cs + beta);
                b1n =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
                b2n =    A * ((A + 1.0f) - (A - 1.0f) * cs - beta);
                a0n =        (A + 1.0f) + (A - 1.0f) * cs + beta;
                a1n =   -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
                a2n =        (A + 1.0f) + (A - 1.0f) * cs - beta;
            }
            else
            {
                b0n =    A * ((A + 1.0f) + (A - 1.0f) * cs + beta);
                b1n = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
                b2n =    A * ((A + 1.0f) + (A - 1.0f) * cs - beta);
                a0n =        (A + 1.0f) - (A - 1.0f) * cs + beta;
                a1n =    2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
                a2n =        (A + 1.0f) - (A - 1.0f) * cs - beta;
            }

            b0 = b0n / a0n;
            b1 = b1n / a0n;
            b2 = b2n / a0n;
            a1 = a1n / a0n;
            a2 = a2n / a0n;
        }
    };
}
