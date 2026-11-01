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
        the combined notch curve hanging from a 0 dB baseline (cut scale on the right), and one
        marker per notch: a chip in the label lane above the baseline (frequency, nearest note,
        cut, Q, hits, time detected, fixed / floating) with a lock and a clear button, a line down
        to a handle at the notch's depth. Chips that do not fit become numbered pins. Right-click a
        chip, pin or handle to clear that notch; click to select it (the table follows).
    */
    class FeedbackGraph final : public juce::Component
    {
    public:
        explicit FeedbackGraph (DooblrFeedbackProcessor& processor);
        ~FeedbackGraph() override;

        struct Marker
        {
            int id = 0, number = 0;           // number: 1.. in frequency order (as in the table)
            bool compact = false;             // a pin (no room for a chip)
            juce::Rectangle<float> chip, lockButton, clearButton;
            juce::Point<float> handle;
        };

        /** Polls the display frame and the notch bank (VBlank does this). */
        bool poll();
        bool advanceDisplay (double dtSeconds);
        void snapDisplay();
        /** Re-reads the notch bank now and lays the markers out. */
        void refreshNotches();

        const std::vector<Marker>& getMarkers() const noexcept { return markers; }
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

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void layoutMarkers();
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
        int selectedNotch = 0, hoverNotch = 0;
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
