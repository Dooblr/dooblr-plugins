#include "TruePeak.h"

#include <algorithm>

namespace dooblrl::dsp
{
    float linearToDb (float linear) noexcept
    {
        if (! (linear > 1.0e-12f))
            return -120.0f;
        return 20.0f * std::log10 (linear);
    }

    float dbToLinear (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    void TruePeak::design()
    {
        // 48-tap low-pass at the 4x rate, cutoff at original Nyquist (0.125 of 4x).
        constexpr double pi = 3.14159265358979323846;
        const auto fc = 0.5 / (double) factor;
        const auto mid = 0.5 * (double) (taps - 1);

        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const auto n = (double) i - mid;
            const auto sinc = std::abs (n) < 1.0e-12 ? 2.0 * fc : std::sin (2.0 * pi * fc * n) / (pi * n);
            const auto w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (taps - 1));
            fir[(size_t) i] = (float) (sinc * w);
            sum += (double) fir[(size_t) i];
        }

        if (sum > 1.0e-12)
        {
            const auto scale = (float) ((double) factor / sum);
            for (auto& c : fir)
                c *= scale;
        }

        designed = true;
    }

    void TruePeak::prepare (double)
    {
        if (! designed)
            design();
        reset();
    }

    void TruePeak::reset() noexcept
    {
        for (auto& c : z)
            c.fill (0.0f);
        write = { 0, 0 };
    }

    float TruePeak::peakFromRing (const float* ring, int ringSize, int newest) const noexcept
    {
        if (ring == nullptr || ringSize < tapsPerPhase)
            return 0.0f;
        if (! designed)
            return std::abs (ring[(newest % ringSize + ringSize) % ringSize]);

        float peak = 0.0f;
        for (int phase = 0; phase < factor; ++phase)
        {
            double acc = 0.0;
            for (int t = 0; t < tapsPerPhase; ++t)
            {
                const auto zi = (newest - t + ringSize * 16) % ringSize;
                acc += (double) fir[(size_t) (t * factor + phase)] * (double) ring[zi];
            }
            peak = std::max (peak, (float) std::abs (acc));
        }
        return peak;
    }

    float TruePeak::pushAndTruePeak (int channel, float sample) noexcept
    {
        channel = channel < 0 ? 0 : (channel > 1 ? 1 : channel);
        auto& buf = z[(size_t) channel];
        auto& w = write[(size_t) channel];
        buf[(size_t) w] = sample;

        const auto peak = peakFromRing (buf.data(), tapsPerPhase, w);
        w = (w + 1) % tapsPerPhase;
        return peak;
    }

    float TruePeak::measureBuffer (const float* data, int numSamples, int channel)
    {
        reset();
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max (peak, pushAndTruePeak (channel, data[i]));
        return peak;
    }
}
