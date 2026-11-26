#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "UI/OutputMeter.h"
#include "UI/ReverbDisplay.h"
#include "UI/Theme.h"

namespace dooblrreverb
{
    /**
        Header: brand bar, name and version; the preset toolbar (< list > menu), Undo / Redo and
        A / B compare (design §12.3). Knob row: Brightness, Character, Distance, then the big
        Space knob with the decay time and room name, then Decay Rate, Stereo Width and Mix.
        The display (Decay Rate EQ + Post EQ over the spectrum) with the output meter. Bottom
        bar: Analyzer, Predelay (ms or synced division), Input, Output and Bypass.
        Every parameter has exactly one control (the EQ bands live in the display).
    */
    class DooblrReverbEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
    {
    public:
        explicit DooblrReverbEditor (DooblrReverbProcessor&);
        ~DooblrReverbEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        // For tests.
        juce::Component* findControl (const juce::String& parameterId) const;
        ui::ReverbDisplay& getDisplay() noexcept { return display; }
        juce::ComboBox& getAnalyzerBox() noexcept { return analyzerBox; }
        juce::String getDecayReadout() const { return decayReadout.getText(); }
        juce::String getRoomReadout() const { return roomReadout.getText(); }
        void stepPreset (int delta);
        void refresh();

    private:
        void timerCallback() override { refresh(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void initPreset();
        void showPresetMenu();
        void updateUndoButtons();
        void updateCompareButtons();
        void setupKnob (juce::Slider& knob, const juce::String& pid, juce::Colour colour, bool textBox = true);
        void setupBarSlider (juce::Slider& slider, const juce::String& pid, juce::Colour colour);

        DooblrReverbProcessor& owner;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, decayReadout, roomReadout;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, presetPrev { "<" }, presetNext { ">" },
            presetMenu { juce::String (juce::CharPointer_UTF8 ("\xe2\x98\xb0")) };
        juce::TextButton compareA { "A" }, compareB { "B" }, compareCopy { "A>B" };
        juce::ComboBox presetBox;

        static constexpr int numKnobs = 7;
        std::array<juce::Slider, numKnobs> knobs;
        std::array<juce::Label, numKnobs> knobCaptions;
        std::array<juce::String, numKnobs> knobIds;

        ui::ReverbDisplay display;
        ui::OutputMeter outputMeter;

        juce::Label analyzerCaption, predelayCaption, inputCaption, outputCaption;
        juce::ComboBox analyzerBox, divisionBox;
        juce::Slider predelaySlider, inputSlider, outputSlider;
        juce::ToggleButton syncButton { "Sync" }, bypassButton { "Bypass" };

        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, bypassAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;

        juce::Rectangle<int> headerArea, knobArea, barArea, heroArea;
        juce::Array<juce::File> userPresetFiles;
        juce::String shownPresetName;
        bool updatingPresetBox = false;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrReverbEditor)
    };
}
