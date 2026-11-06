#include "HistoryList.h"
#include "Theme.h"
#include "../Generation/ModelCatalog.h"

namespace dooblrgenerate::ui
{
    juce::String shortModelName (const juce::String& id)
    {
        if (id == gen::ModelCatalog::stableAudioOpenSmall) return "Stable Audio";
        if (id == gen::ModelCatalog::audioLdm) return "AudioLDM";
        if (id == gen::ModelCatalog::testBackend) return "TEST (not AI)";
        return id;
    }


    HistoryList::HistoryList()
    {
        list.setModel (this);
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getViewport()->setScrollBarThickness (8);
        addAndMakeVisible (list);
    }

    void HistoryList::setEntries (std::vector<gen::HistoryEntry> e)
    {
        entries = std::move (e);
        list.updateContent();
        list.repaint();
    }

    void HistoryList::setCurrentId (const juce::String& id)
    {
        if (id != currentId)
        {
            currentId = id;
            list.repaint();
        }
    }

    juce::File HistoryList::fileForRow (int row) const
    {
        return juce::isPositiveAndBelow (row, (int) entries.size()) ? entries[(size_t) row].wav : juce::File();
    }

    void HistoryList::paint (juce::Graphics& g)
    {
        g.setColour (Palette::background);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    void HistoryList::resized()
    {
        list.setBounds (getLocalBounds().reduced (2));
    }

    void HistoryList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (row, (int) entries.size()))
            return;

        const auto& e = entries[(size_t) row];
        const auto isCurrent = e.id == currentId;
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2);

        if (isCurrent)
        {
            g.setColour (Palette::panelRaised);
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
            g.setColour (e.isTest ? Palette::textMuted : Palette::accent);
            g.fillRect (area.removeFromLeft (3));
        }
        else
        {
            area.removeFromLeft (3);
        }

        area.removeFromLeft (8);
        g.setColour (Palette::textPrimary);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (e.prompt, area.removeFromTop (area.getHeight() / 2 + 1), juce::Justification::bottomLeft, 1, 0.9f);

        juce::String info;
        info << shortModelName (e.model) << "  seed " << e.seed << "  " << juce::String (e.seconds, 2) << " s";
        if (e.generateSeconds > 0)
            info << "  made in " << juce::String (e.totalSeconds, 1) << " s";

        g.setColour (e.isTest ? Palette::dynamic : Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (info, area, juce::Justification::topLeft, 1, 0.9f);

        g.setColour (Palette::divider);
        g.drawHorizontalLine (height - 1, 8.0f, (float) width - 8.0f);
    }

    void HistoryList::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        if (onChoose && juce::isPositiveAndBelow (row, (int) entries.size()))
            onChoose (entries[(size_t) row], false);
    }

    void HistoryList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        if (onChoose && juce::isPositiveAndBelow (row, (int) entries.size()))
            onChoose (entries[(size_t) row], true);
    }

    juce::var HistoryList::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        // The ListBox starts the drag through the nearest DragAndDropContainer (the editor),
        // which turns it into an external file drag when the mouse leaves the plug-in window.
        if (rows.isEmpty())
            return {};

        const auto file = fileForRow (rows[0]);
        return file.existsAsFile() ? juce::var (file.getFullPathName()) : juce::var();
    }
}
