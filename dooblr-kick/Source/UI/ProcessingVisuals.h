#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/MeterTap.h"
#include "OutputVisuals.h"

namespace dooblrkick::ui
{
    /**
        The Drive page's main view (v0.3.0).

        - A type selector (one segment per shaper type).
        - The static transfer curve of the current settings: input -1..1 across, output
          -1..1 up, including drive, output gain, mix and the bitcrush quantiser. A live dot
          rides the curve at the drive input's peak level. Dragging in the plot sets Drive
          (vertical) and Mix (horizontal) in one gesture, and turns the drive on.
        - One cycle of a 0 dBFS sine before (dashed) and after the shaper, and the level of
          the first harmonics of that output.

        Not drawn: the tone low-pass, the DC blocker and the bitcrush sample rate, which are
        frequency-dependent rather than part of the static curve.
    */
    class DriveDisplay final : public juce::Component
    {
    public:
        explicit DriveDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        /** Output of the static curve for the current parameters. */
        float transfer (float x) const;

        // Test hooks.
        juce::Rectangle<float> getTransferArea() const noexcept { return plot; }
        juce::Rectangle<float> getTypeArea (int type) const;
        float getShownInputDb() const noexcept { return shownInDb; }
        int getFramesReceived() const noexcept { return framesReceived; }
        /** Level of harmonic k (1 = fundamental) of the shaped 0 dBFS sine, in dBFS. */
        float getHarmonicDb (int k) const;
        static constexpr int numTypes = 5, numHarmonics = 12;

    private:
        uint64_t paramHash() const;
        void rebuild();

        ParameterGesture gesture;
        juce::Rectangle<float> typeBar, plot, wave, harmonics, readout;
        juce::Path curve, before, after;
        std::array<float, numHarmonics> harmonicDb {};
        uint64_t lastHash = 0;
        float shownInDb = -120.0f;
        int framesReceived = 0;
        float dragDrive = 0, dragMix = 0;
        bool dragging = false;
    };

    /**
        The Envelope page's dry / wet view (v0.3.0): the two equal-gain crossfade lines
        (dry falls, wet rises) with a draggable marker at the current setting, and live
        dry, wet and output meters. Dragging anywhere in the plot sets Dry/Wet in one
        gesture; double-click returns to 100 % wet.
    */
    class MixDisplay final : public juce::Component
    {
    public:
        explicit MixDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        // Test hooks.
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        float getMarkerX() const;
        std::array<float, 3> getShownDb() const noexcept { return shown; } // dry, wet, output
        int getFramesReceived() const noexcept { return framesReceived; }

    private:
        void setFromX (float x);

        ParameterGesture gesture;
        juce::Rectangle<float> plot, meters;
        std::array<float, 3> shown { -120.0f, -120.0f, -120.0f };
        float lastMix = -1.0f;
        int framesReceived = 0;
    };
}
