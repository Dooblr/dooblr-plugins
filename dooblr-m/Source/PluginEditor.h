#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/BoardControls.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"

namespace dooblrm
{
    /**
        One-page Model D-style board in the dooblr aesthetic (design/dooblr-design-system.md):
        60 px header, a slim voice strip, then left-to-right Controllers / Oscillator Bank /
        Mixer / Modifiers / Output, with pitch and mod wheels on the far left.
    */
    class DooblrMEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private juce::ChangeListener
    {
    public:
        explicit DooblrMEditor (DooblrMProcessor&);
        ~DooblrMEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        juce::Component* findControl (const juce::String& parameterId) const;
        juce::Rectangle<int> getContentArea() const noexcept { return contentArea; }
        ui::FilterReadout& getFilterReadout() noexcept { return filterReadout; }
        ui::CutoffContourIndicator& getCutoffContourIndicator() noexcept { return cutoffContour; }
        ui::LfoPulse& getLfoPulse() noexcept { return lfoPulse; }
        ui::ScopeView& getScope() noexcept { return scope; }
        const std::vector<std::unique_ptr<ui::SectionPanel>>& getSections() const { return sections; }
        const std::vector<std::unique_ptr<ui::RotarySwitch>>& getRotaries() const { return rotaries; }

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildBoard();
        ui::Knob& addKnob (juce::Component& parent, const juce::String& id, const juce::String& caption,
                           juce::Colour fill = ui::Palette::accent, bool large = false);
        ui::RotarySwitch& addSwitch (juce::Component& parent, const juce::String& id, const juce::String& caption,
                                     const juce::StringArray& ticks);
        ui::BatToggle& addToggle (juce::Component& parent, const juce::String& id, const juce::String& caption,
                                  const juce::String& downLabel = {}, const juce::String& upLabel = {});
        void addChoiceControl (juce::Component& parent, const juce::String& id, const juce::String& caption,
                               const juce::StringArray& ticks);
        ui::Wheel& addWheel (juce::Component& parent, const juce::String& id, const juce::String& caption, bool spring);
        ui::SectionPanel& addSection (const juce::String& title, juce::Colour colour);
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateStatus();
        void updateUndoButtons();
        void updateEnablement();
        void layoutBoard();

        DooblrMProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" },
                         saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        std::vector<std::unique_ptr<ui::SectionPanel>> sections;
        std::vector<std::unique_ptr<ui::Knob>> knobs;
        ui::CutoffContourIndicator cutoffContour;
        ui::LfoPulse lfoPulse;
        std::vector<std::unique_ptr<ui::RotarySwitch>> rotaries;
        std::vector<std::unique_ptr<ui::BatToggle>> toggles;
        std::vector<std::unique_ptr<ui::Wheel>> wheels;
        ui::ScopeView scope;
        ui::FilterReadout filterReadout;

        juce::Rectangle<int> headerArea, voiceBarArea, contentArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrMEditor)
    };
}
