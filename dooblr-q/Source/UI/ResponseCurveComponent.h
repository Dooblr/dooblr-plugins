#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Analysis/SpectrumDisplay.h"
#include "../DSP/DynamicEQBand.h"
#include "../Parameters.h"

class DooblrQAudioProcessor;

namespace dooblrq::ui
{
    /**
        Frequency-response display computed from the same filter maths as the DSP.

        Shows the static EQ curve and, where dynamics are active, the live curve
        including each band's current dynamic gain (read lock-free). Each band has
        a handle that sits on that band's own response at its frequency, so the
        curve's peak, dip, shelf midpoint or cut corner passes through the handle.

        Mouse: click a handle to select its band; drag it to change frequency
        (horizontal) and, vertically, gain for bell, shelf and tilt types or Q for the
        gainless cut and notch types (up = higher Q). Dragging a disabled band enables it;
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
        void mouseMove (const juce::MouseEvent& event) override;
        void mouseExit (const juce::MouseEvent& event) override;
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

        static constexpr float displayRangeDb = 24.0f;

        /** Analyzer level range mapped onto the plot height (top, bottom), in dBFS after tilt. */
        static constexpr float spectrumTopDb = 0.0f;
        static constexpr float spectrumBottomDb = -90.0f;

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
            a cut (20*log10(Q)), and 20*log10(Q) for a notch too (whose centre is -inf). */
        static double handleDecibels (const dsp::BandSettings& settings, double sampleRate, bool analog = false);

        /** Q for a cut or notch handle dragged to this level (inverse of the 20*log10(Q) height),
            clamped to the Q range. */
        static double qFromHandleDecibels (double decibels);

        /** Bandwidth in octaves of a Q (shown as "Width") and back; see params::qToOctaves. */
        static double qToOctaves (double q);
        static double octavesToQ (double octaves);

        /** The selected band's bandwidth handles: on the curve at centre / 2^(BW/2) and
            centre * 2^(BW/2). Dragging one sideways sets Q; the other mirrors it in
            log-frequency so the centre stays put. Shown for every type (for shelves, cuts
            and tilt the pair marks the transition width that Q controls). */
        std::pair<juce::Point<float>, juce::Point<float>> widthHandlePositions (int bandIndex) const;
        bool widthHandlesVisible() const;

        /** True when the display draws the analogue (Natural/Linear phase) response. */
        bool showsAnalogResponse() const;

        /** Combined response (dB) of all enabled bands at a frequency, as drawn. */
        double responseDecibelsAt (double frequency, bool includeDynamics) const;

        /** Pulls new audio from the processor's analyzer FIFO into the analyzer (called by the
            timer; public so tests can drive it). Returns the number of new FFT frames. */
        int updateAnalyzer();

        /** Advances the smoothed live spectrum and frozen overlay by dtSeconds (called by the
            timer with the real elapsed time; public so tests can drive it). */
        void updateDisplay (double dtSeconds);

        /** Smoothed display levels (dBFS before tilt) on the display's log-frequency grid. */
        const analysis::SpectrumDisplay& getLiveDisplay() const noexcept { return liveDisplay; }
        const analysis::SpectrumDisplay& getOverlayDisplay() const noexcept { return overlayDisplay; }

        /** Display time constants (seconds). The overlay glides as the long-term average grows. */
        static constexpr double liveRiseSeconds = 0.03, liveFallSeconds = 0.16;
        static constexpr double overlaySeconds = 0.25;

        float spectrumDbToY (double decibels) const;

        /** True if the curve needs redrawing (a parameter changed since the last paint). */
        bool isRepaintPending() const noexcept { return repaintPending.load(); }

    private:
        void timerCallback() override;
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}

        struct ResponseModel
        {
            std::array<dsp::SvfCoefficients, params::numBands> coefficients {};
            std::array<dsp::AnalogPrototype, params::numBands> prototypes {};
            std::array<double, params::numBands> frequencies {};
            std::array<bool, params::numBands> enabled {};
            double sampleRate = 48000.0;
            bool analog = false;

            double decibelsAt (double frequency) const;
        };

        ResponseModel makeResponseModel (bool includeDynamics) const;
        juce::Path makeSpectrumPath (const std::vector<float>& binDb, bool closed) const;
        juce::Path makeDisplayPath (const analysis::SpectrumDisplay& display, bool closed) const;
        juce::Path makeSmoothPath (const std::vector<juce::Point<float>>& points, bool closed) const;
        void drawSpectrum (juce::Graphics& g);

        int bandAt (juce::Point<float> position) const;
        int widthHandleAt (juce::Point<float> position) const; // -1 lower, +1 upper, 0 none
        void updateCursor (juce::Point<float> position);
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
        float dragStartGainDb = 0.0f, dragStartY = 0.0f; // for the relative tilt-amount drag
        int widthDragSide = 0;          // bandwidth handle being dragged (-1 lower, +1 upper)
        bool qDrag = false;             // alt/option-drag on a band handle: vertical = Q
        float dragStartQ = 1.0f;
        const char* dragVerticalSuffix = params::band::gain; // gain, or q for cut/notch handles

        std::atomic<bool> repaintPending { true };

        analysis::SpectrumDisplay liveDisplay, overlayDisplay;
        bool overlayPrimed = false;
        double lastDisplayTime = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveComponent)
    };
}
