#include "Presets.h"
#include "Parameters.h"

namespace dooblrthroat::presets
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
            { "Close Front", {
                { vowel, 1.0f }, { height, 0.0f }, { back, 0.0f },
                { breath, 0.08f }, { tension, 0.55f }, { brightness, 0.4f }, { pressure, 8.5f } } },
            { "Open Back", {
                { vowel, 7.0f }, { height, 1.0f }, { back, 0.85f },
                { breath, 0.12f }, { tension, 0.4f }, { brightness, 0.18f }, { pressure, 9.0f } } },
            { "Soft Breath", {
                { vowel, 14.0f }, { breath, 0.55f }, { tension, 0.3f }, { brightness, 0.2f },
                { pressure, 5.5f }, { attack, 0.08f }, { release, 0.25f } } },
            { "Spoken", {
                { vowel, 14.0f }, { breath, 0.2f }, { tension, 0.65f }, { brightness, 0.3f },
                { roughness, 0.18f }, { vibratoDepth, 0.0f }, { glide, 0.03f },
                { attack, 0.015f }, { decay, 0.12f }, { sustain, 0.8f }, { release, 0.08f } } },
            { "Choir Lead", {
                { vowel, 1.0f }, { female, 0.65f }, { breath, 0.12f }, { tension, 0.45f },
                { brightness, 0.35f }, { vibratoDepth, 0.08f }, { vibratoRate, 5.4f },
                { glide, 0.08f }, { attack, 0.05f }, { release, 0.22f }, { output, -4.0f } } },
            { "Low Voice", {
                { vowel, 13.0f }, { female, 0.0f }, { child, 0.0f }, { fry, 0.22f },
                { breath, 0.1f }, { tension, 0.35f }, { brightness, 0.12f }, { pressure, 10.0f },
                { vibratoDepth, 0.03f }, { glide, 0.1f }, { attack, 0.04f }, { release, 0.18f } } },
            { "Whisper", {
                { vowel, 14.0f }, { voiced, 0.0f }, { breath, 0.85f }, { pressure, 6.0f },
                { brightness, 0.15f }, { attack, 0.06f }, { release, 0.2f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-throat/Presets");
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
        juce::XmlElement xml ("DooblrThroatPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrThroatPreset"))
            return "not a dooblr-throat preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
