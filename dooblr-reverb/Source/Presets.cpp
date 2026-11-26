#include "Presets.h"

#include "Parameters.h"

namespace dooblrreverb::presets
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

        using V = std::vector<std::pair<juce::String, float>>;
        using params::DecayShape;
        using params::EqShape;

        void decay (V& v, int index, DecayShape shape, float freq, float rate, float q = 0.8f)
        {
            v.push_back ({ params::decayActive (index), 1.0f });
            v.push_back ({ params::decayShape (index), (float) (int) shape });
            v.push_back ({ params::decayFreq (index), freq });
            v.push_back ({ params::decayRateId (index), rate });
            v.push_back ({ params::decayQ (index), q });
        }

        void eq (V& v, int index, bool active, EqShape shape, float freq, float gain = 0.0f, float q = 0.71f, int slope = 1)
        {
            v.push_back ({ params::eqActive (index), active ? 1.0f : 0.0f });
            v.push_back ({ params::eqShape (index), (float) (int) shape });
            v.push_back ({ params::eqFreq (index), freq });
            v.push_back ({ params::eqGain (index), gain });
            v.push_back ({ params::eqQ (index), q });
            v.push_back ({ params::eqSlope (index), (float) slope });
        }

        V spaceValues (float space, float rate, float bright, float character, float distance, float width, float predelay = 0.0f)
        {
            using namespace params::id;
            return { { params::id::space, space }, { decayRate, rate }, { brightness, bright }, { params::id::character, character },
                     { params::id::distance, distance }, { params::id::width, width }, { params::id::predelay, predelay } };
        }
    }

    bool isKeptOnPresetLoad (const juce::String& pid)
    {
        return pid == params::id::mix || pid == params::id::input || pid == params::id::output || pid == params::id::bypass;
    }

    const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> all = []
        {
            std::vector<Factory> list;
            list.push_back ({ "Default", {} });

            { auto v = spaceValues (4.0f, 100.0f, 70.0f, 20.0f, 10.0f, 120.0f);
              eq (v, 0, true, EqShape::lowCut, 160.0f);
              list.push_back ({ "Tight Ambience", v }); }
            { auto v = spaceValues (16.0f, 100.0f, 55.0f, 20.0f, 25.0f, 100.0f);
              decay (v, 0, DecayShape::lowShelf, 200.0f, 110.0f, 0.7f);
              list.push_back ({ "Small Room", v }); }
            { auto v = spaceValues (22.0f, 95.0f, 65.0f, 15.0f, 15.0f, 120.0f, 4.0f);
              decay (v, 0, DecayShape::lowShelf, 150.0f, 70.0f, 0.7f);
              eq (v, 0, true, EqShape::lowCut, 90.0f);
              list.push_back ({ "Drum Room", v }); }
            { auto v = spaceValues (27.0f, 100.0f, 60.0f, 30.0f, 35.0f, 100.0f, 15.0f);
              eq (v, 0, true, EqShape::lowCut, 150.0f);
              eq (v, 4, true, EqShape::highShelf, 8000.0f, -3.0f);
              list.push_back ({ "Vocal Room", v }); }
            { auto v = spaceValues (33.0f, 100.0f, 55.0f, 25.0f, 40.0f, 110.0f, 8.0f);
              list.push_back ({ "Studio Live", v }); }
            { auto v = spaceValues (40.0f, 105.0f, 55.0f, 35.0f, 30.0f, 100.0f, 10.0f);
              decay (v, 5, DecayShape::highShelf, 6000.0f, 70.0f, 0.7f);
              list.push_back ({ "Chamber", v }); }
            { auto v = spaceValues (48.0f, 100.0f, 85.0f, 10.0f, 70.0f, 110.0f);
              decay (v, 0, DecayShape::lowShelf, 300.0f, 60.0f, 0.7f);
              eq (v, 0, true, EqShape::lowCut, 200.0f, 0.0f, 0.71f, 1);
              list.push_back ({ "Bright Plate", v }); }
            { auto v = spaceValues (52.0f, 100.0f, 25.0f, 15.0f, 65.0f, 105.0f);
              decay (v, 0, DecayShape::lowShelf, 250.0f, 70.0f, 0.7f);
              list.push_back ({ "Dark Plate", v }); }
            { auto v = spaceValues (42.0f, 90.0f, 80.0f, 10.0f, 60.0f, 110.0f, 10.0f);
              decay (v, 1, DecayShape::bell, 200.0f, 60.0f, 0.9f);
              eq (v, 0, true, EqShape::lowCut, 250.0f);
              list.push_back ({ "Snare Plate", v }); }
            { auto v = spaceValues (62.0f, 100.0f, 55.0f, 40.0f, 45.0f, 110.0f, 30.0f);
              decay (v, 0, DecayShape::lowShelf, 200.0f, 120.0f, 0.7f);
              decay (v, 3, DecayShape::bell, 2500.0f, 140.0f, 1.2f);
              decay (v, 5, DecayShape::highShelf, 7000.0f, 60.0f, 0.7f);
              eq (v, 0, true, EqShape::lowCut, 180.0f);
              eq (v, 5, true, EqShape::highCut, 12000.0f);
              list.push_back ({ "Vocal Hall", v }); }
            { auto v = spaceValues (68.0f, 100.0f, 45.0f, 30.0f, 50.0f, 110.0f, 20.0f);
              decay (v, 0, DecayShape::lowShelf, 250.0f, 140.0f, 0.7f);
              list.push_back ({ "Concert Hall", v }); }
            { auto v = spaceValues (76.0f, 100.0f, 50.0f, 35.0f, 55.0f, 115.0f, 25.0f);
              list.push_back ({ "Large Hall", v }); }
            { auto v = spaceValues (70.0f, 110.0f, 20.0f, 30.0f, 60.0f, 110.0f, 10.0f);
              decay (v, 2, DecayShape::bell, 700.0f, 150.0f, 0.8f);
              list.push_back ({ "Dark Tail", v }); }
            { auto v = spaceValues (92.0f, 100.0f, 40.0f, 30.0f, 70.0f, 120.0f, 35.0f);
              decay (v, 0, DecayShape::lowShelf, 300.0f, 130.0f, 0.7f);
              decay (v, 5, DecayShape::highShelf, 4000.0f, 50.0f, 0.7f);
              list.push_back ({ "Cathedral", v }); }
            { auto v = spaceValues (55.0f, 100.0f, 60.0f, 95.0f, 40.0f, 130.0f, 12.0f);
              list.push_back ({ "Chorus Space", v }); }
            { auto v = spaceValues (100.0f, 200.0f, 45.0f, 70.0f, 80.0f, 130.0f);
              decay (v, 5, DecayShape::highShelf, 5000.0f, 50.0f, 0.7f);
              eq (v, 0, true, EqShape::lowCut, 120.0f, 0.0f, 0.71f, 1);
              list.push_back ({ "Infinite Pad", v }); }
            { auto v = spaceValues (30.0f, 100.0f, 55.0f, 20.0f, 30.0f, 0.0f);
              list.push_back ({ "Mono Room", v }); }
            return list;
        }();

        return all;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch)
    {
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ! isKeptOnPresetLoad (ranged->paramID))
                p->setValueNotifyingHost (p->getDefaultValue());

        for (const auto& [id, value] : patch.values)
            setReal (state, id, value);
    }

    juce::File userDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-reverb/Presets");
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
        juce::XmlElement xml ("DooblrReverbPreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrReverbPreset"))
            return "not a dooblr-reverb preset";

        // Missing parameters (older files) get their defaults; the kept ones stay as they are.
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ! isKeptOnPresetLoad (ranged->paramID))
                p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")); p != nullptr && ! isKeptOnPresetLoad (p->paramID))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        return {};
    }
}
