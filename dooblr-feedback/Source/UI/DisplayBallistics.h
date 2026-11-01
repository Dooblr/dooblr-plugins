#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dooblrfeedback::ui
{
    /**
        Analyzer Speed: how fast the drawn spectrum and reduction follow the analysis (display
        only; the audio and its latency are untouched). Each choice has a rise time (a level going
        up) and a fall time (going down), given as the time to cover 90 % of a step. The
        smoothing is a one-pole per display point with coefficient 1 - exp(-dt / tau), tau =
        t90 / ln 10, from the real elapsed time, so it looks the same at any frame rate.

        A UI preference: saved with the session (outside the parameters, so not automatable,
        not part of presets or A / B and not on the undo history).
    */
    enum class AnalyzerSpeed { slow = 0, medium, fast, veryFast };

    constexpr int numAnalyzerSpeeds = 4;
    constexpr AnalyzerSpeed defaultAnalyzerSpeed = AnalyzerSpeed::medium;

    struct AnalyzerTimes
    {
        float riseMs, fallMs; // 90 % times
    };

    constexpr AnalyzerTimes analyzerTimes (AnalyzerSpeed speed) noexcept
    {
        switch (speed)
        {
            case AnalyzerSpeed::slow:     return { 250.0f, 2000.0f };
            case AnalyzerSpeed::medium:   return { 100.0f, 800.0f };
            case AnalyzerSpeed::fast:     return { 40.0f, 250.0f };
            case AnalyzerSpeed::veryFast:
            default:                      return { 15.0f, 60.0f };
        }
    }

    inline const char* analyzerSpeedName (AnalyzerSpeed speed) noexcept
    {
        switch (speed)
        {
            case AnalyzerSpeed::slow:     return "Slow";
            case AnalyzerSpeed::medium:   return "Medium";
            case AnalyzerSpeed::fast:     return "Fast";
            case AnalyzerSpeed::veryFast:
            default:                      return "Very Fast";
        }
    }

    constexpr AnalyzerSpeed analyzerSpeedFromIndex (int index) noexcept
    {
        return (AnalyzerSpeed) std::clamp (index, 0, numAnalyzerSpeeds - 1);
    }

    /** The one-pole coefficient that covers 90 % of a step in t90Ms (0 ms: instant). */
    inline float ballisticsCoefficient (float t90Ms, double dtSeconds) noexcept
    {
        if (t90Ms <= 0.0f)
            return 1.0f;
        const auto tau = (double) t90Ms * 0.001 / std::log (10.0);
        return (float) (1.0 - std::exp (-std::max (0.0, dtSeconds) / tau));
    }

    /** Moves shown[] toward target[] by dt seconds of the given times. Returns the largest change. */
    inline float applyBallistics (const float* target, float* shown, std::size_t n, double dtSeconds, AnalyzerTimes times) noexcept
    {
        const auto rise = ballisticsCoefficient (times.riseMs, dtSeconds);
        const auto fall = ballisticsCoefficient (times.fallMs, dtSeconds);
        auto biggest = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto delta = target[i] - shown[i];
            const auto step = delta * (delta > 0.0f ? rise : fall);
            shown[i] += step;
            biggest = std::max (biggest, std::abs (step));
        }
        return biggest;
    }
}
