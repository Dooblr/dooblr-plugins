#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrchroma::params
{
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        inline constexpr const char* malletStiffness = "malletStiffness";
        inline constexpr const char* malletNoise = "malletNoise";
        inline constexpr const char* malletColor = "malletColor";
        inline constexpr const char* mixMallet = "mixMallet";
        inline constexpr const char* mixNoise = "mixNoise";
        inline constexpr const char* mixDirect = "mixDirect";
        inline constexpr const char* noiseFilter = "noiseFilter";
        inline constexpr const char* noiseFreq = "noiseFreq";
        inline constexpr const char* noiseQ = "noiseQ";
        inline constexpr const char* envMode = "envMode";
        inline constexpr const char* envAttack = "envAttack";
        inline constexpr const char* envHold = "envHold";
        inline constexpr const char* envDecay = "envDecay";
        inline constexpr const char* envSustain = "envSustain";
        inline constexpr const char* envRelease = "envRelease";
        inline constexpr const char* lfoRate = "lfoRate";
        inline constexpr const char* lfoDepth = "lfoDepth";
        inline constexpr const char* unisonCount = "unisonCount";
        inline constexpr const char* unisonDetune = "unisonDetune";
        inline constexpr const char* vibratoRate = "vibratoRate";
        inline constexpr const char* vibratoDepth = "vibratoDepth";
        inline constexpr const char* glide = "glide";
        inline constexpr const char* macro1 = "macro1";
        inline constexpr const char* macro2 = "macro2";
        inline constexpr const char* macro3 = "macro3";
        inline constexpr const char* macro4 = "macro4";
        inline constexpr const char* layerMode = "layerMode";
        inline constexpr const char* splitNote = "splitNote";
        inline constexpr const char* layer1Level = "layer1Level";
        inline constexpr const char* layer2Level = "layer2Level";
        inline constexpr const char* layer2Transpose = "layer2Transpose";
        inline constexpr const char* output = "output";
        inline constexpr const char* eqLow = "eqLow";
        inline constexpr const char* eqMid = "eqMid";
        inline constexpr const char* eqHigh = "eqHigh";
        inline constexpr const char* eqMidFreq = "eqMidFreq";
        inline constexpr const char* compThreshold = "compThreshold";
        inline constexpr const char* compRatio = "compRatio";
        inline constexpr const char* compAttack = "compAttack";
        inline constexpr const char* compRelease = "compRelease";
        inline constexpr const char* compMix = "compMix";
        inline constexpr const char* distDrive = "distDrive";
        inline constexpr const char* distMix = "distMix";
        inline constexpr const char* chorusRate = "chorusRate";
        inline constexpr const char* chorusDepth = "chorusDepth";
        inline constexpr const char* chorusMix = "chorusMix";
        inline constexpr const char* delayTime = "delayTime";
        inline constexpr const char* delayFeedback = "delayFeedback";
        inline constexpr const char* delayMix = "delayMix";
        inline constexpr const char* reverbSize = "reverbSize";
        inline constexpr const char* reverbDamp = "reverbDamp";
        inline constexpr const char* reverbMix = "reverbMix";

        // Per-layer coupling
        inline constexpr const char* l1_couple  = "l1_couple";
        inline constexpr const char* l1_balance = "l1_balance";
        inline constexpr const char* l2_couple  = "l2_couple";
        inline constexpr const char* l2_balance = "l2_balance";

        // Per-layer, per-resonator (l1/l2, a/b)
        // l1 resonator a
        inline constexpr const char* l1_a_on = "l1_a_on";
        inline constexpr const char* l1_a_type = "l1_a_type";
        inline constexpr const char* l1_a_density = "l1_a_density";
        inline constexpr const char* l1_a_pitch = "l1_a_pitch";
        inline constexpr const char* l1_a_cents = "l1_a_cents";
        inline constexpr const char* l1_a_key = "l1_a_key";
        inline constexpr const char* l1_a_pLevel = "l1_a_pLevel";
        inline constexpr const char* l1_a_pRate = "l1_a_pRate";
        inline constexpr const char* l1_a_pVel = "l1_a_pVel";
        inline constexpr const char* l1_a_decay = "l1_a_decay";
        inline constexpr const char* l1_a_decayKey = "l1_a_decayKey";
        inline constexpr const char* l1_a_decayVel = "l1_a_decayVel";
        inline constexpr const char* l1_a_release = "l1_a_release";
        inline constexpr const char* l1_a_material = "l1_a_material";
        inline constexpr const char* l1_a_tone = "l1_a_tone";
        inline constexpr const char* l1_a_lowCut = "l1_a_lowCut";
        inline constexpr const char* l1_a_radius = "l1_a_radius";
        inline constexpr const char* l1_a_hit = "l1_a_hit";
        inline constexpr const char* l1_a_hitVel = "l1_a_hitVel";
        inline constexpr const char* l1_a_hitKey = "l1_a_hitKey";
        inline constexpr const char* l1_a_hitRnd = "l1_a_hitRnd";
        inline constexpr const char* l1_a_part1 = "l1_a_part1";
        inline constexpr const char* l1_a_part2 = "l1_a_part2";
        inline constexpr const char* l1_a_part3 = "l1_a_part3";
        inline constexpr const char* l1_a_part4 = "l1_a_part4";

        // l1 resonator b
        inline constexpr const char* l1_b_on = "l1_b_on";
        inline constexpr const char* l1_b_type = "l1_b_type";
        inline constexpr const char* l1_b_density = "l1_b_density";
        inline constexpr const char* l1_b_pitch = "l1_b_pitch";
        inline constexpr const char* l1_b_cents = "l1_b_cents";
        inline constexpr const char* l1_b_key = "l1_b_key";
        inline constexpr const char* l1_b_pLevel = "l1_b_pLevel";
        inline constexpr const char* l1_b_pRate = "l1_b_pRate";
        inline constexpr const char* l1_b_pVel = "l1_b_pVel";
        inline constexpr const char* l1_b_decay = "l1_b_decay";
        inline constexpr const char* l1_b_decayKey = "l1_b_decayKey";
        inline constexpr const char* l1_b_decayVel = "l1_b_decayVel";
        inline constexpr const char* l1_b_release = "l1_b_release";
        inline constexpr const char* l1_b_material = "l1_b_material";
        inline constexpr const char* l1_b_tone = "l1_b_tone";
        inline constexpr const char* l1_b_lowCut = "l1_b_lowCut";
        inline constexpr const char* l1_b_radius = "l1_b_radius";
        inline constexpr const char* l1_b_hit = "l1_b_hit";
        inline constexpr const char* l1_b_hitVel = "l1_b_hitVel";
        inline constexpr const char* l1_b_hitKey = "l1_b_hitKey";
        inline constexpr const char* l1_b_hitRnd = "l1_b_hitRnd";
        inline constexpr const char* l1_b_part1 = "l1_b_part1";
        inline constexpr const char* l1_b_part2 = "l1_b_part2";
        inline constexpr const char* l1_b_part3 = "l1_b_part3";
        inline constexpr const char* l1_b_part4 = "l1_b_part4";

        // l2 resonator a
        inline constexpr const char* l2_a_on = "l2_a_on";
        inline constexpr const char* l2_a_type = "l2_a_type";
        inline constexpr const char* l2_a_density = "l2_a_density";
        inline constexpr const char* l2_a_pitch = "l2_a_pitch";
        inline constexpr const char* l2_a_cents = "l2_a_cents";
        inline constexpr const char* l2_a_key = "l2_a_key";
        inline constexpr const char* l2_a_pLevel = "l2_a_pLevel";
        inline constexpr const char* l2_a_pRate = "l2_a_pRate";
        inline constexpr const char* l2_a_pVel = "l2_a_pVel";
        inline constexpr const char* l2_a_decay = "l2_a_decay";
        inline constexpr const char* l2_a_decayKey = "l2_a_decayKey";
        inline constexpr const char* l2_a_decayVel = "l2_a_decayVel";
        inline constexpr const char* l2_a_release = "l2_a_release";
        inline constexpr const char* l2_a_material = "l2_a_material";
        inline constexpr const char* l2_a_tone = "l2_a_tone";
        inline constexpr const char* l2_a_lowCut = "l2_a_lowCut";
        inline constexpr const char* l2_a_radius = "l2_a_radius";
        inline constexpr const char* l2_a_hit = "l2_a_hit";
        inline constexpr const char* l2_a_hitVel = "l2_a_hitVel";
        inline constexpr const char* l2_a_hitKey = "l2_a_hitKey";
        inline constexpr const char* l2_a_hitRnd = "l2_a_hitRnd";
        inline constexpr const char* l2_a_part1 = "l2_a_part1";
        inline constexpr const char* l2_a_part2 = "l2_a_part2";
        inline constexpr const char* l2_a_part3 = "l2_a_part3";
        inline constexpr const char* l2_a_part4 = "l2_a_part4";

        // l2 resonator b
        inline constexpr const char* l2_b_on = "l2_b_on";
        inline constexpr const char* l2_b_type = "l2_b_type";
        inline constexpr const char* l2_b_density = "l2_b_density";
        inline constexpr const char* l2_b_pitch = "l2_b_pitch";
        inline constexpr const char* l2_b_cents = "l2_b_cents";
        inline constexpr const char* l2_b_key = "l2_b_key";
        inline constexpr const char* l2_b_pLevel = "l2_b_pLevel";
        inline constexpr const char* l2_b_pRate = "l2_b_pRate";
        inline constexpr const char* l2_b_pVel = "l2_b_pVel";
        inline constexpr const char* l2_b_decay = "l2_b_decay";
        inline constexpr const char* l2_b_decayKey = "l2_b_decayKey";
        inline constexpr const char* l2_b_decayVel = "l2_b_decayVel";
        inline constexpr const char* l2_b_release = "l2_b_release";
        inline constexpr const char* l2_b_material = "l2_b_material";
        inline constexpr const char* l2_b_tone = "l2_b_tone";
        inline constexpr const char* l2_b_lowCut = "l2_b_lowCut";
        inline constexpr const char* l2_b_radius = "l2_b_radius";
        inline constexpr const char* l2_b_hit = "l2_b_hit";
        inline constexpr const char* l2_b_hitVel = "l2_b_hitVel";
        inline constexpr const char* l2_b_hitKey = "l2_b_hitKey";
        inline constexpr const char* l2_b_hitRnd = "l2_b_hitRnd";
        inline constexpr const char* l2_b_part1 = "l2_b_part1";
        inline constexpr const char* l2_b_part2 = "l2_b_part2";
        inline constexpr const char* l2_b_part3 = "l2_b_part3";
        inline constexpr const char* l2_b_part4 = "l2_b_part4";

    }

    enum class NoiseFilter { lowpass = 0, highpass, bandpass, lpHp };
    enum class EnvMode { adsr = 0, ahd };
    enum class LayerMode { layered = 0, split };
    enum class Density { low = 0, medium, high, full };

    inline juce::StringArray resTypeNames()
    {
        return { "String", "Bar", "Marimba", "Plate", "Drumhead", "Membrane",
                 "Open Tube", "Closed Tube", "Manual" };
    }

    inline juce::StringArray densityNames()
    {
        return { "4", "16", "30", "70" };
    }

    inline juce::StringArray noiseFilterNames()
    {
        return { "Low-pass", "High-pass", "Band-pass", "LP+HP" };
    }

    inline juce::StringArray envModeNames() { return { "ADSR", "AHD" }; }
    inline juce::StringArray layerModeNames() { return { "Layered", "Split" }; }
    inline juce::StringArray unisonNames() { return { "1", "2", "3", "4" }; }

    inline juce::ParameterID pid (const char* identifier) { return { identifier, 1 }; }

    using Attributes = juce::AudioParameterFloatAttributes;

    inline juce::NormalisableRange<float> rangeWithCentre (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
