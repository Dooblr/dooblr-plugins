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
            { "Init", {} },
            { "Marimba", {
                { l1_a_type, 2.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.1f }, { l1_a_material, -0.45f }, { l1_a_tone, -4.0f },
                { l1_a_hit, 0.18f }, { l1_a_release, 0.3f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.62f }, { malletNoise, 0.08f }, { mixMallet, 0.9f },
                { mixNoise, 0.0f }, { mixDirect, 0.0f } } },
            { "Kalimba", {
                { l1_a_type, 1.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 0.7f }, { l1_a_material, -0.25f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.14f }, { l1_a_release, 0.25f }, { l1_a_pitch, 12.0f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.78f }, { malletNoise, 0.05f }, { mixMallet, 0.95f } } },
            { "Steel Drum", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.8f }, { l1_a_material, -0.2f }, { l1_a_tone, -1.0f },
                { l1_a_hit, 0.28f }, { l1_a_release, 0.4f },
                { l1_b_type, 3.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, 7.0f },
                { l1_b_decay, 0.9f }, { l1_b_tone, -3.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.42f },
                { malletStiffness, 0.7f }, { mixMallet, 0.88f } } },
            { "Bells", {
                { l1_a_type, 1.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 4.5f }, { l1_a_material, -0.7f }, { l1_a_tone, -6.0f },
                { l1_a_hit, 0.2f }, { l1_a_pitch, 12.0f }, { l1_a_release, 0.5f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.85f }, { malletNoise, 0.04f }, { mixMallet, 0.8f },
                { reverbMix, 0.22f }, { reverbSize, 0.6f } } },
            { "Plucked String", {
                { l1_a_type, 0.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.4f }, { l1_a_material, -0.65f }, { l1_a_tone, -6.0f },
                { l1_a_hit, 0.13f }, { l1_a_release, 0.28f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.9f }, { malletNoise, 0.15f }, { malletColor, 2500.0f },
                { mixMallet, 0.9f }, { mixDirect, 0.05f } } },
            { "Noise Pad", {
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
            { "Open Pipe", {
                { l1_a_type, 6.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 1.6f }, { l1_a_radius, 0.6f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.25f }, { l1_a_release, 0.35f },
                { l1_b_on, 0.0f },
                { malletStiffness, 0.4f }, { mixMallet, 0.55f }, { mixNoise, 0.35f },
                { envAttack, 0.02f }, { envDecay, 0.4f }, { envSustain, 0.15f },
                { envRelease, 0.3f }, { noiseFilter, 0.0f }, { noiseFreq, 1200.0f } } },
            { "Coupled Plates", {
                { l1_a_type, 3.0f }, { l1_a_density, 2.0f }, { l1_a_on, 1.0f },
                { l1_a_decay, 2.2f }, { l1_a_material, -0.35f }, { l1_a_tone, -2.0f },
                { l1_a_hit, 0.22f },
                { l1_b_type, 3.0f }, { l1_b_density, 2.0f }, { l1_b_on, 1.0f },
                { l1_b_decay, 1.6f }, { l1_b_pitch, 5.0f }, { l1_b_tone, -3.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.5f },
                { malletStiffness, 0.6f }, { mixMallet, 0.85f } } },
            { "Bass Drum", {
                { l1_a_type, 4.0f }, { l1_a_density, 1.0f }, { l1_a_on, 1.0f },
                { l1_a_pitch, -12.0f }, { l1_a_decay, 0.55f }, { l1_a_material, -0.5f },
                { l1_a_tone, -8.0f }, { l1_a_hit, 0.85f }, { l1_a_release, 0.4f },
                { l1_b_type, 5.0f }, { l1_b_on, 1.0f }, { l1_b_pitch, -12.0f },
                { l1_b_decay, 0.25f }, { l1_b_tone, -6.0f },
                { l1_couple, 1.0f }, { l1_balance, 0.3f },
                { malletStiffness, 0.35f }, { malletNoise, 0.2f }, { mixMallet, 1.0f } } },
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
