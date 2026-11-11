#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/OutputMeter.h"
#include "UI/SubDisplay.h"
#include "UI/Theme.h"
#include "UI/VerdictPanel.h"

namespace dooblrsub
{
    class DooblrSubEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::ChangeListener
    {
    public:
        explicit DooblrSubEditor (DooblrSubProcessor&);
        ~DooblrSubEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        juce::Component* findControl (const juce::String& parameterId) const;

        static constexpr int defaultWidth = 1080, defaultHeight = 660;
        static constexpr int minWidth = 940, minHeight = 600, maxWidth = 2400, maxHeight = 1400;

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateUndoButtons();

        DooblrSubProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, outputCaption;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::ToggleButton bypassButton { "Bypass" };
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

        ui::ControlPanel subPanel, tonePanel, systemPanel;
        ui::SubDisplay display;
        ui::VerdictPanel verdict;
        ui::OutputMeter meter;

        juce::Rectangle<int> headerArea, displayArea, verdictArea, subArea, toneArea, systemArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName, transientStatus;
        juce::uint32 transientStatusUntil = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrSubEditor)
    };
}
