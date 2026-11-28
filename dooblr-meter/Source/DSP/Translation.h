#pragma once

#include <array>

#include <juce_core/juce_core.h>

#include "Bands.h"
#include "Profiles.h"

namespace dooblrmeter::dsp
{
    /** What the translation model needs from the analysis: long-term mid and side power per
        1/3-octave band, and the deviation of the mix from the chosen reference curve. */
    struct SpectralSummary
    {
        std::array<double, bands::count> mid {};   // mean power per band (linear)
        std::array<double, bands::count> side {};
        bands::Levels deviationDb {};              // mix minus aligned curve, per band
        float truePeakDb = -150.0f;
        bool valid = false;
    };

    struct TranslationResult
    {
        int score = 0;                        // 0..100
        juce::StringArray reasons;            // most important first
        float lowEndLostPercent = 0.0f;       // share of 20-250 Hz energy this system loses
        float monoLossDb = 0.0f;              // loudness lost to this system's mono summing
    };

    /** Scores how well the mix survives a playback system (see design notes, "Translation"). */
    TranslationResult scoreTranslation (const Profile&, const SpectralSummary&);

    /** Loudness change (dB) when mid + side is summed to mono, from band powers. */
    double monoSumLossDb (const SpectralSummary&) noexcept;
}
