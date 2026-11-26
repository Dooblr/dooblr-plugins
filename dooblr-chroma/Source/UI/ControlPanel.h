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

        juce::Label* getTextBox() const;
        juce::Rectangle<int> getRotaryBounds() const;

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

    /** Object body + mallet at the hit position. Drag to move the strike. No hint text. */
    class ResonatorView final : public juce::Component
    {
    public:
        ResonatorView();
        void setState (dsp::ResType type, float hitPosition, bool coupled);
        dsp::ResType getType() const noexcept { return type; }
        float getHit() const noexcept { return hit; }
        juce::Point<float> getHitMarker() const;
        std::function<void (float)> onHitChanged;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        float hitAt (juce::Point<float> p) const;
        void dragTo (juce::Point<float> p);

        dsp::ResType type = dsp::ResType::string;
        float hit = 0.22f;
        bool coupled = false;
        juce::Point<float> marker;
        bool dragging = false;
    };

    /** Icon tiles for picking a resonator object. */
    class ObjectPicker final : public juce::Component
    {
    public:
        ObjectPicker();
        void setType (dsp::ResType t);
        dsp::ResType getType() const noexcept { return type; }
        std::function<void (dsp::ResType)> onChange;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> tileBounds (int index) const;
        int indexAt (juce::Point<int> p) const;

        dsp::ResType type = dsp::ResType::string;
        int hover = -1;
    };

    /** Visual link between resonators A and B. */
    class CouplingArt final : public juce::Component
    {
    public:
        CouplingArt();
        void setCoupled (bool shouldBeCoupled);
        void paint (juce::Graphics&) override;

    private:
        bool coupled = false;
    };
}
