#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/Displays.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"

namespace dooblrprophet
{
    /**
        The editor (design/dooblr-design-system.md): a 60 px header (title, version, undo / redo,
        presets, output), a 150 px page column, and per page a title bar with the page's mode
        switches, a graph and the control panels. Ignored controls are dimmed (disabled), never
        hidden. Cmd-Z undoes, Cmd-Shift-Z / Cmd-Y redo; every other key goes to the host.
    */
    class DooblrProphetEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::ChangeListener
    {
    public:
        enum Page { oscillatorsPage = 0, filtersPage, envelopesPage, modulationPage, voicesPage, numPages };

        explicit DooblrProphetEditor (DooblrProphetProcessor&);
        ~DooblrProphetEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        static const char* pageName (int page);

        // For the tests
        int getSelectedPage() const noexcept { return selectedPage; }
        void selectPage (int page);
        const std::vector<std::unique_ptr<ui::ControlPanel>>& getPanels (int page) const { return pages[(size_t) page].panels; }
        const std::vector<std::unique_ptr<ui::SegmentedToggle>>& getModeToggles (int page) const { return pages[(size_t) page].toggles; }
        ui::GraphView& getGraph (int page) const { return *pages[(size_t) page].graph; }
        juce::TextButton& getVoicePresetButton (int index) { return voicePresetButtons[(size_t) index]; }
        juce::String getStatusText() const { return statusLabel.getText(); }
        juce::Rectangle<int> getContentArea() const noexcept { return contentArea; }
        /** Re-evaluates which controls are ignored by the current settings (the timer does this). */
        void updateEnablement();

    private:
        struct PageContent
        {
            std::unique_ptr<ui::GraphView> graph;
            std::vector<std::unique_ptr<ui::ControlPanel>> panels;
            std::vector<std::vector<int>> rows;      // panel indices per row
            std::vector<std::unique_ptr<ui::SegmentedToggle>> toggles;
            juce::StringArray toggleCaptions;
            std::vector<int> toggleWidths;
        };

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildPages();
        ui::ControlPanel& addPanel (int page, const juce::String& title, const std::vector<ui::ControlPanel::Spec>& specs);
        void addToggle (int page, const juce::String& caption, const juce::String& parameterId, const juce::StringArray& labels);
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updatePageStatus();
        void updateUndoButtons();
        void setVoices (int count);
        void layoutPage (int page);

        DooblrProphetProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        std::array<juce::TextButton, numPages> pageButtons;
        std::array<PageContent, numPages> pages;
        std::array<juce::TextButton, 3> voicePresetButtons;
        int selectedPage = oscillatorsPage;

        juce::Rectangle<int> headerArea, navArea, titleBarArea, contentArea;
        std::vector<juce::Rectangle<int>> toggleCaptionAreas;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrProphetEditor)
    };
}
