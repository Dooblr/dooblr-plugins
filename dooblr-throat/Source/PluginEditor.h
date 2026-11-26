#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"
#include "UI/VowelPad.h"

namespace dooblrthroat
{
    /**
        Editor (design/dooblr-design-system.md): a 60 px header (title, version,
        undo / redo, presets, output) and two pages. Main shows Phrase, Mode, Voice,
        Rate, Formant and Level. Advanced holds Engine and the Tract-engine panels
        (vowel pad, source, pitch, envelope). Cmd-Z undoes, Cmd-Shift-Z / Cmd-Y redo;
        every other key goes to the host.
    */
    class DooblrThroatEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
    {
    public:
        explicit DooblrThroatEditor (DooblrThroatProcessor&);
        ~DooblrThroatEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        const std::vector<std::unique_ptr<ui::ControlPanel>>& getPanels() const { return panels; }
        ui::VowelPad& getVowelPad() noexcept { return vowelPad; }
        juce::String getStatusText() const { return statusLabel.getText(); }
        int getPage() const noexcept { return advancedPage ? 1 : 0; }
        void setPage (int page);

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildBoard();
        ui::ControlPanel& addPanel (const juce::String& title, juce::Colour colour, const std::vector<ui::ControlPanel::Spec>& specs);
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateStatus();
        void updateUndoButtons();
        void layoutBoard();
        void commitPhraseText();
        void refreshPhraseEditor();
        void updatePhraseReadout();

        DooblrThroatProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel, vowelCaption;
        juce::Label phraseCaption, phraseReadout, modeCaption, rateCaption, engineCaption;
        juce::Label voiceCaption, formantCaption, levelCaption, hintLabel;
        juce::TextEditor phraseEditor;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::TextButton restartButton { "Reset" };
        juce::TextButton mainPageButton { "Main" }, advancedPageButton { "Advanced" };
        juce::ComboBox presetBox, modeBox, engineBox, voiceBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider rateSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider formantSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider levelSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> voiceAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> formantAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttachment;
        void layoutMainPage (juce::Rectangle<int> area);
        ui::OutputMeter outputMeter;
        ui::VowelPad vowelPad;

        std::vector<std::unique_ptr<ui::ControlPanel>> panels;

        juce::Rectangle<int> headerArea, contentArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;
        bool advancedPage = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrThroatEditor)
    };
}
