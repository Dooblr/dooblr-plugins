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
        The main display, one plot on one log-frequency axis (labelled along the top): the input
        and output spectrum (faint), the live reduction as a filled area hanging from the 0 dB
        baseline that dips where the plug-in cuts, and on top the weighting curve (a bright line
        with a light fill to 0 dB) with a handle per active band: pills for shelves and cuts, dots
        for bells and band passes. The curve and the reduction share the dB scale at the right
        edge. The curve reads like an EQ: where it dips, more is reduced.

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

        /** The spectrum and the weighting curve share one plot (the same rectangle). */
        juce::Rectangle<float> getSpectrumArea() const noexcept { return spectrumArea; }
        juce::Rectangle<float> getCurveArea() const noexcept { return curveArea; }
        /** The filled region under / over the curve (between the curve and 0 dB). */
        const juce::Path& getCurveFillPath() const noexcept { return curveFillPath; }
        /** Where a band's node is drawn (the node's centre), in local coordinates. */
        juce::Point<float> getNodePosition (int band) const;
        /** The active band whose node is under the point, or -1. */
        int bandAt (juce::Point<float> position) const;
        bool hasSpectrum() const noexcept { return frameSerial >= 0; }
        /** The drawn spectrum / reduction outlines (open paths, left to right), for tests. */
        const juce::Path& getInputPath() const noexcept { return inputPath; }
        const juce::Path& getOutputPath() const noexcept { return outputPath; }
        const juce::Path& getReductionEdge() const noexcept { return reductionEdge; }
        /** The last analysed frame and the reduction as drawn (after the display smoothing), dB. */
        const dsp::DisplayFrame& getFrame() const noexcept { return frame; }
        const std::array<float, dsp::DisplayFrame::numPoints>& getDrawnReduction() const noexcept { return drawnReduction; }
        const std::array<float, dsp::DisplayFrame::numPoints>& getDrawnInput() const noexcept { return drawnInput; }

        /** Light fractional-octave smoothing of display points, wider where analysis bins are sparse:
            a Gaussian of 0.5 x the bin spacing in octaves at that frequency, at most 1/6 octave, and
            none where that is under a third of the point spacing (the highs stay as analysed). */
        static void smoothDisplay (const float* in, float* out, float binHz) noexcept;
        /** Appends a smooth curve through the points (monotone cubic segments, no overshoot). */
        static void addSmoothCurve (juce::Path& path, const float* xs, const float* ys, int n, bool startNewSubPath);

        float xForFrequency (float frequency) const noexcept;
        float frequencyForX (float x) const noexcept;
        float yForCurveDb (float db) const noexcept;
        float curveDbForY (float y) const noexcept;

        /** The shared dB scale: +18 at the top (room for the curve), -24 at the bottom (the deepest
            reduction drawn). Band gains span +-curveRangeDb. */
        static constexpr float spectrumFloorDb = -90.0f, reductionRangeDb = 24.0f, curveRangeDb = 18.0f,
                               scaleTopDb = 18.0f, scaleBottomDb = -24.0f;
        /** Shelves and cuts get a pill handle, bells and band passes a round dot. */
        static bool isPillShape (params::Shape s) noexcept { return s != params::Shape::bell && s != params::Shape::bandPass; }
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
        void rebuildSpectrumPaths();
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
        juce::Path curvePath, curveFillPath, selectedBandPath, inputPath, outputPath, reductionPath, reductionEdge;
        std::array<float, dsp::DisplayFrame::numPoints> pointX {}, drawnInput {}, drawnOutput {}, drawnReduction {},
                                                        inputY {}, outputY {}, reductionY {};

        int selectedBand = 0, hoverBand = -1, dragBand = -1;
        juce::Point<float> hoverPosition { -1.0f, -1.0f }, dragPosition, lastDragMouse;
        bool hovering = false, dragMoved = false;

        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReductionGraph)
    };
}
