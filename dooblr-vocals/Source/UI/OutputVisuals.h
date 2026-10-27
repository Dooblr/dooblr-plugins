#pragma once

#include <array>
#include <initializer_list>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/MeterTap.h"
#include "../DSP/SpectrumAnalyzer.h"

namespace dooblrvocals::ui
{
    /** Wraps parameter edits from a drag in one gesture per drag (= one undo step). */
    class ParameterGesture
    {
    public:
        explicit ParameterGesture (juce::AudioProcessorValueTreeState& s) : state (s) {}
        ~ParameterGesture() { end(); }

        void begin (std::initializer_list<const char*> ids);
        void end();
        bool isActive() const noexcept { return ! active.empty(); }
        void set (const char* id, float plainValue);
        float get (const char* id) const;
        juce::RangedAudioParameter* param (const char* id) const;

    private:
        juce::AudioProcessorValueTreeState& state;
        std::vector<juce::RangedAudioParameter*> active;
    };

    /**
        Scrolling level history drawn into a cached image: one column per MeterFrame (about
        5 ms), newest on the right. Each update scrolls the image and draws only the new
        columns, so painting is one image blit whatever the size.
    */
    class HistoryStrip
    {
    public:
        struct Column { float fillDb = -120, lineDb = -120, secondaryDb = -120, grDb = 0; };
        struct Colours { juce::Colour fill, line, secondary, gr; };

        void setRange (float minDb, float maxDb, std::initializer_list<float> gridDb);
        void setColours (const Colours& c) { colours = c; }
        void setBounds (juce::Rectangle<int> area, float pixelScale); // allocates the image
        juce::Rectangle<int> getBounds() const noexcept { return bounds; }

        void push (const Column& column) noexcept;
        /** Draws the queued columns; returns true when the image changed. */
        bool flush();
        void clear();
        void draw (juce::Graphics& g) const;

        float dbToY (float db) const noexcept;
        float yToDb (float y) const noexcept;
        const Column& latest() const noexcept { return last; }
        int getNumColumns() const noexcept { return bounds.getWidth(); }

    private:
        void drawColumn (juce::Graphics& g, int x, const Column& c, const Column& previous) const;

        juce::Rectangle<int> bounds;
        float scale = 1.0f, minDb = -60.0f, maxDb = 6.0f;
        std::vector<float> grid;
        Colours colours;
        juce::Image image;
        std::array<Column, 4096> pending {};
        int numPending = 0, silentColumns = 1 << 30;
        Column last;
    };

    //==============================================================================
    /**
        Output EQ view: the live post-EQ spectrum (with optional peak hold) behind the 3-band
        response curve, and a handle per band.
        - drag a handle: frequency (horizontal) and gain (vertical)
        - Mid handle: alt-drag vertically or the mouse wheel changes Q (the shelves have a fixed Q)
        - double-click a handle: gain back to 0 dB
        Dragging a handle while the EQ is off still edits the band; the tab power toggle stays off.
    */
    class EqDisplay final : public juce::Component
    {
    public:
        explicit EqDisplay (juce::AudioProcessorValueTreeState& state);

        /** Message thread, once per vblank while visible. */
        void update (const float* samples, int numSamples, double sampleRate, double elapsedSeconds);
        void reset();

        juce::ToggleButton& getPeakHoldButton() noexcept { return peakHoldButton; }

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

        // For tests.
        enum Band { low = 0, mid, high, numBands };
        juce::Point<float> getHandlePosition (int band) const;
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        const dsp::SpectrumAnalyzer& getAnalyzer() const noexcept { return analyzer; }
        bool spectrumHasSignal() const noexcept { return ! analyzer.isSilent(); }

    private:
        struct BandIds { const char* freq; const char* gain; const char* q; };
        static const std::array<BandIds, numBands> bandIds;

        float freqToX (float hz) const noexcept;
        float xToFreq (float x) const noexcept;
        float gainToY (float db) const noexcept;
        float yToGain (float y) const noexcept;
        float spectrumDbToY (float db) const noexcept;
        int hitBand (juce::Point<float> p) const;
        juce::uint64 paramHash() const;
        void rebuildCurve();
        void rebuildSpectrum();
        void rebuildBackground();
        void updateReadout (int band);

        ParameterGesture gesture;
        dsp::SpectrumAnalyzer analyzer;
        juce::ToggleButton peakHoldButton { "Peak hold" };

        juce::Rectangle<float> plot;
        juce::Image background;
        juce::Path spectrumFill, spectrumLine, peakLine, curveLine, curveFill;
        std::vector<float> columnFreqs, columnLevels;
        juce::uint64 lastHash = 0;
        int hoverBand = -1, dragBand = -1;
        bool altDrag = false, spectrumDirty = true;
        float dragStartQ = 1.0f;
        juce::String readout;
    };

