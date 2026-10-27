#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/MeterTap.h"
#include "OutputVisuals.h"

namespace dooblrvocals::ui
{
    /**
        Gate view: scrolling input / output / gain-reduction history and a static
        transfer curve (threshold, range). Drag the threshold line or handle.
    */
    class GateDisplay final : public juce::Component
    {
    public:
        explicit GateDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::Rectangle<float> getTransferArea() const noexcept { return transfer; }
        juce::Rectangle<int> getHistoryArea() const noexcept { return history.getBounds(); }
        float getThresholdLineY() const;
        float getShownInputDb() const noexcept { return shownInDb; }
        float getShownGainReductionDb() const noexcept { return shownGrDb; }
        int getFramesReceived() const noexcept { return framesReceived; }

    private:
        enum class DragKind { none, thresholdLine, thresholdHandle };
        DragKind hitTestDrag (juce::Point<float> p) const;
        float dbToTransferX (float db) const noexcept;
        float dbToTransferY (float db) const noexcept;
        juce::uint64 paramHash() const;
        void rebuildCurve();
        void rebuildBackground();

        ParameterGesture gesture;
        HistoryStrip history;
        juce::Rectangle<float> transfer;
        juce::Image background;
        juce::Path curve;
        juce::uint64 lastHash = 0;
        DragKind hover = DragKind::none, dragging = DragKind::none;
        float shownInDb = -120.0f, shownGrDb = 0.0f, grab = 0.0f;
        int framesReceived = 0;
        juce::String readout;
        static constexpr float minDb = -80.0f, maxDb = 6.0f;
    };
}
