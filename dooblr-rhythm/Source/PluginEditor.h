#pragma once

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/OutputMeter.h"
#include "UI/SequencerGrid.h"
#include "UI/Theme.h"

#include <memory>
#include <vector>

namespace dooblrrhythm
{
    class DooblrRhythmEditor final : public juce::AudioProcessorEditor,
                                     public juce::DragAndDropContainer,
                                     private juce::Timer,
                                     private juce::ChangeListener,
                                     private juce::FileDragAndDropTarget
    {
    public:
        explicit DooblrRhythmEditor (DooblrRhythmProcessor&);
        ~DooblrRhythmEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::Component* findControl (const juce::String& parameterId) const;
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;

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
        void updateBpm();
        void showInstrumentPanel (int index);
        void startExternalDrag (const juce::File& file);

        DooblrRhythmProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, outputCaption, bpmLabel, bpmValue;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::TextButton generateButton { "Generate" }, variationButton { "Variation" }, regenButton { "Regen" };
        juce::TextButton playButton { "Play" }, dragMidiButton { "Drag MIDI" }, dragWavButton { "Drag WAV" };
        juce::TextButton loadSampleButton { "Load WAV" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

        ui::ControlPanel generatePanel, macrosPanel;
        std::vector<std::unique_ptr<ui::ControlPanel>> instrumentPanels;
        ui::SequencerGrid grid;
        ui::OutputMeter meter;

        juce::Rectangle<int> headerArea, generateArea, macrosArea, gridArea, detailArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName, transientStatus;
        juce::uint32 transientStatusUntil = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;
        int selectedInstrument = 0;
        bool dragFromMidi = false, dragFromWav = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrRhythmEditor)
    };
}
