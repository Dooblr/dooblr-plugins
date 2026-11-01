#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/FeedbackEngine.h"

namespace dooblrfeedback
{
    class DooblrFeedbackProcessor;
}

namespace dooblrfeedback::ui
{
    /**
        The notch list, in frequency order (the numbers match the graph's markers): frequency,
        nearest note and cents, current cut, deepest cut so far, Q, the howl level when detected,
        time detected, hits and fixed / floating, with a Lock (fixed) and a Clear button per row.
    */
    class NotchTable final : public juce::Component, private juce::TableListBoxModel
    {
    public:
        explicit NotchTable (DooblrFeedbackProcessor& processor);
        ~NotchTable() override;

        /** Re-reads the notch bank (the editor's timer calls this). */
        void refresh();
        int getNumRows() override { return (int) rows.size(); }
        const std::vector<dsp::Notch>& getRows() const noexcept { return rows; }
        juce::TableListBox& getTable() noexcept { return table; }

        void selectNotch (int notchId);
        std::function<void (int)> onNotchSelected;
        /** The notch under the mouse (0 when none): the graph opens its label. */
        std::function<void (int)> onNotchHovered;
        int getHoveredNotch() const noexcept { return hoveredNotch; }
        /** Called after a row button changed the bank. */
        std::function<void()> onBankChanged;

        enum Column { colNumber = 1, colFreq, colNote, colCut, colNeeded, colQ, colLevel, colDetected, colHits, colType, colLock, colClear };

        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        void setHovered (int notchId);
        void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
        juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
        void selectedRowsChanged (int lastRowSelected) override;
        juce::String cellText (const dsp::Notch& notch, int row, int column) const;

        DooblrFeedbackProcessor& processor;
        juce::TableListBox table;
        std::vector<dsp::Notch> rows;
        bool updating = false;
        int hoveredNotch = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NotchTable)
    };
}
