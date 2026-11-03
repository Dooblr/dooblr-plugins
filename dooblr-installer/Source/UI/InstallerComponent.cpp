#include "InstallerComponent.h"
#include "Core/Clipboard.h"

namespace dooblr::installer::ui
{
    namespace
    {
        juce::Colour statusColour (Status s)
        {
            switch (s)
            {
                case Status::notInstalled:    return Palette::accent;
                case Status::updateAvailable: return Palette::dynamic;
                case Status::upToDate:        return Palette::textMuted;
                case Status::newerInstalled:  return Palette::textMuted;
                case Status::unavailable:     return Palette::divider.brighter (0.4f);
            }
            return Palette::textMuted;
        }

        constexpr int rowHeight = 58, addOnHeight = 46;
    }

    //==========================================================================
    class InstallerComponent::Row final : public juce::Component
    {
    public:
        Row (const Item& i, std::function<void()> changed) : item (i)
        {
            addAndMakeVisible (check);
            check.setTitle ("Install " + item.name);
            check.onClick = std::move (changed);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void setInstalled (const InstalledInfo& i)
        {
            installed = i;
            status = statusFor (item, installed);
            check.setEnabled (status != Status::unavailable);
            if (status == Status::unavailable)
                check.setToggleState (false, juce::dontSendNotification);
            repaint();
        }

        bool isAddOn() const { return item.parent.isNotEmpty(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().withTrimmedLeft (isAddOn() ? 28 : 0);
            g.setColour (isAddOn() ? Palette::panel : Palette::panelRaised.withAlpha (0.55f));
            g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 2.0f), 5.0f);

            r.removeFromLeft (44);
            auto right = r.removeFromRight (300);

            // Status pill
            auto pill = right.removeFromRight (128).withSizeKeepingCentre (116, 24);
            const auto sc = statusColour (status);
            const auto actionable = status == Status::notInstalled || status == Status::updateAvailable;
            // A tag, not a button: actionable states get a tinted fill, the rest are plain text.
            if (actionable)
            {
                g.setColour (sc.withAlpha (0.18f));
                g.fillRoundedRectangle (pill.toFloat(), 12.0f);
            }
            g.setColour (sc);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (statusText (status), pill, juce::Justification::centred);

            // Versions
            right.removeFromRight (12);
            auto versions = right;
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (Palette::textMuted);
            auto top = versions.removeFromTop (versions.getHeight() / 2);
            g.drawText ("Installed", top.removeFromLeft (70), juce::Justification::bottomLeft);
            g.drawText ("Available", versions.removeFromLeft (70), juce::Justification::topLeft);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.setColour (installed.present ? Palette::textPrimary : Palette::textMuted);
            g.drawText (installed.present ? (installed.version.isNotEmpty() ? installed.version : "yes (unknown)") : "-",
                        top, juce::Justification::bottomLeft);
            g.setColour (actionable ? sc : Palette::textPrimary);
            g.drawText (item.version, versions, juce::Justification::topLeft);

            // Name and description
            auto text = r.reduced (0, 8);
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (isAddOn() ? 14.0f : 16.0f, juce::Font::bold));
            auto nameRow = text.removeFromTop (text.getHeight() / 2);
            g.drawFittedText (item.name, nameRow, juce::Justification::bottomLeft, 1);
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (item.description, text, juce::Justification::topLeft, 1, 0.9f);
        }

        // Clicking anywhere on the row toggles it, like the checkbox.
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (check.isEnabled() && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
                check.triggerClick();
        }

        void resized() override
        {
            check.setBounds (getLocalBounds().withTrimmedLeft (isAddOn() ? 28 : 0).removeFromLeft (44).withSizeKeepingCentre (26, 26).translated (4, 0));
        }

