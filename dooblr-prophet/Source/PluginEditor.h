#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/FrontPanel.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"

namespace dooblrprophet
{
    /**
        The editor (design/dooblr-design-system.md): a 60 px header (title, version, undo / redo,
        presets, output), then, since v0.1.1, one front panel with every sound control on it, in
        the classic five-voice polysynth order:

            top row:    POLY-MOD | LFO / WHEEL-MOD | OSCILLATOR A | OSCILLATOR B
            bottom row: MIXER | FILTER | FILTER ENVELOPE | AMP ENVELOPE | VOICES

        Mode switches (oscillator Variable / Switched, LFO Free / Sync, filter model, voice mode)
        sit in their section's title line. Ignored controls are dimmed (disabled), never hidden.
        Cmd-Z undoes, Cmd-Shift-Z / Cmd-Y redo; every other key goes to the host.
    */
    class DooblrProphetEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::ChangeListener
    {
    public:
        explicit DooblrProphetEditor (DooblrProphetProcessor&);
        ~DooblrProphetEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        // For the tests
        const std::vector<std::unique_ptr<ui::Section>>& getSections() const noexcept { return sections; }
        /** Section indices of the top and bottom rows, left to right. */
        const std::array<std::vector<int>, 2>& getRows() const noexcept { return rows; }
        juce::Component* findControl (const juce::String& parameterId) const;
        juce::TextButton& getRandomButton() noexcept { return randomButton; }
        juce::TextButton& getVoicePresetButton (int index) { return voicePresetButtons[(size_t) index]; }
        juce::String getStatusText() const { return statusLabel.getText(); }
        juce::Rectangle<int> getPanelArea() const noexcept { return panelArea; }
        /** Re-evaluates which controls are ignored by the current settings (the timer does this). */
        void updateEnablement();

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildPanel();
        ui::Section& addSection (int row, const juce::String& title, juce::Colour colour, int columns,
                                 const std::vector<ui::Section::Row>& sectionRows);
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateStatus();
        void updateUndoButtons();
        void setVoices (int count);
        void randomise();

        DooblrProphetProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel, voicesCaption;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, randomButton { "Random" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        std::array<juce::TextButton, 3> voicePresetButtons;
        std::vector<std::unique_ptr<ui::Section>> sections;
        std::array<std::vector<int>, 2> rows;

        juce::Rectangle<int> headerArea, panelArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrProphetEditor)
    };
}
