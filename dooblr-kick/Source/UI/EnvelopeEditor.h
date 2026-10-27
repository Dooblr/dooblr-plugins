#pragma once

#include <functional>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Parameters.h"

namespace dooblrkick::ui
{
    /**
        Draggable multi-point editor for the Body pitch / amplitude envelope or the master envelope.

        - drag a node: time (not the first node) and value
        - drag the small diamond in the middle of a segment: curve tension
        - double-click empty space: add a node (up to 8); double-click / right-click a node: remove it
        - v0.3.3: drag up / down in empty space (not on a node, a curve handle or within a few
          pixels of the curve) to zoom the time axis in / out around the mouse; double-click the
          time ruler under the plot to fit the envelope again. View only: no parameter, no undo.
        Every edit is wrapped in parameter gestures, so it is one undo step and the host sees it.
        The master envelope draws dimmed while it is switched off (still editable).

        Drawing is cached: the curve, its fill and the grid labels are rebuilt only when a node,
        the view range or the size changed, so a repaint is a few fills and no allocation. A
        VBlank-synced check repaints when the parameters changed elsewhere (automation, undo).
    */
    class EnvelopeEditor final : public juce::Component
    {
    public:
        EnvelopeEditor (juce::AudioProcessorValueTreeState& state, params::Envelope envelope);

        void setEnvelope (params::Envelope envelope);
        params::Envelope getEnvelope() const noexcept { return envelope; }

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

        // Geometry, public for tests.
        int getNumNodes() const;
        juce::Point<float> getNodePosition (int node) const;
        juce::Point<float> getCurveHandlePosition (int segment) const;
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }

        // Time view (v0.3.3).
        static constexpr float minViewMs = 20.0f;
        float getMaxTimeMs() const noexcept { return params::maxNodeTimeFor (envelope); }
        float getViewStartMs() const noexcept { return viewStartMs; }
        float getViewSpanMs() const noexcept { return viewMs; }
        bool isViewZoomed() const noexcept { return userZoom; }
        /** Whether a drag starting here zooms (empty plot space, clear of nodes, handles and the curve). */
        bool isZoomAreaAt (juce::Point<float> position) const;
        /** Back to the automatic fit (what a double-click on the time ruler does). */
        void resetView();
        /** The time ruler strip under the plot. */
        juce::Rectangle<float> getRulerArea() const noexcept { return plot.withY (plot.getBottom()).withHeight (18.0f); }
        /** The master envelope is switched off (drawn dimmed). */
        bool isBypassedLook() const noexcept;

        /** v0.3.3: the selected point (the Envelope page's point knobs follow it). Clicking a point
            selects it; clicking a curve diamond selects the point that segment leads to. */
        int getSelectedNode() const noexcept { return juce::jmin (selectedNode, getNumNodes() - 1); }
        void setSelectedNode (int node);
        std::function<void (int)> onSelectedNodeChange;

        /** Master only: true when Time Scale / Depth are not at 100 %, so the graph also draws the
            effective (scaled) envelope as a thin light line over the points'. */
        bool showsEffectiveCurve() const noexcept;
        /** The effective master envelope (points after Time Scale / Depth), as the DSP plays it. */
        dsp::MultiPointEnvelope getEffectiveEnvelope() const;

        /** Adds a node at a plot position / removes one (each one undo step). */
        bool addNodeAt (juce::Point<float> position);
        bool removeNode (int node);

    private:
        struct NodeValues { float time, value, curve; };

        void checkForChanges();
        juce::uint64 computeHash() const noexcept;
        void rebuildCache();
        NodeValues readNode (int node) const;
        juce::RangedAudioParameter* param (const juce::String& id) const;

        float timeToX (float ms) const noexcept;
        float xToTime (float x) const noexcept;
        float valueToY (float value) const noexcept;
        float yToValue (float y) const noexcept;
        void updateViewRange();
        float curveYAt (float ms) const;
        float distanceToCurve (juce::Point<float> position) const;

        enum class HitKind { none, node, handle, line, zoom };
        struct Hit { HitKind kind = HitKind::none; int index = -1; };
        Hit hitTestPoint (juce::Point<float> position) const;

        void beginGesture (std::initializer_list<juce::String> ids);
        void endGesture();
        void setParam (const juce::String& id, float plainValue);

        juce::AudioProcessorValueTreeState& state;
        params::Envelope envelope;
        juce::Rectangle<float> plot;
        float viewMs = 600.0f, viewStartMs = 0.0f; // visible span and its start
        bool userZoom = false;
        float zoomStartSpan = 600.0f, zoomStartStart = 0.0f, zoomAnchorMs = 0.0f, zoomAnchorFraction = 0.0f;
        Hit hover, drag;
        float dragStartCurve = 0.0f;
        juce::Point<float> dragStart;
        std::vector<juce::RangedAudioParameter*> gestureParameters;
        juce::uint64 lastHash = 0, cacheHash = 0;

        // Raw parameter values, looked up once (paint/hit tests read them constantly).
        struct Raw
        {
            std::atomic<float>* count = nullptr;
            std::atomic<float>* on = nullptr; // master only
            std::atomic<float>* timeScale = nullptr, * depth = nullptr; // master only (0.3.3 macros)
            std::array<std::atomic<float>*, params::maxNodes> time {}, value {}, curve {};
        };
        std::array<Raw, 3> raw; // indexed by params::Envelope

        struct Label { float position; juce::String text; };
        juce::Path curvePath, fillPath, diamond, effectivePath;
        int selectedNode = 1;
        std::vector<float> timeLines, valueLines;
        std::vector<Label> timeLabels, valueLabels;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
