#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/VocalSettings.h"
#include "../Parameters.h"

namespace dooblrvocals::ui
{
    /**
        v0.3 Reverb page display: the response the settings give, drawn from the same formulas the
        DSP uses - the dry hit, the pre-delay, the early reflections (spread by Size, level by the
        Early / Late balance), the late tail's decay (mid RT60, and the faster high-frequency decay
        that Damping sets), the wet band (low cut / high cut) and the live ducking.
    */
    class ReverbDisplay final : public juce::Component
    {
    public:
        explicit ReverbDisplay (juce::AudioProcessorValueTreeState& state);

        /** Re-reads the parameters and the live values; repaints only on a change. */
        void refresh (float duckDb, double bpm);
        void paint (juce::Graphics&) override;

        /** The plotted span (seconds) for a decay and pre-delay; for tests. */
        static double spanSeconds (double rt60, double preDelayMs) noexcept;

    private:
        params::SettingsReader reader;
        dsp::ReverbSettings shown;
        float shownDuckDb = 0.0f;
        double shownBpm = 120.0;
        bool haveShown = false;
    };

    /**
        v0.3 Delay page display: the repeats on a timeline (L above the centre line, R below),
        their levels from the feedback, the times (ms and note value when synced), the mode and
        the live ducking.
    */
    class DelayDisplay final : public juce::Component
    {
    public:
        explicit DelayDisplay (juce::AudioProcessorValueTreeState& state);

        void refresh (float duckDb, double bpm);
        void paint (juce::Graphics&) override;

    private:
        params::SettingsReader reader;
        dsp::DelaySettings shown;
        float shownDuckDb = 0.0f;
        double shownBpm = 120.0;
        bool haveShown = false;
    };

    /**
        v0.3 FX routing switch (bound to fx_routing): three clickable segments - Parallel,
        Delay > Reverb, Reverb > Delay - over a small signal-flow diagram of the chosen routing.
        Each click is one undo step. One instance sits on the Delay page and one on the Reverb page.
    */
    class FxRoutingView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit FxRoutingView (juce::AudioProcessorValueTreeState& state);
        ~FxRoutingView() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        int getRouting() const noexcept { return routing; }
        /** The clickable area of a routing (0..2), in local coordinates; for tests. */
        juce::Rectangle<int> getSegmentArea (int index) const;
        void choose (int index);

        static constexpr int preferredWidth = 210;

    private:
        juce::AudioProcessorValueTreeState& state;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int routing = 0, hover = -1;
    };
}
