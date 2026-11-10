#pragma once

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
              const juce::String& caption, juce::Colour fill = Palette::accent);
        void resized() override;
        juce::Slider& getSlider() noexcept { return slider; }
        juce::Label& getCaption() noexcept { return captionLabel; }

    private:
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label captionLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
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

    /** Vertical rocker, dooblr-flat (no wood, no chrome). */
    class BatToggle final : public juce::Component
    {
    public:
        BatToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                   const juce::String& caption);
        void paint (juce::Graphics& g) override;
        void resized() override;
        juce::ToggleButton& getButton() noexcept { return button; }
        juce::Label& getCaption() noexcept { return captionLabel; }

    private:
        struct SilentToggle final : juce::ToggleButton
        {
            void paint (juce::Graphics&) override {}
        };

        SilentToggle button;
        juce::Label captionLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
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

    /** Ladder magnitude sketch: cutoff marker, emphasis peak. No hint text. */
    class FilterReadout final : public juce::Component
    {
    public:
        FilterReadout();
        void set (float cutoffHz, float emphasis);
        void paint (juce::Graphics& g) override;
        void resized() override;
        juce::Point<float> getHandlePosition() const noexcept { return handle; }
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }

    private:
        void rebuild();

        float cutoff = 1200.0f, emphasis = 0.15f;
        juce::Image background;
        juce::Path curve;
        juce::Point<float> handle;
        juce::Rectangle<float> plot;
    };
}
