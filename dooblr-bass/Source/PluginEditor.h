#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/Displays.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"

namespace dooblrbass
{
    /**
        One-page Mini-style board (design/dooblr-design-system.md): a 60 px header (title, version,
        undo / redo, presets, output) and a single panel — oscillator, mixer, filter, envelope and
        LFO columns in signal-flow order, with a compact graph strip along the top. No page column.
        Ignored controls are dimmed, never hidden. Cmd-Z undoes, Cmd-Shift-Z / Cmd-Y redo; every
        other key goes to the host.
    */
    class DooblrBassEditor final : public juce::AudioProcessorEditor,
                                   private juce::Timer,
                                   private juce::ChangeListener
    {
    public:
        explicit DooblrBassEditor (DooblrBassProcessor&);
        ~DooblrBassEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        const std::vector<std::unique_ptr<ui::ControlPanel>>& getPanels() const { return panels; }
        const std::vector<std::unique_ptr<ui::SegmentedToggle>>& getModeToggles() const { return modeToggles; }
        const std::vector<std::unique_ptr<juce::ToggleButton>>& getSwitchButtons() const { return switchButtons; }
        ui::OscillatorDisplay& getOscGraph() const { return *oscGraph; }
        ui::FilterDisplay& getFilterGraph() const { return *filterGraph; }
        ui::EnvelopeDisplay& getEnvGraph() const { return *envGraph; }
        ui::LfoDisplay& getLfoGraph() const { return *lfoGraph; }
        juce::TextButton& getRandomButton() noexcept { return randomButton; }
        juce::String getStatusText() const { return statusLabel.getText(); }
        juce::Rectangle<int> getContentArea() const noexcept { return contentArea; }
        void updateEnablement();

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildBoard();
        ui::ControlPanel& addPanel (const juce::String& title, juce::Colour colour, const std::vector<ui::ControlPanel::Spec>& specs);
        void addModeToggle (const juce::String& caption, const juce::String& parameterId, const juce::StringArray& labels, juce::Colour colour);
        void addSwitch (const juce::String& parameterId, const juce::String& label);
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateStatus();
        void updateUndoButtons();
        void randomise();
        void layoutBoard();

        DooblrBassProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, randomButton { "Random" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        std::unique_ptr<ui::OscillatorDisplay> oscGraph;
        std::unique_ptr<ui::FilterDisplay> filterGraph;
        std::unique_ptr<ui::EnvelopeDisplay> envGraph;
        std::unique_ptr<ui::LfoDisplay> lfoGraph;

        std::vector<std::unique_ptr<ui::ControlPanel>> panels;
        std::vector<std::unique_ptr<ui::SegmentedToggle>> modeToggles;
        juce::StringArray modeToggleCaptions;
        std::vector<int> modeToggleWidths;
        std::vector<std::unique_ptr<juce::ToggleButton>> switchButtons;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> switchAttachments;

        juce::Rectangle<int> headerArea, modeBarArea, contentArea, graphStripArea;
        std::vector<juce::Rectangle<int>> toggleCaptionAreas;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrBassEditor)
    };
}
