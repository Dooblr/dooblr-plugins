#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/Board.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"

namespace dooblrdistressor
{
    /**
        Hardware-style board in the dooblr look: header, ratio row + GR meter, detector / audio
        modes, four main knobs, then headroom, mix and I/O meters. Resize 940x600 .. 2400x1400.
    */
    class DooblrDistressorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer,
                                         private juce::ChangeListener
    {
    public:
        explicit DooblrDistressorEditor (DooblrDistressorProcessor&);
        ~DooblrDistressorEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        juce::Component* findControl (const juce::String& parameterId) const;
        ui::SegmentedToggle& getRatioStrip() noexcept { return *ratioStrip; }
        ui::SegmentedToggle& getDistToggle() noexcept { return *distToggle; }
        ui::GrLedMeter& getGrMeter() noexcept { return grMeter; }
        ui::DistLeds& getDistLeds() noexcept { return distLeds; }
        juce::Rectangle<int> getContentArea() const noexcept { return contentArea; }

        static constexpr int defaultWidth = 1080, defaultHeight = 660;
        static constexpr int minWidth = 940, minHeight = 600, maxWidth = 2400, maxHeight = 1400;

    private:
        struct Knob
        {
            juce::Label caption;
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        };

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateUndoButtons();
        Knob& addKnob (const juce::String& parameterId, const juce::String& caption, juce::Colour colour);
        void addToggle (const juce::String& parameterId, const juce::String& label);

        DooblrDistressorProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, statusLabel;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::ToggleButton bypassButton { "Bypass" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

        std::unique_ptr<ui::SegmentedToggle> ratioStrip, distToggle;
        ui::GrLedMeter grMeter;
        ui::DistLeds distLeds;
        std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::ComboBox headroomBox;
        juce::Label headroomCaption, mixCaption, detectorCaption, audioCaption, ratioCaption;
        juce::Slider mixSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> headroomAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
        ui::StereoMeter inMeter { "IN" }, outMeter { "OUT" };

        juce::Rectangle<int> headerArea, contentArea, ratioArea, modeArea, knobArea, bottomArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrDistressorEditor)
    };
}
