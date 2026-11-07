#pragma once

#include "../PluginProcessor.h"
#include "WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblrgenerate::ui
{
    /**
        Drop zone for a source sample used by Variation. Accepts audio files from Finder / Live
        (and internal history drags). Shows a mini waveform, name, length, Play and clear when
        loaded; "Drop sample" when empty. Amount is the Variation Amount parameter.
    */
    class SourceDropZone final : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 public juce::DragAndDropTarget
    {
    public:
        explicit SourceDropZone (DooblrGenerateProcessor&);
        ~SourceDropZone() override = default;

        void refresh();

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragMove (const juce::StringArray&, int, int) override { }
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isDragHighlighted() const noexcept { return highlighted; }
        WaveformView& getWaveform() noexcept { return waveform; }
        juce::TextButton& getPlayButton() noexcept { return playButton; }
        juce::TextButton& getClearButton() noexcept { return clearButton; }
        juce::Slider& getAmountSlider() noexcept { return amount; }
        juce::Label& getNameLabel() noexcept { return nameLabel; }
        juce::Label& getEmptyLabel() noexcept { return emptyLabel; }
        juce::Label& getStatusLabel() noexcept { return statusLabel; }

    private:
        void setHighlighted (bool);
        void loadPath (const juce::String& path);

        DooblrGenerateProcessor& generator;
        WaveformView waveform;
        juce::Label emptyLabel, nameLabel, amountCaption, statusLabel;
        juce::TextButton playButton { "Play" }, clearButton { "x" };
        juce::Slider amount;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
        bool highlighted = false;
        int lastSourceSerial = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceDropZone)
    };
}
