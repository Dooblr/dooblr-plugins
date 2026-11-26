#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "../DSP/ReverbEngine.h"

namespace dooblrreverb
{
    class DooblrReverbProcessor;
}

namespace dooblrreverb::ui
{
    /**
        The large display: two curves over the live spectrum on one log-frequency axis.

        - Decay Rate EQ (accent): how much longer or shorter each frequency rings, as a % of the
          Space decay time, on the left scale (12.5 % .. 400 %, one octave of decay per grid line).
          A faint line under it shows the decay the engine actually aims for, Brightness and the
          room character included.
        - Post EQ (dynamic): a regular EQ on the reverb's output, on the right dB scale (8 dB per
          grid line, so 100 % and 0 dB share the centre line).
        - Spectrum (spectrum colour): the reverb output, and optionally the input faintly behind it.

        Handles: drag (Shift for fine moves), mouse wheel for Q, double-click a handle to remove
        it, right-click for shape / slope / remove. Double-click an empty spot to add a band to the
        layer picked with the DECAY RATE EQ / POST EQ buttons (clicking a handle also picks its
        layer). Hovering shows a readout chip. Opaque; the grid is cached; VBlank drives updates.
    */
    class ReverbDisplay final : public juce::Component
    {
    public:
        enum class Layer { decay = 0, eq = 1 };

        explicit ReverbDisplay (DooblrReverbProcessor& processor);
        ~ReverbDisplay() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** Polls parameters and the analyzer (VBlank does this); returns true if a repaint was queued. */
        bool poll();

        Layer getEditLayer() const noexcept { return editLayer; }
        void setEditLayer (Layer layer);

        // Geometry and hit testing (also for tests).
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        juce::Point<float> getHandlePosition (Layer layer, int band) const;
        /** The handle under the point: { layer, band }, band -1 for none. */
        std::pair<Layer, int> handleAt (juce::Point<float> position) const;
        float xForFrequency (float frequency) const noexcept;
        float frequencyForX (float x) const noexcept;
        float yForDecayLog2 (float log2Rate) const noexcept;
        float yForDb (float db) const noexcept { return yForDecayLog2 (db / dbPerOctave); }
        float decayLog2ForY (float y) const noexcept;
        const juce::Path& getDecayPath() const noexcept { return decayPath; }
        const juce::Path& getEqPath() const noexcept { return eqPath; }
        const juce::Path& getPostSpectrumPath() const noexcept { return postPath; }
        juce::TextButton& getLayerButton (Layer layer) noexcept { return layer == Layer::decay ? decayButton : eqButton; }

        /** Pushes a test tone through the analyzer state at once (tests and snapshots). */
        void feedAnalyzerForTest (const float* post, const float* pre, int numSamples);

        static constexpr float dbPerOctave = 8.0f;   // the EQ scale: 8 dB per octave of decay
        static constexpr float scaleOctaves = 3.0f;  // +-3 octaves of decay = +-24 dB

    private:
        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, numPoints = 160;

        struct Analyzer
        {
            std::array<float, fftSize> history {};
            int writePos = 0, sinceLast = 0;
            std::array<float, numPoints> levels {};
            bool valid = false;
        };

        void drawBackground();
        void rebuildCurves();
        void rebuildSpectrum();
        void addSmoothCurveTo (juce::Path& path) const;
        void feed (Analyzer& a, const float* samples, int n);
        void analyse (Analyzer& a);
        juce::String readoutText() const;
        void setParam (const juce::String& pid, float value);
        void beginGesture (const juce::String& pid);
        void endGesture (const juce::String& pid);
        void addBandAt (Layer layer, juce::Point<float> position);
        void removeBand (Layer layer, int band);
        void showBandMenu (Layer layer, int band);
        void showAddMenu (juce::Point<float> position);
        juce::String freqId (Layer layer, int band) const;
        juce::String valueId (Layer layer, int band) const;   // rate or gain
        juce::String qId (Layer layer, int band) const;
        bool bandActive (Layer layer, int band) const;
        bool bandUsesValue (Layer layer, int band) const;

        DooblrReverbProcessor& processor;
        juce::AudioProcessorValueTreeState& state;
        std::unique_ptr<juce::VBlankAttachment> vblank;
        juce::TextButton decayButton { "DECAY RATE EQ" }, eqButton { "POST EQ" };

        dsp::ReverbEngine::Settings settings;
        bool settingsValid = false;
        Layer editLayer = Layer::decay;

        juce::Rectangle<float> plot;
        juce::Image background;
        float backgroundScale = 0.0f;
        juce::Path decayPath, decayFill, effectivePath, eqPath, eqFill, postPath, postFill, prePath;

        juce::dsp::FFT fft { fftOrder };
        std::array<float, 2 * fftSize> fftData {};
        std::array<float, fftSize> window {};
        std::array<float, numPoints> pointFreq {}, pointX {}, pointY {}, smoothed {};
        Analyzer post, pre;
        int analyzerShown = -1;
        double lastTickMs = 0.0;

        int hoverBand = -1, dragBand = -1;
        Layer hoverLayer = Layer::decay, dragLayer = Layer::decay;
        bool hovering = false, dragMoved = false;
        juce::Point<float> hoverPosition, dragPosition, lastDragMouse;
        static constexpr float handleRadius = 8.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbDisplay)
    };
}
