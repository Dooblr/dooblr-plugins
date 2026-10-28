#include "Presets.h"

#include "Parameters.h"

namespace dooblrprophet::presets
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
            { "Brass Section", {
                { osc2Fine, 8.0f }, { mixOsc2, 0.75f }, { lpfCutoff, 520.0f }, { lpfReso, 0.1f }, { lpfEnvAmt, 0.55f },
                { fenvAttack, 0.07f }, { fenvDecay, 0.8f }, { fenvSustain, 0.45f }, { fenvRelease, 0.3f }, { fenvVelAmt, 0.5f },
                { aenvAttack, 0.025f }, { aenvDecay, 0.6f }, { aenvSustain, 0.9f }, { aenvRelease, 0.3f }, { vintage, 0.5f } } },
            { "Sync Lead", {
                { voiceMode, 2.0f }, { glide, 0.07f }, { osc1Sync, 1.0f }, { osc1Coarse, 7.0f }, { mixOsc1, 1.0f }, { mixOsc2, 0.15f },
                { pmodFiltEnv, 0.4f }, { pmodDestFreq1, 1.0f }, { lpfCutoff, 3200.0f }, { lpfReso, 0.25f }, { lpfEnvAmt, 0.2f },
                { fenvAttack, 0.001f }, { fenvDecay, 1.4f }, { fenvSustain, 0.15f }, { fenvRelease, 0.4f },
                { aenvAttack, 0.002f }, { aenvSustain, 1.0f }, { aenvRelease, 0.25f }, { lfoWheel, 0.3f }, { bendRange, 7.0f } } },
            { "Poly-Mod Bell", {
                { voices, 10.0f }, { osc1Shape, 0.0f }, { osc2Shape, 0.0f }, { osc2Coarse, 19.0f }, { osc2Fine, 0.0f },
                { mixOsc1, 1.0f }, { mixOsc2, 0.0f }, { pmodOsc2, 0.32f }, { pmodFiltEnv, 0.1f }, { pmodDestFreq1, 1.0f },
                { lpfCutoff, 6000.0f }, { lpfReso, 0.0f }, { lpfEnvAmt, 0.15f }, { fenvDecay, 1.6f }, { fenvSustain, 0.0f },
                { aenvAttack, 0.001f }, { aenvDecay, 2.6f }, { aenvSustain, 0.0f }, { aenvRelease, 1.8f }, { aenvVelAmt, 0.6f },
                { vintage, 0.15f } } },
            { "Warm Pad", {
                { voices, 10.0f }, { osc1Shape, 0.78f }, { osc1PW, 0.35f }, { osc2Fine, 9.0f }, { mixOsc2, 0.7f },
                { lfoWave, 0.0f }, { lfoRate, 0.25f }, { lfoInitial, 0.25f }, { lfoDestFreq1, 0.0f }, { lfoDestFreq2, 0.0f },
                { lfoDestShape1, 1.0f }, { lfoDestShape2, 1.0f }, { lpfCutoff, 1400.0f }, { lpfReso, 0.2f }, { lpfEnvAmt, 0.2f },
                { hpfCutoff, 120.0f }, { fenvAttack, 1.2f }, { fenvDecay, 2.0f }, { fenvSustain, 0.6f }, { fenvRelease, 2.0f },
                { aenvAttack, 0.9f }, { aenvDecay, 1.5f }, { aenvSustain, 0.9f }, { aenvRelease, 2.2f }, { vintage, 0.6f } } },
            { "Unison Bass", {
                { voiceMode, 1.0f }, { unisonVoices, 5.0f }, { unisonDetune, 0.3f }, { glide, 0.03f },
                { osc1Coarse, -12.0f }, { osc2Coarse, -12.0f }, { osc2Shape, 1.0f }, { mixOsc2, 0.5f }, { mixSub, 0.6f },
                { lpfCutoff, 330.0f }, { lpfReso, 0.35f }, { lpfDrive, 0.6f }, { lpfEnvAmt, 0.5f }, { lpfKeyTrack, 0.3f },
                { fenvAttack, 0.001f }, { fenvDecay, 0.35f }, { fenvSustain, 0.1f }, { fenvRelease, 0.2f },
                { aenvAttack, 0.001f }, { aenvDecay, 0.6f }, { aenvSustain, 0.8f }, { aenvRelease, 0.15f } } },
            { "Pluck Keys", {
                { voices, 8.0f }, { osc1Shape, 0.62f }, { osc2Shape, 0.3f }, { osc2Coarse, 12.0f }, { mixOsc2, 0.35f },
                { lpfCutoff, 420.0f }, { lpfReso, 0.3f }, { lpfEnvAmt, 0.6f }, { fenvAttack, 0.001f }, { fenvDecay, 0.4f },
                { fenvSustain, 0.0f }, { fenvRelease, 0.4f }, { fenvVelAmt, 0.6f }, { aenvAttack, 0.001f }, { aenvDecay, 1.2f },
                { aenvSustain, 0.2f }, { aenvRelease, 0.5f }, { aenvVelAmt, 0.6f } } },
            { "Sweep Strings", {
                { voices, 10.0f }, { osc2Fine, 7.0f }, { mixOsc2, 0.8f }, { hpfCutoff, 380.0f }, { hpfReso, 0.3f }, { hpfEnvAmt, -0.3f },
                { lpfCutoff, 2400.0f }, { lpfEnvAmt, 0.1f }, { lfoRate, 0.15f }, { lfoInitial, 0.2f }, { lfoDestFreq1, 0.0f },
                { lfoDestFreq2, 0.0f }, { lfoDestLpf, 1.0f }, { fenvAttack, 0.8f }, { fenvDecay, 2.0f }, { fenvSustain, 0.7f },
                { aenvAttack, 0.5f }, { aenvDecay, 1.0f }, { aenvSustain, 0.85f }, { aenvRelease, 1.5f }, { vintage, 0.5f } } },
            { "Resonant S&H", {
                { lfoWave, 4.0f }, { lfoSync, 1.0f }, { lfoDivision, 11.0f }, { lfoInitial, 0.5f }, { lfoDestFreq1, 0.0f },
                { lfoDestFreq2, 0.0f }, { lfoDestLpf, 1.0f }, { lpfCutoff, 900.0f }, { lpfReso, 0.75f }, { lpfEnvAmt, 0.1f },
                { osc2Coarse, -12.0f }, { aenvAttack, 0.001f }, { aenvDecay, 0.3f }, { aenvSustain, 0.7f }, { aenvRelease, 0.4f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-prophet/Presets");
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
        juce::XmlElement xml ("DooblrProphetPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrProphetPreset"))
            return "not a dooblr-prophet preset";

        // Missing parameters (older files) keep their defaults.
        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
