#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "../DSP/LimiterEngine.h"

namespace dooblrl::ui
{
    /** Display-only GR ballistics and a 60 Hz / 3 s history. No Component. */
    class GrMeterModel
    {
    public:
        static constexpr float rangeDb = 24.0f;
        static constexpr float releaseDbPerSecond = 12.0f;
        static constexpr double holdMs = 700.0;
        static constexpr float holdDecayDbPerSecond = 18.0f;
        static constexpr int historyLength = 180;
        static constexpr double historyHz = 60.0;

        void reset() noexcept;
        void tick (float incomingDb, double dtSeconds, double nowMs) noexcept;

        float getDisplayedDb() const noexcept { return displayedDb; }
        float getPeakHoldDb() const noexcept { return peakHoldDb; }
        int getHistoryFilled() const noexcept { return historyFilled; }
        float historyOldest (int index) const noexcept;
        float historyNewest() const noexcept;

    private:
        void pushHistory (float db) noexcept;

        float displayedDb = 0, peakHoldDb = 0;
        double peakHoldUntilMs = 0, sampleAccum = 0;
        std::array<float, historyLength> history {};
        int historyWrite = 0, historyFilled = 0;
    };

    /** Stereo peak + true-peak meter. Instant attack, 26 dB/s release. Cached paint. */
    class LevelMeter final : public juce::Component
    {
    public:
        static constexpr float minDb = -48.0f, maxDb = 3.0f;

        explicit LevelMeter (juce::String caption);

        void push (float peakL, float peakR, float truePeakL, float truePeakR, double nowMs);
        void paint (juce::Graphics&) override;

    private:
        struct Ch { float peak = minDb, tp = minDb; };
        juce::String caption;
        std::array<Ch, 2> ch;
        double lastMs = -1.0;
        int lastPicture = 0;
    };

    /** GR history (newest right). Cached grid, no hint text. */
    class GrHistory final : public juce::Component
    {
    public:
        GrHistory();

        void push (float grDb, double nowMs);
        GrMeterModel& getModel() noexcept { return model; }
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void rebuildGrid();

        GrMeterModel model;
        juce::Image grid;
        double lastMs = -1.0;
        int lastPicture = 0;
    };

    /** LUFS M / S / I and LRA plus Reset. */
    class LoudnessPanel final : public juce::Component
    {
    public:
        LoudnessPanel();

        void setReadings (float m, float s, float i, float lra, bool hasI, float targetLufs);
        std::function<void()> onReset;
        juce::TextButton& getResetButton() noexcept { return reset; }
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        float mLufs = -70, sLufs = -70, iLufs = -70, lra = 0, target = 1.0e9f;
        bool hasI = false;
        juce::TextButton reset { "Reset" };
        int lastPicture = 0;
    };
}
