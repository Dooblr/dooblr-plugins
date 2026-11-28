#pragma once

#include <juce_core/juce_core.h>

#include "Bands.h"

namespace dooblrmeter::dsp
{
    /** Delivery targets. Labels are generic on purpose (no service names in the UI). Values are
        approximate published normalisation targets; re-verify before each release. */
    struct DeliveryTarget
    {
        enum class Policy
        {
            downOnly,        // turns loud masters down, never up
            upPeakLimited,   // turns down; turns up only while the true peak stays under the ceiling
            both,            // a delivery spec: hit it exactly (broadcast, spoken word)
            none             // no normalisation (club play)
        };

        const char* label;
        float lufs;        // target integrated loudness
        float ceiling;     // recommended true-peak ceiling, dBTP
        Policy policy;
    };

    namespace targets
    {
        inline constexpr int count = 9;  // the last entry is Custom
        inline constexpr int customIndex = 8;

        /** The fixed targets (Custom takes its loudness and ceiling from the Advanced page). */
        DeliveryTarget get (int index, float customLufs = -14.0f, float customCeiling = -1.0f) noexcept;
        juce::StringArray names();

        /** Gain the target's playback would apply to a master with this integrated loudness and
            true peak (dB; 0 when the target does not normalise). */
        float predictedGainDb (const DeliveryTarget& t, float integratedLufs, float truePeakDb) noexcept;

        /** One line describing the policy ("turns down only", ...). */
        juce::String policyText (DeliveryTarget::Policy) ;
    }

    /** Reference tilt curves: expected long-term level per 1/3-octave band (band power, dB,
        relative; pink noise is flat). dooblr-authored smoothed tilts, no third-party data. */
    namespace curves
    {
        inline constexpr int count = 6;
        juce::StringArray names();
        float levelDb (int curve, double hz) noexcept;
        bands::Levels levels (int curve) noexcept;
        inline constexpr float tolerance = 3.0f; // dB either side of the curve
    }
}
