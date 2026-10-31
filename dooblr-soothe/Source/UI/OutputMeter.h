#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/OutputPeakTap.h"

namespace dooblrsoothe::ui
{
    /**
        v0.3.4: the header's small vertical stereo peak meter for the final output (after the
        Output gain). Green below -6 dBFS, yellow from -6 to 0, red at or above 0 dBFS, blended
        over a dB just under each threshold. Instant attack, smooth release, a peak-hold tick that
        decays after a moment, and a clip light per channel that stays red until clicked.
        Reads the processor's OutputPeakTap on VBlank; repaints only when the picture changes.
    */
    class OutputMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Zone { green, yellow, red };

        static constexpr float minDb = -48.0f, maxDb = 3.0f, yellowDb = -6.0f, redDb = 0.0f;
        static constexpr float blendDb = 1.0f; // colour blend just below each threshold
        static constexpr double holdMs = 700.0, releaseDbPerSecond = 26.0, holdDecayDbPerSecond = 18.0;

        static Zone zoneFor (float db) noexcept;
        static juce::Colour colourFor (float db) noexcept;
        static juce::Colour zoneColour (Zone zone) noexcept;

        explicit OutputMeter (dsp::OutputPeakTap* tap = nullptr);

        /** Feeds one frame's peaks (linear) at a time in ms: VBlank does this from the tap. */
        void pushPeaks (float left, float right, double nowMs);

        float getLevelDb (int channel) const noexcept { return channels[(size_t) channel].level; }
        float getHoldDb (int channel) const noexcept { return channels[(size_t) channel].hold; }
        bool isClipLatched (int channel) const noexcept { return channels[(size_t) channel].clipped; }
        bool isClipLatched() const noexcept { return isClipLatched (0) || isClipLatched (1); }
        void resetClip();

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        struct Channel
        {
            float level = minDb, hold = minDb;
            double holdSince = 0.0;
            bool clipped = false;
        };

        void tick();
        float dbToY (float db, juce::Rectangle<float> bar) const noexcept;

        dsp::OutputPeakTap* tap;
        std::array<Channel, 2> channels;
        double lastPushMs = -1.0;
        int lastPictureHash = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
