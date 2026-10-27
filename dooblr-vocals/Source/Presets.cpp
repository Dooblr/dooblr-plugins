#include "Presets.h"

#include "Parameters.h"

#include <algorithm>

namespace dooblrvocals::presets
{
    namespace
    {
        void add (ValueList& v, std::initializer_list<std::pair<const char*, float>> values)
        {
            for (const auto& [id, value] : values)
                v.emplace_back (id, value);
        }

        std::vector<FactoryPreset> makePresets()
        {
            std::vector<FactoryPreset> presets;
            presets.push_back ({ "Init", {} });

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "corr_key", 0 }, { "corr_scale", 0 }, { "retune_speed", 8 },
                          { "humanize", 0.08f }, { "corr_amount", 1 }, { "formant", 1 }, { "input_type", 2 },
                          { "quality", 0 }, { "gate_on", 1 }, { "gate_threshold", -42 }, { "gate_attack", 0.8f },
                          { "gate_hold", 40 }, { "gate_release", 60 }, { "gate_range", 50 },
                          { "eq_on", 1 }, { "eq_low_freq", 90 }, { "eq_low_gain", -1.5f },
                          { "eq_mid_freq", 3000 }, { "eq_mid_gain", 2.0f }, { "eq_mid_q", 0.9f },
                          { "eq_high_freq", 10000 }, { "eq_high_gain", 1.5f },
                          { "comp_on", 1 }, { "comp_threshold", -18 }, { "comp_ratio", 3 },
                          { "comp_attack", 10 }, { "comp_release", 90 }, { "comp_makeup", 2 },
                          { "lim_mode", 2 }, { "lim_ceiling", -0.3f },
                          // v0.3: a ducked plate.
                          { "rev_on", 1 }, { "rev_predelay", 30 }, { "rev_decay", 2.2f }, { "rev_size", 0.55f },
                          { "rev_damping", 0.4f }, { "rev_lowcut", 300 }, { "rev_highcut", 10000 }, { "rev_width", 1.2f },
                          { "rev_early", 0.8f }, { "rev_diffusion", 0.85f }, { "rev_mod", 0.3f }, { "rev_duck", 9 },
                          { "rev_duck_release", 350 }, { "rev_mix", 0.24f } });
                presets.push_back ({ "Tight Pop", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 80 }, { "humanize", 0.45f }, { "corr_amount", 0.7f },
                          { "formant", 1 }, { "quality", 0 }, { "gate_on", 0 }, { "eq_on", 0 }, { "comp_on", 0 },
                          { "lim_mode", 2 } });
                presets.push_back ({ "Natural", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 1 }, { "retune_speed", 4 }, { "humanize", 0.05f }, { "corr_amount", 1 },
                          { "formant", 1 }, { "quality", 0 }, { "gate_on", 1 }, { "gate_threshold", -48 },
                          { "comp_on", 1 }, { "comp_threshold", -16 }, { "comp_ratio", 2.5f },
                          { "lim_mode", 2 } });
                presets.push_back ({ "Chromatic Track", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 18 }, { "humanize", 0.12f }, { "corr_amount", 0.85f },
                          { "gate_on", 1 }, { "gate_threshold", -38 }, { "gate_range", 70 },
                          { "eq_on", 1 }, { "eq_low_freq", 120 }, { "eq_low_gain", -3 },
                          { "eq_mid_freq", 2500 }, { "eq_mid_gain", 3.5f }, { "eq_mid_q", 1.2f },
                          { "eq_high_freq", 9000 }, { "eq_high_gain", 2 },
                          { "comp_on", 1 }, { "comp_threshold", -14 }, { "comp_ratio", 4 },
                          { "comp_attack", 8 }, { "comp_release", 80 }, { "comp_makeup", 3 },
                          { "lim_mode", 2 }, { "lim_ceiling", -0.5f },
                          // v0.3: slapback.
                          { "dly_on", 1 }, { "dly_sync", 0 }, { "dly_time", 110 }, { "dly_feedback", 0.1f },
                          { "dly_lowcut", 200 }, { "dly_highcut", 5000 }, { "dly_drive", 0.2f }, { "dly_width", 0.6f },
                          { "dly_mix", 0.18f } });
                presets.push_back ({ "Radio Voice", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 25 }, { "humanize", 0.2f }, { "corr_amount", 1 },
                          { "formant", 1 }, { "quality", 1 }, { "gate_on", 0 },
                          { "eq_on", 1 }, { "eq_high_freq", 12000 }, { "eq_high_gain", 1 },
                          { "comp_on", 1 }, { "comp_threshold", -20 }, { "comp_ratio", 2.5f },
                          { "comp_attack", 15 }, { "comp_release", 140 }, { "lim_mode", 2 },
                          // v0.3: hall + quarter-note ping-pong in parallel.
                          { "rev_on", 1 }, { "rev_predelay", 30 }, { "rev_decay", 2.4f }, { "rev_size", 0.65f },
                          { "rev_damping", 0.45f }, { "rev_lowcut", 250 }, { "rev_highcut", 8500 }, { "rev_width", 1.15f },
                          { "rev_early", 0.6f }, { "rev_mod", 0.35f }, { "rev_mix", 0.2f },
                          { "dly_on", 1 }, { "dly_mode", 1 }, { "dly_sync", 1 }, { "dly_note", 10 }, { "dly_feedback", 0.3f },
                          { "dly_lowcut", 250 }, { "dly_highcut", 6000 }, { "dly_duck", 6 }, { "dly_mix", 0.14f },
                          { "fx_routing", 0 } });
                presets.push_back ({ "HQ Mix", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 15 }, { "humanize", 0.12f }, { "corr_amount", 1 },
                          { "formant", 1 }, { "harm_on", 1 }, { "harm_level", -5 }, { "harm_formant", 1 },
                          { "harm1_on", 1 }, { "harm1_mode", 0 }, { "harm1_degree", 2 }, { "harm1_level", -3 }, { "harm1_pan", -0.4f },
                          { "harm2_on", 1 }, { "harm2_mode", 0 }, { "harm2_degree", 4 }, { "harm2_level", -4 }, { "harm2_pan", 0.4f },
                          { "harm3_on", 0 }, { "harm4_on", 0 },
                          { "comp_on", 1 }, { "comp_threshold", -18 }, { "comp_ratio", 3 }, { "lim_mode", 2 },
                          // v0.3: vocal plate.
                          { "rev_on", 1 }, { "rev_predelay", 20 }, { "rev_decay", 1.8f }, { "rev_size", 0.55f },
                          { "rev_damping", 0.45f }, { "rev_early", 0.75f }, { "rev_diffusion", 0.85f }, { "rev_mix", 0.2f } });
                presets.push_back ({ "Harmony 3rd + 5th", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 20 }, { "humanize", 0.15f }, { "formant", 1 },
                          { "harm_on", 1 }, { "harm_level", -6 }, { "harm_formant", 1 },
                          { "harm1_on", 1 }, { "harm1_mode", 1 }, { "harm1_semis", -12 }, { "harm1_level", -4 },
                          { "harm1_pan", -0.8f }, { "harm1_detune", -7 }, { "harm1_delay", 12 },
                          { "harm2_on", 1 }, { "harm2_mode", 0 }, { "harm2_degree", 4 }, { "harm2_level", -6 },
                          { "harm2_pan", 0.8f }, { "harm2_detune", 6 }, { "harm2_delay", 18 },
                          { "harm3_on", 1 }, { "harm3_mode", 1 }, { "harm3_semis", 12 }, { "harm3_level", -14 },
                          { "harm3_pan", 0 }, { "harm3_detune", 4 }, { "harm3_delay", 6 },
                          { "harm4_on", 0 },
                          { "comp_on", 1 }, { "comp_threshold", -20 }, { "comp_ratio", 2.5f }, { "lim_mode", 2 } });
                presets.push_back ({ "Wide Oct + 5th", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 30 }, { "humanize", 0.3f }, { "corr_amount", 0.9f },
                          { "formant", 1 }, { "quality", 0 },
                          { "eq_on", 1 }, { "eq_low_freq", 100 }, { "eq_low_gain", -2 }, { "eq_high_freq", 11000 }, { "eq_high_gain", 1.5f },
                          { "comp_on", 1 }, { "comp_threshold", -20 }, { "comp_ratio", 2.5f }, { "lim_mode", 2 },
                          { "dly_on", 1 }, { "dly_mode", 1 }, { "dly_sync", 1 }, { "dly_note", 13 }, { "dly_feedback", 0.45f },
                          { "dly_lowcut", 400 }, { "dly_highcut", 4500 }, { "dly_duck", 12 }, { "dly_duck_release", 500 },
                          { "dly_width", 1.3f }, { "dly_mix", 0.22f },
                          { "rev_on", 1 }, { "rev_predelay_sync", 3 }, { "rev_decay", 3.0f }, { "rev_size", 0.75f },
                          { "rev_damping", 0.5f }, { "rev_width", 1.2f }, { "rev_early", 0.65f }, { "rev_mod", 0.4f },
                          { "rev_duck", 5 }, { "rev_duck_release", 600 }, { "rev_mix", 0.26f },
                          { "fx_routing", 1 } });
                presets.push_back ({ "Ballad Throw", std::move (v) });
            }

            return presets;
        }

        std::vector<FactoryPreset> makeReverbPresets()
        {
            // pre-delay ms, decay s, size, damping, low cut, high cut, width, early/late, diffusion, mod, duck dB, duck rel, mix
            struct R { const char* name; float pre, decay, size, damp, lc, hc, width, early, diff, mod, duck, rel, mix; int sync = 0; };
            const R list[] {
                { "Vocal Plate",        20, 1.8f, 0.55f, 0.45f, 250, 9000,  1.0f,  0.75f, 0.85f, 0.30f, 0,  300, 0.22f },
                { "Bright Plate",       25, 2.0f, 0.50f, 0.20f, 300, 14000, 1.1f,  0.80f, 0.85f, 0.35f, 0,  300, 0.20f },
                { "Small Room",          5, 0.6f, 0.20f, 0.50f, 200, 8000,  0.9f,  0.35f, 0.70f, 0.20f, 0,  300, 0.18f },
                { "Vocal Booth",         0, 0.3f, 0.05f, 0.60f, 250, 7000,  0.6f,  0.15f, 0.60f, 0.10f, 0,  300, 0.12f },
                { "Medium Hall",        30, 2.4f, 0.65f, 0.45f, 250, 8500,  1.15f, 0.60f, 0.80f, 0.35f, 0,  300, 0.22f },
                { "Large Hall",         45, 3.6f, 0.85f, 0.50f, 280, 7500,  1.25f, 0.65f, 0.85f, 0.40f, 0,  300, 0.25f },
                { "Cathedral",          60, 7.5f, 1.00f, 0.55f, 300, 6500,  1.35f, 0.70f, 0.90f, 0.45f, 0,  300, 0.25f },
                { "Slap / Short Ambience", 70, 0.45f, 0.30f, 0.40f, 350, 8000, 1.0f, 0.20f, 0.50f, 0.15f, 0, 300, 0.18f },
                { "Dark Chamber",       15, 1.4f, 0.40f, 0.75f, 200, 4500,  0.9f,  0.50f, 0.80f, 0.25f, 0,  300, 0.20f },
                { "Ducked Pop Plate",   30, 2.2f, 0.55f, 0.40f, 300, 10000, 1.2f,  0.80f, 0.85f, 0.30f, 9,  350, 0.30f },
                { "Lo-Fi Room",          8, 0.9f, 0.30f, 0.85f, 450, 3500,  0.7f,  0.40f, 0.55f, 0.70f, 0,  300, 0.22f },
                { "Ballad Hall",        30, 3.0f, 0.75f, 0.50f, 250, 8000,  1.2f,  0.65f, 0.85f, 0.40f, 5,  600, 0.26f, 3 },
                { "Rap Tight Room",      0, 0.35f, 0.15f, 0.55f, 400, 7500, 0.8f,  0.25f, 0.60f, 0.10f, 6,  120, 0.12f },
            };

            std::vector<FactoryPreset> out;
            for (const auto& r : list)
            {
                ValueList v;
                add (v, { { "rev_predelay", r.pre }, { "rev_predelay_sync", (float) r.sync }, { "rev_decay", r.decay },
                          { "rev_size", r.size }, { "rev_damping", r.damp }, { "rev_lowcut", r.lc }, { "rev_highcut", r.hc },
                          { "rev_width", r.width }, { "rev_early", r.early }, { "rev_diffusion", r.diff },
                          { "rev_mod", r.mod }, { "rev_duck", r.duck }, { "rev_duck_release", r.rel }, { "rev_mix", r.mix } });
                out.push_back ({ r.name, std::move (v) });
            }
            return out;
        }

        std::vector<FactoryPreset> makeDelayPresets()
        {
            std::vector<FactoryPreset> out;
            const auto make = [&out] (const char* name, std::initializer_list<std::pair<const char*, float>> values)
            {
                ValueList v;
                add (v, values);
                out.push_back ({ name, std::move (v) });
            };

            make ("Slapback", { { "dly_mode", 0 }, { "dly_sync", 0 }, { "dly_time", 110 }, { "dly_feedback", 0.1f },
                                { "dly_lowcut", 200 }, { "dly_highcut", 5000 }, { "dly_drive", 0.2f },
                                { "dly_width", 0.6f }, { "dly_mix", 0.2f } });
            make ("1/4 Ping-Pong", { { "dly_mode", 1 }, { "dly_sync", 1 }, { "dly_note", 10 }, { "dly_feedback", 0.35f },
                                     { "dly_lowcut", 250 }, { "dly_highcut", 6000 }, { "dly_width", 1.2f },
                                     { "dly_duck", 6 }, { "dly_duck_release", 300 }, { "dly_mix", 0.2f } });
            make ("Dotted 1/8", { { "dly_mode", 0 }, { "dly_sync", 1 }, { "dly_note", 8 }, { "dly_feedback", 0.4f },
                                  { "dly_lowcut", 250 }, { "dly_highcut", 6500 }, { "dly_mod", 0.15f },
                                  { "dly_duck", 6 }, { "dly_duck_release", 250 }, { "dly_mix", 0.2f } });
            make ("Dark Tape", { { "dly_mode", 0 }, { "dly_sync", 1 }, { "dly_note", 10 }, { "dly_feedback", 0.5f },
                                 { "dly_lowcut", 150 }, { "dly_highcut", 2500 }, { "dly_mod", 0.45f },
                                 { "dly_drive", 0.55f }, { "dly_mix", 0.22f } });
            make ("Throw", { { "dly_mode", 1 }, { "dly_sync", 1 }, { "dly_note", 13 }, { "dly_feedback", 0.55f },
                             { "dly_lowcut", 400 }, { "dly_highcut", 4500 }, { "dly_duck", 12 },
                             { "dly_duck_release", 500 }, { "dly_width", 1.3f }, { "dly_mix", 0.35f } });
            make ("Wide Doubler", { { "dly_mode", 2 }, { "dly_sync", 0 }, { "dly_time", 14 }, { "dly_time_r", 23 },
                                    { "dly_feedback", 0 }, { "dly_lowcut", 150 }, { "dly_highcut", 12000 },
                                    { "dly_mod", 0.35f }, { "dly_width", 1.5f }, { "dly_mix", 0.3f } });
            return out;
        }
    }

    const std::vector<FactoryPreset>& getReverbPresets()
    {
        static const auto presets = makeReverbPresets();
        return presets;
    }

    const std::vector<FactoryPreset>& getDelayPresets()
    {
        static const auto presets = makeDelayPresets();
        return presets;
    }

    bool isSessionOnlyParameter (const juce::String& id)
    {
        return id.startsWith ("range_");
    }

    ValueList resolveSection (juce::AudioProcessorValueTreeState& state, const juce::String& prefix, const ValueList& overrides)
    {
        ValueList values;

        for (auto* parameter : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (ranged->paramID.startsWith (prefix))
                    values.emplace_back (ranged->paramID, ranged->paramID == prefix + "on"
                                                              ? 1.0f : ranged->convertFrom0to1 (ranged->getDefaultValue()));

        for (const auto& [id, value] : overrides)
            for (auto& pair : values)
                if (pair.first == id)
                {
                    pair.second = value;
                    break;
                }

        return values;
    }

    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        static const auto presets = makePresets();
        return presets;
    }

    ValueList resolve (juce::AudioProcessorValueTreeState& state, const ValueList& overrides)
    {
        ValueList values;

        for (auto* parameter : state.processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (! isSessionOnlyParameter (ranged->paramID))
                    values.emplace_back (ranged->paramID, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        }

        for (const auto& [id, value] : overrides)
            for (auto& pair : values)
                if (pair.first == id)
                {
                    pair.second = value;
                    break;
                }

        return values;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const ValueList& values, bool asGesture)
    {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;

        for (const auto& [id, value] : values)
        {
            auto* parameter = state.getParameter (id);

            if (parameter == nullptr)
                continue;

            const auto normalised = parameter->convertTo0to1 (parameter->getNormalisableRange().snapToLegalValue (value));

            if (std::abs (normalised - parameter->getValue()) > 1.0e-7f)
                changes.emplace_back (parameter, normalised);
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

    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& random)
    {
        ValueList v;
        const auto f = [&] (const char* id, float lo, float hi)
        {
            v.emplace_back (id, lo + random.nextFloat() * (hi - lo));
        };

        v.emplace_back ("corr_mode", random.nextFloat() > 0.65f ? 1.0f : 0.0f);
        v.emplace_back ("corr_key", (float) random.nextInt (12));
        v.emplace_back ("corr_scale", (float) random.nextInt (9));
        f ("retune_speed", 4.0f, 60.0f);
        f ("humanize", 0.05f, 0.4f);
        f ("corr_amount", 0.7f, 1.0f);
        v.emplace_back ("formant", random.nextFloat() > 0.2f ? 1.0f : 0.0f);
        v.emplace_back ("input_type", (float) random.nextInt (5));
        v.emplace_back ("gate_on", random.nextFloat() > 0.4f ? 1.0f : 0.0f);
        f ("gate_threshold", -55.0f, -30.0f);
        f ("gate_attack", 0.5f, 8.0f);
        f ("gate_hold", 10.0f, 120.0f);
        f ("gate_release", 40.0f, 200.0f);
        v.emplace_back ("eq_on", random.nextFloat() > 0.35f ? 1.0f : 0.0f);
        f ("eq_low_gain", -4.0f, 2.0f);
        f ("eq_mid_gain", -2.0f, 4.0f);
        f ("eq_high_gain", -1.0f, 4.0f);
        v.emplace_back ("comp_on", random.nextFloat() > 0.3f ? 1.0f : 0.0f);
        f ("comp_threshold", -24.0f, -10.0f);
        f ("comp_ratio", 2.0f, 6.0f);
        f ("comp_makeup", 0.0f, 4.0f);
        juce::ignoreUnused (state);
        return v;
    }

    juce::File getUserPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-vocals/Presets");
    }
}
