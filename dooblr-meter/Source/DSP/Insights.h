#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "Bands.h"
#include "Targets.h"
#include "Translation.h"

namespace dooblrmeter::dsp
{
    /** One plain-language finding of the rule-based insights engine (no ML, no model weights). */
    struct Finding
    {
        enum class Severity { fix = 0, watch = 1, info = 2 };

        Severity severity = Severity::info;
        juce::String title;     // short, e.g. "Harshness 2.5-5 kHz"
        juce::String detail;    // what was measured, where, and why it matters
        juce::String fix;       // what to try
        double t0 = -1.0, t1 = -1.0;  // seconds on the analysed timeline; -1 = whole programme
        float fLo = 0.0f, fHi = 0.0f; // Hz band the finding is about; 0 = none
        juce::String key;       // stable detector id (tests, de-duplication)
    };

    /** A 4-second analysis segment. */
    struct SegmentInfo
    {
        double t0 = 0.0, t1 = 0.0;
        float loudness = -150.0f;          // mean K-weighted loudness of the segment
        bands::Levels deviationDb {};      // spectrum minus aligned curve
        bool spectrumValid = false;
        float sibilancePerMinute = 0.0f;
        std::array<float, 3> bandCorrelation { 1.0f, 1.0f, 1.0f }; // <120 Hz, 120 Hz-2 kHz, >2 kHz
        float sideLowToMidLowDb = -150.0f;
    };

    struct InsightInput
    {
        float integrated = -150.0f;
        bool hasIntegrated = false;
        float lra = 0.0f;
        bool hasLra = false;
        float truePeakMax = -150.0f;     // dBTP
        double truePeakTime = -1.0;
        int overRuns = 0;                // places where the true peak crosses the ceiling
        float samplePeakMax = -150.0f;
        int clipRuns = 0;
        double firstClipTime = -1.0;
        float plr = 0.0f;
        float medianPsr = 0.0f;
        float dcLeftDb = -150.0f, dcRightDb = -150.0f;
        float balanceDb = 0.0f;          // L minus R loudness
        float correlation = 1.0f;
        std::array<float, 3> bandCorrelation { 1.0f, 1.0f, 1.0f };
        float sideToMidDb = -150.0f;
        float sideLowToMidLowDb = -150.0f;
        float monoLossDb = 0.0f;
        bands::Levels deviationDb {};
        bool spectrumValid = false;
        float sibilancePerMinute = 0.0f;
        std::vector<SegmentInfo> segments;
        double analysedSeconds = 0.0;
        DeliveryTarget target { "Streaming -14", -14.0f, -1.0f, DeliveryTarget::Policy::upPeakLimited };
        int sensitivity = 1;             // 0 Lenient, 1 Standard, 2 Strict
        std::array<TranslationResult, profiles::count> translation {};
        bool translationValid = false;
        double bpm = 0.0, beatsPerBar = 4.0;
        bool songMode = false;
    };

    std::vector<Finding> findInsights (const InsightInput&);

    juce::String formatTime (double seconds);
    /** "1:52-2:10" or, in Song mode with a tempo, "bars 49-56 (1:52-2:10)". */
    juce::String formatSpan (double t0, double t1, double bpm, double beatsPerBar, bool songMode);
    juce::String severityName (Finding::Severity);
}
