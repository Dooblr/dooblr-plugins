#include "PluginEditor.h"

#include "Presets.h"

namespace dooblrfeedback
{
    namespace id = params::id;
    namespace Palette = ui::Palette;
    using Spec = ui::ControlPanel::Spec;
    using Kind = ui::ControlPanel::Kind;

    namespace
    {
        void styleLabel (juce::Label& label, const juce::String& text, float fontSize, juce::Colour colour,
                         juce::Justification justification, bool bold = false)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (juce::FontOptions (fontSize, bold ? juce::Font::bold : juce::Font::plain)));
            label.setColour (juce::Label::textColourId, colour);
            label.setJustificationType (justification);
            label.setInterceptsMouseClicks (false, false);
        }

        constexpr int factoryIdBase = 1, userIdBase = 1001;
        constexpr int toolbarHeight = 30, margin = 12, panelGap = 8, meterWidth = 16, tabWidth = 92, tabHeight = 26, tabGap = 4;
    }

    DooblrFeedbackEditor::DooblrFeedbackEditor (DooblrFeedbackProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap()),
          modeToggle (p.getState(), id::mode, { "SAFE", "AGGRESSIVE" }, Palette::accent),
          graph (p),
          notchTable (p)
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 20.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        titleLabel.setMinimumHorizontalScale (0.7f);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 11.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        statusLabel.setMinimumHorizontalScale (0.6f);
        styleLabel (latencyLabel, {}, 11.0f, Palette::textMuted, juce::Justification::centredLeft);
        latencyLabel.setComponentID ("latency");
        latencyLabel.setMinimumHorizontalScale (0.6f);
        styleLabel (sensitivityCaption, "SENSITIVITY", 13.0f, Palette::textPrimary, juce::Justification::centred, true);
        styleLabel (maxCutCaption, "MAX CUT", 11.0f, Palette::textMuted, juce::Justification::centred, true);
        styleLabel (bottomTitle, {}, 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);
        bottomTitle.setComponentID ("bottomTitle");
        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &latencyLabel, &sensitivityCaption, &maxCutCaption, &bottomTitle })
            addAndMakeVisible (label);

        const auto setupKnob = [this] (juce::Slider& knob, const juce::String& pid, juce::Colour colour)
        {
            auto* parameter = state.getParameter (pid);
            knob.setComponentID (pid);
            knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
            knob.setColour (juce::Slider::textBoxOutlineColourId, Palette::divider);
            knob.setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelRaised);
            sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, knob));
            knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
            knob.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            addAndMakeVisible (knob);
        };
        setupKnob (sensitivityKnob, id::sensitivity, Palette::accent);
        setupKnob (maxCutKnob, id::maxCut, Palette::dynamic);
        sensitivityKnob.setTooltip ("Sensitivity: higher catches quieter, shorter rings sooner; lower waits for clearer evidence "
                                    "(double-click resets, Shift-drag for fine moves)");
        maxCutKnob.setTooltip (juce::String (juce::CharPointer_UTF8 ("Max cut: the deepest any one notch may go; turned fully up it is \xe2\x88\x9e, a true notch "
                                                                     "that removes its frequency completely (double-click resets to 18 dB)")));

        safetyBadge.setComponentID ("safetyBadge");
        safetyBadge.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        safetyBadge.setJustificationType (juce::Justification::centred);
        safetyBadge.setColour (juce::Label::backgroundColourId, juce::Colour (0xffe0574f) /* meterRed (design §9) */);
        safetyBadge.setColour (juce::Label::textColourId, juce::Colours::white);
        safetyBadge.setTooltip ("Safety duck: the output is over the Safety threshold while feedback is running away, "
                                "so the whole level is pulled down until it is under control (Advanced view)");
        addChildComponent (safetyBadge);

        // Left column switches.
        modeToggle.setTooltip ("Safe: waits longer and rejects anything with harmonics (music). "
                               "Aggressive: reacts faster and deeper, for speech or when feedback must never be heard");
        autoButton.setComponentID (id::detect);
        ringOutButton.setComponentID (id::ringOut);
        sidechainButton.setComponentID (id::sidechain);
        bypassButton.setComponentID (id::bypass);
        autoButton.setTooltip ("Auto: detect feedback and add notches. Off: keep the current notches, add none");
        ringOutButton.setTooltip ("Ring-out: every notch found becomes fixed (stays until cleared). "
                                  "Use it while ringing out the room, then switch it off for the show");
        sidechainButton.setTooltip ("SC: detect on the sidechain input (a reference mic), notch the main input");
        bypassButton.setTooltip ("Bypass: the input unchanged");
        for (auto* b : { &autoButton, &ringOutButton, &sidechainButton })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        }
        ringOutButton.setColour (juce::TextButton::buttonOnColourId, Palette::dynamic.withAlpha (0.55f));
        autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::detect, autoButton);
        ringOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::ringOut, ringOutButton);
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::sidechain, sidechainButton);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);

        // Toolbar.
        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { proc.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { proc.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory settings and your room presets (saved in " + presets::userDirectory().getFullPathName() + ")");
        presetBox.onChange = [this] { presetChosen(); };
        presetPrev.setComponentID ("presetPrev");
        presetNext.setComponentID ("presetNext");
        presetPrev.setTooltip ("Previous preset");
        presetNext.setTooltip ("Next preset");
        presetPrev.onClick = [this] { stepPreset (-1); };
        presetNext.onClick = [this] { stepPreset (1); };
        presetMenu.setComponentID ("presetMenu");
        presetMenu.setTooltip ("Save, load or rename a room preset (settings, notches and notes)");
        presetMenu.onClick = [this] { showPresetMenu(); };

        compareA.setComponentID ("compareA");
        compareB.setComponentID ("compareB");
        compareCopy.setComponentID ("compareCopy");
        compareA.setTooltip ("Compare: settings A (notches and notes are shared)");
        compareB.setTooltip ("Compare: settings B (the first time, B starts as a copy of A)");
        compareCopy.setTooltip ("Copy the current settings to the other compare slot");
        for (auto* b : { &compareA, &compareB })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        }
        compareA.onClick = [this] { proc.selectCompareSlot (0); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareB.onClick = [this] { proc.selectCompareSlot (1); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareCopy.onClick = [this] { proc.copyToOtherCompareSlot(); setStatus ("Copied to " + juce::String (proc.getCompareSlot() == 0 ? "B" : "A")); };

        clearAllButton.setComponentID ("clearAll");
        clearAllButton.setTooltip ("Clear every notch, fixed ones included");
        clearAllButton.onClick = [this]
        {
            proc.clearAllNotches();
            graph.refreshNotches();
            notchTable.refresh();
            setStatus ("Cleared all notches");
        };

        for (juce::Component* c : { (juce::Component*) &modeToggle, (juce::Component*) &autoButton, (juce::Component*) &ringOutButton,
                                    (juce::Component*) &sidechainButton, (juce::Component*) &bypassButton, (juce::Component*) &undoButton,
                                    (juce::Component*) &redoButton, (juce::Component*) &presetPrev, (juce::Component*) &presetBox,
                                    (juce::Component*) &presetNext, (juce::Component*) &presetMenu, (juce::Component*) &compareA,
                                    (juce::Component*) &compareB, (juce::Component*) &compareCopy, (juce::Component*) &clearAllButton,
                                    (juce::Component*) &graph, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        // Bottom: Notches | Notes | Advanced.
        const char* tabNames[] = { "Notches", "Notes", "Advanced" };
        const char* tabIds[] = { "viewNotches", "viewNotes", "viewAdvanced" };
        for (int i = 0; i < 3; ++i)
        {
            auto& tab = viewTabs[(size_t) i];
            tab.setButtonText (tabNames[i]);
            tab.setComponentID (tabIds[i]);
            tab.setRadioGroupId (4001);
            tab.setClickingTogglesState (true);
            tab.setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
            tab.onClick = [this, i] { if (viewTabs[(size_t) i].getToggleState()) setView ((View) i); };
            addAndMakeVisible (tab);
        }

        addChildComponent (notchTable);
        notchTable.onNotchSelected = [this] (int notchId) { graph.setSelectedNotch (notchId); };
        notchTable.onBankChanged = [this] { graph.refreshNotches(); };
        notchTable.onNotchHovered = [this] (int notchId) { graph.setHoveredNotch (notchId); };
        graph.onNotchSelected = [this] (int notchId) { selectNotch (notchId); };

        notesEditor.setComponentID ("notes");
        notesEditor.setMultiLine (true, true);
        notesEditor.setReturnKeyStartsNewLine (true);
        notesEditor.setScrollbarsShown (true);
        notesEditor.setFont (juce::FontOptions (13.0f));
        notesEditor.setColour (juce::TextEditor::backgroundColourId, Palette::panelRaised);
        notesEditor.setColour (juce::TextEditor::textColourId, Palette::textPrimary);
        notesEditor.setColour (juce::TextEditor::outlineColourId, Palette::divider);
        notesEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        notesEditor.setTooltip ("Notes for this room or show; saved with the session and in room presets");
        notesEditor.setText (proc.getNotes(), false);
        syncedNotes = proc.getNotes();
        notesEditor.onTextChange = [this] { syncNotes(); };
        addChildComponent (notesEditor);

        advancedPanel = std::make_unique<ui::ControlPanel> (state, "ADVANCED", Palette::dynamic, std::vector<Spec> {
            { id::notchQ, "Notch Q" }, { id::liveNotches, "Auto-recover", Kind::combo, 112 }, { id::release, "Recover after" },
            { id::recoverSpeed, "Recover speed", Kind::combo, 112 },
            { id::rangeLow, "Range low" }, { id::rangeHigh, "Range high" }, { id::output, "Output" },
            { id::safety, "Safety duck", Kind::toggle }, { id::safetyTrigger, "Duck when", Kind::combo, 112 },
            { id::safetyThreshold, "Threshold" }, { id::safetyDepth, "Depth" }, { id::safetyRelease, "Recover" } });
        analyzerSpeed.caption.setText ("Analyzer", juce::dontSendNotification);
        analyzerSpeed.box.setComponentID ("analyzerSpeed");
        for (int i = 0; i < ui::numAnalyzerSpeeds; ++i)
            analyzerSpeed.box.addItem (ui::analyzerSpeedName (ui::analyzerSpeedFromIndex (i)), i + 1);
        analyzerSpeed.box.setSelectedId (proc.getAnalyzerSpeed() + 1, juce::dontSendNotification);
        analyzerSpeed.box.setTooltip ("Analyzer speed: how fast the drawn spectrum follows the audio (display only)");
        analyzerSpeed.box.onChange = [this]
        {
            if (const auto selected = analyzerSpeed.box.getSelectedId(); selected > 0)
                proc.setAnalyzerSpeed (selected - 1);
        };
        advancedPanel->addExtra (analyzerSpeed, 112, -1, {}, true);
        addChildComponent (*advancedPanel);

        proc.presetChanged.addChangeListener (this);
        proc.roomChanged.addChangeListener (this);
        refreshPresetBox();
        setView (View::notches);
        updateCompareButtons();
        refresh();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
    }

    DooblrFeedbackEditor::~DooblrFeedbackEditor()
    {
        stopTimer();
        proc.presetChanged.removeChangeListener (this);
        proc.roomChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void DooblrFeedbackEditor::setView (View v)
    {
        view = v;
        for (int i = 0; i < 3; ++i)
            viewTabs[(size_t) i].setToggleState (i == (int) v, juce::dontSendNotification);
        notchTable.setVisible (v == View::notches);
        notesEditor.setVisible (v == View::notes);
        advancedPanel->setVisible (v == View::advanced);
        bottomTitle.setVisible (v != View::advanced);
        refresh();
        resized();
        repaint();
    }

    void DooblrFeedbackEditor::selectNotch (int notchId)
    {
        graph.setSelectedNotch (notchId);
        if (view != View::notches)
            setView (View::notches);
        notchTable.refresh();
        notchTable.selectNotch (notchId);
    }

    juce::Component* DooblrFeedbackEditor::findControl (const juce::String& pid) const
    {
        if (auto* c = advancedPanel->findControl (pid))
            return c;
        for (auto* c : { (juce::Component*) &sensitivityKnob, (juce::Component*) &maxCutKnob, (juce::Component*) &modeToggle,
                         (juce::Component*) &autoButton, (juce::Component*) &ringOutButton, (juce::Component*) &sidechainButton,
                         (juce::Component*) &bypassButton })
            if (c->getComponentID() == pid)
                return const_cast<juce::Component*> (c);
        return nullptr;
    }

    void DooblrFeedbackEditor::updateCompareButtons()
    {
        const auto slot = proc.getCompareSlot();
        compareA.setToggleState (slot == 0, juce::dontSendNotification);
        compareB.setToggleState (slot == 1, juce::dontSendNotification);
        compareCopy.setButtonText (juce::String (juce::CharPointer_UTF8 (slot == 0 ? "A\xe2\x86\x92" "B" : "B\xe2\x86\x92" "A")));
    }

    void DooblrFeedbackEditor::syncNotes()
    {
        // Typed text goes to the processor (on the change message or, at the latest, the next
        // timer tick); text set from outside (a preset or session load) comes back to the editor.
        const auto shown = notesEditor.getText();
        if (shown != syncedNotes)
        {
            syncedNotes = shown;
            proc.setNotes (shown);
        }
        else if (shown != proc.getNotes())
        {
            syncedNotes = proc.getNotes();
            notesEditor.setText (syncedNotes, false);
        }
    }

    void DooblrFeedbackEditor::refresh()
    {
        updateUndoButtons();
        if (analyzerSpeed.box.getSelectedId() != proc.getAnalyzerSpeed() + 1)
            analyzerSpeed.box.setSelectedId (proc.getAnalyzerSpeed() + 1, juce::dontSendNotification);
        syncNotes();

        notchTable.refresh();

        const auto connected = proc.isSidechainConnected();
        ui::setDimmed (sidechainButton, ! connected);
        sidechainButton.setTooltip (connected ? "SC: detect on the sidechain input (a reference mic), notch the main input"
                                              : "SC: no sidechain input is routed in the host, so detection uses the main input");
        updateCompareButtons();

        const auto latency = proc.getLatencySamples();
        const auto sr = std::max (1.0, proc.getEngine().getSampleRate());
        const auto latencyText = "Latency " + juce::String (latency) + " smp / " + params::formatValue (latency * 1000.0 / sr, 1) + " ms";
        if (latencyLabel.getText() != latencyText)
            latencyLabel.setText (latencyText, juce::dontSendNotification);

        int active = 0, fixed = 0, ringing = 0;
        for (const auto& n : proc.getNotches())
            if (n.active)
            {
                ++active;
                fixed += n.fixed ? 1 : 0;
                ringing += n.ringing ? 1 : 0;
            }
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
        const auto title = "NOTCHES  " + juce::String (active) + " / " + juce::String (dsp::maxNotches)
                         + (fixed > 0 ? dot + juce::String (fixed) + " fixed" : juce::String())
                         + (ringing > 0 ? dot + juce::String (ringing) + " ringing" : juce::String());
        const auto shownTitle = view == View::notes ? juce::String ("ROOM NOTES") : title;
        if (bottomTitle.getText() != shownTitle)
            bottomTitle.setText (shownTitle, juce::dontSendNotification);
        clearAllButton.setEnabled (active > 0);

        // Safety duck: the toolbar badge, the meter's threshold tick and the status line.
        const auto safety = proc.getEngine().getSafety();
        const auto safetyOn = valueOfBool (id::safety);
        const auto ducking = safety.active && safety.reductionDb > 0.5f;
        if (ducking)
        {
            const auto text = safety.reductionDb >= dsp::FeedbackEngine::fullDuckDb - 0.5f
                                ? juce::String ("SAFETY MUTE")
                                : "SAFETY " + params::formatCut (std::round (safety.reductionDb), true, 0);
            if (safetyBadge.getText() != text) safetyBadge.setText (text, juce::dontSendNotification);
        }
        safetyBadge.setVisible (ducking);
        outputMeter.setThresholdDb (safetyOn ? state.getRawParameterValue (id::safetyThreshold)->load() + state.getRawParameterValue (id::output)->load()
                                             : std::numeric_limits<float>::quiet_NaN());

        if (juce::Time::getMillisecondCounter() > statusUntilMs)
        {
            juce::String status;
            if (ducking)
                status = "Safety duck: feedback over the threshold";
            else if (proc.getEngine().wasBankFull())
                status = "All " + juce::String (dsp::maxNotches) + " notches in use";
            else if (valueOfBool (id::bypass))
                status = "Bypassed";
            else if (! valueOfBool (id::detect))
                status = "Auto off: holding the current notches";
            else if (valueOfBool (id::ringOut))
                status = "Ring-out: new notches are fixed";
            else
                status = ringing > 0 ? "Suppressing" : "Listening";
            if (safety.events > 0)
                status += dot + "ducked " + juce::String (safety.events) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97 (")) + params::formatValue (safety.totalSec, 1) + " s)";
            if (statusLabel.getText() != status)
                statusLabel.setText (status, juce::dontSendNotification);
        }

        if (proc.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    bool DooblrFeedbackEditor::valueOfBool (const juce::String& pid) const
    {
        return state.getRawParameterValue (pid)->load() > 0.5f;
    }

    void DooblrFeedbackEditor::setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
        statusUntilMs = juce::Time::getMillisecondCounter() + 4000;
    }

    //==============================================================================
    void DooblrFeedbackEditor::refreshPresetBox()
    {
        const juce::ScopedValueSetter<bool> guard (updatingPresetBox, true);
        presetBox.clear (juce::dontSendNotification);
        presetBox.addSectionHeading ("Factory");
        const auto& factory = presets::factory();
        for (int i = 0; i < (int) factory.size(); ++i)
            presetBox.addItem (factory[(size_t) i].name, factoryIdBase + i);

        userPresetFiles = presets::userPresets();
        if (! userPresetFiles.isEmpty())
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading ("Rooms");
            for (int i = 0; i < userPresetFiles.size(); ++i)
                presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), userIdBase + i);
        }

        shownPresetName = proc.getPresetName();
        bool selected = false;
        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemText (i) == shownPresetName)
            {
                presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
                selected = true;
                break;
            }
        if (! selected)
            presetBox.setText (shownPresetName, juce::dontSendNotification);
    }

    void DooblrFeedbackEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;
        const auto itemId = presetBox.getSelectedId();
        if (itemId >= userIdBase && itemId - userIdBase < userPresetFiles.size())
            setStatus (proc.loadUserPreset (userPresetFiles[itemId - userIdBase]));
        else if (itemId >= factoryIdBase && itemId < userIdBase)
            proc.setCurrentProgram (itemId - factoryIdBase);
        refreshPresetBox();
        graph.refreshNotches();
        refresh();
    }

    void DooblrFeedbackEditor::stepPreset (int delta)
    {
        juce::Array<int> ids;
        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemId (i) != 0)
                ids.add (presetBox.getItemId (i));
        if (ids.isEmpty())
            return;
        const auto current = ids.indexOf (presetBox.getSelectedId());
        const auto next = current < 0 ? (delta > 0 ? 0 : ids.size() - 1)
                                      : (current + delta % ids.size() + ids.size()) % ids.size();
        presetBox.setSelectedId (ids[next], juce::sendNotificationSync);
    }

    void DooblrFeedbackEditor::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save room preset...");
        menu.addItem (2, "Load room preset...");
        auto currentIsUser = false;
        for (const auto& f : userPresetFiles)
            currentIsUser = currentIsUser || f.getFileNameWithoutExtension() == proc.getPresetName();
        menu.addItem (3, "Rename \"" + proc.getPresetName() + "\"...", currentIsUser);
        juce::Component::SafePointer<DooblrFeedbackEditor> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenu),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr) return;
                                if (result == 1) safeThis->savePreset();
                                if (result == 2) safeThis->loadPreset();
                                if (result == 3) safeThis->renamePreset();
                            });
    }

    void DooblrFeedbackEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (proc.getPresetName());
        if (name.isEmpty())
            name = "Room";
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-feedback room preset", folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrFeedbackEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      const auto file = chooser.getResult().withFileExtension (presets::fileExtension);
                                      safeThis->setStatus (safeThis->proc.saveUserPreset (file));
                                      safeThis->refreshPresetBox();
                                  });
    }

    void DooblrFeedbackEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-feedback room preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrFeedbackEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                          return;
                                      safeThis->setStatus (safeThis->proc.loadUserPreset (chooser.getResult()));
                                      safeThis->refreshPresetBox();
                                      safeThis->graph.refreshNotches();
                                      safeThis->refresh();
                                  });
    }

    void DooblrFeedbackEditor::renamePreset()
    {
        juce::File file;
        for (const auto& f : userPresetFiles)
            if (f.getFileNameWithoutExtension() == proc.getPresetName())
                file = f;
        if (file == juce::File())
            return;

        renameWindow = std::make_unique<juce::AlertWindow> ("Rename room preset", "New name for \"" + file.getFileNameWithoutExtension() + "\"",
                                                            juce::MessageBoxIconType::NoIcon, this);
        renameWindow->addTextEditor ("name", file.getFileNameWithoutExtension());
        renameWindow->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        renameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<DooblrFeedbackEditor> safeThis (this);
        renameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, file] (int result)
        {
            if (safeThis == nullptr || safeThis->renameWindow == nullptr) return;
            const auto newName = safeThis->renameWindow->getTextEditorContents ("name").trim();
            safeThis->renameWindow.reset();
            if (result != 1 || newName.isEmpty()) return;
            safeThis->setStatus (safeThis->proc.renameUserPreset (file, newName));
            safeThis->refreshPresetBox();
        }), false);
    }

    void DooblrFeedbackEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &proc.roomChanged)
        {
            syncNotes();
            graph.refreshNotches();
            notchTable.refresh();
            return;
        }
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrFeedbackEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrFeedbackEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto command = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;
        auto& history = proc.getUndoHistory();
        if (key == juce::KeyPress ('z', command, 0))
        {
            history.undo();
            updateUndoButtons();
            return true;
        }
        if (key == juce::KeyPress ('z', command | shift, 0) || key == juce::KeyPress ('y', command, 0))
        {
            history.redo();
            updateUndoButtons();
            return true;
        }
        return false;
    }

    //==============================================================================
    void DooblrFeedbackEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (columnArea.toFloat(), 6.0f);
        g.setColour (Palette::accent);
        g.fillRect (columnArea.getX() + 12, titleLabel.getBottom() + 2, 28, 2);
        g.setColour (Palette::divider);
        g.fillRect (columnArea.getX() + 12, autoButton.getBottom() + 10, columnArea.getWidth() - 24, 1);
        g.fillRect (toolbarArea.getX(), toolbarArea.getBottom() + 3, toolbarArea.getWidth(), 1);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (tabsArea.toFloat(), 4.0f);
        if (view != View::advanced)
            g.fillRoundedRectangle (bottomArea.toFloat(), 4.0f);
    }

    void DooblrFeedbackEditor::resized()
    {
        auto content = getLocalBounds().reduced (margin);

        columnArea = content.removeFromLeft (juce::jlimit (190, 400, getWidth() * 19 / 100));
        content.removeFromLeft (panelGap + 2);
        {
            auto col = columnArea.reduced (12, 12);
            titleLabel.setBounds (col.removeFromTop (26));
            col.removeFromTop (6);
            auto versionRow = col.removeFromTop (26);
            bypassButton.setBounds (versionRow.removeFromRight (86));
            subtitleLabel.setBounds (versionRow);
            col.removeFromTop (12);
            modeToggle.setBounds (col.removeFromTop (32));
            col.removeFromTop (8);
            auto row = col.removeFromTop (28);
            const auto scWidth = 40, gap = 4;
            sidechainButton.setBounds (row.removeFromRight (scWidth));
            row.removeFromRight (gap);
            const auto half = (row.getWidth() - gap) / 2;
            autoButton.setBounds (row.removeFromLeft (half));
            row.removeFromLeft (gap);
            ringOutButton.setBounds (row);
            col.removeFromTop (20);

            statusLabel.setBounds (col.removeFromBottom (16));
            latencyLabel.setBounds (col.removeFromBottom (16));
            col.removeFromBottom (8);

            constexpr int captionHeight = 18, heroBox = 24, smallBox = 20, knobGap = 12;
            const auto spare = col.getHeight() - 2 * captionHeight - heroBox - smallBox - knobGap;
            const auto hero = juce::jlimit (80, 300, juce::jmin (col.getWidth(), spare * 2 / 3));
            const auto small = juce::jlimit (48, 120, juce::jmin (col.getWidth() / 2, spare - hero));
            auto knobs = col.withSizeKeepingCentre (col.getWidth(), 2 * captionHeight + heroBox + smallBox + knobGap + hero + small);
            sensitivityCaption.setBounds (knobs.removeFromTop (captionHeight));
            sensitivityKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jlimit (72, 120, hero / 2), heroBox - 4);
            sensitivityKnob.getProperties().set ("maxKnobDiameter", (double) hero);
            sensitivityKnob.setBounds (knobs.removeFromTop (hero + heroBox).withSizeKeepingCentre (juce::jmax (hero, 120), hero + heroBox));
            knobs.removeFromTop (knobGap);
            maxCutCaption.setBounds (knobs.removeFromTop (captionHeight));
            maxCutKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jlimit (64, 96, small), smallBox - 4);
            maxCutKnob.getProperties().set ("maxKnobDiameter", (double) small);
            maxCutKnob.setBounds (knobs.removeFromTop (small + smallBox).withSizeKeepingCentre (juce::jmax (small, 96), small + smallBox));
        }

        // Bottom: view tabs on the left, then the table / notes / Advanced panel.
        const auto tabsWidth = tabWidth + 16;
        const auto viewWidth = content.getWidth() - tabsWidth - panelGap;
        const auto bottomHeight = juce::jmax (advancedPanel->getPreferredHeight (viewWidth),
                                              juce::jlimit (150, 330, (getHeight() - 2 * margin) * 30 / 100));
        auto bottom = content.removeFromBottom (bottomHeight);
        content.removeFromBottom (panelGap);
        tabsArea = bottom.removeFromLeft (tabsWidth);
        {
            auto tabs = tabsArea.reduced (8, 0).withTrimmedTop (ui::ControlPanel::titleHeight);
            tabs = tabs.withSizeKeepingCentre (tabWidth, 3 * tabHeight + 2 * tabGap);
            for (int i = 0; i < 3; ++i)
                viewTabs[(size_t) i].setBounds (tabs.getX(), tabs.getY() + i * (tabHeight + tabGap), tabWidth, tabHeight);
        }
        bottom.removeFromLeft (panelGap);
        bottomArea = bottom;
        advancedPanel->setBounds (bottom);
        {
            auto inner = bottom.reduced (8, 0);
            bottomTitle.setBounds (inner.removeFromTop (ui::ControlPanel::titleHeight));
            inner.removeFromBottom (8);
            notchTable.setBounds (inner);
            notesEditor.setBounds (inner);
        }

        toolbarArea = content.removeFromTop (toolbarHeight);
        {
            auto bar = toolbarArea;
            const auto place = [&bar] (juce::Component& c, int w, int h = 26) { c.setBounds (bar.removeFromLeft (w).withSizeKeepingCentre (w, h)); };
            const auto placeRight = [&bar] (juce::Component& c, int w, int h = 26) { c.setBounds (bar.removeFromRight (w).withSizeKeepingCentre (w, h)); };
            placeRight (clearAllButton, 80);
            bar.removeFromRight (10);
            placeRight (compareCopy, 48);
            bar.removeFromRight (2);
            placeRight (compareB, 28);
            bar.removeFromRight (2);
            placeRight (compareA, 28);
            bar.removeFromRight (10);
            placeRight (redoButton, 46);
            bar.removeFromRight (4);
            placeRight (undoButton, 46);
            bar.removeFromRight (12);

            place (presetPrev, 26);
            bar.removeFromLeft (2);
            // The safety badge's room is always kept (it appears without moving anything).
            const auto badgeSlot = bar.removeFromRight (112);
            bar.removeFromRight (8);
            // The box, then > and ... closed up to its right edge, then the badge.
            const auto presetWidth = juce::jmax (80, juce::jmin (bar.getWidth() - (2 + 26 + 2 + 30), 320));
            presetBox.setBounds (bar.removeFromLeft (presetWidth).withSizeKeepingCentre (presetWidth, 26));
            bar.removeFromLeft (2);
            place (presetNext, 26);
            bar.removeFromLeft (2);
            place (presetMenu, 30);
            safetyBadge.setBounds (badgeSlot.withX (juce::jmin (badgeSlot.getX(), presetMenu.getRight() + 8)).withSizeKeepingCentre (104, 22));
        }
        content.removeFromTop (panelGap);

        outputMeter.setBounds (content.removeFromRight (meterWidth));
        content.removeFromRight (4);
        graph.setBounds (content);
    }
}
