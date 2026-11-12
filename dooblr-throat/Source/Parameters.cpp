#include "Parameters.h"

namespace dooblrthroat::params
{
    namespace
    {
        using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
        using Attributes = juce::AudioParameterFloatAttributes;

        juce::ParameterID pid (const char* name) { return { name, 1 }; }

        juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> range (lo, hi);
            range.setSkewForCentre (centre);
            return range;
        }

        juce::String formatSeconds (float seconds, int)
        {
            return seconds < 1.0f ? juce::String (seconds * 1000.0f, seconds < 0.1f ? 1 : 0) + " ms"
                                  : juce::String (seconds, 2) + " s";
        }

        juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

        juce::String formatDb (float v, int)
        {
            return juce::String (v, 1) + " dB";
        }

        juce::String formatHz (float hz, int)
        {
            return juce::String (hz, 1) + " Hz";
        }

        juce::String formatCmH2O (float v, int)
        {
            return juce::String (v, 1) + " cm";
        }

        juce::String formatRate (float v, int)
        {
            return juce::String (v, 2) + " x";
        }

        void addPercent (Layout& layout, const char* id, const juce::String& name, float def,
                         float lo = 0.0f, float hi = 1.0f)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name,
                juce::NormalisableRange<float> (lo, hi), def,
                Attributes().withStringFromValueFunction (formatPercent)));
        }

        void addTime (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, skewed (0.001f, 4.0f, 0.2f), def,
                Attributes().withStringFromValueFunction (formatSeconds).withLabel ("s")));
        }

        void addBool (Layout& layout, const char* id, const juce::String& name, bool def)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        Layout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::vowel), "Vowel", vowelNames(), 14)); // sofa
        addPercent (layout, id::height, "Height", 0.5f);
        addPercent (layout, id::back, "Back", 0.5f);
        addPercent (layout, id::nasal, "Nasal", 0.0f, 0.0f, 1.2f);
        addPercent (layout, id::female, "Female", 0.0f);
        addPercent (layout, id::child, "Child", 0.0f);

        addPercent (layout, id::breath, "Breath", 0.15f);
        addPercent (layout, id::tension, "Tension", 0.5f);
        addPercent (layout, id::brightness, "Bright", 0.25f);
        addPercent (layout, id::fry, "Fry", 0.0f);
        addPercent (layout, id::roughness, "Rough", 0.1f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::pressure), "Pressure",
            juce::NormalisableRange<float> (1.0f, 20.0f), 8.0f,
            Attributes().withStringFromValueFunction (formatCmH2O).withLabel ("cm")));
        addBool (layout, id::voiced, "Voiced", true);

        addPercent (layout, id::vibratoDepth, "Vibrato", 0.0f, 0.0f, 0.2f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::vibratoRate), "Vib Rate",
            juce::NormalisableRange<float> (2.0f, 10.0f), 6.0f,
            Attributes().withStringFromValueFunction (formatHz).withLabel ("Hz")));
        addTime (layout, id::glide, "Glide", 0.06f);
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Bend", 0, 24, 2));

        addTime (layout, id::attack, "Attack", 0.03f);
        addTime (layout, id::decay, "Decay", 0.08f);
        addPercent (layout, id::sustain, "Sustain", 0.85f);
        addTime (layout, id::release, "Release", 0.12f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::output), "Output",
            juce::NormalisableRange<float> (-24.0f, 6.0f), -6.0f,
            Attributes().withStringFromValueFunction (formatDb).withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::mode), "Mode", modeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::rate), "Rate",
            juce::NormalisableRange<float> (0.25f, 2.5f, 0.0f, 0.6f), 1.0f,
            Attributes().withStringFromValueFunction (formatRate).withLabel ("x")));

        return layout;
    }
}
