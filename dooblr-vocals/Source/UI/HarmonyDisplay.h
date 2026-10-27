#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/VocalSettings.h"

namespace dooblrvocals::ui
{
    /**
        v0.2 Harmony page: one strip per harmony voice (4). Each strip has the voice's on/off,
        interval mode (Scale degrees in the plugin key, or Fixed semitones), the interval knob
        (the Scale or the Fixed one, swapped in place by the mode), level, pan, detune and delay,
        plus a readout of the interval and the pan position. Every control's component ID is its
        parameter ID (harm<N>_<suffix>).
    */
    class HarmonyDisplay final : public juce::Component
    {
    public:
        explicit HarmonyDisplay (juce::AudioProcessorValueTreeState& state);
        ~HarmonyDisplay() override;

        /** Re-reads the parameters: interval knob per mode, dimming of voices that are off, readouts. */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Component* findControl (const juce::String& parameterId) const;

        static constexpr int numVoices = dsp::numHarmonyVoices, minStripHeight = 76, headerWidth = 132, knobCell = 72;

    private:
        class Strip;
        juce::AudioProcessorValueTreeState& state;
        std::array<std::unique_ptr<Strip>, numVoices> strips;
    };
}
