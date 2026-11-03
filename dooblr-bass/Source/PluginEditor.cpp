#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"
#include "UI/Theme.h"

namespace dooblrbass
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
        constexpr int headerHeight = 60, modeBarHeight = 36, margin = 12, panelGap = 6;
        constexpr int meterWidth = 34, minGraphHeight = 56, maxGraphHeight = 120;

        float valueOf (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }

        juce::StringArray dimmedIds (juce::AudioProcessorValueTreeState& state)
        {
            juce::StringArray ids;
            if (valueOf (state, id::osc1Wave) < 0.5f)
                ids.add (id::osc1PW);
            if (valueOf (state, id::osc2Wave) < 0.5f)
                ids.add (id::osc2PW);
            if (valueOf (state, id::lfoSync) >= 0.5f)
                ids.add (id::lfoRate);
            else
                ids.add (id::lfoDivision);
            if (valueOf (state, id::sharedDecay) >= 0.5f)
            {
                ids.add (id::fenvRelease);
                ids.add (id::aenvRelease);
            }
            return ids;
        }

        ui::ControlPanel* panelNamed (std::vector<std::unique_ptr<ui::ControlPanel>>& panels, const juce::String& title)
        {
            for (auto& panel : panels)
                if (panel->getTitle() == title)
                    return panel.get();
            return nullptr;
        }
    }

    DooblrBassEditor::DooblrBassEditor (DooblrBassProcessor& p)
        : juce::AudioProcessorEditor (p),
          synth (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");

        for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption, &statusLabel })
            addAndMakeVisible (label);

        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { synth.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { synth.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory and user presets (user presets live in " + presets::userDirectory().getFullPathName() + ")");
        presetBox.onChange = [this] { presetChosen(); };

        randomButton.setComponentID ("random");
        randomButton.setColour (juce::TextButton::buttonColourId, Palette::accent.withAlpha (0.22f));
        randomButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
        randomButton.onClick = [this] { randomise(); };

        saveButton.setComponentID ("savePreset");
        saveButton.setTooltip ("Save the current patch as a user preset file (.dbass)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset file (.dbass)");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, 0.0);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &randomButton, (juce::Component*) &saveButton, (juce::Component*) &loadButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        buildBoard();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateEnablement();
        updateStatus();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
    }

    DooblrBassEditor::~DooblrBassEditor()
    {
        stopTimer();
        synth.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    ui::ControlPanel& DooblrBassEditor::addPanel (const juce::String& title, juce::Colour colour, const std::vector<Spec>& specs)
    {
        panels.push_back (std::make_unique<ui::ControlPanel> (state, title, colour, specs));
        addAndMakeVisible (*panels.back());
        return *panels.back();
    }

    void DooblrBassEditor::addModeToggle (const juce::String& caption, const juce::String& parameterId,
                                         const juce::StringArray& labels, juce::Colour colour)
    {
        modeToggles.push_back (std::make_unique<ui::SegmentedToggle> (state, parameterId, labels, colour));
        modeToggleCaptions.add (caption);

        const juce::Font font (juce::FontOptions (13.0f, juce::Font::bold).withKerningFactor (0.06f));
        float widest = 0.0f;
        for (const auto& label : labels)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, label));
        modeToggleWidths.push_back (juce::jmax ((int) ui::SegmentedToggle::minWidth, labels.size() * (juce::roundToInt (widest) + 30)));
        addAndMakeVisible (*modeToggles.back());
    }

    void DooblrBassEditor::addSwitch (const juce::String& parameterId, const juce::String& label)
    {
        auto button = std::make_unique<juce::ToggleButton> (label);
        button->setComponentID (parameterId);
        button->setTooltip (state.getParameter (parameterId) != nullptr ? state.getParameter (parameterId)->getName (64) : label);
        switchAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *button));
        addAndMakeVisible (*button);
        switchButtons.push_back (std::move (button));
    }

    void DooblrBassEditor::buildBoard()
    {
        const auto source = [&p = synth] { return p.readSettings(); };

        oscGraph = std::make_unique<ui::OscillatorDisplay> (source);
        filterGraph = std::make_unique<ui::FilterDisplay> (source, state.getParameter (id::lpfCutoff),
                                                           state.getParameter (id::lpfReso));
        envGraph = std::make_unique<ui::EnvelopeDisplay> (source);
        lfoGraph = std::make_unique<ui::LfoDisplay> (source, synth.getEngine().getTap());

        oscGraph->setComponentID ("graph_osc");
        filterGraph->setComponentID ("graph_filter");
        envGraph->setComponentID ("graph_env");
        lfoGraph->setComponentID ("graph_lfo");

        for (auto* g : { (juce::Component*) oscGraph.get(), (juce::Component*) filterGraph.get(),
                         (juce::Component*) envGraph.get(), (juce::Component*) lfoGraph.get() })
            addAndMakeVisible (g);

        addModeToggle ("Priority", id::notePriority, { "Last", "Low" }, Palette::spectrum);
        addModeToggle ("Mode", id::sharedDecay, { "ADSR", "Shared Decay" }, Palette::master);
        addSwitch (id::legato, "Legato");
        addSwitch (id::osc3LoFreq, "Lo Freq");
        addSwitch (id::osc3KeyTrack, "Key Track");

        addPanel ("Osc 1", Palette::accent, {
            { id::osc1Wave, "Wave", Kind::combo, 88 },
            { id::osc1Octave, "Octave", Kind::combo, 72 },
            { id::osc1PW, "Width" } });
        addPanel ("Osc 2", Palette::accent, {
            { id::osc2Wave, "Wave", Kind::combo, 88 },
            { id::osc2Octave, "Octave", Kind::combo, 72 },
            { id::osc2Tune, "Tune" },
            { id::osc2PW, "Width" } });
        addPanel ("Osc 3", Palette::accent, {
            { id::osc3Wave, "Wave", Kind::combo, 88 },
            { id::osc3Octave, "Octave", Kind::combo, 72 },
            { id::osc3Tune, "Tune" } });
        addPanel ("Mixer", Palette::spectrum, {
            { id::mixOsc1, "Osc 1" },
            { id::mixOsc2, "Osc 2" },
            { id::mixOsc3, "Osc 3" },
            { id::mixSub, "Sub" },
            { id::subOctave, "Sub Oct", Kind::combo, 80 },
            { id::mixNoise, "Noise" },
            { id::mixDrive, "Drive" },
            { id::mixOverload, "Overload" } });
        addPanel ("Low-pass", Palette::dynamic, {
            { id::lpfCutoff, "Cutoff" },
            { id::lpfReso, "Resonance" },
            { id::lpfEnvAmt, "Env Amt" },
            { id::lpfKeyTrack, "Key Track" } });
        addPanel ("Controllers", Palette::spectrum, {
            { id::glide, "Glide" },
            { id::bendRange, "Bend" } });
        addPanel ("Filter Env", Palette::dynamic, {
            { id::fenvAttack, "Attack" },
            { id::fenvDecay, "Decay" },
            { id::fenvSustain, "Sustain" },
            { id::fenvRelease, "Release" },
            { id::fenvVelAmt, "Velocity" } });
        addPanel ("Amp Env", Palette::master, {
            { id::aenvAttack, "Attack" },
            { id::aenvDecay, "Decay" },
            { id::aenvSustain, "Sustain" },
            { id::aenvRelease, "Release" } });
        addPanel ("LFO", Palette::spectrum, {
            { id::lfoWave, "Wave", Kind::combo, 88 },
            { id::lfoRate, "Rate" },
            { id::lfoSync, "Sync", Kind::toggle },
            { id::lfoDivision, "Division", Kind::combo, 72 },
            { id::lfoToPitch, "Pitch" },
            { id::lfoToFilter, "Filter" },
            { id::lfoWheel, "Wheel" } });
    }

    void DooblrBassEditor::updateEnablement()
    {
        const auto dimmed = dimmedIds (state);

        for (auto& panel : panels)
            for (auto* r : synth.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r))
                    if (auto* control = panel->findControl (ranged->paramID))
                        ui::setDimmed (*control, dimmed.contains (ranged->paramID));
    }

    void DooblrBassEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 4000;
        updateStatus();
    }

    void DooblrBassEditor::updateStatus()
    {
        juce::String text;

        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() < transientStatusUntil)
        {
            text = transientStatus;
        }
        else
        {
            transientStatus.clear();
            const auto& tap = synth.getEngine().getTap();
            const auto note = tap.note.load();
            if (note >= 0)
                text = juce::MidiMessage::getMidiNoteName (note, true, true, 4) + "   "
                       + juce::String (juce::roundToInt (tap.cutoffHz.load())) + " Hz";
            else
                text = "Mono bass";
        }

        if (statusLabel.getText() != text)
            statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrBassEditor::refreshPresetBox()
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

        shownPresetName = synth.getPresetName();
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

    void DooblrBassEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;

        const auto itemId = presetBox.getSelectedId();

        if (itemId >= userIdBase && itemId - userIdBase < userPresetFiles.size())
            setStatus (synth.loadUserPreset (userPresetFiles[itemId - userIdBase]));
        else if (itemId >= factoryIdBase && itemId < userIdBase)
            synth.setCurrentProgram (itemId - factoryIdBase);

        refreshPresetBox();
        updateEnablement();
        updateUndoButtons();
    }

    void DooblrBassEditor::randomise()
    {
        juce::Random random;
        synth.randomisePatch (random);
        refreshPresetBox();
        updateEnablement();
        updateUndoButtons();
    }

    void DooblrBassEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (synth.getPresetName());
        if (name.isEmpty())
            name = "Patch";

        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-bass preset",
                                                           folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrBassEditor> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || chooser.getResult() == juce::File())
                                          return;

                                      const auto file = chooser.getResult().withFileExtension (presets::fileExtension);
                                      safeThis->setStatus (safeThis->synth.saveUserPreset (file));
                                      safeThis->refreshPresetBox();
                                  });
    }

    void DooblrBassEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-bass preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrBassEditor> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                          return;

                                      safeThis->setStatus (safeThis->synth.loadUserPreset (chooser.getResult()));
                                      safeThis->refreshPresetBox();
                                      safeThis->updateEnablement();
                                  });
    }

    void DooblrBassEditor::timerCallback()
    {
        updateEnablement();
        updateUndoButtons();
        updateStatus();

        if (synth.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrBassEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrBassEditor::updateUndoButtons()
    {
        auto& history = synth.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrBassEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto command = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;
        auto& history = synth.getUndoHistory();

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

    void DooblrBassEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.fillRect (modeBarArea);
        g.fillRect (graphStripArea);

        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.fillRect (modeBarArea.getX(), modeBarArea.getBottom() - 1, modeBarArea.getWidth(), 1);
        if (! graphStripArea.isEmpty())
            g.fillRect (graphStripArea.getX(), graphStripArea.getBottom() - 1, graphStripArea.getWidth(), 1);

        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (Palette::textMuted);

        for (size_t i = 0; i < toggleCaptionAreas.size() && (int) i < modeToggleCaptions.size(); ++i)
            g.drawText (modeToggleCaptions[(int) i], toggleCaptionAreas[i], juce::Justification::centredRight);
    }

    void DooblrBassEditor::resized()
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
            const auto sliderWidth = juce::jlimit (140, 260, header.getWidth() / 4);
            outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
            outputCaption.setBounds (header.removeFromRight (52));
            header.removeFromRight (16);

            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            randomButton.setBounds (header.removeFromRight (66).withSizeKeepingCentre (66, 24));
            header.removeFromRight (4);
            const auto presetWidth = juce::jmin (header.getWidth(), 280);
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        modeBarArea = bounds.removeFromTop (modeBarHeight);
        contentArea = bounds.reduced (margin);
        layoutBoard();
    }

    void DooblrBassEditor::layoutBoard()
    {
        auto area = contentArea;

        toggleCaptionAreas.clear();
        {
            auto bar = modeBarArea.reduced (margin, 2);

            for (auto& button : switchButtons)
            {
                const auto textWidth = juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.0f), button->getButtonText());
                const auto width = juce::jlimit (72, 110, (int) std::ceil (textWidth) + 48);
                button->setBounds (bar.removeFromRight (width).withSizeKeepingCentre (width, juce::jmin (bar.getHeight(), 26)));
                bar.removeFromRight (6);
            }

            for (int i = (int) modeToggles.size() - 1; i >= 0; --i)
            {
                auto& t = *modeToggles[(size_t) i];
                const auto width = modeToggleWidths[(size_t) i];
                t.setBounds (bar.removeFromRight (width).withSizeKeepingCentre (width, juce::jmin (bar.getHeight(), 32)));
                toggleCaptionAreas.insert (toggleCaptionAreas.begin(), bar.removeFromRight (64).withTrimmedRight (6));
                bar.removeFromRight (8);
            }

            statusLabel.setBounds (bar.reduced (4, 0));
            statusLabel.setVisible (statusLabel.getWidth() >= 80);
        }

        auto* osc1 = panelNamed (panels, "Osc 1");
        auto* osc2 = panelNamed (panels, "Osc 2");
        auto* osc3 = panelNamed (panels, "Osc 3");
        auto* mixer = panelNamed (panels, "Mixer");
        auto* filter = panelNamed (panels, "Low-pass");
        auto* controllers = panelNamed (panels, "Controllers");
        auto* fenv = panelNamed (panels, "Filter Env");
        auto* aenv = panelNamed (panels, "Amp Env");
        auto* lfo = panelNamed (panels, "LFO");
        if (osc1 == nullptr || mixer == nullptr || lfo == nullptr)
            return;

        // Mini signal-flow rows. Measure preferred heights, then give leftover space to the graphs.
        const auto width = area.getWidth();
        const auto oscW = (width - 2 * panelGap) / 3;
        const int filterMin = 242, voiceMin = 146;
        const auto mixerW = juce::jmax (280, width - filterMin - voiceMin - 2 * panelGap);
        const int fenvMin = 304, aenvMin = 242;
        const auto lfoW = juce::jmax (280, width - fenvMin - aenvMin - 2 * panelGap);

        const auto oscH = juce::jmax (osc1->getPreferredHeight (oscW), osc2->getPreferredHeight (oscW),
                                      osc3->getPreferredHeight (oscW));
        const auto mixH = juce::jmax (mixer->getPreferredHeight (mixerW), filter->getPreferredHeight (filterMin),
                                      controllers->getPreferredHeight (voiceMin));
        const auto envH = juce::jmax (fenv->getPreferredHeight (fenvMin), aenv->getPreferredHeight (aenvMin),
                                      lfo->getPreferredHeight (lfoW));

        const auto rowsHeight = oscH + mixH + envH + 3 * panelGap;
        const auto graphHeight = juce::jlimit (minGraphHeight, maxGraphHeight,
                                               area.getHeight() - rowsHeight);

        {
            graphStripArea = area.removeFromTop (graphHeight);
            auto graphs = graphStripArea;
            const auto gw = (graphs.getWidth() - 3 * panelGap) / 4;
            oscGraph->setBounds (graphs.removeFromLeft (gw));
            graphs.removeFromLeft (panelGap);
            filterGraph->setBounds (graphs.removeFromLeft (gw));
            graphs.removeFromLeft (panelGap);
            envGraph->setBounds (graphs.removeFromLeft (gw));
            graphs.removeFromLeft (panelGap);
            lfoGraph->setBounds (graphs);
        }

        area.removeFromTop (panelGap);
        {
            auto row = area.removeFromTop (oscH);
            const auto cell = (row.getWidth() - 2 * panelGap) / 3;
            osc1->setBounds (row.removeFromLeft (cell));
            row.removeFromLeft (panelGap);
            osc2->setBounds (row.removeFromLeft (cell));
            row.removeFromLeft (panelGap);
            osc3->setBounds (row);
        }

        area.removeFromTop (panelGap);
        {
            auto row = area.removeFromTop (mixH);
            controllers->setBounds (row.removeFromRight (voiceMin));
            row.removeFromRight (panelGap);
            filter->setBounds (row.removeFromRight (filterMin));
            row.removeFromRight (panelGap);
            mixer->setBounds (row);
        }

        area.removeFromTop (panelGap);
        {
            auto row = area.removeFromTop (envH);
            fenv->setBounds (row.removeFromLeft (fenvMin));
            row.removeFromLeft (panelGap);
            aenv->setBounds (row.removeFromLeft (aenvMin));
            row.removeFromLeft (panelGap);
            lfo->setBounds (row);
        }
    }
}
