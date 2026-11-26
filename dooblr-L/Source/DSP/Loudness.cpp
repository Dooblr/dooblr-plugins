#include "Loudness.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dooblrl::dsp
{
    void LoudnessMeter::prepare (double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        design (sampleRate);
        momentarySamples = std::max (1, (int) std::lround (0.400 * sampleRate));
        shortTermSamples = std::max (1, (int) std::lround (3.000 * sampleRate));
        gateSamples      = std::max (1, (int) std::lround (0.400 * sampleRate));
        momEnergy.assign ((size_t) momentarySamples, 0.0f);
        stEnergy.assign ((size_t) shortTermSamples, 0.0f);
        gatedBlocks.assign (4096, 0.0);
        shortTermHistory.assign (1024, -70.0f);
        reset();
    }

    void LoudnessMeter::design (double sr)
    {
        Biquad shelf {}, rlb {};

        if (std::abs (sr - 48000.0) < 0.5)
        {
            shelf.b0 = 1.53512485958697f;
            shelf.b1 = -2.69169618940638f;
            shelf.b2 = 1.19839281085285f;
            shelf.a1 = -1.69065929318241f;
            shelf.a2 = 0.73248077421585f;
            rlb.b0 = 1.0f;
            rlb.b1 = -2.0f;
            rlb.b2 = 1.0f;
            rlb.a1 = -1.99004745483398f;
            rlb.a2 = 0.99007225036621f;
        }
        else
        {
            // Bilinear warp of the BS.1770 analog prototypes at the session rate.
            constexpr double pi = 3.14159265358979323846;
            const auto k = 2.0 * sr;
            const auto hs = 2.0 * pi * 1681.974450955533;
            const auto g  = std::pow (10.0, 4.0 / 20.0);
            auto blt = [k] (double b0, double b1, double b2, double a0, double a1, double a2)
            {
                const auto bk0 = b0 * k * k + b1 * k + b2;
                const auto bk1 = 2.0 * (b2 - b0 * k * k);
                const auto bk2 = b0 * k * k - b1 * k + b2;
                const auto ak0 = a0 * k * k + a1 * k + a2;
                const auto ak1 = 2.0 * (a2 - a0 * k * k);
                const auto ak2 = a0 * k * k - a1 * k + a2;
                Biquad q;
                q.b0 = (float) (bk0 / ak0);
                q.b1 = (float) (bk1 / ak0);
                q.b2 = (float) (bk2 / ak0);
                q.a1 = (float) (ak1 / ak0);
                q.a2 = (float) (ak2 / ak0);
                return q;
            };
            shelf = blt (1.0, 2.0 * hs, hs * hs * g, 1.0, 2.0 * hs, hs * hs);
            const auto hp = 2.0 * pi * 38.13547087602444;
            rlb = blt (1.0, 0.0, 0.0, 1.0, 2.0 * hp, hp * hp);
        }

        for (auto& c : ch)
        {
            c.shelf = shelf;
            c.rlb = rlb;
        }
    }

    void LoudnessMeter::reset() noexcept
    {
        for (auto& c : ch)
        {
            c.shelf.reset();
            c.rlb.reset();
        }
        std::fill (momEnergy.begin(), momEnergy.end(), 0.0f);
        std::fill (stEnergy.begin(), stEnergy.end(), 0.0f);
        std::fill (gatedBlocks.begin(), gatedBlocks.end(), 0.0);
        std::fill (shortTermHistory.begin(), shortTermHistory.end(), -70.0f);
        momWrite = stWrite = gateCount = 0;
        momAccum = stAccum = gateAccum = 0.0;
        gatedCount = gatedWrite = 0;
        stHistCount = stHistWrite = 0;
        processedSamples = 0;
        momentary = shortTerm = integrated = -70.0f;
        lra = 0.0f;
        haveIntegrated = false;
    }

    float LoudnessMeter::meanSquareToLufs (double sumMs) noexcept
    {
        if (sumMs <= 1.0e-12)
            return -70.0f;
        return (float) (-0.691 + 10.0 * std::log10 (sumMs));
    }

    void LoudnessMeter::process (const float* left, const float* right, int numSamples, int numChannels) noexcept
    {
        if (numSamples <= 0 || momentarySamples <= 0 || momEnergy.empty())
            return;

        const auto stereo = numChannels > 1 && right != nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto lIn = left != nullptr ? left[i] : 0.0f;
            const auto rIn = stereo ? right[i] : lIn;
            const auto l = ch[0].rlb.process (ch[0].shelf.process (lIn));
            const auto r = ch[1].rlb.process (ch[1].shelf.process (rIn));
            const auto energy = (double) l * (double) l + (double) r * (double) r;
            ++processedSamples;

            momAccum += energy - (double) momEnergy[(size_t) momWrite];
            momEnergy[(size_t) momWrite] = (float) energy;
            momWrite = (momWrite + 1) % momentarySamples;

            stAccum += energy - (double) stEnergy[(size_t) stWrite];
            stEnergy[(size_t) stWrite] = (float) energy;
            stWrite = (stWrite + 1) % shortTermSamples;

            gateAccum += energy;
            if (++gateCount >= gateSamples)
                finishBlock();
        }

        momentary = meanSquareToLufs (momAccum / (double) momentarySamples);
        shortTerm = meanSquareToLufs (stAccum  / (double) shortTermSamples);
    }

    void LoudnessMeter::finishBlock() noexcept
    {
        const auto mean = gateAccum / (double) std::max (1, gateSamples);
        gateAccum = 0.0;
        gateCount = 0;
        const auto blockLufs = meanSquareToLufs (mean);

        constexpr float absGateLufs = -70.0f;
        constexpr float relGateDelta = -10.0f;
        constexpr float lraRelGateDelta = -20.0f;

        if (blockLufs > absGateLufs && ! gatedBlocks.empty())
        {
            gatedBlocks[(size_t) gatedWrite] = mean;
            gatedWrite = (gatedWrite + 1) % (int) gatedBlocks.size();
            gatedCount = std::min (gatedCount + 1, (int) gatedBlocks.size());
        }

        if (gatedCount > 0)
        {
            double sum = 0.0;
            for (int i = 0; i < gatedCount; ++i)
            {
                const auto idx = (gatedWrite - gatedCount + i + (int) gatedBlocks.size()) % (int) gatedBlocks.size();
                sum += gatedBlocks[(size_t) idx];
            }
            const auto ungated = meanSquareToLufs (sum / (double) gatedCount);
            const auto rel = ungated + relGateDelta;
            double gsum = 0.0;
            int gcount = 0;
            for (int i = 0; i < gatedCount; ++i)
            {
                const auto idx = (gatedWrite - gatedCount + i + (int) gatedBlocks.size()) % (int) gatedBlocks.size();
                const auto z = gatedBlocks[(size_t) idx];
                if (meanSquareToLufs (z) > rel)
                {
                    gsum += z;
                    ++gcount;
                }
            }
            if (gcount > 0)
            {
                integrated = meanSquareToLufs (gsum / (double) gcount);
                haveIntegrated = true;
            }
        }

        if (! shortTermHistory.empty() && processedSamples >= shortTermSamples)
        {
            shortTermHistory[(size_t) stHistWrite] = meanSquareToLufs (stAccum / (double) shortTermSamples);
            stHistWrite = (stHistWrite + 1) % (int) shortTermHistory.size();
            stHistCount = std::min (stHistCount + 1, (int) shortTermHistory.size());
        }

        if (stHistCount >= 4)
        {
            std::array<float, 1024> valid {};
            int nValid = 0;
            for (int i = 0; i < stHistCount; ++i)
            {
                const auto idx = (stHistWrite - stHistCount + i + (int) shortTermHistory.size()) % (int) shortTermHistory.size();
                const auto s = shortTermHistory[(size_t) idx];
                if (s > absGateLufs && nValid < (int) valid.size())
                    valid[(size_t) nValid++] = s;
            }

            if (nValid >= 4)
            {
                double sum = 0.0;
                for (int i = 0; i < nValid; ++i) sum += (double) valid[(size_t) i];
                const auto ungated = (float) (sum / (double) nValid);
                const auto rel = ungated + lraRelGateDelta;
                int nGated = 0;
                for (int i = 0; i < nValid; ++i)
                    if (valid[(size_t) i] > rel)
                        valid[(size_t) nGated++] = valid[(size_t) i];

                if (nGated >= 2)
                {
                    std::sort (valid.begin(), valid.begin() + nGated);
                    const auto lo = valid[(size_t) std::clamp ((int) std::floor (0.10 * (double) (nGated - 1)), 0, nGated - 1)];
                    const auto hi = valid[(size_t) std::clamp ((int) std::floor (0.95 * (double) (nGated - 1)), 0, nGated - 1)];
                    lra = std::max (0.0f, hi - lo);
                }
            }
        }
    }
}
