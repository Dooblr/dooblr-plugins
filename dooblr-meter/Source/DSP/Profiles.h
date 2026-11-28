#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "Biquad.h"

namespace dooblrmeter::dsp
{
    /** A playback-system model: response, how it handles stereo, a bass limiter, its typical
        listening level and the background noise it is heard against. Used by the translation
        scores and by the Listen audition. Generic models, not measurements of any product. */
    struct Profile
    {
        struct Filter { Biquad::Type type; double hz, q, gainDb; };
        enum class Stereo { full, partial, mono };

        const char* name;       // tile / menu name
        const char* shortName;  // compact tile name
        std::vector<Filter> filters;
        Stereo stereo;
        double monoBelowHz;     // partial: side removed below this
        double sideGain;        // side level kept above monoBelowHz (partial) or overall (full)
        bool limiter;           // small-speaker bass limiter
        double splDb;           // typical listening level (dB SPL for the whole programme)
        int noise;              // 0 quiet room, 1 street, 2 road, 3 crowd, 4 living room
        bool lossyChain;        // usually fed by a lossy wireless / streaming codec
    };

    namespace profiles
    {
        inline constexpr int count = 8;
        enum Index { club = 0, bluetooth, phone, laptop, earbuds, headphones, car, tv };

        const Profile& get (int index) noexcept;
        /** "Off" then the eight profile names (the Listen choices). */
        juce::StringArray listenNames();

        /** Response of the profile's filters in dB at hz. */
        double responseDb (const Profile&, double hz) noexcept;
        /** Background noise level heard with this profile, dB SPL per 1/3-octave band. */
        double noiseDb (const Profile&, double hz) noexcept;
    }

    /** Hearing threshold in quiet (dB SPL), Terhardt's approximation of ISO 226. */
    double hearingThresholdDb (double hz) noexcept;

    /** The Listen audition: plays the input through a profile model, level-matched to the dry
        signal. Zero latency (IIR filters, feed-forward limiter). Audio thread only. */
    class ProfileAudition
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;
        void setProfile (int index) noexcept;  // -1 = off (the caller crossfades)
        int getProfile() const noexcept { return profile; }

        /** Processes in place. levelMatch: compensate the profile's loudness change. */
        void process (float* left, float* right, int numSamples, bool levelMatch) noexcept;

        /** The current loudness compensation (dB), for display and tests. */
        float getMatchGainDb() const noexcept { return (float) gainToDb (matchGain); }

    private:
        static constexpr int maxFilters = 8;
        double sampleRate = 48000.0;
        int profile = -1, numFilters = 0;
        std::array<std::array<Biquad, maxFilters>, 2> chain {};
        std::array<Biquad, 2> sideHp {};             // partial mono: side high-passed
        std::array<Biquad, 2> limiterSplit {};       // low band for the limiter (mid only)
        double limEnv = 0.0, limAttack = 0.0, limRelease = 0.0, limThreshold = 0.0;
        // Loudness match: K-weighted energy of dry and wet with a 3 s time constant.
        std::array<Biquad, 4> kDry {}, kWet {};
        double eDry = 1.0e-9, eWet = 1.0e-9, eCoef = 0.0, matchGain = 1.0, gainCoef = 0.0;
        const Profile* model = nullptr;
    };
}
