#include "Presets.h"

#include "Parameters.h"

namespace dooblrsub::presets
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
            { "808 Weight", {
                { subAmount, 80.0f }, { loAmount, 35.0f }, { frequency, 48.0f }, { octaveMode, 0.0f },
                { drive, 40.0f }, { mix, 100.0f }, { trackingSpeed, 40.0f } } },
            { "Kick Sub", {
                { subAmount, 70.0f }, { loAmount, 20.0f }, { frequency, 55.0f }, { octaveMode, 0.0f },
                { drive, 25.0f }, { trackingSpeed, 70.0f } } },
            { "Bass Guitar Sub", {
                { subAmount, 55.0f }, { loAmount, 45.0f }, { frequency, 70.0f }, { octaveMode, 2.0f },
                { drive, 30.0f }, { trackingSpeed, 50.0f } } },
            { "Tom Retune Down", {
                { subAmount, 15.0f }, { loAmount, 10.0f }, { shift, -300.0f }, { shiftMix, 85.0f },
                { transient, 90.0f }, { bodyMix, 100.0f }, { frequency, 80.0f } } },
            { "Snare Tone Up", {
                { subAmount, 0.0f }, { loAmount, 0.0f }, { shift, 300.0f }, { shiftMix, 80.0f },
                { transient, 95.0f }, { bodyMix, 90.0f } } },
            { "Club Safe", {
                { subAmount, 60.0f }, { loAmount, 20.0f }, { frequency, 55.0f }, { drive, 15.0f },
                { target, 0.0f }, { monoBelow, 1.0f }, { infraHpf, 1.0f }, { subLimit, 1.0f },
                { infraFreq, 25.0f }, { monoFreq, 100.0f } } },
            { "Festival Safe", {
                { subAmount, 50.0f }, { loAmount, 15.0f }, { frequency, 50.0f }, { drive, 10.0f },
                { target, 1.0f }, { monoBelow, 1.0f }, { infraHpf, 1.0f }, { subLimit, 1.0f },
                { infraFreq, 28.0f }, { monoFreq, 110.0f } } },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-sub/Presets");
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
        juce::XmlElement xml ("DooblrSubPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrSubPreset"))
            return "not a dooblr-sub preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
