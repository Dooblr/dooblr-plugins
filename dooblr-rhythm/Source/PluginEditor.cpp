#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblrrhythm
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
        constexpr int headerHeight = 60, margin = 12, panelGap = 8, meterWidth = 34;

        void forEachComponent (juce::Component& root, const std::function<void (juce::Component&)>& fn)
        {
            fn (root);
            for (auto* child : root.getChildren())
                if (child != nullptr)
                    forEachComponent (*child, fn);
        }

        using Spec = ui::ControlPanel::Spec;
        using Kind = ui::ControlPanel::Kind;
    }

    DooblrRhythmEditor::DooblrRhythmEditor (DooblrRhythmProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          generatePanel (p.getState(), "Arrange", Palette::accent, {
              { id::style, "Style", Kind::combo, 150 },
              { id::patternLength, "Bars", Kind::knob },
              { id::timeSignature, "Meter", Kind::combo, 88 },
              { id::claveDirection, "Clave", Kind::combo, 72 },
              { id::section, "Section", Kind::combo, 92 },
          }),
          macrosPanel (p.getState(), "Groove", Palette::dynamic, {
              { id::complexity, "Complexity", Kind::knob },
              { id::syncopation, "Syncopation", Kind::knob },
              { id::humanize, "Humanize", Kind::knob },
              { id::swing, "Swing", Kind::knob },
              { id::pushPull, "Push / Pull", Kind::knob },
          }),
          grid (p),
          meter (&p.getKit().peak)
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);
        outputCaption.setComponentID ("outputCaption");
        outputCaption.setMinimumHorizontalScale (1.0f);
        styleLabel (bpmLabel, "Tempo", 11.0f, Palette::textMuted, juce::Justification::centred);
        styleLabel (bpmValue, "120", 18.0f, Palette::textPrimary, juce::Justification::centred, true);
        bpmValue.setComponentID ("bpm");

        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &outputCaption, &bpmLabel, &bpmValue })
            addAndMakeVisible (label);

        undoButton.setComponentID ("undo");
        redoButton.setComponentID ("redo");
        undoButton.setTooltip ("Undo (Cmd-Z)");
        redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
        undoButton.onClick = [this] { proc.getUndoHistory().undo(); updateUndoButtons(); };
        redoButton.onClick = [this] { proc.getUndoHistory().redo(); updateUndoButtons(); };

        presetBox.setComponentID ("preset");
        presetBox.setTooltip ("Factory and user presets");
        presetBox.onChange = [this] { presetChosen(); };
        saveButton.setComponentID ("savePreset");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (id::output)))
            outputSlider.setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);

        generateButton.setComponentID ("generate");
        generateButton.setColour (juce::TextButton::buttonColourId, Palette::accent);
        generateButton.setColour (juce::TextButton::textColourOffId, Palette::background);
        generateButton.onClick = [this]
        {
            proc.generateArrangement();
            grid.refresh();
            setStatus ("Generated seed " + juce::String ((int) proc.currentSeed()));
        };
        variationButton.setComponentID ("variation");
        variationButton.onClick = [this]
        {
            proc.variation();
            grid.refresh();
            setStatus ("Variation " + juce::String ((int) proc.currentSeed()));
        };
        regenButton.setComponentID ("regen");
        regenButton.onClick = [this]
        {
            proc.regenerate();
            grid.refresh();
            setStatus ("Regen " + juce::String ((int) proc.currentSeed()));
        };
        playButton.setComponentID ("play");
        playButton.setClickingTogglesState (true);
        playButton.onClick = [this]
        {
            proc.setPreviewPlaying (playButton.getToggleState());
        };
        dragMidiButton.setComponentID ("dragMidi");
        dragWavButton.setComponentID ("dragWav");
        loadSampleButton.setComponentID ("loadSample");
        loadSampleButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Load a replacement WAV", juce::File(), "*.wav;*.aif;*.aiff");
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto file = chooser.getResult();
                                          if (file == juce::File()) return;
                                          const auto err = proc.loadReplacement (selectedInstrument, 0, file);
                                          setStatus (err.isEmpty() ? "Loaded " + file.getFileName() : err);
                                      });
        };

        dragMidiButton.addMouseListener (this, false);
        dragWavButton.addMouseListener (this, false);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                    (juce::Component*) &presetBox, (juce::Component*) &saveButton,
                                    (juce::Component*) &loadButton, (juce::Component*) &outputSlider,
                                    (juce::Component*) &meter, (juce::Component*) &generateButton,
                                    (juce::Component*) &variationButton, (juce::Component*) &regenButton,
                                    (juce::Component*) &playButton, (juce::Component*) &dragMidiButton,
                                    (juce::Component*) &dragWavButton, (juce::Component*) &loadSampleButton })
            addAndMakeVisible (c);

        addAndMakeVisible (generatePanel);
        addAndMakeVisible (macrosPanel);
        addAndMakeVisible (grid);

        for (int i = 0; i < numInstruments; ++i)
        {
            const auto name = juce::String (instrumentNames[i]);
            auto panel = std::make_unique<ui::ControlPanel> (state, name, ui::bandColour (i), std::vector<Spec> {
                { params::instrumentParam (i, "density"),     "Density",     Kind::knob },
                { params::instrumentParam (i, "complexity"),  "Complexity",  Kind::knob },
                { params::instrumentParam (i, "syncopation"), "Syncopation", Kind::knob },
                { params::instrumentParam (i, "velocity"),    "Velocity",    Kind::knob },
                { params::instrumentParam (i, "humanize"),    "Humanize",    Kind::knob },
                { params::instrumentParam (i, "timing"),      "Timing",      Kind::knob },
                { params::instrumentParam (i, "volume"),      "Volume",      Kind::knob },
                { params::instrumentParam (i, "pan"),         "Pan",         Kind::knob },
                { params::instrumentParam (i, "pitch"),       "Pitch",       Kind::knob },
                { params::instrumentParam (i, "decay"),       "Decay",       Kind::knob },
            });
            addChildComponent (*panel);
            instrumentPanels.push_back (std::move (panel));
        }

        grid.onSelectInstrument = [this] (int i) { showInstrumentPanel (i); };
        grid.onDragInstrumentMidi = [this] (int i)
        {
            setStatus ("MIDI " + juce::String (instrumentNames[i]));
            startExternalDrag (proc.writeMidiFile (i));
        };
        showInstrumentPanel (0);
        grid.refresh();

        proc.presetChanged.addChangeListener (this);
        proc.patternChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateBpm();

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
        {
            grid.setPlayheadStep (proc.playheadStep());
            grid.repaint();
        });

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (defaultWidth, defaultHeight);
        startTimerHz (15);
    }

    DooblrRhythmEditor::~DooblrRhythmEditor()
    {
        stopTimer();
        vblank.reset();
        proc.presetChanged.removeChangeListener (this);
        proc.patternChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    juce::Component* DooblrRhythmEditor::findControl (const juce::String& parameterId) const
    {
        if (auto* g = grid.findControl (parameterId))
            return g;
        juce::Component* found = nullptr;
        forEachComponent (const_cast<DooblrRhythmEditor&> (*this), [&] (juce::Component& c)
        {
            if (c.getComponentID() == parameterId && found == nullptr)
                found = &c;
        });
        return found;
    }

    void DooblrRhythmEditor::showInstrumentPanel (int index)
    {
        selectedInstrument = juce::jlimit (0, numInstruments - 1, index);
        for (int i = 0; i < (int) instrumentPanels.size(); ++i)
            instrumentPanels[(size_t) i]->setVisible (i == selectedInstrument);
        resized();
    }

    void DooblrRhythmEditor::timerCallback()
    {
        updateUndoButtons();
        updateBpm();
        playButton.setToggleState (proc.isPreviewPlaying() && ! proc.hostPlaying(), juce::dontSendNotification);
        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() > transientStatusUntil)
        {
            transientStatus.clear();
            statusLabel.setText ({}, juce::dontSendNotification);
        }
        if (proc.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrRhythmEditor::updateBpm()
    {
        const auto bpm = proc.hostBpm();
        bpmValue.setText (juce::String (juce::roundToInt (bpm)), juce::dontSendNotification);
        bpmLabel.setText (proc.hostPlaying() ? "Host BPM" : "Tempo", juce::dontSendNotification);
    }

    void DooblrRhythmEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &proc.patternChanged)
            grid.refresh();
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrRhythmEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void DooblrRhythmEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrRhythmEditor::refreshPresetBox()
    {
        updatingPresetBox = true;
        presetBox.clear (juce::dontSendNotification);
        presetBox.addSectionHeading ("Factory");
        int i = 0;
        for (const auto& patch : presets::factory())
            presetBox.addItem (patch.name, factoryIdBase + i++);

        userPresetFiles = presets::userPresets();
        if (! userPresetFiles.isEmpty())
        {
            presetBox.addSectionHeading ("User");
            int u = 0;
            for (const auto& file : userPresetFiles)
                presetBox.addItem (file.getFileNameWithoutExtension(), userIdBase + u++);
        }

        shownPresetName = proc.getPresetName();
        int selected = 0;
        for (int n = 0; n < (int) presets::factory().size(); ++n)
            if (presets::factory()[(size_t) n].name == shownPresetName)
                selected = factoryIdBase + n;
        for (int n = 0; n < userPresetFiles.size(); ++n)
            if (userPresetFiles[n].getFileNameWithoutExtension() == shownPresetName)
                selected = userIdBase + n;
        if (selected > 0)
            presetBox.setSelectedId (selected, juce::dontSendNotification);
        updatingPresetBox = false;
    }

    void DooblrRhythmEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;
        const auto chosen = presetBox.getSelectedId();
        if (chosen >= userIdBase)
        {
            const auto index = chosen - userIdBase;
            if (juce::isPositiveAndBelow (index, userPresetFiles.size()))
                setStatus (proc.loadUserPreset (userPresetFiles[index]));
        }
        else if (chosen >= factoryIdBase)
            proc.setCurrentProgram (chosen - factoryIdBase);
        grid.refresh();
    }

    void DooblrRhythmEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-rhythm preset",
                                                           folder.getChildFile (proc.getPresetName() + presets::fileExtension),
                                                           "*" + juce::String (presets::fileExtension));
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file != juce::File())
                                          setStatus (proc.saveUserPreset (file));
                                      refreshPresetBox();
                                  });
    }

    void DooblrRhythmEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-rhythm preset",
                                                           folder.isDirectory() ? folder : juce::File(),
                                                           "*" + juce::String (presets::fileExtension));
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file != juce::File())
                                          setStatus (proc.loadUserPreset (file));
                                      refreshPresetBox();
                                      grid.refresh();
                                  });
    }

    bool DooblrRhythmEditor::keyPressed (const juce::KeyPress& key)
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

    bool DooblrRhythmEditor::isInterestedInFileDrag (const juce::StringArray& files)
    {
        return files.size() == 1 && (files[0].endsWithIgnoreCase (".wav") || files[0].endsWithIgnoreCase (".aif")
                                     || files[0].endsWithIgnoreCase (".aiff"));
    }

    void DooblrRhythmEditor::filesDropped (const juce::StringArray& files, int, int)
    {
        if (files.isEmpty())
            return;
        const auto err = proc.loadReplacement (selectedInstrument, 0, juce::File (files[0]));
        setStatus (err.isEmpty() ? "Loaded " + juce::File (files[0]).getFileName() : err);
    }

    void DooblrRhythmEditor::mouseDrag (const juce::MouseEvent& e)
    {
        AudioProcessorEditor::mouseDrag (e);
        if (e.eventComponent == &dragMidiButton && ! dragFromMidi && e.getDistanceFromDragStart() > 4)
        {
            dragFromMidi = true;
            startExternalDrag (proc.writeMidiFile());
        }
        if (e.eventComponent == &dragWavButton && ! dragFromWav && e.getDistanceFromDragStart() > 4)
        {
            dragFromWav = true;
            startExternalDrag (proc.writeWavFile());
        }
    }

    void DooblrRhythmEditor::mouseUp (const juce::MouseEvent& e)
    {
        AudioProcessorEditor::mouseUp (e);
        if (e.eventComponent == &dragMidiButton)
            dragFromMidi = false;
        if (e.eventComponent == &dragWavButton)
            dragFromWav = false;
    }

    void DooblrRhythmEditor::startExternalDrag (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void DooblrRhythmEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        auto fillPanel = [&g] (juce::Rectangle<int> r)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        };
        fillPanel (generateArea);
        fillPanel (macrosArea);
        fillPanel (detailArea);
        fillPanel (gridArea);
    }

    void DooblrRhythmEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (headerHeight);
        {
            auto header = headerArea.reduced (32, 10);
            auto titleColumn = header.removeFromLeft (juce::jlimit (148, 220, header.getWidth() / 6));
            titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
            subtitleLabel.setBounds (titleColumn);

            header.removeFromLeft (8);
            undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (4);
            redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (8);

            meter.setBounds (header.removeFromRight (meterWidth));
            header.removeFromRight (10);
            const auto sliderWidth = juce::jlimit (88, 150, header.getWidth() / 5);
            outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
            outputCaption.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 24));
            header.removeFromRight (8);
            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (8);
            const auto presetWidth = juce::jlimit (100, 180, header.getWidth());
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        auto body = bounds.reduced (margin, 8);
        statusLabel.setBounds (body.removeFromBottom (16));
        body.removeFromBottom (4);

        const auto topH = juce::jlimit (208, 248, body.getHeight() / 3);
        auto top = body.removeFromTop (topH);
        body.removeFromTop (panelGap);

        generateArea = top.removeFromLeft (juce::jlimit (420, 640, top.getWidth() * 2 / 3));
        top.removeFromLeft (panelGap);
        macrosArea = top;

        {
            auto g = generateArea.reduced (8, 6);
            auto hero = g.removeFromRight (juce::jlimit (200, 248, g.getWidth() / 3));
            generateButton.setBounds (hero.removeFromTop (48).reduced (4, 0));
            hero.removeFromTop (4);
            auto row = hero.removeFromTop (28);
            const int varyW = juce::jmax (112, row.getWidth() * 5 / 8);
            variationButton.setBounds (row.removeFromLeft (varyW).reduced (2, 1));
            regenButton.setBounds (row.reduced (2, 1));
            auto bpmCol = g.removeFromRight (64);
            bpmLabel.setBounds (bpmCol.removeFromTop (16));
            bpmValue.setBounds (bpmCol.removeFromTop (24));
            generatePanel.setBounds (g);
        }
        macrosPanel.setBounds (macrosArea.reduced (8, 6));

        const auto detailH = juce::jlimit (118, 160, body.getHeight() / 4);
        detailArea = body.removeFromBottom (detailH);
        body.removeFromBottom (panelGap);
        gridArea = body;

        {
            auto d = detailArea.reduced (8, 6);
            auto tools = d.removeFromRight (juce::jlimit (90, 120, d.getWidth() / 6));
            playButton.setBounds (tools.removeFromTop (26).reduced (2, 1));
            dragMidiButton.setBounds (tools.removeFromTop (26).reduced (2, 1));
            dragWavButton.setBounds (tools.removeFromTop (26).reduced (2, 1));
            loadSampleButton.setBounds (tools.removeFromTop (26).reduced (2, 1));
            for (auto& panel : instrumentPanels)
                panel->setBounds (d);
        }
        grid.setBounds (gridArea.reduced (6, 6));
    }
}
