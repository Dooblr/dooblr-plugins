#include "Presets.h"

#include "Parameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace dooblrkick::presets
{
    namespace
    {
        struct Node { float timeMs, value, curve; };

        void addEnvelope (ValueList& v, params::Envelope e, std::initializer_list<Node> nodes)
        {
            v.emplace_back (params::envelopeCountId (e), (float) nodes.size());
            int index = 0;

            for (const auto& n : nodes)
            {
                if (index > 0)
                    v.emplace_back (params::nodeTimeId (e, index), n.timeMs);

                v.emplace_back (params::nodeValueId (e, index), n.value);

                if (index < params::maxNodes - 1)
                    v.emplace_back (params::nodeCurveId (e, index), n.curve);

                ++index;
            }
        }

        void addLayer (ValueList& v, const char* layer, std::initializer_list<std::pair<const char*, float>> values)
        {
            for (const auto& [suffix, value] : values)
                v.emplace_back (juce::String (layer) + "_" + suffix, value);
        }

        void add (ValueList& v, std::initializer_list<std::pair<const char*, float>> values)
        {
            for (const auto& [id, value] : values)
                v.emplace_back (id, value);
        }

        // Sample choice indices (params::factorySampleNames).
        constexpr float kitClose = 0, kitMid = 1, bdMuted = 2, bdOpen = 3, bdBoom = 4, cajon = 5,
                        frameDrum = 6, slapstick = 7, woodblock = 8, clap = 9, shaker = 10;
        constexpr float synth = 0, sample = 1;

        std::vector<FactoryPreset> makePresets()
        {
            std::vector<FactoryPreset> presets;
            presets.push_back ({ "Init", {} });

            { // Hardstyle: long, saturated, pitched body through heavy tube drive, slapstick top.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 2600, 0.8f }, { 7, 260, 0.5f }, { 55, 68, 0.2f }, { 600, 57, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 140, 0.95f, 0.1f }, { 420, 0.75f, 0.4f }, { 720, 0, 0 } });
                add (v, { { "root_note", 34 }, { "body_wave", 2 }, { "body_drive", 0.45f }, { "body_level", 0 },
                          { "dist_on", 1 }, { "dist_type", 2 }, { "dist_drive", 26 }, { "dist_tone", 0.65f }, { "dist_out", -6 },
                          { "eq_on", 1 }, { "eq_low_freq", 55 }, { "eq_low_gain", 2 }, { "eq_mid_freq", 420 }, { "eq_mid_gain", -4 },
                          { "eq_mid_q", 1.2f }, { "eq_high_freq", 6000 }, { "eq_high_gain", 2 },
                          { "lim_mode", 1 }, { "lim_ceiling", -0.5f } });
                addLayer (v, "click", { { "on", 1 }, { "source", sample }, { "sample", slapstick }, { "level", -9 }, { "length", 30 },
                                        { "send", 0.2f }, { "ftype", 2 }, { "cutoff", 900 }, { "reso", 0.1f } });
                addLayer (v, "sub", { { "on", 1 }, { "level", -14 }, { "decay", 500 }, { "hold", 120 }, { "send", 0 } });
                addLayer (v, "noise", { { "on", 0 } });
                presets.push_back ({ "Hardstyle", std::move (v) });
            }

            { // Techno Rumble: tight body plus a long, low-passed, driven concert bass drum tail.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 900, 0.7f }, { 14, 130, 0.5f }, { 70, 50, 0.2f }, { 300, 46, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 50, 0.85f, 0.3f }, { 220, 0.4f, 0.5f }, { 380, 0, 0 } });
                add (v, { { "root_note", 31 }, { "body_wave", 0 }, { "body_harm", 0.1f },
                          { "dist_on", 1 }, { "dist_type", 0 }, { "dist_drive", 14 }, { "dist_tone", 0.45f }, { "dist_out", -4 },
                          { "comp_on", 1 }, { "comp_threshold", -16 }, { "comp_ratio", 3 }, { "comp_attack", 10 }, { "comp_release", 120 },
                          { "comp_makeup", 3 } });
                addLayer (v, "click", { { "on", 1 }, { "type", 3 }, { "level", -14 }, { "length", 6 } });
                addLayer (v, "sub", { { "on", 0 } });
                addLayer (v, "tail", { { "on", 1 }, { "source", sample }, { "sample", bdBoom }, { "level", -8 }, { "start", 25 },
                                       { "attack", 40 }, { "hold", 80 }, { "decay", 1100 }, { "curve", 0.3f }, { "drive", 0.5f },
                                       { "ftype", 1 }, { "cutoff", 320 }, { "reso", 0.25f }, { "send", 1 }, { "tune", -2 } });
                presets.push_back ({ "Techno Rumble", std::move (v) });
            }

            { // Dubstep Punch: real close-mic kit kick as the body, synth sub for weight, snappy click.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 600, 0.6f }, { 20, 90, 0.4f }, { 120, 45, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 90, 0.8f, 0.3f }, { 300, 0, 0 } });
                add (v, { { "root_note", 29 }, { "body_smp_penv", 0.15f }, { "body_drive", 0.25f },
                          { "shape_on", 1 }, { "shape_attack", 0.45f }, { "shape_sustain", -0.1f },
                          { "comp_on", 1 }, { "comp_threshold", -14 }, { "comp_ratio", 4 }, { "comp_attack", 3 }, { "comp_release", 90 },
                          { "comp_makeup", 2 }, { "eq_on", 1 }, { "eq_mid_freq", 300 }, { "eq_mid_gain", -3 }, { "eq_high_gain", 3 } });
                addLayer (v, "body", { { "source", sample }, { "sample", kitClose }, { "level", -2 } });
                addLayer (v, "click", { { "on", 1 }, { "type", 2 }, { "level", -10 }, { "length", 10 }, { "tone", 0.2f } });
                addLayer (v, "sub", { { "on", 1 }, { "level", -4 }, { "attack", 2 }, { "hold", 90 }, { "decay", 450 }, { "envamt", 0.2f } });
                presets.push_back ({ "Dubstep Punch", std::move (v) });
            }

            { // House Thump: round synth body, the attack of a mid-mic kit kick as the click, light shaker air.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 420, 0.6f }, { 25, 95, 0.4f }, { 110, 55, 0.1f }, { 300, 52, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 80, 0.85f, 0.2f }, { 260, 0.45f, 0.4f }, { 420, 0, 0 } });
                add (v, { { "root_note", 33 }, { "body_wave", 1 }, { "body_harm", 0.05f },
                          { "dist_on", 1 }, { "dist_type", 0 }, { "dist_drive", 6 }, { "dist_out", -2 },
                          { "eq_on", 1 }, { "eq_low_freq", 70 }, { "eq_low_gain", 2 } });
                addLayer (v, "click", { { "on", 1 }, { "source", sample }, { "sample", kitMid }, { "level", -8 }, { "length", 45 },
                                        { "ftype", 2 }, { "cutoff", 250 } });
                addLayer (v, "sub", { { "on", 0 } });
                addLayer (v, "noise", { { "on", 1 }, { "source", sample }, { "sample", shaker }, { "level", -26 }, { "decay", 120 },
                                        { "hold", 10 } });
                presets.push_back ({ "House Thump", std::move (v) });
            }

            { // Trap 808-ish: all synthesised; long sub boom that follows the body's pitch drop.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 320, 0.6f }, { 30, 70, 0.3f }, { 200, 44, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 60, 0.9f, 0.2f }, { 200, 0, 0 } });
                add (v, { { "root_note", 29 }, { "gate", 0 },
                          { "dist_on", 1 }, { "dist_type", 0 }, { "dist_drive", 9 }, { "dist_out", -3 } });
                addLayer (v, "body", { { "level", -6 } });
                addLayer (v, "click", { { "on", 1 }, { "type", 3 }, { "level", -18 }, { "length", 4 } });
                addLayer (v, "sub", { { "on", 1 }, { "level", 0 }, { "attack", 2 }, { "hold", 300 }, { "decay", 1800 },
                                      { "curve", 0.2f }, { "envamt", 0.35f } });
                presets.push_back ({ "Trap 808-ish", std::move (v) });
            }

            { // DnB: short, punchy, acoustic: cajon click, mid-mic kit body, small sub.
                ValueList v;
                addEnvelope (v, params::Envelope::pitch, { { 0, 900, 0.7f }, { 12, 150, 0.5f }, { 60, 62, 0 } });
                addEnvelope (v, params::Envelope::amp, { { 0, 1, 0 }, { 40, 0.8f, 0.4f }, { 200, 0, 0 } });
                add (v, { { "root_note", 36 }, { "body_smp_penv", 0.1f },
                          { "shape_on", 1 }, { "shape_attack", 0.6f }, { "shape_sustain", -0.3f },
                          { "comp_on", 1 }, { "comp_threshold", -12 }, { "comp_ratio", 5 }, { "comp_attack", 2 }, { "comp_release", 60 },
                          { "comp_makeup", 3 }, { "eq_on", 1 }, { "eq_high_freq", 5000 }, { "eq_high_gain", 4 } });
                addLayer (v, "body", { { "source", sample }, { "sample", kitMid }, { "level", -3 } });
                addLayer (v, "click", { { "on", 1 }, { "source", sample }, { "sample", cajon }, { "level", -10 }, { "length", 25 },
                                        { "ftype", 2 }, { "cutoff", 600 } });
                addLayer (v, "sub", { { "on", 1 }, { "level", -10 }, { "hold", 40 }, { "decay", 220 } });
                presets.push_back ({ "DnB", std::move (v) });
            }

            juce::ignoreUnused (bdMuted, bdOpen, frameDrum, woodblock, clap, synth);
            return presets;
        }

        juce::RangedAudioParameter* find (juce::AudioProcessorValueTreeState& state, const juce::String& id)
        {
            auto* parameter = state.getParameter (id);
            jassert (parameter != nullptr); // unknown id in a preset
            return parameter;
        }
    }

    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        static const auto presets = makePresets();
        return presets;
    }

    ValueList resolve (juce::AudioProcessorValueTreeState& state, const ValueList& overrides)
    {
        ValueList values;

        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                values.emplace_back (ranged->paramID, ranged->convertFrom0to1 (ranged->getDefaultValue()));

        for (const auto& [id, value] : overrides)
            for (auto& entry : values)
                if (entry.first == id)
                    entry.second = value;

        return values;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const ValueList& values, bool asGesture)
    {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;

        for (const auto& [id, value] : values)
        {
            if (auto* parameter = find (state, id))
            {
                const auto normalised = parameter->convertTo0to1 (parameter->getNormalisableRange().snapToLegalValue (value));

                if (std::abs (normalised - parameter->getValue()) > 1.0e-7f)
                    changes.emplace_back (parameter, normalised);
            }
        }

        if (asGesture)
            for (auto& [parameter, value] : changes)
                parameter->beginChangeGesture();

        for (auto& [parameter, value] : changes)
            parameter->setValueNotifyingHost (value);

        if (asGesture)
            for (auto& [parameter, value] : changes)
                parameter->endChangeGesture();
    }

    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& r)
    {
        const auto uni = [&r] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
        const auto logUni = [&r] (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); };
        const auto chance = [&r] (float p) { return r.nextFloat() < p ? 1.0f : 0.0f; };

        ValueList v;

        // Pitch: a fast sweep from a high start down to a low fundamental, 3-4 nodes.
        const auto endHz = logUni (38.0f, 70.0f);
        const auto midHz = endHz * uni (1.6f, 3.5f);
        const auto startHz = logUni (300.0f, 3000.0f);
        const auto t1 = uni (5.0f, 30.0f), t2 = t1 + uni (30.0f, 120.0f), t3 = t2 + uni (100.0f, 500.0f);
        addEnvelope (v, params::Envelope::pitch, { { 0, startHz, uni (0.4f, 0.9f) }, { t1, midHz, uni (0.2f, 0.6f) },
                                                   { t2, endHz * uni (1.0f, 1.15f), uni (0.0f, 0.3f) }, { t3, endHz, 0 } });

        const auto a1 = uni (20.0f, 150.0f), a2 = a1 + uni (80.0f, 350.0f), a3 = a2 + uni (80.0f, 500.0f);
        addEnvelope (v, params::Envelope::amp, { { 0, 1, uni (-0.2f, 0.3f) }, { a1, uni (0.75f, 1.0f), uni (0.0f, 0.4f) },
                                                 { a2, uni (0.3f, 0.75f), uni (0.2f, 0.6f) }, { a3, 0, 0 } });

        // On / off (v0.3.3): every layer is switched by its own draw - Click and Body 80 %,
        // Sub 60 %, Tail and Noise 30 % - so any of them, Click and Body included, can be off.
        // A layer the user has soloed stays on. If all five come up off, one of the five (each
        // equally likely) is switched back on, so a result is never silent. Every layer's sound
        // parameters are set whether it is on or not, so switching one back on sounds coherent.
        // (Its level is floored at the end if it is the only layer heard.)
        std::array<float, params::numLayers> on {};
        {
            constexpr std::array<float, params::numLayers> onChance { 0.8f, 0.8f, 0.6f, 0.3f, 0.3f };
            for (int layer = 0; layer < params::numLayers; ++layer)
            {
                on[(size_t) layer] = chance (onChance[(size_t) layer]);
                if (const auto* solo = state.getRawParameterValue (params::layerId (layer, "solo")); solo != nullptr && solo->load() > 0.5f)
                    on[(size_t) layer] = 1.0f;
            }
            if (std::all_of (on.begin(), on.end(), [] (float o) { return o < 0.5f; }))
                on[(size_t) r.nextInt (params::numLayers)] = 1.0f;
        }
        const auto clickOn = on[0], bodyOn = on[1];

        // Body: centred; mostly the synth (whose pitch is the envelope above and
        // never follows the MIDI note), sometimes a kick sample bent by that envelope. Every Body
        // sound parameter is set, so the result never depends on what was there before.
        // Key tracking (body_keytrack) is left as the user set it.
        {
            const auto bodySample = chance (0.25f);
            static constexpr float bodySamples[] { 0, 1, 2, 11, 12 }; // kit kicks, muted BD, synth kicks
            add (v, { { "body_on", bodyOn }, { "body_level", uni (-3.0f, 0.0f) }, { "body_pan", 0 }, { "body_start", 0 },
                      { "body_send", uni (0.7f, 1.0f) }, { "body_source", bodySample },
                      { "body_sample", bodySamples[r.nextInt ((int) std::size (bodySamples))] },
                      { "body_wave", (float) r.nextInt (4) }, { "body_harm", uni (0.0f, 0.3f) }, { "body_drive", uni (0.0f, 0.5f) },
                      { "body_phase", r.nextBool() ? 0.0f : 90.0f },
                      { "body_tune", bodySample > 0.5f ? uni (-7.0f, 3.0f) : uni (-2.0f, 2.0f) },
                      { "body_smp_penv", bodySample > 0.5f ? uni (0.0f, 0.5f) : 0.0f } });

            const auto lowPass = chance (0.25f);
            add (v, { { "body_ftype", lowPass > 0.5f ? 1.0f : 0.0f }, { "body_cutoff", logUni (1500.0f, 12000.0f) },
                      { "body_reso", uni (0.0f, 0.3f) } });
        }

        // Click: centred; a synth click (type, pitch, length, tone) or a short
        // percussive sample. Its key tracking (click_keytrack) is left as the user set it.
        {
            const auto clickSample = chance (0.4f);
            static constexpr float clickSamples[] { 5, 6, 7, 8, 14 }; // cajon, frame drum, slapstick, woodblock, synth click
            add (v, { { "click_on", clickOn }, { "click_level", uni (-20.0f, -6.0f) }, { "click_pan", 0 }, { "click_start", 0 },
                      { "click_send", uni (0.3f, 1.0f) }, { "click_source", clickSample },
                      { "click_sample", clickSamples[r.nextInt ((int) std::size (clickSamples))] },
                      { "click_type", (float) r.nextInt (5) },
                      { "click_pitch", clickSample > 0.5f ? uni (-5.0f, 5.0f) : uni (-7.0f, 7.0f) },
                      { "click_length", clickSample > 0.5f ? logUni (15.0f, 80.0f) : logUni (3.0f, 40.0f) },
                      { "click_tone", uni (-0.4f, 0.4f) } });

            const auto highPass = chance (0.3f);
            add (v, { { "click_ftype", highPass > 0.5f ? 2.0f : 0.0f }, { "click_cutoff", logUni (300.0f, 3000.0f) },
                      { "click_reso", uni (0.0f, 0.3f) } });
        }

        add (v, { { "sub_on", on[2] }, { "sub_level", uni (-14.0f, -4.0f) }, { "sub_hold", uni (20.0f, 200.0f) },
                  { "sub_decay", uni (150.0f, 900.0f) }, { "sub_envamt", uni (0.0f, 0.3f) } });

        add (v, { { "tail_on", on[3] }, { "tail_level", uni (-18.0f, -8.0f) }, { "tail_source", chance (0.5f) },
                  { "tail_sample", (float) (2 + r.nextInt (3)) }, { "tail_decay", uni (400.0f, 1500.0f) },
                  { "tail_drive", uni (0.0f, 0.6f) }, { "tail_start", uni (0.0f, 40.0f) } });

        add (v, { { "noise_on", on[4] }, { "noise_level", uni (-30.0f, -16.0f) }, { "noise_decay", uni (30.0f, 200.0f) },
                  { "noise_color", (float) r.nextInt (3) } });

        const auto drive = chance (0.6f);
        add (v, { { "dist_on", drive }, { "dist_type", (float) r.nextInt (4) }, { "dist_drive", uni (4.0f, 24.0f) },
                  { "dist_tone", uni (0.4f, 1.0f) }, { "dist_out", -6.0f } });

        // The layer heard on its own (the only one on, or the only soloed one) is never
        // near-silent: its level is at least -9 dB (Click, Sub, Tail and Noise draw lower ones)
        // and its filter is off (a random high-pass over a dark click can all but remove it).
        {
            std::vector<int> heard, soloedOn;
            for (int layer = 0; layer < params::numLayers; ++layer)
            {
                if (on[(size_t) layer] < 0.5f)
                    continue;
                heard.push_back (layer);
                if (const auto* solo = state.getRawParameterValue (params::layerId (layer, "solo")); solo != nullptr && solo->load() > 0.5f)
                    soloedOn.push_back (layer);
            }
            if (! soloedOn.empty())
                heard = soloedOn;

            if (heard.size() == 1)
                for (auto& [id, value] : v)
                {
                    if (id == params::layerId (heard.front(), "level"))
                        value = std::max (value, soloLevelFloorDb);
                    else if (id == params::layerId (heard.front(), "ftype"))
                        value = 0.0f;
                }
        }

        return v;
    }

    juce::File getUserPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-kick").getChildFile ("Presets");
    }
}
