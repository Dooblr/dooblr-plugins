#include "Modes.h"

#include <algorithm>
#include <vector>

namespace dooblrchroma::dsp
{
    namespace
    {
        // Free-free Euler-Bernoulli bar: roots of cos(kL) cosh(kL) = 1.
        constexpr float kBarKL[8] = {
            4.73004074f, 7.85320462f, 10.99560784f, 14.13716549f,
            17.27875966f, 20.42035225f, 23.56194490f, 26.70353756f
        };

        // Circular membrane (drumhead) Bessel zeros, (d,c) ordered by frequency, / first zero.
        // First 40 published ratios of a uniform circular membrane.
        constexpr float kDrumhead[40] = {
            1.0000f, 1.5933f, 2.1355f, 2.2954f, 2.6531f,
            2.9173f, 3.1555f, 3.5001f, 3.5985f, 3.6475f,
            4.0091f, 4.0589f, 4.1313f, 4.2304f, 4.5257f,
            4.6098f, 4.8319f, 4.9728f, 5.0618f, 5.1316f,
            5.2536f, 5.4167f, 5.5294f, 5.6511f, 5.7510f,
            5.8770f, 6.0927f, 6.1526f, 6.3210f, 6.4159f,
            6.5271f, 6.6978f, 6.8140f, 6.9538f, 7.0675f,
            7.1990f, 7.3390f, 7.4327f, 7.5798f, 7.7345f
        };

        // Standard J0, bounded in roughly [-0.4, 1]. (libc++ has no std::cyl_bessel_j.)
        float besselJ0 (float x) noexcept
        {
            x = std::abs (x);
            if (x <= 8.0f)
            {
                const float y = x * x;
                return 1.0f + y * (-0.25f
                     + y * (0.015625f
                     + y * (-4.3402778e-4f
                     + y * (6.7816840e-6f
                     + y * (-6.7816840e-8f
                     + y * 4.7095028e-10f)))));
            }
            const float p = 0.785398163f - x; // pi/4 - x
            const float amp = std::sqrt (0.636619772f / x); // sqrt(2/(pi x))
            return amp * std::cos (p);
        }

        void fillRectangular (int countWanted, float* ratios, bool bending)
        {
            // Square plate (bending, f ~ k^2) or square membrane (f ~ k), simply supported.
            // Unique (m,n) with m,n >= 1, sorted, degenerates kept once.
            std::vector<float> raw;
            raw.reserve (128);
            for (int m = 1; m <= 20; ++m)
                for (int n = m; n <= 20; ++n) // n>=m so each pair once
                {
                    const float k2 = (float) (m * m + n * n);
                    raw.push_back (bending ? (k2 / 2.0f) : std::sqrt (k2 / 2.0f));
                }
            std::sort (raw.begin(), raw.end());
            // Drop exact duplicates (already unique by n>=m, but (m,n) != (n,m) only when m!=n;
            // we already kept each pair once, which is the right spectrum for a symmetric hit).
            const int count = std::min (countWanted, (int) raw.size());
            for (int i = 0; i < count; ++i)
                ratios[i] = raw[(size_t) i];
        }
    }

    int modeCount (ResType type, int densityIndex, float fundamentalHz, float sampleRate) noexcept
    {
        densityIndex = std::clamp (densityIndex, 0, 3);
        const int density = kDensityCounts[densityIndex];
        const float nyquist = 0.49f * sampleRate;

        if (type == ResType::manual)
            return std::clamp (densityIndex + 1, 1, kMaxManualPartials);

        int n = density;
        if (isTube (type))
        {
            // Tubes use every mode up to Nyquist, capped at kMaxModes.
            if (type == ResType::openTube)
                n = (fundamentalHz > 1.0f) ? (int) std::floor (nyquist / fundamentalHz) : kMaxModes;
            else
                n = (fundamentalHz > 1.0f) ? (int) std::floor ((nyquist / fundamentalHz + 1.0f) * 0.5f) : kMaxModes;
            n = std::clamp (n, 1, kMaxModes);
            return n;
        }

        return std::clamp (n, 1, kMaxModes);
    }

