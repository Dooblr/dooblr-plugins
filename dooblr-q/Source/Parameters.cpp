#include "Parameters.h"

namespace dooblrq::params
{
    namespace
    {
        constexpr int parameterVersionHint = 1;

        /** Frequency-style range with an exact logarithmic mapping. */
        juce::NormalisableRange<float> logRange (float minimum, float maximum)
        {
            return { minimum, maximum,
                     [] (float start, float end, float normalised)
                     { return start * std::pow (end / start, normalised); },
                     [] (float start, float end, float value)
                     { return std::log (value / start) / std::log (end / start); },
                     nullptr };
        }

        juce::String formatFrequency (float hz, int)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                                 : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::String formatDecibels (float db, int)
        {
            return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        }

        juce::String formatMilliseconds (float ms, int)
        {
            return ms < 10.0f ? juce::String (ms, 1) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
        }

        juce::String formatQ (float q, int)
        {
            return juce::String (q, 2);
        }

        juce::AudioParameterFloatAttributes withFormatter (juce::String (*formatter) (float, int),
                                                           const juce::String& label = {})
        {
            return juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (formatter)
                .withLabel (label);
        }
    }

    juce::String bandId (int bandIndex, const char* suffix)
    {
        return "b" + juce::String (bandIndex + 1) + "_" + suffix;
    }

    juce::StringArray phaseModeNames()
    {
        return { "Zero Latency", "Natural Phase", "Linear Phase" };
    }

    juce::StringArray filterTypeNames()
    {
        return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Tilt" };
    }

    juce::StringArray slopeNames()
    {
        return { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "60 dB/oct", "Brickwall" };
    }

    juce::StringArray modelNames()
    {
        juce::StringArray names;
        for (int m = 0; m < dsp::numModels; ++m)
            names.add (dsp::modelName ((dsp::Model) m));
        return names;
    }

    BandDefaults bandDefaults (int bandIndex)
    {
        using dsp::FilterType;

        // Spread across the spectrum so a freshly enabled band lands somewhere useful:
        // band 1 a high-pass (Low Cut) and band 6 a low-pass (High Cut), both Butterworth.
        static constexpr BandDefaults defaults[] {
            { FilterType::lowCut,    30.0f,    0.707f },
            { FilterType::lowShelf,  120.0f,   0.707f },
            { FilterType::bell,      400.0f,   1.0f   },
            { FilterType::bell,      1500.0f,  1.0f   },
            { FilterType::bell,      4000.0f,  1.0f   },
            { FilterType::highCut,   18000.0f, 0.707f },
        };

        if (bandIndex < (int) std::size (defaults))
            return defaults[bandIndex];

        // Extra bands (if numBands grows): log-spaced bells.
        const auto position = (float) bandIndex / (float) juce::jmax (1, numBands - 1);
        return { FilterType::bell, minFrequency * std::pow (maxFrequency / minFrequency, position), 1.0f };
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int bandIndex = 0; bandIndex < numBands; ++bandIndex)
        {
            const auto prefix   = "Band " + juce::String (bandIndex + 1) + " ";
            const auto defaults = bandDefaults (bandIndex);
            const auto id = [bandIndex] (const char* suffix)
            {
                return juce::ParameterID { bandId (bandIndex, suffix), parameterVersionHint };
            };

            auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
                "band" + juce::String (bandIndex + 1), "Band " + juce::String (bandIndex + 1), " | ");

            group->addChild (std::make_unique<juce::AudioParameterBool> (
                id (band::enabled), prefix + "Enabled", false));

            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                id (band::type), prefix + "Type", filterTypeNames(), (int) defaults.type));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::frequency), prefix + "Frequency", logRange (minFrequency, maxFrequency),
                defaults.frequency, withFormatter (formatFrequency)));

            // Gain-type ranges are continuous (no interval) so 0 dB is stored as exactly 0
            // and a neutral band is an exact identity.
            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::gain), prefix + "Gain",
                juce::NormalisableRange<float> (minGainDb, maxGainDb), 0.0f,
                withFormatter (formatDecibels)));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::q), prefix + "Q", logRange (minQ, maxQ), defaults.q, withFormatter (formatQ)));

            group->addChild (std::make_unique<juce::AudioParameterBool> (
                id (band::dynamicsEnabled), prefix + "Dynamic", false));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::range), prefix + "Dynamic Range",
                juce::NormalisableRange<float> (minRangeDb, maxRangeDb), 0.0f,
                withFormatter (formatDecibels)));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::threshold), prefix + "Threshold",
                juce::NormalisableRange<float> (minThresholdDb, maxThresholdDb, 0.1f), -24.0f,
                withFormatter (formatDecibels)));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::attack), prefix + "Attack", logRange (minAttackMs, maxAttackMs), 10.0f,
                withFormatter (formatMilliseconds)));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                id (band::release), prefix + "Release", logRange (minReleaseMs, maxReleaseMs), 120.0f,
                withFormatter (formatMilliseconds)));

            layout.add (std::move (group));
        }

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { outputGain, parameterVersionHint }, "Output Gain",
            juce::NormalisableRange<float> (minGainDb, maxGainDb), 0.0f,
            withFormatter (formatDecibels)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { stereoLink, parameterVersionHint }, "Stereo Link", true));

        // Added in 0.4.0, after every older parameter (old order unchanged); sessions saved
        // before it existed load it at its default, Zero Latency.
        constexpr int addedInV040 = 2;
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { phaseMode, addedInV040 }, "Phase Mode", phaseModeNames(), 0));

        // Added in 0.7.0, again after everything older; old sessions load 12 dB/oct, which
        // is exactly the filter every earlier version used.
        constexpr int addedInV070 = 3;
        for (int bandIndex = 0; bandIndex < numBands; ++bandIndex)
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandId (bandIndex, band::slope), addedInV070 },
                "Band " + juce::String (bandIndex + 1) + " Slope", slopeNames(), (int) dsp::Slope::db12));

        // Added in 0.8.0 after everything older: per band Model, Stepped, Drive and Atten.
        // Old sessions load Digital / off / 0 dB / 0 dB, which is exactly the 0.7.1 sound.
        constexpr int addedInV080 = 4;
        for (int bandIndex = 0; bandIndex < numBands; ++bandIndex)
        {
            const auto prefix = "Band " + juce::String (bandIndex + 1) + " ";
            const auto id = [bandIndex] (const char* suffix) { return juce::ParameterID { bandId (bandIndex, suffix), addedInV080 }; };
            layout.add (std::make_unique<juce::AudioParameterChoice> (id (band::model), prefix + "Model", modelNames(), 0));
            layout.add (std::make_unique<juce::AudioParameterBool> (id (band::stepped), prefix + "Stepped", false));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                id (band::drive), prefix + "Drive", juce::NormalisableRange<float> (minDriveDb, maxDriveDb), 0.0f,
                withFormatter (formatDecibels)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                id (band::atten), prefix + "Atten", juce::NormalisableRange<float> (minAttenDb, maxAttenDb), 0.0f,
                withFormatter (formatDecibels)));
        }

        // Added in 0.9.1 after everything older: per band Solo (audition only that band's
        // region). Old sessions load it off.
        constexpr int addedInV091 = 5;
        for (int bandIndex = 0; bandIndex < numBands; ++bandIndex)
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { bandId (bandIndex, band::solo), addedInV091 },
                "Band " + juce::String (bandIndex + 1) + " Solo", false));

        return layout;
    }
}