        Item item;
        InstalledInfo installed;
        Status status = Status::notInstalled;
        juce::ToggleButton check;
    };

    class InstallerComponent::RowList final : public juce::Component
    {
    public:
        void layout (int width)
        {
            int y = 0;
            for (auto* r : rows)
            {
                const auto h = r->isAddOn() ? addOnHeight : rowHeight;
                r->setBounds (0, y, width, h);
                y += h + 4;
            }
            setSize (width, juce::jmax (1, y));
        }

        juce::OwnedArray<Row> rows;
    };

    //==========================================================================
    InstallerComponent::InstallerComponent (juce::URL url, Locations loc, bool startFetch)
        : manifestUrl (std::move (url)), locations (std::move (loc))
    {
        setLookAndFeel (&lnf);
        setOpaque (true);

        for (auto* b : { &continueButton, &retryButton, &newInstallerButton, &selectUpdates, &selectAll, &selectNone,
                         &backButton, &installButton, &cancelButton, &againButton, &closeButton, &saveTokenButton, &copyButton,
                         &getAccessButton, &useCopiedButton })
            addChildComponent (b);

        // Errors: selectable, read-only text (select and Cmd/Ctrl+C, or right-click > Copy), plus "Copy details".
        addChildComponent (errorBox);
        errorBox.setMultiLine (true, true);
        errorBox.setReadOnly (true);
        errorBox.setScrollbarsShown (true);
        errorBox.setCaretVisible (false);
        errorBox.setPopupMenuEnabled (true);
        errorBox.setFont (juce::FontOptions (13.0f));
        errorBox.setColour (juce::TextEditor::textColourId, Palette::textPrimary);
        errorBox.setTitle ("Error details");
        copyButton.setTooltip ("Copies the full error details (installer version, system, step, address, HTTP status, message). No token is ever included.");
        copyButton.onClick = [this] { copyDetails(); };

        // GitHub sign-in (private release)
        addChildComponent (tokenLabel);
        tokenLabel.setText ("Or paste it", juce::dontSendNotification);
        tokenLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addChildComponent (tokenField);
        tokenField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        tokenField.setTextToShowWhenEmpty ("Paste a token (github_pat_... or ghp_...)", Palette::textMuted);
        tokenField.setFont (juce::FontOptions (14.0f));
        tokenField.setIndents (8, 7);
        tokenField.setTitle ("GitHub token");
        tokenField.onReturnKey = [this] { saveTokenButton.triggerClick(); };
        saveTokenButton.onClick = [this] { saveTokenAndRetry (tokenField.getText()); };
        getAccessButton.setToggleState (true, juce::dontSendNotification);   // accent fill: the primary action
        getAccessButton.setClickingTogglesState (false);
        getAccessButton.setTooltip ("Opens GitHub's new-token page, already filled in (name, owner Dooblr, Contents: Read-only, 366 days). "
                                    "You pick the dooblr-plugins repository, click Generate token, then Copy.");
        getAccessButton.onClick = [this] { clickGetAccess(); };
        useCopiedButton.setToggleState (true, juce::dontSendNotification);
        useCopiedButton.setClickingTogglesState (false);
        useCopiedButton.setTooltip ("Checks the token on the clipboard with GitHub and saves it when GitHub accepts it.");
        useCopiedButton.onClick = [this] { if (offeredToken.isNotEmpty()) checkAndSaveToken (offeredToken); };
        clipboardSource = [this]
        {
            // Read the text only when something new was copied (the change count itself is not the clipboard).
            const auto count = clipboardChangeCount();
            if (count >= 0 && count == lastClipboardCount)
                return juce::String();
            lastClipboardCount = count;
            return juce::SystemClipboard::getTextFromClipboard();
        };

        continueButton.setToggleState (true, juce::dontSendNotification); // accent fill: the primary action
        installButton.setToggleState (true, juce::dontSendNotification);
        closeButton.setToggleState (true, juce::dontSendNotification);
        continueButton.setClickingTogglesState (false);

        continueButton.onClick = [this] { refreshInstalled(); showPage (Page::choose); };
        retryButton.onClick = [this] { fetch(); };
        newInstallerButton.onClick = [this]
        {
            const auto& a = currentPlatform() == Platform::windows ? manifest.installerWindows : manifest.installerMac;
            if (a.isValid()) a.url.launchInDefaultBrowser();
        };
        backButton.onClick = [this] { showPage (Page::welcome); };
        installButton.onClick = [this] { startInstall(); };
        cancelButton.onClick = [this] { cancelFlag = true; cancelButton.setEnabled (false); };
        againButton.onClick = [this] { fetch(); showPage (Page::welcome); };
        closeButton.onClick = [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); };

        const auto selectWhere = [this] (std::function<bool (const Row&)> pred)
        {
            if (rows != nullptr)
                for (auto* r : rows->rows)
                    if (r->check.isEnabled())
                        r->check.setToggleState (pred (*r), juce::dontSendNotification);
            updateInstallButton();
        };
        selectUpdates.onClick = [selectWhere] { selectWhere ([] (const Row& r) { return r.status == Status::updateAvailable; }); };
        selectAll.onClick = [selectWhere] { selectWhere ([] (const Row& r) { return r.status == Status::notInstalled || r.status == Status::updateAvailable; }); };
        selectNone.onClick = [selectWhere] { selectWhere ([] (const Row&) { return false; }); };

        addChildComponent (locationBox);
        addChildComponent (locationLabel);
        locationLabel.setText ("Install plug-ins for", juce::dontSendNotification);
        locationLabel.setColour (juce::Label::textColourId, Palette::textMuted);
        locationLabel.setFont (juce::FontOptions (13.0f));
        if (locations.overridden)
            locationBox.addItem ("Folder: " + locations.vst3Dir.getFullPathName(), 3);
        else
        {
            locationBox.addItem ("Everyone on this computer  (" + Locations::systemVst3Dir().getFullPathName() + ")", 1);
            locationBox.addItem ("Only me  (" + Locations::userVst3Dir().getFullPathName() + ")", 2);
        }
        locationBox.setSelectedId (locations.overridden ? 3 : (locations.vst3Dir == Locations::userVst3Dir() ? 2 : 1), juce::dontSendNotification);
        locationBox.onChange = [this] { applyLocation(); };

        addChildComponent (viewport);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);

        addChildComponent (progressBar);
        addChildComponent (log);
        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setScrollbarsShown (true);
        log.setCaretVisible (false);
        log.setFont (juce::FontOptions (13.0f));

        setSize (820, 560);
        showPage (Page::welcome);
        if (startFetch)
            fetch();
    }

    InstallerComponent::~InstallerComponent()
    {
        cancelFlag = true;
        clipboardTimer.stopTimer();
        if (checker.joinable())
            checker.join();
        if (worker.joinable())
            worker.join();
        viewport.setViewedComponent (nullptr, false);
        setLookAndFeel (nullptr);
    }

    //==========================================================================
    void InstallerComponent::fetch()
    {
        if (worker.joinable())
            worker.join();

        haveManifest = false;
        fetchError.clear();
        fetchErr = {};
        errorBox.clear();
        statusLine = "Checking for the latest versions...";
        showPage (page);

        // The saved token always comes from this window's token store (the one "Save and retry" writes).
        auto sources = tokenSources;
        sources.saved = [store = *tokenStore] (juce::String& where) { return store.load (where); };

        worker = std::thread ([url = manifestUrl, sources, sp = safeThis]
        {
            Manifest m;
            Session s;
            ErrorInfo e;
            const auto ok = fetchManifest (url, m, s, e, sources);

            juce::MessageManager::callAsync ([sp, ok, m, s, e]
            {
                if (auto* self = sp.getComponent())
                {
                    if (ok) self->setManifest (m, s);
                    else    self->setFetchError (e);
                }
            });
        });
    }

    void InstallerComponent::setManifest (const Manifest& m, const Session& s)
    {
        manifest = m;
        session = s;
        haveManifest = true;
        fetchError.clear();
        fetchErr = {};
        tokenNote.clear();
        tokenNoteBad = false;
        offeredToken.clear();
        stopWatchingClipboard();
        errorBox.clear();

        rows = std::make_unique<RowList>();
        for (auto& item : manifest.items)
            rows->addAndMakeVisible (rows->rows.add (new Row (item, [this] { updateInstallButton(); })));
        viewport.setViewedComponent (rows.get(), false);

        refreshInstalled();

        // Default selection: updates; on a computer with no dooblr plug-ins, every plug-in (not the optional add-ons).
        bool anyInstalled = false;
        for (auto* r : rows->rows)
            anyInstalled = anyInstalled || r->installed.present;
        for (auto* r : rows->rows)
            r->check.setToggleState (r->status == Status::updateAvailable
                                     || (! anyInstalled && ! r->item.optional && r->status == Status::notInstalled),
                                     juce::dontSendNotification);
        updateInstallButton();

        int updates = 0, missing = 0;
        for (auto* r : rows->rows)
        {
            updates += r->status == Status::updateAvailable ? 1 : 0;
            missing += r->status == Status::notInstalled ? 1 : 0;
        }

        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        statusLine = juce::String (manifest.items.size()) + " items available";
        if (updates > 0) statusLine << dot << updates << (updates == 1 ? " update" : " updates");
        if (missing > 0) statusLine << dot << missing << " not installed";
        if (updates == 0 && missing == 0) statusLine << dot << "everything is up to date";
        if (session.authenticated()) statusLine << dot << "signed in to GitHub";
        showPage (page);
    }

    void InstallerComponent::setFetchError (const juce::String& e)
    {
        ErrorInfo info;
        info.step = "Getting the plug-in list (the manifest)";
        info.message = e;
        info.setUrl (manifestUrl.toString (true));
        setFetchError (info);
    }

    void InstallerComponent::setFetchError (const ErrorInfo& e)
    {
        fetchErr = e;
        if (fetchErr.message.isEmpty())
            fetchErr.message = "Unknown error";
        fetchError = fetchErr.message;
        if (fetchErr.needsSignIn && tokenNote.isNotEmpty() && ! tokenNoteBad)
        {
            tokenNote = "GitHub did not accept the saved token. Paste a new one.";
            tokenNoteBad = true;
        }
        statusLine = fetchErr.needsSignIn ? "Sign in to GitHub to get the plug-in list." : "Could not get the plug-in list.";
        errorBox.setText (fetchErr.shortText() + "\nList address: " + redactUrl (manifestUrl.toString (true)), false);
        showPage (page);
    }

    juce::Result InstallerComponent::saveTokenAndRetry (const juce::String& tokenIn)
    {
        const auto token = tokenIn.trim();
        if (! TokenStore::looksLikeToken (token))
        {
            const auto r = juce::Result::fail ("That does not look like a GitHub token (it should be one word of letters, digits and _ , like github_pat_... or ghp_...). Nothing was saved.");
            tokenNote = r.getErrorMessage();
            tokenNoteBad = true;
            repaint();
            return r;
        }
        tokenField.clear();
        checkAndSaveToken (token);
        return juce::Result::ok();
    }

    juce::URL InstallerComponent::getAccessUrl() const
    {
        const auto ref = parseReleaseUrl (manifestUrl.toString (true), GitHubConfig::fromEnvironment());
        return ref.isValid() ? tokenCreationUrl (ref.owner, ref.repo) : tokenCreationUrl();
    }

    void InstallerComponent::clickGetAccess()
    {
        const auto url = getAccessUrl();
        const bool opened = browserOpener != nullptr && browserOpener (url);
        tokenNoteBad = ! opened;
        tokenNote = opened ? "On GitHub: pick dooblr-plugins, Generate token, Copy. The installer is watching for the copied token."
                           : "Could not open the browser. Go to github.com/settings/personal-access-tokens/new and paste the token below.";
        clipboardTimer.startTimer (1000);
        pollClipboard();
        repaint();
    }

    void InstallerComponent::stopWatchingClipboard()
    {
        clipboardTimer.stopTimer();
    }

    void InstallerComponent::pollClipboard()
    {
        // Only while the sign-in panel is on screen.
        if (page != Page::welcome || haveManifest || ! fetchErr.needsSignIn)
        {
            stopWatchingClipboard();
            return;
        }
        if (checkingToken || clipboardSource == nullptr)
            return;
        const auto text = clipboardSource().trim();
        if (! looksLikeGitHubToken (text) || text == offeredToken || text == rejectedToken)
            return;
        registerSecret (text);
        offeredToken = text;
        tokenNote = "A GitHub token was copied. Click \"Use the copied token\" to check it and save it.";
        tokenNoteBad = false;
        showPage (page);
    }

    void InstallerComponent::checkAndSaveToken (const juce::String& tokenIn)
    {
        const auto token = tokenIn.trim();
        if (token.isEmpty() || checkingToken)
            return;
        registerSecret (token);
        if (checker.joinable())
            checker.join();
        checkingToken = true;
        tokenNote = "Checking the token with GitHub...";
        tokenNoteBad = false;
        useCopiedButton.setEnabled (false);
        saveTokenButton.setEnabled (false);
        repaint();

        const auto ref = parseReleaseUrl (manifestUrl.toString (true), GitHubConfig::fromEnvironment());
        const auto owner = ref.isValid() ? ref.owner : juce::String ("Dooblr");
        const auto repo = ref.isValid() ? ref.repo : juce::String ("dooblr-plugins");
        checker = std::thread ([token, owner, repo, sp = safeThis]
        {
            ErrorInfo e;
            const auto ok = validateToken (token, owner, repo, e);
            juce::MessageManager::callAsync ([sp, token, ok, e]
            {
                if (auto* self = sp.getComponent())
                    self->tokenChecked (token, ok, e);
            });
        });
    }

    void InstallerComponent::tokenChecked (const juce::String& token, bool ok, const ErrorInfo& e)
    {
        checkingToken = false;
        useCopiedButton.setEnabled (true);
        saveTokenButton.setEnabled (true);

        if (! ok)
        {
            rejectedToken = token;
            if (offeredToken == token)
                offeredToken.clear();
            auto shown = e;
            shown.needsSignIn = true;   // keep the sign-in panel up
            if (shown.details.isNotEmpty()) shown.details << "\n";
            shown.details << "The token was not saved.";
            tokenNote = "Not saved: " + e.message.upToFirstOccurrenceOf (".", true, false) + " Details below.";
            tokenNoteBad = true;
            setFetchError (shown);
            return;
        }

        juce::String where;
        const auto r = tokenStore->save (token, where);
        if (r.failed())
        {
            tokenNote = "GitHub accepted the token, but it could not be saved: " + r.getErrorMessage();
            tokenNoteBad = true;
            repaint();
            return;
        }
        stopWatchingClipboard();
        offeredToken.clear();
        rejectedToken.clear();
        // Leave no token on the clipboard once it is saved.
        if (clipboardSource != nullptr && clipboardSource().trim() == token && clipboard != nullptr)
            clipboard ({});
        tokenNote = "GitHub accepted the token. Saved in the " + where + ". Getting the plug-in list...";
        tokenNoteBad = false;
        signedInNote = "GitHub accepted the token (saved in the " + where + ").";
        fetch();
    }

    juce::String InstallerComponent::getCopyText() const
    {
        juce::String s;
        if (page == Page::welcome)
        {
            if (fetchErr.isEmpty())
                return {};
            s << fetchErr.copyText() << "Manifest: " << manifestUrl.toString (true) << "\n";
        }
        else
        {
            if (page == Page::done)
                s << lastReport.copyText();
            else
                s << "dooblr installer " << installerVersion() << " on " << osDescription() << "\n";
            s << "\nLog:\n" << log.getText();
        }
        return redactSecrets (s);
    }

    void InstallerComponent::copyDetails()
    {
        const auto text = getCopyText();
        if (text.isEmpty() || ! clipboard)
            return;
        clipboard (text);
        copyButton.setButtonText ("Copied");
        juce::Timer::callAfterDelay (1600, [sp = safeThis] { if (auto* s = sp.getComponent()) s->copyButton.setButtonText ("Copy details"); });
    }

    void InstallerComponent::refreshInstalled()
    {
        if (rows == nullptr)
            return;
        for (auto* r : rows->rows)
            r->setInstalled (findInstalled (r->item, locations, receipts));
        updateInstallButton();
        repaint();
    }

    void InstallerComponent::applyLocation()
    {
        if (locationBox.getSelectedId() == 1) locations.vst3Dir = Locations::systemVst3Dir();
        if (locationBox.getSelectedId() == 2) locations.vst3Dir = Locations::userVst3Dir();
        refreshInstalled();
    }

    juce::StringArray InstallerComponent::getSelectedIds() const
    {
        juce::StringArray ids;
        if (rows != nullptr)
            for (auto* r : rows->rows)
                if (r->check.getToggleState() && r->check.isEnabled())
                    ids.add (r->item.id);
        return ids;
    }

    void InstallerComponent::setSelected (const juce::String& id, bool on)
    {
        if (rows != nullptr)
            for (auto* r : rows->rows)
                if (r->item.id == id)
                    r->check.setToggleState (on, juce::dontSendNotification);
        updateInstallButton();
    }

    juce::String InstallerComponent::rowStatus (const juce::String& id) const
    {
        if (rows != nullptr)
            for (auto* r : rows->rows)
                if (r->item.id == id)
                    return statusText (r->status);
        return {};
    }

    int InstallerComponent::numRows() const { return rows != nullptr ? rows->rows.size() : 0; }

    void InstallerComponent::updateInstallButton()
    {
        const auto n = getSelectedIds().size();
        installButton.setButtonText (n == 0 ? juce::String ("Install") : "Install " + juce::String (n) + (n == 1 ? " item" : " items"));
        installButton.setEnabled (n > 0);
    }

    //==========================================================================
    void InstallerComponent::startInstall()
    {
        const auto ids = getSelectedIds();
        if (ids.isEmpty())
            return;
        if (worker.joinable())
            worker.join();

        cancelFlag = false;
        cancelButton.setEnabled (true);
        log.clear();
        progress = 0.0;
        showPage (Page::progress);

        worker = std::thread ([this, ids, m = manifest, loc = locations, sess = session, sp = safeThis]
        {
            Receipts rec (Receipts::defaultFile());
            const auto report = installItems (m, ids, loc, rec,
                [sp] (const juce::String& line)
                {
                    juce::MessageManager::callAsync ([sp, line] { if (auto* s = sp.getComponent()) s->setProgress (-1.0, line); });
                },
                [this, sp] (int i, int n, double p)
                {
                    const auto overall = ((double) i + p) / (double) juce::jmax (1, n);
                    juce::MessageManager::callAsync ([sp, overall] { if (auto* s = sp.getComponent()) s->setProgress (overall, {}); });
                    return ! cancelFlag.load();
                }, &sess);

            juce::MessageManager::callAsync ([sp, report] { if (auto* s = sp.getComponent()) s->showReport (report); });
        });
    }

    void InstallerComponent::setProgress (double p, const juce::String& line)
    {
        if (p >= 0.0)
            progress = p;
        if (line.isNotEmpty())
        {
            log.moveCaretToEnd();
            log.insertTextAtCaret (line + "\n");
        }
    }

    void InstallerComponent::showReport (const InstallReport& r)
    {
        progress = 1.0;
        lastReport = r;
        if (r.cancelled)
        {
            doneTitle = "Cancelled";
            doneSubtitle = "Nothing more was installed. Plug-ins already in place were left as they were.";
        }
        else if (r.failed.isEmpty())
        {
            doneTitle = "All done";
            doneSubtitle = "Rescan plug-ins in your DAW (or restart it) to see the new versions.";
        }
        else
        {
            doneTitle = r.done.isEmpty() ? "Nothing was installed" : "Finished with problems";
            doneSubtitle = "See below (select the text, or Copy details). You can run the installer again to retry.";
        }

        log.clear();
        for (auto& d : r.done)   log.insertTextAtCaret (juce::String::fromUTF8 ("\xe2\x9c\x93  ") + d + "\n");
        for (auto& f : r.failed) log.insertTextAtCaret (juce::String::fromUTF8 ("\xe2\x9c\x95  ") + redactSecrets (f) + "\n");
        if (! r.errors.isEmpty())
        {
            log.insertTextAtCaret ("\nDetails:\n");
            for (auto& e : r.errors)
                log.insertTextAtCaret (e.shortText() + "\n\n");
        }
        refreshInstalled();
        showPage (Page::done);
    }

    //==========================================================================
    void InstallerComponent::showPage (Page p)
    {
        page = p;
        for (auto* c : getChildren())
            c->setVisible (false);
        if (p != Page::welcome)
            stopWatchingClipboard();

        switch (page)
        {
            case Page::welcome:
                continueButton.setVisible (true);
                continueButton.setEnabled (haveManifest);
                retryButton.setVisible (fetchError.isNotEmpty());
                errorBox.setVisible (! fetchErr.isEmpty());
                copyButton.setVisible (! fetchErr.isEmpty());
                for (auto* c : std::initializer_list<juce::Component*> { &tokenLabel, &tokenField, &saveTokenButton, &getAccessButton })
                    c->setVisible (fetchErr.needsSignIn);
                useCopiedButton.setVisible (fetchErr.needsSignIn && offeredToken.isNotEmpty());
                newInstallerButton.setVisible (haveManifest && Version::compare (manifest.installerVersion, DOOBLRINSTALLER_VERSION) > 0);
                break;
            case Page::choose:
                for (auto* c : std::initializer_list<juce::Component*> { &locationBox, &locationLabel, &viewport, &selectUpdates,
                                                                         &selectAll, &selectNone, &backButton, &installButton })
                    c->setVisible (true);
                locationBox.setEnabled (! locations.overridden);
                break;
            case Page::progress:
                progressBar.setVisible (true);
                log.setVisible (true);
                cancelButton.setVisible (true);
                copyButton.setVisible (true);
                break;
            case Page::done:
                log.setVisible (true);
                copyButton.setVisible (true);
                againButton.setVisible (true);
                closeButton.setVisible (true);
                break;
        }

        resized();
        repaint();
    }

    void InstallerComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        auto r = getLocalBounds().reduced (28, 22);

        // Header: wordmark, accent underline, version
        auto header = r.removeFromTop (44);
        g.setColour (Palette::textPrimary);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("dooblr", header.removeFromLeft (84), juce::Justification::centredLeft);
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) r.getX(), (float) header.getBottom() - 6.0f, 34.0f, 2.0f), 1.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("installer  v" + juce::String (DOOBLRINSTALLER_VERSION), header, juce::Justification::centredLeft);

        // Steps
        auto steps = header.removeFromRight (360);
        const juce::StringArray names { "Welcome", "Choose", "Install", "Done" };
        const auto current = (int) page;
        for (int i = names.size(); --i >= 0;)
        {
            auto s = steps.removeFromRight (86);
            g.setColour (i == current ? Palette::accent : (i < current ? Palette::textPrimary : Palette::textMuted.withAlpha (0.6f)));
            g.fillEllipse (s.removeFromLeft (14).withSizeKeepingCentre (8, 8).toFloat());
            g.setFont (juce::FontOptions (12.0f, i == current ? juce::Font::bold : juce::Font::plain));
            g.drawText (names[i], s, juce::Justification::centredLeft);
        }

        r.removeFromTop (14);
        g.setColour (Palette::divider);
        g.fillRect (r.removeFromTop (1));
        r.removeFromTop (20);

        g.setColour (Palette::textPrimary);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));

        if (page == Page::welcome)
        {
            const auto L = welcomeLayout();
            const auto bad = ! fetchErr.isEmpty();
            g.drawText ("Welcome to the dooblr plug-in suite", L.title, juce::Justification::centredLeft);
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (15.0f));
            const auto vst3 = locations.vst3Dir.getFullPathName();
            if (bad)
                g.drawFittedText ("This installs and updates the dooblr VST3 plug-ins and Max for Live devices. Every download is checked (sha256) before anything is installed.",
                                  L.desc, juce::Justification::topLeft, 2, 1.0f);
            else
                g.drawFittedText ("This installs and updates the dooblr VST3 plug-ins (effects and instruments for Ableton Live, "
                                  "Bitwig, Reaper, Cubase, Studio One and any other VST3 host) and the dooblr Max for Live devices.\n\n"
                                  "Next you pick what to install or update. Plug-ins go into the VST3 folder (" + vst3 + "); "
                                  "Max for Live devices go into Ableton's User Library (Presets > MIDI Effects > Max MIDI Effect). Every download is checked (sha256) before "
                                  "anything is installed.",
                                  L.desc, juce::Justification::topLeft, 7, 1.0f);

            auto status = L.status;
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (status.toFloat(), 6.0f);
            g.setColour (bad ? (fetchErr.needsSignIn ? Palette::dynamic : Palette::danger) : (haveManifest ? Palette::accent : Palette::textMuted));
            g.fillEllipse (status.removeFromLeft (34).withSizeKeepingCentre (10, 10).toFloat());
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (statusLine, status, juce::Justification::centredLeft);

            if (bad && fetchErr.needsSignIn)
            {
                g.setColour (Palette::textPrimary);
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText ("The dooblr releases are in a private GitHub repository for now, so the installer needs a GitHub token once. "
                                  "Click Get access on GitHub: the token page opens already filled in. There, choose Only select repositories > "
                                  "dooblr-plugins, click Generate token, then Copy. The token is checked with GitHub, kept in the "
                                  + TokenStore::systemStoreName() + " and never shown or logged.",
                                  L.signInText, juce::Justification::topLeft, 3, 1.0f);
                if (tokenNote.isNotEmpty())
                {
                    g.setColour (tokenNoteBad ? Palette::danger : (checkingToken ? Palette::dynamic : Palette::textMuted));
                    g.setFont (juce::FontOptions (12.0f));
                    g.drawFittedText (tokenNote, L.actionRow.withTrimmedLeft (useCopiedButton.isVisible() ? 200 + 190 : 200),
                                      juce::Justification::centredLeft, 2, 0.9f);
                }
            }
            else if (! bad && newInstallerButton.isVisible())
            {
                g.setColour (Palette::dynamic);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText ("A newer installer (" + manifest.installerVersion + ") is available.", L.notice, juce::Justification::centredLeft);
            }
            else if (! bad && haveManifest && signedInNote.isNotEmpty())
            {
                g.setColour (Palette::accent);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (signedInNote, L.notice, juce::Justification::centredLeft);
            }
        }
        else if (page == Page::choose)
        {
            g.drawText ("Choose what to install or update", r.removeFromTop (38), juce::Justification::centredLeft);
        }
        else if (page == Page::progress)
        {
            g.drawText ("Installing...", r.removeFromTop (38), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (doneTitle == "All done" ? Palette::textPrimary : Palette::dynamic);
            g.drawText (doneTitle, r.removeFromTop (38), juce::Justification::centredLeft);
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (doneSubtitle, r.removeFromTop (26), juce::Justification::centredLeft);
        }
    }

    InstallerComponent::WelcomeLayout InstallerComponent::welcomeLayout() const
    {
        WelcomeLayout L;
        auto r = getLocalBounds().reduced (28, 22);
        r.removeFromTop (44 + 14 + 1 + 20);
        r.removeFromBottom (36 + 16);
        const auto bad = ! fetchErr.isEmpty();
        L.title = r.removeFromTop (38);
        r.removeFromTop (10);
        L.desc = r.removeFromTop (bad ? 40 : 130);
        r.removeFromTop (bad ? 10 : 16);
        L.status = r.removeFromTop (46);
        r.removeFromTop (10);
        if (bad)
        {
            if (fetchErr.needsSignIn)
            {
                L.signInText = r.removeFromTop (50);
                r.removeFromTop (6);
                L.actionRow = r.removeFromTop (34);
                r.removeFromTop (8);
                L.tokenRow = r.removeFromTop (32);
                r.removeFromTop (10);
            }
            L.errorBox = r;
        }
        else
            L.notice = r.removeFromTop (24);
        return L;
    }

    void InstallerComponent::resized()
    {
        auto r = getLocalBounds().reduced (28, 22);
        r.removeFromTop (44 + 14 + 1 + 20 + 38);
        auto bottom = r.removeFromBottom (36);
        r.removeFromBottom (16);

        const auto right = [&bottom] (juce::Button& b, int w) { b.setBounds (bottom.removeFromRight (w)); bottom.removeFromRight (8); };
        const auto left = [&bottom] (juce::Button& b, int w) { b.setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (6); };

        switch (page)
        {
            case Page::welcome:
            {
                right (continueButton, 150);
                right (retryButton, 110);
                if (newInstallerButton.isVisible()) left (newInstallerButton, 190);
                else                                left (copyButton, 130);
                const auto L = welcomeLayout();
                errorBox.setBounds (L.errorBox);
                auto row = L.tokenRow;
                tokenLabel.setBounds (row.removeFromLeft (104));
                saveTokenButton.setBounds (row.removeFromRight (140));
                row.removeFromRight (8);
                tokenField.setBounds (row);
                auto actions = L.actionRow;
                getAccessButton.setBounds (actions.removeFromLeft (190));
                actions.removeFromLeft (10);
                useCopiedButton.setBounds (actions.removeFromLeft (180));
                break;
            }
            case Page::choose:
            {
                right (installButton, 160);
                right (backButton, 90);
                left (selectUpdates, 130);
                left (selectAll, 56);
                left (selectNone, 64);
                r.removeFromTop (6);
                auto loc = r.removeFromTop (30);
                locationLabel.setBounds (loc.removeFromLeft (140));
                locationBox.setBounds (loc.removeFromLeft (juce::jmin (loc.getWidth(), 520)));
                r.removeFromTop (12);
                viewport.setBounds (r);
                if (rows != nullptr)
                    rows->layout (r.getWidth() - (rows->getHeight() > r.getHeight() ? 12 : 0));
                if (rows != nullptr && rows->getHeight() > r.getHeight())
                    rows->layout (r.getWidth() - 12);
                break;
            }
            case Page::progress:
                right (cancelButton, 110);
                left (copyButton, 130);
                r.removeFromTop (8);
                progressBar.setBounds (r.removeFromTop (28));
                r.removeFromTop (12);
                log.setBounds (r);
                break;
            case Page::done:
                right (closeButton, 120);
                right (againButton, 130);
                left (copyButton, 130);
                r.removeFromTop (34);
                log.setBounds (r);
                break;
        }
    }
}
