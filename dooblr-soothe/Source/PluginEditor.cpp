#include "PluginEditor.h"

#include "Presets.h"

namespace dooblrsoothe
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
        constexpr int toolbarHeight = 30, margin = 12, panelGap = 8, meterWidth = 16, stripWidth = 26;
        constexpr int tabWidth = 34, tabHeight = 26, tabGap = 4, listenWidth = 104, minGraphHeight = 150;

        float valueOf (juce::AudioProcessorValueTreeState& state, const juce::String& pid)
        {
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }

        void setValue (juce::AudioProcessorValueTreeState& state, const juce::String& pid, float value)
        {
            if (auto* param = state.getParameter (pid))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (value));
                param->endChangeGesture();
            }
        }
    }

    DooblrSootheEditor::DooblrSootheEditor (DooblrSootheProcessor& p)
        : juce::AudioProcessorEditor (p),
          processor (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap()),
          modeToggle (p.getState(), id::mode, { "SOFT", "HARD" }, Palette::accent),
          graph (p)
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        // Left column labels.
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
        styleLabel (listenCaption, "Band", 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);
        styleLabel (amountCaption, "AMOUNT", 13.0f, Palette::textPrimary, juce::Justification::centred, true);
        styleLabel (sharpnessCaption, "SHARPNESS", 11.0f, Palette::textMuted, juce::Justification::centred, true);

        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &latencyLabel, &listenCaption, &amountCaption, &sharpnessCaption })
            addAndMakeVisible (label);

        // Hero Amount knob (the depth parameter) and Sharpness, each with a value box below.
        const auto setupKnob = [this] (juce::Slider& knob, const juce::String& pid, juce::Colour colour)
        {
            auto* parameter = state.getParameter (pid);
            knob.setComponentID (pid);
            knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
            knob.setColour (juce::Slider::textBoxOutlineColourId, Palette::divider);
            knob.setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelRaised);
            knob.setTooltip (parameter->getName (64) + " (double-click resets, Shift-drag for fine moves)");
            sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, knob));
            knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
            knob.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            addAndMakeVisible (knob);
        };
        setupKnob (amountKnob, id::depth, Palette::accent);
        setupKnob (sharpnessKnob, id::sharpness, Palette::accent.withAlpha (0.8f));
        amountKnob.setTooltip ("Amount: how much the resonances are reduced (double-click resets, Shift-drag for fine moves)");

        // Strip of dot-sliders: stereo mode, link, focus.
        const std::array<juce::String, 3> stripIds { id::stereoMode, id::link, id::focus };
        for (size_t i = 0; i < stripSliders.size(); ++i)
        {
            auto& slider = stripSliders[i];
            auto* parameter = state.getParameter (stripIds[i]);
            slider.setComponentID (stripIds[i]);
            setupStripSlider (slider, *parameter);
            if (i == 0)
                slider.setSliderSnapsToMousePosition (true); // two-state choice: the upper half is M/S, the lower L/R
            sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, stripIds[i], slider));
            slider.onValueChange = [this] { repaint (stripArea); };
            addAndMakeVisible (slider);
        }
        stripSliders[0].setTooltip ("Stereo mode: L/R processes left and right, M/S processes mid and side");
        stripSliders[1].setTooltip ("Link: how much the two channels share one reduction (double-click resets)");
        stripSliders[2].setTooltip ("Focus: weights the reduction toward the first channel (L or M, down) or the second (R or S, up)");

        // Toolbar.
        sidechainButton.setComponentID (id::sidechain);
        deltaButton.setComponentID (id::delta);
        bypassButton.setComponentID (id::bypass);
        deltaButton.setTooltip ("Delta: hear only what is being removed");
        bypassButton.setTooltip ("Bypass: the input, delayed by the latency so it lines up");
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::sidechain, sidechainButton);
        deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::delta, deltaButton);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);
        modeToggle.setTooltip ("Soft: reduction relative to the spectrum around each peak. Hard: also pulls loud peaks down by level");

        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { processor.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { processor.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory and user presets (user presets live in " + presets::userDirectory().getFullPathName() + ")");
        presetBox.onChange = [this] { presetChosen(); };
        presetPrev.setComponentID ("presetPrev");
        presetNext.setComponentID ("presetNext");
        presetPrev.setTooltip ("Previous preset");
        presetNext.setTooltip ("Next preset");
        presetPrev.onClick = [this] { stepPreset (-1); };
        presetNext.onClick = [this] { stepPreset (1); };
        presetMenu.setComponentID ("presetMenu");
        presetMenu.setTooltip (juce::String ("Save or load a user preset file (") + presets::fileExtension + ")");
        presetMenu.onClick = [this] { showPresetMenu(); };

        compareA.setComponentID ("compareA");
        compareB.setComponentID ("compareB");
        compareCopy.setComponentID ("compareCopy");
        compareA.setTooltip ("Compare: settings A (A / B are kept while the plug-in is open, not saved with the session)");
        compareB.setTooltip ("Compare: settings B (the first time, B starts as a copy of A)");
        compareCopy.setTooltip ("Copy the current settings to the other compare slot");
        for (auto* b : { &compareA, &compareB })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        }
        compareA.onClick = [this] { processor.selectCompareSlot (0); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareB.onClick = [this] { processor.selectCompareSlot (1); updateCompareButtons(); refreshPresetBox(); refresh(); };
        compareCopy.onClick = [this] { processor.copyToOtherCompareSlot(); setStatus ("Copied to " + juce::String (processor.getCompareSlot() == 0 ? "B" : "A")); };

        advancedButton.setComponentID ("advanced");
        advancedButton.setTooltip ("Show the advanced settings (selectivity, timing, output, quality) or the band controls");
        advancedButton.setClickingTogglesState (false);
        advancedButton.setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        advancedButton.onClick = [this] { setAdvancedShown (! advancedShown); };

        for (juce::Component* c : { (juce::Component*) &modeToggle, (juce::Component*) &sidechainButton, (juce::Component*) &deltaButton,
                                    (juce::Component*) &bypassButton, (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                    (juce::Component*) &presetPrev, (juce::Component*) &presetBox, (juce::Component*) &presetNext,
                                    (juce::Component*) &presetMenu, (juce::Component*) &compareA, (juce::Component*) &compareB,
                                    (juce::Component*) &compareCopy, (juce::Component*) &advancedButton, (juce::Component*) &graph,
                                    (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        // Bottom row, Advanced view.
        advancedPanel = std::make_unique<ui::ControlPanel> (state, "ADVANCED", Palette::dynamic, std::vector<Spec> {
            { id::selectivity, "Selectivity" }, { id::attack, "Attack" }, { id::release, "Release" }, { id::maxCut, "Max cut" },
            { id::wetTrim, "Wet trim" }, { id::mix, "Mix" }, { id::output, "Output" }, { id::quality, "Quality", Kind::combo, 112 } });
        if (auto* quality = dynamic_cast<juce::ComboBox*> (advancedPanel->findControl (id::quality)))
            quality->setTooltip ("FFT resolution: higher quality resolves lows better, with more latency and CPU");
        addChildComponent (*advancedPanel);

        // Bottom row, band view.
        for (int b = 0; b < params::numBands; ++b)
        {
            bandPanels[(size_t) b] = std::make_unique<ui::ControlPanel> (state, "BAND " + juce::String (b + 1), ui::bandColour (b), std::vector<Spec> {
                { params::bandActive (b), "Use", Kind::toggle, 80 }, { params::bandShape (b), "Shape", Kind::combo, 112 },
                { params::bandFreq (b), "Freq" }, { params::bandGain (b), "Gain" }, { params::bandQ (b), "Q" } });
            addChildComponent (*bandPanels[(size_t) b]);

            auto& tab = bandTabs[(size_t) b];
            tab.setButtonText (juce::String (b + 1));
            tab.setComponentID ("band_" + juce::String (b + 1));
            tab.setRadioGroupId (3001);
            tab.setClickingTogglesState (true);
            tab.setColour (juce::TextButton::buttonOnColourId, ui::bandColour (b).withAlpha (0.45f));
            tab.onClick = [this, b] { if (bandTabs[(size_t) b].getToggleState()) selectBand (b); };
            addAndMakeVisible (tab);
        }

        listenButton.setComponentID (id::listenBand);
        listenButton.setClickingTogglesState (false);
        listenButton.setTooltip ("Listen: solo what the selected band's region removes");
        listenButton.onClick = [this]
        {
            const auto listening = juce::roundToInt (valueOf (state, id::listenBand)) == selectedBand + 1;
            setValue (state, id::listenBand, listening ? 0.0f : (float) (selectedBand + 1));
            refresh();
        };
        addAndMakeVisible (listenButton);

        graph.onBandSelected = [this] (int band) { selectBand (band); };

        processor.presetChanged.addChangeListener (this);
        refreshPresetBox();
        selectBand (1);
        updateCompareButtons();
        refresh();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
    }

    void DooblrSootheEditor::setupStripSlider (juce::Slider& slider, juce::RangedAudioParameter& parameter)
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.getProperties().set ("dotSlider", true);
        slider.setColour (juce::Slider::trackColourId, Palette::spectrum);
        slider.setSliderSnapsToMousePosition (false);
        slider.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
        slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    }

    void DooblrSootheEditor::setAdvancedShown (bool shouldShow)
    {
        advancedShown = shouldShow;
        advancedButton.setToggleState (advancedShown, juce::dontSendNotification);
        advancedPanel->setVisible (advancedShown);
        for (int b = 0; b < params::numBands; ++b)
        {
            bandPanels[(size_t) b]->setVisible (! advancedShown && b == selectedBand);
            bandTabs[(size_t) b].setVisible (! advancedShown);
        }
        listenButton.setVisible (! advancedShown);
        listenCaption.setVisible (! advancedShown);
        repaint();
    }

    void DooblrSootheEditor::updateCompareButtons()
    {
        const auto slot = processor.getCompareSlot();
        compareA.setToggleState (slot == 0, juce::dontSendNotification);
        compareB.setToggleState (slot == 1, juce::dontSendNotification);
        compareCopy.setButtonText (juce::String (juce::CharPointer_UTF8 (slot == 0 ? "A\xe2\x86\x92" "B" : "B\xe2\x86\x92" "A")));
    }

    void DooblrSootheEditor::stepPreset (int delta)
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

    void DooblrSootheEditor::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save preset...");
        menu.addItem (2, "Load preset...");
        juce::Component::SafePointer<DooblrSootheEditor> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenu),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;
                                if (result == 1) safeThis->savePreset();
                                if (result == 2) safeThis->loadPreset();
                            });
    }

    DooblrSootheEditor::~DooblrSootheEditor()
    {
        stopTimer();
        processor.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    //==============================================================================
    ui::ControlPanel* DooblrSootheEditor::getPanelFor (const juce::String& pid) const
    {
        for (auto* panel : getPanels())
            if (panel->findControl (pid) != nullptr)
                return panel;
        return nullptr;
    }

    std::vector<ui::ControlPanel*> DooblrSootheEditor::getPanels() const
    {
        std::vector<ui::ControlPanel*> list { advancedPanel.get() };
        for (auto& panel : bandPanels)
            list.push_back (panel.get());
        return list;
    }

    juce::Component* DooblrSootheEditor::findControl (const juce::String& pid) const
    {
        if (auto* panel = getPanelFor (pid))
            return panel->findControl (pid);

        for (auto* c : { (juce::Component*) &amountKnob, (juce::Component*) &sharpnessKnob, (juce::Component*) &stripSliders[0],
                         (juce::Component*) &stripSliders[1], (juce::Component*) &stripSliders[2], (juce::Component*) &modeToggle,
                         (juce::Component*) &sidechainButton, (juce::Component*) &deltaButton, (juce::Component*) &bypassButton,
                         (juce::Component*) &listenButton })
            if (c->getComponentID() == pid)
                return const_cast<juce::Component*> (c);
        return nullptr;
    }

    void DooblrSootheEditor::selectBand (int band)
    {
        selectedBand = juce::jlimit (0, params::numBands - 1, band);
        for (int b = 0; b < params::numBands; ++b)
        {
            bandPanels[(size_t) b]->setVisible (! advancedShown && b == selectedBand);
            bandTabs[(size_t) b].setToggleState (b == selectedBand, juce::dontSendNotification);
        }
        graph.setSelectedBand (selectedBand);
        refresh();
    }

    void DooblrSootheEditor::refresh()
    {
        updateUndoButtons();

        const auto listen = juce::roundToInt (valueOf (state, id::listenBand)) - 1;
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto active = valueOf (state, params::bandActive (b)) > 0.5f;
            auto& tab = bandTabs[(size_t) b];
            const auto text = juce::String (b + 1) + (b == listen ? "*" : "");
            if (tab.getButtonText() != text)
                tab.setButtonText (text);
            tab.setColour (juce::TextButton::textColourOffId, active ? ui::bandColour (b) : Palette::textMuted);
            tab.setColour (juce::TextButton::textColourOnId, Palette::textPrimary);

            const auto shape = (params::Shape) juce::roundToInt (valueOf (state, params::bandShape (b)));
            auto& panel = *bandPanels[(size_t) b];
            if (auto* gain = panel.findControl (params::bandGain (b)))
                ui::setDimmed (*gain, ! active || ! params::shapeUsesGain (shape)); // an unused band dims all of its controls
            for (auto* pid : { "Shape", "Freq", "Gain", "Q" })
                if (auto* c = panel.findControl (params::bandId (b, pid)); c != nullptr && juce::String (pid) != "Gain")
                    ui::setDimmed (*c, ! active);
        }

        listenButton.setToggleState (listen == selectedBand, juce::dontSendNotification);
        listenCaption.setText (listen >= 0 && listen != selectedBand ? "Listening: band " + juce::String (listen + 1) : "Band",
                               juce::dontSendNotification);
        ui::setDimmed (listenButton, valueOf (state, params::bandActive (selectedBand)) < 0.5f);

        const auto connected = processor.isSidechainConnected();
        ui::setDimmed (sidechainButton, ! connected);
        sidechainButton.setTooltip (connected ? "Sidechain: detect resonances on the sidechain input, process the main input"
                                              : "Sidechain: no sidechain input is routed in the host, so detection uses the main input");
        ui::setDimmed (stripSliders[1], processor.getMainBusNumOutputChannels() < 2);
        ui::setDimmed (stripSliders[2], processor.getMainBusNumOutputChannels() < 2);
        updateCompareButtons();

        const auto latency = processor.getLatencySamples();
        const auto sr = std::max (1.0, processor.getEngine().getSampleRate());
        const auto latencyText = "Latency " + juce::String (latency) + " smp / " + params::formatValue (latency * 1000.0 / sr, 1) + " ms";
        if (latencyLabel.getText() != latencyText)
            latencyLabel.setText (latencyText, juce::dontSendNotification);

        if (processor.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrSootheEditor::setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }

    //==============================================================================
    void DooblrSootheEditor::refreshPresetBox()
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

        shownPresetName = processor.getPresetName();
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

    void DooblrSootheEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;

        const auto itemId = presetBox.getSelectedId();
        if (itemId >= userIdBase && itemId - userIdBase < userPresetFiles.size())
            setStatus (processor.loadUserPreset (userPresetFiles[itemId - userIdBase]));
        else if (itemId >= factoryIdBase && itemId < userIdBase)
            processor.setCurrentProgram (itemId - factoryIdBase);

        refreshPresetBox();
        refresh();
    }

    void DooblrSootheEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (processor.getPresetName());
        if (name.isEmpty())
            name = "Preset";

        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-soothe preset", folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrSootheEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      const auto file = chooser.getResult().withFileExtension (presets::fileExtension);
                                      safeThis->setStatus (safeThis->processor.saveUserPreset (file));
                                      safeThis->refreshPresetBox();
                                  });
    }

    void DooblrSootheEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-soothe preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrSootheEditor> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                          return;
                                      safeThis->setStatus (safeThis->processor.loadUserPreset (chooser.getResult()));
                                      safeThis->refreshPresetBox();
                                      safeThis->refresh();
                                  });
    }

    void DooblrSootheEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrSootheEditor::updateUndoButtons()
    {
        auto& history = processor.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrSootheEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto command = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;
        auto& history = processor.getUndoHistory();

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
    void DooblrSootheEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        // Left column panel with an accent rule under the title.
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (columnArea.toFloat(), 6.0f);
        g.setColour (Palette::accent);
        g.fillRect (columnArea.getX() + 12, titleLabel.getBottom() + 2, 28, 2);
        g.setColour (Palette::divider);
        g.fillRect (columnArea.getX() + 12, modeToggle.getBottom() + 8, columnArea.getWidth() - 24, 1);

        // Strip: a panel behind the dot-sliders, captions (name and value) rotated below each.
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (stripArea.toFloat(), 4.0f);
        g.setFont (juce::FontOptions (10.5f));
        for (size_t i = 0; i < stripSliders.size(); ++i)
        {
            auto& slider = stripSliders[i];
            const auto captionTop = slider.getBottom() + 4;
            const auto captionBottom = i + 1 < stripSliders.size() ? stripSliders[i + 1].getY() - 8 : stripArea.getBottom() - 4;
            if (captionBottom - captionTop < 20)
                continue;
            const auto centre = juce::Point<float> ((float) stripArea.getCentreX(), (captionTop + captionBottom) * 0.5f);
            const auto length = (float) (captionBottom - captionTop);
            const auto text = stripNames[i] + "  " + slider.getTextFromValue (slider.getValue());
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, centre.x, centre.y));
            g.setColour (ui::isDimmed (slider) ? Palette::textMuted.withAlpha (0.6f) : Palette::textMuted);
            g.drawFittedText (text, juce::Rectangle<float> (length, 14.0f).withCentre (centre).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
        }

        g.setColour (Palette::divider);
        g.fillRect (toolbarArea.getX(), toolbarArea.getBottom() + 3, toolbarArea.getWidth(), 1);

        if (! advancedShown)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (tabsArea.toFloat(), 4.0f);
        }
    }

    void DooblrSootheEditor::resized()
    {
        auto content = getLocalBounds().reduced (margin);

        // Left column (~20 % of the width).
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
            col.removeFromTop (18);

            statusLabel.setBounds (col.removeFromBottom (16));
            latencyLabel.setBounds (col.removeFromBottom (16));
            col.removeFromBottom (8);

            // Space for both knobs (captions and value boxes included), the hero one larger.
            constexpr int captionHeight = 18, heroBox = 24, smallBox = 20, knobGap = 14;
            const auto spare = col.getHeight() - 2 * captionHeight - heroBox - smallBox - knobGap;
            const auto hero = juce::jlimit (96, 300, juce::jmin (col.getWidth(), spare * 2 / 3));
            const auto small = juce::jlimit (52, 120, juce::jmin (col.getWidth() / 2, spare - hero));

            auto knobs = col.withSizeKeepingCentre (col.getWidth(), 2 * captionHeight + heroBox + smallBox + knobGap + hero + small);
            amountCaption.setBounds (knobs.removeFromTop (captionHeight));
            amountKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jlimit (72, 120, hero / 2), heroBox - 4);
            amountKnob.getProperties().set ("maxKnobDiameter", (double) hero);
            amountKnob.setBounds (knobs.removeFromTop (hero + heroBox).withSizeKeepingCentre (juce::jmax (hero, 120), hero + heroBox));
            knobs.removeFromTop (knobGap);
            sharpnessCaption.setBounds (knobs.removeFromTop (captionHeight));
            sharpnessKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jlimit (64, 96, small), smallBox - 4);
            sharpnessKnob.getProperties().set ("maxKnobDiameter", (double) small);
            sharpnessKnob.setBounds (knobs.removeFromTop (small + smallBox).withSizeKeepingCentre (juce::jmax (small, 96), small + smallBox));
        }

        // Bottom row: band view (tabs, band panel, Listen) or the Advanced panel.
        const auto tabsWidth = 4 * tabWidth + 3 * tabGap + 16;
        const auto bottomWidth = content.getWidth();
        const auto bandWidth = bottomWidth - tabsWidth - listenWidth - 2 * panelGap;
        int bottomHeight = juce::jmax (ui::ControlPanel::titleHeight + 2 * tabHeight + tabGap + 16, advancedPanel->getPreferredHeight (bottomWidth));
        for (auto& panel : bandPanels)
            bottomHeight = juce::jmax (bottomHeight, panel->getPreferredHeight (bandWidth));
        auto bottom = content.removeFromBottom (bottomHeight);
        content.removeFromBottom (panelGap);

        advancedPanel->setBounds (bottom);
        {
            auto row = bottom;
            tabsArea = row.removeFromLeft (tabsWidth);
            auto tabs = tabsArea.reduced (8, 0).withTrimmedTop (ui::ControlPanel::titleHeight);
            tabs = tabs.withSizeKeepingCentre (tabs.getWidth(), 2 * tabHeight + tabGap);
            for (int b = 0; b < params::numBands; ++b)
                bandTabs[(size_t) b].setBounds (tabs.getX() + (b % 4) * (tabWidth + tabGap), tabs.getY() + (b / 4) * (tabHeight + tabGap), tabWidth, tabHeight);
            row.removeFromLeft (panelGap);
            auto listenArea = row.removeFromRight (listenWidth);
            row.removeFromRight (panelGap);
            for (auto& panel : bandPanels)
                panel->setBounds (row);
            listenCaption.setBounds (listenArea.removeFromTop (ui::ControlPanel::titleHeight));
            listenButton.setBounds (listenArea.withSizeKeepingCentre (listenWidth, 26));
        }

        // Toolbar across the strip and graph.
        toolbarArea = content.removeFromTop (toolbarHeight);
        {
            auto bar = toolbarArea;
            const auto place = [&bar] (juce::Component& c, int w, int h = 26) { c.setBounds (bar.removeFromLeft (w).withSizeKeepingCentre (w, h)); };
            const auto placeRight = [&bar] (juce::Component& c, int w, int h = 26) { c.setBounds (bar.removeFromRight (w).withSizeKeepingCentre (w, h)); };
            place (sidechainButton, 58);
            bar.removeFromLeft (4);
            place (deltaButton, 72);
            bar.removeFromLeft (12);

            placeRight (advancedButton, 80);
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
            placeRight (presetMenu, 30);
            bar.removeFromRight (2);
            placeRight (presetNext, 26);
            bar.removeFromRight (2);
            const auto presetWidth = juce::jmin (bar.getWidth(), 320);
            presetBox.setBounds (bar.removeFromLeft (presetWidth).withSizeKeepingCentre (presetWidth, 26));
            presetNext.setBounds (presetNext.getBounds().withX (presetBox.getRight() + 2));
            presetMenu.setBounds (presetMenu.getBounds().withX (presetNext.getRight() + 2));
        }
        content.removeFromTop (panelGap);

        // Strip of dot-sliders, then the graph and the output meter.
        stripArea = content.removeFromLeft (stripWidth);
        content.removeFromLeft (6);
        {
            const auto sectionHeight = stripArea.getHeight() / (int) stripSliders.size();
            for (size_t i = 0; i < stripSliders.size(); ++i)
            {
                auto section = juce::Rectangle<int> (stripArea.getX(), stripArea.getY() + (int) i * sectionHeight, stripArea.getWidth(), sectionHeight);
                const auto sliderHeight = juce::jmax (40, juce::jmin (section.getHeight() / 2, 160));
                stripSliders[i].setBounds (section.withTrimmedTop (6).withHeight (sliderHeight));
            }
        }
        outputMeter.setBounds (content.removeFromRight (meterWidth));
        content.removeFromRight (4);
        graph.setBounds (content.withHeight (juce::jmax (minGraphHeight, content.getHeight())));
    }
}
