#pragma once

#include <functional>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrvocals::ui
{
    /**
        A titled group of parameter controls laid out left to right (wrapping): rotary knobs,
        pill toggles and combo boxes, each bound to a parameter with an APVTS attachment.
        Extra components (owned elsewhere) can be added to the flow.

        v0.3.3: items can share a fixed slot. A slot's cell is always reserved (as wide as its
        widest member) whether or not any member is shown, so swapping the members of a slot
        (e.g. a layer's Synth-only / Sample-only controls) never moves anything else.
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
            /** Combos: leading choices not offered in the box (e.g. a mode's "Off", which belongs to
                the processor's tab power toggle). While the parameter is on one of them the box
                shows nothing selected (see setTextWhenNothingSelected). */
            int hiddenLeadingChoices = 0;
            /** >= 0: the fixed slot this control shares (see the class comment). */
            int slot = -1;
        };

        ControlPanel (juce::AudioProcessorValueTreeState& state, const juce::String& title, juce::Colour colour,
                      const std::vector<Spec>& specs);
        ~ControlPanel() override;

        /** Adds a component (not owned) as a cell of the given width: appended, or placed before the
            control of insertBefore. fullCell: it gets the whole cell (else a 26 px row under the
            caption line, like a toggle). */
        void addExtra (juce::Component& component, int width, int slot = -1, const juce::String& insertBefore = {},
                       bool fullCell = false);

        void paint (juce::Graphics& g) override;
        void resized() override;

        /** Shows or hides a control (with its caption) or an extra; the rest reflow with no gap.
            A hidden parameter keeps its value. Returns true if anything changed. */
        bool setShown (const juce::String& parameterId, bool shouldBeShown);
        bool setShown (juce::Component& extra, bool shouldBeShown);
        bool isShown (const juce::String& parameterId) const;

        /** The control bound to a parameter (nullptr if none); for the editor and tests. */
        juce::Component* findControl (const juce::String& parameterId) const;
        juce::String getTitle() const { return title; }

        /** Dims a control and its caption (still shown and laid out), e.g. Key / Scale while the
            correction is Chromatic. */
        void setDimmed (const juce::String& parameterId, bool shouldBeDimmed, float alpha = 0.38f);

        /** Height needed for the controls at the given width. */
        int getPreferredHeight (int width) const;

        /** The bounds reserved for a slot (empty if there is none); for the editor and tests. */
        juce::Rectangle<int> getSlotBounds (int slot) const;

        static constexpr int titleHeight = 20, knobWidth = 70, knobHeight = 84, comboWidth = 128, toggleWidth = 104;

    private:
        struct Item
        {
            Spec spec;
            std::unique_ptr<juce::Component> control;
            juce::Component* extra = nullptr;
            bool extraFullCell = false;
            int width = 0;
            std::unique_ptr<juce::Label> caption;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
            std::unique_ptr<juce::ParameterAttachment> choiceAttachment;
            bool shown = true;
        };

        bool setItemShown (Item& item, bool shouldBeShown);

        std::vector<juce::Rectangle<int>> layoutCells (int width, std::vector<std::pair<int, juce::Rectangle<int>>>* slots = nullptr) const;

        juce::String title;
        juce::Colour colour;
        std::vector<Item> items;
    };

    /**
        A large two-segment switch bound to a two-choice parameter (e.g. the pitch Mode:
        KEYED | CHROMATIC). The active segment is filled in the colour; the other is dimmed,
        and brightens with a hand cursor on hover. A click picks a segment (on release, inside
        the pressed segment) as one complete gesture, so one undo step; clicking the active
        segment does nothing. Left / Right step through the segments, Space / Return pick the
        next one (wrapping; with two segments that toggles).
    */
    class SegmentedToggle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SegmentedToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                         const juce::StringArray& labels, juce::Colour colour);
        ~SegmentedToggle() override;

        int getSelectedIndex() const noexcept { return selected; }
        int getHoveredIndex() const noexcept { return hovered; }
        int getNumSegments() const noexcept { return labels.size(); }
        juce::String getSegmentText (int index) const { return labels[index]; }
        juce::Rectangle<int> getSegmentBounds (int index) const;
        /** Picks a segment as one complete gesture (one undo step); no-op if already selected. */
        void select (int index);
        /** Picks the next segment, wrapping (Space / Return and the accessibility press). */
        void selectNext();

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void enablementChanged() override { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        static constexpr int minWidth = 236, minHeight = 32;

    private:
        int segmentAt (juce::Point<int> position) const;
        void setHovered (int index);

        juce::StringArray labels;
        juce::Colour colour;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int selected = 0, hovered = -1, pressed = -1;
    };

    /**
        v0.3.3: a layer's Level as a tall vertical fader at the left of its page: caption on top,
        dB readout below, bound to the layer's level parameter (double-click resets, Shift + drag
        is fine, gestures make it one undo step and the host sees them).
    */
    class LevelFader final : public juce::Component
    {
    public:
        LevelFader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, juce::Colour colour);
        ~LevelFader() override;

        void resized() override;
        juce::Slider& getSlider() noexcept { return slider; }

        static constexpr int width = 56;

    private:
        juce::Label caption;
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
