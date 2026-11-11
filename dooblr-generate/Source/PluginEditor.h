#pragma once

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/HistoryList.h"
#include "UI/Theme.h"
#include "UI/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
    One page: a prompt bar (prompt, seed, length, Sync, Generate, Variation) with the job's progress
    under it; the current sample's waveform (drag it out to Ableton) with Play and its details;
    the history of generations on the right; PLAYBACK knobs and the MODEL panel along the bottom.
*/
class DooblrGenerateEditor final : public juce::AudioProcessorEditor,
                                   public juce::DragAndDropContainer,
                                   private juce::Timer
{
public:
    explicit DooblrGenerateEditor (DooblrGenerateProcessor&);
    ~DooblrGenerateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files,
                                               bool& canMoveFiles) override;

    static constexpr int minWidth = 940, minHeight = 600, defaultWidth = 1080, defaultHeight = 660,
                         maxWidth = 2400, maxHeight = 1400;

    //==============================================================================
    // For tests: the controls, and refreshing as the timer would.
    juce::TextEditor& getPromptEditor() noexcept { return prompt; }
    juce::TextEditor& getSeedEditor() noexcept { return seed; }
    juce::Slider& getLengthSlider() noexcept { return length; }
    juce::ComboBox& getLengthChoice() noexcept { return lengthChoice; }
    juce::ToggleButton& getSyncButton() noexcept { return syncButton; }
    juce::Label& getLengthCaption() noexcept { return lengthCaption; }
    juce::TextButton& getGenerateButton() noexcept { return generateButton; }
    juce::TextButton& getVariationButton() noexcept { return variationButton; }
    juce::TextButton& getPlayButton() noexcept { return playButton; }
    juce::TextButton& getDragButton() noexcept { return dragButton; }
    juce::ComboBox& getModelBox() noexcept { return modelBox; }
    juce::TextButton& getDownloadButton() noexcept { return downloadButton; }
    juce::TextButton& getTokenButton() noexcept { return tokenButton; }
    juce::TextButton& getFolderButton() noexcept { return folderButton; }
    juce::TextButton& getCopyButton() noexcept { return copyButton; }
    juce::TextEditor& getDetails() noexcept { return details; }
    juce::TextEditor& getTokenEditor() noexcept { return tokenEditor; }
    juce::Label& getStatusLabel() noexcept { return statusLabel; }
    juce::Label& getProgressLabel() noexcept { return progressLabel; }
    juce::Label& getInfoLabel() noexcept { return infoLabel; }
    dooblrgenerate::ui::WaveformView& getWaveform() noexcept { return waveform; }
    dooblrgenerate::ui::HistoryList& getHistoryList() noexcept { return historyList; }
    dooblrgenerate::ui::ControlPanel& getPlaybackPanel() noexcept { return *playbackPanel; }
    double getProgress() const noexcept { return progress; }
    juce::String getCopyText() const { return copyText; }
    void refresh() { timerCallback(); }

private:
    void timerCallback() override;
    void refreshModelBox();
    void refreshSample();
    void refreshJob();
    void refreshModel();
    void showTokenEntry (bool);
    void chooseFolder();
    void refreshLengthUi();
    void paintPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour) const;

    DooblrGenerateProcessor& generator;
    dooblrgenerate::ui::DooblrLookAndFeel lookAndFeel;

    juce::Label title, subtitle, modelCaption;
    juce::ComboBox modelBox;

    juce::TextEditor prompt, seed;
    juce::Label promptCaption, seedCaption, lengthCaption, syncCaption;
    juce::Slider length;
    juce::ComboBox lengthChoice;
    juce::ToggleButton syncButton { "Sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    juce::TextButton generateButton { "Generate" }, variationButton { "Variation" };
    juce::ProgressBar progressBar { progress };
    double progress = 0.0;
    juce::Label progressLabel;

    dooblrgenerate::ui::WaveformView waveform;
    juce::TextButton playButton { "Play" }, dragButton { "Drag WAV" };
    juce::Label infoLabel;

    dooblrgenerate::ui::HistoryList historyList;

    std::unique_ptr<dooblrgenerate::ui::ControlPanel> playbackPanel;

    juce::Label statusLabel;
    juce::TextButton downloadButton { "Download" }, tokenButton { "Token..." }, folderButton { "Folder..." }, copyButton { "Copy" };
    juce::TextEditor details, tokenEditor;
    juce::TextButton tokenSave { "Save" }, tokenCancel { "Cancel" };
    bool tokenEntryShown = false, dragFromButtonStarted = false;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, waveArea, historyArea, playbackArea, modelArea;
    int lastSampleSerial = -1, lastHistorySize = -1, lastJobSerial = -1;
    juce::String copyText;
    dooblrgenerate::gen::JobStatus::Result lastJobResult = dooblrgenerate::gen::JobStatus::Result::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrGenerateEditor)
};
