#include "Presets.h"
#include "PresetPaths.h"

#include "Parameters.h"

namespace dooblrl::presets
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
            { "Transparent Master", {
                { style, 0.0f }, { threshold, -2.0f }, { attack, 0.2f }, { release, 80.0f },
                { knee, 4.0f }, { enhance, 0.0f }, { ceiling, -1.0f }, { autoComp, 1.0f } } },
            { "Punchy Drums", {
                { style, 1.0f }, { threshold, -6.0f }, { attack, 4.0f }, { release, 30.0f },
                { enhance, 35.0f }, { ceiling, -0.3f }, { transientLink, 40.0f } } },
            { "Dynamic Mix", {
                { style, 2.0f }, { threshold, -8.0f }, { attack, 1.5f }, { release, 60.0f },
                { enhance, 40.0f }, { lookahead, 4.0f } } },
            { "Allround Bus", {
                { style, 3.0f }, { threshold, -4.0f }, { attack, 0.8f }, { release, 50.0f },
                { enhance, 15.0f }, { releaseLink, 80.0f } } },
            { "Aggressive Loud", {
                { style, 4.0f }, { threshold, -12.0f }, { inputGain, 6.0f }, { attack, 0.05f },
                { release, 20.0f }, { knee, 0.5f }, { enhance, 25.0f }, { ceiling, -0.1f } } },
            { "Modern Stream", {
                { style, 5.0f }, { threshold, -5.0f }, { attack, 0.3f }, { release, 40.0f },
                { ceiling, -1.0f }, { lufsTarget, 3.0f }, { autoComp, 1.0f } } },
            { "Bus Pump", {
                { style, 6.0f }, { threshold, -10.0f }, { attack, 2.0f }, { release, 220.0f },
                { knee, 4.0f }, { releaseLink, 100.0f } } },
            { "Safe Peak", {
                { style, 7.0f }, { threshold, -1.5f }, { attack, 0.1f }, { release, 80.0f },
                { knee, 8.0f }, { enhance, 0.0f }, { ceiling, -1.0f }, { lookahead, 8.0f },
                { autoComp, 1.0f } } },
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
        return dooblr::preset_paths::userDirectory ("dooblr-L");
    }

    juce::Array<juce::File> userPresets()
    {
        return dooblr::preset_paths::listUserPresets ("dooblr-L", juce::String ("*") + fileExtension);
    }

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name)
    {
        juce::XmlElement xml ("DooblrLPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrLPreset"))
            return "not a dooblr-L preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
