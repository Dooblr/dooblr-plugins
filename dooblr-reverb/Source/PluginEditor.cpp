#include "PluginEditor.h"

#include "DSP/RoomModels.h"
#include "Presets.h"

namespace dooblrreverb
{
    namespace id = params::id;
    namespace Palette = ui::Palette;

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
        constexpr int margin = 12, gap = 8, headerHeight = 60, barHeight = 30, meterWidth = 16;
        constexpr int knobCell = 84, knobDiameter = 46;

        float valueOf (juce::AudioProcessorValueTreeState& state, const juce::String& pid)
        {
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }
    }

    DooblrReverbEditor::DooblrReverbEditor (DooblrReverbProcessor& p)
        : juce::AudioProcessorEditor (p),
          owner (p),
          state (p.getState()),
          display (p),
          outputMeter (&p.getPeakTap())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        titleLabel.setMinimumHorizontalScale (0.7f);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString "  algorithmic reverb", 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 11.0f, Palette::textMuted, juce::Justification::centredRight);
        statusLabel.setComponentID ("status");
        statusLabel.setMinimumHorizontalScale (0.6f);
        styleLabel (decayReadout, {}, 15.0f, Palette::textPrimary, juce::Justification::centred, true);
        decayReadout.setComponentID ("decayReadout");
        styleLabel (roomReadout, {}, 11.0f, Palette::textMuted, juce::Justification::centred);
        roomReadout.setComponentID ("roomReadout");
        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &decayReadout, &roomReadout })
            addAndMakeVisible (label);

        // Knob row: three either side of the big Space knob.
        knobIds = { id::brightness, id::character, id::distance, id::space, id::decayRate, id::width, id::mix };
        const std::array<const char*, numKnobs> captions { "BRIGHTNESS", "CHARACTER", "DISTANCE", "SPACE", "DECAY RATE", "STEREO WIDTH", "MIX" };
        const std::array<const char*, numKnobs> tips {
            "Brightness: how fast the highs die away compared with the lows (also darkens the early reflections)",
            "Character: from a smooth, still tail to a lively, modulated, chorused one",
            "Distance: near (strong early reflections) to far (diffuse, later, softer reflections)",
            "Space: the room, from a small ambience to an infinite space; sets the size and the decay time",
            "Decay Rate: scales the decay time (50 % to 200 %) without changing the room",
            "Stereo Width: 0 % mono, 100 % natural, 150 % extra wide (mono compatible)",
            "Mix: dry / wet balance (presets keep the current Mix)" };
        for (int i = 0; i < numKnobs; ++i)
        {
            const auto hero = knobIds[(size_t) i] == juce::String (id::space);
            setupKnob (knobs[(size_t) i], knobIds[(size_t) i], Palette::accent, ! hero);
            knobs[(size_t) i].setTooltip (juce::String (tips[(size_t) i]) + " (double-click resets, Shift-drag for fine moves)");
            styleLabel (knobCaptions[(size_t) i], captions[(size_t) i], hero ? 13.0f : 12.0f,
                        hero ? Palette::textPrimary : Palette::textMuted, juce::Justification::centred, hero);
            addAndMakeVisible (knobCaptions[(size_t) i]);
        }

        // Toolbar.
        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { owner.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { owner.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory and user presets (user presets live in " + presets::userDirectory().getFullPathName()
                              + "). Presets keep Mix, Input, Output and Bypass");
        presetBox.onChange = [this] { presetChosen(); };
        presetPrev.setComponentID ("presetPrev");
        presetNext.setComponentID ("presetNext");
        presetPrev.setTooltip ("Previous preset");
        presetNext.setTooltip ("Next preset");
        presetPrev.onClick = [this] { stepPreset (-1); };
        presetNext.onClick = [this] { stepPreset (1); };
        presetMenu.setComponentID ("presetMenu");
        presetMenu.setTooltip ("Save, load or initialise a preset");
        presetMenu.onClick = [this] { showPresetMenu(); };

        compareA.setComponentID ("compareA");
        compareB.setComponentID ("compareB");
        compareCopy.setComponentID ("compareCopy");
        compareA.setTooltip ("Compare: settings A (kept while the plug-in is open, not saved with the session)");
        compareB.setTooltip ("Compare: settings B (the first time, B starts as a copy of A)");
        compareCopy.setTooltip ("Copy the current settings to the other compare slot");
        for (auto* b : { &compareA, &compareB })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        }
        compareA.onClick = [this] { owner.selectCompareSlot (0); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareB.onClick = [this] { owner.selectCompareSlot (1); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareCopy.onClick = [this]
        {
            owner.copyToOtherCompareSlot();
            statusLabel.setText ("Copied to " + juce::String (owner.getCompareSlot() == 0 ? "B" : "A"), juce::dontSendNotification);
        };

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetPrev,
                                    (juce::Component*) &presetBox, (juce::Component*) &presetNext, (juce::Component*) &presetMenu,
                                    (juce::Component*) &compareA, (juce::Component*) &compareB, (juce::Component*) &compareCopy,
                                    (juce::Component*) &display, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        // Bottom bar.
        styleLabel (analyzerCaption, "ANALYZER", 11.0f, Palette::textMuted, juce::Justification::centredRight, true);
        styleLabel (predelayCaption, "PREDELAY", 11.0f, Palette::textMuted, juce::Justification::centredRight, true);
        styleLabel (inputCaption, "IN", 11.0f, Palette::textMuted, juce::Justification::centredRight, true);
        styleLabel (outputCaption, "OUT", 11.0f, Palette::textMuted, juce::Justification::centredRight, true);
        for (auto* label : { &analyzerCaption, &predelayCaption, &inputCaption, &outputCaption })
            addAndMakeVisible (label);

        // Analyzer: a display preference saved with the session (not a parameter).
        analyzerBox.setComponentID ("analyzer");
        analyzerBox.addItemList (DooblrReverbProcessor::analyzerModeNames(), 1);
        analyzerBox.setSelectedId (owner.getAnalyzerMode() + 1, juce::dontSendNotification);
        analyzerBox.setTooltip ("Spectrum display: off, the reverb output, or the input as well (display only)");
        analyzerBox.onChange = [this]
        {
            if (const auto selected = analyzerBox.getSelectedId(); selected > 0)
                owner.setAnalyzerMode (selected - 1);
        };
        addAndMakeVisible (analyzerBox);

        setupBarSlider (predelaySlider, id::predelay, Palette::accent);
        predelaySlider.setTooltip ("Predelay: time before the reverb starts (0 to 500 ms)");
        syncButton.setComponentID (id::predelaySync);
        syncButton.setTooltip ("Sync the predelay to the host tempo (a note division, at most 500 ms)");
        syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::predelaySync, syncButton);
        addAndMakeVisible (syncButton);
        divisionBox.setComponentID (id::predelayDiv);
        divisionBox.addItemList (params::divisionNames(), 1);
        divisionBox.setTooltip ("Synced predelay: note division (dotted and triplet values included)");
        divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id::predelayDiv, divisionBox);
        addAndMakeVisible (divisionBox);

        setupBarSlider (inputSlider, id::input, Palette::spectrum);
        setupBarSlider (outputSlider, id::output, Palette::spectrum);
        inputSlider.setTooltip ("Input: level into the reverb (the dry signal follows it)");
        outputSlider.setTooltip ("Output: level after the mix");

        bypassButton.setComponentID (id::bypass);
        bypassButton.setTooltip ("Bypass: the untouched input (crossfaded)");
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);
        addAndMakeVisible (bypassButton);

        owner.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateCompareButtons();
        refresh();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
    }

    DooblrReverbEditor::~DooblrReverbEditor()
    {
        stopTimer();
        owner.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void DooblrReverbEditor::setupKnob (juce::Slider& knob, const juce::String& pid, juce::Colour colour, bool textBox)
    {
        auto* parameter = state.getParameter (pid);
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (textBox ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, false, 74, 18);
        knob.setComponentID (pid);
        knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
        knob.setColour (juce::Slider::textBoxOutlineColourId, Palette::divider);
        knob.setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelRaised);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, knob));
        knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        knob.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        addAndMakeVisible (knob);
    }

    void DooblrReverbEditor::setupBarSlider (juce::Slider& slider, const juce::String& pid, juce::Colour colour)
    {
        auto* parameter = state.getParameter (pid);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        slider.setComponentID (pid);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setColour (juce::Slider::textBoxOutlineColourId, Palette::divider);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelRaised);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, slider));
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        addAndMakeVisible (slider);
    }

    //==============================================================================
    juce::Component* DooblrReverbEditor::findControl (const juce::String& pid) const
    {
        for (auto& k : knobs)
            if (k.getComponentID() == pid)
                return const_cast<juce::Slider*> (&k);
        for (auto* c : { (const juce::Component*) &predelaySlider, (const juce::Component*) &syncButton, (const juce::Component*) &divisionBox,
                         (const juce::Component*) &inputSlider, (const juce::Component*) &outputSlider, (const juce::Component*) &bypassButton })
            if (c->getComponentID() == pid)
                return const_cast<juce::Component*> (c);
        return nullptr;
    }

    void DooblrReverbEditor::refresh()
    {
        updateUndoButtons();
        updateCompareButtons();

        if (analyzerBox.getSelectedId() != owner.getAnalyzerMode() + 1)
            analyzerBox.setSelectedId (owner.getAnalyzerMode() + 1, juce::dontSendNotification);

        // The big readout: the decay time (Space x Decay Rate) and the room it is in.
        const auto space = valueOf (state, id::space), rate = valueOf (state, id::decayRate);
        const auto decayText = params::formatSeconds (dsp::ReverbEngine::decaySecondsFor (space, rate));
        if (decayReadout.getText() != decayText)
            decayReadout.setText (decayText, juce::dontSendNotification);
        const auto roomText = dsp::roomNameAt (space);
        if (roomReadout.getText() != roomText)
            roomReadout.setText (roomText, juce::dontSendNotification);

        // Predelay: the ms slider is dimmed while synced, the division while free (both stay live).
        const auto synced = valueOf (state, id::predelaySync) > 0.5f;
        ui::setDimmed (predelaySlider, synced);
        ui::setDimmed (divisionBox, ! synced);
        if (synced)
        {
            const auto tip = "Synced predelay: " + params::formatValue (owner.currentPredelayMs(), 1) + " ms at "
                           + params::formatValue (owner.getHostBpm(), 1) + " BPM";
            if (divisionBox.getTooltip() != tip)
                divisionBox.setTooltip (tip);
        }

        if (auto* width = findControl (id::width))
            ui::setDimmed (*width, owner.getMainBusNumOutputChannels() < 2);

        if (owner.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    //==============================================================================
    void DooblrReverbEditor::refreshPresetBox()
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
            presetBox.addSectionHeading ("User");
            for (int i = 0; i < userPresetFiles.size(); ++i)
                presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), userIdBase + i);
        }

        shownPresetName = owner.getPresetName();
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

    void DooblrReverbEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;

        const auto itemId = presetBox.getSelectedId();
        if (itemId >= userIdBase && itemId - userIdBase < userPresetFiles.size())
            statusLabel.setText (owner.loadUserPreset (userPresetFiles[itemId - userIdBase]), juce::dontSendNotification);
        else if (itemId >= factoryIdBase && itemId < userIdBase)
            owner.setCurrentProgram (itemId - factoryIdBase);

        refreshPresetBox();
        refresh();
    }

    void DooblrReverbEditor::stepPreset (int delta)
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

    void DooblrReverbEditor::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save preset...");
        menu.addItem (2, "Load preset...");
        menu.addSeparator();
        menu.addItem (3, "Init (Default)");
        juce::Component::SafePointer<DooblrReverbEditor> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenu),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;
                                if (result == 1) safeThis->savePreset();
                                if (result == 2) safeThis->loadPreset();
                                if (result == 3) safeThis->initPreset();
                            });
    }

    void DooblrReverbEditor::initPreset()
    {
        owner.setCurrentProgram (0);
        refreshPresetBox();
        refresh();
    }

    void DooblrReverbEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (owner.getPresetName());
        if (name.isEmpty())
            name = "Preset";

        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-reverb preset", folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrReverbEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      const auto file = chooser.getResult().withFileExtension (presets::fileExtension);
                                      safeThis->statusLabel.setText (safeThis->owner.saveUserPreset (file), juce::dontSendNotification);
                                      safeThis->refreshPresetBox();
                                  });
    }

    void DooblrReverbEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-reverb preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrReverbEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                          return;
                                      safeThis->statusLabel.setText (safeThis->owner.loadUserPreset (chooser.getResult()), juce::dontSendNotification);
                                      safeThis->refreshPresetBox();
                                      safeThis->refresh();
                                  });
    }

    void DooblrReverbEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrReverbEditor::updateUndoButtons()
    {
        auto& history = owner.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void DooblrReverbEditor::updateCompareButtons()
    {
        const auto slot = owner.getCompareSlot();
        compareA.setToggleState (slot == 0, juce::dontSendNotification);
        compareB.setToggleState (slot == 1, juce::dontSendNotification);
        compareCopy.setButtonText (juce::String (juce::CharPointer_UTF8 (slot == 0 ? "A\xe2\x86\x92" "B" : "B\xe2\x86\x92" "A")));
    }

    bool DooblrReverbEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto command = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;
        auto& history = owner.getUndoHistory();

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
    void DooblrReverbEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (knobArea.toFloat(), 6.0f);
        g.fillRoundedRectangle (barArea.toFloat(), 4.0f);

        // Dividers either side of the Space column.
        g.setColour (Palette::divider);
        g.fillRect (heroArea.getX() - 1, knobArea.getY() + 14, 1, knobArea.getHeight() - 28);
        g.fillRect (heroArea.getRight(), knobArea.getY() + 14, 1, knobArea.getHeight() - 28);
    }

    void DooblrReverbEditor::resized()
    {
        auto area = getLocalBounds();

        // Header: brand and toolbar.
        headerArea = area.removeFromTop (headerHeight);
        {
            auto h = headerArea.reduced (0, 0);
            h.removeFromLeft (32);
            auto titleColumn = h.removeFromLeft (juce::jlimit (180, 280, getWidth() / 5));
            titleLabel.setBounds (titleColumn.withTrimmedTop (8).withHeight (28));
            subtitleLabel.setBounds (titleColumn.withTrimmedTop (36).withHeight (16));

            auto bar = h.reduced (margin, 0).withSizeKeepingCentre (h.getWidth() - 2 * margin, 26);
            const auto placeRight = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromRight (w)); };
            placeRight (compareCopy, 48);
            bar.removeFromRight (2);
            placeRight (compareB, 28);
            bar.removeFromRight (2);
            placeRight (compareA, 28);
            bar.removeFromRight (12);
            placeRight (redoButton, 50);
            bar.removeFromRight (2);
            placeRight (undoButton, 50);
            bar.removeFromRight (12);
            statusLabel.setBounds (bar.removeFromRight (juce::jmax (0, juce::jmin (160, bar.getWidth() - 380))));
            bar.removeFromRight (8);

            const auto presetWidth = juce::jlimit (160, 300, bar.getWidth() - 2 * 28 - 34 - 6);
            auto group = bar.withSizeKeepingCentre (presetWidth + 2 * 26 + 30 + 6, 26);
            presetPrev.setBounds (group.removeFromLeft (26));
            group.removeFromLeft (2);
            presetBox.setBounds (group.removeFromLeft (presetWidth));
            group.removeFromLeft (2);
            presetNext.setBounds (group.removeFromLeft (26));
            group.removeFromLeft (2);
            presetMenu.setBounds (group.removeFromLeft (30));
        }

        area.reduce (margin, 0);
        area.removeFromTop (gap);
        area.removeFromBottom (margin);

        // Bottom bar.
        barArea = area.removeFromBottom (barHeight);
        area.removeFromBottom (gap);
        {
            auto b = barArea.reduced (8, 3);
            const auto caption = [&b] (juce::Label& l, int w) { l.setBounds (b.removeFromLeft (w)); b.removeFromLeft (6); };
            caption (analyzerCaption, 64);
            analyzerBox.setBounds (b.removeFromLeft (juce::jlimit (110, 140, b.getWidth() / 8)));
            b.removeFromLeft (16);

            bypassButton.setBounds (b.removeFromRight (84));
            b.removeFromRight (12);
            const auto levelWidth = juce::jlimit (100, 200, b.getWidth() / 6);
            outputSlider.setBounds (b.removeFromRight (levelWidth));
            b.removeFromRight (4);
            outputCaption.setBounds (b.removeFromRight (30));
            b.removeFromRight (12);
            inputSlider.setBounds (b.removeFromRight (levelWidth));
            b.removeFromRight (4);
            inputCaption.setBounds (b.removeFromRight (22));
            b.removeFromRight (16);

            caption (predelayCaption, 64);
            const auto divisionWidth = 72, syncWidth = 66;
            const auto predelayWidth = juce::jmax (90, b.getWidth() - divisionWidth - syncWidth - 12);
            predelaySlider.setBounds (b.removeFromLeft (predelayWidth));
            b.removeFromLeft (6);
            syncButton.setBounds (b.removeFromLeft (syncWidth));
            b.removeFromLeft (6);
            divisionBox.setBounds (b.removeFromLeft (divisionWidth));
        }

        // Knob row: as tall as the window allows, within limits.
        const auto knobHeight = juce::jlimit (124, 190, area.getHeight() / 3);
        knobArea = area.removeFromTop (knobHeight);
        area.removeFromTop (gap);
        {
            auto k = knobArea.reduced (8, 6);
            const auto heroDiameter = juce::jlimit (90, 150, k.getHeight() - 16 - 34);
            const auto heroWidth = juce::jmax (heroDiameter + 60, 170);
            heroArea = k.withSizeKeepingCentre (heroWidth, k.getHeight());

            auto left = k.withRight (heroArea.getX());
            auto right = k.withLeft (heroArea.getRight());
            const auto cellHeight = 16 + knobDiameter + 8 + 20;

            const auto placeSide = [&] (juce::Rectangle<int> side, int first)
            {
                const auto cellWidth = juce::jmin (side.getWidth() / 3, knobCell + 40);
                auto row = side.withSizeKeepingCentre (cellWidth * 3, cellHeight);
                for (int i = 0; i < 3; ++i)
                {
                    auto cell = row.removeFromLeft (cellWidth);
                    knobCaptions[(size_t) (first + i)].setBounds (cell.removeFromTop (16));
                    knobs[(size_t) (first + i)].setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), knobCell), cell.getHeight()));
                    knobs[(size_t) (first + i)].getProperties().set ("maxKnobDiameter", knobDiameter + 8);
                }
            };
            placeSide (left, 0);
            placeSide (right, 4);

            auto hero = heroArea;
            knobCaptions[3].setBounds (hero.removeFromTop (16));
            auto bottom = hero.removeFromBottom (32);
            decayReadout.setBounds (bottom.removeFromTop (18));
            roomReadout.setBounds (bottom);
            knobs[3].setBounds (hero.withSizeKeepingCentre (heroDiameter + 8, juce::jmin (hero.getHeight(), heroDiameter + 8)));
            knobs[3].getProperties().set ("maxKnobDiameter", heroDiameter + 8);
        }

        // Display and output meter.
        outputMeter.setBounds (area.removeFromRight (meterWidth));
        area.removeFromRight (4);
        display.setBounds (area);
    }
}
