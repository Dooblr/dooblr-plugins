#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrvocals::ui
{
    /** What the Key Follow strip shows; filled by the editor from the processor (message thread). */
    struct KeyFollowView
    {
        int detectedKey = -1;     // 0..23 (C..B major, then minor) or -1
        int candidateKey = -1;    // best key right now (may differ while the hysteresis holds)
        int appliedKey = -1;      // what Keyed correction uses (-1: Key Follow is not overriding)
        float confidence = 0.0f;  // 0..1
        int activeSource = 0;     // 0 holding / silent, 1 sidechain, 2 vocal
        bool sidechainConnected = false, sidechainSignal = false;
    };

    /**
        Key Follow (pitch page, under the KEYED | CHROMATIC bar): a FOLLOW pill toggle, the detected
        key with a confidence bar, LOCK, Sensitivity and Source, and a one-line status. Every control
        is an APVTS attachment whose component ID is its parameter ID (key_follow, key_lock,
        key_sens, key_source). The readout is driven by refresh() from the editor's timer.
    */
    class KeyFollowStrip final : public juce::Component
    {
    public:
        explicit KeyFollowStrip (juce::AudioProcessorValueTreeState& state);
        ~KeyFollowStrip() override;

        void refresh (const KeyFollowView& view);
        const KeyFollowView& getView() const noexcept { return view; }

        juce::String getKeyText() const;     // "E major", or an em dash
        juce::String getStatusText() const;  // e.g. "Following sidechain"

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int preferredHeight = 28;

    private:
        juce::AudioProcessorValueTreeState& state;
        KeyFollowView view;
        juce::ToggleButton followButton { "Key Follow" }, lockButton { "Lock" };
        juce::Slider sensSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::ComboBox sourceBox;
        juce::Rectangle<int> readoutArea, barArea, sensCaptionArea, statusArea;

        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        std::unique_ptr<ButtonAttachment> followAttachment, lockAttachment;
        std::unique_ptr<SliderAttachment> sensAttachment;
        std::unique_ptr<ComboAttachment> sourceAttachment;

        bool isOn() const;
        int sourceChoice() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyFollowStrip)
    };
}
