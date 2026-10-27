#pragma once

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
        Every edit is wrapped in parameter gestures, so it is one undo step and the host sees it.

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

        enum class HitKind { none, node, handle };
        struct Hit { HitKind kind = HitKind::none; int index = -1; };
        Hit hitTestPoint (juce::Point<float> position) const;

        void beginGesture (std::initializer_list<juce::String> ids);
        void endGesture();
        void setParam (const juce::String& id, float plainValue);

        juce::AudioProcessorValueTreeState& state;
        params::Envelope envelope;
        juce::Rectangle<float> plot;
        float viewMs = 600.0f;
        Hit hover, drag;
        float dragStartCurve = 0.0f;
        juce::Point<float> dragStart;
        std::vector<juce::RangedAudioParameter*> gestureParameters;
        juce::uint64 lastHash = 0, cacheHash = 0;

        // Raw parameter values, looked up once (paint/hit tests read them constantly).
        struct Raw
        {
            std::atomic<float>* count = nullptr;
            std::array<std::atomic<float>*, params::maxNodes> time {}, value {}, curve {};
        };
        std::array<Raw, 3> raw; // indexed by params::Envelope

        struct Label { float position; juce::String text; };
        juce::Path curvePath, fillPath, diamond;
        std::vector<float> timeLines, valueLines;
        std::vector<Label> timeLabels, valueLabels;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
