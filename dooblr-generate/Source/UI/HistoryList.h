#pragma once

#include "../Generation/History.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dooblrgenerate::ui
{
    /** A short label for a model id ("Stable Audio", "AudioLDM", "TEST"). */
    juce::String shortModelName (const juce::String& modelId);

    /**
        The history of generations, newest first. Click a row to load it as the current sample,
        double-click to load and play it; drag a row out to drag its WAV file into Ableton.
    */
    class HistoryList final : public juce::Component, private juce::ListBoxModel
    {
    public:
        HistoryList();

        void setEntries (std::vector<gen::HistoryEntry>);
        void setCurrentId (const juce::String&);
        int getNumEntries() const noexcept { return (int) entries.size(); }
        const std::vector<gen::HistoryEntry>& getEntries() const noexcept { return entries; }

        std::function<void (const gen::HistoryEntry&, bool play)> onChoose;

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::ListBox& getListBox() noexcept { return list; }
        /** Starts an external drag of a row's WAV (tests check the file it would drag). */
        juce::File fileForRow (int row) const;

    private:
        int getNumRows() override { return (int) entries.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
        bool mayDragToExternalWindows() const override { return true; }

        juce::ListBox list;
        std::vector<gen::HistoryEntry> entries;
        juce::String currentId;
    };
}
