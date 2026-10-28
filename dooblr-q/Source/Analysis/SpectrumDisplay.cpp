#include "SpectrumDisplay.h"
#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace dooblrq::analysis
{
    SpectrumDisplay::SpectrumDisplay (int numPoints, double minF, double maxF, double fraction)
        : minFrequency (minF), maxFrequency (maxF), octaveFraction (fraction),
          frequencies ((size_t) std::max (2, numPoints)),
          mappings ((size_t) std::max (2, numPoints)),
          levels ((size_t) std::max (2, numPoints), SpectrumAnalyzer::floorDb),
          target ((size_t) std::max (2, numPoints), SpectrumAnalyzer::floorDb)
    {
        const auto n = (int) frequencies.size();

        for (int i = 0; i < n; ++i)
            frequencies[(size_t) i] = minFrequency * std::pow (maxFrequency / minFrequency, (double) i / (double) (n - 1));
    }

    void SpectrumDisplay::prepare (double sampleRate, int numBins)
    {
        if (sampleRate <= 0.0 || numBins < 4)
            return;

        if (std::abs (sampleRate - preparedRate) < 0.5 && numBins == preparedBins)
            return;

        preparedRate = sampleRate;
        preparedBins = numBins;
        powerPrefix.assign ((size_t) numBins + 1, 0.0);

        const auto binWidth = sampleRate / (double) ((numBins - 1) * 2);
        const auto halfBand = std::pow (2.0, 0.5 * octaveFraction);

        for (size_t i = 0; i < mappings.size(); ++i)
        {
            auto& m = mappings[i];
            const auto f = frequencies[i];
            const auto low = f / halfBand / binWidth;
            const auto high = f * halfBand / binWidth;

            m.position = std::clamp (f / binWidth, 1.0, (double) (numBins - 1));
            m.firstBin = std::clamp ((int) std::ceil (low), 1, numBins - 1);
            m.lastBin = std::clamp ((int) std::floor (high), 1, numBins - 1);

            // Bands narrower than ~1 bin: interpolate; wider than ~3 bins: average; blend between.
            m.averageWeight = (float) std::clamp ((high - low - 1.0) / 2.0, 0.0, 1.0);

            if (m.lastBin < m.firstBin)
                m.averageWeight = 0.0f;
        }

        reset (SpectrumAnalyzer::floorDb);
    }

    void SpectrumDisplay::computeTarget (const std::vector<float>& binDb, std::vector<float>& result) const
    {
        result.resize (levels.size());

        const auto numBins = (int) binDb.size();

        if (numBins != preparedBins || numBins < 4)
        {
            std::fill (result.begin(), result.end(), SpectrumAnalyzer::floorDb);
            return;
        }

        powerPrefix[0] = 0.0;

        for (int b = 0; b < numBins; ++b)
            powerPrefix[(size_t) b + 1] = powerPrefix[(size_t) b] + std::pow (10.0, 0.1 * (double) binDb[(size_t) b]);

        const auto floorDb = (double) SpectrumAnalyzer::floorDb;
        const auto bin = [&] (int index) { return (double) binDb[(size_t) std::clamp (index, 1, numBins - 1)]; };

        for (size_t i = 0; i < mappings.size(); ++i)
        {
            const auto& m = mappings[i];

            // Catmull-Rom through the neighbouring bins (dB).
            const auto k = std::min ((int) m.position, numBins - 2);
            const auto t = m.position - (double) k;
            const auto p0 = bin (k - 1), p1 = bin (k), p2 = bin (k + 1), p3 = bin (k + 2);
            const auto interpolated = 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
                                             + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);

            // Limited to the two neighbours so it never overshoots between bins.
            auto level = std::max (floorDb, std::min (std::max (interpolated, std::min (p1, p2)), std::max (p1, p2)));

            if (m.averageWeight > 0.0f)
            {
                const auto count = (double) (m.lastBin - m.firstBin + 1);
                const auto mean = (powerPrefix[(size_t) m.lastBin + 1] - powerPrefix[(size_t) m.firstBin]) / count;
                const auto averaged = mean > 0.0 ? std::max (floorDb, 10.0 * std::log10 (mean)) : floorDb;
                level += (double) m.averageWeight * (averaged - level);
            }

            result[i] = std::isfinite (level) ? (float) level : SpectrumAnalyzer::floorDb;
        }
    }

    void SpectrumDisplay::advance (const std::vector<float>& binDb, double dtSeconds, double riseSeconds, double fallSeconds)
    {
        computeTarget (binDb, target);

        const auto dt = std::clamp (dtSeconds, 0.0, 0.25);
        const auto rise = (float) (1.0 - std::exp (-dt / std::max (1.0e-3, riseSeconds)));
        const auto fall = (float) (1.0 - std::exp (-dt / std::max (1.0e-3, fallSeconds)));
        auto movement = 0.0f;

        for (size_t i = 0; i < levels.size(); ++i)
        {
            const auto difference = target[i] - levels[i];
            levels[i] += (difference > 0.0f ? rise : fall) * difference;
            movement = std::max (movement, std::abs (target[i] - levels[i]));
        }

        lastMovement = movement;
    }

    void SpectrumDisplay::snapTo (const std::vector<float>& binDb)
    {
        computeTarget (binDb, levels);
        lastMovement = 0.0f;
    }

    void SpectrumDisplay::reset (float levelDb)
    {
        std::fill (levels.begin(), levels.end(), levelDb);
        lastMovement = 0.0f;
    }
}