    int fillRatios (ResType type, int n, float* ratios, const float* manualRanks) noexcept
    {
        n = std::clamp (n, 0, kMaxModes);
        if (n <= 0)
            return 0;

        switch (type)
        {
            case ResType::string:
            case ResType::openTube:
                for (int i = 0; i < n; ++i)
                    ratios[i] = (float) (i + 1);
                break;

            case ResType::closedTube:
                for (int i = 0; i < n; ++i)
                    ratios[i] = (float) (2 * i + 1);
                break;

            case ResType::bar:
                for (int i = 0; i < n; ++i)
                {
                    const float kl = (i < 8) ? kBarKL[i]
                                             : ((2.0f * (float) i + 1.5f) * 3.14159265358979323846f * 0.5f);
                    ratios[i] = (kl / kBarKL[0]) * (kl / kBarKL[0]);
                }
                break;

            case ResType::marimba:
                // Undercut bar: first overtone at 4x, second near 9.3x, then ~n^2.
                for (int i = 0; i < n; ++i)
                {
                    if (i == 0)      ratios[i] = 1.0f;
                    else if (i == 1) ratios[i] = 4.0f;
                    else if (i == 2) ratios[i] = 9.3f;
                    else             ratios[i] = (float) ((i + 1) * (i + 1)) * (16.4f / 16.0f);
                }
                break;

            case ResType::plate:
                fillRectangular (n, ratios, true);
                break;

            case ResType::membrane:
                fillRectangular (n, ratios, false);
                break;

            case ResType::drumhead:
                for (int i = 0; i < n; ++i)
                {
                    if (i < 40)
                        ratios[i] = kDrumhead[i];
                    else
                        ratios[i] = kDrumhead[39] + 0.18f * (float) (i - 39);
                }
                break;

            case ResType::manual:
                for (int i = 0; i < n; ++i)
                {
                    const float rank = (manualRanks != nullptr) ? manualRanks[i] : (float) (i + 1);
                    ratios[i] = std::max (0.25f, rank);
                }
                break;
        }

        return n;
    }

    float hitAmplitude (ResType type, int modeIndex, float ratio, float position) noexcept
    {
        const float pos = std::clamp (position, 0.0f, 1.0f);
        // Border -> centre maps to a physical coordinate in (epsilon, 0.5].
        const float x = 0.02f + pos * 0.48f;

        switch (type)
        {
            case ResType::string:
            case ResType::openTube:
            case ResType::manual:
            {
                const float n = (type == ResType::manual) ? ratio : (float) (modeIndex + 1);
                return std::sin (n * 3.14159265358979323846f * x);
            }
            case ResType::closedTube:
            {
                const float harmonic = (float) (2 * modeIndex + 1);
                return std::cos (harmonic * 3.14159265358979323846f * x * 0.5f);
            }
            case ResType::bar:
            case ResType::marimba:
            {
                // Approximate free-free / undercut beam shape with a raised sinusoid.
                const float kn = 3.14159265358979323846f * (float) (modeIndex + 1);
                return std::sin (kn * x) + 0.15f * std::sin (2.0f * kn * x);
            }
            case ResType::plate:
            case ResType::membrane:
            {
                // Strike along the diagonal of a square, mode (approx by index).
                const int m = 1 + modeIndex / 4;
                const int n = 1 + (modeIndex % 4);
                return std::sin ((float) m * 3.14159265358979323846f * x)
                     * std::sin ((float) n * 3.14159265358979323846f * x);
            }
            case ResType::drumhead:
            {
                // Radial position: centre = 0, edge = 1. J0 stays in [-0.4, 1].
                const float r = 1.0f - pos;
                const float arg = std::min (2.4f * ratio * r, 20.0f);
                return besselJ0 (arg);
            }
        }
        return 1.0f;
    }
}
