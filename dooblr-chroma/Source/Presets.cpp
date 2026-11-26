#include "Presets.h"
#include "PresetPaths.h"
#include "Parameters.h"

namespace dooblrchroma::presets
{
    namespace
    {
        void setReal (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
        {
            if (auto* p = state.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
            else
                jassertfalse;
        }
    }

    const std::vector<Factory>& factory()
    {
        using namespace params::id;
        static const std::vector<Factory> patches {
            { "Init", "", {} },

            // --- Mallets ---
            { "Marimba", "Mallets", {
                { l1_a_type, 2.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.1f }, { l1_a_material, -0.45f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.18f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.62f }, { malletNoise, 0.08f }, { mixMallet, 0.9f },
                { mixNoise, 0.0f }, { mixDirect, 0.0f } } },
            { "Kalimba", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.7f }, { l1_a_material, -0.25f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.14f }, { l1_a_release, 0.25f }, { l1_a_pitch, 12.0f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.78f }, { malletNoise, 0.05f }, { mixMallet, 0.95f } } },
            { "Bells", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 4.5f }, { l1_a_material, -0.7f }, { l1_a_tone, -6.0f },
                { l1_a_hit, 0.2f }, { l1_a_pitch, 12.0f }, { l1_a_release, 0.5f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.85f }, { malletNoise, 0.04f }, { mixMallet, 0.8f },
                { reverbMix, 0.22f }, { reverbSize, 0.6f } } },
            { "Vibraphone", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.8f }, { l1_a_material, -0.35f }, { l1_a_tone, -3.0f },
                { l1_a_hit, 0.2f }, { l1_a_release, 0.45f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.55f }, { malletNoise, 0.04f }, { mixMallet, 0.88f },
                { vibratoRate, 4.5f }, { vibratoDepth, 18.0f },
                { chorusMix, 0.12f }, { chorusRate, 0.9f }, { chorusDepth, 0.35f },
                { reverbMix, 0.16f }, { reverbSize, 0.45f } } },
            { "Glockenspiel", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 19.0f }, { l1_a_decay, 2.2f }, { l1_a_material, -0.7f },
                { l1_a_tone, -2.0f }, { l1_a_hit, 0.2f }, { l1_a_release, 0.4f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.85f }, { malletNoise, 0.04f }, { mixMallet, 1.0f },
                { reverbMix, 0.18f }, { reverbSize, 0.5f } } },
            { "Xylophone", "Mallets", {
                { l1_a_type, 2.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.45f }, { l1_a_material, -0.3f }, { l1_a_tone, -1.0f },
                { l1_a_hit, 0.16f }, { l1_a_release, 0.2f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.8f }, { malletNoise, 0.1f }, { mixMallet, 0.92f } } },
            { "Celesta", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 12.0f }, { l1_a_decay, 1.8f }, { l1_a_material, -0.5f },
                { l1_a_tone, -3.0f }, { l1_a_hit, 0.2f }, { l1_a_release, 0.4f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.7f }, { malletNoise, 0.04f }, { mixMallet, 1.0f },
                { reverbMix, 0.24f }, { reverbSize, 0.55f }, { reverbDamp, 0.4f } } },
            { "Music Box", "Mallets", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 12.0f }, { l1_a_decay, 1.4f }, { l1_a_material, -0.4f },
                { l1_a_tone, -2.0f }, { l1_a_hit, 0.16f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.82f }, { malletNoise, 0.04f }, { mixMallet, 1.0f },
                { reverbMix, 0.14f }, { reverbSize, 0.3f } } },

            // --- Plucked ---
            { "Plucked String", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.4f }, { l1_a_material, -0.65f }, { l1_a_tone, -6.0f },
                { l1_a_hit, 0.13f }, { l1_a_release, 0.28f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.9f }, { malletNoise, 0.15f }, { malletColor, 2500.0f },
                { mixMallet, 0.9f }, { mixDirect, 0.05f } } },
            { "Harp", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 3.6f }, { l1_a_material, -0.55f }, { l1_a_tone, -7.0f },
                { l1_a_hit, 0.1f }, { l1_a_release, 0.5f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.88f }, { malletNoise, 0.08f }, { mixMallet, 0.9f },
                { reverbMix, 0.2f }, { reverbSize, 0.65f } } },
            { "Koto", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.8f }, { l1_a_material, 0.25f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.17f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.86f }, { malletNoise, 0.12f }, { malletColor, 1800.0f },
                { mixMallet, 0.92f }, { mixDirect, 0.03f } } },
            { "Pizzicato", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.55f }, { l1_a_material, -0.2f }, { l1_a_tone, -5.0f },
                { l1_a_hit, 0.12f }, { l1_a_release, 0.18f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.94f }, { malletNoise, 0.1f }, { mixMallet, 0.95f } } },
            { "Plucked Guitar", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.0f }, { l1_a_material, -0.45f }, { l1_a_tone, -8.0f },
                { l1_a_hit, 0.14f }, { l1_a_release, 0.32f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 0.35f },
                { l1_b_tone, -6.0f }, { l1_b_pitch, -12.0f }, { l1_b_hit, 0.4f },
                { l1_couple, 0.0f }, { l1_balance, 0.18f },
                { malletStiffness, 0.87f }, { malletNoise, 0.18f }, { malletColor, 2000.0f },
                { mixMallet, 0.9f }, { mixDirect, 0.04f } } },
            { "Dulcimer", "Plucked", {
                { l1_a_type, 0.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.6f }, { l1_a_material, -0.3f }, { l1_a_tone, -3.0f },
                { l1_a_hit, 0.2f }, { l1_a_release, 0.28f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.84f }, { malletNoise, 0.08f }, { mixMallet, 0.9f },
                { unisonCount, 1.0f }, { unisonDetune, 8.0f } } },

            // --- Struck / drums ---
            { "Steel Drum", "Struck", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.8f }, { l1_a_material, -0.2f }, { l1_a_tone, -1.0f },
                { l1_a_hit, 0.28f }, { l1_a_release, 0.4f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, 7.0f },
                { l1_b_decay, 0.9f }, { l1_b_tone, -3.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.42f },
                { malletStiffness, 0.7f }, { mixMallet, 0.88f } } },
            { "Coupled Plates", "Struck", {
                { l1_a_type, 3.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.2f }, { l1_a_material, -0.35f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.22f },
                { l1_b_type, 3.0f }, { l1_b_density, 1.0f }, { l1_b_on, 1.0f },
                { l1_b_decay, 1.6f }, { l1_b_pitch, 5.0f }, { l1_b_tone, -3.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.5f },
                { malletStiffness, 0.58f }, { mixMallet, 1.0f } } },
            { "Bass Drum", "Struck", {
                { l1_a_type, 4.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, -12.0f }, { l1_a_decay, 0.55f }, { l1_a_material, -0.5f },
                { l1_a_tone, -8.0f }, { l1_a_hit, 0.85f }, { l1_a_release, 0.4f },
                { l1_b_type, 5.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, -12.0f },
                { l1_b_decay, 0.25f }, { l1_b_tone, -6.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.3f },
                { malletStiffness, 0.35f }, { malletNoise, 0.2f }, { mixMallet, 1.0f } } },
            { "Tom", "Struck", {
                { l1_a_type, 4.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, -5.0f }, { l1_a_decay, 0.7f }, { l1_a_material, -0.35f },
                { l1_a_tone, -5.0f }, { l1_a_hit, 0.7f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.45f }, { malletNoise, 0.12f }, { mixMallet, 0.95f } } },
            { "Tabla", "Struck", {
                { l1_a_type, 4.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 7.0f }, { l1_a_decay, 0.35f }, { l1_a_material, -0.15f },
                { l1_a_tone, -2.0f }, { l1_a_hit, 0.55f }, { l1_a_release, 0.2f },
                { l1_b_type, 7.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, 7.0f },
                { l1_b_decay, 0.2f }, { l1_b_radius, 0.35f },
                { l1_couple, 1.0f }, { l1_balance, 0.28f },
                { malletStiffness, 0.7f }, { malletNoise, 0.08f }, { mixMallet, 0.9f } } },
            { "Hand Drum", "Struck", {
                { l1_a_type, 5.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.5f }, { l1_a_material, -0.2f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.65f }, { l1_a_release, 0.25f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.4f }, { malletNoise, 0.15f }, { mixMallet, 0.92f } } },
            { "Gong", "Struck", {
                { l1_a_type, 3.0f }, { l1_a_density, 3.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, -12.0f }, { l1_a_decay, 6.0f }, { l1_a_material, -0.5f },
                { l1_a_tone, -3.0f }, { l1_a_hit, 0.35f }, { l1_a_release, 0.8f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.4f }, { malletNoise, 0.1f }, { mixMallet, 0.6f },
                { reverbMix, 0.22f }, { reverbSize, 0.75f }, { reverbDamp, 0.35f } } },
            { "Thin Cymbal", "Struck", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 7.0f }, { l1_a_decay, 3.5f }, { l1_a_material, -0.7f },
                { l1_a_tone, 1.0f }, { l1_a_hit, 0.12f }, { l1_a_release, 0.6f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.65f }, { malletNoise, 0.06f }, { malletColor, 3500.0f },
                { mixMallet, 0.4f }, { reverbMix, 0.16f } } },
            { "Tank Drum", "Struck", {
                { l1_a_type, 3.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.4f }, { l1_a_material, -0.55f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.25f }, { l1_a_release, 0.45f },
                { l1_b_type, 1.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 1.4f },
                { l1_b_pitch, 12.0f }, { l1_b_tone, -3.0f }, { l1_b_hit, 0.2f },
                { l1_couple, 1.0f }, { l1_balance, 0.38f },
                { malletStiffness, 0.68f }, { mixMallet, 0.88f },
                { reverbMix, 0.14f } } },

            // --- Winds / pipes ---
            { "Open Pipe", "Winds", {
                { l1_a_type, 6.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.6f }, { l1_a_radius, 0.6f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.25f }, { l1_a_release, 0.35f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.4f }, { mixMallet, 0.55f }, { mixNoise, 0.35f },
                { envAttack, 0.02f }, { envDecay, 0.4f }, { envSustain, 0.15f },
                { envRelease, 0.3f }, { noiseFilter, 0.0f }, { noiseFreq, 1200.0f } } },
            { "Flute Pipe", "Winds", {
                { l1_a_type, 6.0f }, { l1_a_on, 1.0f }, { l1_a_density, 1.0f },
                { l1_a_decay, 1.2f }, { l1_a_radius, 0.45f }, { l1_a_tone, -1.0f },
                { l1_a_hit, 0.3f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.3f }, { mixMallet, 0.12f }, { mixNoise, 0.5f },
                { envAttack, 0.06f }, { envDecay, 0.5f }, { envSustain, 0.45f },
                { envRelease, 0.25f }, { noiseFilter, 0.0f }, { noiseFreq, 1600.0f },
                { noiseQ, 0.7f }, { lfoRate, 5.0f }, { lfoDepth, 0.12f } } },
            { "Pan Pipes", "Winds", {
                { l1_a_type, 6.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.9f }, { l1_a_radius, 0.55f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.28f }, { l1_a_release, 0.25f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.35f }, { mixMallet, 0.3f }, { mixNoise, 0.55f },
                { envAttack, 0.03f }, { envDecay, 0.35f }, { envSustain, 0.2f },
                { envRelease, 0.22f }, { noiseFilter, 0.0f }, { noiseFreq, 1400.0f },
                { reverbMix, 0.12f } } },
            { "Organ Pipe", "Winds", {
                { l1_a_type, 7.0f }, { l1_a_on, 1.0f }, { l1_a_density, 2.0f },
                { l1_a_decay, 3.0f }, { l1_a_radius, 0.7f }, { l1_a_tone, -3.0f },
                { l1_a_hit, 0.35f }, { l1_a_release, 0.5f },
                { l1_b_on, 0.0f },
                { mixMallet, 0.05f }, { mixNoise, 0.8f },
                { envAttack, 0.08f }, { envDecay, 0.6f }, { envSustain, 0.85f },
                { envRelease, 0.4f }, { noiseFilter, 0.0f }, { noiseFreq, 800.0f },
                { chorusMix, 0.1f } } },
            { "Bottle", "Winds", {
                { l1_a_type, 7.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.8f }, { l1_a_radius, 0.3f }, { l1_a_tone, 0.0f },
                { l1_a_hit, 0.4f }, { l1_a_release, 0.22f }, { l1_a_pitch, 7.0f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.25f }, { mixMallet, 0.08f }, { mixNoise, 0.08f },
                { envAttack, 0.04f }, { envDecay, 0.3f }, { envSustain, 0.15f },
                { envRelease, 0.2f }, { noiseFilter, 2.0f }, { noiseFreq, 700.0f }, { noiseQ, 0.7f } } },

            // --- Bowed / sustained ---
            { "Bowed Bar", "Bowed", {
                { l1_a_type, 1.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 4.0f }, { l1_a_material, -0.2f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.3f }, { l1_a_release, 0.6f },
                { l1_b_on, 0.0f },
                { mixMallet, 0.0f }, { mixNoise, 0.7f },
                { envAttack, 0.25f }, { envDecay, 0.8f }, { envSustain, 0.75f },
                { envRelease, 0.5f }, { noiseFilter, 2.0f }, { noiseFreq, 600.0f }, { noiseQ, 2.5f },
                { lfoRate, 5.5f }, { lfoDepth, 0.08f }, { reverbMix, 0.18f } } },
            { "Bowed Glass", "Bowed", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 5.0f }, { l1_a_material, -0.6f }, { l1_a_tone, -1.0f },
                { l1_a_hit, 0.25f }, { l1_a_release, 0.7f }, { l1_a_pitch, 12.0f },
                { l1_b_on, 0.0f },
                { mixMallet, 0.0f }, { mixNoise, 0.48f },
                { envAttack, 0.4f }, { envDecay, 1.0f }, { envSustain, 0.8f },
                { envRelease, 0.8f }, { noiseFilter, 2.0f }, { noiseFreq, 1400.0f }, { noiseQ, 3.0f },
                { reverbMix, 0.3f }, { reverbSize, 0.7f } } },
            { "Singing Bowl", "Bowed", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 6.0f }, { l1_a_material, -0.5f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.35f }, { l1_a_release, 0.8f }, { l1_a_pitch, -5.0f },
                { l1_b_type, 4.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 4.0f },
                { l1_b_pitch, -5.0f }, { l1_b_tone, -3.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.4f },
                { mixMallet, 0.25f }, { mixNoise, 0.5f }, { malletStiffness, 0.3f },
                { envAttack, 0.15f }, { envDecay, 1.2f }, { envSustain, 0.7f },
                { envRelease, 0.9f }, { noiseFilter, 2.0f }, { noiseFreq, 500.0f }, { noiseQ, 2.0f },
                { reverbMix, 0.26f }, { reverbSize, 0.6f } } },

            // --- Keys ---
            { "Tine Piano", "Keys", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.4f }, { l1_a_material, 0.35f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.18f }, { l1_a_release, 0.35f },
                { l1_b_type, 0.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 0.8f },
                { l1_b_tone, -8.0f }, { l1_b_hit, 0.15f },
                { l1_couple, 0.0f }, { l1_balance, 0.22f },
                { malletStiffness, 0.82f }, { malletNoise, 0.06f }, { mixMallet, 0.9f },
                { chorusMix, 0.08f }, { reverbMix, 0.1f } } },
            { "Toy Piano", "Keys", {
                { l1_a_type, 1.0f }, { l1_a_density, 0.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 12.0f }, { l1_a_decay, 0.7f }, { l1_a_material, -0.25f },
                { l1_a_tone, -1.0f }, { l1_a_hit, 0.14f }, { l1_a_release, 0.22f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.9f }, { malletNoise, 0.05f }, { mixMallet, 0.88f },
                { reverbMix, 0.08f } } },

            // --- Pads / textures ---
            { "Noise Pad", "Pads", {
                { l1_a_type, 5.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 3.5f }, { l1_a_material, -0.3f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.4f }, { l1_a_release, 0.7f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 4.0f },
                { l1_couple, 0.0f }, { l1_balance, 0.5f },
                { mixMallet, 0.0f }, { mixNoise, 0.75f },
                { envMode, 0.0f }, { envAttack, 0.35f }, { envDecay, 1.4f },
                { envSustain, 0.7f }, { envRelease, 1.2f },
                { noiseFilter, 3.0f }, { noiseFreq, 900.0f }, { noiseQ, 1.2f },
                { lfoRate, 0.35f }, { lfoDepth, 0.25f },
                { reverbMix, 0.28f }, { chorusMix, 0.2f } } },
            { "Warm Pad", "Pads", {
                { l1_a_type, 5.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 4.0f }, { l1_a_material, -0.2f }, { l1_a_tone, -6.0f },
                { l1_a_hit, 0.45f }, { l1_a_release, 0.9f },
                { l1_b_type, 0.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 3.5f },
                { l1_b_tone, -8.0f }, { l1_couple, 0.0f }, { l1_balance, 0.4f },
                { mixMallet, 0.0f }, { mixNoise, 0.48f },
                { envAttack, 0.5f }, { envDecay, 1.2f }, { envSustain, 0.8f },
                { envRelease, 1.4f }, { noiseFilter, 0.0f }, { noiseFreq, 700.0f },
                { chorusMix, 0.22f }, { reverbMix, 0.3f }, { reverbSize, 0.7f } } },
            { "Airy Texture", "Pads", {
                { l1_a_type, 5.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 3.2f }, { l1_a_material, -0.4f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.5f }, { l1_a_release, 0.8f },
                { l1_b_on, 0.0f },
                { mixMallet, 0.0f }, { mixNoise, 0.7f },
                { envAttack, 0.6f }, { envDecay, 1.5f }, { envSustain, 0.65f },
                { envRelease, 1.3f }, { noiseFilter, 1.0f }, { noiseFreq, 1200.0f },
                { lfoRate, 0.2f }, { lfoDepth, 0.3f },
                { reverbMix, 0.32f }, { reverbSize, 0.8f }, { delayMix, 0.12f }, { delayTime, 0.35f } } },
            { "Shimmer Pad", "Pads", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, 12.0f }, { l1_a_decay, 4.5f }, { l1_a_material, -0.65f },
                { l1_a_tone, -1.0f }, { l1_a_hit, 0.3f }, { l1_a_release, 1.0f },
                { l1_b_on, 0.0f },
                { mixMallet, 0.0f }, { mixNoise, 0.22f }, { malletStiffness, 0.5f },
                { envAttack, 0.45f }, { envDecay, 1.0f }, { envSustain, 0.75f },
                { envRelease, 1.5f }, { noiseFilter, 2.0f }, { noiseFreq, 1800.0f }, { noiseQ, 1.1f },
                { chorusMix, 0.25f }, { reverbMix, 0.34f }, { reverbSize, 0.85f },
                { delayMix, 0.1f }, { delayTime, 0.45f }, { delayFeedback, 0.35f } } },

            // --- FX ---
            { "Deep Impact", "FX", {
                { l1_a_type, 4.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, -17.0f }, { l1_a_decay, 1.4f }, { l1_a_material, -0.6f },
                { l1_a_tone, -8.0f }, { l1_a_hit, 0.9f }, { l1_a_release, 0.6f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, -17.0f },
                { l1_b_decay, 2.0f }, { l1_b_tone, -5.0f }, { l1_b_hit, 0.5f },
                { l1_couple, 1.0f }, { l1_balance, 0.35f },
                { malletStiffness, 0.25f }, { malletNoise, 0.25f }, { mixMallet, 1.0f },
                { distDrive, 0.15f }, { distMix, 0.2f },
                { reverbMix, 0.22f }, { reverbSize, 0.7f } } },
            { "Metal Scrape", "FX", {
                { l1_a_type, 1.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.5f }, { l1_a_material, 0.4f }, { l1_a_tone, 0.5f },
                { l1_a_hit, 0.08f }, { l1_a_release, 0.5f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_decay, 2.0f },
                { l1_b_tone, 0.0f }, { l1_b_hit, 0.1f },
                { l1_couple, 1.0f }, { l1_balance, 0.45f },
                { mixMallet, 0.04f }, { mixNoise, 0.07f },
                { envAttack, 0.12f }, { envDecay, 0.8f }, { envSustain, 0.5f },
                { envRelease, 0.6f }, { noiseFilter, 1.0f }, { noiseFreq, 2500.0f }, { noiseQ, 0.7f },
                { lfoRate, 3.0f }, { lfoDepth, 0.15f } } },
        };
        return patches;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch)
    {
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

        for (const auto& [identifier, value] : patch.values)
            setReal (state, identifier, value);
    }

    juce::File userDirectory()
    {
        return dooblr::preset_paths::userDirectory ("dooblr-chroma");
    }

    juce::Array<juce::File> userPresets()
    {
        return userDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    }

    bool save (juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name)
    {
        auto tree = state.copyState();
        tree.setProperty ("stateVersion", params::stateVersion, nullptr);
        tree.setProperty ("presetName", name, nullptr);
        if (auto xml = tree.createXml())
            return xml->writeTo (file);
        return false;
    }

    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return {};

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid())
            return {};

        state.replaceState (tree);
        return tree.getProperty ("presetName", file.getFileNameWithoutExtension()).toString();
    }
}
