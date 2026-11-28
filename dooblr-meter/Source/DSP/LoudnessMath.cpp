#include "LoudnessMath.h"

#include <algorithm>
#include <cmath>

#include "KWeighting.h"

namespace dooblrmeter::dsp
{
    namespace
    {
        /** Prefix sums so every window is O(1). Empty slots counted separately. */
        struct Prefix
        {
            std::vector<double> e;
            std::vector<long long> n;
            std::vector<int> empty;

            explicit Prefix (const SlotEnergy* s, size_t count)
                : e (count + 1, 0.0), n (count + 1, 0), empty (count + 1, 0)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    e[i + 1] = e[i] + s[i].energy;
                    n[i + 1] = n[i] + s[i].n;
                    empty[i + 1] = empty[i] + (s[i].n > 0 ? 0 : 1);
                }
            }

            /** Mean square of slots [a, b); negative if any slot is empty. */
            double mean (size_t a, size_t b) const
            {
                if (empty[b] - empty[a] > 0)
                    return -1.0;
                const auto count = n[b] - n[a];
                return count > 0 ? (e[b] - e[a]) / (double) count : -1.0;
            }
        };

        double percentile (std::vector<double>& v, double p)
        {
            // Linear interpolation between closest ranks (as most R128 implementations).
            std::sort (v.begin(), v.end());
            const auto pos = p * (double) (v.size() - 1);
            const auto lo = (size_t) std::floor (pos);
            const auto hi = std::min (v.size() - 1, lo + 1);
            const auto t = pos - (double) lo;
            return v[lo] * (1.0 - t) + v[hi] * t;
        }
    }

    LoudnessResult computeLoudness (const SlotEnergy* slots, size_t count)
    {
        LoudnessResult r;
        if (count < 4)
            return r;

        const Prefix prefix (slots, count);
        const double absGate = lufsToMeanSquare (-70.0);

        // Integrated: 400 ms blocks.
        std::vector<double> blocks;
        blocks.reserve (count);
        for (size_t i = 0; i + 4 <= count; ++i)
        {
            const auto ms = prefix.mean (i, i + 4);
            if (ms > absGate)
                blocks.push_back (ms);
        }

        if (! blocks.empty())
        {
            double sum = 0.0;
            for (auto b : blocks)
                sum += b;
            const auto relGate = lufsToMeanSquare (meanSquareToLufs (sum / (double) blocks.size()) - 10.0);
            double gated = 0.0;
            size_t gatedCount = 0;
            for (auto b : blocks)
                if (b > relGate)
                {
                    gated += b;
                    ++gatedCount;
                }
            if (gatedCount > 0)
            {
                r.integrated = meanSquareToLufs (gated / (double) gatedCount);
                r.hasIntegrated = true;
                r.relativeGate = meanSquareToLufs (relGate);
            }
        }

        // Loudness range: 3 s short-term windows.
        std::vector<double> st;
        for (size_t i = 0; i + 30 <= count; ++i)
        {
            const auto ms = prefix.mean (i, i + 30);
            if (ms > absGate)
                st.push_back (ms);
        }
        if (! st.empty())
        {
            double sum = 0.0;
            for (auto v : st)
                sum += v;
            const auto relGate = lufsToMeanSquare (meanSquareToLufs (sum / (double) st.size()) - 20.0);
            std::vector<double> gated;
            gated.reserve (st.size());
            for (auto v : st)
                if (v > relGate)
                    gated.push_back (meanSquareToLufs (v));
            if (gated.size() >= 2)
            {
                auto copy = gated;
                r.lra = percentile (copy, 0.95) - percentile (gated, 0.10);
                r.hasLra = true;
            }
        }
        return r;
    }

    double windowLoudness (const SlotEnergy* slots, size_t count, size_t last, int window)
    {
        if (count == 0 || last >= count || (size_t) window > last + 1)
            return -150.0;
        double e = 0.0;
        long long n = 0;
        for (size_t i = last + 1 - (size_t) window; i <= last; ++i)
        {
            if (slots[i].n <= 0)
                return -150.0;
            e += slots[i].energy;
            n += slots[i].n;
        }
        return n > 0 ? meanSquareToLufs (e / (double) n) : -150.0;
    }
}
