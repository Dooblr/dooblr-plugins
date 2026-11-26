#pragma once

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "../DSP/Modes.h"

namespace dooblrchroma::ui
{
    class Knob final : public juce::Component
    {
    public:
        Knob (const juce::String& caption, const juce::String& paramId);
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        juce::String paramId;
        void resized() override;
        void attach (juce::AudioProcessorValueTreeState& state);
        void detach();

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class Choice final : public juce::Component
    {
    public:
        Choice (const juce::String& caption, const juce::String& paramId);
        juce::ComboBox box;
        juce::Label caption;
        juce::String paramId;
        void resized() override;
        void attach (juce::AudioProcessorValueTreeState& state);
        void detach();

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    class Toggle final : public juce::Component
    {
    public:
        Toggle (const juce::String& caption, const juce::String& paramId);
        juce::ToggleButton button;
        juce::String paramId;
        void resized() override;
        void attach (juce::AudioProcessorValueTreeState& state);
        void detach();

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    class Panel final : public juce::Component
    {
    public:
        explicit Panel (juce::String titleToUse);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::Rectangle<int> contentBounds() const;
        juce::String getTitle() const { return title; }
        juce::Component* findControl (const juce::String& paramId) const;

        juce::String title;
    };

    /** Object body + strike marker. No hint text. */
    class ResonatorView final : public juce::Component
    {
    public:
        ResonatorView();
        void setState (dsp::ResType type, float hitPosition, bool coupled);
        dsp::ResType getType() const noexcept { return type; }
        float getHit() const noexcept { return hit; }
        juce::Point<float> getHitMarker() const;
        void paint (juce::Graphics&) override;

    private:
        dsp::ResType type = dsp::ResType::string;
        float hit = 0.22f;
        bool coupled = false;
        juce::Point<float> marker;
    };
}
