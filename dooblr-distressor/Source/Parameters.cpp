#include "Parameters.h"

#include <cmath>

namespace dooblrdistressor::params
{
    juce::String formatValue (double value, int decimals)
    {
        decimals = juce::jlimit (0, 6, decimals);
        const auto scale = std::pow (10.0, (double) decimals);
        auto rounded = std::round (value * scale) / scale;
        if (! std::isfinite (rounded)) rounded = 0.0;
        if (std::abs (rounded) < 0.5 / scale) rounded = 0.0;
        if (decimals == 0) return juce::String (juce::roundToInt (rounded));
        return juce::String (rounded, decimals);
    }

    juce::StringArray ratioNames()    { return { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "Nuke" }; }
    juce::StringArray distModeNames() { return { "Off", "Dist 2", "Dist 3" }; }
    juce::StringArray headroomNames() { return { "4 dB", "8 dB", "12 dB", "16 dB", "20 dB", "24 dB", "28 dB" }; }

    int headroomDbForIndex (int index)
    {
        return 4 + 4 * juce::jlimit (0, numHeadroomSteps - 1, index);
    }

    int indexForHeadroomDb (int db)
    {
        return juce::jlimit (0, numHeadroomSteps - 1, (db - 4) / 4);
    }

    void migrateParameterTree (juce::ValueTree&, int) {}

    namespace
    {
        using Attributes = juce::AudioParameterFloatAttributes;

        std::unique_ptr<juce::AudioParameterFloat> knobParam (const char* pid, const juce::String& name, float defaultValue)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, name, juce::NormalisableRange<float> (knobMin, knobMax, 0.01f), defaultValue,
                Attributes().withStringFromValueFunction ([] (float v, int) { return formatValue (v, 1); })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                return juce::jlimit (knobMin, knobMax, text.retainCharacters ("-0123456789.").getFloatValue());
                            }));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (knobParam (id::input, "Input", knobDefault));
        layout.add (knobParam (id::attack, "Attack", knobDefault));
        layout.add (knobParam (id::release, "Release", knobDefault));
        layout.add (knobParam (id::output, "Output", knobDefault));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::ratio, 1 }, "Ratio", ratioNames(), defaultRatioIndex));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::audioHp, 1 }, "Audio HP", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::distMode, 1 }, "Distortion", distModeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::detHp, 1 }, "Detector HP", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::detBand, 1 }, "Band Emphasis", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::stereoLink, 1 }, "Link", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::headroom, 1 }, "Headroom", headroomNames(), defaultHeadroomIndex));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::mix, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), mixDefault,
            Attributes().withLabel ("%")
                        .withStringFromValueFunction ([] (float v, int) { return formatValue (v, 1) + " %"; })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, 100.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, 1 }, "Bypass", false));
        return layout;
    }
}
