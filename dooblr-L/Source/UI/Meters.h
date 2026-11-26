#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "../DSP/HistoryTap.h"
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

    /** Scrolling input / output / GR history with a draggable threshold line. Cached grid and
        waveform image, no hint text.

        Y axis is level: 0 dB at the top, −40 dB at the bottom. Input is a dim filled peak
        envelope; output is a brighter fill and line on top of it; GR is a distinct overlay
        from the top down and in the band between input and output. The line is Pre-Process
        **threshold**. Vertical drag writes threshold with begin/endChangeGesture; Shift or
        Command is 0.1× fine adjust; double-click resets. */
    class GrHistory final : public juce::Component
    {
    public:
        static constexpr float thresholdMinDb = -40.0f;
        static constexpr float thresholdMaxDb = 0.0f;
        static constexpr float hitSlopPx = 7.0f;
        static constexpr float fineDragScale = 0.1f;
        static constexpr float scaleWidth = 32.0f;
        static constexpr int maxColumns = 4096;

        struct Column
        {
            float inPeakDb = thresholdMinDb, outPeakDb = thresholdMinDb, grDb = 0;
        };

        explicit GrHistory (juce::AudioProcessorValueTreeState&);

        void ingest (const dsp::HistoryFrame* frames, int numFrames);
        void pushColumn (const Column&);
        void clear() noexcept;
        GrMeterModel& getModel() noexcept { return model; }
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        juce::Rectangle<float> plotBounds() const noexcept;
        float levelDbToY (float db) const noexcept;
        float thresholdDbToY (float db) const noexcept { return levelDbToY (db); }
        float yToThresholdDb (float y) const noexcept;
        float getThresholdLineY() const noexcept { return thresholdDbToY (shownDb); }
        float getThresholdDb() const noexcept { return shownDb; }

        int getFilledColumns() const noexcept { return filled; }
        Column getColumnFromOldest (int index) const noexcept;
        Column getColumnFromNewest (int index) const noexcept;

    private:
        void rebuildGrid();
        void rebuildWaveform();
        void drawColumn (juce::Graphics&, juce::Rectangle<float> plot, float x, float width, const Column&) const;
        bool overThresholdLine (juce::Point<float>) const noexcept;
        void setHover (bool);
        void applyDragY (float y);
        static float toDb (float linear) noexcept;

        GrMeterModel model;
        juce::Image grid, waveform;
        std::array<Column, maxColumns> ring {};
        int write = 0, filled = 0;

        juce::RangedAudioParameter* thresholdParam = nullptr;
        std::unique_ptr<juce::ParameterAttachment> thresholdAttachment;
        float shownDb = -3.0f;
        bool hover = false, dragging = false;
        float downMouseY = 0, downLineY = 0;
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
