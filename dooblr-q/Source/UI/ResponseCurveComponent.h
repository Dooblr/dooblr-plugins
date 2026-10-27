#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
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

        Shows the combined static EQ curve. Each band whose dynamics are currently moving
        its gain (read lock-free) also gets its own coloured region, in the band's colour:
        the area between the curve without and with that band's dynamic gain, drawn only
        over the frequencies where that band's dynamic change exceeds
        dynamicRegionThresholdDb, plus the live curve over that region. Each band has
        a handle that sits on that band's own response at its frequency, so the
        curve's peak, dip, shelf midpoint or cut corner passes through the handle.

        Mouse: click a handle to select its band; drag it to change frequency
        (horizontal) and, vertically, gain for bell, shelf and tilt types or Q for the
        gainless cut and notch types (up = higher Q). Dragging a disabled band enables it;
        double-click a handle to toggle its band on or off.

        0.9.2 hover ring: moving the pointer within hoverProximity of an enabled band's handle
        fades in (100 ms ease-out, display-refresh driven) a ring around the nearest such
        handle; it fades out (150 ms) when the pointer leaves, unless the ring is being dragged.
        The ring edits the band's dynamic Range in "upward amount" (see
        dsp::DynamicEQBand::upwardSign): counter-clockwise = downward compression, clockwise =
        upward expansion, +/-24 dB at +/-135 degrees from 12 o'clock; vertical drags (up =
        upward) work too, with the panel ring's direction lock. Moving it off 0 switches the
        band's Dynamic on in the same undo step; double-click resets to 0. The ring annulus
        starts outside the handle's own hit radius, and handles (any band) win over rings.
        A blue S badge on the ring's top-right edge toggles the band's Solo (alt = exclusive);
        it stays visible while the band is soloed.
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

        //==============================================================================
        // 0.9.2: hover ring and blue S (public for the tests).
        static constexpr float hoverProximity = 40.0f;      // px from a handle that shows its ring
        static constexpr float handleHitRadius = 14.4f;     // the handle's own click radius
        static constexpr float hoverRingRadius = 25.0f;     // centre line of the ring stroke
        static constexpr float hoverRingWidth = 5.0f;
        static constexpr float hoverRingInnerHit = 15.0f;   // annulus that takes clicks
        static constexpr float hoverRingOuterHit = 34.0f;
        static constexpr float soloBadgeSize = 19.0f;       // the blue S (square, px)
        static constexpr float soloBadgeDistance = 35.0f;   // from the handle centre, at 45 degrees
        static constexpr double hoverFadeInSeconds = 0.1, hoverFadeOutSeconds = 0.15;
        static constexpr double hoverSweepRadians = 0.75 * juce::MathConstants<double>::pi; // +/-24 dB at +/-135 degrees

        enum class Hit { none, widthHandle, handle, soloBadge, rangeRing };

        /** What a click at this position would grab, and for which band. */
        Hit hitAt (juce::Point<float> position, int* bandOut = nullptr) const;

        /** The band whose ring annulus is under this position, ignoring any S (-1 none). */
        int ringBandAt (juce::Point<float> position) const;

        /** Max pointer travel (px) for a press on the S to count as a click. */
        static constexpr float soloClickSlop = 3.0f;

        /** The band whose ring the pointer at this position would show (-1 none). */
        int hoverBandAt (juce::Point<float> position) const;

        /** Updates the hover target from a pointer position (what mouseMove does). */
        void updateHover (juce::Point<float> position);
        /** The pointer left the display: fade out unless a ring drag is in progress. */
        void clearHover();

        int getHoverBand() const noexcept { return hoverBand; }
        float getHoverAlpha (int bandIndex) const noexcept;
        bool isRingDragging() const noexcept { return ringBand >= 0; }

        /** 0.9.4: the band whose S is being dragged (auditioned and moved), -1 none. */
        int getSoloDragBand() const noexcept { return soloDragBand; }

        /** Advances the fades (called from the display's vblank; public so tests can drive it). */
        void advanceHoverAnimation (double dtSeconds);

        /** A band can show the ring when it is on and its type has dynamics (not Notch). */
        bool hoverRingAvailable (int bandIndex) const;
        /** The blue S shows while hovered (enabled bands) or while the band is soloed. */
        bool soloBadgeVisible (int bandIndex) const;
        juce::Rectangle<float> soloBadgeBounds (int bandIndex) const;

        /** Ring angle (radians clockwise from 12 o'clock) for an upward amount (dB) and back. */
        static double angleForUpwardAmount (double upwardDb) noexcept;

        /** Called when the blue S is clicked: (band, exclusive). The editor routes this to its
            Solo toggle, so the graph and the S buttons share one behaviour. */
        std::function<void (int, bool)> onSoloClicked;

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

        /** Right-click menu on a handle: the band's slope choices (current one ticked). For
            a bell or notch the choices are shown greyed with the reason, since slope does
            not apply to them. Item IDs are slope index + 1. */
        juce::PopupMenu createSlopeMenu (int bandIndex) const;

        /** Sets a band's slope as one undoable gesture (what the slope menu does). */
        void setBandSlope (int bandIndex, dsp::Slope slope);

        /** 0.8.0: the menu also lists the models (IDs modelMenuBase + model) and Stepped (steppedMenuId). */
        static constexpr int modelMenuBase = 100, steppedMenuId = 200;

        /** Applies a band menu choice (slope, model or Stepped) as one undoable gesture. */
        void handleBandMenuResult (int bandIndex, int result);

        /** True when the display draws the analogue (Natural/Linear phase) response. */
        bool showsAnalogResponse() const;

        void setBandChoice (int bandIndex, const char* suffix, float value);

        /** Combined response (dB) of all enabled bands at a frequency, as drawn. */
        double responseDecibelsAt (double frequency, bool includeDynamics) const;

        /** A band's dynamic region is drawn where its own dynamic change exceeds this. */
        static constexpr double dynamicRegionThresholdDb = 0.1;

        /** One band's own response change (dB) at a frequency from its current dynamic gain:
            its response with the dynamic gain minus without it. 0 when the band is off or its
            dynamics are off. */
        double bandDynamicDeltaDecibelsAt (int bandIndex, double frequency) const;

        /** The frequency spans over which a band's dynamic region is drawn (|delta| above the
            threshold, sampled per 2 px as painted). Empty when its dynamics are idle. */
        std::vector<juce::Range<double>> dynamicRegions (int bandIndex) const;

        /** 0.9.0: a Low / High Cut band with dynamics shows its static cutoff (dashed), the live
            moved cutoff (solid, with an arrow from the static one) and, faintly, how far the full
            Range takes it, so the direction is visible even while idle. */
        struct CutMarker
        {
            bool visible = false;
            double staticHz = 0.0, liveHz = 0.0, limitHz = 0.0;
        };

        CutMarker cutMarker (int bandIndex) const;

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
        void onVBlank();
        void endRingDrag();
        void drawHoverRing (juce::Graphics& g, int bandIndex, float alpha);
        void drawSoloBadge (juce::Graphics& g, int bandIndex, float alpha);
        void drawSoloDragReadout (juce::Graphics& g, int bandIndex);
        void beginSoloDrag (int bandIndex);
        void endSoloAudition();
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}

        struct ResponseModel
        {
            std::array<dsp::BandCoefficients, params::numBands> coefficients {};
            std::array<dsp::BandPrototype, params::numBands> prototypes {};
            std::array<double, params::numBands> frequencies {};
            std::array<bool, params::numBands> enabled {};
            double sampleRate = 48000.0;
            bool analog = false;

            double decibelsAt (double frequency) const;
            double bandDecibelsAt (size_t bandIndex, double frequency) const; // 0 if disabled
        };

        /** Per band, the painted x columns with their dynamic delta (dB), split into the
            contiguous runs where |delta| > dynamicRegionThresholdDb. */
        struct DynamicRun
        {
            int band = -1;
            std::vector<std::pair<float, double>> points; // x, delta dB
        };

        std::vector<DynamicRun> computeDynamicRuns (int onlyBand = -1) const;
        bool bandDynamicsMoving (const dsp::BandSettings& settings, int bandIndex) const;

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

        // 0.9.2 hover ring state.
        int hoverBand = -1;
        std::array<float, params::numBands> hoverPhase {};   // 0..1 progress of the current fade
        std::array<bool, params::numBands> hoverRising {};
        int ringBand = -1;                                  // band whose ring is being dragged
        int ringDragMode = 0;                               // 0 undecided, 1 vertical, 2 rotary
        juce::Point<float> ringDownPosition, ringLastPosition;
        double ringAmount = 0.0;                            // upward amount (dB) while dragging
        bool ringGesture = false, ringEngagedDynamics = false;
        // A press on the S toggles Solo on release, and only if it was a click (no drag, 0.9.3);
        // 0.9.4: a drag that starts on it auditions that band alone and moves it.
        int soloPressBand = -1;
        int soloDragBand = -1;
        bool soloPressAlt = false;
        juce::Point<float> soloPressPosition;
        double lastVBlankTime = 0.0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        analysis::SpectrumDisplay liveDisplay, overlayDisplay;
        bool overlayPrimed = false;
        double lastDisplayTime = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveComponent)
    };
}
