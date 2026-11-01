#include "NotchTable.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "Theme.h"

namespace dooblrfeedback::ui
{
    namespace
    {
        struct RowButton final : public juce::TextButton
        {
            int notchId = 0;
        };
    }

    NotchTable::NotchTable (DooblrFeedbackProcessor& p) : processor (p)
    {
        setComponentID ("notchTable");
        table.setModel (this);
        table.setRowHeight (22);
        table.setHeaderHeight (20);
        table.setColour (juce::ListBox::backgroundColourId, Palette::panel);
        table.setColour (juce::ListBox::outlineColourId, Palette::divider);
        table.setOutlineThickness (0);
        auto& header = table.getHeader();
        const auto flags = juce::TableHeaderComponent::visible;
        header.addColumn ("#", colNumber, 30, 24, 60, flags);
        header.addColumn ("Frequency", colFreq, 84, 60, 160, flags);
        header.addColumn ("Note", colNote, 74, 50, 140, flags);
        header.addColumn ("Cut", colCut, 64, 48, 120, flags);
        header.addColumn ("Needed", colNeeded, 64, 48, 120, flags);
        header.addColumn ("Q", colQ, 44, 34, 90, flags);
        header.addColumn ("Level", colLevel, 66, 50, 120, flags);
        header.addColumn ("Detected", colDetected, 74, 60, 140, flags);
        header.addColumn ("Hits", colHits, 44, 34, 90, flags);
        header.addColumn ("Type", colType, 62, 50, 120, flags);
        header.addColumn ({}, colLock, 64, 56, 100, flags);
        header.addColumn ({}, colClear, 60, 52, 100, flags);
        header.setStretchToFitActive (true);
        header.setColour (juce::TableHeaderComponent::backgroundColourId, Palette::panelRaised);
        header.setColour (juce::TableHeaderComponent::textColourId, Palette::textMuted);
        header.setColour (juce::TableHeaderComponent::outlineColourId, Palette::divider);
        rows.reserve (dsp::maxNotches);
        addAndMakeVisible (table);
        refresh();
    }

    NotchTable::~NotchTable()
    {
        table.setModel (nullptr);
    }

    void NotchTable::resized()
    {
        table.setBounds (getLocalBounds());
    }

    void NotchTable::refresh()
    {
        const auto all = processor.getNotches();
        std::vector<dsp::Notch> next;
        next.reserve (dsp::maxNotches);
        for (const auto& n : all)
            if (n.active) next.push_back (n);
        std::sort (next.begin(), next.end(), [] (const auto& a, const auto& b) { return a.freq < b.freq; });

        auto structural = next.size() != rows.size();
        for (size_t i = 0; ! structural && i < next.size(); ++i)
            structural = next[i].id != rows[i].id || next[i].fixed != rows[i].fixed;

        const auto selectedId = juce::isPositiveAndBelow (table.getSelectedRow(), (int) rows.size()) ? rows[(size_t) table.getSelectedRow()].id : 0;
        rows = std::move (next);
        if (structural)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            table.updateContent();
            table.deselectAllRows();
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].id == selectedId) table.selectRow ((int) i, false, true);
        }
        table.repaint();
    }

    void NotchTable::selectNotch (int notchId)
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == notchId)
            {
                table.selectRow ((int) i);
                return;
            }
        table.deselectAllRows();
    }

    void NotchTable::selectedRowsChanged (int lastRowSelected)
    {
        if (updating || onNotchSelected == nullptr) return;
        onNotchSelected (juce::isPositiveAndBelow (lastRowSelected, (int) rows.size()) ? rows[(size_t) lastRowSelected].id : 0);
    }

    void NotchTable::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
    {
        g.setColour (selected ? Palette::accent.withAlpha (0.22f) : (row % 2 == 0 ? Palette::panel : Palette::panel.brighter (0.03f)));
        g.fillRect (0, 0, width, height);
        if (juce::isPositiveAndBelow (row, (int) rows.size()))
        {
            g.setColour (rows[(size_t) row].fixed ? Palette::accent : Palette::dynamic);
            g.fillRect (0, 3, 2, height - 6);
        }
    }

    juce::String NotchTable::cellText (const dsp::Notch& n, int row, int column) const
    {
        switch (column)
        {
            case colNumber:   return juce::String (row + 1);
            case colFreq:     return params::formatValue (n.freq, n.freq < 1000.0f ? 1 : 0) + " Hz";
            case colNote:     return params::noteWithCents (n.freq);
            case colCut:      return params::formatValue (-n.cutDb, 1) + " dB";
            case colNeeded:   return params::formatValue (-n.neededDb, 1) + " dB";
            case colQ:        return params::formatValue (n.q, 1);
            case colLevel:    return n.levelDb > -200.0f ? params::formatValue (n.levelDb, 0) + " dBFS" : juce::String ("-");
            case colDetected: return n.detectedMs > 0 ? juce::Time (n.detectedMs).formatted ("%H:%M:%S") : juce::String ("-");
            case colHits:     return juce::String (n.hits);
            case colType:     return juce::String (n.fixed ? "Fixed" : "Floating") + (n.ringing ? " *" : "");
            default:          return {};
        }
    }

    void NotchTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || column == colLock || column == colClear)
            return;
        const auto& n = rows[(size_t) row];
        g.setFont (juce::FontOptions (12.0f, column == colFreq ? juce::Font::bold : juce::Font::plain));
        auto colour = Palette::textPrimary;
        if (column == colCut) colour = n.fixed ? Palette::accent : Palette::dynamic;
        if (column == colNumber || column == colDetected || column == colLevel) colour = Palette::textMuted;
        g.setColour (colour);
        g.drawFittedText (cellText (n, row, column), 6, 0, width - 10, height, juce::Justification::centredLeft, 1, 0.7f);
    }

    juce::Component* NotchTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
    {
        if (column != colLock && column != colClear)
        {
            delete existing;
            return nullptr;
        }
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        {
            delete existing;
            return nullptr;
        }

        auto* button = dynamic_cast<RowButton*> (existing);
        if (button == nullptr)
        {
            delete existing;
            button = new RowButton();
            button->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        }
        const auto& n = rows[(size_t) row];
        button->notchId = n.id;
        const auto lock = column == colLock;
        button->setComponentID ((lock ? "notchLock_" : "notchClear_") + juce::String (n.id));
        button->setButtonText (lock ? (n.fixed ? "Fixed" : "Lock") : "Clear");
        button->setClickingTogglesState (false);
        button->setToggleState (lock && n.fixed, juce::dontSendNotification);
        button->setTooltip (lock ? (n.fixed ? "Fixed: this notch stays until cleared. Click to let it float (release when quiet)"
                                            : "Lock this notch: keep it at its current depth until cleared")
                                 : "Clear this notch");
        const auto id = n.id;
        const auto fixed = n.fixed;
        button->onClick = [this, lock, id, fixed]
        {
            if (lock) processor.setNotchFixed (id, ! fixed);
            else      processor.clearNotch (id);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NotchTable> (this)]
            {
                if (safe == nullptr) return;
                safe->refresh();
                if (safe->onBankChanged) safe->onBankChanged();
            });
        };
        return button;
    }
}
