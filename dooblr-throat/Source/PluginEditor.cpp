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

        for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption, &statusLabel, &vowelCaption })
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

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &saveButton, (juce::Component*) &loadButton,
                                    (juce::Component*) &outputSlider, (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        buildBoard();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        updateStatus();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (980, 640);
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
            { id::nasal, "Nasal" } });
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
    }

    void DooblrThroatEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! presetBox.isPopupActive())
            refreshPresetBox();
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
        statusLabel.setBounds (status.reduced (4, 0));
        area.removeFromBottom (panelGap);

        const auto width = area.getWidth();
        const int padSize = juce::jlimit (ui::VowelPad::minSize, 240, juce::jmin (area.getHeight() / 2, width / 3));

        auto top = area.removeFromTop (juce::jmax (padSize + 16, vowel->getPreferredHeight (width - padSize - panelGap)));
        {
            auto padCol = top.removeFromLeft (padSize);
            vowelCaption.setBounds (padCol.removeFromTop (16));
            vowelPad.setBounds (padCol.removeFromTop (padSize));
            top.removeFromLeft (panelGap);
            const auto colW = (top.getWidth() - panelGap) / 2;
            const auto vowelH = juce::jmax (vowel->getPreferredHeight (colW), voice->getPreferredHeight (colW));
            auto row = top.removeFromTop (vowelH);
            vowel->setBounds (row.removeFromLeft (colW));
            row.removeFromLeft (panelGap);
            voice->setBounds (row);
        }

        area.removeFromTop (panelGap);
        const auto sourceH = source->getPreferredHeight (width);
        source->setBounds (area.removeFromTop (sourceH));
        area.removeFromTop (panelGap);

        const auto midW = (width - panelGap) / 2;
        const auto midH = juce::jmax (pitch->getPreferredHeight (midW), env->getPreferredHeight (midW));
        auto mid = area.removeFromTop (midH);
        pitch->setBounds (mid.removeFromLeft (midW));
        mid.removeFromLeft (panelGap);
        env->setBounds (mid);
    }
}
