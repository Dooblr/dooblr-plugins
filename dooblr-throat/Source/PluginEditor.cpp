#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"
#include "UI/Theme.h"

namespace dooblrthroat
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
        constexpr int headerHeight = 60, margin = 12, panelGap = 6;
        constexpr int phraseRowH = 40;
        constexpr int meterWidth = 34;
    }

    DooblrThroatEditor::DooblrThroatEditor (DooblrThroatProcessor& p)
        : juce::AudioProcessorEditor (p),
          synth (p),
          state (p.getState()),
          outputMeter (&p.getPeakTap()),
          vowelPad (p.getState(), id::height, id::back, id::vowel)
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        styleLabel (vowelCaption, "Vowel", 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);
        styleLabel (phraseCaption, "Phrase", 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);
        styleLabel (phraseReadout, {}, 11.0f, Palette::textMuted, juce::Justification::centredRight);

        for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption, &statusLabel, &vowelCaption, &phraseCaption, &phraseReadout })
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
        saveButton.setTooltip ("Save the current patch as a user preset file (.dthroat)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset file (.dthroat)");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, -6.0);

        vowelPad.setComponentID ("vowelPad");
        addAndMakeVisible (vowelPad);

        phraseEditor.setComponentID ("phraseText");
        phraseEditor.setMultiLine (false);
        phraseEditor.setReturnKeyStartsNewLine (false);
        phraseEditor.setEscapeAndReturnKeysConsumed (true);
        phraseEditor.setJustification (juce::Justification::centredLeft);
        phraseEditor.setTextToShowWhenEmpty ("type a phrase, then play a MIDI note", Palette::textMuted);
        phraseEditor.setColour (juce::TextEditor::backgroundColourId, Palette::panel);
        phraseEditor.setColour (juce::TextEditor::outlineColourId, Palette::divider);
        phraseEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        phraseEditor.setColour (juce::TextEditor::textColourId, Palette::textPrimary);
        phraseEditor.setFont (juce::Font (juce::FontOptions (14.0f)));
        phraseEditor.onReturnKey = [this] { commitPhraseText(); };
        phraseEditor.onFocusLost = [this] { commitPhraseText(); };
        addAndMakeVisible (phraseEditor);

        restartButton.setComponentID ("phraseReset");
        restartButton.setTooltip ("Restart the phrase from the first syllable");
        restartButton.onClick = [this] { synth.requestPhraseRestart(); updatePhraseReadout(); };
        addAndMakeVisible (restartButton);

        styleLabel (modeCaption, "Mode", 11.0f, Palette::textMuted, juce::Justification::centredRight);
        styleLabel (rateCaption, "Rate", 11.0f, Palette::textMuted, juce::Justification::centredRight);
        modeBox.setComponentID (id::mode);
        modeBox.setTooltip ("Sing: one syllable per note. Speak: the whole phrase on a single note.");
        modeBox.addItemList (params::modeNames(), 1);
        modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id::mode, modeBox);
        rateSlider.setComponentID (id::rate);
        rateSlider.setTooltip ("Speak-mode rate (does not change MIDI pitch).");
        rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        rateSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        rateSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::rate, rateSlider);
        for (juce::Component* c : { (juce::Component*) &modeCaption, (juce::Component*) &modeBox,
                                    (juce::Component*) &rateCaption, (juce::Component*) &rateSlider })
            addAndMakeVisible (c);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        buildBoard();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateStatus();
        refreshPhraseEditor();
        updatePhraseReadout();

        setResizable (true, true);
        setResizeLimits (900, 560, 2400, 1600);
        setSize (980, 600);
        startTimerHz (15);
    }

    DooblrThroatEditor::~DooblrThroatEditor()
    {
        stopTimer();
        synth.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    ui::ControlPanel& DooblrThroatEditor::addPanel (const juce::String& title, juce::Colour colour, const std::vector<Spec>& specs)
    {
        panels.push_back (std::make_unique<ui::ControlPanel> (state, title, colour, specs));
        addAndMakeVisible (*panels.back());
        return *panels.back();
    }

    void DooblrThroatEditor::buildBoard()
    {
        addPanel ("Vowel", Palette::accent, {
            { id::vowel, "Vowel", Kind::combo, 128 },
            { id::height, "Height" },
            { id::back, "Back" } });
        addPanel ("Voice", Palette::dynamic, {
            { id::female, "Female" },
            { id::child, "Child" },
            { id::nasal, "Nasal" },
            { id::effort, "Effort" },
            { id::formantTune, "Tuning" } });
        addPanel ("Source", Palette::spectrum, {
            { id::breath, "Breath" },
            { id::tension, "Tension" },
            { id::brightness, "Bright" },
            { id::fry, "Fry" },
            { id::roughness, "Rough" },
            { id::pressure, "Pressure" },
            { id::voiced, "Voiced", Kind::toggle } });
        addPanel ("Pitch", Palette::frozen, {
            { id::vibratoDepth, "Vibrato" },
            { id::vibratoRate, "Vib Rate" },
            { id::glide, "Glide" },
            { id::bendRange, "Bend" } });
        addPanel ("Envelope", Palette::master, {
            { id::attack, "Attack" },
            { id::decay, "Decay" },
            { id::sustain, "Sustain" },
            { id::release, "Release" } });
    }

    void DooblrThroatEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 4000;
        updateStatus();
    }

    void DooblrThroatEditor::updateStatus()
    {
        juce::String text;

        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() < transientStatusUntil)
        {
            text = transientStatus;
        }
        else
        {
            transientStatus.clear();
            text = synth.getEngine().isSounding()
                       ? juce::String ("Voice")
                       : juce::String::fromUTF8 ("Idle  \xc2\xb7  MIDI instrument");
        }

        if (statusLabel.getText() != text)
            statusLabel.setText (text, juce::dontSendNotification);
    }

    void DooblrThroatEditor::refreshPresetBox()
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

    void DooblrThroatEditor::presetChosen()
    {
        if (updatingPresetBox)
            return;

        const auto itemId = presetBox.getSelectedId();

        if (itemId >= userIdBase && itemId - userIdBase < userPresetFiles.size())
            setStatus (synth.loadUserPreset (userPresetFiles[itemId - userIdBase]));
        else if (itemId >= factoryIdBase && itemId < userIdBase)
            synth.setCurrentProgram (itemId - factoryIdBase);

        refreshPresetBox();
        updateUndoButtons();
    }

    void DooblrThroatEditor::savePreset()
    {
        const auto folder = presets::userDirectory();
        folder.createDirectory();
        auto name = juce::File::createLegalFileName (synth.getPresetName());
        if (name.isEmpty())
            name = "Patch";

        fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-throat preset",
                                                           folder.getChildFile (name + presets::fileExtension),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrThroatEditor> safeThis (this);

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

    void DooblrThroatEditor::loadPreset()
    {
        const auto folder = presets::userDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-throat preset", folder.isDirectory() ? folder : juce::File(),
                                                           juce::String ("*") + presets::fileExtension);
        juce::Component::SafePointer<DooblrThroatEditor> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                          return;

                                      safeThis->setStatus (safeThis->synth.loadUserPreset (chooser.getResult()));
                                      safeThis->refreshPresetBox();
                                  });
    }

    void DooblrThroatEditor::timerCallback()
    {
        updateUndoButtons();
        updateStatus();

        if (synth.getPresetName() != shownPresetName && ! presetBox.isPopupActive())
            refreshPresetBox();
        updatePhraseReadout();
    }

    void DooblrThroatEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
        {
            refreshPresetBox();
            refreshPhraseEditor();
        }
        updatePhraseReadout();
    }

    void DooblrThroatEditor::updateUndoButtons()
    {
        auto& history = synth.getUndoHistory();
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
    }

    bool DooblrThroatEditor::keyPressed (const juce::KeyPress& key)
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

    void DooblrThroatEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);

        g.setColour (Palette::panel);
        g.fillRect (headerArea);

        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);

        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);
    }

    void DooblrThroatEditor::resized()
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
            const auto presetWidth = juce::jmin (header.getWidth(), 280);
            presetBox.setBounds (header.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        }

        contentArea = bounds.reduced (margin);
        layoutBoard();
    }

    void DooblrThroatEditor::layoutBoard()
    {
        auto area = contentArea;
        if (panels.size() < 5)
            return;

        auto* vowel = panels[0].get();
        auto* voice = panels[1].get();
        auto* source = panels[2].get();
        auto* pitch = panels[3].get();
        auto* env = panels[4].get();

        const int statusH = 22;
        auto status = area.removeFromBottom (statusH);
        statusLabel.setBounds (status.reduced (4, 0).withTrimmedRight (22));
        area.removeFromBottom (panelGap);

        {
            auto strip = area.removeFromTop (phraseRowH);
            phraseCaption.setBounds (strip.removeFromLeft (52).withSizeKeepingCentre (52, 16));
            strip.removeFromLeft (6);

            auto rateArea = strip.removeFromRight (168);
            rateCaption.setBounds (rateArea.removeFromLeft (36).withSizeKeepingCentre (36, 16));
            rateSlider.setBounds (rateArea.withSizeKeepingCentre (rateArea.getWidth(), 24));
            strip.removeFromRight (8);
            modeBox.setBounds (strip.removeFromRight (96).withSizeKeepingCentre (96, 26));
            strip.removeFromRight (4);
            modeCaption.setBounds (strip.removeFromRight (40).withSizeKeepingCentre (40, 16));
            strip.removeFromRight (8);
            restartButton.setBounds (strip.removeFromRight (56).withSizeKeepingCentre (56, 24));
            strip.removeFromRight (6);
            phraseReadout.setBounds (strip.removeFromRight (108).withSizeKeepingCentre (108, 22));
            strip.removeFromRight (6);
            phraseEditor.setBounds (strip.reduced (0, 6));
        }
        area.removeFromTop (panelGap);

        const auto width = area.getWidth();
        const auto sourceH = source->getPreferredHeight (width);
        const auto midW = (width - panelGap) / 2;
        const auto midH = juce::jmax (pitch->getPreferredHeight (midW), env->getPreferredHeight (midW));
        const int topBudget = juce::jmax (ui::VowelPad::minSize + 16,
                                          area.getHeight() - sourceH - midH - 2 * panelGap);
        const int padSize = juce::jlimit (ui::VowelPad::minSize, 240,
                                          juce::jmin (topBudget - 16, width / 3));
        const auto colW = width - padSize - panelGap;
        const auto vowelH = vowel->getPreferredHeight (colW);
        const auto voiceH = voice->getPreferredHeight (colW);
        const int topH = juce::jmax (padSize + 16, vowelH + panelGap + voiceH);
        auto top = area.removeFromTop (juce::jmin (topH, juce::jmax (padSize + 16, area.getHeight() - sourceH - midH - 2 * panelGap)));
        {
            auto padCol = top.removeFromLeft (padSize);
            vowelCaption.setBounds (padCol.removeFromTop (16));
            const int padH = juce::jmin (padSize, padCol.getHeight());
            vowelPad.setBounds (padCol.removeFromTop (padH));
            top.removeFromLeft (panelGap);
            vowel->setBounds (top.removeFromTop (vowelH));
            if (top.getHeight() > 0)
                top.removeFromTop (juce::jmin (panelGap, top.getHeight()));
            voice->setBounds (top.removeFromTop (juce::jmin (voiceH, juce::jmax (0, top.getHeight()))));
        }

        area.removeFromTop (panelGap);
        source->setBounds (area.removeFromTop (sourceH));
        area.removeFromTop (panelGap);

        auto mid = area.removeFromTop (midH);
        pitch->setBounds (mid.removeFromLeft (midW));
        mid.removeFromLeft (panelGap);
        env->setBounds (mid);
    }

    void DooblrThroatEditor::commitPhraseText()
    {
        const auto text = phraseEditor.getText();
        if (text == synth.getPhraseText())
            return;
        synth.setPhraseText (text);
    }

    void DooblrThroatEditor::refreshPhraseEditor()
    {
        const auto text = synth.getPhraseText();
        if (phraseEditor.getText() != text && ! phraseEditor.hasKeyboardFocus (true))
            phraseEditor.setText (text, false);
    }

    void DooblrThroatEditor::updatePhraseReadout()
    {
        char buf[64] {};
        synth.getEngine().copyReadout (buf, sizeof (buf));
        juce::String text (buf);
        const int n = synth.getEngine().syllableCount();
        if (text.isEmpty() && n > 0 && synth.getPhraseText().isNotEmpty())
            text = juce::String (synth.getEngine().syllableIndex() + 1) + "/" + juce::String (n);
        if (phraseReadout.getText() != text)
            phraseReadout.setText (text, juce::dontSendNotification);
    }
}
