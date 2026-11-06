#pragma once

#include "../Engine/Sample.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace dooblrgenerate::ui
{
    /**
        The current sample's waveform (min/max per pixel column, both channels overlaid) with the
        Start point as a vertical line. Dragging it out drags the WAV file (into Ableton, Finder,
        anywhere that accepts files). Draws no text.
    */
    class WaveformView final : public juce::Component
    {
    public:
        void setSample (std::shared_ptr<const engine::Sample>);
        void setStartFraction (float);
        float getStartFraction() const noexcept { return startFraction; }
        bool hasSample() const noexcept { return sample != nullptr; }

        void paint (juce::Graphics&) override;
        void resized() override { rebuild(); }
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

        /** Peak envelope as drawn (for tests): one (min, max) per column. */
        const std::vector<juce::Range<float>>& getColumns() const noexcept { return columns; }

        /** Starts an external drag of the sample's file; false if there is none. */
        bool dragOut();

    private:
        void rebuild();

        std::shared_ptr<const engine::Sample> sample;
        std::vector<juce::Range<float>> columns;
        float startFraction = 0.0f;
        bool dragStarted = false;
    };
}
