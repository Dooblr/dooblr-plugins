#include "Presets.h"

#include "DSP/Patch.h"
#include "Parameters.h"

namespace dooblr0coast::presets
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
        using O = dsp::OutJack;
        using I = dsp::InJack;
        static const std::vector<Factory> patches {
            { "Init", {}, {} },
            { "Default Voice", {
                { overtone, 0.12f }, { multiply, 0.18f }, { balance, 0.32f },
                { contourOnset, 0.004f }, { contourSustain, 0.72f }, { contourDecay, 0.28f },
                { dynamics, 0.88f }, { level, 0.72f } }, {} },
            { "Drone", {
                { overtone, 0.2f }, { multiply, 0.35f }, { balance, 0.55f },
                { slopeCycle, 1.0f }, { slopeRise, 0.18f }, { slopeFall, 0.22f },
                { multiplyCv, 0.45f }, { dynamics, 1.0f }, { mathAmt, 0.85f },
                { contourSustain, 0.0f } },
              { { (int) O::Math1, (int) I::DynamicsCv } } },
            { "Simple Bass", {
                { pitch, -1.0f }, { overtone, 0.08f }, { multiply, 0.12f }, { balance, 0.22f },
                { contourOnset, 0.003f }, { contourSustain, 0.85f }, { contourDecay, 0.18f },
                { dynamics, 0.92f }, { level, 0.7f } }, {} },
            { "Flutter Arp", {
                { arpOn, 1.0f }, { clockDiv, 3.0f }, { slopeCycle, 1.0f },
                { slopeRise, 0.04f }, { slopeFall, 0.12f }, { multiplyCv, 0.6f },
                { overtone, 0.35f }, { multiply, 0.4f }, { balance, 0.48f },
                { contourOnset, 0.002f }, { contourSustain, 0.0f }, { contourDecay, 0.12f },
                { dynamics, 0.9f } }, {} },
            { "Percussion", {
                { overtone, 0.45f }, { multiply, 0.55f }, { balance, 0.7f },
                { contourOnset, 0.001f }, { contourSustain, 0.0f }, { contourDecay, 0.16f },
                { contourResponse, 0.85f }, { dynamics, 1.0f }, { level, 0.68f } }, {} },
            { "Wobble", {
                { slopeCycle, 1.0f }, { slopeRise, 0.08f }, { slopeFall, 0.08f },
                { midiBCv, 0.0f }, { overtone, 0.4f }, { multiply, 0.3f }, { balance, 0.5f },
                { contourOnset, 0.01f }, { contourSustain, 0.8f }, { contourDecay, 0.35f },
                { dynamics, 0.9f } },
              { { (int) O::SlopeCv, (int) I::DynamicsCv },
                { (int) O::ContourCv, (int) I::OvertoneCv },
                { (int) O::MidiBCv, (int) I::SlopeTime } } },
            { "Pitched Noise", {
                { slopeCycle, 1.0f }, { slopeRise, 0.012f }, { slopeFall, 0.012f },
                { linFm, 0.55f }, { overtone, 0.15f }, { multiply, 0.2f }, { balance, 0.25f },
                { contourSustain, 0.8f }, { dynamics, 0.85f } },
              { { (int) O::SlopeCv, (int) I::OscLinFm } } },
            { "Arcade", {
                { arpOn, 1.0f }, { clockDiv, 4.0f }, { overtone, 0.55f }, { multiply, 0.62f },
                { balance, 0.6f }, { slopeCycle, 1.0f }, { slopeRise, 0.03f }, { slopeFall, 0.05f },
                { multiplyCv, 0.7f }, { contourOnset, 0.002f }, { contourSustain, 0.0f },
                { contourDecay, 0.08f }, { dynamics, 0.95f } }, {} },
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-0coast/Presets");
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

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name,
               const juce::ValueTree& cables)
    {
        juce::XmlElement xml ("Dooblr0CoastPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("stateVersion", params::stateVersion);

        for (auto* p : state.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* child = xml.createNewChildElement ("Param");
                child->setAttribute ("id", withId->paramID);
                child->setAttribute ("value", (double) withId->convertFrom0to1 (withId->getValue()));
            }

        if (auto cablesXml = cables.createXml())
            xml.addChildElement (cablesXml.release());

        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }

    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file, juce::ValueTree& cables)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("Dooblr0CoastPreset"))
            return "not a dooblr-0coast preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        if (auto* cablesXml = xml->getChildByName ("CABLES"))
            cables = juce::ValueTree::fromXml (*cablesXml);
        else
            cables = juce::ValueTree ("CABLES");

        return {};
    }
}
