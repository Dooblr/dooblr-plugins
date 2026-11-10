#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace dooblrthroat::ui
{
    /** Height / back vowel pad (close at the top, front on the left). Dragging sets both
        parameters and switches the vowel choice to Pad. */
    class VowelPad final : public juce::Component, private juce::Timer
    {
    public:
        VowelPad (juce::AudioProcessorValueTreeState& state,
                  const juce::String& heightId,
                  const juce::String& backId,
                  const juce::String& vowelId);
        ~VowelPad() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr int minSize = 168;

    private:
        void timerCallback() override;
        void applyAt (juce::Point<float> pos, bool gestureStart, bool gestureEnd);
        juce::Point<float> markerPos() const;

        juce::AudioProcessorValueTreeState& state;
        juce::String heightId, backId, vowelId;
        bool dragging = false;
    };
}
