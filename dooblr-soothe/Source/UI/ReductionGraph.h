#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/DepthCurve.h"
#include "../DSP/ResonanceEngine.h"

namespace dooblrsoothe
{
    class DooblrSootheProcessor;
}

namespace dooblrsoothe::ui
{
    /**
        The main display. Top pane: the input spectrum (filled), the output spectrum (line) and
        the live reduction hanging from the top edge (dB of cut, right-hand scale). Bottom pane:
        the depth curve (+-18 dB) with a node per active band.

        Nodes: drag to move frequency and gain (one undo step; Shift for fine moves), mouse
        wheel for Q, double-click to remove, right-click for shape / listen / remove. Double-click
        on an empty spot adds the first unused band there. Hovering shows a readout.
        Opaque; the grid is cached per size and scale; VBlank polls the engine's display frames.
    */
    class ReductionGraph final : public juce::Component
    {
    public:
        explicit ReductionGraph (DooblrSootheProcessor& processor);
        ~ReductionGraph() override;

        std::function<void (int)> onBandSelected;
        void setSelectedBand (int band);
        int getSelectedBand() const noexcept { return selectedBand; }

        /** Polls the engine and the bands (VBlank does this); returns true if a repaint was queued. */
        bool poll();

        juce::Rectangle<float> getSpectrumArea() const noexcept { return spectrumArea; }
        juce::Rectangle<float> getCurveArea() const noexcept { return curveArea; }
        /** Where a band's node is drawn (the node's centre), in local coordinates. */
        juce::Point<float> getNodePosition (int band) const;
        /** The active band whose node is under the point, or -1. */
        int bandAt (juce::Point<float> position) const;
        bool hasSpectrum() const noexcept { return frameSerial >= 0; }

        float xForFrequency (float frequency) const noexcept;
        float frequencyForX (float x) const noexcept;
        float yForCurveDb (float db) const noexcept;
        float curveDbForY (float y) const noexcept;

        static constexpr float spectrumFloorDb = -90.0f, reductionRangeDb = 30.0f, curveRangeDb = 18.0f;
        static constexpr float nodeRadius = 7.0f;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void drawBackground();
        void rebuildCurve();
        void setParam (const juce::String& id, float value);
        void beginGesture (const juce::String& id);
        void endGesture (const juce::String& id);
        void showBandMenu (int band);
        void addBandAt (juce::Point<float> position);
        void removeBand (int band);
        juce::String readoutText() const;

        DooblrSootheProcessor& processor;
        juce::AudioProcessorValueTreeState& state;

        juce::Rectangle<float> spectrumArea, curveArea;
        juce::Image background;
        float backgroundScale = 0.0f;

        dsp::DisplayFrame frame;
        int frameSerial = -1;
        dsp::Bands bands {};
        bool bandsValid = false;
        juce::Path curvePath, selectedBandPath, inputPath, outputPath, reductionPath;

        int selectedBand = 0, hoverBand = -1, dragBand = -1;
        juce::Point<float> hoverPosition { -1.0f, -1.0f }, dragPosition, lastDragMouse;
        bool hovering = false, dragMoved = false;

        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReductionGraph)
    };
}
