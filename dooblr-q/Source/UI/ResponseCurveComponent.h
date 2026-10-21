#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

class DooblrQAudioProcessor;

namespace dooblrq::ui
{
    /**
        Frequency-response display computed from the same filter maths as the DSP.
        Shows the static EQ curve and, where dynamics are active, the live curve
        including each band's current dynamic gain (read lock-free on a timer).
        Band handles can be clicked to select a band and dragged to change its
        frequency and, for bell and shelf types, its gain.
    */
    class ResponseCurveComponent final : public juce::Component,
                                         private juce::Timer
    {
    public:
        explicit ResponseCurveComponent (DooblrQAudioProcessor& processorToDisplay);
        ~ResponseCurveComponent() override;

        void setSelectedBand (int bandIndex);
        std::function<void (int)> onBandSelected;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;
        void mouseDrag (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;

        static constexpr float displayRangeDb = 24.0f;

    private:
        void timerCallback() override;

        juce::Rectangle<float> plotArea() const;
        float frequencyToX (double frequency) const;
        double xToFrequency (float x) const;
        float decibelsToY (double decibels) const;
        double yToDecibels (float y) const;
        juce::Point<float> handlePosition (int bandIndex) const;
        int bandAt (juce::Point<float> position) const;

        void drawGrid (juce::Graphics& g) const;
        juce::Path makeResponsePath (bool includeDynamics) const;

        void setParameter (int bandIndex, const char* suffix, float value);

        DooblrQAudioProcessor& processor;
        int selectedBand = 0;
        int draggingBand = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveComponent)
    };
}
