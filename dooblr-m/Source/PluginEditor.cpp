#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblrm
{
    namespace id = params::id;
    namespace Palette = ui::Palette;

    namespace
    {
        void styleLabel (juce::Label& label, const juce::String& text, float fontSize, juce::Colour colour,
                         juce::Justification justification, bool bold = false)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::FontOptions (fontSize, bold ? juce::Font::bold : juce::Font::plain));
            label.setColour (juce::Label::textColourId, colour);
            label.setJustificationType (justification);
            label.setInterceptsMouseClicks (false, false);
        }

        constexpr int factoryIdBase = 1, userIdBase = 1001;
        constexpr int headerHeight = 60, voiceBarHeight = 84, margin = 8, gap = 6;
        constexpr int meterWidth = 34;

        float valueOf (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }

        juce::StringArray dimmedIds (juce::AudioProcessorValueTreeState& state)
        {
            juce::StringArray ids;
            if (valueOf (state, id::mixOsc2On) < 0.5f)
            {
                ids.add (id::mixOsc2);
                ids.add (id::osc2Freq);
                ids.add (id::osc2Range);
                ids.add (id::osc2Wave);
            }
            if (valueOf (state, id::mixOsc3On) < 0.5f && valueOf (state, id::oscMod) < 0.5f
                && valueOf (state, id::filterMod) < 0.5f)
            {
                ids.add (id::mixOsc3);
            }
            if (valueOf (state, id::mixOsc1On) < 0.5f)
                ids.add (id::mixOsc1);
            if (valueOf (state, id::mixExtOn) < 0.5f)
                ids.add (id::mixExt);
            if (valueOf (state, id::mixNoiseOn) < 0.5f)
            {
                ids.add (id::mixNoise);
                ids.add (id::noiseType);
            }
            if (valueOf (state, id::oscMod) < 0.5f && valueOf (state, id::filterMod) < 0.5f)
            {
                ids.add (id::modMix);
                ids.add (id::modWheel);
            }
            return ids;
        }
    }

    DooblrMEditor::DooblrMEditor (DooblrMProcessor& p)
        : juce::AudioProcessorEditor (p),
          synth (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap()),
          scope (&p.getEngine().getScopeTap())
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

        saveButton.setComponentID ("savePreset");
        saveButton.setTooltip ("Save the current patch as a user preset file (.dm)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset file (.dm)");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, 0.0);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        buildBoard();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateEnablement();

        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        setResizable (true, true);
        startTimerHz (15);
    }

    DooblrMEditor::~DooblrMEditor()
    {
        synth.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    ui::SectionPanel& DooblrMEditor::addSection (const juce::String& title, juce::Colour colour)
    {
        sections.push_back (std::make_unique<ui::SectionPanel> (title, colour));
        addAndMakeVisible (*sections.back());
        return *sections.back();
    }

    ui::Knob& DooblrMEditor::addKnob (juce::Component& parent, const juce::String& pid, const juce::String& caption,
                                      juce::Colour fill)
    {
        knobs.push_back (std::make_unique<ui::Knob> (state, pid, caption, fill));
        parent.addAndMakeVisible (*knobs.back());
        return *knobs.back();
    }

    ui::RotarySwitch& DooblrMEditor::addSwitch (juce::Component& parent, const juce::String& pid, const juce::String& caption,
                                                const juce::StringArray& ticks)
    {
        rotaries.push_back (std::make_unique<ui::RotarySwitch> (state, pid, caption, ticks));
        parent.addAndMakeVisible (*rotaries.back());
        return *rotaries.back();
    }

    ui::BatToggle& DooblrMEditor::addToggle (juce::Component& parent, const juce::String& pid, const juce::String& caption)
    {
        toggles.push_back (std::make_unique<ui::BatToggle> (state, pid, caption));
        parent.addAndMakeVisible (*toggles.back());
        return *toggles.back();
    }

    ui::Wheel& DooblrMEditor::addWheel (juce::Component& parent, const juce::String& pid, const juce::String& caption, bool spring)
    {
        wheels.push_back (std::make_unique<ui::Wheel> (state, pid, caption, spring));
        parent.addAndMakeVisible (*wheels.back());
        return *wheels.back();
    }

    void DooblrMEditor::buildBoard()
    {
        auto& wheelsSec = addSection ("WHEELS", Palette::master);
        addWheel (wheelsSec, id::pitchBend, "Pitch", true);
        addWheel (wheelsSec, id::modWheel, "Mod", false);

        auto& voice = addSection ("VOICE", Palette::master);
        addSwitch (voice, id::notePriority, "Priority", juce::StringArray { "Low", "Last" });
        addToggle (voice, id::legato, "Legato");
        addToggle (voice, id::velocityOn, "Velocity");
        addKnob (voice, id::bendRange, "Bend", Palette::master);

        auto& ctrl = addSection ("CONTROLLERS", Palette::spectrum);
        addKnob (ctrl, id::tune, "Tune");
        addKnob (ctrl, id::glide, "Glide");
        addKnob (ctrl, id::modMix, "Mod Mix", Palette::dynamic);
        addToggle (ctrl, id::osc3Control, "Osc 3 Ctrl");
        addToggle (ctrl, id::oscMod, "Osc Mod");
        addToggle (ctrl, id::filterMod, "Filt Mod");

        auto& osc = addSection ("OSCILLATOR BANK", ui::bandColour (0));
        addSwitch (osc, id::osc1Range, "Osc 1 Range", params::rangeNames());
        addSwitch (osc, id::osc1Wave, "Osc 1 Wave", juce::StringArray { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" });
        addSwitch (osc, id::osc2Range, "Osc 2 Range", params::rangeNames());
        addKnob (osc, id::osc2Freq, "Osc 2 Freq", ui::bandColour (1));
        addSwitch (osc, id::osc2Wave, "Osc 2 Wave", juce::StringArray { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" });
        addSwitch (osc, id::osc3Range, "Osc 3 Range", params::rangeNames());
        addKnob (osc, id::osc3Freq, "Osc 3 Freq", ui::bandColour (2));
        addSwitch (osc, id::osc3Wave, "Osc 3 Wave", juce::StringArray { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" });

        auto& mix = addSection ("MIXER", ui::bandColour (3));
        addKnob (mix, id::mixOsc1, "Osc 1");
        addToggle (mix, id::mixOsc1On, "Osc 1");
        addKnob (mix, id::mixOsc2, "Osc 2");
        addToggle (mix, id::mixOsc2On, "Osc 2");
        addKnob (mix, id::mixOsc3, "Osc 3");
        addToggle (mix, id::mixOsc3On, "Osc 3");
        addKnob (mix, id::mixExt, "Ext / FB", Palette::dynamic);
        addToggle (mix, id::mixExtOn, "Ext");
        addKnob (mix, id::mixNoise, "Noise");
        addToggle (mix, id::mixNoiseOn, "Noise");
        addSwitch (mix, id::noiseType, "Noise", params::noiseNames());

        auto& mod = addSection ("MODIFIERS", Palette::dynamic);
        addKnob (mod, id::cutoff, "Cutoff", Palette::dynamic);
        addKnob (mod, id::emphasis, "Emphasis", Palette::dynamic);
        addKnob (mod, id::contourAmt, "Contour");
        addKnob (mod, id::fAttack, "F Atk");
        addKnob (mod, id::fDecay, "F Dec");
        addKnob (mod, id::fSustain, "F Sus");
        addToggle (mod, id::kybd1, "Kybd 1");
        addToggle (mod, id::kybd2, "Kybd 2");
        addKnob (mod, id::aAttack, "L Atk", Palette::accent);
        addKnob (mod, id::aDecay, "L Dec", Palette::accent);
        addKnob (mod, id::aSustain, "L Sus", Palette::accent);
        addToggle (mod, id::decaySwitch, "Decay");

        auto& out = addSection ("OUTPUT", Palette::accent);
        addKnob (out, id::volume, "Volume");
        addToggle (out, id::a440, "A-440");
        out.addAndMakeVisible (scope);
        out.addAndMakeVisible (filterReadout);
    }

    juce::Component* DooblrMEditor::findControl (const juce::String& parameterId) const
    {
        const std::function<juce::Component* (const juce::Component&)> search =
            [&] (const juce::Component& root) -> juce::Component*
        {
            if (root.getComponentID() == parameterId)
                return const_cast<juce::Component*> (&root);
            for (int i = 0; i < root.getNumChildComponents(); ++i)
                if (auto* found = search (*root.getChildComponent (i)))
                    return found;
            return nullptr;
        };
        return search (*this);
    }

    void DooblrMEditor::timerCallback()
    {
        updateUndoButtons();
        updateStatus();
        updateEnablement();
        scope.refresh();
        if (auto* p = state.getParameter (id::cutoff))
            if (auto* e = state.getParameter (id::emphasis))
                filterReadout.set (p->convertFrom0to1 (p->getValue()), e->convertFrom0to1 (e->getValue()));

        if (synth.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrMEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrMEditor::updateEnablement()
    {
        const auto dimmed = dimmedIds (state);
        const auto apply = [&] (juce::Component& c)
        {
            ui::setDimmed (c, dimmed.contains (c.getComponentID()));
        };

        for (auto& k : knobs) apply (k->getSlider());
        for (auto& r : rotaries) apply (r->getSlider());
        for (auto& t : toggles) apply (t->getButton());
        for (auto& w : wheels) apply (w->getSlider());
    }

    void DooblrMEditor::refreshPresetBox()
    {
        updatingPresetBox = true;
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
        bool found = false;
        for (int i = 0; i < (int) factory.size(); ++i)
            if (factory[(size_t) i].name == shownPresetName)
            {
                presetBox.setSelectedId (factoryIdBase + i, juce::dontSendNotification);
                found = true;
                break;
            }
        if (! found)
            for (int i = 0; i < userPresetFiles.size(); ++i)
                if (userPresetFiles[i].getFileNameWithoutExtension() == shownPresetName)
                {
                    presetBox.setSelectedId (userIdBase + i, juce::dontSendNotification);
                    found = true;
                    break;
                }
        if (! found)
        {
            presetBox.setText (shownPresetName, juce::dontSendNotification);
        }
        updatingPresetBox = false;
    }

    void DooblrMEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;
        const auto sel = presetBox.getSelectedId();
        if (sel >= userIdBase)
        {
            const auto index = sel - userIdBase;
            if (juce::isPositiveAndBelow (index, userPresetFiles.size()))
                setStatus (synth.loadUserPreset (userPresetFiles[index]));
            return;
        }
        if (sel >= factoryIdBase)
            synth.setCurrentProgram (sel - factoryIdBase);
    }

    void DooblrMEditor::savePreset()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save preset", presets::userDirectory(),
                                                           juce::String ("*") + presets::fileExtension);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (presets::fileExtension);
            setStatus (synth.saveUserPreset (file));
            refreshPresetBox();
        });
    }

    void DooblrMEditor::loadPreset()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load preset", presets::userDirectory(),
                                                           juce::String ("*") + presets::fileExtension);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file != juce::File())
            {
                setStatus (synth.loadUserPreset (file));
                refreshPresetBox();
            }
        });
    }

    void DooblrMEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrMEditor::updateStatus()
    {
        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() < transientStatusUntil)
            return;
        transientStatus = {};
        statusLabel.setText ({}, juce::dontSendNotification);
    }

    void DooblrMEditor::updateUndoButtons()
    {
        auto& history = synth.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrMEditor::keyPressed (const juce::KeyPress& key)
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

    void DooblrMEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);
    }

    void DooblrMEditor::resized()
    {
        auto bounds = getLocalBounds();

        headerArea = bounds.removeFromTop (headerHeight);
        {
            auto header = headerArea.reduced (32, 10);
            auto titleColumn = header.removeFromLeft (juce::jlimit (140, 210, header.getWidth() / 6));
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
            header.removeFromRight (12);

            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            const auto presetWidth = juce::jmin (header.getWidth(), 280);
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
            header.removeFromRight (8);
            statusLabel.setBounds (header);
        }

        contentArea = bounds.reduced (margin);
        layoutBoard();
    }

    void DooblrMEditor::layoutBoard()
    {
        if (sections.size() < 7)
            return;

        auto area = contentArea;
        auto& wheelsSec = *sections[0];
        auto& voice = *sections[1];
        auto& ctrl = *sections[2];
        auto& osc = *sections[3];
        auto& mix = *sections[4];
        auto& mod = *sections[5];
        auto& out = *sections[6];

        const auto place = [] (juce::Component& c, juce::Rectangle<int> r)
        {
            c.setBounds (r);
        };

        const auto voiceH = juce::jlimit (72, 96, voiceBarHeight);
        voiceBarArea = area.removeFromTop (voiceH);
        place (voice, voiceBarArea);
        area.removeFromTop (gap);

        auto body = area;
        const auto col = [&] (float share, int minW) {
            return juce::jmax (minW, (int) std::round ((float) body.getWidth() * share));
        };

        auto remain = body;
        const auto wW = col (0.08f, 64);
        place (wheelsSec, remain.removeFromLeft (wW));
        remain.removeFromLeft (gap);
        const auto cW = col (0.13f, 110);
        place (ctrl, remain.removeFromLeft (cW));
        remain.removeFromLeft (gap);
        const auto oW = col (0.27f, 220);
        place (osc, remain.removeFromLeft (oW));
        remain.removeFromLeft (gap);
        const auto mW = col (0.15f, 120);
        place (mix, remain.removeFromLeft (mW));
        remain.removeFromLeft (gap);
        const auto fW = col (0.24f, 200);
        place (mod, remain.removeFromLeft (fW));
        remain.removeFromLeft (gap);
        place (out, remain);

        auto grid = [] (juce::Rectangle<int> box, int cols, int rows, int index, int colSpan = 1)
        {
            const auto cw = box.getWidth() / juce::jmax (1, cols);
            const auto rh = box.getHeight() / juce::jmax (1, rows);
            const auto x = index % cols;
            const auto y = index / cols;
            return juce::Rectangle<int> (box.getX() + x * cw, box.getY() + y * rh, cw * colSpan, rh).reduced (2);
        };

        // Wheels: pitch | mod
        {
            auto box = wheelsSec.contentBounds();
            wheels[0]->setBounds (box.removeFromLeft (box.getWidth() / 2).reduced (2));
            wheels[1]->setBounds (box.reduced (2));
        }

        // Voice strip: priority, legato, velocity, bend
        {
            auto box = voice.contentBounds();
            const auto cell = box.getWidth() / 4;
            rotaries[0]->setBounds (box.removeFromLeft (cell).reduced (2)); // notePriority
            toggles[0]->setBounds (box.removeFromLeft (cell).reduced (2));  // legato
            toggles[1]->setBounds (box.removeFromLeft (cell).reduced (2));  // velocity
            knobs[0]->setBounds (box.reduced (2));                          // bendRange
        }

        // Controllers: 3 knobs + 3 toggles
        {
            auto box = ctrl.contentBounds();
            knobs[1]->setBounds (grid (box, 3, 2, 0)); // tune
            knobs[2]->setBounds (grid (box, 3, 2, 1)); // glide
            knobs[3]->setBounds (grid (box, 3, 2, 2)); // modMix
            toggles[2]->setBounds (grid (box, 3, 2, 3));
            toggles[3]->setBounds (grid (box, 3, 2, 4));
            toggles[4]->setBounds (grid (box, 3, 2, 5));
        }

        // Oscillator bank: 3 rows × range, freq, wave (osc 1 has no freq — range spans two cells)
        {
            auto box = osc.contentBounds();
            // rotaries: [0]=priority already used. osc starts at rotaries[1]
            // osc1Range, osc1Wave, osc2Range, osc2Wave, osc3Range, osc3Wave  => rotaries 1..6
            // osc2Freq, osc3Freq => knobs 4, 5  (0=bend, 1=tune, 2=glide, 3=modMix)
            const auto rowH = box.getHeight() / 3;
            auto row1 = box.removeFromTop (rowH);
            rotaries[1]->setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (2)); // osc1Range
            rotaries[2]->setBounds (row1.reduced (2)); // osc1Wave
            auto row2 = box.removeFromTop (rowH);
            const auto c2 = row2.getWidth() / 3;
            rotaries[3]->setBounds (row2.removeFromLeft (c2).reduced (2));
            knobs[4]->setBounds (row2.removeFromLeft (c2).reduced (2));
            rotaries[4]->setBounds (row2.reduced (2));
            auto row3 = box;
            const auto c3 = row3.getWidth() / 3;
            rotaries[5]->setBounds (row3.removeFromLeft (c3).reduced (2));
            knobs[5]->setBounds (row3.removeFromLeft (c3).reduced (2));
            rotaries[6]->setBounds (row3.reduced (2));
        }

        // Mixer: 5 rows of knob+toggle, noise type at the bottom
        {
            auto box = mix.contentBounds();
            const auto rowH = box.getHeight() / 6;
            const auto row = [&] (int i, ui::Knob& k, ui::BatToggle& t)
            {
                auto r = juce::Rectangle<int> (box.getX(), box.getY() + i * rowH, box.getWidth(), rowH);
                t.setBounds (r.removeFromLeft (juce::jmin (48, r.getWidth() / 3)).reduced (1));
                k.setBounds (r.reduced (1));
            };
            // knobs 6..10 mixOsc1,2,3,ext,noise; toggles 5..9 corresponding ons
            row (0, *knobs[6], *toggles[5]);
            row (1, *knobs[7], *toggles[6]);
            row (2, *knobs[8], *toggles[7]);
            row (3, *knobs[9], *toggles[8]);
            row (4, *knobs[10], *toggles[9]);
            rotaries[7]->setBounds (juce::Rectangle<int> (box.getX(), box.getY() + 5 * rowH, box.getWidth(), rowH).reduced (1));
        }

        // Modifiers: 3+3 knobs, 2 kybd, 3 loudness, decay
        {
            auto box = mod.contentBounds();
            const auto rowH = box.getHeight() / 4;
            auto r0 = box.removeFromTop (rowH);
            const auto w0 = r0.getWidth() / 3;
            knobs[11]->setBounds (r0.removeFromLeft (w0).reduced (2)); // cutoff
            knobs[12]->setBounds (r0.removeFromLeft (w0).reduced (2)); // emphasis
            knobs[13]->setBounds (r0.reduced (2));                     // contour
            auto r1 = box.removeFromTop (rowH);
            const auto w1 = r1.getWidth() / 3;
            knobs[14]->setBounds (r1.removeFromLeft (w1).reduced (2));
            knobs[15]->setBounds (r1.removeFromLeft (w1).reduced (2));
            knobs[16]->setBounds (r1.reduced (2));
            auto r2 = box.removeFromTop (rowH);
            toggles[10]->setBounds (r2.removeFromLeft (r2.getWidth() / 4).reduced (2));
            toggles[11]->setBounds (r2.removeFromLeft (r2.getWidth() / 3).reduced (2));
            toggles[12]->setBounds (r2.reduced (2)); // decay
            auto r3 = box;
            const auto w3 = r3.getWidth() / 3;
            knobs[17]->setBounds (r3.removeFromLeft (w3).reduced (2));
            knobs[18]->setBounds (r3.removeFromLeft (w3).reduced (2));
            knobs[19]->setBounds (r3.reduced (2));
        }

        // Output: volume + a440 on top, scope and filter stacked
        {
            auto box = out.contentBounds();
            auto top = box.removeFromTop (juce::jlimit (72, 110, box.getHeight() / 3));
            knobs[20]->setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (2));
            toggles[13]->setBounds (top.reduced (2));
            auto half = box.getHeight() / 2;
            scope.setBounds (box.removeFromTop (half).reduced (2));
            filterReadout.setBounds (box.reduced (2));
        }
    }
}
