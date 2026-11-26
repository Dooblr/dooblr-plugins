#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/Meters.h"
#include "UI/Theme.h"

namespace dooblrl
{
    class DooblrLEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private juce::ChangeListener
    {
    public:
        explicit DooblrLEditor (DooblrLProcessor&);
        ~DooblrLEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        juce::Component* findControl (const juce::String& parameterId) const;
        ui::GrHistory& getGrHistory() noexcept { return grHistory; }
        ui::LoudnessPanel& getLoudnessPanel() noexcept { return loudness; }
        ui::SegmentedToggle& getStyleStrip() noexcept { return *styleStrip; }
        juce::TextButton& getLearnButton() noexcept { return learnButton; }
        juce::TextButton& getLearnRevertButton() noexcept { return learnRevertButton; }
        void pollMeters();
        void refreshLearnUi();

        static constexpr int defaultWidth = 1080, defaultHeight = 660;
        static constexpr int minWidth = 940, minHeight = 600, maxWidth = 2400, maxHeight = 1400;

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        std::array<dsp::HistoryFrame, 128> historyScratch {};
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateUndoButtons();
        void updateDimmed();
        void updateLearnUi();
        void toggleLearn();

        DooblrLProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel, styleCaption;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::ToggleButton bypassButton { "Bypass" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

        std::unique_ptr<ui::SegmentedToggle> styleStrip;
        juce::TextButton learnButton { "Learn" }, learnRevertButton { "Revert" };
        class LearnBar final : public juce::Component
        {
        public:
            void setProgress (float p, bool active)
            {
                progress = juce::jlimit (0.0f, 1.0f, p);
                listening = active;
                repaint();
            }
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0, 6);
                g.setColour (ui::Palette::divider);
                g.fillRoundedRectangle (r, 3.0f);
                if (progress > 0.0f)
                {
                    g.setColour ((listening ? ui::Palette::frozen : ui::Palette::accent).withAlpha (listening ? 0.5f : 1.0f));
                    g.fillRoundedRectangle (r.withWidth (r.getWidth() * progress), 3.0f);
                }
            }
        private:
            float progress = 0;
            bool listening = false;
        } learnBar;
        ui::ControlPanel prePanel, enhancePanel, linkPanel, timePanel, outPanel;
        ui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
        ui::GrHistory grHistory;
        ui::LoudnessPanel loudness;

        juce::Rectangle<int> headerArea, styleArea, meterArea, preArea, enhanceArea, linkArea, timeArea, outArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName, transientStatus;
        juce::uint32 transientStatusUntil = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrLEditor)
    };
}
