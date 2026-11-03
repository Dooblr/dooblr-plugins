#pragma once

#include "Core/InstallEngine.h"
#include "Core/TokenStore.h"
#include "ProgressRing.h"
#include "Theme.h"

#include <atomic>
#include <map>
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
        void setManifest (const Manifest&, const Session& = {});
        void setFetchError (const juce::String&);
        /** Shows the error in a selectable box with "Copy details"; needsSignIn shows the GitHub token panel. */
        void setFetchError (const ErrorInfo&);
        const ErrorInfo& getFetchError() const noexcept { return fetchErr; }
        /** Fetches the manifest again (the "Try again" button). */
        void refetch() { fetch(); }
        /** Where tokens come from (tests use fakes). The saved token is always read from the token store. */
        void setTokenSources (TokenSources s) { tokenSources = std::move (s); }
        void setTokenStore (const TokenStore& s) { tokenStore = std::make_unique<TokenStore> (s); }
        /** Where "Copy details" puts the text (default: the system clipboard). */
        void setClipboardSink (std::function<void (const juce::String&)> f) { clipboard = std::move (f); }
        /** What "Copy details" copies on the current page (the error, or the install report and log). */
        juce::String getCopyText() const;
        /** Presses "Copy details". */
        void clickCopy() { copyDetails(); }   // what the "Copy details" button runs
        bool isSignInVisible() const { return tokenField.isVisible(); }
        bool isCopyVisible() const { return copyButton.isVisible(); }
        bool isTokenFieldMasked() const { return tokenField.getPasswordCharacter() != 0; }
        bool isErrorTextSelectable() const { return errorBox.isVisible() && errorBox.isReadOnly() && errorBox.isMultiLine(); }
        juce::String getErrorBoxText() const { return errorBox.getText(); }
        juce::String getLogText() const { return log.getText(); }
        /** Types a token into the sign-in field and presses "Save and retry": the token is checked with
            GitHub (GET /repos/<owner>/<repo>) and saved only when GitHub accepts it, then the list is read
            again. Fails at once (nothing sent) when the text does not look like a token. */
        juce::Result saveTokenAndRetry (const juce::String& token);
        /** Presses "Get access on GitHub": opens GitHub's token page (pre-filled) and starts watching the
            clipboard (about once a second) for a copied token while the sign-in panel is shown. */
        void clickGetAccess();
        /** Where "Get access on GitHub" opens the page (default: the browser). Returns false when it could not. */
        void setBrowserOpener (std::function<bool (const juce::URL&)> f) { browserOpener = std::move (f); }
        /** Where the clipboard is read from (default: the system clipboard). */
        void setClipboardSource (std::function<juce::String()> f) { clipboardSource = std::move (f); }
        /** Reads the clipboard once (what the 1 s timer does). */
        void pollClipboard();
        bool isWatchingClipboard() const { return clipboardTimer.isTimerRunning(); }
        /** "Use the copied token" is offered (a token was found on the clipboard). */
        bool isUseCopiedVisible() const { return useCopiedButton.isVisible(); }
        /** Presses "Use the copied token" (synchronously, like a mouse click; triggerClick() would post it). */
        void clickUseCopied() { if (useCopiedButton.isVisible() && useCopiedButton.isEnabled() && useCopiedButton.onClick) useCopiedButton.onClick(); }
        bool isCheckingToken() const noexcept { return checkingToken; }
        juce::String getTokenNote() const { return tokenNote; }
        bool isTokenNoteBad() const noexcept { return tokenNoteBad; }
        /** The URL "Get access on GitHub" opens. */
        juce::URL getAccessUrl() const;
        const Session& getSession() const noexcept { return session; }
        void showPage (Page);
        /** The choose page's check boxes, by item id. */
        juce::StringArray getSelectedIds() const;
        /** Ticks or unticks one item, like clicking its check box (counts as the user's choice). */
        void setSelected (const juce::String& id, bool);
        /** Clicks one row's check box (synchronously, like a mouse click). */
        void clickRow (const juce::String& id);
        /** The choose page's "Updates", "All" and "None" buttons (synchronously). */
        void clickSelectUpdates() { selectUpdates.onClick(); }
        void clickSelectAll()     { selectAll.onClick(); }
        void clickSelectNone()    { selectNone.onClick(); }
        juce::String getInstallButtonText() const { return installButton.getButtonText(); }
        bool isInstallEnabled() const { return installButton.isEnabled(); }
        /** "3 of 8 selected" (shown next to the selection buttons). */
        juce::String getSelectionSummary() const { return selectionSummary; }
        /** Presses the install button (starts the install on a worker thread). */
        void clickInstall() { if (installButton.isEnabled()) startInstall(); }
        /** Shows the result of an install (done page). */
        void showReport (const InstallReport&);
        /** Starts the progress page for these item ids (what the install button does before the worker runs). */
        void beginProgress (const juce::StringArray& ids);
        /** Overall progress (0..1, or < 0 to keep it) and a step line (kept in the log for Copy details). */
        void setProgress (double p, const juce::String& line);
        /** Which item is being worked on (index into the ids given to beginProgress) and its own progress. */
        void setItemProgress (int index, int count, double itemProgress);
        double getProgressFraction() const noexcept { return ring.getProgress(); }
        juce::String getProgressItem() const { return progressItem; }
        juce::String getProgressStep() const { return progressStep; }
        /** The done page summary ("2 installed · 1 updated · 1 failed"). */
        juce::String getSummaryText() const { return summaryText; }
        bool isLogVisible() const { return log.isVisible(); }
        bool isRingVisible() const { return ring.isVisible(); }
        /** Done page: the failures box is shown, read-only, multi-line (select + Cmd/Ctrl+C). */
        bool isDetailsSelectable() const { return detailsBox.isVisible() && detailsBox.isReadOnly() && detailsBox.isMultiLine(); }
        juce::String getDetailsText() const { return detailsBox.getText(); }
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
        void selectWhere (const std::function<bool (Status)>&);
        juce::String itemLabel (const juce::String& id) const;
        void applyLocation();
        void copyDetails();
        void checkAndSaveToken (const juce::String& token);
        void tokenChecked (const juce::String& token, bool ok, const ErrorInfo&);
        void stopWatchingClipboard();
        struct WelcomeLayout { juce::Rectangle<int> title, desc, status, signInText, actionRow, tokenRow, errorBox, notice; };
        WelcomeLayout welcomeLayout() const;
        struct ProgressLayout { juce::Rectangle<int> ring, item, step, count, summary, subtitle, details; };
        ProgressLayout progressLayout() const;
        juce::Rectangle<int> contentArea() const;   // below the page title, above the button row

        juce::URL manifestUrl;
        Locations locations;
        Receipts receipts;
        LookAndFeel lnf;

        Page page = Page::welcome;
        bool haveManifest = false;
        Manifest manifest;
        juce::String statusLine = "Checking for the latest versions...";
        juce::String fetchError;   // short text, empty when the list was read
        ErrorInfo fetchErr;
        Session session;
        TokenSources tokenSources = TokenSources::system();
        std::unique_ptr<TokenStore> tokenStore = std::make_unique<TokenStore> (TokenStore::defaultStore());
        std::function<void (const juce::String&)> clipboard = [] (const juce::String& t) { juce::SystemClipboard::copyTextToClipboard (t); };
        InstallReport lastReport;
        juce::String tokenNote;
        bool tokenNoteBad = false;
        juce::String signedInNote;     // "GitHub accepted the token ..." shown on the list once signed in
        bool checkingToken = false;
        juce::int64 lastClipboardCount = -2;
        juce::String offeredToken, rejectedToken;   // in memory only, never shown or logged
        std::function<bool (const juce::URL&)> browserOpener = [] (const juce::URL& u) { return u.launchInDefaultBrowser(); };
        std::function<juce::String()> clipboardSource = [] { return juce::SystemClipboard::getTextFromClipboard(); };
        juce::TimedCallback clipboardTimer { [this] { pollClipboard(); } };
        std::thread checker;
        juce::StringArray heading { "Welcome", "Choose plug-ins", "Installing", "Done" };

        // Welcome
        juce::TextButton continueButton { "Continue" }, retryButton { "Try again" }, newInstallerButton { "Get the new installer" };
        juce::TextEditor errorBox, tokenField;
        juce::Label tokenLabel;
        juce::TextButton saveTokenButton { "Save and retry" }, copyButton { "Copy details" };
        juce::TextButton getAccessButton { "Get access on GitHub" }, useCopiedButton { "Use the copied token" };
        // Choose
        juce::ComboBox locationBox;
        juce::Label locationLabel;
        std::unique_ptr<RowList> rows;
        juce::Viewport viewport;
        juce::TextButton selectUpdates { "Updates" }, selectAll { "All" }, selectNone { "None" },
                         backButton { "Back" }, installButton { "Install" };
        juce::String selectionSummary;
        bool selectionChosen = false;   // the user changed the ticks: keep them when the list is read again
        // Progress / done
        ProgressRing ring;
        juce::StringArray progressIds;
        std::map<juce::String, Status> statusBefore;   // per id, when the install started (installed vs updated)
        juce::String progressItem, progressStep, progressCount, summaryText;
        juce::TextEditor detailsBox;   // done page: what failed and why (selectable, Copy details)
        juce::TextEditor log;          // every step, for Copy details; shown on the done page only when something failed
        juce::TextButton cancelButton { "Cancel" }, againButton { "Check again" }, closeButton { "Close" };
        juce::String doneTitle, doneSubtitle;

        std::atomic<bool> cancelFlag { false };
        std::thread worker;
        juce::Component::SafePointer<InstallerComponent> safeThis { this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstallerComponent)
    };
}
