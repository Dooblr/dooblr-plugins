#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/SystemCheck.h"

namespace dooblrsub::ui
{
    class VerdictPanel final : public juce::Component
    {
    public:
        void setState (dsp::Verdict verdict, const dsp::ReasonList& reasons,
                       float trackedHz, int latencySamples, double sampleRate);

        void paint (juce::Graphics&) override;

    private:
        dsp::Verdict verdict = dsp::Verdict::works;
        dsp::ReasonList reasons;
        float trackedHz = 0.0f;
        int latency = 0;
        double sampleRate = 48000.0;
    };
}
