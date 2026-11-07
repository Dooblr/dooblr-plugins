#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/DistressorEngine.h"
#include "../Parameters.h"
#include "GrMeterModel.h"
#include "Theme.h"

namespace dooblrdistressor::ui
{
    /**
        Prominent compression display: a static input/output transfer curve with a live signal
        dot, a vertical GR meter with a decaying peak-hold tick, and a few seconds of scrolling
        GR history. Draggable handles drive Input, Output, Ratio, Attack and Release through
        the APVTS (gestures, so host automation and undo work). No hint text.
    */
    class CompressionDisplay final : public juce::Component
    {
    public:
        enum class Handle { none, knee, slope, attack, release };

        explicit CompressionDisplay (juce::AudioProcessorValueTreeState& state);

        GrMeterModel& model() noexcept { return meter; }
        const GrMeterModel& model() const noexcept { return meter; }

        /** Audio-thread meters, called on VBlank. GR keeps updating while Bypass is on. */
        void pushMeters (float grDb, float inPeakL, float inPeakR, float /*outPeakL*/, float /*outPeakR*/,
                         float matchDb, double nowMs);

        Handle handleAt (juce::Point<float> position) const;
        juce::Point<float> handleCentre (Handle handle) const;
        juce::Rectangle<float> getCurveArea() const noexcept { return curveArea; }
        juce::Rectangle<float> getMeterArea() const noexcept { return meterArea; }
        juce::Rectangle<float> getHistoryArea() const noexcept { return historyArea; }

        static constexpr float handleRadius = 7.0f, handleHit = 12.0f;
        static constexpr float plotMinDb = -24.0f, plotMaxDb = 12.0f;
        static constexpr int curvePoints = 64;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        CompressionMap::Params readParams() const;
        void writeParam (const juce::String& pid, float value);
        void beginGesture (const juce::String& pid);
        void endGesture (const juce::String& pid);
        void endOpenGestures();
        juce::Point<float> toPlot (juce::Rectangle<float> area, float nx, float ny) const noexcept;
        float xToNorm (juce::Rectangle<float> area, float x) const noexcept;
        float yToNorm (juce::Rectangle<float> area, float y) const noexcept;
        float dbToX (float db) const noexcept;
        float dbToYCurve (float db) const noexcept;
        float dbToYGr (juce::Rectangle<float> area, float db) const noexcept;
        void drawHandle (juce::Graphics& g, juce::Point<float> centre, juce::Colour colour, bool hot) const;
        void applyDrag (juce::Point<float> position);

        juce::AudioProcessorValueTreeState& state;
        GrMeterModel meter;
        double lastNowMs = -1.0;
        float inPeak = 0.0f, matchDb = 0.0f;

        juce::Rectangle<float> curveArea, meterArea, historyArea;

        Handle hover = Handle::none, drag = Handle::none;
        bool gestureOpen = false;
        juce::String gestureA, gestureB;
    };
}
