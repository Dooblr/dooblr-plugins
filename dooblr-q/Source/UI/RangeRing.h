#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace dooblrq::ui
{
    /**
        0.9.0: the outer ring around a band knob that edits the band's dynamic Range.

        The ring is a second control for the same parameter as the Range knob (it holds
        its own ParameterAttachment, so the two always agree). It sits concentrically
        around its target knob - Gain for gain types, Frequency for Low / High Cut, where
        the dynamics move the cutoff - and draws the dynamic excursion as an arc from the
        knob's current value to where the dynamics can take it, plus a dot for the live
        amount.

        Mouse: only the ring (an annulus) takes clicks; the inner knob behaves as before.
        A drag that starts mostly up/down edits vertically (up = more range); one that
        moves around the ring edits by rotation (clockwise = more range). Shift = fine,
        double-click = 0 dB, one undo step per drag. Disabled (dimmed) when the band's
        dynamics are off or its type has none.
    */
    class RangeRing final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        RangeRing();
        ~RangeRing() override;

        /** Binds to a Range parameter (nullptr detaches). */
        void attach (juce::RangedAudioParameter* rangeParameter);

        /** The knob the ring surrounds. The ring's own bounds are placed around it by placeAround(). */
        void setTarget (juce::Slider* knob);
        juce::Slider* getTarget() const noexcept { return target; }

        /** Positions the ring over its target (call after the target's bounds change). */
        void placeAround();

        /** Maps a dynamic amount in dB to the target knob's value (gain + dB, or the moved cutoff). */
        void setMapping (std::function<double (double dynamicDb)> toKnobValue);

        /** The live dynamic amount for the dot (dB, same sign as Range). */
        void setLiveAmount (double dynamicDb);

        void setRingColour (juce::Colour colour);

        /** True when the dynamics move a cutoff (Low / High Cut) rather than a gain; used by the tooltip. */
        void setMovesCutoff (bool movesCutoff, bool isHighCut);

        double getRangeDb() const noexcept { return rangeDb; }

        //==============================================================================
        enum class DragMode { undecided, vertical, rotary };

        static constexpr double minRangeDb = -24.0, maxRangeDb = 24.0;
        static constexpr float decisionDistance = 4.0f;          // pixels before a drag picks its mode
        static constexpr double pixelsForFullSpan = 240.0;      // vertical: 0.2 dB per pixel
        static constexpr double fineFactor = 0.1;               // Shift

        /** Picks the drag mode from the movement so far (centre / start / current in the same space). */
        static DragMode chooseDragMode (juce::Point<float> centre, juce::Point<float> start, juce::Point<float> current) noexcept;

        /** dB change for a vertical movement (positive dy = down = less range). */
        static double verticalDeltaDb (double dyPixels, bool fine) noexcept;

        /** Clockwise angle (radians, -pi..pi) from a to b around the centre (screen coordinates, y down). */
        static double clockwiseAngle (juce::Point<float> centre, juce::Point<float> a, juce::Point<float> b) noexcept;

        /** dB change for a rotation: the knob's full rotary sweep spans the whole Range. */
        static double rotaryDeltaDb (double radians, double sweepRadians, bool fine) noexcept;

        //==============================================================================
        /** Ring geometry in this component's coordinates (public for tests). */
        juce::Point<float> getCentre() const;
        float getRingRadius() const;
        float getInnerHitRadius() const;
        float getOuterHitRadius() const;
        DragMode getDragMode() const noexcept { return dragMode; }
        bool isHovered() const noexcept { return hovered; }

        bool hitTest (int x, int y) override;
        void paint (juce::Graphics& g) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void enablementChanged() override;

        /** Ring padding around the target slider's bounds. */
        static constexpr int margin = 14;

    private:
        void parameterChanged (float newValue);
        void setRangeFromUi (double newRangeDb);
        void updateTooltip();
        float angleForKnobValue (double value) const;

        juce::Slider* target = nullptr;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        std::function<double (double)> mapping;
        juce::Colour ringColour { Palette::accent };

        double rangeDb = 0.0, liveDb = 0.0, dragValue = 0.0, lastMappedFrom = 0.0, lastMappedTo = 0.0;
        bool hovered = false, gestureActive = false, cutoff = false, highCut = false;
        DragMode dragMode = DragMode::undecided;
        juce::Point<float> downPosition, lastPosition;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RangeRing)
    };
}
