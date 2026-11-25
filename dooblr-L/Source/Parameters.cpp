#include "Parameters.h"

#include <cmath>
#include <limits>

namespace dooblrl::params
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

    juce::StringArray styleNames()
    {
        return { "Transparent", "Punchy", "Dynamic", "Allround", "Aggressive", "Modern", "Bus", "Safe" };
    }

    juce::StringArray styleShortNames()
    {
        return { "Trans", "Punch", "Dyn", "All", "Aggr", "Mod", "Bus", "Safe" };
    }

    juce::StringArray oversamplingNames() { return { "Off", "2x", "4x", "8x", "16x", "32x" }; }
    juce::StringArray ditherNames()       { return { "Off", "16-bit", "24-bit" }; }
    juce::StringArray shapingNames()      { return { "Off", "Low", "Medium", "High" }; }
    juce::StringArray lufsTargetNames()   { return { "Off", "-23 LUFS", "-16 LUFS", "-14 LUFS", "-12 LUFS" }; }

    int oversamplingFactor (int index)
    {
        static constexpr int factors[] { 1, 2, 4, 8, 16, 32 };
        return factors[juce::jlimit (0, numOversampling - 1, index)];
    }

    float lufsTargetValue (int index)
    {
        static constexpr float values[] { 0.0f, -23.0f, -16.0f, -14.0f, -12.0f };
        index = juce::jlimit (0, numLufsTargets - 1, index);
        return index == 0 ? std::numeric_limits<float>::quiet_NaN() : values[index];
    }

    void migrateParameterTree (juce::ValueTree&, int) {}

    namespace
    {
        using Attributes = juce::AudioParameterFloatAttributes;

        juce::String dbText (float v, int decimals)
        {
            const auto t = formatValue ((double) v, decimals);
            return (v > 0.0f ? "+" + t : t) + " dB";
        }

        juce::String percentText (float v) { return formatValue ((double) v, 1) + " %"; }
        juce::String msText (float v, int decimals) { return formatValue ((double) v, decimals) + " ms"; }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::threshold, 1 }, "Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), thresholdDefault,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return dbText (v, 1); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (-40.0f, 0.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::attack, 1 }, "Attack",
            juce::NormalisableRange<float> (0.01f, 40.0f, 0.01f, 0.4f), attackDefaultMs,
            Attributes().withLabel ("ms")
                        .withStringFromValueFunction ([] (float v, int) { return msText (v, v < 1.0f ? 2 : 1); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.01f, 40.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::release, 1 }, "Release",
            juce::NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.35f), releaseDefaultMs,
            Attributes().withLabel ("ms")
                        .withStringFromValueFunction ([] (float v, int) { return msText (v, v < 10.0f ? 1 : 0); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (1.0f, 2000.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::knee, 1 }, "Knee",
            juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), kneeDefaultDb,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return formatValue ((double) v, 1) + " dB"; })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, 12.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::enhance, 1 }, "Enhance",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), enhanceDefault,
            Attributes().withLabel ("%")
                        .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, 100.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id::style, 1 }, "Style", styleNames(), defaultStyleIndex));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id::oversampling, 1 }, "Oversampling", oversamplingNames(), defaultOversamplingIndex));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::lookahead, 1 }, "Lookahead",
            juce::NormalisableRange<float> (0.0f, lookaheadMaxMs, 0.01f, 0.45f), lookaheadDefaultMs,
            Attributes().withLabel ("ms")
                        .withStringFromValueFunction ([] (float v, int) { return msText (v, v < 10.0f ? 2 : 1); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, lookaheadMaxMs, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::transientLink, 1 }, "Transient Link",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), transientLinkDefault,
            Attributes().withLabel ("%")
                        .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, 100.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::releaseLink, 1 }, "Release Link",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), releaseLinkDefault,
            Attributes().withLabel ("%")
                        .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (0.0f, 100.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::inputGain, 1 }, "Input",
            juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), inputGainDefault,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return dbText (v, 1); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (-12.0f, 24.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::ceiling, 1 }, "Ceiling",
            juce::NormalisableRange<float> (-12.0f, 0.0f, 0.01f), ceilingDefault,
            Attributes().withLabel ("dBTP")
                        .withStringFromValueFunction ([] (float v, int) { return formatValue ((double) v, 2) + " dBTP"; })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return juce::jlimit (-12.0f, 0.0f, text.retainCharacters ("-0123456789.").getFloatValue());
                        })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::unityGain, 1 }, "Unity Gain", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::delta, 1 }, "Delta", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::autoComp, 1 }, "Auto Comp", true));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id::dither, 1 }, "Dither", ditherNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id::shaping, 1 }, "Shaping", shapingNames(), defaultShapingIndex));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id::lufsTarget, 1 }, "LUFS Target", lufsTargetNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::bypass, 1 }, "Bypass", false));

        return layout;
    }
}
