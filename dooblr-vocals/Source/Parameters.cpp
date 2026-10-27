#include "Parameters.h"

#include "DSP/ProcessingChain.h"

#include <cmath>
#include <functional>
#include <memory>

namespace dooblrvocals::params
{
    namespace
    {
        using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
        using Float = juce::AudioParameterFloat;
        using StringFn = std::function<juce::String (float, int)>;

        constexpr int parameterVersion = 1;

        juce::NormalisableRange<float> range (float lo, float hi, float centre = 0.0f)
        {
            juce::NormalisableRange<float> r (lo, hi);

            if (centre > lo && centre < hi)
                r.setSkewForCentre (centre);

            return r;
        }

        juce::NormalisableRange<float> logRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float start, float end, float v) { return start * std::pow (end / start, v); },
                     [] (float start, float end, float x) { return std::log (x / start) / std::log (end / start); } };
        }

        StringFn suffixed (const juce::String& unit, int decimals)
        {
            return [unit, decimals] (float v, int) { return juce::String (v, decimals) + unit; };
        }

        StringFn hzString()      { return [] (float v, int) { return formatHz (v); }; }
        StringFn percentString() { return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }; }

        StringFn msString()
        {
            return [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " ms"
                                            : v < 100.0f ? juce::String (v, 1) + " ms"
                                                         : juce::String (juce::roundToInt (v)) + " ms"; };
        }

        StringFn dbString()
        {
            return [] (float v, int) { return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
        }

        void addFloat (Layout& layout, const juce::String& id, const juce::String& name,
                       juce::NormalisableRange<float> r, float defaultValue, StringFn toString,
                       const juce::String& label = {})
        {
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction (std::move (toString))
                                  .withLabel (label);
            layout.add (std::make_unique<Float> (juce::ParameterID { id, parameterVersion }, name, r, defaultValue, attributes));
        }

        void addBool (Layout& layout, const juce::String& id, const juce::String& name, bool defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, parameterVersion }, name, defaultValue));
        }

        void addChoice (Layout& layout, const juce::String& id, const juce::String& name,
                        const juce::StringArray& choices, int defaultIndex)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, parameterVersion }, name, choices, defaultIndex));
        }

        void addInt (Layout& layout, const juce::String& id, const juce::String& name, int lo, int hi, int def)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, parameterVersion }, name, lo, hi, def));
        }
    }

    juce::StringArray chainOrderChoices()
    {
        juce::StringArray choices;

        for (int index = 0; index < dsp::numChainOrders; ++index)
        {
            juce::StringArray names;

            for (const auto stage : dsp::chainOrderFromIndex (index))
                names.add (dsp::chainStageName (stage));

            choices.add (names.joinIntoString (" > "));
        }

        return choices;
    }

    juce::StringArray keyChoices()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    juce::StringArray scaleChoices()
    {
        return { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
                 "Harm. minor", "Mel. minor", "Custom" };
    }

    juce::String noteName (int midiNote)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";

        return juce::String (hz, hz >= 100.0f ? 0 : 1) + " Hz";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        Layout layout;

        //== Pitch correction ============================================================
        addBool (layout, "pitch_on", "Pitch On", true);
        addChoice (layout, "corr_mode", "Mode", { "Keyed", "Chromatic" }, 0);
        addChoice (layout, "corr_key", "Key", keyChoices(), 0);
        addChoice (layout, "corr_scale", "Scale", scaleChoices(), 0);
        addInt (layout, "corr_mask", "Scale Mask", 0, 4095, dsp::maskFromDegrees (0, 2, 4, 5, 7, 9, 11));
        addFloat (layout, "retune_speed", "Retune Speed", range (0.0f, 400.0f, 20.0f), 20.0f, msString(), "ms");
        addFloat (layout, "humanize", "Humanize", range (0.0f, 1.0f), 0.15f, percentString());
        addFloat (layout, "corr_amount", "Amount", range (0.0f, 1.0f), 1.0f, percentString());
        addFloat (layout, "a4_hz", "A4", range (415.0f, 466.0f), 440.0f, hzString(), "Hz");
        addBool (layout, "formant", "Formant", true);
        addChoice (layout, "input_type", "Range", { "Soprano", "Alto", "Tenor", "Baritone", "Bass" }, 2);
        addChoice (layout, "quality", "Quality", { "Live", "HQ" }, 0);

        //== Gate ========================================================================
        addBool (layout, "gate_on", "Gate On", false);
        addFloat (layout, "gate_threshold", "Gate Threshold", range (-80.0f, 0.0f), -40.0f, dbString(), "dB");
        addFloat (layout, "gate_attack", "Gate Attack", range (0.1f, 50.0f, 1.0f), 1.0f, msString(), "ms");
        addFloat (layout, "gate_hold", "Gate Hold", range (0.0f, 500.0f, 50.0f), 50.0f, msString(), "ms");
        addFloat (layout, "gate_release", "Gate Release", range (1.0f, 1000.0f, 80.0f), 80.0f, msString(), "ms");
        addFloat (layout, "gate_range", "Gate Range", range (0.0f, 80.0f), 60.0f, dbString(), "dB");

        //== EQ ==========================================================================
        addBool (layout, "eq_on", "EQ On", false);
        addFloat (layout, "eq_low_freq", "EQ Low Freq", logRange (20.0f, 500.0f), 80.0f, hzString(), "Hz");
        addFloat (layout, "eq_low_gain", "EQ Low Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");
        addFloat (layout, "eq_mid_freq", "EQ Mid Freq", logRange (100.0f, 10000.0f), 1000.0f, hzString(), "Hz");
        addFloat (layout, "eq_mid_gain", "EQ Mid Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");
        addFloat (layout, "eq_mid_q", "EQ Mid Q", logRange (0.2f, 10.0f), 1.0f, suffixed ({}, 2));
        addFloat (layout, "eq_high_freq", "EQ High Freq", logRange (2000.0f, 20000.0f), 8000.0f, hzString(), "Hz");
        addFloat (layout, "eq_high_gain", "EQ High Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");

        //== Compressor ==================================================================
        addBool (layout, "comp_on", "Comp On", false);
        addFloat (layout, "comp_threshold", "Comp Threshold", range (-48.0f, 0.0f), -18.0f, dbString(), "dB");
        addFloat (layout, "comp_ratio", "Comp Ratio", range (1.0f, 20.0f, 4.0f), 3.0f, suffixed (":1", 1));
        addFloat (layout, "comp_attack", "Comp Attack", range (0.1f, 100.0f, 10.0f), 12.0f, msString(), "ms");
        addFloat (layout, "comp_release", "Comp Release", range (10.0f, 1000.0f, 100.0f), 120.0f, msString(), "ms");
        addFloat (layout, "comp_makeup", "Comp Makeup", range (0.0f, 24.0f), 0.0f, dbString(), "dB");

        //== Limiter / output ============================================================
        addChoice (layout, "lim_mode", "Master Mode", { "Off", "Clipper", "Limiter" }, 2);
        addFloat (layout, "lim_ceiling", "Ceiling", range (-24.0f, 0.0f), -0.3f, dbString(), "dB");
        addFloat (layout, "lim_release", "Limiter Release", range (1.0f, 500.0f, 60.0f), 60.0f, msString(), "ms");
        addFloat (layout, "output_gain", "Output", range (-24.0f, 6.0f), 0.0f, dbString(), "dB");

        addChoice (layout, "chain_order", "Chain Order", chainOrderChoices(), 0);

        return layout;
    }

    SettingsReader::SettingsReader (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        auto getId = [this] (const char* id) { return state.getRawParameterValue (id); };

        pitchOn = getId ("pitch_on"); corrMode = getId ("corr_mode"); corrKey = getId ("corr_key");
        corrScale = getId ("corr_scale"); corrMask = getId ("corr_mask");
        retune = getId ("retune_speed"); humanize = getId ("humanize"); amount = getId ("corr_amount");
        a4 = getId ("a4_hz"); formant = getId ("formant"); inputType = getId ("input_type"); quality = getId ("quality");
        gateOn = getId ("gate_on"); gateThreshold = getId ("gate_threshold"); gateAttack = getId ("gate_attack");
        gateHold = getId ("gate_hold"); gateRelease = getId ("gate_release"); gateRange = getId ("gate_range");
        eqOn = getId ("eq_on"); eqLowFreq = getId ("eq_low_freq"); eqLowGain = getId ("eq_low_gain");
        eqMidFreq = getId ("eq_mid_freq"); eqMidGain = getId ("eq_mid_gain"); eqMidQ = getId ("eq_mid_q");
        eqHighFreq = getId ("eq_high_freq"); eqHighGain = getId ("eq_high_gain");
        compOn = getId ("comp_on"); compThreshold = getId ("comp_threshold"); compRatio = getId ("comp_ratio");
        compAttack = getId ("comp_attack"); compRelease = getId ("comp_release"); compMakeup = getId ("comp_makeup");
        limMode = getId ("lim_mode"); limCeiling = getId ("lim_ceiling"); limRelease = getId ("lim_release");
        outputGain = getId ("output_gain"); chainOrder = getId ("chain_order");
    }

    dsp::VocalSettings SettingsReader::read() const noexcept
    {
        auto f = [] (Value v) { return v != nullptr ? v->load() : 0.0f; };
        auto b = [&] (Value v) { return f (v) > 0.5f; };
        auto i = [&] (Value v) { return juce::roundToInt (f (v)); };

        dsp::VocalSettings s;
        s.pitch.enabled = b (pitchOn);
        s.pitch.mode = (dsp::CorrectionMode) i (corrMode);
        s.pitch.key = i (corrKey);
        s.pitch.scale = (dsp::ScaleType) i (corrScale);
        s.pitch.customMask = i (corrMask);
        s.pitch.retuneMs = f (retune);
        s.pitch.humanize = f (humanize);
        s.pitch.amount = f (amount);
        s.pitch.a4Hz = f (a4);
        s.pitch.formant = b (formant);
        s.pitch.input = (dsp::InputType) i (inputType);
        s.pitch.quality = (dsp::QualityMode) i (quality);
        s.gate = { b (gateOn), f (gateThreshold), f (gateAttack), f (gateHold), f (gateRelease), f (gateRange) };
        s.eq = { b (eqOn), f (eqLowFreq), f (eqLowGain), f (eqMidFreq), f (eqMidGain), f (eqMidQ), f (eqHighFreq), f (eqHighGain) };
        s.compressor = { b (compOn), f (compThreshold), f (compRatio), f (compAttack), f (compRelease), f (compMakeup) };
        s.limiter = { i (limMode), f (limCeiling), f (limRelease), f (outputGain) };
        s.chainOrder = i (chainOrder);
        return s;
    }
}
