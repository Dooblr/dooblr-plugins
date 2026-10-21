#pragma once

#include <atomic>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/DynamicEQBand.h"

class DooblrQAudioProcessor;

namespace dooblrq::ui
{
    /**
        Frequency-response display computed from the same filter maths as the DSP.

        Shows the static EQ curve and, where dynamics are active, the live curve
        including each band's current dynamic gain (read lock-free). Each band has
        a handle that sits on that band's own response at its frequency, so the
        curve's peak, dip, shelf midpoint or cut corner passes through the handle.

        Mouse: click a handle to select its band; drag it to change frequency and,
        for bell and shelf types, gain (dragging a disabled band enables it);
        double-click a handle to toggle its band on or off.
    */
    class ResponseCurveComponent final : public juce::Component,
                                         private juce::AudioProcessorParameter::Listener,
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
        void mouseDoubleClick (const juce::MouseEvent& event) override;

        static constexpr float displayRangeDb = 24.0f;

        //==============================================================================
        // Geometry and response queries (also used by the tests).

        juce::Rectangle<float> plotArea() const;
        float frequencyToX (double frequency) const;
        double xToFrequency (float x) const;
        float decibelsToY (double decibels) const;
        double yToDecibels (float y) const;

        /** Where a band's handle is drawn: x at its frequency, y on its own response there. */
        juce::Point<float> handlePosition (int bandIndex) const;

        /** Level (dB) of a band's own static response at its frequency, i.e. the handle level:
            the gain for a bell, half the gain for a shelf (its midpoint), the corner level for
            a cut, and 0 dB for a notch (whose centre is -inf). */
        static double handleDecibels (const dsp::BandSettings& settings, double sampleRate);

        /** Combined response (dB) of all enabled bands at a frequency, as drawn. */
        double responseDecibelsAt (double frequency, bool includeDynamics) const;

        /** True if the curve needs redrawing (a parameter changed since the last paint). */
        bool isRepaintPending() const noexcept { return repaintPending.load(); }

    private:
        void timerCallback() override;
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}

        int bandAt (juce::Point<float> position) const;
        bool anyDynamicsActive() const;

        void drawGrid (juce::Graphics& g) const;
        juce::Path makeResponsePath (bool includeDynamics) const;

        juce::RangedAudioParameter* bandParameter (int bandIndex, const char* suffix) const;
        void setBandParameter (int bandIndex, const char* suffix, float value);

        DooblrQAudioProcessor& processor;
        int selectedBand = 0;
        int draggingBand = -1;
        bool dragEnabledBand = false;
        juce::Point<float> dragOffset;

        std::atomic<bool> repaintPending { true };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveComponent)
    };
}
