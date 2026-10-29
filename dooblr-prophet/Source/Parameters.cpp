#include "Parameters.h"

namespace dooblrprophet::params
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

        void addOscillator (Layout& layout, int n)
        {
            const auto prefix = "Osc " + juce::String (n) + " ";
            const bool first = n == 1;
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid (first ? id::osc1Mode : id::osc2Mode), prefix + "Wave Mode",
                                                                      juce::StringArray { "Variable", "Switched" }, 0));
            layout.add (std::make_unique<juce::AudioParameterInt> (pid (first ? id::osc1Coarse : id::osc2Coarse), prefix + "Coarse", -24, 24, 0,
                                                                   juce::AudioParameterIntAttributes().withLabel ("st")));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (first ? id::osc1Fine : id::osc2Fine), prefix + "Fine",
                                                                     juce::NormalisableRange<float> (-50.0f, 50.0f), first ? 0.0f : 6.0f,
                                                                     Attributes().withLabel ("ct").withStringFromValueFunction ([] (float v, int)
                                                                     { return juce::String (v, 1) + " ct"; })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (first ? id::osc1Shape : id::osc2Shape), prefix + "Shape",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                                     Attributes().withStringFromValueFunction ([] (float v, int)
                                                                     {
                                                                         if (v < 0.02f) return juce::String ("Tri");
                                                                         if (std::abs (v - 0.5f) < 0.02f) return juce::String ("Saw");
                                                                         if (v > 0.98f) return juce::String ("Pulse");
                                                                         return v < 0.5f ? "Tri/Saw " + juce::String (juce::roundToInt (v * 200.0f)) + "%"
                                                                                         : "Saw/Pulse " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + "%";
                                                                     })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid (first ? id::osc1PW : id::osc2PW), prefix + "Pulse Width",
                                                                     juce::NormalisableRange<float> (0.05f, 0.95f), 0.5f,
                                                                     Attributes().withStringFromValueFunction (formatPercent)));
            addBool (layout, first ? id::osc1Saw : id::osc2Saw, prefix + "Saw", true);
            addBool (layout, first ? id::osc1Tri : id::osc2Tri, prefix + "Triangle", false);
            addBool (layout, first ? id::osc1Pulse : id::osc2Pulse, prefix + "Pulse", false);

            if (first)
            {
                addBool (layout, id::osc1Sync, "Osc 1 Hard Sync", false);
            }
            else
            {
                addBool (layout, id::osc2LowFreq, "Osc 2 Low Freq", false);
                addBool (layout, id::osc2KeyTrack, "Osc 2 Key Track", true);
            }
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        Layout layout;

        // Voices
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::voices), "Voices", 1, maxVoices, 6));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::voiceMode), "Voice Mode",
                                                                  juce::StringArray { "Poly", "Unison", "Mono" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::unisonVoices), "Unison Voices", 2, maxVoices, 4));
        addPercent (layout, id::unisonDetune, "Unison Detune", 0.25f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::glide), "Glide", skewed (0.0f, 5.0f, 0.3f), 0.0f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 { return v <= 0.0005f ? juce::String ("Off") : formatSeconds (v, 0); })));
        addPercent (layout, id::vintage, "Vintage", 0.3f);
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Bend Range", 0, 12, 2,
                                                               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                               { return juce::String (v) + " st"; })));
        addBipolar (layout, id::atFilter, "Aftertouch to Filter", 0.0f);
        addPercent (layout, id::atLfo, "Aftertouch to LFO", 0.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                                 Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::String (v, 1) + " dB"; })));

        addOscillator (layout, 1);
        addOscillator (layout, 2);

        addPercent (layout, id::mixOsc1, "Osc 1 Level", 0.8f);
        addPercent (layout, id::mixOsc2, "Osc 2 Level", 0.6f);
        addPercent (layout, id::mixSub, "Sub Level", 0.0f);
        addPercent (layout, id::mixNoise, "Noise Level", 0.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lpfCutoff), "Low-Pass Cutoff", skewed (20.0f, 20000.0f, 800.0f), 4000.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
        addPercent (layout, id::lpfReso, "Low-Pass Resonance", 0.15f);
        addPercent (layout, id::lpfDrive, "Low-Pass Drive", 0.2f);
        addBipolar (layout, id::lpfEnvAmt, "Low-Pass Env Amount", 0.35f);
        addPercent (layout, id::lpfKeyTrack, "Low-Pass Key Track", 0.5f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::hpfCutoff), "High-Pass Cutoff", skewed (20.0f, 20000.0f, 800.0f), 20.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
        addPercent (layout, id::hpfReso, "High-Pass Resonance", 0.0f);
        addBipolar (layout, id::hpfEnvAmt, "High-Pass Env Amount", 0.0f);

        addTime (layout, id::fenvAttack, "Filter Env Attack", 0.005f);
        addTime (layout, id::fenvDecay, "Filter Env Decay", 0.6f);
        addPercent (layout, id::fenvSustain, "Filter Env Sustain", 0.3f);
        addTime (layout, id::fenvRelease, "Filter Env Release", 0.5f);
        addPercent (layout, id::fenvVelAmt, "Filter Env Velocity", 0.3f);
        addTime (layout, id::aenvAttack, "Amp Env Attack", 0.003f);
        addTime (layout, id::aenvDecay, "Amp Env Decay", 0.8f);
        addPercent (layout, id::aenvSustain, "Amp Env Sustain", 0.8f);
        addTime (layout, id::aenvRelease, "Amp Env Release", 0.35f);
        addPercent (layout, id::aenvVelAmt, "Amp Env Velocity", 0.3f);

        addBipolar (layout, id::pmodFiltEnv, "Poly-Mod Filter Env", 0.0f);
        addPercent (layout, id::pmodOsc2, "Poly-Mod Osc 2", 0.0f);
        addBool (layout, id::pmodDestFreq1, "Poly-Mod to Osc 1 Freq", true);
        addBool (layout, id::pmodDestShape1, "Poly-Mod to Osc 1 Shape", false);
        addBool (layout, id::pmodDestFilter, "Poly-Mod to Filter", false);

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::lfoWave), "LFO Wave",
                                                                  juce::StringArray { "Triangle", "Saw", "Reverse Saw", "Square", "Sample & Hold" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoRate), "LFO Rate", skewed (0.05f, 40.0f, 4.0f), 5.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; })));
        addBool (layout, id::lfoSync, "LFO Sync", false);
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::lfoDivision), "LFO Division", lfoDivisionNames(), 6));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoInitial), "LFO Initial Amount", skewed (0.0f, 1.0f, 0.2f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPercent)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoWheel), "LFO Wheel Amount", skewed (0.0f, 1.0f, 0.2f), 0.25f,
                                                                 Attributes().withStringFromValueFunction (formatPercent)));
        addPercent (layout, id::lfoNoiseMix, "LFO Noise Mix", 0.0f);
        addBool (layout, id::lfoDestFreq1, "LFO to Osc 1 Freq", true);
        addBool (layout, id::lfoDestFreq2, "LFO to Osc 2 Freq", true);
        addBool (layout, id::lfoDestShape1, "LFO to Osc 1 Shape", false);
        addBool (layout, id::lfoDestShape2, "LFO to Osc 2 Shape", false);
        addBool (layout, id::lfoDestLpf, "LFO to Low-Pass", false);
        addBool (layout, id::lfoDestHpf, "LFO to High-Pass", false);
        addBool (layout, id::lfoDestAmp, "LFO to Amp", false);

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::lpfModel), "Low-Pass Model", lpfModelNames(), 0));
        // Added in 0.1.4, kept last so existing parameter indices do not move.
        addBool (layout, id::fenvSync, "Filter Env Sync to Amp", false);

        return layout;
    }

}
