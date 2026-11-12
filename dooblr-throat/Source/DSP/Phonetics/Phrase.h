#pragma once
// Message-thread parse of English text into an immutable phoneme/gesture
// plan that the audio thread can play. Ported in part from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge).

#include "English.h"
#include "Planner.h"
#include "Syllables.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    inline constexpr double kLiveT0 = -0.045;
    inline constexpr double kHoldVowel = 30.0;
    inline constexpr double kSpeakF0Ref = 110.0;

    enum class SpeechMode { Sing = 0, Speak = 1 };

    struct SyllablePlan
    {
        std::string text;
        std::string nucleus;
        std::string readout; // onset + nucleus + coda IPA
        std::vector<Gesture> hold;  // onset + held vowel, relative time (LIVE_T0)
        std::vector<Gesture> coda;  // off-glide + coda, relative time (LIVE_T0)
        std::vector<PlannedSegment> holdSegments;
        std::vector<PlannedSegment> codaSegments;
        double holdEnd = 0;
        double codaEnd = 0;
        bool wordEnd = false;
        bool phraseEnd = false;
        bool melisma = false;
    };

    struct SpeakPlan
    {
        std::vector<Gesture> gestures;
        std::vector<PlannedSegment> segments;
        double end = 0;
        double f0Ref = kSpeakF0Ref;
    };

    struct PublishedPhrase
    {
        std::string text;
        std::vector<Syllable> syllables;
        std::vector<SyllablePlan> plans;
        SpeakPlan speak;
        bool empty() const { return text.empty() || (plans.empty() && speak.gestures.empty()); }
    };

    /** Build a phrase plan. Allocates; call on the message thread only. */
    std::unique_ptr<PublishedPhrase> buildPhrase (const EnglishG2P& g2p, std::string_view text, const BodyMix& body);

    /** Current phoneme label at plan time t, or empty. */
    std::string segmentAt (const std::vector<PlannedSegment>& segs, double t);
}

namespace dooblrthroat
{
    /** Load bundled CMUdict into g2p (message thread). Safe to call more than once. */
    bool loadBundledCmudict (dsp::phonetics::EnglishG2P& g2p);
}
