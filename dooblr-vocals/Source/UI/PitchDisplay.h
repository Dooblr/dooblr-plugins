#pragma once

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/PitchTap.h"
#include "OutputVisuals.h"

namespace dooblrvocals::ui
{
    /**
        Detected vs corrected pitch over time, against a note grid and a keyboard.
        Click a key in Custom scale mode to toggle that pitch class (one undo step).
        No captions on the graph: the title lives in the editor chrome.
    */
    class PitchDisplay final : public juce::Component
    {
    public:
        explicit PitchDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::PitchFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        juce::Rectangle<float> getKeyboardArea() const noexcept { return keyboard; }
        float getShownDetectedHz() const noexcept { return shownDetected; }
        float getShownCorrectedHz() const noexcept { return shownCorrected; }
        int getFramesReceived() const noexcept { return framesReceived; }

        static constexpr int minMidi = 36, maxMidi = 84; // C2 .. C6

    private:
        float midiToY (float midi) const noexcept;
        int hitPitchClass (juce::Point<float> p) const;
        void rebuildBackground();
        int currentMask() const;
        bool isCustom() const;
        bool isChromatic() const;

        ParameterGesture gesture;
        juce::Rectangle<float> keyboard, plot;
        juce::Image background;
        static constexpr int maxColumns = 2048;
        std::array<float, maxColumns> detectedMidi {}, correctedMidi {};
        std::array<char, maxColumns> voiced {};
        int write = 0, filled = 0, hoverClass = -1, framesReceived = 0;
        float shownDetected = 0, shownCorrected = 0;
        float pixelScale = 1.0f;
        int lastMask = -1, lastKey = -1, lastScale = -1, lastMode = -1;
        juce::String readout;
    };
}
