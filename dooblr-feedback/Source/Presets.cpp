#include "Presets.h"

#include "Parameters.h"

namespace dooblrfeedback::presets
{
    namespace
    {
        constexpr const char* rootTag = "DooblrFeedbackPreset";

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

        static const std::vector<Factory> all = []
        {
            std::vector<Factory> list;
            list.push_back ({ "Default", {} });
            list.push_back ({ "Speech / Lectern", { { mode, 1.0f }, { sensitivity, 60.0f }, { notchQ, 16.0f }, { rangeLow, 100.0f } } });
            list.push_back ({ "Vocal in a Band", { { mode, 0.0f }, { sensitivity, 45.0f }, { notchQ, 24.0f }, { maxCut, 12.0f } } });
            list.push_back ({ "Monitor Wedge", { { mode, 1.0f }, { sensitivity, 65.0f }, { maxCut, 24.0f }, { liveNotches, 1.0f } } });
            list.push_back ({ "Theatre Lav", { { mode, 1.0f }, { sensitivity, 55.0f }, { rangeLow, 150.0f }, { rangeHigh, 12000.0f }, { release, 30.0f } } });
            list.push_back ({ "Ring-out Start", { { ringOut, 1.0f }, { mode, 1.0f }, { sensitivity, 70.0f }, { maxCut, 24.0f } } });
            return list;
        }();
        return all;
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-feedback/Presets");
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

    juce::ValueTree notchesToTree (const dsp::NotchArray& notches)
    {
        juce::ValueTree tree ("NOTCHES");
        for (const auto& n : notches)
        {
            if (! n.active) continue;
            juce::ValueTree child ("NOTCH");
            child.setProperty ("freq", (double) n.freq, nullptr);
            child.setProperty ("cut", (double) n.cutDb, nullptr);
            child.setProperty ("needed", (double) n.neededDb, nullptr);
            child.setProperty ("q", (double) n.q, nullptr);
            child.setProperty ("fixed", n.fixed, nullptr);
            child.setProperty ("hits", n.hits, nullptr);
            child.setProperty ("detected", juce::String (n.detectedMs), nullptr);
            tree.appendChild (child, nullptr);
        }
        return tree;
    }

    dsp::NotchArray notchesFromTree (const juce::ValueTree& tree)
    {
        dsp::NotchArray notches {};
        size_t i = 0;
        for (const auto child : tree)
        {
            if (i >= notches.size()) break;
            if (! child.hasType ("NOTCH")) continue;
            auto& n = notches[i++];
            n.active = true;
            n.freq = (float) (double) child.getProperty ("freq", 1000.0);
            n.cutDb = (float) (double) child.getProperty ("cut", 0.0);
            n.neededDb = (float) (double) child.getProperty ("needed", (double) n.cutDb);
            n.q = (float) (double) child.getProperty ("q", 20.0);
            n.fixed = (bool) child.getProperty ("fixed", false);
            n.hits = (int) child.getProperty ("hits", 1);
            n.detectedMs = child.getProperty ("detected").toString().getLargeIntValue();
            n.detectedSample = -1;
        }
        return notches;
    }

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::String& notes, const dsp::NotchArray& notches,
               const juce::File& file, const juce::String& name)
    {
        juce::XmlElement xml (rootTag);
        xml.setAttribute ("name", name);
        xml.setAttribute ("stateVersion", params::stateVersion);

        for (auto* p : state.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* child = xml.createNewChildElement ("Param");
                child->setAttribute ("id", withId->paramID);
                child->setAttribute ("value", (double) withId->convertFrom0to1 (withId->getValue()));
            }

        xml.createNewChildElement ("Notes")->addTextElement (notes);
        if (auto notchXml = notchesToTree (notches).createXml())
            xml.addChildElement (notchXml.release());

        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }

    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file, juce::String& notes, dsp::NotchArray& notches)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName (rootTag))
            return "not a dooblr-feedback preset";

        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        notes = {};
        if (auto* n = xml->getChildByName ("Notes"))
            notes = n->getAllSubText();

        notches = {};
        if (auto* n = xml->getChildByName ("NOTCHES"))
            notches = notchesFromTree (juce::ValueTree::fromXml (*n));
        return {};
    }

    juce::File rename (const juce::File& file, const juce::String& newName, juce::String& error)
    {
        const auto clean = juce::File::createLegalFileName (newName.trim());
        if (clean.isEmpty()) { error = "empty name"; return {}; }
        const auto target = file.getSiblingFile (clean + fileExtension);
        if (target == file) return file;
        if (target.exists()) { error = "a preset called \"" + clean + "\" already exists"; return {}; }

        auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName (rootTag)) { error = "not a dooblr-feedback preset"; return {}; }
        xml->setAttribute ("name", clean);
        if (! xml->writeTo (target)) { error = "could not write " + target.getFullPathName(); return {}; }
        // The renamed copy is written and verified before the old name goes.
        if (juce::parseXML (target) == nullptr) { error = "could not verify " + target.getFullPathName(); return {}; }
        file.deleteFile();
        return target;
    }
}
