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
        constexpr int headerHeight = 60, toolbarHeight = 34, margin = 12, panelGap = 8, meterWidth = 34;
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

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredRight);
        statusLabel.setComponentID ("status");
        styleLabel (latencyLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredRight);
        latencyLabel.setComponentID ("latency");
        styleLabel (qualityCaption, "Quality", 12.0f, Palette::textMuted, juce::Justification::centredRight);
        styleLabel (listenCaption, "Band", 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);

        for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption, &statusLabel, &latencyLabel, &qualityCaption, &listenCaption })
            addAndMakeVisible (label);

        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { processor.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { processor.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory and user presets (user presets live in " + presets::userDirectory().getFullPathName() + ")");
        presetBox.onChange = [this] { presetChosen(); };

        saveButton.setComponentID ("savePreset");
        saveButton.setTooltip (juce::String ("Save the current settings as a user preset file (") + presets::fileExtension + ")");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip (juce::String ("Load a user preset file (") + presets::fileExtension + ")");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, 0.0);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &outputSlider,
                                    (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        // Toolbar.
        modeToggle.setTooltip ("Soft: reduction relative to the spectrum around each peak. Hard: also pulls loud peaks down by level");
        qualityBox.setComponentID (id::quality);
        qualityBox.addItemList (params::qualityNames(), 1);
        qualityBox.setTooltip ("FFT resolution: higher quality resolves lows better, with more latency and CPU");
        qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id::quality, qualityBox);

        sidechainButton.setComponentID (id::sidechain);
        deltaButton.setComponentID (id::delta);
        bypassButton.setComponentID (id::bypass);
        deltaButton.setTooltip ("Delta: hear only what is being removed");
        bypassButton.setTooltip ("Bypass: the input, delayed by the latency so it lines up");
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::sidechain, sidechainButton);
        deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::delta, deltaButton);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);

        for (juce::Component* c : { (juce::Component*) &modeToggle, (juce::Component*) &qualityBox, (juce::Component*) &sidechainButton,
                                    (juce::Component*) &deltaButton, (juce::Component*) &bypassButton, (juce::Component*) &graph })
            addAndMakeVisible (c);

        // Panels.
        detectionPanel = std::make_unique<ui::ControlPanel> (state, "DETECTION", Palette::accent, std::vector<Spec> {
            { id::depth, "Depth" }, { id::sharpness, "Sharpness" }, { id::selectivity, "Selectivity" },
            { id::attack, "Attack" }, { id::release, "Release" } });
        stereoPanel = std::make_unique<ui::ControlPanel> (state, "STEREO", Palette::spectrum, std::vector<Spec> {
            { id::stereoMode, "Mode", Kind::combo, 96 }, { id::link, "Link" }, { id::focus, "Focus" } });
        outputPanel = std::make_unique<ui::ControlPanel> (state, "OUTPUT", Palette::dynamic, std::vector<Spec> {
            { id::maxCut, "Max cut" }, { id::wetTrim, "Wet trim" }, { id::mix, "Mix" } });

        for (auto* panel : { detectionPanel.get(), stereoPanel.get(), outputPanel.get() })
            addAndMakeVisible (*panel);

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
        refresh();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
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
        std::vector<ui::ControlPanel*> list { detectionPanel.get(), stereoPanel.get(), outputPanel.get() };
        for (auto& panel : bandPanels)
            list.push_back (panel.get());
        return list;
    }

    juce::Component* DooblrSootheEditor::findControl (const juce::String& pid) const
    {
        if (auto* panel = getPanelFor (pid))
            return panel->findControl (pid);

        for (auto* c : { (juce::Component*) &outputSlider, (juce::Component*) &modeToggle, (juce::Component*) &qualityBox,
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
            bandPanels[(size_t) b]->setVisible (b == selectedBand);
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
        ui::setDimmed (*stereoPanel->findControl (id::link), processor.getMainBusNumOutputChannels() < 2);

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

        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        g.setColour (Palette::divider);
        g.fillRect (toolbarArea.getX(), toolbarArea.getBottom() + 3, toolbarArea.getWidth(), 1);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (tabsArea.toFloat(), 4.0f);
    }

    void DooblrSootheEditor::resized()
    {
        auto bounds = getLocalBounds();

        headerArea = bounds.removeFromTop (headerHeight);
        {
            auto header = headerArea.reduced (32, 10);
            auto titleColumn = header.removeFromLeft (juce::jlimit (150, 220, header.getWidth() / 6));
            titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
            subtitleLabel.setBounds (titleColumn);

            header.removeFromLeft (8);
            undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (4);
            redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (12);

            outputMeter.setBounds (header.removeFromRight (meterWidth));
            header.removeFromRight (10);
            const auto sliderWidth = juce::jlimit (170, 260, header.getWidth() / 4);
            outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
            outputCaption.setBounds (header.removeFromRight (52));
            header.removeFromRight (16);

            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            const auto presetWidth = juce::jmin (header.getWidth(), 280);
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        auto content = bounds.reduced (margin);

        toolbarArea = content.removeFromTop (toolbarHeight);
        {
            auto bar = toolbarArea;
            modeToggle.setBounds (bar.removeFromLeft (236).withSizeKeepingCentre (236, 32));
            bar.removeFromLeft (16);
            qualityCaption.setBounds (bar.removeFromLeft (52));
            bar.removeFromLeft (6);
            qualityBox.setBounds (bar.removeFromLeft (112).withSizeKeepingCentre (112, 26));
            bar.removeFromLeft (16);
            for (auto* t : { &sidechainButton, &deltaButton, &bypassButton })
            {
                t->setBounds (bar.removeFromLeft (104).withSizeKeepingCentre (104, 26));
                bar.removeFromLeft (4);
            }
            latencyLabel.setBounds (bar.removeFromRight (juce::jmin (bar.getWidth(), 200)));
            statusLabel.setBounds (bar.reduced (4, 0));
            statusLabel.setVisible (statusLabel.getWidth() >= 120);
        }
        content.removeFromTop (panelGap);

        // Row 1: Detection | Stereo | Output, shared by their natural widths (5 : 3.5 : 3).
        const auto rowWidth = content.getWidth() - 2 * panelGap;
        const int w1 = rowWidth * 10 / 23, w2 = rowWidth * 7 / 23, w3 = rowWidth - w1 - w2;
        const auto row1Height = juce::jmax (detectionPanel->getPreferredHeight (w1), stereoPanel->getPreferredHeight (w2),
                                            outputPanel->getPreferredHeight (w3));

        // Row 2: band tabs, the selected band's panel, Listen.
        const auto tabsWidth = 4 * tabWidth + 3 * tabGap + 16;
        const auto bandWidth = content.getWidth() - tabsWidth - listenWidth - 2 * panelGap;
        int row2Height = ui::ControlPanel::titleHeight + 2 * tabHeight + tabGap + 16;
        for (auto& panel : bandPanels)
            row2Height = juce::jmax (row2Height, panel->getPreferredHeight (bandWidth));

        const auto graphHeight = juce::jmax (minGraphHeight, content.getHeight() - row1Height - row2Height - 2 * panelGap);
        graph.setBounds (content.removeFromTop (graphHeight));

        content.removeFromTop (panelGap);
        auto row1 = content.removeFromTop (row1Height);
        detectionPanel->setBounds (row1.removeFromLeft (w1));
        row1.removeFromLeft (panelGap);
        stereoPanel->setBounds (row1.removeFromLeft (w2));
        row1.removeFromLeft (panelGap);
        outputPanel->setBounds (row1);

        content.removeFromTop (panelGap);
        bandRowArea = content.removeFromTop (row2Height);
        auto row2 = bandRowArea;
        tabsArea = row2.removeFromLeft (tabsWidth);
        {
            auto tabs = tabsArea.reduced (8, 0);
            tabs.removeFromTop (ui::ControlPanel::titleHeight);
            for (int r = 0; r < 2; ++r)
            {
                auto line = tabs.removeFromTop (tabHeight);
                for (int c = 0; c < 4; ++c)
                {
                    bandTabs[(size_t) (r * 4 + c)].setBounds (line.removeFromLeft (tabWidth));
                    line.removeFromLeft (tabGap);
                }
                tabs.removeFromTop (tabGap);
            }
        }
        row2.removeFromLeft (panelGap);
        auto listenArea = row2.removeFromRight (listenWidth);
        row2.removeFromRight (panelGap);
        for (auto& panel : bandPanels)
            panel->setBounds (row2);

        listenCaption.setBounds (listenArea.removeFromTop (ui::ControlPanel::titleHeight));
        listenButton.setBounds (listenArea.removeFromTop (30).withSizeKeepingCentre (listenWidth, 26));
    }
}
