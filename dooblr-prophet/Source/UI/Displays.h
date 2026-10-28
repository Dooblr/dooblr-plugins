#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/SynthEngine.h"

namespace dooblrprophet::ui
{
    /**
        Base for the page graphs: opaque, with the grid drawn once into a cached image at the
        physical pixel scale; the curves are drawn over it into pre-allocated paths. A VBlank
        callback polls the sound settings and repaints only when what the graph shows changed.
        Titles are uppercase in the curve colour; readouts appear only while hovering.
    */
    class GraphView : public juce::Component
    {
    public:
        using SettingsSource = std::function<dsp::Settings()>;

        GraphView (SettingsSource source, const juce::String& accessibleTitle);
        ~GraphView() override;

        void paint (juce::Graphics& g) override final;
        void resized() override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

        /** The plot area (inside the title strip and margins). */
        juce::Rectangle<float> getPlotArea() const;
        const dsp::Settings& getShownSettings() const noexcept { return settings; }
        /** Polls now (the VBlank callback does this); returns true if a repaint was queued. */
        bool refresh();
        /** The hover readout text ("" when not hovering); for tests. */
        juce::String getReadout() const { return hover ? readoutAt (hoverPoint) : juce::String(); }
        void setHoverPoint (juce::Point<float> p) { hover = true; hoverPoint = p; repaint(); }

        static constexpr int titleStripHeight = 22;

    protected:
        /** Draws the grid (once per size / scale) into the cached background. */
        virtual void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) = 0;
        virtual void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) = 0;
        virtual juce::String readoutAt (juce::Point<float>) const { return {}; }
        /** True when the graph must repaint for these settings (default: whenever they differ). */
        virtual bool changed (const dsp::Settings& before, const dsp::Settings& now) const;
        /** Live graphs: a hash of the live values shown; a change repaints. */
        virtual int liveSignature() const { return 0; }

        static void drawTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour);
        static void drawHorizontalGrid (juce::Graphics& g, juce::Rectangle<float> plot, int divisions);

        dsp::Settings settings;

    private:
        SettingsSource source;
        juce::Image background;
        float backgroundScale = 0.0f;
        int lastLiveSignature = 0;
        bool everPolled = false;
        bool hover = false;
        juce::Point<float> hoverPoint;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };

    /** Oscillators page: one cycle (twice) of each oscillator's wave as set, side by side. */
    class OscillatorDisplay final : public GraphView
    {
    public:
        explicit OscillatorDisplay (SettingsSource source);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        juce::Path paths[2];
    };

    /** Filters page: the ladder low-pass and the high-pass responses (analytic), log frequency. */
    class FilterDisplay final : public GraphView
    {
    public:
        explicit FilterDisplay (SettingsSource source);

        /** Response in dB of the low-pass ladder and the high-pass, as set, at a frequency. */
        static float lowPassDb (const dsp::Settings& s, float hz) noexcept;
        static float highPassDb (const dsp::Settings& s, float hz) noexcept;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -48.0f, maxDb = 24.0f;

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        juce::String readoutAt (juce::Point<float>) const override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        float xToHz (float x, juce::Rectangle<float> plot) const noexcept;
        juce::Path lowPath, highPath, combinedPath;
    };

    /** Envelopes page: the filter and amp envelopes, computed with the engine's own envelope. */
    class EnvelopeDisplay final : public GraphView
    {
    public:
        explicit EnvelopeDisplay (SettingsSource source);
        /** The time span shown (attack + decay + a sustain hold + release, of the longer one). */
        static float spanSeconds (const dsp::Settings& s) noexcept;

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        juce::String readoutAt (juce::Point<float>) const override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        void trace (juce::Path& path, const dsp::EnvSettings& env, juce::Rectangle<float> plot, float span, float hold) const;
        juce::Path filterPath, ampPath;
    };

    /** Modulation page: the LFO wave (two cycles) and how much of it is applied now. */
    class LfoDisplay final : public GraphView
    {
    public:
        LfoDisplay (SettingsSource source, const dsp::SynthEngine::VoiceTap& tap);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        int liveSignature() const override;
        const dsp::SynthEngine::VoiceTap& tap;
        juce::Path path;
    };

    /** Voices page: one lane per voice, the note it plays and its level, live. */
    class VoiceDisplay final : public GraphView
    {
    public:
        VoiceDisplay (SettingsSource source, const dsp::SynthEngine::VoiceTap& tap);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        int liveSignature() const override;
        const dsp::SynthEngine::VoiceTap& tap;
        // Built once so paint() does not allocate strings.
        std::array<juce::String, 128> noteLabels;
        std::array<juce::String, dsp::maxVoices> laneLabels;
    };

    juce::Colour pageColour (int page);
}
