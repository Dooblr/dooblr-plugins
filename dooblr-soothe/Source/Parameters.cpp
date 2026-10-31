#include "Parameters.h"

#include <cmath>

namespace dooblrsoothe::params
{
    juce::String formatValue (double value, int decimals)
    {
        decimals = juce::jlimit (0, 6, decimals);
        const auto scale = std::pow (10.0, (double) decimals);
        auto rounded = std::round (value * scale) / scale;
        if (! std::isfinite (rounded)) rounded = 0.0;
        if (std::abs (rounded) < 0.5 / scale) rounded = 0.0; // never "-0"
        if (decimals == 0) return juce::String (juce::roundToInt (rounded));
        return juce::String (rounded, decimals);
    }

    juce::String bandId (int band, const char* field)
    {
        return "b" + juce::String (band + 1) + field;
    }

    bool isBandGainId (const juce::String& parameterId)
    {
        for (int b = 0; b < numBands; ++b)
            if (parameterId == bandGain (b))
                return true;
        return false;
    }

    juce::StringArray shapeNames()
    {
        return { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Band pass" };
    }

    juce::StringArray qualityNames()
    {
        return { "Standard", "High", "Ultra" };
    }

    BandDefault bandDefault (int band)
    {
        // Bands 1-3 are in the curve by default: a gentle low cut on the detection (so bass
        // notes are left alone), a broad presence dip (more reduction there) and a top-end shelf that eases off the air. The rest are
        // free slots, spread out so a newly added band lands somewhere sensible.
        switch (band)
        {
            case 0:  return { true,  Shape::lowCut,    60.0f,    0.0f, 1.0f };
            case 1:  return { true,  Shape::bell,      3000.0f, -3.0f, 0.8f };
            case 2:  return { true,  Shape::highShelf, 9000.0f,  3.0f, 0.7f };
            case 3:  return { false, Shape::bell,      150.0f,   0.0f, 1.0f };
            case 4:  return { false, Shape::bell,      400.0f,   0.0f, 1.0f };
            case 5:  return { false, Shape::bell,      1000.0f,  0.0f, 1.0f };
            case 6:  return { false, Shape::bell,      6000.0f,  0.0f, 1.0f };
            default: return { false, Shape::highCut,   18000.0f, 0.0f, 1.0f };
        }
    }

    namespace
    {
        using Float = juce::AudioParameterFloat;
        using Choice = juce::AudioParameterChoice;
        using Bool = juce::AudioParameterBool;

        juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float interval = 0.0f)
        {
            juce::NormalisableRange<float> r (lo, hi, interval);
            r.setSkewForCentre (centre);
            return r;
        }

        juce::AudioParameterFloatAttributes withUnit (const juce::String& unit, int decimals)
        {
            return juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
                [unit, decimals] (float v, int) { return formatValue (v, decimals) + (unit.isEmpty() ? "" : " " + unit); });
        }

        juce::AudioParameterFloatAttributes frequencyAttributes()
        {
            return juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? formatValue (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : formatValue (v, 0) + " Hz";
            });
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto v = [] (const juce::String& pid) { return juce::ParameterID { pid, 1 }; };

        layout.add (std::make_unique<Choice> (v (id::mode), "Mode", juce::StringArray { "Soft", "Hard" }, 0));
        layout.add (std::make_unique<Float> (v (id::depth), "Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, withUnit ("%", 1)));
        layout.add (std::make_unique<Float> (v (id::sharpness), "Sharpness", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f, withUnit ({}, 2)));
        layout.add (std::make_unique<Float> (v (id::selectivity), "Selectivity", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f, withUnit ({}, 2)));
        layout.add (std::make_unique<Float> (v (id::attack), "Attack", skewed (0.5f, 100.0f, 10.0f, 0.01f), 6.0f, withUnit ("ms", 1)));
        layout.add (std::make_unique<Float> (v (id::release), "Release", skewed (5.0f, 500.0f, 80.0f, 0.1f), 80.0f, withUnit ("ms", 0)));

        layout.add (std::make_unique<Choice> (v (id::stereoMode), "Stereo Mode", juce::StringArray { "L/R", "M/S" }, 0));
        layout.add (std::make_unique<Float> (v (id::link), "Stereo Link", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::focus), "Stereo Focus", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, withUnit ("%", 0)));

        layout.add (std::make_unique<Float> (v (id::maxCut), "Max Cut", juce::NormalisableRange<float> (3.0f, 36.0f, 0.1f), 24.0f, withUnit ("dB", 1)));
        layout.add (std::make_unique<Float> (v (id::wetTrim), "Wet Trim", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, withUnit ("dB", 1)));
        layout.add (std::make_unique<Float> (v (id::mix), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, withUnit ("dB", 1)));

        layout.add (std::make_unique<Bool> (v (id::delta), "Delta", false));
        layout.add (std::make_unique<Bool> (v (id::bypass), "Bypass", false));
        layout.add (std::make_unique<Bool> (v (id::sidechain), "Sidechain", false));
        layout.add (std::make_unique<Choice> (v (id::quality), "Quality", qualityNames(), 1));

        juce::StringArray listenChoices { "Off" };
        for (int b = 0; b < numBands; ++b)
            listenChoices.add ("Band " + juce::String (b + 1));
        layout.add (std::make_unique<Choice> (v (id::listenBand), "Band Listen", listenChoices, 0));

        for (int b = 0; b < numBands; ++b)
        {
            const auto d = bandDefault (b);
            const auto name = "Band " + juce::String (b + 1) + " ";
            layout.add (std::make_unique<Bool> (v (bandActive (b)), name + "Active", d.active));
            layout.add (std::make_unique<Choice> (v (bandShape (b)), name + "Shape", shapeNames(), (int) d.shape));
            layout.add (std::make_unique<Float> (v (bandFreq (b)), name + "Freq", skewed (minFreq, maxFreq, 1000.0f, 0.01f), d.freq, frequencyAttributes()));
            layout.add (std::make_unique<Float> (v (bandGain (b)), name + "Gain", juce::NormalisableRange<float> (minGain, maxGain, 0.01f), d.gain, withUnit ("dB", 1)));
            layout.add (std::make_unique<Float> (v (bandQ (b)), name + "Q", skewed (minQ, maxQ, 1.0f, 0.001f), d.q, withUnit ({}, 2)));
        }

        return layout;
    }
}
