#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

namespace dooblrprophet
{
    namespace id = params::id;
    namespace Palette = ui::Palette;
    using Cell = ui::Section::Cell;
    using Kind = ui::Section::Kind;

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
        constexpr int headerHeight = 60, margin = 10, sectionGap = 8, meterWidth = 34;
        constexpr std::array<int, 3> voicePresets { 5, 6, 10 };

        // Section colours: the design-system palette, one per signal-path stage.
        const juce::Colour modColour = Palette::frozen, oscColour = Palette::accent, filterColour = Palette::dynamic,
                           envColour = Palette::master, voiceColour = Palette::spectrum;

        Cell knob (const char* pid, const char* caption, float span = 1.0f) { return { Kind::knob, pid, caption, nullptr, span }; }
        Cell toggle (const char* pid, const char* caption, float span = 1.0f) { return { Kind::toggle, pid, caption, nullptr, span }; }
        Cell combo (const char* pid, const char* caption, float span = 1.0f) { return { Kind::combo, pid, caption, nullptr, span }; }
        Cell extra (juce::Component& c, float span = 1.0f) { return { Kind::extra, {}, {}, &c, span }; }

        float valueOf (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            // From the parameter itself, not the raw value (which can lag a listener callback).
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }
    }

    DooblrProphetEditor::DooblrProphetEditor (DooblrProphetProcessor& p)
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
        styleLabel (statusLabel, {}, 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        statusLabel.setMinimumHorizontalScale (0.85f);
        styleLabel (voicesCaption, "Voices", 12.0f, Palette::textMuted, juce::Justification::centredLeft);

        for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption })
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
        randomButton.setColour (juce::TextButton::buttonColourId, modColour.withAlpha (0.22f));
        randomButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
        randomButton.onClick = [this] { randomise(); };

        saveButton.setComponentID ("savePreset");
        saveButton.setTooltip ("Save the current patch as a user preset file (.dprophet)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset file (.dprophet)");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, 0.0);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &randomButton, (juce::Component*) &saveButton, (juce::Component*) &loadButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        buildPanel();

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

    DooblrProphetEditor::~DooblrProphetEditor()
    {
        stopTimer();
        synth.presetChanged.removeChangeListener (this);
        sections.clear(); // before the extras they lay out
        setLookAndFeel (nullptr);
    }

    //==============================================================================
    ui::Section& DooblrProphetEditor::addSection (int row, const juce::String& title, juce::Colour colour, int columns,
                                                  const std::vector<ui::Section::Row>& sectionRows)
    {
        sections.push_back (std::make_unique<ui::Section> (state, title, colour, columns, sectionRows));
        rows[(size_t) row].push_back ((int) sections.size() - 1);
        addAndMakeVisible (*sections.back());
        return *sections.back();
    }

    void DooblrProphetEditor::buildPanel()
    {
        // Top row: modulation and oscillators.
        addSection (0, "Poly-Mod", modColour, 2,
                    { { knob (id::pmodFiltEnv, "Filter Env"), knob (id::pmodOsc2, "Osc B") },
                      { toggle (id::pmodDestFreq1, "Freq A", 2.0f) },
                      { toggle (id::pmodDestShape1, "Shape A", 2.0f) },
                      { toggle (id::pmodDestFilter, "Filter", 2.0f) } });

        addSection (0, "LFO / Wheel-Mod", modColour, 5,
                    { { combo (id::lfoWave, "Wave", 2.5f), combo (id::lfoDivision, "Sync Division", 2.5f) },
                      { knob (id::lfoRate, "Rate", 1.25f), knob (id::lfoInitial, "Initial", 1.25f),
                        knob (id::lfoWheel, "Wheel", 1.25f), knob (id::lfoNoiseMix, "Noise Mix", 1.25f) },
                      { toggle (id::lfoDestFreq1, "Freq A", 5.0f / 3.0f), toggle (id::lfoDestFreq2, "Freq B", 5.0f / 3.0f),
                        toggle (id::lfoDestShape1, "Shape A", 5.0f / 3.0f) },
                      { toggle (id::lfoDestShape2, "Shape B", 5.0f / 3.0f), toggle (id::lfoDestLpf, "Low-Pass", 5.0f / 3.0f),
                        toggle (id::lfoDestHpf, "High-Pass", 5.0f / 3.0f) },
                      { toggle (id::lfoDestAmp, "Amp", 5.0f / 3.0f) } })
            .setTitleSwitch (state, id::lfoSync, { "Free", "Sync" });

        addSection (0, "Oscillator A", oscColour, 4,
                    { { knob (id::osc1Coarse, "Freq"), knob (id::osc1Fine, "Fine"), knob (id::osc1Shape, "Shape"), knob (id::osc1PW, "PW") },
                      { toggle (id::osc1Saw, "Saw", 4.0f / 3.0f), toggle (id::osc1Tri, "Tri", 4.0f / 3.0f), toggle (id::osc1Pulse, "Pulse", 4.0f / 3.0f) },
                      { toggle (id::osc1Sync, "Sync", 4.0f / 3.0f) } })
            .setTitleSwitch (state, id::osc1Mode, { "Variable", "Switched" });

        addSection (0, "Oscillator B", oscColour, 4,
                    { { knob (id::osc2Coarse, "Freq"), knob (id::osc2Fine, "Fine"), knob (id::osc2Shape, "Shape"), knob (id::osc2PW, "PW") },
                      { toggle (id::osc2Saw, "Saw", 4.0f / 3.0f), toggle (id::osc2Tri, "Tri", 4.0f / 3.0f), toggle (id::osc2Pulse, "Pulse", 4.0f / 3.0f) },
                      { toggle (id::osc2LowFreq, "Lo Freq", 4.0f / 3.0f), toggle (id::osc2KeyTrack, "Kbd", 4.0f / 3.0f) } })
            .setTitleSwitch (state, id::osc2Mode, { "Variable", "Switched" });

        // Bottom row: mixer, filter, envelopes, voices.
        addSection (1, "Mixer", oscColour, 2,
                    { { knob (id::mixOsc1, "Osc A"), knob (id::mixOsc2, "Osc B") },
                      { knob (id::mixSub, "Sub"), knob (id::mixNoise, "Noise") } });

        addSection (1, "Filter", filterColour, 4,
                    { { knob (id::lpfCutoff, "Cutoff"), knob (id::lpfReso, "Resonance"), knob (id::lpfEnvAmt, "Env Amt"), knob (id::lpfKeyTrack, "Kbd") },
                      { knob (id::lpfDrive, "Drive"), knob (id::hpfCutoff, "HP Cutoff"), knob (id::hpfReso, "HP Reso"), knob (id::hpfEnvAmt, "HP Env") } })
            .setTitleSwitch (state, id::lpfModel, { "3320", "2040" });

        addSection (1, "Filter Envelope", envColour, 3,
                    { { knob (id::fenvAttack, "Attack"), knob (id::fenvDecay, "Decay"), knob (id::fenvSustain, "Sustain") },
                      { knob (id::fenvRelease, "Release"), knob (id::fenvVelAmt, "Velocity") } });

        addSection (1, "Amp Envelope", envColour, 3,
                    { { knob (id::aenvAttack, "Attack"), knob (id::aenvDecay, "Decay"), knob (id::aenvSustain, "Sustain") },
                      { knob (id::aenvRelease, "Release"), knob (id::aenvVelAmt, "Velocity") } });

        for (size_t i = 0; i < voicePresetButtons.size(); ++i)
        {
            auto& button = voicePresetButtons[i];
            const auto count = voicePresets[i];
            button.setButtonText (juce::String (count));
            button.setComponentID ("voicesPreset" + juce::String (count));
            button.setTooltip ("Set Voices to " + juce::String (count));
            button.setClickingTogglesState (false);
            button.setColour (juce::TextButton::buttonOnColourId, voiceColour.withAlpha (0.45f));
            button.onClick = [this, count] { setVoices (count); };
        }

        addSection (1, "Voices", voiceColour, 4,
                    { { knob (id::voices, "Voices"), knob (id::unisonVoices, "Unison"), knob (id::unisonDetune, "Detune"), knob (id::glide, "Glide") },
                      { extra (voicesCaption), extra (voicePresetButtons[0]), extra (voicePresetButtons[1]), extra (voicePresetButtons[2]) },
                      { knob (id::vintage, "Vintage"), knob (id::bendRange, "Bend"), knob (id::atFilter, "AT Filter"), knob (id::atLfo, "AT LFO") },
                      { extra (statusLabel, 4.0f) } })
            .setTitleSwitch (state, id::voiceMode, { "Poly", "Unison", "Mono" });
    }

    juce::Component* DooblrProphetEditor::findControl (const juce::String& parameterId) const
    {
        for (const auto& section : sections)
            if (auto* control = section->findControl (parameterId))
                return control;

        return parameterId == id::output ? const_cast<juce::Slider*> (&outputSlider) : nullptr;
    }

    void DooblrProphetEditor::setVoices (int count)
    {
        if (auto* param = state.getParameter (id::voices))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) count));
            param->endChangeGesture();
        }

        updateEnablement();
        updateUndoButtons();
    }

    void DooblrProphetEditor::randomise()
    {
        juce::Random random; // seeded from the clock: a new patch each press
        synth.randomisePatch (random);
        refreshPresetBox();
        updateEnablement();
        updateUndoButtons();
    }

    void DooblrProphetEditor::updateEnablement()
    {
        const auto setEnabled = [this] (const char* pid, bool enabled)
        {
            if (auto* control = findControl (pid))
                control->setEnabled (enabled);
        };

        // Variable mode uses the Shape knob; Switched mode uses the wave switches. Pulse Width acts in both.
        const bool osc1Switched = valueOf (state, id::osc1Mode) > 0.5f;
        const bool osc2Switched = valueOf (state, id::osc2Mode) > 0.5f;
        setEnabled (id::osc1Shape, ! osc1Switched);
        setEnabled (id::osc2Shape, ! osc2Switched);

        for (auto* pid : { id::osc1Saw, id::osc1Tri, id::osc1Pulse })
            setEnabled (pid, osc1Switched);

        for (auto* pid : { id::osc2Saw, id::osc2Tri, id::osc2Pulse })
            setEnabled (pid, osc2Switched);

        // Poly-Mod destinations only matter when a Poly-Mod source is turned up.
        const bool polyModOn = std::abs (valueOf (state, id::pmodFiltEnv)) > 1.0e-4f || valueOf (state, id::pmodOsc2) > 1.0e-4f;

        for (auto* pid : { id::pmodDestFreq1, id::pmodDestShape1, id::pmodDestFilter })
            setEnabled (pid, polyModOn);

        const bool synced = valueOf (state, id::lfoSync) > 0.5f;
        setEnabled (id::lfoRate, ! synced);
        setEnabled (id::lfoDivision, synced);

        // Voices sets the poly voice count; Unison and Detune only act in Unison mode; Mono uses one voice.
        const auto mode = juce::roundToInt (valueOf (state, id::voiceMode));
        setEnabled (id::voices, mode == 0);
        setEnabled (id::unisonVoices, mode == 1);
        setEnabled (id::unisonDetune, mode == 1);

        const auto voices = juce::roundToInt (valueOf (state, id::voices));

        for (size_t i = 0; i < voicePresetButtons.size(); ++i)
        {
            voicePresetButtons[i].setEnabled (mode == 0);
            voicePresetButtons[i].setToggleState (voices == voicePresets[i], juce::dontSendNotification);
        }

        voicesCaption.setEnabled (mode == 0);
        voicesCaption.setAlpha (mode == 0 ? 1.0f : 0.5f);
    }

    void DooblrProphetEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 4000;
        updateStatus();
    }

    void DooblrProphetEditor::updateStatus()
    {
        juce::String text;

        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() < transientStatusUntil)
        {
            text = transientStatus;
        }
        else
        {
            transientStatus.clear();
            const auto s = synth.readSettings();
            const bool fm = s.pmodFreq1 && (std::abs (s.pmodFiltEnv) > 1.0e-4f || s.pmodOsc2 > 1.0e-4f);
            text = juce::String (synth.getEngine().getNumActiveVoices()) + " sounding  |  " + (fm ? "4x (Poly-Mod FM)" : "2x oversampled");
        }

        if (statusLabel.getText() != text)
            statusLabel.setText (text, juce::dontSendNotification);
    }

    //==============================================================================
    void DooblrProphetEditor::refreshPresetBox()
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

    void DooblrProphetEditor::presetChosen()
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

    void DooblrProphetEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (synth.getPresetName());
        if (name.isEmpty())
            name = "Patch";

        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-prophet preset",
                                                           folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrProphetEditor> safeThis (this);

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

    void DooblrProphetEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-prophet preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrProphetEditor> safeThis (this);

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

    //==============================================================================
    void DooblrProphetEditor::timerCallback()
    {
        updateEnablement();
        updateUndoButtons();
        updateStatus();

        if (synth.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrProphetEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrProphetEditor::updateUndoButtons()
    {
        auto& history = synth.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrProphetEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto command = juce::ModifierKeys::commandModifier; // Cmd on macOS, Ctrl elsewhere
        const auto shift = juce::ModifierKeys::shiftModifier;
        auto& history = synth.getUndoHistory();

        if (key == juce::KeyPress ('z', command, 0))
        {
            history.undo();
            updateUndoButtons();
            return true; // claimed, so the host does not also undo
        }

        if (key == juce::KeyPress ('z', command | shift, 0) || key == juce::KeyPress ('y', command, 0))
        {
            history.redo();
            updateUndoButtons();
            return true;
        }

        return false; // everything else goes to the host
    }

    //==============================================================================
    void DooblrProphetEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);

        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);
    }

    void DooblrProphetEditor::resized()
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

        panelArea = bounds.reduced (margin);

        // Each row shares its width by the sections' column counts; the two rows share the
        // height so every knob row gets the same extra room (the bottom row has two).
        const auto rowWidths = [this] (const std::vector<int>& row)
        {
            int columns = 0;
            for (auto index : row)
                columns += sections[(size_t) index]->getColumns();

            const auto spare = panelArea.getWidth() - sectionGap * ((int) row.size() - 1) - 2 * ui::Section::padding * (int) row.size();
            const auto unit = (float) spare / (float) juce::jmax (1, columns);
            std::vector<int> widths;
            float x = 0.0f;
            int used = 0;

            for (size_t i = 0; i < row.size(); ++i)
            {
                x += unit * (float) sections[(size_t) row[i]]->getColumns() + 2.0f * ui::Section::padding;
                const auto right = i + 1 == row.size() ? panelArea.getWidth() - sectionGap * ((int) row.size() - 1) : juce::roundToInt (x);
                widths.push_back (right - used);
                used = right;
            }

            return widths;
        };

        std::array<std::vector<int>, 2> widths { rowWidths (rows[0]), rowWidths (rows[1]) };
        std::array<int, 2> minHeights {}, knobRows {};

        for (size_t r = 0; r < rows.size(); ++r)
            for (size_t i = 0; i < rows[r].size(); ++i)
            {
                auto& section = *sections[(size_t) rows[r][i]];
                section.setSize (widths[r][i], section.getHeight()); // the preferred height depends on the width
                minHeights[r] = juce::jmax (minHeights[r], section.getPreferredHeight (ui::Section::minKnobRowHeight));
                knobRows[r] = juce::jmax (knobRows[r], section.getNumKnobRows());
            }

        const auto spare = juce::jmax (0, panelArea.getHeight() - sectionGap - minHeights[0] - minHeights[1]);
        const auto topHeight = minHeights[0] + spare * knobRows[0] / juce::jmax (1, knobRows[0] + knobRows[1]);

        auto area = panelArea;
        std::array<juce::Rectangle<int>, 2> rowAreas;
        rowAreas[0] = area.removeFromTop (topHeight);
        area.removeFromTop (sectionGap);
        rowAreas[1] = area;

        for (size_t r = 0; r < rows.size(); ++r)
        {
            auto rowArea = rowAreas[r];
            for (size_t i = 0; i < rows[r].size(); ++i)
            {
                sections[(size_t) rows[r][i]]->setBounds (rowArea.removeFromLeft (widths[r][i]));
                rowArea.removeFromLeft (sectionGap);
            }
        }
    }
}
