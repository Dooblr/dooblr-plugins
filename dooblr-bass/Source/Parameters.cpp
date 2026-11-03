#include "Parameters.h"

namespace dooblrbass::params
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

        juce::String formatHz (float hz, int)
        {
            return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz";
        }

        juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

        juce::String formatBipolar (float v, int)
        {
            const auto p = juce::roundToInt (v * 100.0f);
            return (p > 0 ? "+" : "") + juce::String (p) + "%";
        }

        void addPercent (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
                                                                     Attributes().withStringFromValueFunction (formatPercent)));
        }

        void addBipolar (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (-1.0f, 1.0f), def,
                                                                     Attributes().withStringFromValueFunction (formatBipolar)));
        }

        void addTime (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, skewed (0.001f, 10.0f, 0.4f), def,
                                                                     Attributes().withStringFromValueFunction (formatSeconds).withLabel ("s")));
        }

        void addBool (Layout& layout, const char* id, const juce::String& name, bool def)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
        }

        void addPulseWidth (Layout& layout, const char* id, const juce::String& name)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (0.05f, 0.95f), 0.5f,
                                                                     Attributes().withStringFromValueFunction (formatPercent)));
        }

        void addTune (Layout& layout, const char* id, const juce::String& name)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (-7.0f, 7.0f), 0.0f,
                                                                     Attributes().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                                                                     {
                                                                         return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
                                                                     })));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        Layout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc1Wave), "Osc 1 Wave",
                                                                  juce::StringArray { "Saw", "Square" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc1Octave), "Osc 1 Octave", octaveNames(), 1));
        addPulseWidth (layout, id::osc1PW, "Osc 1 Pulse Width");

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc2Wave), "Osc 2 Wave",
                                                                  juce::StringArray { "Saw", "Square" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc2Octave), "Osc 2 Octave", octaveNames(), 1));
        addTune (layout, id::osc2Tune, "Osc 2 Tune");
        addPulseWidth (layout, id::osc2PW, "Osc 2 Pulse Width");

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc3Wave), "Osc 3 Wave",
                                                                  juce::StringArray { "Triangle", "Saw", "Square" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::osc3Octave), "Osc 3 Octave", octaveNames(), 1));
        addTune (layout, id::osc3Tune, "Osc 3 Tune");
        addBool (layout, id::osc3LoFreq, "Osc 3 Lo Freq", false);
        addBool (layout, id::osc3KeyTrack, "Osc 3 Key Track", true);

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::subOctave), "Sub Octave", subOctaveNames(), 0));

        addPercent (layout, id::mixOsc1, "Osc 1 Level", 0.85f);
        addPercent (layout, id::mixOsc2, "Osc 2 Level", 0.45f);
        addPercent (layout, id::mixOsc3, "Osc 3 Level", 0.0f);
        addPercent (layout, id::mixSub, "Sub Level", 0.35f);
        addPercent (layout, id::mixNoise, "Noise Level", 0.0f);
        addPercent (layout, id::mixDrive, "Drive", 0.25f);
        addPercent (layout, id::mixOverload, "Overload", 0.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lpfCutoff), "Cutoff", skewed (20.0f, 20000.0f, 400.0f), 420.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
        addPercent (layout, id::lpfReso, "Resonance", 0.28f);
        addBipolar (layout, id::lpfEnvAmt, "Filter Env Amount", 0.45f);
        addPercent (layout, id::lpfKeyTrack, "Key Track", 0.35f);

        addTime (layout, id::fenvAttack, "Filter Env Attack", 0.002f);
        addTime (layout, id::fenvDecay, "Filter Env Decay", 0.28f);
        addPercent (layout, id::fenvSustain, "Filter Env Sustain", 0.18f);
        addTime (layout, id::fenvRelease, "Filter Env Release", 0.22f);
        addPercent (layout, id::fenvVelAmt, "Filter Env Velocity", 0.35f);
        addTime (layout, id::aenvAttack, "Amp Env Attack", 0.002f);
        addTime (layout, id::aenvDecay, "Amp Env Decay", 0.45f);
        addPercent (layout, id::aenvSustain, "Amp Env Sustain", 0.85f);
        addTime (layout, id::aenvRelease, "Amp Env Release", 0.18f);
        addBool (layout, id::sharedDecay, "Shared Decay", false);

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::glide), "Glide", skewed (0.0f, 2.5f, 0.12f), 0.0f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 { return v <= 0.0005f ? juce::String ("Off") : formatSeconds (v, 0); })));
        addBool (layout, id::legato, "Legato", false);
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::notePriority), "Note Priority",
                                                                  juce::StringArray { "Last", "Low" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Bend Range", 0, 12, 2,
                                                               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                               { return juce::String (v) + " st"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                                 Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::String (v, 1) + " dB"; })));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::lfoWave), "LFO Wave",
                                                                  juce::StringArray { "Triangle", "Saw", "Reverse Saw", "Square", "Sample & Hold" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoRate), "LFO Rate", skewed (0.05f, 30.0f, 3.0f), 4.5f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; })));
        addBool (layout, id::lfoSync, "LFO Sync", false);
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::lfoDivision), "LFO Division", lfoDivisionNames(), 6));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoToPitch), "LFO to Pitch", skewed (0.0f, 1.0f, 0.2f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPercent)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoToFilter), "LFO to Filter", skewed (0.0f, 1.0f, 0.2f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPercent)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoWheel), "LFO Wheel Amount", skewed (0.0f, 1.0f, 0.2f), 0.3f,
                                                                 Attributes().withStringFromValueFunction (formatPercent)));

        return layout;
    }
}
