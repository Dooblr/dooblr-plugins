#include "Presets.h"
#include "Parameters.h"
#include "Engine/Types.h"

namespace dooblrrhythm::presets
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
            { "Son 3-2", {
                { style, 0.0f }, { claveDirection, 0.0f }, { timeSignature, 0.0f },
                { patternLength, 4.0f }, { complexity, 40.0f }, { syncopation, 35.0f }, { section, 1.0f } } },
            { "Rumba", {
                { style, 1.0f }, { claveDirection, 0.0f }, { complexity, 55.0f }, { syncopation, 50.0f } } },
            { "Samba", {
                { style, 3.0f }, { timeSignature, 0.0f }, { complexity, 60.0f }, { swing, 12.0f } } },
            { "Afro-Cuban 6/8", {
                { style, 4.0f }, { timeSignature, 3.0f }, { complexity, 45.0f }, { syncopation, 30.0f } } },
            { "West African", {
                { style, 5.0f }, { timeSignature, 4.0f }, { complexity, 50.0f } } },
            { "Afrobeat", {
                { style, 6.0f }, { complexity, 55.0f }, { syncopation, 45.0f }, { humanize, 30.0f } } },
            { "Afro-house", {
                { style, 7.0f }, { complexity, 35.0f }, { swing, 8.0f }, { humanize, 20.0f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-rhythm/Presets");
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
        juce::XmlElement xml ("DooblrRhythmPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("stateVersion", params::stateVersion);

        for (auto* p : state.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* child = xml.createNewChildElement ("Param");
                child->setAttribute ("id", withId->paramID);
                child->setAttribute ("value", (double) withId->convertFrom0to1 (withId->getValue()));
            }

        if (auto pattern = state.state.getChildWithName ("Pattern"); pattern.isValid())
            xml.addChildElement (pattern.createXml().release());

        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }

    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("DooblrRhythmPreset"))
            return "not a dooblr-rhythm preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        if (auto* patternXml = xml->getChildByName ("Pattern"))
        {
            auto tree = juce::ValueTree::fromXml (*patternXml);
            auto existing = state.state.getChildWithName ("Pattern");
            if (existing.isValid())
                state.state.removeChild (existing, nullptr);
            state.state.appendChild (tree, nullptr);
        }

        return {};
    }
}
