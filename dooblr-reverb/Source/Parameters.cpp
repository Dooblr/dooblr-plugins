#include "Parameters.h"

#include <cmath>

namespace dooblrreverb::params
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

    juce::String formatFrequency (double hz)
    {
        return hz >= 1000.0 ? formatValue (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : formatValue (hz, 0) + " Hz";
    }

    juce::String formatSeconds (double seconds)
    {
        return seconds < 1.0 ? formatValue (seconds * 1000.0, 0) + " ms" : formatValue (seconds, seconds < 10.0 ? 2 : 1) + " s";
    }

    juce::String decayId (int band, const char* field) { return "d" + juce::String (band + 1) + field; }
    juce::String eqId (int band, const char* field)    { return "e" + juce::String (band + 1) + field; }

    juce::StringArray decayShapeNames() { return { "Bell", "Low shelf", "High shelf", "Notch" }; }
    juce::StringArray eqShapeNames()    { return { "Bell", "Low shelf", "High shelf", "Low cut", "High cut" }; }
    juce::StringArray slopeNames()      { return { "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }

    juce::StringArray divisionNames()
    {
        return { "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
    }

    double divisionBeats (int index) noexcept
    {
        static constexpr double beats[] { 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 3.0 / 8, 1.0 / 3, 1.0 / 2, 3.0 / 4, 2.0 / 3, 1.0, 1.5, 2.0 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }

    double spaceToSeconds (double spacePercent) noexcept
    {
        const auto s = juce::jlimit (0.0, 1.0, spacePercent * 0.01);
        return 0.2 * std::pow (50.0, s);
    }

    DecayDefault decayDefault (int band)
    {
        // A free slot per region of the spectrum; all off, so the decay is flat (100 %) by default.
        switch (band)
        {
            case 0:  return { false, DecayShape::lowShelf,  200.0f,  100.0f, 0.7f };
            case 1:  return { false, DecayShape::bell,      500.0f,  100.0f, 1.0f };
            case 2:  return { false, DecayShape::bell,      1200.0f, 100.0f, 1.0f };
            case 3:  return { false, DecayShape::bell,      3000.0f, 100.0f, 1.0f };
            case 4:  return { false, DecayShape::bell,      6000.0f, 100.0f, 1.0f };
            default: return { false, DecayShape::highShelf, 9000.0f, 100.0f, 0.7f };
        }
    }

    EqDefault eqDefault (int band)
    {
        // A gentle low cut on the reverb is on by default (reverbs rarely want sub energy).
        switch (band)
        {
            case 0:  return { true,  EqShape::lowCut,    100.0f,   0.0f, 0.71f, 0 };
            case 1:  return { false, EqShape::lowShelf,  250.0f,   0.0f, 0.71f, 1 };
            case 2:  return { false, EqShape::bell,      800.0f,   0.0f, 1.0f,  1 };
            case 3:  return { false, EqShape::bell,      2500.0f,  0.0f, 1.0f,  1 };
            case 4:  return { false, EqShape::highShelf, 8000.0f,  0.0f, 0.71f, 1 };
            default: return { false, EqShape::highCut,   14000.0f, 0.0f, 0.71f, 1 };
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

        /** Logarithmic range: equal knob travel per ratio (frequency, decay rate). */
        juce::NormalisableRange<float> logRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                     [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                     [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
        }

        juce::AudioParameterFloatAttributes withUnit (const juce::String& unit, int decimals, bool plusSign = false)
        {
            return juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
                [unit, decimals, plusSign] (float v, int)
                {
                    auto text = formatValue (v, decimals);
                    if (plusSign && text != formatValue (0.0, decimals) && v > 0.0f)
                        text = "+" + text;
                    return text + (unit.isEmpty() ? "" : " " + unit);
                });
        }

        juce::AudioParameterFloatAttributes frequencyAttributes()
        {
            return juce::AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
                .withValueFromStringFunction ([] (const juce::String& text)
                {
                    const auto t = text.trim().toLowerCase();
                    const auto v = t.getDoubleValue();
                    return (float) (t.containsChar ('k') ? v * 1000.0 : v);
                });
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto v = [] (const juce::String& pid) { return juce::ParameterID { pid, 1 }; };

        layout.add (std::make_unique<Float> (v (id::space), "Space", juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 45.0f,
                                             juce::AudioParameterFloatAttributes()
                                                 .withStringFromValueFunction ([] (float x, int) { return formatSeconds (spaceToSeconds (x)); })
                                                 .withValueFromStringFunction ([] (const juce::String& text)
                                                 {
                                                     // The room's decay time: "1.8 s", "800 ms", a bare number is seconds, "40 %" the knob position.
                                                     const auto t = text.trim().toLowerCase();
                                                     const auto number = t.getDoubleValue();
                                                     if (t.endsWithChar ('%'))
                                                         return (float) juce::jlimit (0.0, 100.0, number);
                                                     const auto seconds = t.contains ("ms") ? number * 0.001 : number;
                                                     const auto clamped = juce::jlimit (0.2, 10.0, seconds);
                                                     return (float) (100.0 * std::log (clamped / 0.2) / std::log (50.0));
                                                 })));
        layout.add (std::make_unique<Float> (v (id::decayRate), "Decay Rate", logRange (50.0f, 200.0f), 100.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::brightness), "Brightness", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::character), "Character", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::distance), "Distance", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::width), "Stereo Width", juce::NormalisableRange<float> (0.0f, 150.0f, 0.1f), 100.0f, withUnit ("%", 0)));
        layout.add (std::make_unique<Float> (v (id::mix), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, withUnit ("%", 0)));

        layout.add (std::make_unique<Float> (v (id::predelay), "Predelay", skewed (0.0f, maxPredelayMs, 60.0f, 0.1f), 0.0f, withUnit ("ms", 0)));
        layout.add (std::make_unique<Bool> (v (id::predelaySync), "Predelay Sync", false));
        layout.add (std::make_unique<Choice> (v (id::predelayDiv), "Predelay Division", divisionNames(), 4));
        layout.add (std::make_unique<Float> (v (id::input), "Input", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, withUnit ("dB", 1, true)));
        layout.add (std::make_unique<Float> (v (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, withUnit ("dB", 1, true)));
        layout.add (std::make_unique<Bool> (v (id::bypass), "Bypass", false));

        for (int b = 0; b < numDecayBands; ++b)
        {
            const auto d = decayDefault (b);
            const auto name = "Decay Band " + juce::String (b + 1) + " ";
            layout.add (std::make_unique<Bool> (v (decayActive (b)), name + "Active", d.active));
            layout.add (std::make_unique<Choice> (v (decayShape (b)), name + "Shape", decayShapeNames(), (int) d.shape));
            layout.add (std::make_unique<Float> (v (decayFreq (b)), name + "Freq", logRange (minFreq, maxFreq), d.freq, frequencyAttributes()));
            layout.add (std::make_unique<Float> (v (decayRateId (b)), name + "Rate", logRange (minRate, maxRate), d.rate, withUnit ("%", 0)));
            layout.add (std::make_unique<Float> (v (decayQ (b)), name + "Q", logRange (minQ, maxQ), d.q, withUnit ({}, 2)));
        }

        for (int b = 0; b < numEqBands; ++b)
        {
            const auto d = eqDefault (b);
            const auto name = "EQ Band " + juce::String (b + 1) + " ";
            layout.add (std::make_unique<Bool> (v (eqActive (b)), name + "Active", d.active));
            layout.add (std::make_unique<Choice> (v (eqShape (b)), name + "Shape", eqShapeNames(), (int) d.shape));
            layout.add (std::make_unique<Float> (v (eqFreq (b)), name + "Freq", logRange (minFreq, maxFreq), d.freq, frequencyAttributes()));
            layout.add (std::make_unique<Float> (v (eqGain (b)), name + "Gain", juce::NormalisableRange<float> (minGain, maxGain, 0.01f), d.gain, withUnit ("dB", 1, true)));
            layout.add (std::make_unique<Float> (v (eqQ (b)), name + "Q", logRange (minQ, maxQ), d.q, withUnit ({}, 2)));
            layout.add (std::make_unique<Choice> (v (eqSlope (b)), name + "Slope", slopeNames(), d.slope));
        }

        return layout;
    }
}
