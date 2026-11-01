#include "Parameters.h"

#include "DSP/ProcessingChain.h"

#include <array>
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

    juce::String harmonyId (int voice, const char* suffix)
    {
        return "harm" + juce::String (voice + 1) + "_" + suffix;
    }

    const dsp::HarmonyVoice& defaultHarmonyVoice (int voice)
    {
        // Voices 1 / 2: a third and a fifth above in the key; 3 / 4: octaves (off).
        static const std::array<dsp::HarmonyVoice, dsp::numHarmonyVoices> defaults { {
            { true,  false,  2,   4, -3.0f, -0.35f, 0.0f, 0.0f },
            { true,  false,  4,   7, -4.0f,  0.35f, 0.0f, 0.0f },
            { false, true,  -7, -12, -6.0f, -0.70f, 0.0f, 0.0f },
            { false, true,   7,  12, -6.0f,  0.70f, 0.0f, 0.0f },
        } };
        return defaults[(size_t) juce::jlimit (0, dsp::numHarmonyVoices - 1, voice)];
    }

    juce::String degreeName (int steps)
    {
        static const char* names[] = { "Unison", "2nd", "3rd", "4th", "5th", "6th", "7th", "Oct",
                                       "9th", "10th", "11th", "12th", "13th", "14th", "2 Oct" };
        const auto a = juce::jlimit (0, 14, std::abs (steps));
        return a == 0 ? juce::String (names[0]) : juce::String (steps > 0 ? "+" : "-") + names[a];
    }

    int degreeFromName (const juce::String& text)
    {
        const auto t = text.trim();
        for (int steps = -14; steps <= 14; ++steps)
            if (t.equalsIgnoreCase (degreeName (steps)) || (steps > 0 && t.equalsIgnoreCase (degreeName (steps).substring (1))))
                return steps;
        return juce::jlimit (-14, 14, t.retainCharacters ("-0123456789").getIntValue());
    }

    juce::String panName (float pan)
    {
        const auto p = juce::roundToInt (pan * 100.0f);
        return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p));
    }

    juce::String noteName (int midiNote)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4); // scientific pitch: A4 = 440 Hz, middle C = C4
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
        addBool (layout, "eq_hpf_on", "EQ High-Pass On", false);
        addFloat (layout, "eq_hpf_freq", "EQ High-Pass Freq", logRange (20.0f, 500.0f), 80.0f, hzString(), "Hz");
        addChoice (layout, "eq_hpf_slope", "EQ High-Pass Slope", { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);

        //== Compressor ==================================================================
        addBool (layout, "comp_on", "Comp On", false);
        addFloat (layout, "comp_threshold", "Comp Threshold", range (-48.0f, 0.0f), -18.0f, dbString(), "dB");
        addFloat (layout, "comp_ratio", "Comp Ratio", range (1.0f, 20.0f, 4.0f), 3.0f, suffixed (":1", 1));
        addFloat (layout, "comp_attack", "Comp Attack", range (0.1f, 100.0f, 10.0f), 12.0f, msString(), "ms");
        addFloat (layout, "comp_release", "Comp Release", range (10.0f, 1000.0f, 100.0f), 120.0f, msString(), "ms");
        addFloat (layout, "comp_makeup", "Comp Makeup", range (0.0f, 24.0f), 0.0f, dbString(), "dB");
        // Default 6 dB = the knee the compressor always had, so existing presets and sessions render bit-identically.
        addFloat (layout, "comp_knee", "Comp Knee", range (0.0f, 24.0f), 6.0f, dbString(), "dB");

        //== Limiter / output ============================================================
        addChoice (layout, "lim_mode", "Master Mode", { "Off", "Clipper", "Limiter" }, 2);
        addFloat (layout, "lim_ceiling", "Ceiling", range (-24.0f, 0.0f), -0.3f, dbString(), "dB");
        addFloat (layout, "lim_release", "Limiter Release", range (1.0f, 500.0f, 60.0f), 60.0f, msString(), "ms");
        addFloat (layout, "output_gain", "Output", range (-24.0f, 6.0f), 0.0f, dbString(), "dB");

        addChoice (layout, "chain_order", "Chain Order", chainOrderChoices(), 0);

        //== v0.2: appended so older sessions keep their parameter indices ==================
        // Default Mix is the v0.1 Live path, so a v0.1 session sounds and reports the same.
        addChoice (layout, "latency_mode", "Latency", { "Tracking", "Mix" }, 1);

        addBool (layout, "harm_on", "Harmony On", false);
        addFloat (layout, "harm_level", "Harmony Level", range (-60.0f, 6.0f, -12.0f), -6.0f, dbString(), "dB");
        addBool (layout, "harm_formant", "Harmony Formant", true);

        for (int v = 0; v < dsp::numHarmonyVoices; ++v)
        {
            const auto& d = defaultHarmonyVoice (v);
            const auto id = [v] (const char* suffix) { return harmonyId (v, suffix); };
            const auto name = [v] (const char* what) { return "Voice " + juce::String (v + 1) + " " + what; };

            addBool (layout, id ("on"), name ("On"), d.enabled);
            addChoice (layout, id ("mode"), name ("Mode"), { "Scale", "Fixed" }, d.fixed ? 1 : 0);
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { id ("degree"), parameterVersion }, name ("Scale Interval"), -14, 14, d.degrees,
                juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int value, int) { return degreeName (value); })
                    .withValueFromStringFunction ([] (const juce::String& text) { return degreeFromName (text); })));
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { id ("semis"), parameterVersion }, name ("Semitones"), -24, 24, d.semitones,
                juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int value, int) { return (value > 0 ? "+" : "") + juce::String (value) + " st"; })
                    .withValueFromStringFunction ([] (const juce::String& text) { return text.retainCharacters ("-0123456789").getIntValue(); })));
            addFloat (layout, id ("level"), name ("Level"), range (-60.0f, 6.0f, -12.0f), d.levelDb, dbString(), "dB");
            addFloat (layout, id ("pan"), name ("Pan"), range (-1.0f, 1.0f), d.pan, [] (float x, int) { return panName (x); });
            addFloat (layout, id ("detune"), name ("Detune"), range (-50.0f, 50.0f), d.detuneCents,
                      [] (float c, int) { return (c > 0.05f ? "+" : "") + juce::String (c, 1) + " c"; }, "cents");
            addFloat (layout, id ("delay"), name ("Delay"), range (0.0f, 30.0f, 8.0f), d.delayMs, msString(), "ms");
        }

        //== v0.3: time FX (delay + reverb block) and the learned vocal range ===============
        // Everything defaults to off / unchanged, so older sessions sound identical.
        {
            juce::StringArray notes, preSyncs;
            for (int n = 0; n < dsp::numNoteValues; ++n) notes.add (dsp::noteValueName (n));
            for (int n = 0; n < dsp::numPreDelaySyncs; ++n) preSyncs.add (dsp::preDelaySyncName (n));
            const dsp::ReverbSettings r;
            const dsp::DelaySettings d;
            const auto pct150 = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
            const auto secs = [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; };
            const auto duck = [] (float v, int) { return v < 0.05f ? juce::String ("Off") : "-" + juce::String (v, 1) + " dB"; };

            addBool (layout, "rev_on", "Reverb On", false);
            addFloat (layout, "rev_predelay", "Reverb Pre-delay", range (0.0f, 250.0f, 40.0f), r.preDelayMs, msString(), "ms");
            addChoice (layout, "rev_predelay_sync", "Reverb Pre-delay Sync", preSyncs, 0);
            addFloat (layout, "rev_decay", "Reverb Decay", range (0.2f, 12.0f, 2.0f), r.decaySeconds, secs, "s");
            addFloat (layout, "rev_size", "Reverb Size", range (0.0f, 1.0f), r.size, percentString());
            addFloat (layout, "rev_damping", "Reverb HF Damping", range (0.0f, 1.0f), r.damping, percentString());
            addFloat (layout, "rev_lowcut", "Reverb Low Cut", logRange (20.0f, 1000.0f), r.lowCutHz, hzString(), "Hz");
            addFloat (layout, "rev_highcut", "Reverb High Cut", logRange (1000.0f, 20000.0f), r.highCutHz, hzString(), "Hz");
            addFloat (layout, "rev_width", "Reverb Width", range (0.0f, 1.5f), r.width, pct150);
            addFloat (layout, "rev_early", "Reverb Early / Late", range (0.0f, 1.0f), r.earlyLate,
                      [] (float v, int) { return juce::String (juce::roundToInt ((1.0f - v) * 100.0f)) + "/" + juce::String (juce::roundToInt (v * 100.0f)); });
            addFloat (layout, "rev_diffusion", "Reverb Diffusion", range (0.0f, 1.0f), r.diffusion, percentString());
            addFloat (layout, "rev_mod", "Reverb Modulation", range (0.0f, 1.0f), r.modulation, percentString());
            addFloat (layout, "rev_duck", "Reverb Ducking", range (0.0f, 24.0f), r.duckDb, duck, "dB");
            addFloat (layout, "rev_duck_release", "Reverb Duck Release", range (20.0f, 2000.0f, 300.0f), r.duckReleaseMs, msString(), "ms");
            addFloat (layout, "rev_mix", "Reverb Mix", range (0.0f, 1.0f), r.mix, percentString());

            addBool (layout, "dly_on", "Delay On", false);
            addChoice (layout, "dly_mode", "Delay Mode", { "Stereo", "Ping-Pong", "Dual L/R" }, d.mode);
            addBool (layout, "dly_sync", "Delay Sync", d.sync);
            addChoice (layout, "dly_note", "Delay Note (L)", notes, d.noteL);
            addChoice (layout, "dly_note_r", "Delay Note R", notes, d.noteR);
            addFloat (layout, "dly_time", "Delay Time (L)", range (1.0f, 2000.0f, 300.0f), d.timeLMs, msString(), "ms");
            addFloat (layout, "dly_time_r", "Delay Time R", range (1.0f, 2000.0f, 300.0f), d.timeRMs, msString(), "ms");
            addFloat (layout, "dly_feedback", "Delay Feedback", range (0.0f, dsp::VocalDelay::maxFeedback), d.feedback, percentString());
            addFloat (layout, "dly_lowcut", "Delay Low Cut", logRange (20.0f, 2000.0f), d.lowCutHz, hzString(), "Hz");
            addFloat (layout, "dly_highcut", "Delay High Cut", logRange (1000.0f, 20000.0f), d.highCutHz, hzString(), "Hz");
            addFloat (layout, "dly_mod", "Delay Modulation", range (0.0f, 1.0f), d.modulation, percentString());
            addFloat (layout, "dly_drive", "Delay Saturation", range (0.0f, 1.0f), d.drive, percentString());
            addFloat (layout, "dly_duck", "Delay Ducking", range (0.0f, 24.0f), d.duckDb, duck, "dB");
            addFloat (layout, "dly_duck_release", "Delay Duck Release", range (20.0f, 2000.0f, 300.0f), d.duckReleaseMs, msString(), "ms");
            addFloat (layout, "dly_width", "Delay Width", range (0.0f, 1.5f), d.width, pct150);
            addFloat (layout, "dly_mix", "Delay Mix", range (0.0f, 1.0f), d.mix, percentString());

            addChoice (layout, "fx_routing", "FX Routing", { "Parallel", "Delay > Reverb", "Reverb > Delay" }, 0);

            // Learned vocal range (the detector's search range when on).
            const auto noteHz = [] (float hz, int) { return noteName (juce::roundToInt (69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / 440.0f))); };
            addBool (layout, "range_custom", "Learned Range On", false);
            addFloat (layout, "range_low", "Learned Range Low", logRange (dsp::liveMinHz, 1400.0f), 130.0f, noteHz, "Hz");
            addFloat (layout, "range_high", "Learned Range High", logRange (dsp::liveMinHz, 1400.0f), 530.0f, noteHz, "Hz");

            // Vibrato Respect (added last so existing sessions / automation indices are unchanged).
            addFloat (layout, "vibrato", "Vibrato Respect", range (0.0f, 1.0f), 0.0f, percentString());

            // Key Follow (appended after Vibrato Respect: existing indices unchanged, defaults = off).
            addBool (layout, "key_follow", "Key Follow", false);
            addFloat (layout, "key_sens", "Key Follow Sensitivity", range (0.0f, 1.0f), 0.5f, percentString());
            addBool (layout, "key_lock", "Key Follow Lock", false);
            addChoice (layout, "key_source", "Key Follow Source", keySourceChoices(), 0);

            // Harmony Lo-Fi (appended after Key Follow; 0 = off and bit-identical to earlier versions).
            addFloat (layout, "harm_lofi", "Harmony Lo-Fi", range (0.0f, 1.0f), 0.0f, percentString());
        }

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
        eqHpfOn = getId ("eq_hpf_on"); eqHpfFreq = getId ("eq_hpf_freq"); eqHpfSlope = getId ("eq_hpf_slope");
        compOn = getId ("comp_on"); compThreshold = getId ("comp_threshold"); compRatio = getId ("comp_ratio");
        compAttack = getId ("comp_attack"); compRelease = getId ("comp_release"); compMakeup = getId ("comp_makeup"); compKnee = getId ("comp_knee");
        limMode = getId ("lim_mode"); limCeiling = getId ("lim_ceiling"); limRelease = getId ("lim_release");
        outputGain = getId ("output_gain"); chainOrder = getId ("chain_order");
        latencyMode = getId ("latency_mode");
        harmOn = getId ("harm_on"); harmLevel = getId ("harm_level"); harmFormant = getId ("harm_formant");
        harmLofi = getId ("harm_lofi");

        revOn = getId ("rev_on"); revPre = getId ("rev_predelay"); revPreSync = getId ("rev_predelay_sync");
        revDecay = getId ("rev_decay"); revSize = getId ("rev_size"); revDamping = getId ("rev_damping");
        revLowCut = getId ("rev_lowcut"); revHighCut = getId ("rev_highcut"); revWidth = getId ("rev_width");
        revEarly = getId ("rev_early"); revDiffusion = getId ("rev_diffusion"); revMod = getId ("rev_mod");
        revDuck = getId ("rev_duck"); revDuckRelease = getId ("rev_duck_release"); revMix = getId ("rev_mix");
        dlyOn = getId ("dly_on"); dlyMode = getId ("dly_mode"); dlySync = getId ("dly_sync");
        dlyNote = getId ("dly_note"); dlyNoteR = getId ("dly_note_r"); dlyTime = getId ("dly_time"); dlyTimeR = getId ("dly_time_r");
        dlyFeedback = getId ("dly_feedback"); dlyLowCut = getId ("dly_lowcut"); dlyHighCut = getId ("dly_highcut");
        dlyMod = getId ("dly_mod"); dlyDrive = getId ("dly_drive"); dlyDuck = getId ("dly_duck");
        dlyDuckRelease = getId ("dly_duck_release"); dlyWidth = getId ("dly_width"); dlyMix = getId ("dly_mix");
        fxRouting = getId ("fx_routing");
        rangeCustom = getId ("range_custom"); rangeLow = getId ("range_low"); rangeHigh = getId ("range_high");
        vibratoRespect = getId ("vibrato");
        keyFollow = getId ("key_follow"); keySens = getId ("key_sens"); keyLock = getId ("key_lock");
        keySource = getId ("key_source");

        for (int v = 0; v < dsp::numHarmonyVoices; ++v)
        {
            auto& h = harmVoices[(size_t) v];
            h.on = getId (harmonyId (v, "on").toRawUTF8());
            h.mode = getId (harmonyId (v, "mode").toRawUTF8());
            h.degree = getId (harmonyId (v, "degree").toRawUTF8());
            h.semis = getId (harmonyId (v, "semis").toRawUTF8());
            h.level = getId (harmonyId (v, "level").toRawUTF8());
            h.pan = getId (harmonyId (v, "pan").toRawUTF8());
            h.detune = getId (harmonyId (v, "detune").toRawUTF8());
            h.delay = getId (harmonyId (v, "delay").toRawUTF8());
        }
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
        s.pitch.vibratoRespect = f (vibratoRespect);
        s.pitch.amount = f (amount);
        s.pitch.a4Hz = f (a4);
        s.pitch.formant = b (formant);
        s.pitch.input = (dsp::InputType) i (inputType);
        s.pitch.quality = (dsp::QualityMode) i (quality);
        s.gate = { b (gateOn), f (gateThreshold), f (gateAttack), f (gateHold), f (gateRelease), f (gateRange) };
        s.eq = { b (eqOn), f (eqLowFreq), f (eqLowGain), f (eqMidFreq), f (eqMidGain), f (eqMidQ), f (eqHighFreq), f (eqHighGain) };
        s.eq.hpfOn = b (eqHpfOn);
        s.eq.hpfFreq = f (eqHpfFreq);
        s.eq.hpfSlope = juce::jlimit (0, 2, i (eqHpfSlope));
        s.compressor = { b (compOn), f (compThreshold), f (compRatio), f (compAttack), f (compRelease), f (compMakeup), f (compKnee) };
        s.limiter = { i (limMode), f (limCeiling), f (limRelease), f (outputGain) };
        s.chainOrder = i (chainOrder);
        s.pitch.latency = (dsp::LatencyMode) juce::jlimit (0, 1, i (latencyMode));

        s.harmony.enabled = b (harmOn);
        s.harmony.levelDb = f (harmLevel);
        s.harmony.formant = b (harmFormant);
        s.harmony.lofi = f (harmLofi);

        for (size_t v = 0; v < harmVoices.size(); ++v)
        {
            const auto& h = harmVoices[v];
            auto& voice = s.harmony.voices[v];
            voice.enabled = b (h.on);
            voice.fixed = i (h.mode) == 1;
            voice.degrees = i (h.degree);
            voice.semitones = i (h.semis);
            voice.levelDb = f (h.level);
            voice.pan = f (h.pan);
            voice.detuneCents = f (h.detune);
            voice.delayMs = f (h.delay);
        }

        s.reverb = { b (revOn), f (revPre), i (revPreSync), f (revDecay), f (revSize), f (revDamping), f (revLowCut),
                     f (revHighCut), f (revWidth), f (revEarly), f (revDiffusion), f (revMod), f (revDuck),
                     f (revDuckRelease), f (revMix) };
        s.delay.enabled = b (dlyOn);
        s.delay.mode = juce::jlimit (0, 2, i (dlyMode));
        s.delay.sync = b (dlySync);
        s.delay.noteL = i (dlyNote);
        s.delay.noteR = i (dlyNoteR);
        s.delay.timeLMs = f (dlyTime);
        s.delay.timeRMs = f (dlyTimeR);
        s.delay.feedback = f (dlyFeedback);
        s.delay.lowCutHz = f (dlyLowCut);
        s.delay.highCutHz = f (dlyHighCut);
        s.delay.modulation = f (dlyMod);
        s.delay.drive = f (dlyDrive);
        s.delay.duckDb = f (dlyDuck);
        s.delay.duckReleaseMs = f (dlyDuckRelease);
        s.delay.width = f (dlyWidth);
        s.delay.mix = f (dlyMix);
        s.fxRouting = (dsp::FxRouting) juce::jlimit (0, 2, i (fxRouting));

        if (b (rangeCustom))
        {
            s.pitch.rangeLowHz = f (rangeLow);
            s.pitch.rangeHighHz = f (rangeHigh);
        }

        return s;
    }

    juce::StringArray keySourceChoices() { return { "Auto", "Sidechain", "Vocal" }; }

    KeyFollowSettings SettingsReader::readKeyFollow() const noexcept
    {
        auto f = [] (Value v) { return v != nullptr ? v->load() : 0.0f; };
        KeyFollowSettings k;
        k.enabled = f (keyFollow) > 0.5f;
        k.sensitivity = juce::jlimit (0.0f, 1.0f, keySens != nullptr ? keySens->load() : 0.5f);
        k.locked = f (keyLock) > 0.5f;
        k.source = juce::jlimit (0, 2, juce::roundToInt (f (keySource)));
        return k;
    }
}
