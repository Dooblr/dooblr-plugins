#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <cmath>
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
            label.setBorderSize ({ 0, 1, 0, 1 });
            label.setMinimumHorizontalScale (1.0f);
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
            const auto lfoOn = valueOf (state, id::osc3Lfo) >= 0.5f;
            if ((valueOf (state, id::mixOsc3On) < 0.5f && valueOf (state, id::oscMod) < 0.5f
                 && valueOf (state, id::filterMod) < 0.5f) || lfoOn)
            {
                ids.add (id::mixOsc3);
            }
            if (lfoOn)
            {
                ids.add (id::mixOsc3On);
                ids.add (id::osc3Range);
                ids.add (id::osc3Freq);
                ids.add (id::osc3Control);
            }
            else
            {
                ids.add (id::lfoRate);
                ids.add (id::lfoSync);
                ids.add (id::lfoDivision);
                ids.add (id::lfoRetrigger);
            }
            if (lfoOn && valueOf (state, id::lfoSync) >= 0.5f)
                ids.add (id::lfoRate);
            if (lfoOn && valueOf (state, id::lfoSync) < 0.5f)
                ids.add (id::lfoDivision);
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
          scope (&p.getEngine().getScopeTap()),
          filterReadout (p.getState(), p.getEngine().getFilterEnvTap(), p.getEngine().getFilterEnvStageTap())
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
                                      juce::Colour fill, bool large)
    {
        knobs.push_back (std::make_unique<ui::Knob> (state, pid, caption, fill, large));
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

    ui::BatToggle& DooblrMEditor::addToggle (juce::Component& parent, const juce::String& pid, const juce::String& caption,
                                             const juce::String& downLabel, const juce::String& upLabel)
    {
        toggles.push_back (std::make_unique<ui::BatToggle> (state, pid, caption, downLabel, upLabel));
        parent.addAndMakeVisible (*toggles.back());
        return *toggles.back();
    }

    void DooblrMEditor::addChoiceControl (juce::Component& parent, const juce::String& pid, const juce::String& caption,
                                          const juce::StringArray& ticks)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid));
        if (choice != nullptr && choice->choices.size() == 2)
        {
            const auto down = ticks.size() > 0 ? ticks[0] : choice->choices[0];
            const auto up = ticks.size() > 1 ? ticks[1] : choice->choices[1];
            addToggle (parent, pid, caption, down, up);
            return;
        }
        addSwitch (parent, pid, caption, ticks);
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
        addChoiceControl (voice, id::notePriority, "Priority", juce::StringArray { "Low", "Last" });
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
        addToggle (ctrl, id::osc3Lfo, "LFO");
        addToggle (ctrl, id::lfoRetrigger, "Retrig");

        auto& osc = addSection ("OSCILLATOR BANK", ui::bandColour (0));
        addSwitch (osc, id::osc1Range, "Osc 1 Range", params::rangeNames());
        addSwitch (osc, id::osc1Wave, "Osc 1 Wave", juce::StringArray { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" });
        addSwitch (osc, id::osc2Range, "Osc 2 Range", params::rangeNames());
        addKnob (osc, id::osc2Freq, "Osc 2 Freq", ui::bandColour (1));
        addSwitch (osc, id::osc2Wave, "Osc 2 Wave", juce::StringArray { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" });
        addSwitch (osc, id::osc3Range, "Osc 3 Range", params::rangeNames());
        addKnob (osc, id::osc3Freq, "Osc 3 Freq", ui::bandColour (2));
        addSwitch (osc, id::osc3Wave, "Osc 3 Wave", params::osc3WaveNames());
        auto& rate = addKnob (osc, id::lfoRate, "Rate", Palette::dynamic);
        lfoPulse.attach (state, synth.getEngine().getLfoTap());
        rate.addChildComponent (lfoPulse);
        addToggle (osc, id::lfoSync, "Sync");
        addSwitch (osc, id::lfoDivision, "Div", params::lfoDivisionNames());

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
        addChoiceControl (mix, id::noiseType, "Colour", params::noiseNames());

        auto& mod = addSection ("MODIFIERS", Palette::dynamic);
        auto& cutoff = addKnob (mod, id::cutoff, "Cutoff", Palette::dynamic, true);
        cutoffContour.attach (state, cutoff.getSlider(), synth.getEngine().getFilterEnvTap());
        cutoff.addChildComponent (cutoffContour);
        addKnob (mod, id::emphasis, "Resonance", Palette::dynamic);
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
        lfoPulse.refresh();
        scope.refresh();
        filterReadout.refresh();

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

        const auto lfoOn = valueOf (state, id::osc3Lfo) >= 0.5f;
        for (auto& r : rotaries)
            if (r->getComponentID() == id::osc3Wave)
                r->setTickTexts (lfoOn ? params::osc3LfoWaveNames() : params::osc3WaveNames());
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

        const auto voiceH = juce::jlimit (78, 90, voiceBarHeight);
        auto voiceRow = area.removeFromTop (voiceH);
        const auto voiceCell = juce::jlimit (72, 88, voiceRow.getWidth() / 8);
        voiceBarArea = voiceRow.removeFromLeft (voiceCell * 4 + 12);
        place (voice, voiceBarArea);
        area.removeFromTop (gap);

        auto remain = area;
        // Minimums keep captions and rotary ticks inside their cells at 940 px; leftover goes
        // mostly to the oscillator bank. Modifiers get a little extra so the large cutoff fits.
        const int minW[] { 70, 144, 254, 114, 204, 108 };
        const float weight[] { 0.4f, 1.2f, 3.0f, 0.8f, 1.6f, 1.0f };
        const int gaps = 5 * gap;
        const int inner = juce::jmax (0, remain.getWidth() - gaps);
        int minSum = 0;
        float weightSum = 0.0f;
        for (int i = 0; i < 6; ++i) { minSum += minW[i]; weightSum += weight[i]; }
        const int extra = juce::jmax (0, inner - minSum);
        int widths[6] {};
        int assignedExtra = 0;
        for (int i = 0; i < 5; ++i)
        {
            const auto add = (int) std::round ((float) extra * weight[i] / weightSum);
            widths[i] = minW[i] + add;
            assignedExtra += add;
        }
        widths[5] = minW[5] + (extra - assignedExtra);

        juce::Component* cols[] { &wheelsSec, &ctrl, &osc, &mix, &mod, &out };
        for (int i = 0; i < 6; ++i)
        {
            place (*cols[i], remain.removeFromLeft (widths[i]));
            if (i < 5)
                remain.removeFromLeft (gap);
        }

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

        const auto placeId = [this] (const juce::String& pid, juce::Rectangle<int> r)
        {
            if (auto* c = findControl (pid))
                c->setBounds (r);
        };

        // Voice strip: the panel is only as wide as the four extras (not a full-width empty bar).
        {
            auto box = voice.contentBounds();
            const auto cell = box.getWidth() / 4;
            placeId (id::notePriority, box.removeFromLeft (cell).reduced (2));
            placeId (id::legato, box.removeFromLeft (cell).reduced (2));
            placeId (id::velocityOn, box.removeFromLeft (cell).reduced (2));
            placeId (id::bendRange, box.reduced (2));
        }

        // Controllers: knobs in the left column, switches in the right (2×4) so "Mod Mix"
        // and "Osc 3 Ctrl" have a cell wider than three-across. LFO / Retrig sit under Filt Mod.
        {
            auto box = ctrl.contentBounds();
            placeId (id::tune, grid (box, 2, 4, 0));
            placeId (id::osc3Control, grid (box, 2, 4, 1));
            placeId (id::glide, grid (box, 2, 4, 2));
            placeId (id::oscMod, grid (box, 2, 4, 3));
            placeId (id::modMix, grid (box, 2, 4, 4));
            placeId (id::filterMod, grid (box, 2, 4, 5));
            placeId (id::osc3Lfo, grid (box, 2, 4, 6));
            placeId (id::lfoRetrigger, grid (box, 2, 4, 7));
        }

        // Oscillator bank: Osc 1 / 2 keep a row each; Osc 3 is double-height so Range/Freq/Wave
        // stay three-across at 940 px and Rate / Sync / Div sit on the row below.
        {
            auto box = osc.contentBounds();
            const auto unit = box.getHeight() / 4;
            auto row1 = box.removeFromTop (unit);
            placeId (id::osc1Range, row1.removeFromLeft (row1.getWidth() / 2).reduced (2));
            placeId (id::osc1Wave, row1.reduced (2));
            auto row2 = box.removeFromTop (unit);
            const auto c2 = row2.getWidth() / 3;
            placeId (id::osc2Range, row2.removeFromLeft (c2).reduced (2));
            placeId (id::osc2Freq, row2.removeFromLeft (c2).reduced (2));
            placeId (id::osc2Wave, row2.reduced (2));
            auto row3 = box.removeFromTop (unit);
            const auto c3 = row3.getWidth() / 3;
            placeId (id::osc3Range, row3.removeFromLeft (c3).reduced (2));
            placeId (id::osc3Freq, row3.removeFromLeft (c3).reduced (2));
            placeId (id::osc3Wave, row3.reduced (2));
            auto row4 = box;
            const auto c4 = row4.getWidth() / 3;
            placeId (id::lfoRate, row4.removeFromLeft (c4).reduced (2));
            placeId (id::lfoSync, row4.removeFromLeft (c4).reduced (2));
            placeId (id::lfoDivision, row4.reduced (2));
        }

        {
            const auto lfoOn = valueOf (state, id::osc3Lfo) >= 0.5f;
            for (auto& r : rotaries)
                if (r->getComponentID() == id::osc3Wave)
                    r->setTickTexts (lfoOn ? params::osc3LfoWaveNames() : params::osc3WaveNames());
        }

        // Mixer: 5 rows of knob+toggle, noise colour rocker at the bottom
        {
            auto box = mix.contentBounds();
            const auto rowH = box.getHeight() / 6;
            const auto row = [&] (int i, const char* knobId, const char* toggleId)
            {
                auto r = juce::Rectangle<int> (box.getX(), box.getY() + i * rowH, box.getWidth(), rowH);
                placeId (toggleId, r.removeFromLeft (juce::jmin (48, r.getWidth() / 3)).reduced (1));
                placeId (knobId, r.reduced (1));
            };
            row (0, id::mixOsc1, id::mixOsc1On);
            row (1, id::mixOsc2, id::mixOsc2On);
            row (2, id::mixOsc3, id::mixOsc3On);
            row (3, id::mixExt, id::mixExtOn);
            row (4, id::mixNoise, id::mixNoiseOn);
            placeId (id::noiseType, juce::Rectangle<int> (box.getX(), box.getY() + 5 * rowH, box.getWidth(), rowH).reduced (1));
        }

        // Modifiers: taller first row so Cutoff is ~1.6–2× the other knobs; resonance + contour
        // stack on its right. Remaining rows stay three-across.
        {
            auto box = mod.contentBounds();
            const auto topH = juce::jmax (box.getHeight() * 38 / 100, 92);
            auto r0 = box.removeFromTop (topH);
            const auto cutW = r0.getWidth() * 3 / 5;
            placeId (id::cutoff, r0.removeFromLeft (cutW).reduced (2));
            auto side = r0;
            const auto sideH = side.getHeight() / 2;
            placeId (id::emphasis, side.removeFromTop (sideH).reduced (2));
            placeId (id::contourAmt, side.reduced (2));
            auto r1 = box.removeFromTop (box.getHeight() / 3);
            const auto w1 = r1.getWidth() / 3;
            placeId (id::fAttack, r1.removeFromLeft (w1).reduced (2));
            placeId (id::fDecay, r1.removeFromLeft (w1).reduced (2));
            placeId (id::fSustain, r1.reduced (2));
            auto r2 = box.removeFromTop (box.getHeight() / 2);
            placeId (id::kybd1, r2.removeFromLeft (r2.getWidth() / 3).reduced (2));
            placeId (id::kybd2, r2.removeFromLeft (r2.getWidth() / 2).reduced (2));
            placeId (id::decaySwitch, r2.reduced (2));
            auto r3 = box;
            const auto w3 = r3.getWidth() / 3;
            placeId (id::aAttack, r3.removeFromLeft (w3).reduced (2));
            placeId (id::aDecay, r3.removeFromLeft (w3).reduced (2));
            placeId (id::aSustain, r3.reduced (2));
        }

        // Output: volume full width, then scope and the interactive filter envelope stacked
        {
            auto box = out.contentBounds();
            auto top = box.removeFromTop (juce::jlimit (64, 96, box.getHeight() / 4));
            placeId (id::volume, top.reduced (2));
            const auto envH = juce::jmax (box.getHeight() / 2, 56);
            filterReadout.setBounds (box.removeFromBottom (envH).reduced (2));
            scope.setBounds (box.reduced (2));
        }
    }
}
