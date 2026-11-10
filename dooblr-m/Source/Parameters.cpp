#include "Parameters.h"

namespace dooblrm::params
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
            return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz"
                                : juce::String (hz / 1000.0f, 2) + " kHz";
        }

        juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

        juce::String formatSemitones (float v, int)
        {
            return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
        }

        void addPercent (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
                Attributes().withStringFromValueFunction (formatPercent)));
        }

        void addBipolar (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, juce::NormalisableRange<float> (-1.0f, 1.0f), def,
                Attributes().withStringFromValueFunction ([] (float v, int)
                {
                    const auto p = juce::roundToInt (v * 100.0f);
                    return (p > 0 ? "+" : "") + juce::String (p) + "%";
                })));
        }

        void addTime (Layout& layout, const char* id, const juce::String& name, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, skewed (0.001f, 10.0f, 0.4f), def,
                Attributes().withStringFromValueFunction (formatSeconds).withLabel ("s")));
        }

        void addBool (Layout& layout, const char* id, const juce::String& name, bool def)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
        }

        void addChoice (Layout& layout, const char* id, const juce::String& name,
                        const juce::StringArray& names, int def)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id), name, names, def));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        Layout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::tune), "Tune", juce::NormalisableRange<float> (-2.5f, 2.5f), 0.0f,
            Attributes().withLabel ("st").withStringFromValueFunction (formatSemitones)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::glide), "Glide", skewed (0.0f, 2.5f, 0.12f), 0.0f,
            Attributes().withStringFromValueFunction (formatSeconds).withLabel ("s")));
        addPercent (layout, id::modMix, "Modulation Mix", 0.0f);
        addBool (layout, id::osc3Control, "Osc 3 Control", true);
        addBool (layout, id::oscMod, "Oscillator Mod", false);
        addBool (layout, id::filterMod, "Filter Mod", false);

        addChoice (layout, id::osc1Range, "Osc 1 Range", rangeNames(), 2); // 8'
        addChoice (layout, id::osc1Wave, "Osc 1 Waveform", waveNames(), 2); // saw
        addChoice (layout, id::osc2Range, "Osc 2 Range", rangeNames(), 2);
        addChoice (layout, id::osc2Wave, "Osc 2 Waveform", waveNames(), 2);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::osc2Freq), "Osc 2 Frequency", juce::NormalisableRange<float> (-8.0f, 8.0f), 0.0f,
            Attributes().withLabel ("st").withStringFromValueFunction (formatSemitones)));
        addChoice (layout, id::osc3Range, "Osc 3 Range", rangeNames(), 2);
        addChoice (layout, id::osc3Wave, "Osc 3 Waveform", waveNames(), 0); // triangle, the usual mod source
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::osc3Freq), "Osc 3 Frequency", juce::NormalisableRange<float> (-8.0f, 8.0f), 0.0f,
            Attributes().withLabel ("st").withStringFromValueFunction (formatSemitones)));

        addPercent (layout, id::mixOsc1, "Osc 1", 0.8f);
        addBool (layout, id::mixOsc1On, "Osc 1 On", true);
        addPercent (layout, id::mixOsc2, "Osc 2", 0.0f);
        addBool (layout, id::mixOsc2On, "Osc 2 On", false);
        addPercent (layout, id::mixOsc3, "Osc 3", 0.0f);
        addBool (layout, id::mixOsc3On, "Osc 3 On", false);
        addPercent (layout, id::mixExt, "Ext / Feedback", 0.0f);
        addBool (layout, id::mixExtOn, "Ext On", false);
        addPercent (layout, id::mixNoise, "Noise", 0.0f);
        addBool (layout, id::mixNoiseOn, "Noise On", false);
        addChoice (layout, id::noiseType, "Noise Type", noiseNames(), 0);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::cutoff), "Cutoff", skewed (20.0f, 20000.0f, 800.0f), 1200.0f,
            Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
        addPercent (layout, id::emphasis, "Emphasis", 0.15f);
        addPercent (layout, id::contourAmt, "Amount of Contour", 0.0f);
        addTime (layout, id::fAttack, "Filter Attack", 0.002f);
        addTime (layout, id::fDecay, "Filter Decay", 0.4f);
        addPercent (layout, id::fSustain, "Filter Sustain", 0.0f);
        addBool (layout, id::kybd1, "Keyboard Control 1", false);
        addBool (layout, id::kybd2, "Keyboard Control 2", false);

        addTime (layout, id::aAttack, "Loudness Attack", 0.002f);
        addTime (layout, id::aDecay, "Loudness Decay", 0.5f);
        addPercent (layout, id::aSustain, "Loudness Sustain", 1.0f);
        addBool (layout, id::decaySwitch, "Decay", true);

        addPercent (layout, id::volume, "Volume", 0.7f);
        addBool (layout, id::a440, "A-440", false);

        addChoice (layout, id::notePriority, "Note Priority", priorityNames(), 0); // low, like the original
        addBool (layout, id::legato, "Legato", true);
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Bend Range", 0, 12, 2));
        addBool (layout, id::velocityOn, "Velocity", false);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
            Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
            })));

        addPercent (layout, id::modWheel, "Mod Wheel", 0.0f);
        addBipolar (layout, id::pitchBend, "Pitch Bend", 0.0f);

        return layout;
    }
}
