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
                          { "lim_mode", 2 }, { "lim_ceiling", -0.3f } });
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
                          { "lim_mode", 2 }, { "lim_ceiling", -0.5f } });
                presets.push_back ({ "Radio Voice", std::move (v) });
            }

            {
                ValueList v;
                add (v, { { "corr_mode", 0 }, { "retune_speed", 25 }, { "humanize", 0.2f }, { "corr_amount", 1 },
                          { "formant", 1 }, { "quality", 1 }, { "gate_on", 0 },
                          { "eq_on", 1 }, { "eq_high_freq", 12000 }, { "eq_high_gain", 1 },
                          { "comp_on", 1 }, { "comp_threshold", -20 }, { "comp_ratio", 2.5f },
                          { "comp_attack", 15 }, { "comp_release", 140 }, { "lim_mode", 2 } });
                presets.push_back ({ "HQ Mix", std::move (v) });
            }

            return presets;
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

        for (auto* parameter : state.processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
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
