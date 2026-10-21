#pragma once

#include <vector>

namespace dooblrq::analysis
{
    /**
        Turns per-bin analyzer levels into a smooth display curve on a fixed
        log-frequency grid (message thread only; never touches the audio thread).

        Frequency smoothing: each grid point is the power average of the FFT bins within
        +/- half of a fractional-octave band around it (1/12 octave by default). Where the
        band holds fewer than about three bins (low frequencies), the level is a Catmull-Rom
        interpolation between neighbouring bins instead, blended smoothly with the average,
        so the low end is a smooth curve rather than straight segments between sparse bins.
        Note: like any fractional-octave smoothing, a pure tone reads lower than its
        per-bin level (its power is spread over the band); the peak-hold line is not
        smoothed and still shows tone peaks.

        Temporal smoothing: per grid point, one-pole in dB with separate rise and fall
        time constants, advanced by the real elapsed time, so it behaves the same at any
        frame rate.
    */
    class SpectrumDisplay
    {
    public:
        static constexpr int defaultNumPoints = 480;
        static constexpr double defaultOctaveFraction = 1.0 / 12.0;

        SpectrumDisplay (int numPoints = defaultNumPoints, double minFrequency = 20.0, double maxFrequency = 20000.0,
                         double octaveFraction = defaultOctaveFraction);

        /** Rebuilds the bin mapping if the rate or bin count changed (and resets). */
        void prepare (double sampleRate, int numBins);

        /** Frequency-smoothed levels of binDb on the grid (no temporal smoothing). */
        void computeTarget (const std::vector<float>& binDb, std::vector<float>& result) const;

        /** Moves the displayed levels towards the frequency-smoothed binDb over dtSeconds. */
        void advance (const std::vector<float>& binDb, double dtSeconds, double riseSeconds, double fallSeconds);

        /** Jumps straight to the frequency-smoothed binDb. */
        void snapTo (const std::vector<float>& binDb);

        void reset (float levelDb);

        int getNumPoints() const noexcept { return (int) levels.size(); }
        double getFrequency (int point) const noexcept { return frequencies[(size_t) point]; }
        const std::vector<float>& getLevels() const noexcept { return levels; }

        /** Largest |target - level| after the last advance (dB): how much is still moving. */
        float getLastMovement() const noexcept { return lastMovement; }

    private:
        struct Mapping
        {
            int firstBin = 0, lastBin = -1; // inclusive range for the power average
            double position = 0.0;          // fractional bin of the grid frequency
            float averageWeight = 0.0f;     // 0 = interpolation only, 1 = average only
        };

        double minFrequency, maxFrequency, octaveFraction;
        double preparedRate = 0.0;
        int preparedBins = 0;
        std::vector<double> frequencies;
        std::vector<Mapping> mappings;
        std::vector<float> levels, target;
        mutable std::vector<double> powerPrefix;
        float lastMovement = 0.0f;
    };
}
