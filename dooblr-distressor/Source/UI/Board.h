#pragma once

#include <array>
#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace dooblrdistressor::ui
{
    /** Gain-reduction LED bar, hardware-style: 16 lamps from 0.5 dB to 20 dB. */
    class GrLedMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int numLeds = 16;
        static constexpr float minDb = 0.5f, maxDb = 20.0f;

        GrLedMeter();

        void setGainReductionDb (float db);
        float getGainReductionDb() const noexcept { return grDb; }

        void paint (juce::Graphics&) override;

    private:
        float grDb = 0.0f;
        int lastPicture = 0;
    };

    /** 1 % and 3 % / clip distortion lamps. Yellow = 1 % (UA); red = 3 % / clip. */
    class DistLeds final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        DistLeds();

        void setState (bool led1pct, bool led3pct);
        bool is1pct() const noexcept { return led1; }
        bool is3pct() const noexcept { return led3; }

        void paint (juce::Graphics&) override;

    private:
        bool led1 = false, led3 = false;
    };

    /** Simple stereo peak meter (input or output). Instant attack, 26 dB/s release. */
    class StereoMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr float minDb = -48.0f, maxDb = 3.0f;

        explicit StereoMeter (juce::String caption);

        void pushPeaks (float left, float right, double nowMs);
        float getLevelDb (int channel) const noexcept { return channels[(size_t) channel].level; }

        void paint (juce::Graphics&) override;

    private:
        struct Channel { float level = minDb; };

        juce::String caption;
        std::array<Channel, 2> channels;
        double lastMs = -1.0;
    };
}
