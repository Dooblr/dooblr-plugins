#pragma once

#include <array>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/SystemCheck.h"

namespace dooblrsub::ui
{
    /** Spectrum (20-200 Hz) and five sub-band meters. No hint text. */
    class SubDisplay final : public juce::Component
    {
    public:
        void setFrame (const std::array<float, dsp::SystemCheck::spectrumBins>& spectrum,
                       const std::array<float, dsp::numSubBands>& bands,
                       float trackedHz);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void rebuildBackground();

        std::array<float, dsp::SystemCheck::spectrumBins> spectrum {};
        std::array<float, dsp::numSubBands> bands {};
        float trackedHz = 0.0f;
        juce::Image background;
        int lastW = 0, lastH = 0, lastScale = 0;
    };
}
