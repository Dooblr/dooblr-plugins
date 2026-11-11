#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblrsub
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

        using Spec = ui::ControlPanel::Spec;
        using Kind = ui::ControlPanel::Kind;
    }

    DooblrSubEditor::DooblrSubEditor (DooblrSubProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          subPanel (p.getState(), "Sub", Palette::accent, {
              { id::subAmount, "Sub", Kind::knob },
              { id::loAmount, "Lo", Kind::knob },
              { id::frequency, "Freq", Kind::knob },
              { id::octaveMode, "Octave", Kind::combo, 118 },
              { id::trackingSpeed, "Track", Kind::knob },
              { id::drive, "Drive", Kind::knob },
              { id::mix, "Mix", Kind::knob },
          }),
          tonePanel (p.getState(), "Tone Shift", Palette::dynamic, {
              { id::shift, "Shift", Kind::knob },
              { id::transient, "Transient", Kind::knob },
              { id::bodyMix, "Body", Kind::knob },
              { id::shiftMix, "Shift Mix", Kind::knob },
          }),
          systemPanel (p.getState(), "System Check", Palette::spectrum, {
              { id::target, "Target", Kind::combo, 140 },
              { id::monoBelow, "Mono", Kind::toggle },
              { id::monoFreq, "Mono Hz", Kind::knob },
              { id::infraHpf, "Infra", Kind::toggle },
              { id::infraFreq, "Infra Hz", Kind::knob },
              { id::subLimit, "Limit", Kind::toggle },
          }),
          meter (&p.getEngine().getPeakTap())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);

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
        saveButton.setTooltip ("Save a user preset (.dsub)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset");
        loadButton.onClick = [this] { loadPreset(); };

        bypassButton.setComponentID (id::bypass);
        bypassButton.setTooltip (state.getParameter (id::bypass)->getName (64));
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::bypass, bypassButton);

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setTooltip (state.getParameter (id::output)->getName (64));
        outputSlider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (id::output)))
            outputSlider.setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                    (juce::Component*) &presetBox, (juce::Component*) &saveButton,
                                    (juce::Component*) &loadButton, (juce::Component*) &bypassButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &meter })
            addAndMakeVisible (c);

        addAndMakeVisible (subPanel);
        addAndMakeVisible (tonePanel);
        addAndMakeVisible (systemPanel);
        addAndMakeVisible (display);
        addAndMakeVisible (verdict);

        proc.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
        {
            const auto snap = proc.getEngine().display();
            display.setFrame (snap.spectrum, snap.bands, snap.trackedHz);
            verdict.setState (snap.verdict, snap.reasons, snap.trackedHz,
                              proc.getLatencySamples(), proc.getSampleRate());
        });

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (defaultWidth, defaultHeight);
        startTimerHz (15);
    }

    DooblrSubEditor::~DooblrSubEditor()
    {
        stopTimer();
        vblank.reset();
        proc.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    juce::Component* DooblrSubEditor::findControl (const juce::String& parameterId) const
    {
        juce::Component* found = nullptr;
        forEachComponent (const_cast<DooblrSubEditor&> (*this), [&] (juce::Component& c)
        {
            if (c.getComponentID() == parameterId && found == nullptr)
                found = &c;
        });
        return found;
    }

    void DooblrSubEditor::timerCallback()
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

    void DooblrSubEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrSubEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void DooblrSubEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrSubEditor::refreshPresetBox()
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

    void DooblrSubEditor::presetChosen()
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

    void DooblrSubEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-sub preset",
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

    void DooblrSubEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-sub preset",
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

    bool DooblrSubEditor::keyPressed (const juce::KeyPress& key)
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

    void DooblrSubEditor::paint (juce::Graphics& g)
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
        fillPanel (subArea);
        fillPanel (toneArea);
        fillPanel (systemArea);
    }

    void DooblrSubEditor::resized()
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

            meter.setBounds (header.removeFromRight (20));
            header.removeFromRight (10);
            outputSlider.setBounds (header.removeFromRight (juce::jlimit (88, 160, header.getWidth() / 5))
                                          .withSizeKeepingCentre (juce::jlimit (88, 160, header.getWidth()), 24));
            outputCaption.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 24));
            header.removeFromRight (8);
            bypassButton.setBounds (header.removeFromRight (88).withSizeKeepingCentre (88, 24));
            header.removeFromRight (8);
            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (8);
            const auto presetWidth = juce::jlimit (110, 220, header.getWidth());
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        auto body = bounds.reduced (margin, 12);
        const auto displayH = juce::jlimit (150, 240, body.getHeight() / 3);
        auto top = body.removeFromTop (displayH);
        body.removeFromTop (panelGap);
        const auto verdictW = juce::jlimit (220, 340, top.getWidth() / 3);
        verdictArea = top.removeFromRight (verdictW);
        top.removeFromRight (panelGap);
        displayArea = top;
        display.setBounds (displayArea);
        verdict.setBounds (verdictArea);

        const auto colW = (body.getWidth() - 2 * panelGap) / 3;
        subArea = body.removeFromLeft (colW);
        body.removeFromLeft (panelGap);
        toneArea = body.removeFromLeft (colW);
        body.removeFromLeft (panelGap);
        systemArea = body;

        subPanel.setBounds (subArea.reduced (10, 8));
        tonePanel.setBounds (toneArea.reduced (10, 8));
        systemPanel.setBounds (systemArea.reduced (10, 8));

        statusLabel.setBounds (getLocalBounds().removeFromBottom (16).reduced (24, 0));
    }
}
