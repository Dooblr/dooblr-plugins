#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"

#include <functional>

namespace dooblrl
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

    DooblrLEditor::DooblrLEditor (DooblrLProcessor& p)
        : juce::AudioProcessorEditor (p),
          proc (p),
          state (p.getState()),
          prePanel (p.getState(), "Pre-Process", Palette::dynamic, {
              { id::threshold, "Thresh", Kind::knob },
              { id::attack, "Attack", Kind::knob },
              { id::release, "Release", Kind::knob },
              { id::knee, "Knee", Kind::knob },
          }),
          enhancePanel (p.getState(), "Enhance", Palette::accent, {
              { id::enhance, "Amount", Kind::knob },
              { id::autoComp, "Auto Comp", Kind::toggle, 110 },
          }),
          linkPanel (p.getState(), "Stereo Link", Palette::spectrum, {
              { id::transientLink, "Trans", Kind::knob },
              { id::releaseLink, "Release", Kind::knob },
          }),
          timePanel (p.getState(), "Time", Palette::master, {
              { id::lookahead, "Look", Kind::knob },
              { id::oversampling, "OS", Kind::combo, 88 },
              { id::latencyMode, "Latency", Kind::combo, 96 },
          }),
          outPanel (p.getState(), "Output", Palette::accent, {
              { id::inputGain, "Input", Kind::knob },
              { id::ceiling, "Ceiling", Kind::knob },
              { id::unityGain, "Unity", Kind::toggle, 80 },
              { id::delta, "Delta", Kind::toggle, 80 },
              { id::dither, "Dither", Kind::combo, 96 },
              { id::shaping, "Shape", Kind::combo, 96 },
              { id::lufsTarget, "Target", Kind::combo, 110 },
          }),
          grHistory (p.getState())
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        styleLabel (styleCaption, "STYLE", 11.0f, Palette::dynamic, juce::Justification::centredLeft, true);

        for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &styleCaption })
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
        saveButton.setTooltip ("Save a user preset (.dl)");
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

        styleStrip = std::make_unique<ui::SegmentedToggle> (state, id::style, params::styleShortNames(), Palette::dynamic);
        styleStrip->setTooltip ("Limiting style");
        addAndMakeVisible (*styleStrip);

        learnButton.setComponentID ("learn");
        learnButton.setTooltip ("Listen to 20 s of incoming audio, then pick limiter settings that stay loud without flattening transients. Analysis runs on a background CPU thread (no GPU).");
        learnButton.onClick = [this] { toggleLearn(); };
        learnRevertButton.setComponentID ("learnRevert");
        learnRevertButton.setTooltip ("Restore the settings from before the last Learn.");
        learnRevertButton.onClick = [this]
        {
            proc.revertLearn();
            updateLearnUi();
            updateUndoButtons();
        };
        learnBar.setComponentID ("learnProgress");
        addAndMakeVisible (learnButton);
        addAndMakeVisible (learnRevertButton);
        addAndMakeVisible (learnBar);

        addAndMakeVisible (prePanel);
        addAndMakeVisible (enhancePanel);
        addAndMakeVisible (linkPanel);
        addAndMakeVisible (timePanel);
        addAndMakeVisible (outPanel);
        if (auto* box = dynamic_cast<juce::ComboBox*> (findControl (id::latencyMode)))
            box->setTooltip ("Zero: no lookahead, no oversampling delay (min-phase IIR or Off). "
                             "True-peak catching still honours the ceiling as well as a zero-look path can. "
                             "Natural: the Look knob plus linear-phase oversampling. "
                             "High: at least 8 ms look plus linear-phase oversampling.");
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);
        addAndMakeVisible (grHistory);
        addAndMakeVisible (loudness);

        loudness.onReset = [this]
        {
            proc.resetMeters();
            grHistory.clear();
        };

        proc.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateDimmed();
        updateLearnUi();

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { pollMeters(); });

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (defaultWidth, defaultHeight);
        startTimerHz (15);
    }

    DooblrLEditor::~DooblrLEditor()
    {
        stopTimer();
        vblank.reset();
        if (proc.isLearning())
            proc.cancelLearn();
        proc.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void DooblrLEditor::pollMeters()
    {
        const auto n = proc.getHistoryTap().popFrames (historyScratch.data(), (int) historyScratch.size());
        if (n > 0)
            grHistory.ingest (historyScratch.data(), n);

        const auto m = proc.takeMeters();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        inMeter.push (m.inputPeakL, m.inputPeakR, m.inputTruePeakL, m.inputTruePeakR, now);
        outMeter.push (m.outputPeakL, m.outputPeakR, m.outputTruePeakL, m.outputTruePeakR, now);
        const auto target = params::lufsTargetValue ((int) std::lround (
            state.getRawParameterValue (id::lufsTarget)->load()));
        loudness.setReadings (m.momentaryLufs, m.shortTermLufs, m.integratedLufs, m.lra, m.hasIntegrated, target);
        updateDimmed();
    }

    juce::Component* DooblrLEditor::findControl (const juce::String& parameterId) const
    {
        juce::Component* found = nullptr;
        forEachComponent (const_cast<DooblrLEditor&> (*this), [&] (juce::Component& c)
        {
            if (c.getComponentID() == parameterId && found == nullptr)
                found = &c;
        });
        return found;
    }

    void DooblrLEditor::timerCallback()
    {
        updateUndoButtons();
        proc.pollLearn();
        updateLearnUi();
        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() > transientStatusUntil)
        {
            transientStatus.clear();
            statusLabel.setText ({}, juce::dontSendNotification);
        }
        if (proc.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrLEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
    }

    void DooblrLEditor::updateUndoButtons()
    {
        auto& history = proc.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    void DooblrLEditor::updateDimmed()
    {
        const auto ditherOn = state.getRawParameterValue (id::dither)->load() > 0.5f;
        if (auto* c = findControl (id::shaping))
            ui::setDimmed (*c, ! ditherOn);
        const auto zero = (int) std::lround (state.getRawParameterValue (id::latencyMode)->load()) <= 0;
        if (auto* c = findControl (id::lookahead))
            ui::setDimmed (*c, zero);
    }

    void DooblrLEditor::toggleLearn()
    {
        if (proc.isLearning())
            proc.cancelLearn();
        else
            proc.startLearn();
        updateLearnUi();
    }

    void DooblrLEditor::updateLearnUi()
    {
        const auto busy = proc.isLearning();
        learnButton.setButtonText (busy ? "Cancel" : "Learn");
        const auto canRevert = proc.canRevertLearn() && ! busy;
        const auto visChanged = learnRevertButton.isVisible() != canRevert || learnBar.isVisible() != busy;
        learnRevertButton.setVisible (canRevert);
        learnRevertButton.setEnabled (canRevert);
        learnBar.setVisible (busy);
        learnBar.setProgress (proc.learnProgress(), busy);
        const auto text = proc.getLearnStatus();
        if (text.isNotEmpty())
            statusLabel.setText (text, juce::dontSendNotification);
        if (visChanged)
            resized();
    }

    void DooblrLEditor::refreshLearnUi()
    {
        proc.pollLearn();
        updateLearnUi();
    }

    void DooblrLEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 2500;
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrLEditor::refreshPresetBox()
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

    void DooblrLEditor::presetChosen()
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

    void DooblrLEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-L preset",
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

    void DooblrLEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-L preset",
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

    bool DooblrLEditor::keyPressed (const juce::KeyPress& key)
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

    void DooblrLEditor::paint (juce::Graphics& g)
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
        fillPanel (styleArea);
        fillPanel (preArea);
        fillPanel (enhanceArea);
        fillPanel (linkArea);
        fillPanel (timeArea);
        fillPanel (outArea);
    }

    void DooblrLEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (headerHeight);
        {
            auto header = headerArea.reduced (32, 10);
            auto titleColumn = header.removeFromLeft (juce::jlimit (140, 200, header.getWidth() / 6));
            titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
            subtitleLabel.setBounds (titleColumn);

            header.removeFromLeft (8);
            undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (4);
            redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            header.removeFromLeft (8);
            learnButton.setBounds (header.removeFromLeft (56).withSizeKeepingCentre (56, 24));
            header.removeFromLeft (4);
            if (learnRevertButton.isVisible())
            {
                learnRevertButton.setBounds (header.removeFromLeft (56).withSizeKeepingCentre (56, 24));
                header.removeFromLeft (4);
            }
            else
                learnRevertButton.setBounds ({});

            bypassButton.setBounds (header.removeFromRight (88).withSizeKeepingCentre (88, 24));
            header.removeFromRight (8);
            loadButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (4);
            saveButton.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));
            header.removeFromRight (8);
            const auto presetWidth = juce::jlimit (110, 240, header.getWidth());
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        auto statusRow = bounds.removeFromBottom (16).reduced (24, 0);
        if (learnBar.isVisible())
        {
            learnBar.setBounds (statusRow.removeFromRight (100));
            statusRow.removeFromRight (8);
        }
        else
            learnBar.setBounds ({});
        statusLabel.setBounds (statusRow);
        auto body = bounds.reduced (margin, 8);

        const auto styleH = juce::jlimit (48, 64, body.getHeight() / 11);
        styleArea = body.removeFromTop (styleH);
        body.removeFromTop (panelGap);
        {
            auto row = styleArea.reduced (12, 6);
            styleCaption.setBounds (row.removeFromTop (14));
            styleStrip->setBounds (row);
        }

        const auto colGap = panelGap;
        const auto w = body.getWidth();
        const auto preW = (w - 3 * colGap) * 34 / 100;
        const auto enhW = (w - 3 * colGap) * 22 / 100;
        const auto linkW = (w - 3 * colGap) * 22 / 100;
        const auto timeW = juce::jmax (1, w - preW - enhW - linkW - 3 * colGap);
        const auto colNeed = 16 + juce::jmax (prePanel.getPreferredHeight (juce::jmax (1, preW - 20)),
                                             enhancePanel.getPreferredHeight (juce::jmax (1, enhW - 20)),
                                             linkPanel.getPreferredHeight (juce::jmax (1, linkW - 20)),
                                             timePanel.getPreferredHeight (juce::jmax (1, timeW - 20)));
        constexpr int outPadX = 10, outPadY = 8;
        const auto outNeed = juce::jmax (outPanel.getPreferredHeight (juce::jmax (1, w - 2 * outPadX)) + 2 * outPadY, 108);
        const auto minLower = colNeed + panelGap + outNeed;
        auto meterH = juce::jlimit (120, 280, body.getHeight() * 36 / 100);
        if (body.getHeight() - meterH - panelGap < minLower)
            meterH = juce::jmax (96, body.getHeight() - panelGap - minLower);
        meterArea = body.removeFromTop (meterH);
        body.removeFromTop (panelGap);
        {
            auto row = meterArea;
            const auto side = juce::jlimit (64, 100, row.getWidth() / 12);
            inMeter.setBounds (row.removeFromLeft (side));
            row.removeFromLeft (6);
            outMeter.setBounds (row.removeFromLeft (side));
            row.removeFromLeft (8);
            const auto lufsW = juce::jlimit (150, 220, row.getWidth() / 4);
            loudness.setBounds (row.removeFromRight (lufsW));
            row.removeFromRight (8);
            grHistory.setBounds (row);
        }

        const auto outInnerW = juce::jmax (1, body.getWidth() - 2 * outPadX);
        const auto outContentH = outPanel.getPreferredHeight (outInnerW);
        const auto outH = juce::jmax (outContentH + 2 * outPadY,
                                      juce::jlimit (108, 150, body.getHeight() / 3));
        outArea = body.removeFromBottom (outH);
        body.removeFromBottom (panelGap);
        outPanel.setBounds (outArea.reduced (outPadX, outPadY));

        preArea = body.removeFromLeft (preW);
        body.removeFromLeft (colGap);
        enhanceArea = body.removeFromLeft (enhW);
        body.removeFromLeft (colGap);
        linkArea = body.removeFromLeft (linkW);
        body.removeFromLeft (colGap);
        timeArea = body;

        prePanel.setBounds (preArea.reduced (10, 8));
        enhancePanel.setBounds (enhanceArea.reduced (10, 8));
        linkPanel.setBounds (linkArea.reduced (10, 8));
        timePanel.setBounds (timeArea.reduced (10, 8));
    }
}
