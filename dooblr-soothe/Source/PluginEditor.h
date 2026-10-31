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
        Left column: name, version, Bypass, the Soft / Hard switch, the big Amount knob (the
        depth parameter) and Sharpness, latency and status. Right of it a thin strip of
        dot-sliders (stereo mode, link, focus), then the toolbar (Sidechain, Delta, preset
        prev / list / next / menu, undo / redo, A / B compare and copy, Advanced), the graph
        with the output meter, and a bottom row that shows either the selected band (tabs 1-8,
        its controls, Listen) or the Advanced panel (detection timing, output and quality).
        Every parameter has exactly one control.
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
        /** Bottom row: the band view or the Advanced panel (the Advanced button switches). */
        void setAdvancedShown (bool shouldShow);
        bool isAdvancedShown() const noexcept { return advancedShown; }
        /** Steps through the preset list (factory, then user files), wrapping. */
        void stepPreset (int delta);
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
        void updateCompareButtons();
        void showPresetMenu();
        static void setupStripSlider (juce::Slider& slider, juce::RangedAudioParameter& parameter);
        void setStatus (const juce::String& text);

        DooblrSootheProcessor& processor;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, latencyLabel, listenCaption, amountCaption, sharpnessCaption;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, presetPrev { "<" }, presetNext { ">" }, presetMenu { "..." };
        juce::TextButton compareA { "A" }, compareB { "B" }, compareCopy { "A>B" }, advancedButton { "Advanced" };
        juce::ComboBox presetBox;
        ui::OutputMeter outputMeter;

        ui::SegmentedToggle modeToggle;
        juce::ToggleButton sidechainButton { "SC" }, deltaButton { "Delta" }, bypassButton { "Bypass" }, listenButton { "Listen" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment, deltaAttachment, bypassAttachment;

        juce::Slider amountKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Slider sharpnessKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::array<juce::Slider, 3> stripSliders;
        std::array<juce::String, 3> stripNames { "stereo", "link", "focus" };
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

        ui::ReductionGraph graph;
        std::unique_ptr<ui::ControlPanel> advancedPanel;
        std::array<std::unique_ptr<ui::ControlPanel>, params::numBands> bandPanels;
        std::array<juce::TextButton, params::numBands> bandTabs;
        int selectedBand = 1;
        bool advancedShown = false;

        juce::Rectangle<int> columnArea, stripArea, toolbarArea, tabsArea;
        juce::Array<juce::File> userPresetFiles;
        juce::String shownPresetName;
        bool updatingPresetBox = false;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrSootheEditor)
    };
}
