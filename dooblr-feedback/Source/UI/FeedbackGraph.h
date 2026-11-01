#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/FeedbackEngine.h"

namespace dooblrfeedback
{
    class DooblrFeedbackProcessor;
}

namespace dooblrfeedback::ui
{
    /**
        The live view: the mic spectrum (before the notches, faint fill) and the output (line),
        the combined notch curve hanging from a 0 dB baseline (cut scale on the right, a full
        notch on the "-inf" line at the bottom), and one marker per notch: a handle at the notch's
        depth, and either a label chip in the lanes above the baseline (frequency, nearest note,
        cut, Q, hits, time detected, fixed / floating, with a lock and a clear button) or a
        numbered dot just above the baseline.

        Labels are calm by design (0.1.1): each keeps its lane and position until a notch is
        added or removed, a label opens or closes, or its own notch moves out from under it; they
        never relayout because a cut or a level changed. Moves ease over moveMs, labels fade in
        and out over fadeMs, the numbers on them refresh at most every valueRefreshMs, and the
        chips are a fixed width. A label is open while its notch is ringing or deepening and for
        activeHoldMs after, while hovered (here or in the table, with a short linger) or selected;
        otherwise the notch shows as a numbered dot, and a cleared notch's marker fades away.
        Opening labels need a little more room than staying ones (hysteresis), so a crowded
        graph does not flicker between chips and dots. Right-click a chip, dot or handle to clear
        that notch; click to select it (the table follows).
    */
    class FeedbackGraph final : public juce::Component
    {
    public:
        explicit FeedbackGraph (DooblrFeedbackProcessor& processor);
        ~FeedbackGraph() override;

        struct Marker
        {
            int id = 0, number = 0;           // number: 1.. in frequency order (as in the table)
            bool compact = false;             // a numbered dot (label closed, or no room for it)
            int lane = -1;                    // the label lane (0 = top) when open
            juce::Rectangle<float> chip;      // as drawn now: the label chip, or the dot when compact
            juce::Rectangle<float> target;    // where it is heading (equals chip once settled)
            juce::Rectangle<float> lockButton, clearButton;
            juce::Point<float> handle;
        };

        /** Polls the display frame and the notch bank (VBlank does this). */
        bool poll();
        bool advanceDisplay (double dtSeconds);
        void snapDisplay();
        /** Re-reads the notch bank now and lays the markers out. */
        void refreshNotches();

        const std::vector<Marker>& getMarkers() const noexcept { return markers; }
        /** How many times the labels were laid out (adds, removes, labels opening / closing). */
        int getLayoutCount() const noexcept { return layoutCount; }
        /** How many times any label's displayed numbers refreshed. */
        int getValueRefreshCount() const noexcept { return valueRefreshes; }
        /** Labels being drawn, including markers fading out after their notch went. */
        int getLabelCount() const noexcept { return (int) labels.size(); }
        bool isAnimating() const noexcept { return animating; }
        /** The table's hovered row (0 = none): opens that notch's label. */
        void setHoveredNotch (int notchId);
        /** Tests drive time: once set, the graph uses this clock (ms) instead of the real one. */
        void setClockForTesting (double ms) noexcept { testClockMs = ms; }
        const Marker* markerFor (int notchId) const noexcept;
        const dsp::NotchArray& getShownNotches() const noexcept { return notches; }
        bool hasSpectrum() const noexcept { return hasFrame; }
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        float getBaselineY() const noexcept { return baselineY; }
        const juce::Path& getNotchCurve() const noexcept { return curveEdge; }

        int getSelectedNotch() const noexcept { return selectedNotch; }
        void setSelectedNotch (int notchId);
        std::function<void (int)> onNotchSelected;

        float xForFrequency (float f) const noexcept;
        float frequencyForX (float x) const noexcept;
        float yForLevel (float dbfs) const noexcept;
        float yForCut (float cutDb) const noexcept;

        static constexpr float minHz = 20.0f, maxHz = 20000.0f, cutRangeDb = 30.0f, spectrumFloorDb = -100.0f;
        static constexpr float chipWidth = 124.0f, chipHeight = 44.0f, pinRadius = 8.0f;
        static constexpr double moveMs = 250.0, fadeMs = 300.0, activeHoldMs = 4000.0, hoverLingerMs = 700.0, valueRefreshMs = 200.0;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        struct Label
        {
            int id = 0, number = 0, lane = -1;
            bool want = false, open = false, gone = false, placed = false;
            double bornMs = 0, goneMs = 0, lastActiveMs = 0, hoverUntilMs = 0, openChangedMs = -1.0e9, moveStartMs = -1.0e9, valueMs = -1.0e9;
            float lastCut = 0.0f;
            juce::Rectangle<float> from, to, dotFrom, dotTo, drawn, dotDrawn;
            dsp::Notch shown {};           // the numbers on the label (refreshed calmly)
            float labelAlpha = 0.0f, dotAlpha = 0.0f, markerAlpha = 0.0f;
        };

        double clockMs() const noexcept;
        bool updateLabels (double nowMs);
        void layoutMarkers (double nowMs, bool snap);
        bool animateLabels (double nowMs);
        void buildMarkers();
        Label* labelFor (int notchId) noexcept;
        void rebuildSpectrumPaths();
        void rebuildCurve();
        void drawBackground();
        int notchAt (juce::Point<float> position) const;
        const dsp::Notch* notchById (int notchId) const noexcept;
        juce::String readoutText() const;
        static void smoothDisplay (const float* in, float* out, float binHz) noexcept;
        static void addSmoothCurve (juce::Path& path, const float* xs, const float* ys, int n, bool startNewSubPath);

        DooblrFeedbackProcessor& processor;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        dsp::DisplayFrame target, frame;
        bool hasFrame = false, shownValid = false, settling = false;
        double lastTickMs = 0.0;

        dsp::NotchArray notches {};
        std::vector<Marker> markers;
        std::vector<Label> labels;
        int layoutCount = 0, valueRefreshes = 0;
        bool animating = false;
        double testClockMs = -1.0;
        int selectedNotch = 0, hoverNotch = 0, tableHoverNotch = 0;
        juce::Point<float> hoverPoint;
        bool hovering = false;

        juce::Rectangle<float> plot;
        float baselineY = 0.0f;
        juce::Image background;
        float backgroundScale = 0.0f;

        std::array<float, dsp::DisplayFrame::numPoints> drawnInput {}, drawnOutput {}, pointX {}, inputY {}, outputY {};
        juce::Path inputPath, outputPath, curveFill, curveEdge;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackGraph)
    };
}
