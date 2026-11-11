#include "Parameters.h"

#include <cmath>

namespace dooblrsub::params
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

    juce::String formatFrequency (double hz)
    {
        if (! std::isfinite (hz)) hz = 0.0;
        if (hz >= 1000.0) return formatValue (hz / 1000.0, 2) + " kHz";
        return formatValue (hz, 0) + " Hz";
    }

    juce::String formatCents (double cents)
    {
        if (! std::isfinite (cents)) cents = 0.0;
        const auto rounded = std::round (cents);
        if (std::abs (rounded) < 0.5) return "0 c";
        return (rounded > 0.0 ? "+" : "") + formatValue (rounded, 0) + " c";
    }

    void migrateParameterTree (juce::ValueTree&, int) {}

    juce::StringArray octaveNames() { return { "-1 oct", "Unison", "Both" }; }
    juce::StringArray targetNames() { return { "Club", "Festival / PA", "Home / Studio" }; }

    namespace
    {
        using Attributes = juce::AudioParameterFloatAttributes;

        std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* pid, const juce::String& name,
                                                               juce::NormalisableRange<float> range,
                                                               float defaultValue, const juce::String& unit, int decimals)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, name, range, defaultValue,
                Attributes().withLabel (unit)
                            .withStringFromValueFunction ([unit, decimals] (float v, int)
                            {
                                return formatValue (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
                            })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                return text.retainCharacters ("-0123456789.").getFloatValue();
                            }));
        }

        std::unique_ptr<juce::AudioParameterFloat> freqParam (const char* pid, const juce::String& name,
                                                              float lo, float hi, float defaultValue)
        {
            juce::NormalisableRange<float> range (lo, hi);
            range.setSkewForCentre (std::sqrt (lo * hi));
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, name, range, defaultValue,
                Attributes().withLabel ("Hz")
                            .withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                auto v = text.retainCharacters ("0123456789.").getFloatValue();
                                if (text.containsIgnoreCase ("k")) v *= 1000.0f;
                                return v;
                            }));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (floatParam (id::subAmount, "Sub", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
        layout.add (floatParam (id::loAmount, "Lo", { 0.0f, 100.0f, 0.1f }, 25.0f, "%", 0));
        layout.add (freqParam (id::frequency, "Frequency", freqMinHz, freqMaxHz, freqDefaultHz));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::octaveMode, 1 },
                                                                 "Octave", octaveNames(), 0));
        layout.add (floatParam (id::trackingSpeed, "Tracking", { 0.0f, 100.0f, 0.1f }, 55.0f, "%", 0));
        layout.add (floatParam (id::drive, "Drive", { 0.0f, 100.0f, 0.1f }, 20.0f, "%", 0));
        layout.add (floatParam (id::mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
        layout.add (floatParam (id::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB", 1));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::shift, 1 }, "Shift",
            juce::NormalisableRange<float> (shiftMinCents, shiftMaxCents, 1.0f), 0.0f,
            Attributes().withLabel ("c")
                        .withStringFromValueFunction ([] (float v, int) { return formatCents (v); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return text.retainCharacters ("-0123456789.").getFloatValue();
                        })));
        layout.add (floatParam (id::transient, "Transient", { 0.0f, 100.0f, 0.1f }, 80.0f, "%", 0));
        layout.add (floatParam (id::bodyMix, "Body", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
        layout.add (floatParam (id::shiftMix, "Shift Mix", { 0.0f, 100.0f, 0.1f }, 0.0f, "%", 0));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::monoBelow, 1 }, "Mono below", true));
        layout.add (freqParam (id::monoFreq, "Mono freq", monoMinHz, monoMaxHz, monoDefaultHz));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::infraHpf, 1 }, "Infra HPF", true));
        layout.add (freqParam (id::infraFreq, "Infra freq", infraMinHz, infraMaxHz, infraDefaultHz));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::subLimit, 1 }, "Sub limit", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::target, 1 },
                                                                 "Target", targetNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, 1 }, "Bypass", false));
        return layout;
    }
}
