#include "Parameters.h"

#include <cmath>

namespace dooblrgenerate::params
{
    namespace
    {
        using Float = juce::AudioParameterFloat;
        using Int = juce::AudioParameterInt;
        using Attributes = juce::AudioParameterFloatAttributes;

        juce::String formatMs (float ms)
        {
            if (ms >= 1000.0f)
                return juce::String (ms / 1000.0f, ms >= 10000.0f ? 1 : 2) + " s";

            return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
        }

        juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step)
        {
            juce::NormalisableRange<float> r (lo, hi, step);
            r.setSkewForCentre (centre);
            return r;
        }
    }

    juce::StringArray allIds()
    {
        return { id::output, id::tune, id::fine, id::start, id::attack, id::decay, id::velocity };
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<Float> (juce::ParameterID { id::output, 1 }, "Output",
            juce::NormalisableRange<float> (-48.0f, 12.0f, 0.01f), 0.0f,
            Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
            {
                if (v <= -47.99f)
                    return juce::String ("-inf dB");
                if (std::abs (v) < 0.05f)
                    return juce::String ("0.0 dB");
                return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
            })));

        layout.add (std::make_unique<Int> (juce::ParameterID { id::tune, 1 }, "Tune", -24, 24, 0,
            juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int v, int)
            {
                return (v > 0 ? "+" : "") + juce::String (v) + " st";
            })));

        layout.add (std::make_unique<Float> (juce::ParameterID { id::fine, 1 }, "Fine",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
            Attributes().withLabel ("ct").withStringFromValueFunction ([] (float v, int)
            {
                return std::abs (v) < 0.05f ? juce::String ("0.0 ct") : (v > 0 ? "+" : "") + juce::String (v, 1) + " ct";
            })));

        layout.add (std::make_unique<Float> (juce::ParameterID { id::start, 1 }, "Start",
            juce::NormalisableRange<float> (0.0f, 95.0f, 0.1f), 0.0f,
            Attributes().withLabel ("%").withStringFromValueFunction ([] (float v, int)
            {
                return juce::String (v, 1) + " %";
            })));

        layout.add (std::make_unique<Float> (juce::ParameterID { id::attack, 1 }, "Attack",
            skewed (0.0f, 1000.0f, 50.0f, 0.01f), 0.0f,
            Attributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return formatMs (v); })));

        layout.add (std::make_unique<Float> (juce::ParameterID { id::decay, 1 }, "Decay",
            skewed (10.0f, decayOffMs, 1000.0f, 0.1f), decayOffMs,
            Attributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
            {
                return v >= decayOffMs - 0.05f ? juce::String ("Off") : formatMs (v);
            })));

        layout.add (std::make_unique<Float> (juce::ParameterID { id::velocity, 1 }, "Velocity",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            Attributes().withLabel ("%").withStringFromValueFunction ([] (float v, int)
            {
                return juce::String (v, 0) + " %";
            })));

        return layout;
    }
}
