#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/OutputMeter.h"
#include "UI/ReductionGraph.h"
#include "UI/Theme.h"

namespace dooblrsoothe
{
    /**
        Header (title, version, undo / redo, presets, Output, meter), a toolbar (Mode, Quality,
        Sidechain, Delta, Bypass, latency), the reduction graph, then two rows of panels:
        Detection / Stereo / Output, and the selected depth-curve band (tabs 1-8, its controls
        and the shared Listen switch). Every parameter has exactly one control.
    */
    class DooblrSootheEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
    {
    public:
        explicit DooblrSootheEditor (DooblrSootheProcessor&);
        ~DooblrSootheEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        // For tests.
        ui::ControlPanel* getPanelFor (const juce::String& parameterId) const;
        juce::Component* findControl (const juce::String& parameterId) const;
        std::vector<ui::ControlPanel*> getPanels() const;
        ui::ReductionGraph& getGraph() noexcept { return graph; }
        int getSelectedBand() const noexcept { return selectedBand; }
        void selectBand (int band);
        juce::TextButton& getBandTab (int band) { return bandTabs[(size_t) band]; }
        juce::ToggleButton& getListenButton() noexcept { return listenButton; }
        /** Refreshes enablement, undo buttons and labels now (the timer does this). */
        void refresh();

    private:
        void timerCallback() override { refresh(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void updateUndoButtons();
        void setStatus (const juce::String& text);

        DooblrSootheProcessor& processor;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel, latencyLabel, qualityCaption, listenCaption;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        ui::SegmentedToggle modeToggle;
        juce::ComboBox qualityBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
        juce::ToggleButton sidechainButton { "Sidechain" }, deltaButton { "Delta" }, bypassButton { "Bypass" }, listenButton { "Listen" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment, deltaAttachment, bypassAttachment;

        ui::ReductionGraph graph;
        std::unique_ptr<ui::ControlPanel> detectionPanel, stereoPanel, outputPanel;
        std::array<std::unique_ptr<ui::ControlPanel>, params::numBands> bandPanels;
        std::array<juce::TextButton, params::numBands> bandTabs;
        int selectedBand = 1;

        juce::Rectangle<int> headerArea, toolbarArea, bandRowArea, tabsArea;
        juce::Array<juce::File> userPresetFiles;
        juce::String shownPresetName;
        bool updatingPresetBox = false;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrSootheEditor)
    };
}
