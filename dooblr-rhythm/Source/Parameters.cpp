#include "Parameters.h"
#include "Engine/RhythmEngine.h"

#include <cmath>

namespace dooblrrhythm::params
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

    juce::String formatDb (double db)
    {
        if (! std::isfinite (db) || db <= -59.0) return "-inf dB";
        return (db > 0.0 ? "+" : "") + formatValue (db, 1) + " dB";
    }

    juce::String formatPan (double pan)
    {
        if (std::abs (pan) < 0.5) return "C";
        return formatValue (std::abs (pan), 0) + (pan < 0.0 ? " L" : " R");
    }

    juce::String instrumentParam (int instrument, const char* suffix)
    {
        instrument = juce::jlimit (0, numInstruments - 1, instrument);
        return juce::String (instrumentIds[instrument]) + "_" + suffix;
    }

    int instrumentIndexFromParam (const juce::String& parameterId)
    {
        const auto slug = parameterId.upToFirstOccurrenceOf ("_", false, false);
        for (int i = 0; i < numInstruments; ++i)
            if (slug == instrumentIds[i])
                return i;
        return -1;
    }

    juce::StringArray styleChoices()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) Style::count; ++i)
            a.add (styleNames[i]);
        return a;
    }

    juce::StringArray timeSignatureChoices()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) TimeSignature::count; ++i)
            a.add (timeSignatureNames[i]);
        return a;
    }

    juce::StringArray claveChoices() { return { claveNames[0], claveNames[1] }; }
    juce::StringArray sectionChoices()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) Section::count; ++i)
            a.add (sectionNames[i]);
        return a;
    }

    void migrateParameterTree (juce::ValueTree&, int) {}

    namespace
    {
        using Attributes = juce::AudioParameterFloatAttributes;

        std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& pid, const juce::String& name,
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
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::style, 1 },
                                                                 "Style", styleChoices(), 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id::patternLength, 1 },
                                                              "Length", minBars, maxBars, 4));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::timeSignature, 1 },
                                                                 "Time Sig", timeSignatureChoices(), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::claveDirection, 1 },
                                                                 "Clave", claveChoices(), 0));
        layout.add (floatParam (id::complexity, "Complexity", { 0.0f, 100.0f, 0.1f }, 45.0f, "%", 0));
        layout.add (floatParam (id::syncopation, "Syncopation", { 0.0f, 100.0f, 0.1f }, 40.0f, "%", 0));
        layout.add (floatParam (id::humanize, "Humanize", { 0.0f, 100.0f, 0.1f }, 25.0f, "%", 0));
        layout.add (floatParam (id::swing, "Swing", { 0.0f, 100.0f, 0.1f }, 0.0f, "%", 0));
        layout.add (floatParam (id::pushPull, "Push / Pull", { -100.0f, 100.0f, 0.1f }, 0.0f, "%", 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::section, 1 },
                                                                 "Section", sectionChoices(), 1));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::output, 1 }, "Output",
            juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            return text.retainCharacters ("-0123456789.").getFloatValue();
                        })));

        for (int i = 0; i < numInstruments; ++i)
        {
            const auto name = juce::String (instrumentNames[i]);
            const auto inEnsemble = inStyleEnsemble (Style::sonSalsa, (Instrument) i);
            const auto dens = inEnsemble ? 70.0f : 0.0f;
            layout.add (floatParam (instrumentParam (i, "density"),     name + " Density",     { 0.0f, 100.0f, 0.1f }, dens, "%", 0));
            layout.add (floatParam (instrumentParam (i, "complexity"),  name + " Complexity",  { 0.0f, 100.0f, 0.1f }, 40.0f, "%", 0));
            layout.add (floatParam (instrumentParam (i, "syncopation"), name + " Syncopation", { 0.0f, 100.0f, 0.1f }, 35.0f, "%", 0));
            layout.add (floatParam (instrumentParam (i, "velocity"),    name + " Velocity",    { 0.0f, 100.0f, 0.1f }, 80.0f, "%", 0));
            layout.add (floatParam (instrumentParam (i, "humanize"),    name + " Humanize",    { 0.0f, 100.0f, 0.1f }, 20.0f, "%", 0));
            layout.add (floatParam (instrumentParam (i, "timing"),      name + " Timing",      { -100.0f, 100.0f, 0.1f }, 0.0f, "%", 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { instrumentParam (i, "volume"), 1 }, name + " Volume",
                juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
                Attributes().withLabel ("dB")
                            .withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                return text.retainCharacters ("-0123456789.").getFloatValue();
                            })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { instrumentParam (i, "pan"), 1 }, name + " Pan",
                juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                Attributes().withStringFromValueFunction ([] (float v, int) { return formatPan (v); })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                return text.retainCharacters ("-0123456789.").getFloatValue();
                            })));
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { instrumentParam (i, "mute"), 1 },
                                                                   name + " Mute", false));
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { instrumentParam (i, "solo"), 1 },
                                                                   name + " Solo", false));
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { instrumentParam (i, "lock"), 1 },
                                                                   name + " Lock", false));
            layout.add (floatParam (instrumentParam (i, "pitch"), name + " Pitch", { -12.0f, 12.0f, 0.01f }, 0.0f, "st", 2));
            layout.add (floatParam (instrumentParam (i, "decay"), name + " Decay", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
        }

        return layout;
    }
}
