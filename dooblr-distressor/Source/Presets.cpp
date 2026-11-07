#include "Presets.h"

#include "Parameters.h"

namespace dooblrdistressor::presets
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
            { "Vocals", {
                { ratio, 4.0f }, { input, 6.5f }, { attack, 5.0f }, { release, 4.0f }, { output, 6.0f },
                { audioHp, 1.0f }, { detHp, 1.0f } } },
            { "Aggressive Vocal", {
                { ratio, 4.0f }, { input, 7.5f }, { attack, 3.0f }, { release, 1.0f }, { output, 6.0f },
                { audioHp, 1.0f }, { distMode, 1.0f } } },
            { "Bass", {
                { ratio, 3.0f }, { input, 6.0f }, { attack, 5.0f }, { release, 4.0f }, { output, 6.0f },
                { distMode, 1.0f } } },
            { "Electric Guitar", {
                { ratio, 4.0f }, { input, 6.0f }, { attack, 2.0f }, { release, 5.0f }, { output, 6.0f },
                { detBand, 1.0f } } },
            { "Acoustic Guitar", {
                { ratio, 4.0f }, { input, 7.0f }, { attack, 2.0f }, { release, 5.0f }, { output, 7.0f },
                { audioHp, 1.0f }, { detHp, 1.0f } } },
            { "Piano", {
                { ratio, 5.0f }, { input, 6.0f }, { attack, 5.0f }, { release, 5.0f }, { output, 6.0f },
                { detHp, 1.0f } } },
            { "Snare", {
                { ratio, 2.0f }, { input, 6.0f }, { attack, 5.0f }, { release, 5.0f }, { output, 6.0f } } },
            { "Kick Opto", {
                { ratio, 5.0f }, { input, 7.0f }, { attack, 10.0f }, { release, 0.0f }, { output, 6.5f },
                { detHp, 1.0f } } },
            { "Room Nuke", {
                { ratio, 7.0f }, { input, 10.0f }, { attack, 6.0f }, { release, 2.5f }, { output, 6.0f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-distressor/Presets");
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
        juce::XmlElement xml ("DooblrDistressorPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrDistressorPreset"))
            return "not a dooblr-distressor preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
