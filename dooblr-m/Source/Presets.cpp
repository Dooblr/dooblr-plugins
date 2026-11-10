#include "Presets.h"

#include "Parameters.h"

namespace dooblrm::presets
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
            { "Classic Bass", {
                { osc1Range, 1.0f }, { osc1Wave, 2.0f },
                { osc2Range, 1.0f }, { osc2Wave, 2.0f }, { osc2Freq, 0.07f },
                { mixOsc1, 0.85f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.4f }, { mixOsc2On, 1.0f },
                { cutoff, 220.0f }, { emphasis, 0.28f }, { contourAmt, 0.55f },
                { fAttack, 0.001f }, { fDecay, 0.32f }, { fSustain, 0.12f },
                { aAttack, 0.001f }, { aDecay, 0.45f }, { aSustain, 0.85f },
                { decaySwitch, 1.0f }, { kybd1, 1.0f }, { volume, 0.75f } } },
            { "Lead", {
                { osc1Range, 2.0f }, { osc1Wave, 2.0f },
                { osc2Range, 2.0f }, { osc2Wave, 3.0f }, { osc2Freq, 0.12f },
                { mixOsc1, 0.8f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.45f }, { mixOsc2On, 1.0f },
                { cutoff, 1800.0f }, { emphasis, 0.22f }, { contourAmt, 0.25f },
                { fAttack, 0.008f }, { fDecay, 0.35f }, { fSustain, 0.4f },
                { aAttack, 0.012f }, { aDecay, 0.4f }, { aSustain, 0.9f },
                { glide, 0.06f }, { legato, 1.0f }, { volume, 0.7f } } },
            { "Brass", {
                { osc1Range, 2.0f }, { osc1Wave, 2.0f },
                { osc2Range, 2.0f }, { osc2Wave, 2.0f }, { osc2Freq, 0.15f },
                { osc3Range, 2.0f }, { osc3Wave, 3.0f }, { osc3Freq, -0.08f },
                { mixOsc1, 0.7f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.55f }, { mixOsc2On, 1.0f },
                { mixOsc3, 0.25f }, { mixOsc3On, 1.0f },
                { cutoff, 900.0f }, { emphasis, 0.18f }, { contourAmt, 0.45f },
                { fAttack, 0.08f }, { fDecay, 0.4f }, { fSustain, 0.55f },
                { aAttack, 0.07f }, { aDecay, 0.5f }, { aSustain, 0.85f },
                { kybd1, 1.0f }, { kybd2, 1.0f }, { volume, 0.68f } } },
            { "Self-osc Filter Whistle", {
                { mixOsc1On, 0.0f }, { mixOsc2On, 0.0f }, { mixOsc3On, 0.0f },
                { mixNoiseOn, 0.0f }, { mixExtOn, 0.0f },
                { cutoff, 880.0f }, { emphasis, 1.0f }, { contourAmt, 0.15f },
                { fAttack, 0.02f }, { fDecay, 0.8f }, { fSustain, 0.7f },
                { aAttack, 0.01f }, { aDecay, 0.6f }, { aSustain, 1.0f },
                { kybd1, 1.0f }, { kybd2, 1.0f }, { volume, 0.55f } } },
            { "Pluck", {
                { osc1Range, 2.0f }, { osc1Wave, 2.0f },
                { osc2Range, 3.0f }, { osc2Wave, 4.0f }, { osc2Freq, 0.2f },
                { mixOsc1, 0.85f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.3f }, { mixOsc2On, 1.0f },
                { cutoff, 2400.0f }, { emphasis, 0.35f }, { contourAmt, 0.7f },
                { fAttack, 0.001f }, { fDecay, 0.14f }, { fSustain, 0.0f },
                { aAttack, 0.001f }, { aDecay, 0.22f }, { aSustain, 0.0f },
                { decaySwitch, 1.0f }, { volume, 0.72f } } },
            { "Pad", {
                { osc1Range, 2.0f }, { osc1Wave, 0.0f },
                { osc2Range, 2.0f }, { osc2Wave, 2.0f }, { osc2Freq, 0.08f },
                { osc3Range, 1.0f }, { osc3Wave, 0.0f }, { osc3Freq, -0.11f },
                { mixOsc1, 0.55f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.45f }, { mixOsc2On, 1.0f },
                { mixOsc3, 0.3f }, { mixOsc3On, 1.0f },
                { mixNoise, 0.04f }, { mixNoiseOn, 1.0f }, { noiseType, 1.0f },
                { cutoff, 1400.0f }, { emphasis, 0.12f }, { contourAmt, 0.2f },
                { fAttack, 0.4f }, { fDecay, 1.2f }, { fSustain, 0.6f },
                { aAttack, 0.35f }, { aDecay, 1.4f }, { aSustain, 0.9f },
                { decaySwitch, 1.0f }, { volume, 0.6f } } },
            { "Kick / Percussion", {
                { osc1Range, 0.0f }, { osc1Wave, 2.0f },
                { osc2Range, 0.0f }, { osc2Wave, 0.0f }, { osc2Freq, -4.0f },
                { mixOsc1, 0.9f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.4f }, { mixOsc2On, 1.0f },
                { mixNoise, 0.15f }, { mixNoiseOn, 1.0f }, { noiseType, 0.0f },
                { cutoff, 180.0f }, { emphasis, 0.2f }, { contourAmt, 0.85f },
                { fAttack, 0.001f }, { fDecay, 0.12f }, { fSustain, 0.0f },
                { aAttack, 0.001f }, { aDecay, 0.22f }, { aSustain, 0.0f },
                { decaySwitch, 1.0f }, { legato, 0.0f }, { volume, 0.8f } } },
            { "Two-Osc Detune", {
                { osc1Range, 2.0f }, { osc1Wave, 2.0f },
                { osc2Range, 2.0f }, { osc2Wave, 2.0f }, { osc2Freq, 0.09f },
                { mixOsc1, 0.75f }, { mixOsc1On, 1.0f },
                { mixOsc2, 0.75f }, { mixOsc2On, 1.0f },
                { mixExt, 0.18f }, { mixExtOn, 1.0f },
                { cutoff, 1600.0f }, { emphasis, 0.2f }, { contourAmt, 0.1f },
                { aAttack, 0.004f }, { aDecay, 0.5f }, { aSustain, 1.0f },
                { volume, 0.65f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-m/Presets");
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
        juce::XmlElement xml ("DooblrMPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrMPreset"))
            return "not a dooblr-m preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
