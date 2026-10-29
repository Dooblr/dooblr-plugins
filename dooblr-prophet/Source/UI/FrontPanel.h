#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ControlPanel.h"

namespace dooblrprophet::ui
{
    /**
        One section of the single-page front panel (v0.1.1), e.g. OSCILLATOR A or FILTER.

        A title line (the section name, and optionally a compact mode switch on its right), then
        rows of cells on a column grid: rows of rotary knobs (caption above, value below), rows
        of pill toggles, a row of combo boxes, or a row of extra components owned elsewhere (the
        5 / 6 / 10 voice buttons, a status line). Each cell spans a fraction of the columns, so
        knobs in different rows line up. Knob rows share whatever height the section is given,
        so the panel fills the window at every size; the knob itself is capped by its cell.
    */
    class Section final : public juce::Component
    {
    public:
        enum class Kind { knob, toggle, combo, extra };

        struct Cell
        {
            Kind kind = Kind::knob;
            juce::String parameterId;    // knob, toggle, combo
            juce::String caption;
            juce::Component* extra = nullptr; // Kind::extra (not owned)
            float span = 1.0f;           // in columns
        };

        using Row = std::vector<Cell>;

        Section (juce::AudioProcessorValueTreeState& state, const juce::String& title, juce::Colour colour,
                 int columns, const std::vector<Row>& rows);
        ~Section() override;

        /** Adds the section's mode switch (owned) to the right of the title. */
        SegmentedToggle& setTitleSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                                         const juce::StringArray& labels);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The control bound to a parameter (the title switch included), or nullptr. */
        juce::Component* findControl (const juce::String& parameterId) const;
        SegmentedToggle* getTitleSwitch() const noexcept { return titleSwitch.get(); }
        juce::String getTitle() const { return title; }
        int getColumns() const noexcept { return columns; }
        int getNumKnobRows() const;
        /** The area of the title text (the switch sits to its right, never over it). */
        juce::Rectangle<int> getTitleBounds() const;

        /** Height the section needs with knob rows of the given height. */
        int getPreferredHeight (int knobRowHeight) const;

        static constexpr int titleHeight = 24, padding = 8, rowGap = 4;
        static constexpr int toggleRowHeight = 24, comboRowHeight = 40, extraRowHeight = 24;
        static constexpr int minKnobRowHeight = 72;

    private:
        struct Item
        {
            Cell cell;
            std::unique_ptr<juce::Component> control;
            std::unique_ptr<juce::Label> caption;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        };

        static Kind rowKind (const std::vector<Item>& row);
        static int fixedRowHeight (Kind kind);

        juce::String title;
        juce::Colour colour;
        int columns = 1;
        std::vector<std::vector<Item>> rows;
        std::unique_ptr<SegmentedToggle> titleSwitch;
        int titleSwitchWidth = 0;
    };
}
