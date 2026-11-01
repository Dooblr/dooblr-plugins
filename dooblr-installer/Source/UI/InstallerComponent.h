#pragma once

#include "Core/InstallEngine.h"
#include "Theme.h"

#include <atomic>
#include <thread>

namespace dooblr::installer::ui
{
    /** The installer / updater window: Welcome -> Choose -> Installing -> Done. */
    class InstallerComponent final : public juce::Component
    {
    public:
        enum class Page { welcome, choose, progress, done };

        /** startFetch = false (tests, snapshots) leaves the manifest to setManifest(). */
        InstallerComponent (juce::URL manifestUrl, Locations, bool startFetch = true);
        ~InstallerComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        Page getPage() const noexcept { return page; }

        //== Test hooks (also what the background fetch calls) ===================
        void setManifest (const Manifest&);
        void setFetchError (const juce::String&);
        void showPage (Page);
        /** The choose page's check boxes, by item id. */
        juce::StringArray getSelectedIds() const;
        void setSelected (const juce::String& id, bool);
        /** Shows the result of an install (done page). */
        void showReport (const InstallReport&);
        void setProgress (double p, const juce::String& line);
        juce::String getStatusLine() const { return statusLine; }
        juce::String rowStatus (const juce::String& id) const;
        int numRows() const;

    private:
        class Row;
        class RowList;

        void fetch();
        void refreshInstalled();
        void startInstall();
        void layoutChoose (juce::Rectangle<int>);
        void updateInstallButton();
        void applyLocation();

        juce::URL manifestUrl;
        Locations locations;
        Receipts receipts;
        LookAndFeel lnf;

        Page page = Page::welcome;
        bool haveManifest = false;
        Manifest manifest;
        juce::String statusLine = "Checking for the latest versions...";
        juce::String fetchError;
        juce::StringArray heading { "Welcome", "Choose plug-ins", "Installing", "Done" };

        // Welcome
        juce::TextButton continueButton { "Continue" }, retryButton { "Try again" }, newInstallerButton { "Get the new installer" };
        // Choose
        juce::ComboBox locationBox;
        juce::Label locationLabel;
        std::unique_ptr<RowList> rows;
        juce::Viewport viewport;
        juce::TextButton selectUpdates { "Select updates" }, selectAll { "All" }, selectNone { "None" },
                         backButton { "Back" }, installButton { "Install" };
        // Progress / done
        double progress = 0.0;
        juce::ProgressBar progressBar { progress };
        juce::TextEditor log;
        juce::TextButton cancelButton { "Cancel" }, againButton { "Check again" }, closeButton { "Close" };
        juce::String doneTitle, doneSubtitle;

        std::atomic<bool> cancelFlag { false };
        std::thread worker;
        juce::Component::SafePointer<InstallerComponent> safeThis { this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstallerComponent)
    };
}
