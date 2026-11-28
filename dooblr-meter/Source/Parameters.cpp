#include "Parameters.h"

#include "DSP/Profiles.h"
#include "DSP/Targets.h"

namespace dooblrmeter::params
{
    juce::StringArray historySpanNames() { return { "30 s", "1 min", "3 min", "10 min", "Whole" }; }
    juce::StringArray slopeNames() { return { "0 dB/oct", "3 dB/oct", "4.5 dB/oct" }; }
    juce::StringArray averagingNames() { return { "Fast", "Medium", "Slow", "Infinite" }; }
    juce::StringArray sensitivityNames() { return { "Lenient", "Standard", "Strict" }; }
    juce::StringArray modeNames() { return { "Live", "Song" }; }

    float slopeDbPerOctave (int index) noexcept
    {
        return index <= 0 ? 0.0f : (index == 1 ? 3.0f : 4.5f);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using Choice = juce::AudioParameterChoice;
        using Float = juce::AudioParameterFloat;
        using Bool = juce::AudioParameterBool;
        const auto pid = [] (const char* s) { return juce::ParameterID { s, 1 }; };
        const auto choiceAttr = [] { return juce::AudioParameterChoiceAttributes().withAutomatable (false); };
        const auto boolAttr = [] { return juce::AudioParameterBoolAttributes().withAutomatable (false); };

        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<Choice> (pid (id::target), "Target", dsp::targets::names(), 0, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::curve), "Curve", dsp::curves::names(), 0, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::listen), "Listen", dsp::profiles::listenNames(), 0, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::mode), "Mode", modeNames(), 1, choiceAttr()));

        layout.add (std::make_unique<Float> (pid (id::customLufs), "Custom Loudness", juce::NormalisableRange<float> (-30.0f, -5.0f, 0.5f), -14.0f,
                                             juce::AudioParameterFloatAttributes().withAutomatable (false).withLabel ("LUFS")
                                                 .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " LUFS"; })));
        layout.add (std::make_unique<Float> (pid (id::customTp), "Custom Ceiling", juce::NormalisableRange<float> (-3.0f, 0.0f, 0.1f), -1.0f,
                                             juce::AudioParameterFloatAttributes().withAutomatable (false).withLabel ("dBTP")
                                                 .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dBTP"; })));
        layout.add (std::make_unique<Choice> (pid (id::historySpan), "History", historySpanNames(), 4, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::slope), "Spectrum Slope", slopeNames(), 1, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::averaging), "Averaging", averagingNames(), 1, choiceAttr()));
        layout.add (std::make_unique<Choice> (pid (id::sensitivity), "Sensitivity", sensitivityNames(), 1, choiceAttr()));
        layout.add (std::make_unique<Bool> (pid (id::monoAudition), "Mono", false, boolAttr()));
        layout.add (std::make_unique<Bool> (pid (id::listenMatch), "Level Match", true, boolAttr()));
        layout.add (std::make_unique<Bool> (pid (id::freeze), "Freeze", false, boolAttr()));
        return layout;
    }
}
