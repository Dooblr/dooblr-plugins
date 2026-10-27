#pragma once

#include <functional>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrkick::ui
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

    /**
        v0.3.3: Time / Level / Curve knobs for one point of the master envelope, with a point
        selector. The knobs are bound (APVTS attachments) to that point's own host parameters,
        "Master Env N Time / Level", and Curve to "Master Env N-1 Curve" (the segment into the
        point), so they and the graph edit the same automatable values. Point 1 sits at 0 ms with
        no segment before it: its Time and Curve knobs are disabled there.
    */
    class EnvelopePointKnobs final : public juce::Component
    {
    public:
        EnvelopePointKnobs (juce::AudioProcessorValueTreeState& state, juce::Colour colour);
        ~EnvelopePointKnobs() override;

        /** Rebinds the knobs to a point (0-based, clamped to the point count). */
        void setPoint (int point);
        int getPoint() const noexcept { return point; }
        /** The selector buttons changed the point (the graph follows). */
        std::function<void (int)> onPointChange;

        juce::Slider& getTimeKnob() noexcept { return knobs[0]; }
        juce::Slider& getLevelKnob() noexcept { return knobs[1]; }
        juce::Slider& getCurveKnob() noexcept { return knobs[2]; }
        juce::TextButton& getPreviousButton() noexcept { return previous; }
        juce::TextButton& getNextButton() noexcept { return next; }
        /** The parameter a knob (0 time, 1 level, 2 curve) is bound to now ("" when disabled). */
        juce::String getBoundParameterId (int knob) const { return boundIds[(size_t) knob]; }

        void resized() override;
        void paint (juce::Graphics&) override;

        static constexpr int preferredWidth = 120 + 3 * 70 + 3 * 6;

    private:
        void refreshPointLabel();
        int getNumPoints() const;

        juce::AudioProcessorValueTreeState& state;
        std::atomic<float>* count = nullptr;
        int point = 1, shownCount = -1;
        juce::Label caption, pointLabel;
        juce::TextButton previous { "<" }, next { ">" };
        std::array<juce::Slider, 3> knobs;
        std::array<juce::Label, 3> knobCaptions;
        std::array<juce::String, 3> boundIds;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}

