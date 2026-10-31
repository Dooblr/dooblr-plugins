#include "Presets.h"

#include "Parameters.h"

namespace dooblrsoothe::presets
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

        /** A band's values: active, shape, frequency, gain, Q. */
        void band (V& v, int index, bool active, params::Shape shape = params::Shape::bell, float freq = 1000.0f,
                   float gain = 0.0f, float q = 1.0f)
        {
            v.push_back ({ params::bandActive (index), active ? 1.0f : 0.0f });
            v.push_back ({ params::bandShape (index), (float) (int) shape });
            v.push_back ({ params::bandFreq (index), freq });
            v.push_back ({ params::bandGain (index), gain });
            v.push_back ({ params::bandQ (index), q });
        }
    }

    const std::vector<Factory>& factory()
    {
        using namespace params::id;
        using params::Shape;

        static const std::vector<Factory> all = []
        {
            std::vector<Factory> list;
            list.push_back ({ "Default", {} });

            {
                V v { { depth, 45.0f }, { sharpness, 6.0f }, { selectivity, 4.0f }, { attack, 4.0f }, { release, 60.0f } };
                band (v, 0, true, Shape::lowCut, 150.0f, 0.0f, 1.0f);
                band (v, 1, true, Shape::bell, 3500.0f, -6.0f, 0.9f);
                band (v, 2, true, Shape::highShelf, 9000.0f, 3.0f, 0.7f);
                list.push_back ({ "Vocal Harshness", v });
            }
            {
                V v { { depth, 20.0f }, { sharpness, 3.0f }, { selectivity, 2.0f }, { attack, 10.0f }, { release, 120.0f } };
                band (v, 1, true, Shape::bell, 2500.0f, -2.0f, 0.6f);
                list.push_back ({ "Gentle Mix Bus", v });
            }
            {
                V v { { depth, 40.0f }, { sharpness, 7.0f }, { selectivity, 5.0f }, { attack, 3.0f }, { release, 50.0f } };
                band (v, 0, true, Shape::lowCut, 90.0f, 0.0f, 1.0f);
                band (v, 1, true, Shape::bell, 1200.0f, -4.0f, 0.7f);
                band (v, 2, true, Shape::bell, 4000.0f, -3.0f, 1.0f);
                list.push_back ({ "Acoustic Ring", v });
            }
            {
                V v { { mode, 1.0f }, { depth, 35.0f }, { sharpness, 5.0f }, { attack, 2.0f }, { release, 40.0f } };
                band (v, 0, true, Shape::lowCut, 200.0f, 0.0f, 1.0f);
                band (v, 1, true, Shape::bell, 5000.0f, -4.0f, 0.8f);
                list.push_back ({ "Drum Room Tamer", v });
            }
            {
                V v { { depth, 40.0f }, { sharpness, 5.0f }, { attack, 12.0f }, { release, 150.0f } };
                band (v, 0, true, Shape::bandPass, 120.0f, 0.0f, 0.5f);
                band (v, 1, false);
                band (v, 2, false);
                list.push_back ({ "Low-End Boom", v });
            }
            {
                V v { { depth, 50.0f }, { sharpness, 6.0f }, { attack, 1.0f }, { release, 30.0f } };
                band (v, 0, true, Shape::bandPass, 7000.0f, -3.0f, 0.8f);
                band (v, 1, false);
                band (v, 2, false);
                list.push_back ({ "Sibilance Only", v });
            }
            {
                V v { { stereoMode, 1.0f }, { focus, 100.0f }, { depth, 35.0f } };
                band (v, 2, true, Shape::highShelf, 6000.0f, -3.0f, 0.7f);
                list.push_back ({ "Side Air", v });
            }
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-soothe/Presets");
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
        juce::XmlElement xml ("DooblrSoothePreset");
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
        if (xml == nullptr || ! xml->hasTagName ("DooblrSoothePreset"))
            return "not a dooblr-soothe preset";

        // Missing parameters (older files) keep their defaults. Version-1 files stored band Gain
        // with the opposite sign (see params::stateVersion).
        const auto flipGains = xml->getIntAttribute ("stateVersion", 1) < 2;
        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (auto* child : xml->getChildWithTagNameIterator ("Param"))
            if (auto* p = state.getParameter (child->getStringAttribute ("id")))
            {
                auto value = (float) child->getDoubleAttribute ("value");
                if (flipGains && params::isBandGainId (child->getStringAttribute ("id")))
                    value = -value;
                p->setValueNotifyingHost (p->convertTo0to1 (value));
            }

        return {};
    }
}
