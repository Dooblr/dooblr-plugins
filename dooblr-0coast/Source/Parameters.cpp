#include "Parameters.h"

#include <cmath>

namespace dooblr0coast::params
{
    juce::String formatValue (double value, int decimals)
    {
        decimals = juce::jlimit (0, 6, decimals);
        const auto scale = std::pow (10.0, (double) decimals);
        auto rounded = std::round (value * scale) / scale;
        if (! std::isfinite (rounded))
            rounded = 0.0;
        if (std::abs (rounded) < 0.5 / scale)
            rounded = 0.0;
        if (decimals == 0)
            return juce::String (juce::roundToInt (rounded));
        return juce::String (rounded, decimals);
    }

    void migrateParameterTree (juce::ValueTree&, int) {}

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
            if (! std::isfinite (seconds))
                seconds = 0.0f;
            return seconds < 1.0f ? juce::String (seconds * 1000.0f, seconds < 0.1f ? 1 : 0) + " ms"
                                  : juce::String (seconds, 2) + " s";
        }

        juce::String formatHz (float hz, int)
        {
            if (! std::isfinite (hz))
                hz = 0.0f;
            return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz"
                                : juce::String (hz / 1000.0f, 2) + " kHz";
        }

        juce::String formatPercent (float v, int)
        {
            return formatValue (v * 100.0, 0) + " %";
        }

        juce::String formatBipolar (float v, int)
        {
            const auto p = juce::roundToInt (v * 100.0f);
            return (p > 0 ? "+" : "") + juce::String (p) + " %";
        }

        juce::String formatDb (float v, int)
        {
            const auto s = formatValue ((double) v, 1);
            return (v > 0.0f ? "+" : "") + s + " dB";
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
                Attributes().withStringFromValueFunction (formatBipolar)));
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
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        Layout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::pitch), "Pitch", juce::NormalisableRange<float> (-5.0f, 5.0f, 0.01f), 0.0f,
            Attributes().withLabel ("oct").withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.0f ? "+" : "") + formatValue ((double) v, 2) + " oct";
            })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::fine), "Fine", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
            Attributes().withLabel ("ct").withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.0f ? "+" : "") + formatValue ((double) v, 1) + " ct";
            })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::portamento), "Portamento", skewed (0.0f, 2.0f, 0.12f), 0.0f,
            Attributes().withStringFromValueFunction (formatSeconds).withLabel ("s")));

        addPercent (layout, id::linFm, "Lin FM", 0.0f);
        addPercent (layout, id::overtone, "Overtone", 0.12f);
        addPercent (layout, id::overtoneCv, "Overtone CV", 0.0f);
        addPercent (layout, id::multiply, "Multiply", 0.18f);
        addBipolar (layout, id::multiplyCv, "Multiply CV", 0.0f);

        addBool (layout, id::slopeCycle, "Slope Cycle", false);
        addTime (layout, id::slopeRise, "Rise", 0.22f);
        addTime (layout, id::slopeFall, "Fall", 0.28f);
        addPercent (layout, id::slopeResponse, "Slope Response", 0.5f);

        addTime (layout, id::contourOnset, "Onset", 0.004f);
        addPercent (layout, id::contourSustain, "Sustain", 0.72f);
        addTime (layout, id::contourDecay, "Decay", 0.28f);
        addPercent (layout, id::contourResponse, "Contour Response", 0.62f);

        addPercent (layout, id::balance, "Balance", 0.32f);
        addPercent (layout, id::balanceCv, "Balance CV", 1.0f);
        addPercent (layout, id::dynamics, "Dynamics", 0.88f);
        addPercent (layout, id::level, "Level", 0.72f);

        addBool (layout, id::arpOn, "Arp", false);
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::arpType), "Arp Type", arpTypeNames(), 0));
        addBool (layout, id::legato, "Legato", true);
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::midiBCv), "MIDI B CV", midiBCvNames(), 1));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::midiBGate), "MIDI B Gate", midiBGateNames(), 2));
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::clockDiv), "Clock", clockDivNames(), 3));
        addBipolar (layout, id::mathAmt, "Math", 0.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::midiBLfoRate), "MIDI B LFO", skewed (0.05f, 20.0f, 2.0f), 2.0f,
            Attributes().withStringFromValueFunction (formatHz).withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Bend Range", 0, 12, 2));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            pid (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
            Attributes().withStringFromValueFunction (formatDb).withLabel ("dB")));

        return layout;
    }
}
