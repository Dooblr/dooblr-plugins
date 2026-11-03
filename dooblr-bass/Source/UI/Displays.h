#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/SynthEngine.h"

namespace dooblrbass::ui
{
    juce::Colour pageColour (int page);

    /**
        Base for the board graphs: opaque, with the grid drawn once into a cached image at the
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

        juce::Rectangle<float> getPlotArea() const;
        const dsp::Settings& getShownSettings() const noexcept { return settings; }
        bool refresh();
        juce::String getReadout() const { return hover ? readoutAt (hoverPoint) : juce::String(); }
        void setHoverPoint (juce::Point<float> p) { hover = true; hoverPoint = p; repaint(); }

        static constexpr int titleStripHeight = 22;

    protected:
        virtual void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) = 0;
        virtual void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) = 0;
        virtual juce::String readoutAt (juce::Point<float>) const { return {}; }
        virtual bool changed (const dsp::Settings& before, const dsp::Settings& now) const;
        virtual int liveSignature() const { return 0; }

        static void drawTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour);
        static void drawHorizontalGrid (juce::Graphics& g, juce::Rectangle<float> plot, int divisions);

        dsp::Settings settings;

    private:
        SettingsSource source;
        juce::Image background;
        float backgroundScale = 0.0f;
        int lastLiveSignature = 0;
        bool hover = false;
        juce::Point<float> hoverPoint;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };

    /** Oscillator strip: one cycle (twice) of osc 1 / 2 / 3. */
    class OscillatorDisplay final : public GraphView
    {
    public:
        explicit OscillatorDisplay (SettingsSource source);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        juce::Path paths[3];
    };

    /** Filter strip: log-frequency ladder response with a draggable cutoff / resonance handle. */
    class FilterDisplay final : public GraphView
    {
    public:
        FilterDisplay (SettingsSource source, juce::RangedAudioParameter* cutoff, juce::RangedAudioParameter* resonance);

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        juce::Point<float> getHandlePosition() const;
        static constexpr float handleRadius = 8.0f, minHz = 20.0f, maxHz = 20000.0f;

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        juce::String readoutAt (juce::Point<float>) const override;

        static float lowPassDb (const dsp::Settings& s, float hz) noexcept;
        static juce::Point<float> positionFor (float hz, float resonance, juce::Rectangle<float> plot) noexcept;
        static float cutoffAt (float x, juce::Rectangle<float> plot) noexcept;
        static float resonanceAt (float y, juce::Rectangle<float> plot) noexcept;
        float cutoffOf() const;
        float resonanceOf() const;
        void setValues (float hz, float resonance);

        juce::RangedAudioParameter* cutoffParam = nullptr;
        juce::RangedAudioParameter* resoParam = nullptr;
        juce::Path path;
        bool dragging = false, hovering = false;
        float dragX = 0.0f, dragY = 0.0f;
        juce::Point<float> lastDragPosition;
    };

    /** Envelope strip: filter and amp ADSR curves (the times in effect, including Shared Decay). */
    class EnvelopeDisplay final : public GraphView
    {
    public:
        explicit EnvelopeDisplay (SettingsSource source);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        juce::Path filterPath, ampPath;
    };

    /** LFO strip: one cycle of the LFO. */
    class LfoDisplay final : public GraphView
    {
    public:
        LfoDisplay (SettingsSource source, dsp::SynthEngine::Tap& tap);

    private:
        void drawBackground (juce::Graphics& g, juce::Rectangle<float> plot) override;
        void drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) override;
        bool changed (const dsp::Settings& a, const dsp::Settings& b) const override;
        int liveSignature() const override;
        juce::Path path;
        dsp::SynthEngine::Tap* tap = nullptr;
    };
}
