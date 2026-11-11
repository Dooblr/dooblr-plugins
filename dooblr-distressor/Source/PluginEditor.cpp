#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblrdistressor
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
        constexpr int headerHeight = 60, margin = 16, panelGap = 10;

        void forEachComponent (juce::Component& root, const std::function<void (juce::Component&)>& fn)
        {
            fn (root);
            for (auto* child : root.getChildren())
                if (child != nullptr)
                    forEachComponent (*child, fn);
        }
    }

    DooblrDistressorEditor::DooblrDistressorEditor (DooblrDistressorProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          compressionDisplay (p.getState())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");

        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &ratioCaption, &detectorCaption, &audioCaption,
                             &headroomCaption, &mixCaption, &matchCaption })
            addAndMakeVisible (label);

        styleLabel (ratioCaption, "RATIO", 11.0f, Palette::dynamic, juce::Justification::centredLeft, true);
        styleLabel (detectorCaption, "DETECTOR", 11.0f, Palette::spectrum, juce::Justification::centredLeft, true);
        styleLabel (audioCaption, "AUDIO", 11.0f, Palette::accent, juce::Justification::centredLeft, true);
        styleLabel (headroomCaption, "HEADROOM", 11.0f, Palette::textMuted, juce::Justification::centred, true);
        styleLabel (mixCaption, "MIX", 12.0f, Palette::textMuted, juce::Justification::centred);

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
        saveButton.setTooltip ("Save a user preset (.ddistressor)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset");
        loadButton.onClick = [this] { loadPreset(); };

        bypassButton.setComponentID (id::bypass);
        bypassButton.setTooltip (state.getParameter (id::bypass)->getName (64));
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &bypassButton })
            addAndMakeVisible (c);

        ratioStrip = std::make_unique<ui::SegmentedToggle> (state, id::ratio, params::ratioNames(), Palette::dynamic);
        distToggle = std::make_unique<ui::SegmentedToggle> (state, id::distMode, params::distModeNames(), Palette::accent);
        addAndMakeVisible (*ratioStrip);
        addAndMakeVisible (*distToggle);
        addAndMakeVisible (grMeter);
        addAndMakeVisible (distLeds);
        addAndMakeVisible (compressionDisplay);

        addToggle (id::detHp, "HP");
        addToggle (id::detBand, "Band");
        addToggle (id::stereoLink, "Link");
        addToggle (id::audioHp, "HP");

        addKnob (id::input, "INPUT", Palette::accent);
        addKnob (id::attack, "ATTACK", Palette::dynamic);
        addKnob (id::release, "RELEASE", Palette::dynamic);
        addKnob (id::output, "OUTPUT", Palette::accent);

        styleLabel (matchCaption, "GAIN MATCH", 12.0f, Palette::textMuted, juce::Justification::centred);
        matchButton.setComponentID (id::gainMatch);
        matchButton.setTooltip (state.getParameter (id::gainMatch)->getName (64));
        matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::gainMatch, matchButton);
        addAndMakeVisible (matchButton);
        styleLabel (matchReadout, {}, 11.0f, Palette::textMuted, juce::Justification::centred);
        matchReadout.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (matchReadout);

        headroomBox.setComponentID (id::headroom);
        headroomBox.setTooltip (state.getParameter (id::headroom)->getName (64));
        headroomBox.addItemList (params::headroomNames(), 1);
        headroomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id::headroom, headroomBox);
        addAndMakeVisible (headroomBox);

        mixSlider.setComponentID (id::mix);
        mixSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        mixSlider.setColour (juce::Slider::rotarySliderFillColourId, Palette::master);
        mixSlider.setTooltip (state.getParameter (id::mix)->getName (64));
        mixSlider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        mixSlider.setDoubleClickReturnValue (true, params::mixDefault);
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::mix, mixSlider);
        addAndMakeVisible (mixSlider);

        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);

        proc.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
        {
            const auto m = proc.takeMeters();
            grMeter.setGainReductionDb (m.gainReductionDb);
            distLeds.setState (m.led1pct, m.led3pct);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            compressionDisplay.pushMeters (m.gainReductionDb, m.inputPeakL, m.inputPeakR,
                                           m.outputPeakL, m.outputPeakR, m.matchDb, now);
            inMeter.pushPeaks (m.inputPeakL, m.inputPeakR, now);
            outMeter.pushPeaks (m.outputPeakL, m.outputPeakR, now);
            const auto matchOn = proc.readSettings().gainMatch;
            if (matchOn)
            {
                const auto text = params::formatValue ((double) m.matchDb, 1) + " dB";
                matchReadout.setText (m.matchDb > 0.05f ? "+" + text : text, juce::dontSendNotification);
            }
            else
            {
                matchReadout.setText ({}, juce::dontSendNotification);
            }
        });

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (defaultWidth, defaultHeight);
        startTimerHz (15);
    }

    DooblrDistressorEditor::~DooblrDistressorEditor()
    {
        stopTimer();
        vblank.reset();
        proc.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    DooblrDistressorEditor::Knob& DooblrDistressorEditor::addKnob (const juce::String& parameterId, const juce::String& caption, juce::Colour colour)
    {
        auto knob = std::make_unique<Knob>();
        auto* parameter = state.getParameter (parameterId);
        styleLabel (knob->caption, caption, 12.0f, Palette::textMuted, juce::Justification::centred);
        knob->slider.setComponentID (parameterId);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        knob->slider.setTooltip (parameter != nullptr ? parameter->getName (64) : caption);
        knob->slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            knob->slider.setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, knob->slider);
        addAndMakeVisible (knob->caption);
        addAndMakeVisible (knob->slider);
        knobs.push_back (std::move (knob));
        return *knobs.back();
    }

    void DooblrDistressorEditor::addToggle (const juce::String& parameterId, const juce::String& label)
    {
        auto button = std::make_unique<juce::ToggleButton> (label);
        button->setComponentID (parameterId);
        button->setTooltip (state.getParameter (parameterId) != nullptr ? state.getParameter (parameterId)->getName (64) : label);
        toggleAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *button));
        addAndMakeVisible (*button);
        toggles.push_back (std::move (button));
    }

    juce::Component* DooblrDistressorEditor::findControl (const juce::String& parameterId) const
    {
        juce::Component* found = nullptr;
        forEachComponent (const_cast<DooblrDistressorEditor&> (*this), [&] (juce::Component& c)
        {
            if (c.getComponentID() == parameterId && found == nullptr)
                found = &c;
        });
        return found;
    }

    void DooblrDistressorEditor::timerCallback()
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

    void DooblrDistressorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrDistressorEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void DooblrDistressorEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrDistressorEditor::refreshPresetBox()
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

    void DooblrDistressorEditor::presetChosen()
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

    void DooblrDistressorEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-distressor preset",
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

    void DooblrDistressorEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-distressor preset",
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

    bool DooblrDistressorEditor::keyPressed (const juce::KeyPress& key)
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

    void DooblrDistressorEditor::paint (juce::Graphics& g)
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
        fillPanel (ratioArea);
        fillPanel (modeArea);
        fillPanel (knobArea);
        fillPanel (bottomArea);
    }

    void DooblrDistressorEditor::resized()
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

            bypassButton.setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 24));
            header.removeFromRight (8);
            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (8);
            const auto presetWidth = juce::jlimit (120, 280, header.getWidth());
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        contentArea = bounds.reduced (margin, 12);
        auto area = contentArea;

        const auto ratioH = juce::jlimit (64, 88, area.getHeight() / 8);
        ratioArea = area.removeFromTop (ratioH);
        area.removeFromTop (panelGap);
        {
            auto row = ratioArea.reduced (12, 8);
            ratioCaption.setBounds (row.removeFromTop (16));
            const auto meterW = juce::jlimit (180, 280, row.getWidth() / 3);
            grMeter.setBounds (row.removeFromRight (meterW));
            row.removeFromRight (10);
            ratioStrip->setBounds (row);
        }

        const auto modeH = juce::jlimit (60, 84, area.getHeight() / 8);
        modeArea = area.removeFromTop (modeH);
        area.removeFromTop (panelGap);
        {
            auto row = modeArea.reduced (12, 8);
            const auto distW = juce::jlimit (90, 120, row.getWidth() / 7);
            distLeds.setBounds (row.removeFromRight (distW));
            row.removeFromRight (10);

            auto audio = row.removeFromRight (juce::jlimit (220, 340, row.getWidth() / 2));
            row.removeFromRight (10);
            auto detector = row;

            detectorCaption.setBounds (detector.removeFromTop (16));
            // det HP, Band, Link
            const auto tw = detector.getWidth() / 3;
            for (int i = 0; i < 3 && i < (int) toggles.size(); ++i)
            {
                auto cell = detector.removeFromLeft (tw);
                toggles[(size_t) i]->setBounds (cell.reduced (2, 2).withSizeKeepingCentre (cell.getWidth() - 4, 26));
            }

            audioCaption.setBounds (audio.removeFromTop (16));
            const auto hpW = juce::jlimit (72, 100, audio.getWidth() / 4);
            if (toggles.size() > 3)
                toggles[3]->setBounds (audio.removeFromLeft (hpW).withSizeKeepingCentre (hpW, 26));
            audio.removeFromLeft (6);
            distToggle->setBounds (audio.withSizeKeepingCentre (audio.getWidth(), juce::jmin (32, audio.getHeight())));
        }

        const auto bottomH = juce::jlimit (100, 148, area.getHeight() / 5);
        bottomArea = area.removeFromBottom (bottomH);
        area.removeFromBottom (panelGap);

        const auto displayH = juce::jlimit (96, 220, area.getHeight() * 11 / 20);
        displayArea = area.removeFromTop (displayH);
        area.removeFromTop (panelGap);
        compressionDisplay.setBounds (displayArea);
        knobArea = area;
        {
            auto row = knobArea.reduced (8, 6);
            const auto n = (int) knobs.size();
            const auto columns = n + 1; // Gain Match sits with Input / Output
            const auto cellW = columns > 0 ? row.getWidth() / columns : row.getWidth();
            for (int i = 0; i < n; ++i)
            {
                auto cell = row.removeFromLeft (cellW).reduced (6, 0);
                knobs[(size_t) i]->caption.setBounds (cell.removeFromTop (18));
                knobs[(size_t) i]->slider.setBounds (cell);
            }
            auto matchCell = row.reduced (6, 0);
            matchCaption.setBounds (matchCell.removeFromTop (18));
            matchReadout.setBounds (matchCell.removeFromBottom (18));
            matchButton.setBounds (matchCell.withSizeKeepingCentre (juce::jmin (132, matchCell.getWidth()), 26));
        }

        {
            auto row = bottomArea.reduced (12, 8);
            statusLabel.setBounds (row.removeFromBottom (18));
            row.removeFromBottom (4);

            const auto meterW = juce::jlimit (52, 80, row.getWidth() / 10);
            outMeter.setBounds (row.removeFromRight (meterW));
            row.removeFromRight (8);
            inMeter.setBounds (row.removeFromRight (meterW));
            row.removeFromRight (16);

            auto mix = row.removeFromRight (juce::jlimit (80, 120, row.getWidth() / 5));
            mixCaption.setBounds (mix.removeFromTop (16));
            mixSlider.setBounds (mix);

            row.removeFromRight (12);
            auto hr = row.removeFromLeft (juce::jlimit (120, 180, row.getWidth() / 3));
            headroomCaption.setBounds (hr.removeFromTop (16));
            headroomBox.setBounds (hr.removeFromTop (28));
        }
    }
}
