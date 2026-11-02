#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/FeedbackGraph.h"
#include "UI/NotchTable.h"
#include "UI/OutputMeter.h"
#include "UI/Theme.h"

namespace dooblrfeedback
{
    namespace ui
    {
        /** A caption over a combo box, laid out like a ControlPanel combo cell (for a setting
            that is not a parameter, such as Analyzer Speed). */
        struct CaptionedCombo final : public juce::Component
        {
            juce::Label caption;
            juce::ComboBox box;

            CaptionedCombo()
            {
                caption.setFont (juce::FontOptions (12.0f));
                caption.setColour (juce::Label::textColourId, Palette::textMuted);
                caption.setJustificationType (juce::Justification::centred);
                caption.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (caption);
                addAndMakeVisible (box);
            }

            void resized() override
            {
                auto area = getLocalBounds();
                caption.setBounds (area.removeFromTop (16));
                box.setBounds (area.removeFromTop (28).reduced (0, 1));
            }
        };
    }

    /**
        Left column: name, version, Bypass, the Safe / Aggressive switch, the large HOLD button,
        Auto / Ring-out / SC, the big Sensitivity knob and Max cut, latency and status. Right of it the toolbar (preset
        prev / list / next / menu, undo / redo, A / B compare and copy, Clear all), the graph with
        the output meter, and a bottom section with three views: the notch table, the room notes
        and the Advanced panel. Every parameter has exactly one control.
    */
    class DooblrFeedbackEditor final : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::ChangeListener
    {
    public:
        explicit DooblrFeedbackEditor (DooblrFeedbackProcessor&);
        ~DooblrFeedbackEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        enum class View { notches, notes, advanced };
        void setView (View view);
        View getView() const noexcept { return view; }

        // For tests.
        juce::Component* findControl (const juce::String& parameterId) const;
        ui::ControlPanel& getAdvancedPanel() noexcept { return *advancedPanel; }
        ui::FeedbackGraph& getGraph() noexcept { return graph; }
        ui::NotchTable& getNotchTable() noexcept { return notchTable; }
        juce::TextEditor& getNotesEditor() noexcept { return notesEditor; }
        juce::ComboBox& getAnalyzerSpeedBox() noexcept { return analyzerSpeed.box; }
        juce::TextButton& getViewTab (View v) noexcept { return viewTabs[(size_t) v]; }
        juce::TextButton& getClearAllButton() noexcept { return clearAllButton; }
        juce::Label& getStatusLabel() noexcept { return statusLabel; }
        juce::Label& getSafetyBadge() noexcept { return safetyBadge; }
        ui::OutputMeter& getOutputMeter() noexcept { return outputMeter; }
        void stepPreset (int delta);
        void refresh();

    private:
        void timerCallback() override { refresh(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void renamePreset();
        void updateUndoButtons();
        void updateCompareButtons();
        void showPresetMenu();
        void setStatus (const juce::String& text);
        void selectNotch (int notchId);
        bool valueOfBool (const juce::String& parameterId) const;
        void syncNotes();

        DooblrFeedbackProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, latencyLabel, sensitivityCaption, maxCutCaption, bottomTitle;
        juce::Label safetyBadge; // "SAFETY -x dB" in the toolbar while the safety duck is pulling the level down
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, presetPrev { "<" }, presetNext { ">" }, presetMenu { "..." };
        juce::TextButton compareA { "A" }, compareB { "B" }, compareCopy { "A>B" }, clearAllButton { "Clear all" };
        juce::TextButton holdButton { "HOLD" }, autoButton { "AUTO" }, ringOutButton { "RING-OUT" }, sidechainButton { "SC" };
        juce::ToggleButton bypassButton { "Bypass" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment, autoAttachment, ringOutAttachment, sidechainAttachment, bypassAttachment;
        juce::ComboBox presetBox;
        ui::OutputMeter outputMeter;
        ui::SegmentedToggle modeToggle;

        juce::Slider sensitivityKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Slider maxCutKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

        ui::FeedbackGraph graph;
        ui::NotchTable notchTable;
        juce::TextEditor notesEditor;
        juce::String syncedNotes;   // the notes text last exchanged with the processor
        std::unique_ptr<ui::ControlPanel> advancedPanel;
        ui::CaptionedCombo analyzerSpeed;
        std::array<juce::TextButton, 3> viewTabs;
        View view = View::notches;

        juce::Rectangle<int> columnArea, toolbarArea, tabsArea, bottomArea;
        juce::Array<juce::File> userPresetFiles;
        juce::String shownPresetName;
        bool updatingPresetBox = false;
        std::unique_ptr<juce::FileChooser> fileChooser;
        std::unique_ptr<juce::AlertWindow> renameWindow;
        juce::uint32 statusUntilMs = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrFeedbackEditor)
    };
}