    //==============================================================================
    /**
        Dynamics view: a scrolling history of the compressor (input fill, output line,
        pre-shaper input as a thin line, gain reduction hanging from the top) and the
        compressor's transfer curve (threshold, ratio, 6 dB soft knee, makeup) with a dot at the
        current input / output level.
        - drag the threshold line in the history, or the threshold handle on the curve: threshold
        - drag the curve above the threshold: ratio (the curve follows the mouse)
        Dragging while the compressor is off switches it on in the same gesture.
    */
    class DynamicsDisplay final : public juce::Component
    {
    public:
        explicit DynamicsDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

        // For tests.
        juce::Rectangle<float> getTransferArea() const noexcept { return transfer; }
        juce::Rectangle<int> getHistoryArea() const noexcept { return history.getBounds(); }
        juce::Point<float> getThresholdHandlePosition() const;
        float getThresholdLineY() const { return history.dbToY (gesture.get ("comp_threshold")); }
        juce::Point<float> transferPoint (float inDb, float outDb) const noexcept;
        float getShownInputDb() const noexcept { return shownInDb; }
        float getShownGainReductionDb() const noexcept { return shownGrDb; }
        int getFramesReceived() const noexcept { return framesReceived; }

    private:
        enum class DragKind { none, thresholdLine, thresholdHandle, ratio };
        DragKind hitTestDrag (juce::Point<float> p) const;
        float dbToTransferX (float db) const noexcept;
        float dbToTransferY (float db) const noexcept;
        float transferXToDb (float x) const noexcept;
        float transferYToDb (float y) const noexcept;
        juce::uint64 paramHash() const;
        void rebuildCurve();
        void rebuildBackground();
        juce::Rectangle<int> dotBounds() const;
        void startDrag (DragKind kind);

        ParameterGesture gesture;
        HistoryStrip history;
        juce::Rectangle<float> transfer;
        juce::Image background;
        juce::Path curve;
        juce::uint64 lastHash = 0;
        DragKind hover = DragKind::none, dragging = DragKind::none;
        float shownInDb = -120.0f, shownGrDb = 0.0f;
        int framesReceived = 0;
        juce::String readout;
    };

    //==============================================================================
    /**
        The limiter page's right column: a vertical output meter (zone colours from the
        header's OutputMeter, instant attack, smooth release, a peak-hold tick that decays
        after a moment, a dB scale) beside a gain-reduction bar growing down from the top, with
        the ceiling drawn across the level bar. Fed by LimiterDisplay::update (the editor's
        VBlank read of the meter FIFO); repaints only when the picture changes. Mouse-transparent.
    */
    class LimiterMeter final : public juce::Component
    {
    public:
        static constexpr float minDb = -36.0f, maxDb = 12.0f, grRangeDb = 24.0f;

        LimiterMeter();

        /** One VBlank batch: loudest limiter output and deepest GR (dB, <= 0) since the last. */
        void push (float outDb, float grDb, double elapsedSeconds);
        void setCeiling (float ceilingDb, bool limiterOn);
        void reset();

        float getLevelDb() const noexcept { return level; }
        float getHoldDb() const noexcept { return hold; }
        float getGainReductionDb() const noexcept { return gr; }
        float getCeilingDb() const noexcept { return ceiling; }
        juce::Rectangle<float> getLevelBar() const;
        juce::Rectangle<float> getGrBar() const;
        float dbToY (float db) const;

        void paint (juce::Graphics& g) override;

    private:
        int pictureHash() const;

        float level = minDb, hold = minDb, gr = 0.0f, ceiling = 0.0f;
        double holdAge = 0.0;
        bool on = true;
        int lastHash = 0;
    };

    //==============================================================================
    /**
        Limiter view: a scrolling history of the clipper / limiter input (fill), its output
        before the output gain (line) and its gain reduction (from the top), with the ceiling.
        - drag the ceiling line vertically: ceiling
        Dragging while the mode is Off switches the limiter on in the same gesture.
    */
    class LimiterDisplay final : public juce::Component
    {
    public:
        explicit LimiterDisplay (juce::AudioProcessorValueTreeState& state);

        void update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds);
        void reset();

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

        // For tests.
        juce::Rectangle<int> getHistoryArea() const noexcept { return history.getBounds(); }
        float getCeilingY() const;
        float getShownGainReductionDb() const noexcept { return shownGrDb; }
        float getShownPeakOutDb() const noexcept { return shownOutDb; }
        int getFramesReceived() const noexcept { return framesReceived; }
        LimiterMeter& getMeter() noexcept { return meter; }

    private:
        juce::uint64 paramHash() const;
        void rebuildBackground();
        void updateReadouts();

        ParameterGesture gesture;
        HistoryStrip history;
        LimiterMeter meter;
        juce::Rectangle<int> readoutArea;
        juce::Image background;
        juce::uint64 lastHash = 0;
        bool hover = false, dragging = false;
        float shownGrDb = 0.0f, shownOutDb = -120.0f;
        int framesReceived = 0;
        juce::String ceilingText;
    };
}
