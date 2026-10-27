#pragma once

#include <functional>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/PlayheadTap.h"
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
        - v0.3.4: a drag in empty space locks to an axis once it has moved a few pixels: mostly
          vertical zooms, mostly horizontal pans. A horizontal wheel / trackpad swipe (or
          Shift + wheel) pans, Cmd/Ctrl + wheel or a pinch zooms. The fitted view animates
          (eased, VBlank driven) to 0 .. last point whenever the envelope's extent changes;
          a manual zoom / pan suspends that until the ruler is double-clicked.
        - v0.3.4: a playhead line sweeps across the graph while a hit plays (from the
          processor's lock-free PlayheadTap), with a short trail and a dot on the curve.
        - v0.3.5: the playhead (line, trail, dot, glow) dims with the level it plays.
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

        /** Binds the graph to the Body pitch / amp envelope or the master envelope. */
        void setEnvelope (params::Envelope envelope);
        params::Envelope getEnvelope() const noexcept { return envelope; }

        /** v0.3.5: what the graph draws and edits. points: a multi-point envelope (Body pitch /
            amp, master). ahd: a layer's Attack / Hold / Decay / Curve (Sub, Tail, Noise) drawn
            as 4 points: the 3 times drag, the decay's diamond is the Curve. click: the Click's
            amplitude over its Length (the end point drags the Length). */
        enum class Shape { points, ahd, click };
        /** Binds the graph to a layer's own amplitude envelope (Click, Sub, Tail, Noise; the
            Body uses setEnvelope). */
        void setLayerEnvelope (int layer);
        Shape getShape() const noexcept { return shape; }

        /** v0.3.5: call before switching what the graph shows: the old picture is captured and
            fades out (with a slight slide) over crossfadeMs while the new one is drawn under it. */
        void startCrossfade();
        float getCrossfadeAmount() const noexcept { return crossfadeAmount; } // 1 at the start .. 0 done
        void advanceCrossfade (double nowMs);
        static constexpr double crossfadeMs = 200.0;
        /** The layer whose envelope is shown (0..4), or -1 for the master envelope. */
        int getLayer() const noexcept { return layer; }
        /** The parameters the graph draws / edits; for tests. */
        juce::StringArray getBoundParameterIds() const;
        /** Points and curve diamonds that can be dragged in the current shape. */
        bool isNodeEditable (int node) const;
        bool isHandleEditable (int segment) const;

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

        // Geometry, public for tests.
        int getNumNodes() const;
        juce::Point<float> getNodePosition (int node) const;
        juce::Point<float> getCurveHandlePosition (int segment) const;
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }

        // Time view (v0.3.3).
        static constexpr float minViewMs = 20.0f;
        float getMaxTimeMs() const;
        float getViewStartMs() const noexcept { return viewStartMs; }
        float getViewSpanMs() const noexcept { return viewMs; }
        /** True after a manual zoom or pan (auto-fit is suspended until resetView()). */
        bool isViewZoomed() const noexcept { return userZoom; }

        // v0.3.4: pan, axis lock, animated fit. Static helpers are public for tests.
        enum class DragAxis { undecided, zoom, pan };
        static constexpr float axisLockPixels = 4.0f;
        /** Which way an empty-space drag goes, from its offset since mouse-down. */
        static DragAxis decideDragAxis (juce::Point<float> offset) noexcept;
        /** A view start kept inside 0 .. maxMs - span. */
        static float clampViewStart (float startMs, float spanMs, float maxMs) noexcept;
        /** The fitted span for an envelope whose last point (after Time Scale) is at lastMs. */
        static float fitSpanFor (float lastMs, float maxMs) noexcept;
        /** The fitted span for the current envelope (what resetView() / auto-fit move to). */
        float getFitSpanMs() const;
        /** Pans the view by a time offset (clamped); a manual view change. */
        void panBy (float deltaMs);
        /** Zooms by a factor around a plot x position (clamped); a manual view change. */
        void zoomAround (float x, float spanFactor);
        /** Fit animation length; 0 jumps. The animation only runs while the graph is on screen
            (or when forced for tests), otherwise the view jumps to the fit. */
        static constexpr double fitAnimationMs = 200.0;
        void setAnimateOffscreen (bool shouldAnimate) noexcept { animateOffscreen = shouldAnimate; }
        bool isViewAnimating() const noexcept { return animating; }
        /** Moves the fit animation to the given time (VBlank calls this with the real clock). */
        void advanceViewAnimation (double nowMs);
        /** The view the animation is heading to (start, span). */
        juce::Point<float> getViewTarget() const noexcept { return animating ? juce::Point<float> (toStart, toSpan) : juce::Point<float> (viewStartMs, viewMs); }

        // v0.3.4: playhead.
        static constexpr double playheadFadeMs = 300.0;
        /** Where the playhead reads the live hit; nullptr (default) shows none. */
        void setPlayheadTap (const dsp::PlayheadTap* tap);
        /** Reads the tap and updates the playhead (VBlank calls this with the real clock). */
        void updatePlayhead (double nowMs);
        /** Playhead position in this graph's time (ms; the Body graphs subtract the Body Start). */
        float getPlayheadMs() const noexcept { return playheadMs; }
        /** 1 while the hit sounds, fading to 0 over playheadFadeMs after it ends. */
        float getPlayheadAlpha() const noexcept { return playheadAlpha; }
        /** Graph time for a hit position: samples / rate in ms, minus the Body Start on the Body
            graphs. The master curve is drawn after Time Scale, so real time maps directly. */
        static float playheadTimeFor (std::int64_t samples, double sampleRate, float offsetMs) noexcept;
        /** v0.3.5: the playhead's brightness follows the level it's playing. Gain (linear) to
            alpha, in dB: -60 dB .. 0 dB maps to playheadAlphaFloor .. 1 (monotonic). */
        static constexpr float playheadAlphaFloor = 0.1f, playheadFloorDb = -60.0f;
        static float playheadAlphaForGain (float gain) noexcept;
        /** The level gain that drives the playhead at a graph time: the effective master
            envelope (Time Scale + Depth) on the master graph, 1 while the master is switched
            off; the Body amp envelope on both Body graphs (pitch isn't a level); a layer's own
            AHD / Click amplitude on the other layers' graphs. */
        float getPlayheadLevelGainAt (float ms) const;
        /** playheadAlphaForGain at the playhead (before the end-of-hit fade). */
        float getPlayheadLevelAlpha() const { return playheadAlphaForGain (getPlayheadLevelGainAt (playheadMs)); }
        /** The playhead's x, or nothing while it's hidden or outside the view. */
        std::optional<float> getPlayheadX() const noexcept;
        /** Whether a drag starting here zooms (empty plot space, clear of nodes, handles and the curve). */
        bool isZoomAreaAt (juce::Point<float> position) const;
        /** Back to the automatic fit (what a double-click on the time ruler does). */
        void resetView();
        /** The time ruler strip under the plot. */
        juce::Rectangle<float> getRulerArea() const noexcept { return plot.withY (plot.getBottom()).withHeight (18.0f); }
        /** The master envelope is switched off (drawn dimmed). */
        bool isBypassedLook() const noexcept;
        /** The curve colour: Body pitch orange, Body amp green, master blue, other layers their
            band colour. */
        juce::Colour getCurveColour() const;

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

        /** What VBlank does first: picks up parameter changes (automation, undo, presets) and
            starts the auto-fit when the extent changed. Public so tests can run it. */
        void checkForChanges();

    private:
        struct NodeValues { float time, value, curve; };

        void tick();
        void rebind();
        void refit (bool animate);
        void animateTo (float start, float span);
        void setManualView (float start, float span);
        void keepTimeVisible (float ms);
        float getExtentMs() const;
        void paintPlayhead (juce::Graphics& g) const;
        juce::uint64 computeHash() const noexcept;
        void rebuildCache();
        NodeValues readNode (int node) const;
        NodeValues readNodeOf (params::Envelope e, int node) const;
        float clickLevelAt (float ms) const;
        float ahdParam (int which) const; // 0 attack, 1 hold, 2 decay, 3 curve (current layer)
        /** The level envelope that drives the playhead (see getPlayheadLevelGainAt). */
        dsp::MultiPointEnvelope getPlayheadLevelEnvelope() const;
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
        DragAxis dragAxis = DragAxis::undecided;
        bool animating = false, animateOffscreen = false;
        float fromStart = 0.0f, fromSpan = 600.0f, toStart = 0.0f, toSpan = 600.0f;
        double animStartMs = -1.0; // < 0: starts on the next advance

        const dsp::PlayheadTap* playheadTap = nullptr;
        Shape shape = Shape::points;
        juce::Image crossfadeImage;
        double crossfadeStartMs = -1.0;
        float crossfadeAmount = 0.0f;
        int layer = 1;
        std::array<std::atomic<float>*, 5> layerStart {};
        struct LayerRaw { std::array<std::atomic<float>*, 4> ahd {}; std::atomic<float>* length = nullptr, * type = nullptr, * source = nullptr; float maxMs = 1000.0f; };
        std::array<LayerRaw, 5> layerRaw;
        std::uint32_t playheadHit = 0;
        bool playheadSeen = false, playheadActive = false;
        double playheadEndedAt = 0.0;
        float playheadMs = 0.0f, playheadAlpha = 0.0f;
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
