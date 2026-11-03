#include "Presets.h"

#include "Parameters.h"

namespace dooblrbass::presets
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
            { "Rubber Sub", {
                { osc1Wave, 0.0f }, { osc1Octave, 1.0f }, { osc2Wave, 0.0f }, { osc2Octave, 1.0f }, { osc2Tune, 0.07f },
                { mixOsc1, 0.7f }, { mixOsc2, 0.35f }, { mixSub, 0.7f }, { mixDrive, 0.4f },
                { lpfCutoff, 180.0f }, { lpfReso, 0.22f }, { lpfEnvAmt, 0.35f }, { lpfKeyTrack, 0.2f },
                { fenvAttack, 0.001f }, { fenvDecay, 0.35f }, { fenvSustain, 0.12f }, { fenvRelease, 0.22f },
                { aenvAttack, 0.001f }, { aenvDecay, 0.55f }, { aenvSustain, 0.9f }, { aenvRelease, 0.18f } } },
            { "Disco Envelope", {
                { osc1Wave, 0.0f }, { osc1Octave, 1.0f }, { osc2Wave, 1.0f }, { osc2Octave, 2.0f }, { osc2Tune, 0.12f },
                { mixOsc1, 0.8f }, { mixOsc2, 0.4f }, { mixSub, 0.25f }, { mixDrive, 0.2f },
                { lpfCutoff, 90.0f }, { lpfReso, 0.45f }, { lpfEnvAmt, 0.85f }, { lpfKeyTrack, 0.15f },
                { fenvAttack, 0.001f }, { fenvDecay, 0.22f }, { fenvSustain, 0.0f }, { fenvRelease, 0.18f }, { fenvVelAmt, 0.55f },
                { aenvAttack, 0.001f }, { aenvDecay, 0.35f }, { aenvSustain, 0.75f }, { aenvRelease, 0.12f } } },
            { "Dark Square", {
                { osc1Wave, 1.0f }, { osc1Octave, 1.0f }, { osc1PW, 0.38f }, { osc2Wave, 1.0f }, { osc2Octave, 1.0f },
                { osc2Tune, -0.08f }, { osc2PW, 0.62f }, { mixOsc1, 0.75f }, { mixOsc2, 0.45f }, { mixSub, 0.55f },
                { mixDrive, 0.55f }, { lpfCutoff, 260.0f }, { lpfReso, 0.32f }, { lpfEnvAmt, 0.4f }, { lpfKeyTrack, 0.4f },
                { fenvDecay, 0.4f }, { fenvSustain, 0.25f }, { aenvSustain, 1.0f }, { aenvRelease, 0.25f } } },
            { "Growl Fifth", {
                { osc1Wave, 0.0f }, { osc1Octave, 1.0f }, { osc2Wave, 0.0f }, { osc2Octave, 1.0f }, { osc2Tune, 7.0f },
                { mixOsc1, 0.7f }, { mixOsc2, 0.55f }, { mixSub, 0.4f }, { mixDrive, 0.65f }, { mixOverload, 0.18f },
                { lpfCutoff, 320.0f }, { lpfReso, 0.55f }, { lpfEnvAmt, 0.5f }, { lpfKeyTrack, 0.25f },
                { fenvAttack, 0.01f }, { fenvDecay, 0.5f }, { fenvSustain, 0.3f }, { fenvRelease, 0.3f },
                { aenvAttack, 0.008f }, { aenvDecay, 0.4f }, { aenvSustain, 0.85f }, { aenvRelease, 0.22f } } },
            { "Mini Pluck", {
                { osc1Wave, 0.0f }, { osc1Octave, 2.0f }, { osc2Wave, 1.0f }, { osc2Octave, 2.0f }, { osc2Tune, 0.15f },
                { mixOsc1, 0.85f }, { mixOsc2, 0.3f }, { mixSub, 0.15f }, { mixDrive, 0.15f },
                { lpfCutoff, 700.0f }, { lpfReso, 0.38f }, { lpfEnvAmt, 0.7f }, { lpfKeyTrack, 0.5f },
                { fenvAttack, 0.001f }, { fenvDecay, 0.14f }, { fenvSustain, 0.0f }, { fenvRelease, 0.12f }, { fenvVelAmt, 0.7f },
                { aenvAttack, 0.001f }, { aenvDecay, 0.22f }, { aenvSustain, 0.0f }, { aenvRelease, 0.16f },
                { sharedDecay, 1.0f } } },
            { "Acid Slide", {
                { osc1Wave, 1.0f }, { osc1Octave, 1.0f }, { osc1PW, 0.5f }, { osc2Wave, 1.0f }, { osc2Octave, 2.0f },
                { osc2Tune, 0.05f }, { mixOsc1, 0.9f }, { mixOsc2, 0.2f }, { mixSub, 0.2f }, { mixDrive, 0.45f },
                { lpfCutoff, 140.0f }, { lpfReso, 0.78f }, { lpfEnvAmt, 0.72f }, { lpfKeyTrack, 0.1f },
                { fenvAttack, 0.001f }, { fenvDecay, 0.18f }, { fenvSustain, 0.05f }, { fenvRelease, 0.14f },
                { aenvAttack, 0.001f }, { aenvSustain, 0.9f }, { aenvRelease, 0.08f },
                { glide, 0.09f }, { legato, 1.0f } } },
            { "Sub Drone", {
                { osc1Wave, 0.0f }, { osc1Octave, 0.0f }, { osc2Wave, 0.0f }, { osc2Octave, 0.0f }, { osc2Tune, -0.04f },
                { osc3Wave, 0.0f }, { osc3Octave, 1.0f }, { osc3LoFreq, 1.0f }, { osc3KeyTrack, 0.0f },
                { mixOsc1, 0.55f }, { mixOsc2, 0.35f }, { mixOsc3, 0.12f }, { mixSub, 0.8f }, { mixNoise, 0.06f },
                { mixDrive, 0.5f }, { mixOverload, 0.25f },
                { lpfCutoff, 110.0f }, { lpfReso, 0.4f }, { lpfEnvAmt, 0.15f }, { lpfKeyTrack, 0.0f },
                { fenvAttack, 0.2f }, { fenvDecay, 1.2f }, { fenvSustain, 0.5f }, { fenvRelease, 0.8f },
                { aenvAttack, 0.08f }, { aenvDecay, 0.6f }, { aenvSustain, 1.0f }, { aenvRelease, 0.7f } } },
            { "Filter Wobble", {
                { osc1Wave, 0.0f }, { osc1Octave, 1.0f }, { osc2Wave, 0.0f }, { osc2Octave, 1.0f }, { osc2Tune, 0.09f },
                { mixOsc1, 0.8f }, { mixOsc2, 0.5f }, { mixSub, 0.4f }, { mixDrive, 0.35f },
                { lpfCutoff, 380.0f }, { lpfReso, 0.62f }, { lpfEnvAmt, 0.2f }, { lpfKeyTrack, 0.3f },
                { fenvDecay, 0.4f }, { fenvSustain, 0.4f }, { aenvSustain, 1.0f }, { aenvRelease, 0.2f },
                { lfoWave, 0.0f }, { lfoRate, 2.2f }, { lfoToFilter, 0.55f }, { lfoWheel, 0.2f } } },
        };
        return patches;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch)
    {
        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (const auto& [id, value] : patch.values)
            setReal (state, id, value);
    }

    juce::File userDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-bass/Presets");
    }

    juce::Array<juce::File> userPresets()
    {
        auto files = userDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
        });
        return files;
    }

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name)
    {
        juce::XmlElement xml ("DooblrBassPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("stateVersion", params::stateVersion);

        for (auto* p : state.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* child = xml.createNewChildElement ("Param");
                child->setAttribute ("id", withId->paramID);
                child->setAttribute ("value", (double) withId->convertFrom0to1 (withId->getValue()));
            }

        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }

    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("DooblrBassPreset"))
            return "not a dooblr-bass preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
