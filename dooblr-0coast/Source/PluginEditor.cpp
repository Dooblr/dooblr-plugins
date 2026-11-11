#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblr0coast
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
        constexpr int headerHeight = 60, margin = 10, panelGap = 6;

        void forEachComponent (juce::Component& root, const std::function<void (juce::Component&)>& fn)
        {
            fn (root);
            for (auto* child : root.getChildren())
                if (child != nullptr)
                    forEachComponent (*child, fn);
        }

        juce::Rectangle<int> take (juce::Rectangle<int>& col, int h, int gap = 3)
        {
            auto r = col.removeFromTop (h);
            col.removeFromTop (gap);
            return r;
        }
    }

    Dooblr0CoastEditor::Dooblr0CoastEditor (Dooblr0CoastProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        styleLabel (outputCaption, "Output", 12.0f, Palette::textMuted, juce::Justification::centredRight);

        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &outputCaption })
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
        saveButton.setTooltip ("Save a user preset (.dcoast)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputSlider.setTooltip (state.getParameter (id::output)->getName (64));
        outputSlider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        outputSlider.setDoubleClickReturnValue (true, 0.0);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &outputSlider,
                                    (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        const auto osc = Palette::accent;
        const auto tim = juce::Colour (ui::bandColour (0));
        const auto slp = Palette::dynamic;
        const auto ctr = ui::bandColour (5);
        const auto dyn = Palette::spectrum;
        const auto ctl = Palette::master;

        addToggle (id::arpOn, "Arp");
        addCombo (id::arpType, "Type", params::arpTypeNames());
        addCombo (id::clockDiv, "Clock", params::clockDivNames());
        addToggle (id::legato, "Legato");
        addCombo (id::midiBCv, "B CV", params::midiBCvNames());
        addCombo (id::midiBGate, "B Gate", params::midiBGateNames());
        addKnob (id::midiBLfoRate, "B LFO", ctl);
        addKnob (id::mathAmt, "Math", ctl);
        addKnob (id::bendRange, "Bend", ctl);

        addKnob (id::pitch, "Pitch", osc);
        addKnob (id::fine, "Fine", osc);
        addKnob (id::portamento, "Porta", osc);
        addKnob (id::linFm, "Lin FM", osc);

        addKnob (id::overtone, "Overtone", tim);
        addKnob (id::overtoneCv, "OV CV", tim);
        addKnob (id::multiply, "Multiply", tim);
        addKnob (id::multiplyCv, "ML CV", tim);

        addToggle (id::slopeCycle, "Cycle");
        addKnob (id::slopeRise, "Rise", slp);
        addKnob (id::slopeFall, "Fall", slp);
        addKnob (id::slopeResponse, "Vari", slp);

        addKnob (id::contourOnset, "Onset", ctr);
        addKnob (id::contourSustain, "Sustain", ctr);
        addKnob (id::contourDecay, "Decay", ctr);
        addKnob (id::contourResponse, "Vari", ctr);

        addKnob (id::balance, "Balance", dyn);
        addKnob (id::balanceCv, "Bal CV", dyn);
        addKnob (id::dynamics, "Dynamics", dyn);
        addKnob (id::level, "Level", dyn);

        auto addOut = [this] (dsp::OutJack j, juce::Colour c)
        {
            addJack (true, (int) j, c, dsp::outJackName (j));
        };
        auto addIn = [this] (dsp::InJack j, juce::Colour c)
        {
            addJack (false, (int) j, c, dsp::inJackName (j));
        };

        addOut (dsp::OutJack::MidiACv, ctl); addOut (dsp::OutJack::MidiAGate, ctl);
        addOut (dsp::OutJack::MidiBCv, ctl); addOut (dsp::OutJack::MidiBGate, ctl);
        addOut (dsp::OutJack::Clock, ctl); addOut (dsp::OutJack::Random, ctl);
        addOut (dsp::OutJack::Math1, ctl); addOut (dsp::OutJack::Math2, ctl);
        addIn (dsp::InJack::Tempo, ctl); addIn (dsp::InJack::MathCh1, ctl); addIn (dsp::InJack::MathCh2, ctl);

        addOut (dsp::OutJack::OscTri, osc); addOut (dsp::OutJack::OscSqr, osc);
        addIn (dsp::InJack::Osc1VOct, osc); addIn (dsp::InJack::OscLinFm, osc);

        addIn (dsp::InJack::OvertoneCv, tim); addIn (dsp::InJack::MultiplyCv, tim);

        addOut (dsp::OutJack::SlopeEoc, slp); addOut (dsp::OutJack::SlopeCv, slp);
        addIn (dsp::InJack::SlopeTime, slp); addIn (dsp::InJack::SlopeTrig, slp);

        addOut (dsp::OutJack::ContourEon, ctr); addOut (dsp::OutJack::ContourCv, ctr);
        addIn (dsp::InJack::ContourDecay, ctr); addIn (dsp::InJack::ContourGate, ctr);

        addOut (dsp::OutJack::Dynamics, dyn);
        addIn (dsp::InJack::BalanceExt, dyn); addIn (dsp::InJack::BalanceCv, dyn); addIn (dsp::InJack::DynamicsCv, dyn);

        addAndMakeVisible (scope);
        scope.setComponentID ("scope");
        addAndMakeVisible (cableLayer);
        cableLayer.onRemove = [this] (dsp::OutJack, dsp::InJack to)
        {
            proc.getEngine().disconnectInput (to);
            refreshCables();
            proc.cablesChanged.sendChangeMessage();
        };

        proc.presetChanged.addChangeListener (this);
        proc.cablesChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto peaks = proc.getPeakTap().take();
            outputMeter.pushPeaks (peaks.first, peaks.second, now);
            activity = proc.getEngine().takeActivity();
            float buf[dsp::SynthEngine::scopeSize];
            proc.getEngine().copyScope (buf, dsp::SynthEngine::scopeSize);
            scope.setSamples (buf, dsp::SynthEngine::scopeSize);
            repaint (contentArea);
        });

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (defaultWidth, defaultHeight);
        startTimerHz (15);
    }

    Dooblr0CoastEditor::~Dooblr0CoastEditor()
    {
        stopTimer();
        vblank.reset();
        proc.presetChanged.removeChangeListener (this);
        proc.cablesChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    Dooblr0CoastEditor::Knob& Dooblr0CoastEditor::addKnob (const juce::String& parameterId, const juce::String& caption, juce::Colour colour)
    {
        auto knob = std::make_unique<Knob>();
        auto* parameter = state.getParameter (parameterId);
        styleLabel (knob->caption, caption, 12.0f, Palette::textMuted, juce::Justification::centred);
        knob->slider.setComponentID (parameterId);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        knob->slider.getProperties().set ("maxKnobDiameter", 46.0);
        knob->slider.setTooltip (parameter != nullptr ? parameter->getName (64) : caption);
        knob->slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            knob->slider.setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        if (parameter != nullptr && parameter->getLabel() == "oct")
            knob->slider.setMouseDragSensitivity (720);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, knob->slider);
        addAndMakeVisible (knob->caption);
        addAndMakeVisible (knob->slider);
        knobs.push_back (std::move (knob));
        return *knobs.back();
    }

    void Dooblr0CoastEditor::addToggle (const juce::String& parameterId, const juce::String& label)
    {
        auto button = std::make_unique<juce::ToggleButton> (label);
        button->setComponentID (parameterId);
        button->setTooltip (state.getParameter (parameterId) != nullptr ? state.getParameter (parameterId)->getName (64) : label);
        toggleAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *button));
        addAndMakeVisible (*button);
        toggles.push_back (std::move (button));
    }

    Dooblr0CoastEditor::Combo& Dooblr0CoastEditor::addCombo (const juce::String& parameterId, const juce::String& caption,
                                                             const juce::StringArray& items)
    {
        auto combo = std::make_unique<Combo>();
        styleLabel (combo->caption, caption, 11.0f, Palette::textMuted, juce::Justification::centredLeft);
        combo->box.setComponentID (parameterId);
        combo->box.addItemList (items, 1);
        combo->box.setTooltip (state.getParameter (parameterId)->getName (64));
        combo->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterId, combo->box);
        addAndMakeVisible (combo->caption);
        addAndMakeVisible (combo->box);
        combos.push_back (std::move (combo));
        return *combos.back();
    }

    ui::Jack& Dooblr0CoastEditor::addJack (bool output, int index, juce::Colour colour, const juce::String& label)
    {
        auto jack = std::make_unique<ui::Jack> (output, index, colour, label);
        jack->onDragFrom = [this] (ui::Jack& j) { beginCable (j); };
        jack->onDragMove = [this] (ui::Jack& j, juce::Point<int> s) { dragCable (j, s); };
        jack->onDragEnd = [this] (ui::Jack& j, juce::Point<int> s) { endCable (j, s); };
        addAndMakeVisible (*jack);
        jacks.push_back (std::move (jack));
        return *jacks.back();
    }

    void Dooblr0CoastEditor::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
    {
        knob.caption.setBounds (cell.removeFromTop (14));
        knob.slider.setBounds (cell);
    }

    juce::Component* Dooblr0CoastEditor::findControl (const juce::String& parameterId) const
    {
        juce::Component* found = nullptr;
        forEachComponent (const_cast<Dooblr0CoastEditor&> (*this), [&] (juce::Component& c)
        {
            if (c.getComponentID() == parameterId && found == nullptr)
                found = &c;
        });
        return found;
    }

    void Dooblr0CoastEditor::beginCable (ui::Jack& from)
    {
        draggingFrom = &from;
        cableLayer.setDrag (from.centreInParent(), from.centreInParent(), from.getColour(), true);
    }

    void Dooblr0CoastEditor::dragCable (ui::Jack& from, juce::Point<int> screen)
    {
        const auto local = getLocalPoint (nullptr, screen).toFloat();
        cableLayer.setDrag (from.centreInParent(), local, from.getColour(), true);
    }

    void Dooblr0CoastEditor::endCable (ui::Jack& from, juce::Point<int> screen)
    {
        const auto local = getLocalPoint (nullptr, screen);
        for (auto& jack : jacks)
        {
            if (jack->isOutput())
                continue;
            if (jack->getBounds().expanded (4).contains (local))
            {
                proc.getEngine().connect ((dsp::OutJack) from.getIndex(), (dsp::InJack) jack->getIndex());
                proc.cablesChanged.sendChangeMessage();
                break;
            }
        }
        draggingFrom = nullptr;
        cableLayer.setDrag ({}, {}, Palette::accent, false);
        refreshCables();
    }

    void Dooblr0CoastEditor::refreshCables()
    {
        std::vector<ui::CableLayer::Drawn> drawn;
        const auto n = proc.getEngine().numCables();
        for (int i = 0; i < n; ++i)
        {
            const auto cable = proc.getEngine().cableAt (i);
            ui::Jack* a = nullptr;
            ui::Jack* b = nullptr;
            for (auto& jack : jacks)
            {
                if (jack->isOutput() && jack->getIndex() == (int) cable.from)
                    a = jack.get();
                if (! jack->isOutput() && jack->getIndex() == (int) cable.to)
                    b = jack.get();
            }
            if (a == nullptr || b == nullptr)
                continue;
            drawn.push_back ({ cable.from, cable.to, a->centreInParent(), b->centreInParent(), a->getColour() });
        }
        cableLayer.setCables (std::move (drawn));
    }

    void Dooblr0CoastEditor::timerCallback()
    {
        updateUndoButtons();
        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() > transientStatusUntil)
        {
            transientStatus.clear();
            statusLabel.setText ({}, juce::dontSendNotification);
        }
        if (proc.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void Dooblr0CoastEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &proc.presetChanged && ! presetBox.isPopupActive())
            refreshPresetBox();
        if (source == &proc.cablesChanged)
            refreshCables();
    }

    void Dooblr0CoastEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void Dooblr0CoastEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void Dooblr0CoastEditor::refreshPresetBox()
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

    void Dooblr0CoastEditor::presetChosen()
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
    }

    void Dooblr0CoastEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-0coast preset",
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

    void Dooblr0CoastEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-0coast preset",
                                                           folder.isDirectory() ? folder : juce::File(),
                                                           "*" + juce::String (presets::fileExtension));
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file != juce::File())
                                          setStatus (proc.loadUserPreset (file));
                                      refreshPresetBox();
                                  });
    }

    bool Dooblr0CoastEditor::keyPressed (const juce::KeyPress& key)
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

    void Dooblr0CoastEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        g.setColour (Palette::panel);
        g.fillRect (headerArea);
        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        const juce::Colour colColours[] {
            Palette::master, Palette::accent, ui::bandColour (0),
            Palette::dynamic, ui::bandColour (5), Palette::spectrum
        };
        const char* titles[] { "CTRL", "OSCILLATOR", "TIMBRE", "SLOPE", "CONTOUR", "DYNAMICS" };

        for (int i = 0; i < 6; ++i)
        {
            auto r = columns[(size_t) i].toFloat();
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (colColours[i]);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (titles[i],
                        juce::Rectangle<int> (columns[(size_t) i].getX() + 8, columns[(size_t) i].getY() + 4,
                                             columns[(size_t) i].getWidth() - 16, 18),
                        juce::Justification::centredLeft);
        }

        auto led = [&g] (juce::Rectangle<int> col, float lit, juce::Colour colour, int xOff)
        {
            auto p = juce::Point<float> ((float) (col.getX() + col.getWidth() - 14 - xOff), (float) (col.getY() + 12));
            g.setColour (lit > 0.15f ? colour : Palette::divider);
            g.fillEllipse (p.x, p.y, 7.0f, 7.0f);
        };
        led (columns[0], activity.midiA, juce::Colour (0xffe0574f), 22);
        led (columns[0], activity.midiB, juce::Colour (0xffe0574f), 12);
        led (columns[0], activity.tempo, juce::Colour (0xffe0574f), 2);
        led (columns[3], activity.slope, Palette::accent, 2);
        led (columns[4], activity.contour, Palette::accent, 2);
        led (columns[5], activity.dynamics, Palette::dynamic, 2);
    }

    void Dooblr0CoastEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (headerHeight);
        {
            auto header = headerArea.reduced (32, 10);
            auto titleColumn = header.removeFromLeft (juce::jlimit (168, 240, header.getWidth() / 5));
            titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
            subtitleLabel.setBounds (titleColumn);

            header.removeFromLeft (8);
            undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (4);
            redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (12);

            outputMeter.setBounds (header.removeFromRight (20).reduced (0, 2));
            header.removeFromRight (8);
            auto out = header.removeFromRight (juce::jlimit (88, 200, header.getWidth() / 4));
            outputCaption.setBounds (out.removeFromTop (14));
            outputSlider.setBounds (out);
            header.removeFromRight (8);
            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (8);
            const auto presetWidth = juce::jlimit (120, 220, header.getWidth());
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        contentArea = bounds.reduced (margin, 8);
        statusLabel.setBounds (contentArea.removeFromBottom (16));
        contentArea.removeFromBottom (4);
        cableLayer.setBounds (contentArea);

        auto area = contentArea;
        const auto gap = panelGap;
        const auto colW = (area.getWidth() - 5 * gap) / 6;
        for (int i = 0; i < 6; ++i)
        {
            columns[(size_t) i] = area.removeFromLeft (colW);
            if (i < 5)
                area.removeFromLeft (gap);
        }

        auto placeJackRow = [this] (juce::Rectangle<int> row, std::initializer_list<const char*> ids)
        {
            const auto n = (int) ids.size();
            const auto w = n > 0 ? row.getWidth() / n : row.getWidth();
            int i = 0;
            for (auto* cid : ids)
            {
                auto cell = row.removeFromLeft (w);
                if (auto* c = findControl (cid))
                    c->setBounds (cell.withSizeKeepingCentre (juce::jmin (22, cell.getWidth() - 2), 22));
                ++i;
            }
        };

        // CTRL
        {
            auto col = columns[0].reduced (6, 22);
            toggles[0]->setBounds (take (col, 20)); // arp
            {
                auto row = take (col, 32);
                combos[0]->caption.setBounds (row.removeFromTop (11));
                combos[0]->box.setBounds (row); // arp type
            }
            {
                auto row = take (col, 32);
                combos[1]->caption.setBounds (row.removeFromTop (11));
                combos[1]->box.setBounds (row); // clock
            }
            toggles[1]->setBounds (take (col, 20)); // legato
            {
                auto row = take (col, 32);
                combos[2]->caption.setBounds (row.removeFromTop (11));
                combos[2]->box.setBounds (row);
            }
            {
                auto row = take (col, 32);
                combos[3]->caption.setBounds (row.removeFromTop (11));
                combos[3]->box.setBounds (row);
            }
            layoutKnob (*knobs[0], take (col, 56)); // B LFO
            layoutKnob (*knobs[1], take (col, 56)); // Math
            layoutKnob (*knobs[2], take (col, 52)); // Bend
            placeJackRow (take (col, 24), { "out_midiACv", "out_midiAGate", "out_midiBCv", "out_midiBGate" });
            placeJackRow (take (col, 24), { "out_clock", "out_random", "out_math1", "out_math2" });
            placeJackRow (take (col, 24), { "in_tempo", "in_mathCh1", "in_mathCh2" });
        }

        // OSC
        {
            auto col = columns[1].reduced (6, 22);
            layoutKnob (*knobs[3], take (col, 86));
            layoutKnob (*knobs[4], take (col, 80));
            layoutKnob (*knobs[5], take (col, 80));
            layoutKnob (*knobs[6], take (col, 80));
            placeJackRow (take (col, 24), { "out_oscTri", "out_oscSqr" });
            placeJackRow (take (col, 24), { "in_osc1VOct", "in_oscLinFm" });
        }

        // TIMBRE
        {
            auto col = columns[2].reduced (6, 22);
            layoutKnob (*knobs[7], take (col, 86));
            layoutKnob (*knobs[8], take (col, 80));
            layoutKnob (*knobs[9], take (col, 80));
            layoutKnob (*knobs[10], take (col, 80));
            placeJackRow (take (col, 24), { "in_overtoneCv", "in_multiplyCv" });
        }

        // SLOPE
        {
            auto col = columns[3].reduced (6, 22);
            toggles[2]->setBounds (take (col, 22));
            layoutKnob (*knobs[11], take (col, 86));
            layoutKnob (*knobs[12], take (col, 80));
            layoutKnob (*knobs[13], take (col, 80));
            placeJackRow (take (col, 24), { "out_slopeEoc", "out_slopeCv" });
            placeJackRow (take (col, 24), { "in_slopeTime", "in_slopeTrig" });
        }

        // CONTOUR
        {
            auto col = columns[4].reduced (6, 22);
            layoutKnob (*knobs[14], take (col, 86));
            layoutKnob (*knobs[15], take (col, 80));
            layoutKnob (*knobs[16], take (col, 80));
            layoutKnob (*knobs[17], take (col, 80));
            placeJackRow (take (col, 24), { "out_contourEon", "out_contourCv" });
            placeJackRow (take (col, 24), { "in_contourDecay", "in_contourGate" });
        }

        // DYNAMICS
        {
            auto col = columns[5].reduced (6, 22);
            layoutKnob (*knobs[18], take (col, 80));
            layoutKnob (*knobs[19], take (col, 72));
            layoutKnob (*knobs[20], take (col, 80));
            layoutKnob (*knobs[21], take (col, 80));
            scope.setBounds (take (col, juce::jlimit (48, 80, col.getHeight() / 3)));
            placeJackRow (take (col, 24), { "out_dynamics" });
            placeJackRow (take (col, 24), { "in_balanceExt", "in_balanceCv", "in_dynamicsCv" });
        }

        refreshCables();
    }
}
