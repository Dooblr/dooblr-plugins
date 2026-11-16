#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <functional>

namespace dooblrrhythm::ui
{
    class SequencerGrid final : public juce::Component
    {
    public:
        explicit SequencerGrid (DooblrRhythmProcessor&);
        ~SequencerGrid() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        void setSelectedInstrument (int index);
        int getSelectedInstrument() const noexcept { return selected; }
        void setPlayheadStep (int step) { playhead = step; }
        void refresh();

        juce::Component* findControl (const juce::String& parameterId) const;
        juce::Rectangle<int> getGridBounds() const { return gridArea; }

        std::function<void (int)> onSelectInstrument;
        std::function<void (int)> onDragInstrumentMidi;

        static constexpr int rowH = 26, labelW = 78, btnW = 36;

    private:
        struct Row
        {
            juce::Label name;
            juce::ToggleButton mute { "M" }, solo { "S" }, lock { "L" };
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteA, soloA, lockA;
        };

        int rowAt (juce::Point<int>) const;
        int stepAt (juce::Point<int>) const;
        juce::Rectangle<int> cellBounds (int row, int step) const;
        void rebuildBackground();

        DooblrRhythmProcessor& proc;
        std::array<Row, numInstruments> rows;
        int selected = 0, playhead = -1, scrollY = 0, dragInst = -1, dragStep = -1, midiDragRow = -1;
        juce::Rectangle<int> gridArea, labelArea;
        juce::Image background;
        int bgW = 0, bgH = 0, bgSteps = 0, bgSeed = -1;
        Pattern lastPattern;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerGrid)
    };
}
