#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrkick::ui
{
    /**
        A titled group of parameter controls laid out left to right (wrapping): rotary knobs,
        pill toggles and combo boxes, each bound to a parameter with an APVTS attachment.
        Extra components (owned elsewhere) can be appended to the flow.
    */
    class ControlPanel final : public juce::Component
    {
    public:
        enum class Kind { knob, toggle, combo };

        struct Spec
        {
            juce::String parameterId;
            juce::String caption;
            Kind kind = Kind::knob;
            int width = 0; // 0: the kind's standard width
        };

        ControlPanel (juce::AudioProcessorValueTreeState& state, const juce::String& title, juce::Colour colour,
                      const std::vector<Spec>& specs);
        ~ControlPanel() override;

        /** Appends a component (not owned) as a cell of the given width. */
        void addExtra (juce::Component& component, int width);

        void paint (juce::Graphics& g) override;
        void resized() override;

        /** The control bound to a parameter (nullptr if none); for the editor and tests. */
        juce::Component* findControl (const juce::String& parameterId) const;
        juce::String getTitle() const { return title; }

        /** Height needed for the controls at the given width. */
        int getPreferredHeight (int width) const;

        static constexpr int titleHeight = 20, knobWidth = 70, knobHeight = 84, comboWidth = 128, toggleWidth = 104;

    private:
        struct Item
        {
            Spec spec;
            std::unique_ptr<juce::Component> control;
            juce::Component* extra = nullptr;
            int width = 0;
            std::unique_ptr<juce::Label> caption;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        };

        std::vector<juce::Rectangle<int>> layoutCells (int width) const;

        juce::String title;
        juce::Colour colour;
        std::vector<Item> items;
    };
}
