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

        /** Key Follow (v0.2.0): the key Keyed correction is following (0..23, C..B major then
            minor), or -1 to use the Key / Scale parameters. The note grid shows this key. */
        void setFollowedKey (int key);
        int getFollowedKey() const noexcept { return followedKey; }

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
        /** Vibrato Respect hint: the latest reading's vibrato weight (0 = none shown). */
        float getShownVibrato() const noexcept { return shownVibrato; }
        /** How often the traces (and the dashed centre) were rebuilt: on new data or a resize,
            never in paint; for tests. */
        int getTraceBuilds() const noexcept { return traceBuilds; }

        static constexpr int minMidi = 36, maxMidi = 84; // C2 .. C6

    private:
        float midiToY (float midi) const noexcept;
        int hitPitchClass (juce::Point<float> p) const;
        void rebuildBackground();
        void rebuildTraces();
        int currentMask() const;
        bool isCustom() const;
        bool isChromatic() const;
        int shownKey() const;
        int shownScale() const;

        ParameterGesture gesture;
        juce::Rectangle<float> keyboard, plot;
        juce::Image background;
        static constexpr int maxColumns = 2048;
        std::array<float, maxColumns> detectedMidi {}, correctedMidi {}, centreMidi {};
        std::array<char, maxColumns> voiced {};
        int write = 0, filled = 0, hoverClass = -1, framesReceived = 0;
        float shownDetected = 0, shownCorrected = 0, shownVibrato = 0, shownVibRate = 0, shownVibCents = 0;
        float pixelScale = 1.0f;
        int lastMask = -1, lastKey = -1, lastScale = -1, lastMode = -1, followedKey = -1;
        juce::String readout;
        juce::Path detected, corrected, centre, centreDashed, detectedOutline, correctedOutline;
        const juce::PathStrokeType detectedStroke { 1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded },
                                   correctedStroke { 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded };
        int traceBuilds = 0;
    };
}
