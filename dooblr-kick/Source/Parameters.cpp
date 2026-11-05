#include "Parameters.h"

#include "DSP/ProcessingChain.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace dooblrkick::params
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
        StringFn bipolarPercent(){ return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }; }

        /** Time Scale readout / Ableton text: "1.00x", "2.50x". Accepts "2x", "2.5", "200 %". */
        StringFn scaleString()
        {
            return [] (float v, int) { return juce::String (v, 2) + "x"; };
        }

        float parseScale (const juce::String& text)
        {
            auto t = text.trim().toLowerCase().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
            t = t.replace ("x", "").replace (" ", "");

            if (t.endsWithChar ('%'))
            {
                const auto pct = t.dropLastCharacters (1).getFloatValue();
                return juce::jlimit (minMasterTimeScale, maxMasterTimeScale, pct / 100.0f);
            }

            return juce::jlimit (minMasterTimeScale, maxMasterTimeScale, t.getFloatValue());
        }

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

        StringFn panString()
        {
            return [] (float v, int)
            {
                const auto amount = juce::roundToInt (std::abs (v) * 100.0f);
                return amount == 0 ? juce::String ("C") : (v < 0.0f ? "L" : "R") + juce::String (amount);
            };
        }

        StringFn semitoneString()
        {
            return [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st"; };
        }

        // Sample-layer tunes: nearest semitone plus signed cents, e.g. "-12 st -25 ct".
        StringFn tuneString()
        {
            return [] (float v, int) { return formatTune (v); };
        }

        StringFn curveString()
        {
            return [] (float v, int) { return juce::String (v, 2); };
        }

        void addFloat (Layout& layout, const juce::String& id, const juce::String& name,
                       juce::NormalisableRange<float> r, float defaultValue, StringFn toString,
                       const juce::String& label = {}, std::function<float (const juce::String&)> fromString = {})
        {
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction (std::move (toString))
                                  .withLabel (label);

            if (fromString)
                attributes = attributes.withValueFromStringFunction (std::move (fromString));

            layout.add (std::make_unique<Float> (juce::ParameterID { id, parameterVersion }, name, r, defaultValue, attributes));
        }

        void addTune (Layout& layout, const juce::String& id, const juce::String& name)
        {
            addFloat (layout, id, name, range (dsp::minSampleTuneSt, dsp::maxSampleTuneSt), 0.0f, tuneString(), "st",
                      [] (const juce::String& text) { return parseTune (text); });
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

        const char* envelopePrefix (Envelope e)
        {
            constexpr std::array<const char*, numEnvelopes> prefixes { "penv", "aenv", "menv", "cpenv", "caenv", "spenv",
                                                                       "saenv", "tpenv", "taenv", "npenv", "naenv" };
            return prefixes[(size_t) e];
        }

        struct DefaultNode { float timeMs, value, curve; };

        constexpr std::array<DefaultNode, maxNodes> defaultPitchNodes { {
            { 0.0f, 1100.0f, 0.7f }, { 18.0f, 140.0f, 0.5f }, { 90.0f, 52.0f, 0.3f }, { 400.0f, 44.0f, 0.0f },
            { 600.0f, 44.0f, 0.0f }, { 800.0f, 44.0f, 0.0f }, { 1000.0f, 44.0f, 0.0f }, { 1200.0f, 44.0f, 0.0f } } };

        constexpr std::array<DefaultNode, maxNodes> defaultAmpNodes { {
            { 0.0f, 1.0f, 0.0f }, { 60.0f, 0.9f, 0.2f }, { 250.0f, 0.55f, 0.5f }, { 480.0f, 0.0f, 0.0f },
            { 600.0f, 0.0f, 0.0f }, { 800.0f, 0.0f, 0.0f }, { 1000.0f, 0.0f, 0.0f }, { 1200.0f, 0.0f, 0.0f } } };

        // Master envelope: flat at 100 % (transparent) on every node, so the default changes nothing.
        constexpr std::array<DefaultNode, maxNodes> defaultMasterNodes { {
            { 0.0f, 1.0f, 0.0f }, { 1000.0f, 1.0f, 0.0f }, { 1200.0f, 1.0f, 0.0f }, { 1400.0f, 1.0f, 0.0f },
            { 1600.0f, 1.0f, 0.0f }, { 1800.0f, 1.0f, 0.0f }, { 1900.0f, 1.0f, 0.0f }, { 2000.0f, 1.0f, 0.0f } } };

        /** v0.3.6 layer envelope defaults (dsp::defaultLayerEnvelope), padded to 8 nodes: an unused
            node sits after the last one at the last value, so adding a point keeps the shape. */
        std::array<DefaultNode, maxNodes> layerDefaultNodes (int layer, bool amp, int& count)
        {
            const auto e = dsp::defaultLayerEnvelope (layer, amp);
            count = e.numNodes;
            std::array<DefaultNode, maxNodes> nodes {};
            const auto step = std::max (10.0f, e.getEndTimeMs() * 0.25f);

            for (int i = 0; i < maxNodes; ++i)
            {
                if (i < e.numNodes)
                    nodes[(size_t) i] = { e.nodes[(size_t) i].timeMs, e.nodes[(size_t) i].value, e.nodes[(size_t) i].curve };
                else
                    nodes[(size_t) i] = { std::min (e.getEndTimeMs() + step * (float) (i - e.numNodes + 1), maxNodeTimeFor (layerEnvelope (layer, amp))),
                                          e.getEndValue(), 0.0f };
            }

            return nodes;
        }

        /** "<Layer> Pitch Env 3 Time", "Click Amp Env 2 Level", "Master Env 1 Curve" ... */
        juce::String envelopeName (Envelope e)
        {
            const auto layer = envelopeLayer (e);

            if (layer < 0)
                return "Master Env";

            return juce::String (layerNames[(size_t) layer]) + (isAmpEnvelope (e) ? " Amp Env" : " Pitch Env");
        }

        void addEnvelope (Layout& layout, Envelope e, const std::array<DefaultNode, maxNodes>& defaults, int defaultCount)
        {
            const auto name = envelopeName (e);
            const auto maxTime = maxNodeTimeFor (e);
            const auto timeCentre = envelopeLayer (e) == dsp::clickLayer ? 40.0f : 200.0f;

            layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { envelopeCountId (e), parameterVersion },
                                                                   name + " Points", 2, maxNodes, defaultCount));

            for (int node = 0; node < maxNodes; ++node)
            {
                const auto& d = defaults[(size_t) node];
                const auto n = " " + juce::String (node + 1);

                if (node > 0)
                    addFloat (layout, nodeTimeId (e, node), name + n + " Time", range (0.0f, maxTime, timeCentre), d.timeMs, msString(), "ms");

                if (e == Envelope::pitch)
                    addFloat (layout, nodeValueId (e, node), name + n + " Freq", logRange (minPitchHz, maxPitchHz), d.value, hzString(), "Hz");
                else if (isSemitoneEnvelope (e))
                    addFloat (layout, nodeValueId (e, node), name + n + " St", range (minEnvSemitones, maxEnvSemitones), d.value,
                              [] (float v, int) { return formatSemitones (v); }, "st");
                else
                    addFloat (layout, nodeValueId (e, node), name + n + " Level", range (0.0f, 1.0f), d.value, percentString());

                if (node < maxNodes - 1)
                    addFloat (layout, nodeCurveId (e, node), name + n + " Curve", range (-1.0f, 1.0f), d.curve, curveString());
            }
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

    juce::StringArray sampleChoices()
    {
        juce::StringArray names;

        for (const auto* name : factorySampleNames)
            names.add (name);

        names.add ("User file");
        return names;
    }

    juce::String layerId (int layer, const char* suffix)
    {
        return juce::String (layerPrefixes[(size_t) layer]) + "_" + suffix;
    }

    juce::String envelopeCountId (Envelope e) { return juce::String (envelopePrefix (e)) + "_count"; }
    juce::String nodeTimeId (Envelope e, int node) { return juce::String (envelopePrefix (e)) + juce::String (node) + "_time"; }
    juce::String nodeCurveId (Envelope e, int node) { return juce::String (envelopePrefix (e)) + juce::String (node) + "_curve"; }

    juce::String nodeValueId (Envelope e, int node)
    {
        return juce::String (envelopePrefix (e)) + juce::String (node)
             + (e == Envelope::pitch ? "_freq" : isSemitoneEnvelope (e) ? "_st" : "_level");
    }

    juce::String noteName (int midiNote)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
    }

    namespace
    {
        constexpr std::array<std::pair<float, float>, 5> levelTaper { { { 0.0f, minLevelDb }, { 0.10f, -48.0f }, { 0.25f, -24.0f },
                                                                        { 0.78f, 0.0f }, { 1.0f, maxLevelDb } } };
    }

    float levelDbToTravel (float db) noexcept
    {
        if (! (db > minLevelDb)) return 0.0f;
        if (db >= maxLevelDb) return 1.0f;

        for (size_t i = 1; i < levelTaper.size(); ++i)
            if (db <= levelTaper[i].second)
                return juce::jmap (db, levelTaper[i - 1].second, levelTaper[i].second, levelTaper[i - 1].first, levelTaper[i].first);

        return 1.0f;
    }

    float travelToLevelDb (float travel) noexcept
    {
        if (! (travel > 0.0f)) return minLevelDb;
        if (travel >= 1.0f) return maxLevelDb;

        for (size_t i = 1; i < levelTaper.size(); ++i)
            if (travel <= levelTaper[i].first)
                return juce::jmap (travel, levelTaper[i - 1].first, levelTaper[i].first, levelTaper[i - 1].second, levelTaper[i].second);

        return maxLevelDb;
    }

    juce::NormalisableRange<float> levelRange()
    {
        return { minLevelDb, maxLevelDb,
                 [] (float, float, float travel) { return travelToLevelDb (travel); },
                 [] (float, float, float db) { return levelDbToTravel (db); },
                 [] (float lo, float hi, float db) { return juce::jlimit (lo, hi, db); } };
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";

        return juce::String (hz, hz >= 100.0f ? 0 : 1) + " Hz";
    }

    juce::String formatSemitones (float st)
    {
        return (st > 0.0f ? "+" : "") + juce::String (st, 1) + " st";
    }

    juce::String formatTune (float semitones)
    {
        const auto totalCents = juce::roundToInt (semitones * 100.0f);
        const auto st = (int) std::lround ((double) totalCents / 100.0);
        const auto ct = totalCents - st * 100;
        const auto signedInt = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v); };

        auto text = signedInt (st) + " st";

        if (ct != 0)
            text << " " << signedInt (ct) << " ct";

        return text;
    }

    float parseTune (const juce::String& text)
    {
        // Accepts "-12 st -25 ct", "-12st", "-12.25", "+7", "-30 ct" and the like.
        auto rest = text.trim().toLowerCase().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
        double semitones = 0.0;

        while (rest.isNotEmpty())
        {
            const auto numberEnd = rest.indexOfAnyOf ("abcdefghijklmnopqrstuvwxyz", 0);
            const auto number = (numberEnd < 0 ? rest : rest.substring (0, numberEnd)).trim();
            rest = numberEnd < 0 ? juce::String() : rest.substring (numberEnd);

            const auto unitEnd = rest.indexOfAnyOf ("+-0123456789. ", 0);
            const auto unit = (unitEnd < 0 ? rest : rest.substring (0, unitEnd)).trim();
            rest = unitEnd < 0 ? juce::String() : rest.substring (unitEnd).trim();

            if (number.isNotEmpty())
                semitones += number.removeCharacters ("+ ").getDoubleValue() / (unit.startsWith ("c") ? 100.0 : 1.0);
        }

        return (float) std::clamp (semitones, (double) dsp::minSampleTuneSt, (double) dsp::maxSampleTuneSt);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        Layout layout;

        //== Layers (common controls) ==================================================
        constexpr std::array<bool, numLayers> defaultOn { true, true, true, false, false };
        constexpr std::array<float, numLayers> defaultLevel { -8.0f, 0.0f, -6.0f, -12.0f, -18.0f };

        for (int layer = 0; layer < numLayers; ++layer)
        {
            const juce::String name (layerNames[(size_t) layer]);
            addBool (layout, layerId (layer, "on"), name + " On", defaultOn[(size_t) layer]);
            addBool (layout, layerId (layer, "solo"), name + " Solo", false);
            addFloat (layout, layerId (layer, "level"), name + " Level", levelRange(), defaultLevel[(size_t) layer], dbString(), "dB");
            addFloat (layout, layerId (layer, "pan"), name + " Pan", range (-1.0f, 1.0f), 0.0f, panString());
            addFloat (layout, layerId (layer, "start"), name + " Start", range (0.0f, 100.0f, 10.0f), 0.0f, msString(), "ms");
            addFloat (layout, layerId (layer, "send"), name + " Drive Send", range (0.0f, 1.0f), 1.0f, percentString());
            addChoice (layout, layerId (layer, "ftype"), name + " Filter", { "Off", "Low-pass", "High-pass", "Band-pass" }, 0);
            addFloat (layout, layerId (layer, "cutoff"), name + " Cutoff", logRange (20.0f, 20000.0f), 2000.0f, hzString(), "Hz");
            addFloat (layout, layerId (layer, "reso"), name + " Resonance", range (0.0f, 1.0f), 0.1f, percentString());

            if (layerHasSample (layer))
            {
                // Defaults: Click Slapstick, Body Kit Kick Close, Tail Concert BD Boom, Noise Shaker Hit.
                constexpr std::array<int, numLayers> defaultSample { 7, 0, 0, 4, 10 };
                addChoice (layout, layerId (layer, "source"), name + " Source", { "Synth", "Sample" }, 0);
                addChoice (layout, layerId (layer, "sample"), name + " Sample", sampleChoices(), defaultSample[(size_t) layer]);
            }
        }

        //== Click ===========================================================================
        addChoice (layout, "click_type", "Click Type", { "Chirp", "Noise burst", "Snap", "Tick", "Knock" }, 0);
        addTune (layout, "click_pitch", "Click Pitch");
        addFloat (layout, "click_tone", "Click Tone", range (-1.0f, 1.0f), 0.0f, bipolarPercent());
        addBool (layout, "click_keytrack", "Click Key Track", false);

        //== Body ============================================================================
        addChoice (layout, "body_wave", "Body Wave", { "Sine", "Triangle", "Saturated", "Square" }, 0);
        addFloat (layout, "body_harm", "Body Harmonics", range (0.0f, 1.0f), 0.0f, percentString());
        addTune (layout, "body_tune", "Body Tune");
        addFloat (layout, "body_phase", "Body Start Phase", range (0.0f, 360.0f), 0.0f, suffixed (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0));
        addFloat (layout, "body_drive", "Body Drive", range (0.0f, 1.0f), 0.0f, percentString());
        addBool (layout, "body_keytrack", "Body Key Track", false);
        addFloat (layout, "body_smp_penv", "Body Sample Pitch Env", range (0.0f, 1.0f), 0.0f, percentString());

        //== Envelopes =======================================================================
        addEnvelope (layout, Envelope::pitch, defaultPitchNodes, 4);
        addEnvelope (layout, Envelope::amp, defaultAmpNodes, 4);

        //== Sub =============================================================================
        addFloat (layout, "sub_tune", "Sub Tune", range (-24.0f, 24.0f), 0.0f, semitoneString(), "st");
        addFloat (layout, "sub_fine", "Sub Fine", range (-100.0f, 100.0f), 0.0f, suffixed (" ct", 0), "ct");
        addFloat (layout, "sub_phase", "Sub Start Phase", range (0.0f, 360.0f), 0.0f, suffixed (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0));
        addFloat (layout, "sub_envamt", "Sub Follow Body Pitch", range (0.0f, 1.0f), 0.0f, percentString());

        //== Tail ============================================================================
        addChoice (layout, "tail_wave", "Tail Wave", { "Sine", "Triangle", "Saturated" }, 0);
        addTune (layout, "tail_tune", "Tail Tune");
        addFloat (layout, "tail_spread", "Tail Spread", range (0.0f, 50.0f), 8.0f, suffixed (" ct", 1), "ct");
        addFloat (layout, "tail_phase", "Tail Start Phase", range (0.0f, 360.0f), 0.0f, suffixed (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0));
        addFloat (layout, "tail_drive", "Tail Drive", range (0.0f, 1.0f), 0.3f, percentString());
        addBool (layout, "tail_keytrack", "Tail Key Track", false);

        //== Noise ===========================================================================
        addChoice (layout, "noise_color", "Noise Colour", { "White", "Pink", "Brown" }, 0);
        addFloat (layout, "noise_width", "Noise Width", range (0.0f, 1.0f), 0.5f, percentString());
        addTune (layout, "noise_tune", "Noise Sample Tune");

        //== Play ============================================================================
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "root_note", parameterVersion }, "Key", 12, 72, 29,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                .withValueFromStringFunction ([] (const juce::String& text)
                                              {
                                                  for (int n = 12; n <= 72; ++n)
                                                      if (noteName (n).equalsIgnoreCase (text.trim()))
                                                          return n;
                                                  return text.getIntValue();
                                              })));
        addFloat (layout, "vel_sens", "Velocity Sensitivity", range (0.0f, 1.0f), 0.5f, percentString());
        addBool (layout, "gate", "Gate", false);
        addFloat (layout, "release", "Gate Release", range (1.0f, 2000.0f, 100.0f), 50.0f, msString(), "ms");

        //== Drive ===========================================================================
        addBool (layout, "dist_on", "Drive On", false);
        addChoice (layout, "dist_type", "Drive Type", { "Soft clip", "Hard clip", "Tube", "Fold", "Bitcrush" }, 0);
        addFloat (layout, "dist_drive", "Drive", range (0.0f, 48.0f), 12.0f, dbString(), "dB");
        addFloat (layout, "dist_tone", "Drive Tone", range (0.0f, 1.0f), 1.0f, percentString());
        addFloat (layout, "dist_bits", "Crush Bits", range (1.0f, 16.0f), 8.0f, suffixed (" bit", 1));
        addFloat (layout, "dist_rate", "Crush Rate", logRange (500.0f, 48000.0f), 12000.0f, hzString(), "Hz");
        addFloat (layout, "dist_out", "Drive Output", range (-24.0f, 12.0f), -3.0f, dbString(), "dB");
        addFloat (layout, "dist_mix", "Drive Mix", range (0.0f, 1.0f), 1.0f, percentString());

        //== EQ ==============================================================================
        addBool (layout, "eq_on", "EQ On", false);
        addFloat (layout, "eq_low_freq", "EQ Low Freq", logRange (20.0f, 500.0f), 60.0f, hzString(), "Hz");
        addFloat (layout, "eq_low_gain", "EQ Low Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");
        addFloat (layout, "eq_mid_freq", "EQ Mid Freq", logRange (100.0f, 10000.0f), 1000.0f, hzString(), "Hz");
        addFloat (layout, "eq_mid_gain", "EQ Mid Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");
        addFloat (layout, "eq_mid_q", "EQ Mid Q", logRange (0.2f, 10.0f), 1.0f, suffixed ({}, 2));
        addFloat (layout, "eq_high_freq", "EQ High Freq", logRange (2000.0f, 20000.0f), 8000.0f, hzString(), "Hz");
        addFloat (layout, "eq_high_gain", "EQ High Gain", range (-24.0f, 24.0f), 0.0f, dbString(), "dB");

        //== Dynamics ========================================================================
        addBool (layout, "shape_on", "Transient On", false);
        addFloat (layout, "shape_attack", "Transient Attack", range (-1.0f, 1.0f), 0.0f, bipolarPercent());
        addFloat (layout, "shape_sustain", "Transient Sustain", range (-1.0f, 1.0f), 0.0f, bipolarPercent());

        addBool (layout, "comp_on", "Comp On", false);
        addFloat (layout, "comp_threshold", "Comp Threshold", range (-48.0f, 0.0f), -12.0f, dbString(), "dB");
        addFloat (layout, "comp_ratio", "Comp Ratio", range (1.0f, 20.0f, 4.0f), 4.0f, suffixed (":1", 1));
        addFloat (layout, "comp_attack", "Comp Attack", range (0.1f, 100.0f, 10.0f), 5.0f, msString(), "ms");
        addFloat (layout, "comp_release", "Comp Release", range (10.0f, 1000.0f, 100.0f), 80.0f, msString(), "ms");
        addFloat (layout, "comp_makeup", "Comp Makeup", range (0.0f, 24.0f), 0.0f, dbString(), "dB");

        //== Master ==========================================================================
        addChoice (layout, "lim_mode", "Master Mode", { "Off", "Clipper", "Limiter" }, 2);
        addFloat (layout, "lim_ceiling", "Ceiling", range (-24.0f, 0.0f), -0.3f, dbString(), "dB");
        addFloat (layout, "lim_release", "Limiter Release", range (1.0f, 500.0f, 60.0f), 60.0f, msString(), "ms");
        addFloat (layout, "output_gain", "Output", range (-24.0f, 6.0f), 0.0f, dbString(), "dB");

        //== Envelope page (0.3.0): master envelope, dry / wet, chain order ===================
        addBool (layout, "menv_on", "Master Env On", true);
        addEnvelope (layout, Envelope::master, defaultMasterNodes, 2);

        addFloat (layout, "mix_drywet", "Dry/Wet", range (0.0f, 1.0f), 1.0f, percentString());
        addChoice (layout, "chain_order", "Chain Order", chainOrderChoices(), 0);

        //== 0.3.3 / 0.3.8 (appended): master envelope macros for the Envelope page knobs ======
        // Defaults 1.00x / 100 % leave the point times and levels unchanged.
        // Time Scale (Ableton / host name "Time Scale"): multiplies every Master Env N Time in DSP.
        // Stored point times stay editable; effectiveTimeMs = min (stored * scale, 6000). Readout "1.00x".
        addFloat (layout, "menv_tscale", "Time Scale", logRange (minMasterTimeScale, maxMasterTimeScale), 1.0f, scaleString(),
                  {}, [] (const juce::String& text) { return parseScale (text); });
        addFloat (layout, "menv_depth", "Master Env Depth", range (0.0f, 1.0f), 1.0f, percentString());

        // 0.3.5: key tracking on every layer (appended; Click / Body / Tail reuse their old
        // sample key-track switches, which now also apply in Synth mode).
        addBool (layout, "sub_keytrack", "Sub Key Track", false);
        addBool (layout, "noise_keytrack", "Noise Key Track", false);

        // 0.3.6: a pitch and an amp envelope on every layer (the Body's are above), and on every
        // sample layer an amount for how far the pitch envelope bends the sample (as the Body's).
        for (const auto layer : { dsp::clickLayer, dsp::subLayer, dsp::tailLayer, dsp::noiseLayer })
            for (const auto amp : { false, true })
            {
                int count = 2;
                const auto defaults = layerDefaultNodes (layer, amp, count);
                addEnvelope (layout, layerEnvelope (layer, amp), defaults, count);
            }

        addFloat (layout, "click_smp_penv", "Click Sample Pitch Env", range (0.0f, 1.0f), 1.0f, percentString());
        addFloat (layout, "tail_smp_penv", "Tail Sample Pitch Env", range (0.0f, 1.0f), 1.0f, percentString());
        addFloat (layout, "noise_smp_penv", "Noise Sample Pitch Env", range (0.0f, 1.0f), 1.0f, percentString());

        return layout;
    }

    //==============================================================================
    SettingsReader::SettingsReader (juce::AudioProcessorValueTreeState& stateToRead)
        : state (stateToRead)
    {
        for (int layer = 0; layer < numLayers; ++layer)
            common[(size_t) layer] = getCommon (layer);

        clickType = get ("click_type"); clickPitch = get ("click_pitch");
        clickTone = get ("click_tone"); clickKeyTrack = get ("click_keytrack"); clickSampleEnv = get ("click_smp_penv");

        bodyWave = get ("body_wave"); bodyHarmonics = get ("body_harm"); bodyTune = get ("body_tune");
        bodyPhase = get ("body_phase"); bodyDrive = get ("body_drive"); bodyKeyTrack = get ("body_keytrack");
        bodySampleEnv = get ("body_smp_penv");

        for (int e = 0; e < numEnvelopes; ++e)
            envelopes[(size_t) e] = getEnvelope ((Envelope) e);

        subTune = get ("sub_tune"); subFine = get ("sub_fine"); subPhase = get ("sub_phase");
        subEnvAmount = get ("sub_envamt");

        tailWave = get ("tail_wave"); tailTune = get ("tail_tune"); tailSpread = get ("tail_spread");
        tailPhase = get ("tail_phase"); tailDrive = get ("tail_drive");
        tailKeyTrack = get ("tail_keytrack"); tailSampleEnv = get ("tail_smp_penv");

        noiseColour = get ("noise_color"); noiseWidth = get ("noise_width");
        noiseTune = get ("noise_tune"); noiseKeyTrack = get ("noise_keytrack"); noiseSampleEnv = get ("noise_smp_penv");
        subKeyTrack = get ("sub_keytrack");

        keyNote = get ("root_note");
        velocitySensitivity = get ("vel_sens"); gate = get ("gate"); release = get ("release");

        distOn = get ("dist_on"); distType = get ("dist_type"); distDrive = get ("dist_drive"); distTone = get ("dist_tone");
        distBits = get ("dist_bits"); distRate = get ("dist_rate"); distOut = get ("dist_out"); distMix = get ("dist_mix");

        eqOn = get ("eq_on"); eqLowFreq = get ("eq_low_freq"); eqLowGain = get ("eq_low_gain");
        eqMidFreq = get ("eq_mid_freq"); eqMidGain = get ("eq_mid_gain"); eqMidQ = get ("eq_mid_q");
        eqHighFreq = get ("eq_high_freq"); eqHighGain = get ("eq_high_gain");

        shapeOn = get ("shape_on"); shapeAttack = get ("shape_attack"); shapeSustain = get ("shape_sustain");

        compOn = get ("comp_on"); compThreshold = get ("comp_threshold"); compRatio = get ("comp_ratio");
        compAttack = get ("comp_attack"); compRelease = get ("comp_release"); compMakeup = get ("comp_makeup");

        limMode = get ("lim_mode"); limCeiling = get ("lim_ceiling"); limRelease = get ("lim_release");
        outputGain = get ("output_gain");

        masterOn = get ("menv_on");
        dryWet = get ("mix_drywet"); chainOrder = get ("chain_order");
        masterTimeScale = get ("menv_tscale"); masterDepth = get ("menv_depth");
    }

    SettingsReader::EnvelopeValues SettingsReader::getEnvelope (Envelope e) const
    {
        EnvelopeValues v;
        v.count = get (envelopeCountId (e));

        for (int node = 0; node < maxNodes; ++node)
        {
            v.time[(size_t) node] = node > 0 ? get (nodeTimeId (e, node)) : nullptr;
            v.value[(size_t) node] = get (nodeValueId (e, node));
            v.curve[(size_t) node] = node < maxNodes - 1 ? get (nodeCurveId (e, node)) : nullptr;
        }

        return v;
    }

    SettingsReader::Value SettingsReader::get (const juce::String& id) const
    {
        auto* value = state.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    }

    SettingsReader::Common SettingsReader::getCommon (int layer) const
    {
        return { get (layerId (layer, "on")), get (layerId (layer, "solo")), get (layerId (layer, "level")),
                 get (layerId (layer, "pan")), get (layerId (layer, "start")), get (layerId (layer, "send")),
                 get (layerId (layer, "ftype")), get (layerId (layer, "cutoff")), get (layerId (layer, "reso")),
                 layerHasSample (layer) ? get (layerId (layer, "source")) : nullptr,
                 layerHasSample (layer) ? get (layerId (layer, "sample")) : nullptr };
    }

    dsp::KickSettings SettingsReader::read() const noexcept
    {
        dsp::KickSettings s;

        const auto f = [] (Value v) { return v->load (std::memory_order_relaxed); };
        const auto b = [&f] (Value v) { return f (v) >= 0.5f; };
        const auto i = [&f] (Value v) { return juce::roundToInt (f (v)); };

        // valueMap: Body pitch Hz -> log2 Hz; every other envelope stores its value as is.
        const auto readEnvelope = [&] (Envelope e, dsp::MultiPointEnvelope& out)
        {
            const auto& v = envelopes[(size_t) e];
            out.numNodes = i (v.count);

            for (int node = 0; node < maxNodes; ++node)
            {
                auto& n = out.nodes[(size_t) node];
                n.timeMs = node > 0 ? f (v.time[(size_t) node]) : 0.0f;
                n.value = f (v.value[(size_t) node]);
                n.curve = node < maxNodes - 1 ? f (v.curve[(size_t) node]) : 0.0f;

                if (e == Envelope::pitch)
                    n.value = std::log2 (juce::jmax (1.0f, n.value));
            }

            out.sanitise();
        };

        const auto readCommon = [&] (int layer, dsp::LayerCommon& c)
        {
            const auto& v = common[(size_t) layer];
            c.enabled = b (v.enabled);
            c.solo = b (v.solo);
            c.levelDb = f (v.level);
            c.pan = f (v.pan);
            c.startMs = f (v.start);
            c.send = f (v.send);
            c.filterType = i (v.filterType);
            c.cutoffHz = f (v.cutoff);
            c.resonance = f (v.resonance);
        };

        readCommon (dsp::clickLayer, s.click.common);
        readCommon (dsp::bodyLayer, s.body.common);
        readCommon (dsp::subLayer, s.sub.common);
        readCommon (dsp::tailLayer, s.tail.common);
        readCommon (dsp::noiseLayer, s.noise.common);

        const auto readSource = [&] (int layer, dsp::SampleSource& source)
        {
            const auto& v = common[(size_t) layer];
            source.enabled = v.source != nullptr && i (v.source) == 1;
            source.index = v.sample != nullptr ? i (v.sample) : 0;
            source.data = nullptr; // resolved by the processor
        };

        readSource (dsp::clickLayer, s.click.sample);
        readSource (dsp::bodyLayer, s.body.sample);
        readSource (dsp::tailLayer, s.tail.sample);
        readSource (dsp::noiseLayer, s.noise.sample);

        s.click.type = i (clickType);
        s.click.pitchSt = f (clickPitch);
        s.click.sampleEnvAmount = f (clickSampleEnv);
        readEnvelope (Envelope::clickPitch, s.click.pitch);
        readEnvelope (Envelope::clickAmp, s.click.amp);
        s.click.tone = f (clickTone);
        s.click.keyTrack = b (clickKeyTrack);

        s.body.wave = i (bodyWave);
        s.body.harmonics = f (bodyHarmonics);
        s.body.tuneSt = f (bodyTune);
        s.body.phaseDeg = f (bodyPhase);
        s.body.drive = f (bodyDrive);
        s.body.keyTrack = b (bodyKeyTrack);
        s.body.sampleEnvAmount = f (bodySampleEnv);

        readEnvelope (Envelope::pitch, s.body.pitch);
        readEnvelope (Envelope::amp, s.body.amp);

        s.sub.tuneSt = f (subTune);
        s.sub.fineCents = f (subFine);
        s.sub.phaseDeg = f (subPhase);
        s.sub.envAmount = f (subEnvAmount);
        s.sub.keyTrack = b (subKeyTrack);
        readEnvelope (Envelope::subPitch, s.sub.pitch);
        readEnvelope (Envelope::subAmp, s.sub.amp);

        s.tail.wave = i (tailWave);
        s.tail.tuneSt = f (tailTune);
        s.tail.spreadCents = f (tailSpread);
        s.tail.phaseDeg = f (tailPhase);
        readEnvelope (Envelope::tailPitch, s.tail.pitch);
        readEnvelope (Envelope::tailAmp, s.tail.amp);
        s.tail.sampleEnvAmount = f (tailSampleEnv);
        s.tail.drive = f (tailDrive);
        s.tail.keyTrack = b (tailKeyTrack);

        s.noise.colour = i (noiseColour);
        s.noise.width = f (noiseWidth);
        s.noise.tuneSt = f (noiseTune);
        s.noise.keyTrack = b (noiseKeyTrack);
        readEnvelope (Envelope::noisePitch, s.noise.pitch);
        readEnvelope (Envelope::noiseAmp, s.noise.amp);
        s.noise.sampleEnvAmount = f (noiseSampleEnv);

        s.play.keyNote = i (keyNote);
        s.play.velocitySensitivity = f (velocitySensitivity);
        s.play.gate = b (gate);
        s.play.releaseMs = f (release);

        s.drive = { b (distOn), i (distType), f (distDrive), f (distTone), f (distBits), f (distRate), f (distOut), f (distMix) };
        s.eq = { b (eqOn), f (eqLowFreq), f (eqLowGain), f (eqMidFreq), f (eqMidGain), f (eqMidQ), f (eqHighFreq), f (eqHighGain) };
        s.shaper = { b (shapeOn), f (shapeAttack), f (shapeSustain) };
        s.compressor = { b (compOn), f (compThreshold), f (compRatio), f (compAttack), f (compRelease), f (compMakeup) };
        s.output = { i (limMode), f (limCeiling), f (limRelease), f (outputGain) };

        s.master.envelopeOn = b (masterOn);
        readEnvelope (Envelope::master, s.master.envelope);

        for (int node = 0; node < maxNodes; ++node)
        {
            auto& m = s.master.envelope.nodes[(size_t) node];
            const auto curve = m.curve;
            m = applyMasterMacros (m, f (masterTimeScale), f (masterDepth));
            m.curve = curve;
        }

        s.master.envelope.sanitise();
        s.master.dryWet = f (dryWet);
        s.master.chainOrder = i (chainOrder);
        return s;
    }
}
