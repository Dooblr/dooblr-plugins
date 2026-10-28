#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/VocalSettings.h"
#include "../Parameters.h"
#include "ControlPanel.h"

namespace dooblrvocals::ui
{
    /**
        Reverb page display: the response the settings give, drawn from the same formulas the
        DSP uses - the dry hit, the pre-delay, the early reflections (spread by Size, level by the
        Early / Late balance), the late tail's decay (mid RT60, and the faster high-frequency decay
        that Damping sets), the wet band (low cut / high cut) and the live ducking.
    */
    class ReverbDisplay final : public juce::Component
    {
    public:
        explicit ReverbDisplay (juce::AudioProcessorValueTreeState& state);

        /** Re-reads the parameters and the live values; repaints only when what is drawn changes. */
        void refresh (float duckDb, double bpm);
        void paint (juce::Graphics&) override;
        void resized() override { picture = {}; }

        /** Draws the whole picture (what the cached image holds), in logical coordinates. */
        void drawPicture (juce::Graphics&) const;

        /** The plotted span (seconds) for a decay and pre-delay; for tests. */
        static double spanSeconds (double rt60, double preDelayMs) noexcept;

        // For tests.
        int getPictureBuilds() const noexcept { return pictureBuilds; }
        int getRepaintRequests() const noexcept { return repaintRequests; }
        /** True while the reverb is off: the plot is drawn dimmed (no caption). */
        bool isDimmed() const noexcept { return haveShown && ! shown.enabled; }

    private:
        juce::uint64 pictureKey() const;

        params::SettingsReader reader;
        dsp::ReverbSettings shown;
        float shownDuckDb = 0.0f;
        double shownBpm = 120.0;
        bool haveShown = false;
        juce::uint64 shownKey = 0, pictureBuiltKey = 0;
        juce::Image picture;
        float pictureScale = 0.0f;
        int pictureBuilds = 0, repaintRequests = 0;
    };

    /**
        Delay page display: the repeats on a timeline (L above the centre line, R below),
        their levels from the feedback, the times (ms and note value when synced), the mode and
        the live ducking.
    */
    class DelayDisplay final : public juce::Component
    {
    public:
        explicit DelayDisplay (juce::AudioProcessorValueTreeState& state);

        /** Re-reads the parameters and the live values; repaints only when what is drawn changes. */
        void refresh (float duckDb, double bpm);
        void paint (juce::Graphics&) override;
        void resized() override { picture = {}; }

        /** Draws the whole picture (what the cached image holds), in logical coordinates. */
        void drawPicture (juce::Graphics&) const;

        // For tests.
        int getPictureBuilds() const noexcept { return pictureBuilds; }
        int getRepaintRequests() const noexcept { return repaintRequests; }
        /** True while the delay is off: the plot is drawn dimmed (no caption). */
        bool isDimmed() const noexcept { return haveShown && ! shown.enabled; }

    private:
        juce::uint64 pictureKey() const;

        params::SettingsReader reader;
        dsp::DelaySettings shown;
        float shownDuckDb = 0.0f;
        double shownBpm = 120.0;
        bool haveShown = false;
        juce::uint64 shownKey = 0, pictureBuiltKey = 0;
        juce::Image picture;
        float pictureScale = 0.0f;
        int pictureBuilds = 0, repaintRequests = 0;
    };

    /**
        FX routing switch (bound to fx_routing): a three-segment SegmentedToggle - Parallel,
        Delay > Reverb, Reverb > Delay - over a small signal-flow diagram of the chosen routing.
        Like every segmented toggle it picks on mouse-up inside the pressed segment (one undo
        step), takes Left / Right / Space / Return and has an accessibility handler. One instance
        sits on the Delay page and one on the Reverb page.
    */
    class FxRoutingView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit FxRoutingView (juce::AudioProcessorValueTreeState& state);
        ~FxRoutingView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getRouting() const noexcept { return routing; }
        /** The clickable area of a routing (0..2), in this view's coordinates; for tests. */
        juce::Rectangle<int> getSegmentArea (int index) const;
        /** The segmented switch itself (componentID "fx_routing_segments"). */
        SegmentedToggle& getSegments() noexcept { return segments; }
        void choose (int index);

        static constexpr int preferredWidth = 210;

    private:
        SegmentedToggle segments;
        std::unique_ptr<juce::ParameterAttachment> sketchAttachment;
        int routing = 0;
    };
}
