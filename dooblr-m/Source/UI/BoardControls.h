#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/Primitives.h"
#include "Theme.h"

namespace dooblrm::ui
{
    /** Titled panel background used by every Model D column. */
    class SectionPanel final : public juce::Component
    {
    public:
        SectionPanel (juce::String titleToUse, juce::Colour accentToUse);
        void paint (juce::Graphics& g) override;
        juce::Rectangle<int> contentBounds() const;
        juce::String getTitle() const { return title; }

        static constexpr int titleHeight = 20;

    private:
        juce::String title;
        juce::Colour accent;
    };

    /** Rotary knob bound to an APVTS parameter. Caption under the knob, no value box. */
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
              const juce::String& caption, juce::Colour fill = Palette::accent, bool large = false);
        void resized() override;
        juce::Slider& getSlider() noexcept { return slider; }
        juce::Label& getCaption() noexcept { return captionLabel; }
        bool isLarge() const noexcept { return large; }

    private:
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label captionLabel;
        bool large = false;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Faint envelope-sweep arc and live marker on the large Cutoff knob. Hidden when
        Amount of Contour is 0. Drawn over the knob; does not intercept mouse. */
    class CutoffContourIndicator final : public juce::Component,
                                         private juce::Timer,
                                         private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        CutoffContourIndicator();
        ~CutoffContourIndicator() override;

        void attach (juce::AudioProcessorValueTreeState& state,
                     juce::Slider& cutoffSlider,
                     const std::atomic<float>& envTap);

        void paint (juce::Graphics& g) override;
        void parentSizeChanged() override;
        void parentHierarchyChanged() override;
        bool hitTest (int, int) override { return false; }

        void refresh();

        bool isRangeVisible() const noexcept;
        bool isLiveMarkerVisible() const noexcept;
        float getContourAmount() const noexcept;
        float getRangeStartHz() const noexcept;
        float getRangeEndHz() const noexcept;
        float getLiveHz() const noexcept;
        float getLiveEnvelope() const noexcept;
        float getRangeStartAngle() const noexcept;
        float getRangeEndAngle() const noexcept;
        float getLiveAngle() const noexcept;
        juce::Point<float> getCentre() const noexcept;
        float getRingRadius() const noexcept;
        juce::Point<float> getLiveMarkerPosition() const noexcept;
        int getRepaintRequests() const noexcept { return repaintRequests; }
        juce::Rectangle<int> getLastRepaintBounds() const noexcept { return lastRepaintBounds; }

        static constexpr float contourOctaves = 5.0f;

    private:
        struct Picture
        {
            bool rangeVisible = false, liveVisible = false;
            float contourAmt = 0, cutoffHz = 0, rangeEndHz = 0, liveHz = 0, liveEnv = 0;
            float startAngle = 0, endAngle = 0, liveAngle = 0, ringRadius = 0, stroke = 1.2f;
            juce::Point<float> centre, livePos;
            int width = 0, height = 0;
        };

        struct Key
        {
            int contourQ = 0, cutoffQ = 0, envQ = 0, w = 0, h = 0;
            bool operator== (const Key&) const = default;
        };

        void timerCallback() override;
        void parameterChanged (const juce::String&, float) override;
        void detachListeners();
        Picture makePicture() const;
        Key makeKey (const Picture&) const;
        float paramHz() const;
        float paramContour() const;
        float angleForHz (float hz) const;

        juce::AudioProcessorValueTreeState* state = nullptr;
        juce::Slider* cutoffSlider = nullptr;
        const std::atomic<float>* envTap = nullptr;
        Picture picture;
        Key lastKey;
        int repaintRequests = 0;
        juce::Rectangle<int> lastRepaintBounds;
        bool keyValid = false;
    };

    /** Discrete rotary switch (range / waveform) with tick marks. */
    class RotarySwitch final : public juce::Component
    {
    public:
        struct TickLabel
        {
            juce::String text;
            juce::Rectangle<float> bounds;
        };

        RotarySwitch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                      const juce::String& caption, const juce::StringArray& tickLabels);
        void paint (juce::Graphics& g) override;
        void resized() override;
        juce::Slider& getSlider() noexcept { return slider; }
        juce::Label& getCaption() noexcept { return captionLabel; }
        const std::vector<TickLabel>& getTickLabels() const noexcept { return tickLayout; }

        static constexpr int captionHeight = 16;

    private:
        juce::Font tickFont() const;
        std::vector<TickLabel> makeTickLayout() const;

        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label captionLabel;
        juce::StringArray ticks;
        std::vector<TickLabel> tickLayout;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Vertical rocker, dooblr-flat (no wood, no chrome). Click toggles; drag the lever
        (up = on, down = off) or rake across neighbouring rockers in one undo gesture. */
    class BatToggle final : public juce::Component
    {
    public:
        BatToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                   const juce::String& caption,
                   const juce::String& downLabel = {}, const juce::String& upLabel = {});
        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::ToggleButton& getButton() noexcept { return button; }
        juce::Label& getCaption() noexcept { return captionLabel; }

    private:
        struct SilentToggle final : juce::ToggleButton
        {
            void paint (juce::Graphics&) override {}
        };

        void setOnFromSlide (bool on);
        void endSlideGesture();
        void applySlideTo (BatToggle* other);

        SilentToggle button;
        juce::Label captionLabel;
        juce::String downLabel, upLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        juce::RangedAudioParameter* parameter = nullptr;
        std::vector<BatToggle*> slideGroup;
        bool sliding = false, slideOn = false, stateAtDown = false, gestureOpen = false;
    };

    /** Pitch or mod wheel. Pitch springs back to centre on mouse-up. */
    class Wheel final : public juce::Component
    {
    public:
        Wheel (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
               const juce::String& caption, bool springReturn);
        void resized() override;
        void paint (juce::Graphics& g) override;
        juce::Slider& getSlider() noexcept { return slider; }
        juce::Label& getCaption() noexcept { return captionLabel; }

    private:
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::Label captionLabel;
        bool spring = false;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Live output scope. Opaque, cached grid, no captions. */
    class ScopeView final : public juce::Component
    {
    public:
        explicit ScopeView (dsp::ScopeTap* tap);
        void paint (juce::Graphics& g) override;
        void resized() override;
        void refresh();

    private:
        void rebuildBackground();

        dsp::ScopeTap* tap = nullptr;
        juce::Image background;
        std::vector<float> samples;
        float lastHash = 0;
    };

    /** Filter contour: static ADS shape from Attack / Decay / Sustain / Amount, live playhead
        from the DSP envelope (lock-free atomic). Four draggable handles; no hint text. */
    class FilterReadout final : public juce::Component,
                                private juce::Timer,
                                private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        enum class Handle { none = -1, attack = 0, decay, sustain, amount };

        FilterReadout (juce::AudioProcessorValueTreeState& state,
                       const std::atomic<float>& envTap,
                       const std::atomic<int>& stageTap);
        ~FilterReadout() override;

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        void refresh();
        juce::Point<float> getHandlePosition (Handle) const;
        Handle handleAt (juce::Point<float>) const;
        juce::Point<float> getPlayheadPosition() const noexcept { return playhead; }
        float getLiveEnvelope() const noexcept { return liveEnv; }
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }

    private:
        struct Geometry
        {
            juce::Rectangle<float> plot;
            float x0 = 0, xPeak = 0, xSus = 0, xEnd = 0;
            float y0 = 0, yPeak = 0, ySus = 0, yTop = 0;
        };

        void timerCallback() override;
        void parameterChanged (const juce::String&, float) override;
        Geometry makeGeometry() const;
        juce::RangedAudioParameter* paramFor (Handle) const;
        float paramNorm (const char* id) const;
        void setHandleFromPoint (Handle, juce::Point<float>);
        void beginHandleGesture (Handle);
        void endHandleGesture();

        juce::AudioProcessorValueTreeState& state;
        const std::atomic<float>& envTap;
        const std::atomic<int>& stageTap;
        juce::Point<float> playhead;
        juce::Rectangle<float> plot;
        std::vector<juce::Point<float>> trail;
        float liveEnv = 0;
        int liveStage = 0;
        Handle dragHandle = Handle::none;
        juce::Point<float> grabOffset;
        bool gestureOpen = false;
    };
}
