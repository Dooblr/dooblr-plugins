#include "PluginEditor.h"

#include "Parameters.h"
#include "Presets.h"
#include "DSP/Effects.h"
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
        constexpr int headerHeight = 60, navWidth = 150, titleBarHeight = 44, margin = 12, panelGap = 8;
        constexpr int meterWidth = 34, minGraphHeight = 120;
        constexpr std::array<int, 3> voicePresets { 5, 6, 10 };

        Spec knob (const char* pid, const char* caption) { return { pid, caption, Kind::knob }; }
        Spec toggle (const char* pid, const char* caption) { return { pid, caption, Kind::toggle }; }
        Spec combo (const char* pid, const char* caption) { return { pid, caption, Kind::combo }; }

        float valueOf (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            // From the parameter itself, not the raw value (which can lag a listener callback).
            auto* param = state.getParameter (pid);
            return param->convertFrom0to1 (param->getValue());
        }
    }

    const char* DooblrProphetEditor::pageName (int page)
    {
        static const char* names[] { "Oscillators", "Filters", "Envelopes", "Modulation", "Voices", "FX" };
        return names[juce::jlimit (0, (int) numPages - 1, page)];
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
        styleLabel (statusLabel, {}, 12.0f, Palette::textMuted, juce::Justification::centredRight);
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
            button.onClick = [this, page] { if (pageButtons[(size_t) page].getToggleState()) selectPage (page); };
            addAndMakeVisible (button);
        }

        buildPages();

        synth.presetChanged.addChangeListener (this);
        refreshPresetBox();
        updateUndoButtons();
        selectPage (oscillatorsPage);
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
        content.panels.push_back (std::make_unique<ui::ControlPanel> (state, title, ui::pageColour (page), specs));
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

        pages[oscillatorsPage].graph = std::make_unique<ui::OscillatorDisplay> (source);
        auto& apvts = synth.getState();
        pages[filtersPage].graph = std::make_unique<ui::FilterDisplay> (
            source, ui::FilterDisplay::Parameters { apvts.getParameter (id::lpfCutoff), apvts.getParameter (id::lpfReso),
                                                    apvts.getParameter (id::hpfCutoff), apvts.getParameter (id::hpfReso) });
        pages[envelopesPage].graph = std::make_unique<ui::EnvelopeDisplay> (source);
        pages[modulationPage].graph = std::make_unique<ui::LfoDisplay> (source, tap);
        pages[voicesPage].graph = std::make_unique<ui::VoiceDisplay> (source, tap);
        pages[fxPage].graph = std::make_unique<ui::FxDisplay> (source, [&p = synth] { return p.getHostBpm(); });

        for (auto& content : pages)
            addChildComponent (*content.graph);

        // Oscillators
        addToggle (oscillatorsPage, "OSC 1", id::osc1Mode, { "Variable", "Switched" });
        addToggle (oscillatorsPage, "OSC 2", id::osc2Mode, { "Variable", "Switched" });
        addPanel (oscillatorsPage, "OSCILLATOR 1", { knob (id::osc1Coarse, "Coarse"), knob (id::osc1Fine, "Fine"),
                                                     knob (id::osc1Shape, "Shape"), knob (id::osc1PW, "Pulse Width"),
                                                     toggle (id::osc1Saw, "Saw"), toggle (id::osc1Tri, "Triangle"),
                                                     toggle (id::osc1Pulse, "Pulse"), toggle (id::osc1Sync, "Sync") });
        addPanel (oscillatorsPage, "OSCILLATOR 2", { knob (id::osc2Coarse, "Coarse"), knob (id::osc2Fine, "Fine"),
                                                     knob (id::osc2Shape, "Shape"), knob (id::osc2PW, "Pulse Width"),
                                                     toggle (id::osc2Saw, "Saw"), toggle (id::osc2Tri, "Triangle"),
                                                     toggle (id::osc2Pulse, "Pulse"), toggle (id::osc2LowFreq, "Low Freq"),
                                                     toggle (id::osc2KeyTrack, "Key Track") });
        addPanel (oscillatorsPage, "MIXER", { knob (id::mixOsc1, "Osc 1"), knob (id::mixOsc2, "Osc 2"),
                                              knob (id::mixSub, "Sub"), knob (id::mixNoise, "Noise") });
        pages[oscillatorsPage].rows = { { 0 }, { 1 }, { 2 } };

        // Filters
        addToggle (filtersPage, "LOW-PASS", id::lpfModel, params::lpfModelNames());
        addPanel (filtersPage, "HIGH-PASS (2-POLE)", { knob (id::hpfCutoff, "Cutoff"), knob (id::hpfReso, "Resonance"),
                                                       knob (id::hpfEnvAmt, "Env Amount") });
        addPanel (filtersPage, "LOW-PASS (4-POLE LADDER)", { knob (id::lpfCutoff, "Cutoff"), knob (id::lpfReso, "Resonance"),
                                                             knob (id::lpfDrive, "Drive"), knob (id::lpfEnvAmt, "Env Amount"),
                                                             knob (id::lpfKeyTrack, "Key Track") });
        pages[filtersPage].rows = { { 0, 1 } };

        // Envelopes
        addPanel (envelopesPage, "FILTER ENVELOPE", { knob (id::fenvAttack, "Attack"), knob (id::fenvDecay, "Decay"),
                                                      knob (id::fenvSustain, "Sustain"), knob (id::fenvRelease, "Release"),
                                                      knob (id::fenvVelAmt, "Velocity"), toggle (id::fenvSync, "Sync to Amp") });
        addPanel (envelopesPage, "AMP ENVELOPE", { knob (id::aenvAttack, "Attack"), knob (id::aenvDecay, "Decay"),
                                                   knob (id::aenvSustain, "Sustain"), knob (id::aenvRelease, "Release"),
                                                   knob (id::aenvVelAmt, "Velocity") });
        pages[envelopesPage].rows = { { 0 }, { 1 } };

        // Modulation
        addToggle (modulationPage, "LFO", id::lfoSync, { "Free", "Tempo Sync" });
        addPanel (modulationPage, "POLY-MOD", { knob (id::pmodFiltEnv, "Filter Env"), knob (id::pmodOsc2, "Osc 2"),
                                                toggle (id::pmodDestFreq1, "Osc 1 Freq"), toggle (id::pmodDestShape1, "Osc 1 Shape"),
                                                toggle (id::pmodDestFilter, "Filter") });
        addPanel (modulationPage, "LFO", { combo (id::lfoWave, "Wave"), knob (id::lfoRate, "Rate"), combo (id::lfoDivision, "Division"),
                                           knob (id::lfoInitial, "Initial"), knob (id::lfoWheel, "Wheel"), knob (id::lfoNoiseMix, "Noise Mix") });
        addPanel (modulationPage, "LFO DESTINATIONS", { toggle (id::lfoDestFreq1, "Osc 1 Freq"), toggle (id::lfoDestFreq2, "Osc 2 Freq"),
                                                        toggle (id::lfoDestShape1, "Osc 1 Shape"), toggle (id::lfoDestShape2, "Osc 2 Shape"),
                                                        toggle (id::lfoDestLpf, "Low-Pass"), toggle (id::lfoDestHpf, "High-Pass"),
                                                        toggle (id::lfoDestAmp, "Amp") });
        pages[modulationPage].rows = { { 0 }, { 1 }, { 2 } };

        // Voices
        addToggle (voicesPage, "MODE", id::voiceMode, { "Poly", "Unison", "Mono" });
        auto& voicePanel = addPanel (voicesPage, "VOICES", { knob (id::voices, "Voices"), knob (id::unisonVoices, "Unison"),
                                                             knob (id::unisonDetune, "Detune"), knob (id::glide, "Glide"),
                                                             knob (id::vintage, "Vintage") });

        for (size_t i = 0; i < voicePresetButtons.size(); ++i)
        {
            auto& button = voicePresetButtons[i];
            const auto count = voicePresets[i];
            button.setButtonText (juce::String (count));
            button.setComponentID ("voicesPreset" + juce::String (count));
            button.setTooltip ("Set Voices to " + juce::String (count));
            button.setClickingTogglesState (false);
            button.setColour (juce::TextButton::buttonOnColourId, ui::pageColour (voicesPage).withAlpha (0.45f));
            button.onClick = [this, count] { setVoices (count); };
            voicePanel.addExtra (button, 44, (int) i + 1);
        }

        addPanel (voicesPage, "PERFORMANCE", { knob (id::bendRange, "Bend Range"), knob (id::atFilter, "AT > Filter"),
                                               knob (id::atLfo, "AT > LFO") });
        pages[voicesPage].rows = { { 0 }, { 1 } };

        // FX: Echo, then Reverb, after the voice mix.
        auto& echoPanel = addPanel (fxPage, "ECHO", { toggle (id::echoOn, "On"), knob (id::echoMix, "Mix"), knob (id::echoTime, "Time"),
                                                      toggle (id::echoSync, "Sync"), combo (id::echoDivision, "Division"),
                                                      knob (id::echoFeedback, "Feedback"), toggle (id::echoPingPong, "Ping-Pong") });
        auto& reverbPanel = addPanel (fxPage, "REVERB", { toggle (id::reverbOn, "On"), knob (id::reverbMix, "Mix"),
                                                          knob (id::reverbSize, "Size"), knob (id::reverbTone, "Tone") });

        for (int module = 0; module < 2; ++module)
        {
            auto& picker = fxPresetPickers[(size_t) module];
            const auto& names = module == 0 ? dsp::echoPresetNames() : dsp::reverbPresetNames();
            picker.box.addItemList (names, 1);
            picker.box.setComponentID (module == 0 ? "echoPresetBox" : "reverbPresetBox");
            picker.box.setSelectedItemIndex (juce::roundToInt (valueOf (state, module == 0 ? id::echoPreset : id::reverbPreset)), juce::dontSendNotification);
            picker.box.onChange = [this, module]
            {
                synth.applyFxPreset (module, fxPresetPickers[(size_t) module].box.getSelectedItemIndex());
                updateEnablement();
                updateUndoButtons();
            };
            (module == 0 ? echoPanel : reverbPanel).addExtra (picker, 150, -1, module == 0 ? id::echoMix : id::reverbMix, true);
        }
        pages[fxPage].rows = { { 0 }, { 1 } };
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
            content.graph->setVisible (shown);

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
        const auto setLive = [this] (int page, const char* pid, bool live)
        {
            for (auto& panel : pages[(size_t) page].panels)
                if (auto* control = panel->findControl (pid))
                    ui::setDimmed (*control, ! live);
        };

        // Variable mode uses the Shape knob; Switched mode uses the wave switches. Pulse Width acts in both.
        const bool osc1Switched = valueOf (state, id::osc1Mode) > 0.5f;
        const bool osc2Switched = valueOf (state, id::osc2Mode) > 0.5f;
        setLive (oscillatorsPage, id::osc1Shape, ! osc1Switched);
        setLive (oscillatorsPage, id::osc2Shape, ! osc2Switched);

        for (auto* pid : { id::osc1Saw, id::osc1Tri, id::osc1Pulse })
            setLive (oscillatorsPage, pid, osc1Switched);

        for (auto* pid : { id::osc2Saw, id::osc2Tri, id::osc2Pulse })
            setLive (oscillatorsPage, pid, osc2Switched);

        // Poly-Mod destinations only matter when a Poly-Mod source is turned up.
        const bool polyModOn = std::abs (valueOf (state, id::pmodFiltEnv)) > 1.0e-4f || valueOf (state, id::pmodOsc2) > 1.0e-4f;

        for (auto* pid : { id::pmodDestFreq1, id::pmodDestShape1, id::pmodDestFilter })
            setLive (modulationPage, pid, polyModOn);

        // Filter env synced to the amp envelope: its own ADSR is kept but not used.
        const bool envSynced = valueOf (state, id::fenvSync) > 0.5f;
        for (auto* pid : { id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease })
            setLive (envelopesPage, pid, ! envSynced);

        const bool synced = valueOf (state, id::lfoSync) > 0.5f;
        setLive (modulationPage, id::lfoRate, ! synced);
        setLive (modulationPage, id::lfoDivision, synced);

        // Voices sets the poly voice count; Unison and Detune only act in Unison mode; Mono uses one voice.
        const auto mode = juce::roundToInt (valueOf (state, id::voiceMode));
        setLive (voicesPage, id::voices, mode == 0);
        setLive (voicesPage, id::unisonVoices, mode == 1);
        setLive (voicesPage, id::unisonDetune, mode == 1);

        // FX: a module's controls are dimmed while it is off; Time or Division by the Sync switch.
        const bool echoOn = valueOf (state, id::echoOn) > 0.5f, reverbOn = valueOf (state, id::reverbOn) > 0.5f;
        const bool echoSync = valueOf (state, id::echoSync) > 0.5f;
        setLive (fxPage, id::echoMix, echoOn);
        setLive (fxPage, id::echoFeedback, echoOn);
        setLive (fxPage, id::echoSync, echoOn);
        setLive (fxPage, id::echoPingPong, echoOn);
        setLive (fxPage, id::echoTime, echoOn && ! echoSync);
        setLive (fxPage, id::echoDivision, echoOn && echoSync);
        for (auto* pid : { id::reverbMix, id::reverbSize, id::reverbTone })
            setLive (fxPage, pid, reverbOn);

        for (int module = 0; module < 2; ++module)
        {
            auto& picker = fxPresetPickers[(size_t) module];
            ui::setDimmed (picker.box, ! (module == 0 ? echoOn : reverbOn));
            const auto index = juce::roundToInt (valueOf (state, module == 0 ? id::echoPreset : id::reverbPreset));
            if (picker.box.getSelectedItemIndex() != index && ! picker.box.isPopupActive())
                picker.box.setSelectedItemIndex (index, juce::dontSendNotification);
        }

        const auto voices = juce::roundToInt (valueOf (state, id::voices));

        for (size_t i = 0; i < voicePresetButtons.size(); ++i)
        {
            ui::setDimmed (voicePresetButtons[i], mode != 0);
            voicePresetButtons[i].setToggleState (voices == voicePresets[i], juce::dontSendNotification);
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
            const auto s = synth.readSettings();

            switch (selectedPage)
            {
                case oscillatorsPage:
                {
                    const bool fm = s.pmodFreq1 && (std::abs (s.pmodFiltEnv) > 1.0e-4f || s.pmodOsc2 > 1.0e-4f);
                    text = fm ? "Poly-Mod FM active: 4x oversampled" : "2x oversampled";
                    break;
                }
                case filtersPage:
                    text = juce::String ("High-pass, then ") + (s.lpfModel == 1 ? "2040-style ladder" : "3320-style ladder");
                    break;
                case voicesPage:
                    text = juce::String (synth.getEngine().getNumActiveVoices()) + " sounding";
                    break;
                default:
                    break;
            }
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
        g.fillRect (navArea);

        g.setColour (Palette::divider);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.fillRect (navArea.getRight() - 1, navArea.getY(), 1, navArea.getHeight());

        g.setColour (Palette::accent);
        g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

        const auto colour = ui::pageColour (selectedPage);
        g.setColour (colour);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (juce::String (pageName (selectedPage)).toUpperCase(), titleBarArea.withWidth (160), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (Palette::textMuted);
        const auto& captions = pages[(size_t) selectedPage].toggleCaptions;

        for (size_t i = 0; i < toggleCaptionAreas.size() && (int) i < captions.size(); ++i)
            g.drawText (captions[(int) i], toggleCaptionAreas[i], juce::Justification::centredRight);

        g.setColour (Palette::divider);
        g.fillRect (titleBarArea.getX(), titleBarArea.getBottom() + 3, titleBarArea.getWidth(), 1);
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

        navArea = bounds.removeFromLeft (navWidth);
        {
            auto nav = navArea.reduced (12, 16);
            const auto pitch = juce::jlimit (28, 44, nav.getHeight() / numPages);

            for (auto& button : pageButtons)
                button.setBounds (nav.removeFromTop (pitch).reduced (0, 3));
        }

        contentArea = bounds.reduced (margin);
        layoutPage (selectedPage);
    }

    void DooblrProphetEditor::layoutPage (int page)
    {
        auto area = contentArea;
        auto& content = pages[(size_t) page];

        // Title bar: page title, status, then each mode switch with its caption on the right.
        titleBarArea = area.removeFromTop (titleBarHeight - 8);
        area.removeFromTop (8);
        toggleCaptionAreas.clear();
        {
            auto bar = titleBarArea;
            bar.removeFromLeft (160);

            for (int i = (int) content.toggles.size() - 1; i >= 0; --i)
            {
                auto& t = *content.toggles[(size_t) i];
                const auto width = content.toggleWidths[(size_t) i];
                t.setBounds (bar.removeFromRight (width).withSizeKeepingCentre (width, juce::jmin (bar.getHeight(), 32)));
                toggleCaptionAreas.insert (toggleCaptionAreas.begin(), bar.removeFromRight (58).withTrimmedRight (8));
                bar.removeFromRight (8);
            }

            // The status line only shows where it has room (it carries no control).
            statusLabel.setBounds (bar.reduced (4, 0));
            statusLabel.setVisible (statusLabel.getWidth() >= 120);
        }

        // Panels from the bottom up, each row sharing its width by the panels' natural widths.
        std::vector<int> rowHeights;
        int panelsHeight = 0;

        for (const auto& row : content.rows)
        {
            const auto rowWidth = (area.getWidth() - panelGap * ((int) row.size() - 1)) / (int) row.size();
            int height = 0;

            for (auto index : row)
                height = juce::jmax (height, content.panels[(size_t) index]->getPreferredHeight (rowWidth));

            rowHeights.push_back (height);
            panelsHeight += height + panelGap;
        }

        const auto graphHeight = juce::jmax (minGraphHeight, area.getHeight() - panelsHeight);
        content.graph->setBounds (area.removeFromTop (graphHeight));

        for (size_t r = 0; r < content.rows.size(); ++r)
        {
            area.removeFromTop (panelGap);
            auto rowArea = area.removeFromTop (rowHeights[r]);
            const auto& row = content.rows[r];
            const auto rowWidth = (rowArea.getWidth() - panelGap * ((int) row.size() - 1)) / (int) row.size();

            for (size_t i = 0; i < row.size(); ++i)
            {
                content.panels[(size_t) row[i]]->setBounds (i + 1 == row.size() ? rowArea : rowArea.removeFromLeft (rowWidth));
                rowArea.removeFromLeft (panelGap);
            }
        }
    }
}
