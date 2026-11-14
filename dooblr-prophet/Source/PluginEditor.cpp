#include "PluginEditor.h"

#include "ControlRules.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/Effects.h"
#include "UI/Layout.h"
#include "UI/Theme.h"

namespace dooblrprophet
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
        constexpr int headerHeight = 60, tabBarHeight = 36, margin = 10, panelGap = 6;
        constexpr int meterWidth = 34, minGraphHeight = 120;

        juce::Colour colourForPanel (const juce::String& title, int page)
        {
            if (title.containsIgnoreCase ("OSC") || title == "MIXER")
                return Palette::accent;
            if (title.containsIgnoreCase ("FILTER") || title.containsIgnoreCase ("HIGH-PASS")
                || title.containsIgnoreCase ("LOW-PASS") || title.containsIgnoreCase ("AMP"))
                return title.containsIgnoreCase ("AMP") ? Palette::master : Palette::dynamic;
            if (title.containsIgnoreCase ("LFO") || title.containsIgnoreCase ("POLY-MOD") || title.containsIgnoreCase ("MOD"))
                return Palette::frozen;
            return ui::pageColour (page);
        }

        float valueOf (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            // From the parameter itself, not the raw value (which can lag a listener callback).
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }
    }

    const char* DooblrProphetEditor::pageName (int page)
    {
        return layout::pages()[(size_t) juce::jlimit (0, (int) numPages - 1, page)].name;
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
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredLeft);
        statusLabel.setComponentID ("status");
        statusLabel.setMinimumHorizontalScale (0.85f);

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

        // Random: a new playable patch (Randomiser.h) as one undo step, named "Random".
        randomButton.setComponentID ("random");
        randomButton.setColour (juce::TextButton::buttonColourId, Palette::accent.withAlpha (0.22f));
        randomButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
        randomButton.onClick = [this] { randomise(); };

        saveButton.setComponentID ("savePreset");
        saveButton.setTooltip ("Save the current patch as a user preset file (.dprophet)");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.setComponentID ("loadPreset");
        loadButton.setTooltip ("Load a user preset file (.dprophet)");
        loadButton.onClick = [this] { loadPreset(); };

        outputSlider.setComponentID (id::output);
        outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
        outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id::output, outputSlider);
        outputSlider.setDoubleClickReturnValue (true, 0.0);

        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                    (juce::Component*) &randomButton, (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &outputSlider,
                                    (juce::Component*) &outputMeter })
            addAndMakeVisible (c);

        for (int page = 0; page < numPages; ++page)
        {
            auto& button = pageButtons[(size_t) page];
            button.setButtonText (pageName (page));
            button.setComponentID ("page_" + juce::String (pageName (page)));
            button.setRadioGroupId (2001);
            button.setClickingTogglesState (true);
            button.setColour (juce::TextButton::buttonOnColourId, ui::pageColour (page).withAlpha (0.35f));
            button.onClick = [this, page] { selectPage (page); };
            addAndMakeVisible (button);
        }

        buildPages();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        selectPage (soundPage);
        updateEnablement();
        updatePageStatus();

        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        startTimerHz (15);
    }

    DooblrProphetEditor::~DooblrProphetEditor()
    {
        stopTimer();
        synth.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    //==============================================================================
    ui::ControlPanel& DooblrProphetEditor::addPanel (int page, const juce::String& title, const std::vector<Spec>& specs)
    {
        auto& content = pages[(size_t) page];
        content.panels.push_back (std::make_unique<ui::ControlPanel> (state, title, colourForPanel (title, page), specs));
        addChildComponent (*content.panels.back());
        return *content.panels.back();
    }

    void DooblrProphetEditor::addToggle (int page, const juce::String& caption, const juce::String& parameterId,
                                         const juce::StringArray& labels)
    {
        auto& content = pages[(size_t) page];
        content.toggles.push_back (std::make_unique<ui::SegmentedToggle> (state, parameterId, labels, ui::pageColour (page)));
        content.toggleCaptions.add (caption);

        // Wide enough for the longest label in every segment.
        const juce::Font font (juce::FontOptions (13.0f, juce::Font::bold).withKerningFactor (0.06f));
        float widest = 0.0f;
        for (const auto& label : labels)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, label));
        content.toggleWidths.push_back (juce::jmax ((int) ui::SegmentedToggle::minWidth, labels.size() * (juce::roundToInt (widest) + 30)));
        addChildComponent (*content.toggles.back());
    }

    void DooblrProphetEditor::buildPages()
    {
        const auto source = [&p = synth] { return p.readSettings(); };
        auto& tap = synth.getEngine().getVoiceTap();

        auto& apvts = synth.getState();
        auto& sound = pages[(size_t) soundPage];
        sound.graphs.push_back (std::make_unique<ui::OscillatorDisplay> (source));
        sound.graphs.back()->setComponentID ("graph_osc");
        sound.graphs.push_back (std::make_unique<ui::FilterDisplay> (
            source, ui::FilterDisplay::Parameters { apvts.getParameter (id::lpfCutoff), apvts.getParameter (id::lpfReso),
                                                    apvts.getParameter (id::hpfCutoff), apvts.getParameter (id::hpfReso) }));
        sound.graphs.back()->setComponentID ("graph_filter");
        sound.graphs.push_back (std::make_unique<ui::EnvelopeDisplay> (source));
        sound.graphs.back()->setComponentID ("graph_env");
        pages[(size_t) modulationPage].graphs.push_back (std::make_unique<ui::LfoDisplay> (source, tap));
        pages[(size_t) modulationPage].graphs.back()->setComponentID ("graph_lfo");
        pages[(size_t) voicesPage].graphs.push_back (std::make_unique<ui::VoiceDisplay> (source, tap));
        pages[(size_t) voicesPage].graphs.back()->setComponentID ("graph_voices");
        pages[(size_t) fxPage].graphs.push_back (std::make_unique<ui::FxDisplay> (source, [&p = synth] { return p.getHostBpm(); }));
        pages[(size_t) fxPage].graphs.back()->setComponentID ("graph_fx");

        for (auto& content : pages)
            for (auto& graph : content.graphs)
                addChildComponent (*graph);

        // The pages, panels and controls come from layout::pages() (shared with the remote link).
        const auto& layoutPages = layout::pages();
        jassert ((int) layoutPages.size() == numPages);

        for (int page = 0; page < numPages; ++page)
        {
            const auto& def = layoutPages[(size_t) page];

            for (const auto& t : def.toggles)
                addToggle (page, t.caption, t.id, t.labels);

            for (const auto& panelDef : def.panels)
            {
                std::vector<Spec> specs;
                for (const auto& c : panelDef.controls)
                    specs.push_back ({ c.id, c.caption, c.kind, c.width, 0, -1, c.stack });
                auto& panel = addPanel (page, panelDef.title, specs);

                if (panelDef.voicePresets)
                {
                    for (size_t i = 0; i < voicePresetButtons.size(); ++i)
                    {
                        auto& button = voicePresetButtons[i];
                        const auto count = layout::voicePresetCounts[i];
                        button.setButtonText (juce::String (count));
                        button.setComponentID (layout::voicePresetsId + juce::String (count));
                        button.setTooltip ("Set Voices to " + juce::String (count));
                        button.setClickingTogglesState (false);
                        button.setColour (juce::TextButton::buttonOnColourId, ui::pageColour (voicesPage).withAlpha (0.45f));
                        button.onClick = [this, count] { setVoices (count); };
                        panel.addExtra (button, 44, (int) i + 1);
                    }
                }

                if (panelDef.fxPicker >= 0)
                {
                    const auto module = panelDef.fxPicker;
                    auto& picker = fxPresetPickers[(size_t) module];
                    const auto& names = module == 0 ? dsp::echoPresetNames() : dsp::reverbPresetNames();
                    picker.box.addItemList (names, 1);
                    picker.setComponentID (module == 0 ? layout::echoPresetPickerId : layout::reverbPresetPickerId);
                    picker.box.setComponentID (module == 0 ? "echoPresetBox" : "reverbPresetBox");
                    picker.box.setSelectedItemIndex (juce::roundToInt (valueOf (state, module == 0 ? id::echoPreset : id::reverbPreset)), juce::dontSendNotification);
                    picker.box.onChange = [this, module]
                    {
                        synth.applyFxPreset (module, fxPresetPickers[(size_t) module].box.getSelectedItemIndex());
                        updateEnablement();
                        updateUndoButtons();
                    };
                    panel.addExtra (picker, 150, -1, module == 0 ? id::echoMix : id::reverbMix, true);
                }
            }

            pages[(size_t) page].rows = def.rows;
        }
    }

    DooblrProphetEditor::FxPresetPicker::FxPresetPicker()
    {
        caption.setText ("Preset", juce::dontSendNotification);
        caption.setFont (juce::FontOptions (12.0f));
        caption.setColour (juce::Label::textColourId, Palette::textMuted);
        caption.setJustificationType (juce::Justification::centredLeft);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);
        addAndMakeVisible (box);
    }

    void DooblrProphetEditor::FxPresetPicker::resized()
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (16));
        box.setBounds (area.removeFromTop (28).reduced (0, 1));
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

    //==============================================================================
    void DooblrProphetEditor::selectPage (int page)
    {
        selectedPage = juce::jlimit (0, (int) numPages - 1, page);
        pageButtons[(size_t) selectedPage].setToggleState (true, juce::dontSendNotification);

        for (int p = 0; p < numPages; ++p)
        {
            const bool shown = p == selectedPage;
            auto& content = pages[(size_t) p];

            for (auto& graph : content.graphs)
                graph->setVisible (shown);

            for (auto& panel : content.panels)
                panel->setVisible (shown);

            for (auto& t : content.toggles)
                t->setVisible (shown);
        }

        updatePageStatus();
        resized();
        repaint();
    }

    void DooblrProphetEditor::updateEnablement()
    {
        // Ignored controls are dimmed, never disabled: a disabled JUCE component swallows clicks, so
        // the Saw / Triangle / Pulse switches could not be switched on in Variable mode (0.1.4 fix).
        // The rules live in ControlRules.cpp, shared with the remote link.
        const auto dimmed = rules::dimmedControls ([this] (const char* pid) { return valueOf (state, pid); });
        const auto& layoutPages = layout::pages();

        for (int page = 0; page < numPages; ++page)
            for (size_t i = 0; i < pages[(size_t) page].panels.size(); ++i)
                for (const auto& c : layoutPages[(size_t) page].panels[i].controls)
                    if (auto* control = pages[(size_t) page].panels[i]->findControl (c.id))
                        ui::setDimmed (*control, dimmed.contains (c.id));

        for (int module = 0; module < 2; ++module)
        {
            auto& picker = fxPresetPickers[(size_t) module];
            ui::setDimmed (picker.box, dimmed.contains (module == 0 ? layout::echoPresetPickerId : layout::reverbPresetPickerId));
            const auto index = juce::roundToInt (valueOf (state, module == 0 ? id::echoPreset : id::reverbPreset));
            if (picker.box.getSelectedItemIndex() != index && ! picker.box.isPopupActive())
                picker.box.setSelectedItemIndex (index, juce::dontSendNotification);
        }

        const auto voices = juce::roundToInt (valueOf (state, id::voices));

        for (size_t i = 0; i < voicePresetButtons.size(); ++i)
        {
            ui::setDimmed (voicePresetButtons[i], dimmed.contains (layout::voicePresetsId));
            voicePresetButtons[i].setToggleState (voices == layout::voicePresetCounts[i], juce::dontSendNotification);
        }
    }

    void DooblrProphetEditor::setStatus (const juce::String& text)
    {
        transientStatus = text;
        transientStatusUntil = juce::Time::getMillisecondCounter() + 4000;
        updatePageStatus();
    }

    void DooblrProphetEditor::updatePageStatus()
    {
        juce::String text;

        if (transientStatus.isNotEmpty() && juce::Time::getMillisecondCounter() < transientStatusUntil)
        {
            text = transientStatus;
        }
        else
        {
            transientStatus.clear();
            text = rules::pageStatus (selectedPage, synth.readSettings(), synth.getEngine().getNumActiveVoices());
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

    void DooblrProphetEditor::randomise()
    {
        juce::Random random; // seeded from the clock: a new patch each press
        synth.randomisePatch (random);
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
        updatePageStatus();

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
        g.fillRect (tabBarArea);

        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.fillRect (tabBarArea.getX(), tabBarArea.getBottom() - 1, tabBarArea.getWidth(), 1);

        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (Palette::textMuted);
        const auto& captions = pages[(size_t) selectedPage].toggleCaptions;

        for (size_t i = 0; i < toggleCaptionAreas.size() && (int) i < captions.size(); ++i)
            g.drawText (captions[(int) i], toggleCaptionAreas[i], juce::Justification::centredRight);
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

        tabBarArea = bounds.removeFromTop (tabBarHeight);
        {
            auto bar = tabBarArea.reduced (margin, 4);
            toggleCaptionAreas.clear();

            const auto toggleSpaceFor = [] (const PageContent& content)
            {
                int space = 0;
                for (int i = 0; i < (int) content.toggles.size(); ++i)
                    space += juce::jmax (ui::SegmentedToggle::minWidth, content.toggleWidths[(size_t) i]) + 52 + 8;
                return space;
            };

            // Tab width ignores this page's mode switches, so Sound and FX stay the same size.
            int maxToggleSpace = 0;
            for (const auto& page : pages)
                maxToggleSpace = juce::jmax (maxToggleSpace, toggleSpaceFor (page));

            auto& content = pages[(size_t) selectedPage];
            const int toggleSpace = toggleSpaceFor (content);
            auto toggleBar = toggleSpace > 0 && toggleSpace < bar.getWidth()
                                 ? bar.removeFromRight (toggleSpace)
                                 : juce::Rectangle<int>{};

            for (int i = (int) content.toggles.size() - 1; i >= 0; --i)
            {
                auto& t = *content.toggles[(size_t) i];
                const auto width = juce::jmax (ui::SegmentedToggle::minWidth, content.toggleWidths[(size_t) i]);
                t.setBounds (toggleBar.removeFromRight (width).withSizeKeepingCentre (width, juce::jmin (bar.getHeight(), 32)));
                toggleCaptionAreas.insert (toggleCaptionAreas.begin(), toggleBar.removeFromRight (52).withTrimmedRight (6));
                toggleBar.removeFromRight (8);
            }

            const auto fullBarWidth = tabBarArea.reduced (margin, 4).getWidth();
            const auto tabsBudget = juce::jmax (0, fullBarWidth - (maxToggleSpace < fullBarWidth ? maxToggleSpace : 0));
            const auto tabWidth = juce::jlimit (88, 112,
                                                (tabsBudget - 8) / juce::jmax (1, (int) numPages + (tabsBudget > 200 ? 1 : 0)));
            for (auto& button : pageButtons)
                button.setBounds (bar.removeFromLeft (tabWidth).reduced (2, 1));

            // One line only: a taller box would wrap "2x oversampled" onto two lines.
            statusLabel.setBounds (bar.reduced (8, 0).withSizeKeepingCentre (juce::jmax (0, bar.getWidth() - 16), 16));
            statusLabel.setVisible (statusLabel.getWidth() >= 80);
        }

        contentArea = bounds.reduced (margin);
        layoutPage (selectedPage);
    }

    ui::OscillatorDisplay* DooblrProphetEditor::getOscillatorDisplay() const
    {
        for (auto& g : pages[(size_t) soundPage].graphs)
            if (auto* d = dynamic_cast<ui::OscillatorDisplay*> (g.get()))
                return d;
        return nullptr;
    }

    ui::FilterDisplay* DooblrProphetEditor::getFilterDisplay() const
    {
        for (auto& g : pages[(size_t) soundPage].graphs)
            if (auto* d = dynamic_cast<ui::FilterDisplay*> (g.get()))
                return d;
        return nullptr;
    }

    ui::EnvelopeDisplay* DooblrProphetEditor::getEnvelopeDisplay() const
    {
        for (auto& g : pages[(size_t) soundPage].graphs)
            if (auto* d = dynamic_cast<ui::EnvelopeDisplay*> (g.get()))
                return d;
        return nullptr;
    }

    ui::LfoDisplay* DooblrProphetEditor::getLfoDisplay() const
    {
        for (auto& g : pages[(size_t) modulationPage].graphs)
            if (auto* d = dynamic_cast<ui::LfoDisplay*> (g.get()))
                return d;
        return nullptr;
    }

    void DooblrProphetEditor::layoutPage (int page)
    {
        auto area = contentArea;
        auto& content = pages[(size_t) page];
        auto rows = content.rows;

        const auto widthsForRow = [&] (const std::vector<int>& row) -> std::vector<int>
        {
            std::vector<int> mins, widths (row.size(), 0);
            int sum = 0;

            for (auto index : row)
            {
                const auto w = juce::jmax (80, content.panels[(size_t) index]->getMinOneRowWidth());
                mins.push_back (w);
                sum += w;
            }

            const int gaps = panelGap * juce::jmax (0, (int) row.size() - 1);
            const int avail = juce::jmax (0, area.getWidth() - gaps);

            if (row.empty())
                return widths;

            if (sum > 0 && sum <= avail)
            {
                const int extra = avail - sum;

                for (size_t i = 0; i < row.size(); ++i)
                    widths[i] = mins[i] + extra / (int) row.size() + ((int) i < extra % (int) row.size() ? 1 : 0);
            }
            else if (sum > 0)
            {
                int used = 0;

                for (size_t i = 0; i < row.size(); ++i)
                {
                    widths[i] = i + 1 == row.size() ? juce::jmax (80, avail - used)
                                                    : juce::jmax (80, avail * mins[i] / sum);
                    used += widths[i];
                }
            }
            else
            {
                const auto equal = avail / juce::jmax (1, (int) row.size());

                for (size_t i = 0; i < row.size(); ++i)
                    widths[i] = i + 1 == row.size() ? avail - equal * ((int) row.size() - 1) : equal;
            }

            return widths;
        };

        std::vector<int> rowHeights;
        std::vector<std::vector<int>> widthsPerRow;
        int panelsHeight = 0;

        for (const auto& row : rows)
        {
            auto widths = widthsForRow (row);
            int height = 0;

            for (size_t i = 0; i < row.size(); ++i)
                height = juce::jmax (height, content.panels[(size_t) row[i]]->getPreferredHeight (widths[i]));

            rowHeights.push_back (height);
            panelsHeight += height + panelGap;
            widthsPerRow.push_back (std::move (widths));
        }

        const auto graphHeight = juce::jmax (minGraphHeight, area.getHeight() - panelsHeight);
        auto graphStrip = area.removeFromTop (graphHeight);
        const auto nGraphs = (int) content.graphs.size();

        if (nGraphs <= 1)
        {
            if (nGraphs == 1)
                content.graphs[0]->setBounds (graphStrip);
        }
        else
        {
            const auto gw = (graphStrip.getWidth() - panelGap * (nGraphs - 1)) / nGraphs;

            for (int i = 0; i < nGraphs; ++i)
            {
                content.graphs[(size_t) i]->setBounds (i + 1 == nGraphs ? graphStrip : graphStrip.removeFromLeft (gw));
                graphStrip.removeFromLeft (panelGap);
            }
        }

        for (size_t r = 0; r < rows.size(); ++r)
        {
            area.removeFromTop (panelGap);
            auto rowArea = area.removeFromTop (rowHeights[r]);
            const auto& row = rows[r];
            const auto& widths = widthsPerRow[r];

            for (size_t i = 0; i < row.size(); ++i)
            {
                content.panels[(size_t) row[i]]->setBounds (i + 1 == row.size() ? rowArea : rowArea.removeFromLeft (widths[i]));
                rowArea.removeFromLeft (panelGap);
            }
        }
    }
}
