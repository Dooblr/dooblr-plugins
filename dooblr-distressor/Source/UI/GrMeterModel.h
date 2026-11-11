#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace dooblrdistressor::ui
{
    /** Display-only gain-reduction ballistics, peak-hold and a 60 Hz history buffer.
        Pure logic: no Component, no painting. Drive it with explicit dt from tests or VBlank. */
    class GrMeterModel
    {
    public:
        static constexpr float rangeDb = 24.0f;
        static constexpr float releaseDbPerSecond = 12.0f;
        static constexpr double holdMs = 700.0;
        static constexpr float holdDecayDbPerSecond = 18.0f;
        static constexpr int historyLength = 180;          // 3 s at 60 Hz
        static constexpr float historySeconds = 3.0f;
        static constexpr double historyHz = 60.0;

        void reset() noexcept
        {
            displayedDb = peakHoldDb = 0.0f;
            peakHoldUntilMs = 0.0;
            sampleAccum = 0.0;
            historyWrite = historyFilled = 0;
            history.fill (0.0f);
            lastNowMs = -1.0;
        }

        /** Advances ballistics by `dtSeconds` toward `incomingDb` (positive GR) at time `nowMs`. */
        void tick (float incomingDb, double dtSeconds, double nowMs) noexcept
        {
            if (! std::isfinite (incomingDb)) incomingDb = 0.0f;
            incomingDb = std::clamp (incomingDb, 0.0f, rangeDb);
            dtSeconds = std::max (0.0, dtSeconds);

            if (incomingDb >= displayedDb)
                displayedDb = incomingDb;
            else
                displayedDb = std::max (incomingDb, displayedDb - releaseDbPerSecond * (float) dtSeconds);

            if (displayedDb >= peakHoldDb - 1.0e-6f)
            {
                peakHoldDb = displayedDb;
                peakHoldUntilMs = nowMs + holdMs;
            }
            else if (nowMs >= peakHoldUntilMs)
            {
                peakHoldDb = std::max (displayedDb, peakHoldDb - holdDecayDbPerSecond * (float) dtSeconds);
            }

            sampleAccum += dtSeconds;
            const auto period = 1.0 / historyHz;
            while (sampleAccum >= period)
            {
                pushHistory (displayedDb);
                sampleAccum -= period;
            }

            lastNowMs = nowMs;
        }

        float getDisplayedDb() const noexcept { return displayedDb; }
        float getPeakHoldDb() const noexcept { return peakHoldDb; }
        double getPeakHoldUntilMs() const noexcept { return peakHoldUntilMs; }

        int getHistoryFilled() const noexcept { return historyFilled; }
        int getHistoryLength() const noexcept { return historyLength; }

        /** Oldest sample first. Out of range is 0. */
        float historyOldest (int index) const noexcept
        {
            if (index < 0 || index >= historyFilled)
                return 0.0f;
            const auto idx = (historyWrite - historyFilled + index + historyLength) % historyLength;
            return history[(size_t) idx];
        }

        float historyNewest() const noexcept
        {
            return historyFilled > 0 ? historyOldest (historyFilled - 1) : 0.0f;
        }

    private:
        void pushHistory (float db) noexcept
        {
            history[(size_t) historyWrite] = db;
            historyWrite = (historyWrite + 1) % historyLength;
            historyFilled = std::min (historyFilled + 1, historyLength);
        }

        float displayedDb = 0.0f, peakHoldDb = 0.0f;
        double peakHoldUntilMs = 0.0, sampleAccum = 0.0, lastNowMs = -1.0;
        std::array<float, historyLength> history {};
        int historyWrite = 0, historyFilled = 0;
    };

    /** Maps the compression-display handles to the real VST parameters and back.
        Normalised coordinates are 0..1 inside each handle's plot. Ratio is stepped. */
    struct CompressionMap
    {
        static constexpr float knobMin = 0.0f, knobMax = 10.5f, knobDefault = 5.0f, knobStep = 0.01f;
        static constexpr int numRatios = 8, defaultRatio = 4;

        struct Params
        {
            float input = knobDefault, output = knobDefault, attack = knobDefault, release = knobDefault;
            int ratio = defaultRatio;
        };

        /** Handle centres in unit-square plot space (x right, y up). */
        struct Points
        {
            float kneeX = 0.5f, kneeY = 0.5f;
            float slopeT = (float) defaultRatio / (float) (numRatios - 1);
            float attackT = 0.5f, releaseT = 0.5f;
        };

        static float clamp01 (float n) noexcept { return std::clamp (n, 0.0f, 1.0f); }

        static float clampKnob (float v) noexcept
        {
            v = std::clamp (v, knobMin, knobMax);
            return std::round (v / knobStep) * knobStep;
        }

        static int clampRatio (int r) noexcept { return std::clamp (r, 0, numRatios - 1); }

        static float knobToNorm (float knob) noexcept
        {
            return clamp01 ((clampKnob (knob) - knobMin) / (knobMax - knobMin));
        }

        static float normToKnob (float n) noexcept
        {
            return clampKnob (knobMin + clamp01 (n) * (knobMax - knobMin));
        }

        static int snapRatio (float t) noexcept
        {
            return clampRatio ((int) std::lround ((double) clamp01 (t) * (double) (numRatios - 1)));
        }

        static float ratioToNorm (int ratio) noexcept
        {
            return (float) clampRatio (ratio) / (float) (numRatios - 1);
        }

        static Points fromParams (const Params& p) noexcept
        {
            Points q;
            q.kneeX = knobToNorm (p.input);
            q.kneeY = knobToNorm (p.output);
            q.slopeT = ratioToNorm (p.ratio);
            q.attackT = knobToNorm (p.attack);
            q.releaseT = knobToNorm (p.release);
            return q;
        }

        static Params applyKnee (Params p, float nx, float ny) noexcept
        {
            p.input = normToKnob (nx);
            p.output = normToKnob (ny);
            return p;
        }

        static Params applySlope (Params p, float t) noexcept
        {
            p.ratio = snapRatio (t);
            return p;
        }

        static Params applyAttack (Params p, float t) noexcept
        {
            p.attack = normToKnob (t);
            return p;
        }

        static Params applyRelease (Params p, float t) noexcept
        {
            p.release = normToKnob (t);
            return p;
        }
    };
}
