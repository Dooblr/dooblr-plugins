#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "RangeLearner.h"

#include <algorithm>
#include <cmath>

namespace params = dooblrvocals::params;
namespace Palette = dooblrvocals::ui::Palette;
using dooblrvocals::ui::ControlPanel;

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

    const char* pageName (int page)
    {
        static const char* names[] { "Pitch", "Harmony", "Gate", "EQ", "Comp", "Limiter", "Delay", "Reverb" };
        return names[page];
    }

    juce::Colour pageColour (int page)
    {
        return page == DooblrVocalsAudioProcessorEditor::pitchPage     ? Palette::accent
             : page == DooblrVocalsAudioProcessorEditor::harmonyPage ? Palette::master
             : page >= DooblrVocalsAudioProcessorEditor::delayPage   ? Palette::fx
                                                                     : Palette::dynamic;
    }

    using K = ControlPanel::Kind;
    using Specs = std::vector<ControlPanel::Spec>;

    juce::String safeFileName (const juce::String& name)
    {
        auto s = juce::File::createLegalFileName (name).replaceCharacter (' ', '-');
        return s.isEmpty() ? juce::String ("vocals") : s;
    }
}

//==============================================================================
ProcessorPowerButton::ProcessorPowerButton (int stageIndex)
    : juce::Button ("power"), stage (stageIndex)
{
    static constexpr const char* ids[] = { "power_gate", "power_eq", "power_dynamics", "power_limiter" };
    static constexpr const char* names[] = { "Gate", "EQ", "Compressor", "Limiter" };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    if (stage == pitchStage)
    {
        setComponentID ("power_pitch");
        setTooltip ("Pitch correction on/off (off: delayed dry signal, no snap)");
        setClickingTogglesState (true);
        return;
    }

    if (stage == harmonyStage)
    {
        setComponentID ("power_harmony");
        setTooltip ("Harmonizer on/off (4 voices, set on the Harmony page)");
        setClickingTogglesState (true);
        return;
    }

    if (stage == delayStage || stage == reverbStage)
    {
        setComponentID (stage == delayStage ? "power_delay" : "power_reverb");
        setTooltip (stage == delayStage ? "Delay on/off (after the processing chain, with the reverb)"
                                        : "Reverb on/off (after the processing chain, with the delay)");
        setClickingTogglesState (true);
        return;
    }

    setComponentID (ids[stage]);
    setTooltip (juce::String (names[stage]) + " on/off");
    setClickingTogglesState (false);
}

void ProcessorPowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto colour = pageColour (stage == pitchStage     ? (int) DooblrVocalsAudioProcessorEditor::pitchPage
                                    : stage == harmonyStage ? (int) DooblrVocalsAudioProcessorEditor::harmonyPage
                                    : stage == delayStage   ? (int) DooblrVocalsAudioProcessorEditor::delayPage
                                    : stage == reverbStage  ? (int) DooblrVocalsAudioProcessorEditor::reverbPage
                                                            : DooblrVocalsAudioProcessorEditor::pageForStage (stage));
    auto circle = getLocalBounds().toFloat().withSizeKeepingCentre ((float) size, (float) size).reduced (1.0f);

    if (down)
        circle = circle.reduced (0.6f);

    if (on)
    {
        g.setColour (colour.withAlpha (highlighted ? 1.0f : 0.92f));
        g.fillEllipse (circle);
    }
    else
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillEllipse (circle);
        g.setColour (Palette::textMuted.withAlpha (highlighted ? 0.85f : 0.5f));
        g.drawEllipse (circle.reduced (0.5f), 1.0f);
    }

    const auto glyph = circle.reduced (circle.getWidth() * 0.27f);
    const auto c = glyph.getCentre();
    const auto r = glyph.getWidth() * 0.5f;
    juce::Path ring;
    ring.addCentredArc (c.x, c.y, r, r, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
    const auto ink = on ? Palette::background : Palette::textMuted.withAlpha (highlighted ? 0.95f : 0.7f);
    g.setColour (ink);
    g.strokePath (ring, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (c.x, glyph.getY() - 1.0f, c.x, c.y, 1.6f);
}

//==============================================================================
void NavButton::setProcessorOn (bool on)
{
    if (processorOn != on)
    {
        processorOn = on;
        repaint();
    }
}

void NavButton::setHasPowerToggle (bool has)
{
    hasPowerToggle = has;
    repaint();
}

juce::Rectangle<int> NavButton::getPowerArea() const
{
    constexpr int size = ProcessorPowerButton::size;

    if (chainSlot < 0)
        return hasPowerToggle ? juce::Rectangle<int> (6 + 3, (getHeight() - size) / 2, size, size) : juce::Rectangle<int>();

    return { 6 + 16 + 3, (getHeight() - size) / 2, size, size };
}

void NavButton::setChainSlot (int slot)
{
    if (chainSlot != slot)
    {
        chainSlot = slot;
        repaint();
    }
}

juce::Rectangle<int> NavButton::getHandleArea() const
{
    return chainSlot < 0 ? juce::Rectangle<int>() : getLocalBounds().removeFromRight (26);
}

void NavButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (chainSlot < 0 && ! hasPowerToggle)
    {
        juce::TextButton::paintButton (g, highlighted, down);
        return;
    }

    auto& lf = getLookAndFeel();
    const auto on = getToggleState();
    lf.drawButtonBackground (g, *this, findColour (on ? buttonOnColourId : buttonColourId),
                             highlighted || handleDrag, down || handleDrag);

    if (! processorOn)
    {
        g.setColour (Palette::background.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }

    auto area = getLocalBounds().reduced (6, 0);
    const auto textColour = findColour (on ? textColourOnId : textColourOffId).withMultipliedAlpha (processorOn ? 1.0f : 0.45f);
    const auto fontSize = juce::jlimit (12.0f, 15.0f, (float) getHeight() * 0.40f);

    if (chainSlot >= 0)
    {
        const auto badge = area.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
        g.setColour (on ? textColour.withAlpha (0.25f) : Palette::background.withAlpha (0.7f));
        g.fillEllipse (badge);
        g.setColour (textColour.withAlpha (0.9f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (chainSlot + 1), badge, juce::Justification::centred);

        const auto handle = getHandleArea().toFloat().withSizeKeepingCentre (12.0f, 10.0f);
        g.setColour (textColour.withAlpha (highlighted || handleDrag ? 0.95f : 0.55f));
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (handle.getX(), handle.getY() + 4.0f * (float) i, handle.getWidth(), 1.6f, 0.8f);

        area.removeFromRight (20);
    }

    area.removeFromLeft (3 + ProcessorPowerButton::size + 5);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (fontSize));
    g.drawFittedText (getButtonText(), area, juce::Justification::centredLeft, 1, 0.8f);
}

void NavButton::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (getHandleArea().contains (e.getPosition()) ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    juce::TextButton::mouseMove (e);
}

void NavButton::mouseDown (const juce::MouseEvent& e)
{
    if (getHandleArea().contains (e.getPosition()))
    {
        handleDrag = true;
        if (onHandleDown) onHandleDown (e);
        repaint();
        return;
    }

    juce::TextButton::mouseDown (e);
}

void NavButton::mouseDrag (const juce::MouseEvent& e)
{
    if (handleDrag)
    {
        if (onHandleDrag) onHandleDrag (e);
        return;
    }

    juce::TextButton::mouseDrag (e);
}

void NavButton::mouseUp (const juce::MouseEvent& e)
{
    if (handleDrag)
    {
        handleDrag = false;
        if (onHandleUp) onHandleUp (e);
        repaint();
        return;
    }

    juce::TextButton::mouseUp (e);
}

//==============================================================================
DooblrVocalsAudioProcessorEditor::DooblrVocalsAudioProcessorEditor (DooblrVocalsAudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      audioProcessor (processorToEdit),
      state (processorToEdit.getParameterState()),
      pitchDisplay (processorToEdit.getParameterState()),
      harmonyDisplay (processorToEdit.getParameterState()),
      gateDisplay (processorToEdit.getParameterState()),
      eqDisplay (processorToEdit.getParameterState()),
      dynamicsDisplay (processorToEdit.getParameterState()),
      limiterDisplay (processorToEdit.getParameterState()),
      delayDisplay (processorToEdit.getParameterState()),
      reverbDisplay (processorToEdit.getParameterState()),
      delayRouting (processorToEdit.getParameterState()),
      reverbRouting (processorToEdit.getParameterState()),
      outputMeter (&processorToEdit.getOutputPeakTap())
{
    setLookAndFeel (&lookAndFeel);

    styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
    styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
    subtitleLabel.setComponentID ("version");
    styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);
    styleLabel (statusLabel, {}, 11.0f, Palette::textMuted, juce::Justification::centredRight);
    statusLabel.setComponentID ("status");

    for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption, &statusLabel })
        addAndMakeVisible (label);

    addAndMakeVisible (outputMeter);

    undoButton.setComponentID ("undo");
    redoButton.setComponentID ("redo");
    undoButton.setTooltip ("Undo (Cmd-Z)");
    redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
    undoButton.onClick = [this] { audioProcessor.getUndoHistory().undo(); updateUndoButtons(); };
    redoButton.onClick = [this] { audioProcessor.getUndoHistory().redo(); updateUndoButtons(); };

    presetBox.setComponentID ("preset");
    presetBox.setTooltip ("Factory and user presets (user presets live in ~/Music/dooblr-vocals/Presets)");
    presetBox.onChange = [this] { presetChosen(); };

    saveButton.setComponentID ("savePreset");
    saveButton.setTooltip ("Save the current settings as a user preset file (.dvocal)");
    saveButton.onClick = [this] { savePreset(); };
    loadButton.setComponentID ("loadPreset");
    loadButton.setTooltip ("Load a user preset file (.dvocal)");
    loadButton.onClick = [this] { loadPreset(); };
    randomButton.setComponentID ("randomise");
    randomButton.setTooltip ("Randomise correction and chain settings (one undo step)");
    randomButton.onClick = [this]
    {
        juce::Random random;
        audioProcessor.randomise (random);
        refreshPresetBox();
    };

    outputSlider.setComponentID ("output_gain");
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
    outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);

    for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &randomButton,
                                (juce::Component*) &outputSlider })
        addAndMakeVisible (c);

    for (int p = 0; p < numPages; ++p)
    {
        auto& button = pageButtons[(size_t) p];
        button.setButtonText (pageName (p));
        button.setComponentID ("page_" + juce::String (pageName (p)));
        button.setRadioGroupId (2001);
        button.setClickingTogglesState (true);
        button.onClick = [this, p] { if (pageButtons[(size_t) p].getToggleState()) selectPage (p); };
        addAndMakeVisible (button);

        if (p == pitchPage)
        {
            pitchPower = std::make_unique<ProcessorPowerButton> (ProcessorPowerButton::pitchStage);
            button.setHasPowerToggle (true);
            button.addAndMakeVisible (*pitchPower);
            pitchPowerAttachment = std::make_unique<ButtonAttachment> (state, "pitch_on", *pitchPower);
            pitchPower->onStateChange = [this] { pageButtons[(size_t) pitchPage].setProcessorOn (pitchPower->getToggleState()); };
        }

        if (p == harmonyPage)
        {
            harmonyPower = std::make_unique<ProcessorPowerButton> (ProcessorPowerButton::harmonyStage);
            button.setHasPowerToggle (true);
            button.setTooltip ("Harmony - up to 4 voices after the correction, before the processing chain");
            button.addAndMakeVisible (*harmonyPower);
            harmonyPowerAttachment = std::make_unique<ButtonAttachment> (state, "harm_on", *harmonyPower);
            harmonyPower->onStateChange = [this]
            {
                pageButtons[(size_t) harmonyPage].setProcessorOn (harmonyPower->getToggleState());
                harmonyDisplay.refresh();
            };
        }

        if (p == delayPage || p == reverbPage)
        {
            const auto isDelay = p == delayPage;
            auto& power = isDelay ? delayPower : reverbPower;
            power = std::make_unique<ProcessorPowerButton> (isDelay ? ProcessorPowerButton::delayStage : ProcessorPowerButton::reverbStage);
            button.setHasPowerToggle (true);
            button.setTooltip (isDelay ? "Delay - tempo-synced or ms; with the reverb in parallel or in series (after the chain)"
                                       : "Reverb - vocal plate / room / hall; with the delay in parallel or in series (after the chain)");
            button.addAndMakeVisible (*power);
            (isDelay ? delayPowerAttachment : reverbPowerAttachment) = std::make_unique<ButtonAttachment> (state, isDelay ? "dly_on" : "rev_on", *power);
            power->onStateChange = [this, p]
            {
                auto* pw = (p == delayPage ? delayPower : reverbPower).get();
                if (pw != nullptr)
                    pageButtons[(size_t) p].setProcessorOn (pw->getToggleState());
            };
        }

        if (p >= gatePage && p <= limiterPage)
        {
            const auto stage = p - gatePage;
            button.setTooltip (juce::String (pageName (p)) + " - drag the handle to move it in the processing chain");
            button.onHandleDown = [this, stage] (const juce::MouseEvent& e) { chainHandleDown (stage, e); };
            button.onHandleDrag = [this, stage] (const juce::MouseEvent& e) { chainHandleDrag (stage, e); };
            button.onHandleUp = [this, stage] (const juce::MouseEvent& e) { chainHandleUp (stage, e); };

            auto& power = powerButtons[(size_t) stage];
            power = std::make_unique<ProcessorPowerButton> (stage);
            power->onClick = [this, stage] { setStageOn (stage, ! isStageOn (stage)); };
            button.addAndMakeVisible (*power);
        }
    }

    shownOrder = chainOrderParameter();

    addChildComponent (pitchDisplay);
    addChildComponent (harmonyDisplay);
    addChildComponent (gateDisplay);
    addChildComponent (eqDisplay);
    addChildComponent (eqDisplay.getPeakHoldButton());
    addChildComponent (dynamicsDisplay);
    addChildComponent (limiterDisplay);
    addChildComponent (delayDisplay);
    addChildComponent (reverbDisplay);
    meterSpectrumScratch.resize (1 << 15);
    meterFrameScratch.resize (1 << 11);
    pitchFrameScratch.resize (1 << 11);

    pagePanels[pitchPage].push_back (std::make_unique<ControlPanel> (state, "Correction", pageColour (pitchPage), Specs {
        { "corr_key", "Key", K::combo }, { "corr_scale", "Scale", K::combo },
        { "retune_speed", "Retune", K::knob }, { "humanize", "Humanize", K::knob }, { "vibrato", "Vibrato", K::knob },
        { "corr_amount", "Amount", K::knob } }));
    pagePanels[pitchPage].push_back (std::make_unique<ControlPanel> (state, "Detector", pageColour (pitchPage), Specs {
        { "input_type", "Range", K::combo, 110 }, { "a4_hz", "A4", K::knob }, { "formant", "Formant", K::toggle, 88 },
        { "quality", "Quality", K::combo, 104 }, { "latency_mode", "Latency", K::combo, 112 } }));
    pagePanels[harmonyPage].push_back (std::make_unique<ControlPanel> (state, "Harmony bus", pageColour (harmonyPage), Specs {
        { "harm_level", "Level", K::knob }, { "harm_formant", "Formant", K::toggle }, { "harm_lofi", "Lo-Fi", K::knob } }));
    pagePanels[gatePage].push_back (std::make_unique<ControlPanel> (state, "Gate", pageColour (gatePage), Specs {
        { "gate_threshold", "Threshold", K::knob }, { "gate_attack", "Attack", K::knob }, { "gate_hold", "Hold", K::knob },
        { "gate_release", "Release", K::knob }, { "gate_range", "Range", K::knob } }));
    pagePanels[eqPage].push_back (std::make_unique<ControlPanel> (state, "EQ", pageColour (eqPage), Specs {
        { "eq_low_freq", "Low freq", K::knob }, { "eq_low_gain", "Low gain", K::knob },
        { "eq_mid_freq", "Mid freq", K::knob }, { "eq_mid_gain", "Mid gain", K::knob }, { "eq_mid_q", "Mid Q", K::knob },
        { "eq_high_freq", "High freq", K::knob }, { "eq_high_gain", "High gain", K::knob } }));
    pagePanels[dynamicsPage].push_back (std::make_unique<ControlPanel> (state, "Compressor", pageColour (dynamicsPage), Specs {
        { "comp_threshold", "Threshold", K::knob }, { "comp_ratio", "Ratio", K::knob },
        { "comp_attack", "Attack", K::knob }, { "comp_release", "Release", K::knob }, { "comp_makeup", "Makeup", K::knob } }));
    pagePanels[limiterPage].push_back (std::make_unique<ControlPanel> (state, "Limiter", pageColour (limiterPage), Specs {
        { "lim_mode", "Mode", K::combo, 0, 1 }, { "lim_ceiling", "Ceiling", K::knob }, { "lim_release", "Release", K::knob } }));

    // v0.3 time FX: Delay and Reverb pages (each with its section presets and the shared routing).
    pagePanels[delayPage].push_back (std::make_unique<ControlPanel> (state, "Delay", pageColour (delayPage), Specs {
        { "dly_mode", "Mode", K::combo, 104 }, { "dly_sync", "Sync", K::toggle, 76 },
        { "dly_note", "Time", K::combo, 84, 0, 0 }, { "dly_time", "Time", K::knob, 0, 0, 0 },
        { "dly_note_r", "Time R", K::combo, 84, 0, 1 }, { "dly_time_r", "Time R", K::knob, 0, 0, 1 },
        { "dly_feedback", "Feedback", K::knob }, { "dly_mod", "Mod", K::knob }, { "dly_drive", "Drive", K::knob } }));
    pagePanels[delayPage].push_back (std::make_unique<ControlPanel> (state, "Tone, ducking and mix", pageColour (delayPage), Specs {
        { "dly_lowcut", "Low cut", K::knob }, { "dly_highcut", "High cut", K::knob }, { "dly_duck", "Duck", K::knob },
        { "dly_duck_release", "Duck rel", K::knob }, { "dly_width", "Width", K::knob }, { "dly_mix", "Mix", K::knob } }));
    pagePanels[reverbPage].push_back (std::make_unique<ControlPanel> (state, "Reverb", pageColour (reverbPage), Specs {
        { "rev_predelay", "Pre-delay", K::knob }, { "rev_predelay_sync", "Pre sync", K::combo, 84 },
        { "rev_decay", "Decay", K::knob }, { "rev_size", "Size", K::knob }, { "rev_damping", "Damping", K::knob },
        { "rev_diffusion", "Diffusion", K::knob }, { "rev_early", "Early/Late", K::knob }, { "rev_mod", "Mod", K::knob } }));
    pagePanels[reverbPage].push_back (std::make_unique<ControlPanel> (state, "Tone, ducking and mix", pageColour (reverbPage), Specs {
        { "rev_lowcut", "Low cut", K::knob }, { "rev_highcut", "High cut", K::knob }, { "rev_width", "Width", K::knob },
        { "rev_duck", "Duck", K::knob }, { "rev_duck_release", "Duck rel", K::knob }, { "rev_mix", "Mix", K::knob } }));

    for (auto [page, box] : { std::pair<int, juce::ComboBox*> { delayPage, &delayPresetBox }, { reverbPage, &reverbPresetBox } })
    {
        const auto& list = page == delayPage ? dooblrvocals::presets::getDelayPresets() : dooblrvocals::presets::getReverbPresets();
        box->setComponentID (page == delayPage ? "delayPresets" : "reverbPresets");
        box->setTextWhenNothingSelected ("Presets");
        box->setTooltip (page == delayPage ? "Delay presets (load only the delay settings, one undo step)"
                                           : "Reverb presets (load only the reverb settings, one undo step)");
        for (int i = 0; i < (int) list.size(); ++i)
            box->addItem (list[(size_t) i].name, i + 1);
        box->onChange = [this, page] { sectionPresetChosen (page); };
        pagePanels[(size_t) page].front()->addExtra (*box, 124, -1, page == delayPage ? "dly_mode" : "rev_predelay");
        pagePanels[(size_t) page].back()->addExtra (page == delayPage ? (juce::Component&) delayRouting : (juce::Component&) reverbRouting,
                                                    dooblrvocals::ui::FxRoutingView::preferredWidth, -1, {}, true);
    }

    // The correction Mode as a large KEYED | CHROMATIC switch at the top of the pitch page.
    modeToggle = std::make_unique<dooblrvocals::ui::SegmentedToggle> (state, "corr_mode", juce::StringArray { "KEYED", "CHROMATIC" },
                                                                      Palette::accent);
    modeToggle->setTooltip ("Keyed: notes snap to the Key and Scale. Chromatic: notes snap to the nearest semitone "
                            "(Key and Scale ignored). One undo step.");
    addChildComponent (*modeToggle);

    // Key Follow (detected key, confidence, Follow / Lock / Sensitivity / Source) under the switch.
    keyFollowStrip = std::make_unique<dooblrvocals::ui::KeyFollowStrip> (state);
    addChildComponent (*keyFollowStrip);

    // Learn Range (Detector panel) and the learned range readout (pitch page bar).
    learnButton.setComponentID ("learnRange");
    learnButton.setClickingTogglesState (false);
    learnButton.setTooltip ("Learn Range: click, sing your lowest to highest notes, click again. Sets the detector "
                            "range (faster, steadier tracking) and the Range type. One undo step.");
    learnButton.onClick = [this] { toggleRangeLearn(); };
    styleLabel (learnLabel, {}, 12.0f, Palette::textPrimary, juce::Justification::centredRight);
    learnLabel.setComponentID ("learnedRange");
    addChildComponent (learnLabel);
    learnResetButton.setComponentID ("learnReset");
    learnResetButton.setTooltip ("Back to the Range type's default detector range (one undo step)");
    learnResetButton.onClick = [this]
    {
        audioProcessor.resetLearnedRange();
        learnMessage = {};
        updateLearnUi();
    };
    addChildComponent (learnResetButton);

    for (auto& panels : pagePanels)
        for (auto& panel : panels)
            addChildComponent (*panel);

    // Latency readout after the Latency switch: the mode in use and what the host compensates.
    styleLabel (latencyLabel, {}, 12.0f, Palette::textPrimary, juce::Justification::centredLeft);
    latencyLabel.setComponentID ("latencyReadout");
    latencyLabel.setTooltip ("Tracking: ~5 ms, for monitoring while recording (slightly rougher onsets).\n"
                             "Mix: 43 ms look-ahead for clean grains, compensated by the host.\n"
                             "HQ (Quality) overrides both and is always used for offline renders.");
    pagePanels[pitchPage][1]->addExtra (latencyLabel, 118);
    pagePanels[pitchPage][1]->addExtra (learnButton, 100);
    updateLatencyLabel();

    if (auto* latencyBox = dynamic_cast<juce::ComboBox*> (pagePanels[pitchPage][1]->findControl ("latency_mode")))
        latencyBox->setTooltip ("Latency: Tracking (near-zero, for live monitoring) or Mix (look-ahead, higher fidelity). "
                                "Quality HQ overrides it.");

    if (auto* modeBox = dynamic_cast<juce::ComboBox*> (pagePanels[limiterPage].front()->findControl ("lim_mode")))
    {
        modeBox->setTextWhenNothingSelected ("Off");
        modeBox->setTooltip ("Limiter mode (switch the limiter on / off with the Limiter tab's toggle)");
    }

    refreshPowerButtonsNow();
    outputAttachment = std::make_unique<SliderAttachment> (state, "output_gain", outputSlider);

    refreshPresetBox();
    selectPage (pitchPage);
    updateUndoButtons();
    audioProcessor.getUndoHistory().getUndoManager().addChangeListener (this);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (940, 600, 2400, 1400);
    setSize (1080, 660);
    startTimerHz (15);
    updateKeyFollowUi();
    meterVBlank = std::make_unique<juce::VBlankAttachment> (this, [this] { pollMeters(); });
}

DooblrVocalsAudioProcessorEditor::~DooblrVocalsAudioProcessorEditor()
{
    meterVBlank.reset();
    if (audioProcessor.isLearningRange())
        audioProcessor.cancelRangeLearn();
    audioProcessor.getUndoHistory().getUndoManager().removeChangeListener (this);
    for (auto& panels : pagePanels)
        panels.clear();
    setLookAndFeel (nullptr);
}

//==============================================================================
void DooblrVocalsAudioProcessorEditor::selectPage (int page)
{
    selectedPage = juce::jlimit (0, numPages - 1, page);
    pageButtons[(size_t) selectedPage].setToggleState (true, juce::dontSendNotification);

    for (int p = 0; p < numPages; ++p)
        for (auto& panel : pagePanels[(size_t) p])
            panel->setVisible (p == selectedPage);

    pitchDisplay.setVisible (selectedPage == pitchPage);
    harmonyDisplay.setVisible (selectedPage == harmonyPage);
    harmonyDisplay.refresh();
    gateDisplay.setVisible (selectedPage == gatePage);
    eqDisplay.setVisible (selectedPage == eqPage);
    eqDisplay.getPeakHoldButton().setVisible (selectedPage == eqPage);
    dynamicsDisplay.setVisible (selectedPage == dynamicsPage);
    limiterDisplay.setVisible (selectedPage == limiterPage);
    delayDisplay.setVisible (selectedPage == delayPage);
    reverbDisplay.setVisible (selectedPage == reverbPage);
    learnLabel.setVisible (selectedPage == pitchPage);
    modeToggle->setVisible (selectedPage == pitchPage);
    keyFollowStrip->setVisible (selectedPage == pitchPage);
    updateLearnUi();

    audioProcessor.getMeterTap().discardAll();
    audioProcessor.getPitchTap().discardAll();
    lastMeterPollMs = juce::Time::getMillisecondCounterHiRes();
    if (selectedPage == pitchPage) pitchDisplay.reset();
    if (selectedPage == gatePage) gateDisplay.reset();
    if (selectedPage == eqPage) eqDisplay.reset();
    if (selectedPage == dynamicsPage) dynamicsDisplay.reset();
    if (selectedPage == limiterPage) limiterDisplay.reset();
    if (selectedPage == delayPage) delayDisplay.refresh (audioProcessor.getDelayDuckDb(), audioProcessor.getHostBpm());
    if (selectedPage == reverbPage) reverbDisplay.refresh (audioProcessor.getReverbDuckDb(), audioProcessor.getHostBpm());

    updateEnablement();
    resized();
    repaint();
}

void DooblrVocalsAudioProcessorEditor::updateLatencyLabel()
{
    namespace dsp = dooblrvocals::dsp;
    dsp::PitchSettings s;
    s.quality = (dsp::QualityMode) juce::roundToInt (state.getRawParameterValue ("quality")->load());
    s.latency = (dsp::LatencyMode) juce::roundToInt (state.getRawParameterValue ("latency_mode")->load());
    const auto mode = dsp::processModeFor (s, false);
    const auto rate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 48000.0;
    const auto ms = 1000.0 * dsp::latencyForMode (mode, rate) / rate;
    const auto text = juce::String (dsp::processModeName (mode)) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                      + (ms < 10.0 ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms";

    if (latencyLabel.getText() != text)
        latencyLabel.setText (text, juce::dontSendNotification);
}

void DooblrVocalsAudioProcessorEditor::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void DooblrVocalsAudioProcessorEditor::updateEnablement()
{
    const auto value = [this] (const juce::String& id)
    {
        auto* param = state.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };

    const auto setEnabled = [this] (int page, const juce::String& id, bool enabled)
    {
        for (auto& panel : pagePanels[(size_t) page])
            if (auto* control = panel->findControl (id))
                control->setEnabled (enabled);
    };

    // Key / scale also drive the harmony voices in Scale mode, so they stay live then.
    const auto chromatic = juce::roundToInt (value ("corr_mode")) != 0;
    const auto harmony = value ("harm_on") > 0.5f;
    setEnabled (pitchPage, "corr_key", ! chromatic || harmony);
    setEnabled (pitchPage, "corr_scale", ! chromatic || harmony);
    // Chromatic: dimmed (but shown). With the harmonizer on they stay live for the voices, so
    // only half dimmed, and the tooltip says why.
    // Key Follow driving the key: the parameters stay editable (they are the fallback, and
    // automation is never overwritten) but half dimmed, and the tooltip names the followed key.
    const auto followed = keyFollowStrip != nullptr ? keyFollowStrip->getView().appliedKey : -1;
    const auto following = value ("key_follow") > 0.5f && followed >= 0;
    for (const auto* id : { "corr_key", "corr_scale" })
    {
        auto& panel = *pagePanels[pitchPage].front();
        panel.setDimmed (id, chromatic || following, chromatic ? (harmony ? 0.6f : 0.38f) : 0.6f);
        if (auto* box = dynamic_cast<juce::ComboBox*> (panel.findControl (id)))
            box->setTooltip (chromatic ? (harmony ? juce::String ("Chromatic: pitch correction ignores this; it still sets the harmony voices' scale")
                                                  : juce::String ("Chromatic: ignored (switch to KEYED to use it)"))
                             : following ? "Key Follow is using " + dooblrvocals::dsp::KeyDetector::keyName (followed)
                                               + "; this is the fallback when Key Follow is off"
                             : (state.getParameter (id) != nullptr ? state.getParameter (id)->getName (64) : juce::String()));
    }
    setEnabled (pitchPage, "latency_mode", juce::roundToInt (value ("quality")) == 0);

    const auto limiter = juce::roundToInt (value ("lim_mode"));
    setEnabled (limiterPage, "lim_mode", limiter > 0);
    setEnabled (limiterPage, "lim_ceiling", limiter > 0);
    setEnabled (limiterPage, "lim_release", limiter == 2);

    // Delay: synced note values or ms share their slots; the R time only in Dual L/R.
    const auto sync = value ("dly_sync") > 0.5f;
    const auto dual = juce::roundToInt (value ("dly_mode")) == (int) dooblrvocals::dsp::DelaySettings::dual;
    auto& delayPanel = *pagePanels[delayPage].front();
    auto changed = delayPanel.setShown ("dly_note", sync);
    changed = delayPanel.setShown ("dly_time", ! sync) || changed;
    changed = delayPanel.setShown ("dly_note_r", sync && dual) || changed;
    changed = delayPanel.setShown ("dly_time_r", ! sync && dual) || changed;
    if (changed)
        delayPanel.resized();

    setEnabled (reverbPage, "rev_predelay", juce::roundToInt (value ("rev_predelay_sync")) == 0);
    setEnabled (reverbPage, "rev_duck_release", value ("rev_duck") > 0.05f);
    setEnabled (delayPage, "dly_duck_release", value ("dly_duck") > 0.05f);
}

void DooblrVocalsAudioProcessorEditor::sectionPresetChosen (int page)
{
    auto& box = page == delayPage ? delayPresetBox : reverbPresetBox;
    const auto& list = page == delayPage ? dooblrvocals::presets::getDelayPresets() : dooblrvocals::presets::getReverbPresets();
    const auto index = box.getSelectedId() - 1;

    if (index < 0 || index >= (int) list.size())
        return;

    namespace presets = dooblrvocals::presets;
    presets::apply (state, presets::resolveSection (state, page == delayPage ? "dly_" : "rev_", list[(size_t) index].values), true);
    setStatus ((page == delayPage ? "Delay: " : "Reverb: ") + list[(size_t) index].name);
    updateEnablement();
}

void DooblrVocalsAudioProcessorEditor::toggleRangeLearn()
{
    if (! audioProcessor.isLearningRange())
    {
        audioProcessor.startRangeLearn();
        learnMessage = {};
    }
    else
    {
        const auto result = audioProcessor.stopRangeLearn();
        learnMessage = result.valid ? juce::String() : result.message;
        if (result.valid)
            setStatus ("Learned range " + dooblrvocals::RangeLearner::rangeText (result.lowHz, result.highHz)
                       + " (" + juce::String (result.voicedSeconds, 1) + " s of voice)");
    }

    updateLearnUi();
}

void DooblrVocalsAudioProcessorEditor::updateLearnPulse (double nowMs)
{
    // One sine period about every second, quantised to 1/64 of the swing so an unchanged
    // colour never repaints the button.
    const auto phase = 0.5f + 0.5f * std::sin ((float) std::fmod (nowMs * 0.006, juce::MathConstants<double>::twoPi));
    const auto alpha = 0.35f + 0.45f * std::round (phase * 64.0f) / 64.0f;
    const auto colour = Palette::frozen.withAlpha (alpha);

    if (! learnButton.isColourSpecified (juce::TextButton::buttonColourId)
        || learnButton.findColour (juce::TextButton::buttonColourId) != colour)
        learnButton.setColour (juce::TextButton::buttonColourId, colour);
}

void DooblrVocalsAudioProcessorEditor::updateLearnUi()
{
    const auto learning = audioProcessor.isLearningRange();
    const auto& learner = audioProcessor.getRangeLearner();
    juce::String buttonText = "Learn Range", labelText;

    if (learning)
    {
        const auto hz = learner.getLastVoicedHz();
        buttonText = "Stop" + (hz > 0.0f ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + params::noteName (juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0)))
                                         : juce::String());
        labelText = "Listening - sing low to high, then Stop (" + juce::String (learner.getVoicedSeconds(), 1) + " s voiced)";
        // Pulse while armed: the VBlank drives it (updateLearnPulse); this only sets the first frame.
        if (! learnButton.isColourSpecified (juce::TextButton::buttonColourId))
            updateLearnPulse (juce::Time::getMillisecondCounterHiRes());
        learnButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
    }
    else
    {
        learnButton.removeColour (juce::TextButton::buttonColourId);
        learnButton.removeColour (juce::TextButton::textColourOffId);

        if (learnMessage.isNotEmpty())
            labelText = learnMessage;
        else if (state.getRawParameterValue ("range_custom")->load() > 0.5f)
            labelText = "Detector range " + dooblrvocals::RangeLearner::rangeText (state.getRawParameterValue ("range_low")->load(),
                                                                                    state.getRawParameterValue ("range_high")->load())
                        + " (learned)";
    }

    if (learnButton.getButtonText() != buttonText)
        learnButton.setButtonText (buttonText);
    learnButton.getProperties().set ("armed", learning);
    if (learnLabel.getText() != labelText)
        learnLabel.setText (labelText, juce::dontSendNotification);
    learnLabel.setColour (juce::Label::textColourId, learnMessage.isNotEmpty() && ! learning ? Palette::dynamic : Palette::textPrimary);
    learnResetButton.setVisible (selectedPage == pitchPage && ! learning && state.getRawParameterValue ("range_custom")->load() > 0.5f);
}

//==============================================================================
namespace
{
    const juce::Identifier limiterResumeProperty { "powerResumeLimiter" };
}

bool DooblrVocalsAudioProcessorEditor::isStageOn (int stage) const
{
    const auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    switch (stage)
    {
        case dooblrvocals::dsp::gateStage:      return value ("gate_on") > 0.5f;
        case dooblrvocals::dsp::eqStage:        return value ("eq_on") > 0.5f;
        case dooblrvocals::dsp::dynamicsStage:  return value ("comp_on") > 0.5f;
        case dooblrvocals::dsp::limiterStage:   return juce::roundToInt (value ("lim_mode")) != 0;
        default: return false;
    }
}

void DooblrVocalsAudioProcessorEditor::setStageOn (int stage, bool on)
{
    dooblrvocals::ui::ParameterGesture gesture (state);

    switch (stage)
    {
        case dooblrvocals::dsp::gateStage:
            gesture.begin ({ "gate_on" });
            gesture.set ("gate_on", on ? 1.0f : 0.0f);
            break;
        case dooblrvocals::dsp::eqStage:
            gesture.begin ({ "eq_on" });
            gesture.set ("eq_on", on ? 1.0f : 0.0f);
            break;
        case dooblrvocals::dsp::dynamicsStage:
            gesture.begin ({ "comp_on" });
            gesture.set ("comp_on", on ? 1.0f : 0.0f);
            break;
        case dooblrvocals::dsp::limiterStage:
        {
            gesture.begin ({ "lim_mode" });
            if (on)
            {
                const auto mode = (int) state.state.getProperty (limiterResumeProperty, 2);
                gesture.set ("lim_mode", (float) (mode == 1 ? 1 : 2));
            }
            else
            {
                if (const auto mode = juce::roundToInt (gesture.get ("lim_mode")); mode != 0)
                    state.state.setProperty (limiterResumeProperty, mode, nullptr);
                gesture.set ("lim_mode", 0.0f);
            }
            break;
        }
        default: break;
    }

    gesture.end();
    refreshPowerButtonsNow();
    updateEnablement();
}

void DooblrVocalsAudioProcessorEditor::refreshPowerButtonsNow()
{
    for (int stage = 0; stage < dooblrvocals::dsp::numChainStages; ++stage)
    {
        const auto on = isStageOn (stage);
        powerButtons[(size_t) stage]->setToggleState (on, juce::dontSendNotification);
        pageButtons[(size_t) pageForStage (stage)].setProcessorOn (on);
    }

    if (pitchPower != nullptr)
        pageButtons[(size_t) pitchPage].setProcessorOn (pitchPower->getToggleState());
    if (harmonyPower != nullptr)
        pageButtons[(size_t) harmonyPage].setProcessorOn (harmonyPower->getToggleState());
}

//==============================================================================
void DooblrVocalsAudioProcessorEditor::refreshPresetBox()
{
    const juce::ScopedValueSetter<bool> guard (updatingPresetBox, true);
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");

    const auto& factory = dooblrvocals::presets::getFactoryPresets();

    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, factoryIdBase + i);

    userPresetFiles.clear();
    const auto folder = dooblrvocals::presets::getUserPresetFolder();

    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + dooblrvocals::presets::userPresetExtension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        if (! files.isEmpty())
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading ("User");

            for (int i = 0; i < files.size(); ++i)
            {
                userPresetFiles.push_back (files[i]);
                presetBox.addItem (files[i].getFileNameWithoutExtension(), userIdBase + i);
            }
        }
    }

    const auto name = audioProcessor.getPresetName();
    auto selected = false;

    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == name)
        {
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
            selected = true;
            break;
        }

    if (! selected)
        presetBox.setText (name, juce::dontSendNotification);
}

void DooblrVocalsAudioProcessorEditor::presetChosen()
{
    if (updatingPresetBox)
        return;

    const auto id = presetBox.getSelectedId();

    if (id >= userIdBase && id - userIdBase < (int) userPresetFiles.size())
    {
        juce::String error;
        if (! audioProcessor.loadUserPreset (userPresetFiles[(size_t) (id - userIdBase)], error))
            setStatus ("Could not load preset: " + error);
        else
            setStatus ("Loaded " + userPresetFiles[(size_t) (id - userIdBase)].getFileNameWithoutExtension());
    }
    else if (id >= factoryIdBase)
    {
        audioProcessor.loadFactoryPreset (id - factoryIdBase, true);
        setStatus ({});
    }

    selectPage (selectedPage);
}

void DooblrVocalsAudioProcessorEditor::savePreset()
{
    const auto folder = dooblrvocals::presets::getUserPresetFolder();
    folder.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-vocals preset",
                                                       folder.getChildFile (safeFileName (audioProcessor.getPresetName()) + dooblrvocals::presets::userPresetExtension),
                                                       juce::String ("*") + dooblrvocals::presets::userPresetExtension);
    juce::Component::SafePointer<DooblrVocalsAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || chooser.getResult() == juce::File())
                                      return;

                                  auto file = chooser.getResult().withFileExtension (dooblrvocals::presets::userPresetExtension);
                                  juce::String error;

                                  if (! safeThis->audioProcessor.saveUserPreset (file, error))
                                      safeThis->setStatus ("Could not save preset: " + error);
                                  else
                                      safeThis->setStatus ("Saved " + file.getFileName());

                                  safeThis->refreshPresetBox();
                              });
}

void DooblrVocalsAudioProcessorEditor::loadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-vocals preset",
                                                       dooblrvocals::presets::getUserPresetFolder(),
                                                       juce::String ("*") + dooblrvocals::presets::userPresetExtension);
    juce::Component::SafePointer<DooblrVocalsAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                      return;

                                  juce::String error;

                                  if (! safeThis->audioProcessor.loadUserPreset (chooser.getResult(), error))
                                      safeThis->setStatus ("Could not load preset: " + error);
                                  else
                                      safeThis->setStatus ("Loaded " + chooser.getResult().getFileNameWithoutExtension());

                                  safeThis->refreshPresetBox();
                                  safeThis->selectPage (safeThis->selectedPage);
                              });
}

void DooblrVocalsAudioProcessorEditor::pollMeters()
{
    if (! isShowing())
    {
        audioProcessor.getMeterTap().discardAll();
        audioProcessor.getPitchTap().discardAll();
        return;
    }

    if (audioProcessor.isLearningRange())
        updateLearnPulse (juce::Time::getMillisecondCounterHiRes());

    readMeters();
}

void DooblrVocalsAudioProcessorEditor::readMeters()
{
    auto& tap = audioProcessor.getMeterTap();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jlimit (0.0, 0.1, (now - lastMeterPollMs) * 0.001);
    lastMeterPollMs = now;

    if (selectedPage == pitchPage)
    {
        tap.discardAll();
        const auto n = audioProcessor.getPitchTap().pop (pitchFrameScratch.data(), (int) pitchFrameScratch.size());
        pitchDisplay.update (pitchFrameScratch.data(), n, elapsed);
        return;
    }

    while (audioProcessor.getPitchTap().pop (pitchFrameScratch.data(), (int) pitchFrameScratch.size()) > 0) {}

    if (selectedPage == harmonyPage || selectedPage >= delayPage)
    {
        tap.discardAll();
        return;
    }

    if (selectedPage == eqPage)
    {
        tap.popFrames (meterFrameScratch.data(), (int) meterFrameScratch.size());
        const auto n = tap.popSpectrum (meterSpectrumScratch.data(), (int) meterSpectrumScratch.size());
        eqDisplay.update (meterSpectrumScratch.data(), n, tap.getSampleRate(), elapsed);
        return;
    }

    while (tap.popSpectrum (meterSpectrumScratch.data(), (int) meterSpectrumScratch.size()) > 0) {}
    const auto frames = tap.popFrames (meterFrameScratch.data(), (int) meterFrameScratch.size());

    if (selectedPage == gatePage)
        gateDisplay.update (meterFrameScratch.data(), frames, elapsed);
    else if (selectedPage == dynamicsPage)
        dynamicsDisplay.update (meterFrameScratch.data(), frames, elapsed);
    else
        limiterDisplay.update (meterFrameScratch.data(), frames, elapsed);
}

void DooblrVocalsAudioProcessorEditor::updateKeyFollowUi()
{
    const auto status = audioProcessor.getKeyFollowStatus();
    dooblrvocals::ui::KeyFollowView view;
    view.detectedKey = status.detectedKey;
    view.candidateKey = status.candidateKey;
    view.appliedKey = status.appliedKey;
    view.confidence = status.confidence;
    view.activeSource = status.activeSource;
    view.sidechainConnected = status.sidechainConnected;
    view.sidechainSignal = status.sidechainSignal;
    keyFollowStrip->refresh (view);
    pitchDisplay.setFollowedKey (status.appliedKey);
    harmonyDisplay.setFollowedKey (status.appliedKey);
}

void DooblrVocalsAudioProcessorEditor::timerCallback()
{
    updateKeyFollowUi();
    updateEnablement();
    refreshPowerButtonsNow();
    syncChainOrderNow();
    updateLatencyLabel();
    if (selectedPage == harmonyPage)
        harmonyDisplay.refresh();
    if (selectedPage == delayPage)
        delayDisplay.refresh (audioProcessor.getDelayDuckDb(), audioProcessor.getHostBpm());
    if (selectedPage == reverbPage)
        reverbDisplay.refresh (audioProcessor.getReverbDuckDb(), audioProcessor.getHostBpm());

    if (audioProcessor.isLearningRange())
        audioProcessor.pollRangeLearn();
    updateLearnUi();

    if (presetBox.getText() != audioProcessor.getPresetName() && ! presetBox.isPopupActive())
        refreshPresetBox();
}

void DooblrVocalsAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateUndoButtons();
}

void DooblrVocalsAudioProcessorEditor::updateUndoButtons()
{
    auto& history = audioProcessor.getUndoHistory();
    undoButton.setEnabled (history.canUndo());
    redoButton.setEnabled (history.canRedo());
}

bool DooblrVocalsAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    auto& history = audioProcessor.getUndoHistory();

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

//==============================================================================
void DooblrVocalsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::panel);
    g.fillRect (headerArea);
    g.fillRect (navArea);
    g.fillRect (controlsArea);

    g.setColour (Palette::divider);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
    g.fillRect (navArea.getRight() - 1, navArea.getY(), 1, navArea.getHeight());
    g.fillRect (controlsArea.getX(), controlsArea.getY(), controlsArea.getWidth(), 1);

    g.setColour (Palette::accent);
    g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (Palette::textMuted);
    g.drawText ("CORRECTION", correctionTitleArea, juce::Justification::centredLeft);
    g.drawText ("PROCESSING", fxTitleArea, juce::Justification::centredLeft);
    g.drawText ("TIME FX", timeFxTitleArea, juce::Justification::centredLeft);

    g.setColour (pageColour (selectedPage));
    g.drawText (selectedPage == pitchPage ? "PITCH"
                : selectedPage == harmonyPage ? "HARMONY"
                : selectedPage == gatePage ? "GATE"
                : selectedPage == eqPage ? "OUTPUT EQ"
                : selectedPage == dynamicsPage ? "COMPRESSOR"
                : selectedPage == limiterPage ? "LIMITER"
                : selectedPage == delayPage ? "DELAY" : "REVERB",
                graphTitleArea, juce::Justification::centredLeft);
}

void DooblrVocalsAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    headerArea = bounds.removeFromTop (60);
    {
        auto header = headerArea.reduced (32, 10);
        constexpr int fixedLeft = 8 + 50 + 4 + 50 + 12 + 4 + 46 + 4 + 46 + 8 + 64;
        constexpr int fixedRight = meterWidth + 12, captionWidth = 52;
        const auto flexible = header.getWidth() - fixedLeft - fixedRight - captionWidth;
        auto spare = juce::jmax (0, flexible - 88 - 110 - 120);
        auto sliderWidth = 88 + juce::jmin (24, spare);
        spare -= sliderWidth - 88;
        const auto titleWidth = 120 + juce::jmin (52, spare);
        spare -= titleWidth - 120;
        const auto presetWidth = 110 + juce::jmin (60, spare);
        spare -= presetWidth - 110;
        sliderWidth = juce::jmin (200, sliderWidth + spare);

        auto titleColumn = header.removeFromLeft (titleWidth);
        titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
        subtitleLabel.setBounds (titleColumn);

        header.removeFromLeft (8);
        undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
        header.removeFromLeft (4);
        redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
        header.removeFromLeft (12);

        presetBox.setBounds (header.removeFromLeft (presetWidth).withSizeKeepingCentre (presetWidth, 26));
        header.removeFromLeft (4);
        saveButton.setBounds (header.removeFromLeft (46).withSizeKeepingCentre (46, 24));
        header.removeFromLeft (4);
        loadButton.setBounds (header.removeFromLeft (46).withSizeKeepingCentre (46, 24));
        header.removeFromLeft (8);
        randomButton.setBounds (header.removeFromLeft (64).withSizeKeepingCentre (64, 24));

        outputMeter.setBounds (header.removeFromRight (meterWidth));
        header.removeFromRight (12);
        outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
        outputCaption.setBounds (header.removeFromRight (52));
    }

    navArea = bounds.removeFromLeft (150);
    {
        auto nav = navArea.reduced (12, 12);
        constexpr int titleHeight = 20, groupGap = 10;
        const auto pitch = juce::jmax (28, (nav.getHeight() - 3 * titleHeight - 2 * groupGap) / numPages);
        const auto gap = juce::jlimit (2, 4, pitch / 14);
        correctionTitleArea = nav.removeFromTop (titleHeight);

        for (int p = 0; p < numPages; ++p)
        {
            if (p == gatePage)
            {
                nav.removeFromTop (groupGap);
                fxTitleArea = nav.removeFromTop (titleHeight);
            }

            if (p == delayPage)
            {
                nav.removeFromTop (groupGap);
                timeFxTitleArea = nav.removeFromTop (titleHeight);
            }

            const auto slot = nav.removeFromTop (pitch).reduced (0, gap);

            if (p >= gatePage && p <= limiterPage)
            {
                chainSlotBounds[(size_t) (p - gatePage)] = slot;
                continue;
            }

            auto& button = pageButtons[(size_t) p];
            button.setBounds (slot);
            if (p == pitchPage)
                pitchPower->setBounds (button.getPowerArea());
            if (p == harmonyPage)
                harmonyPower->setBounds (button.getPowerArea());
            if (p == delayPage)
                delayPower->setBounds (button.getPowerArea());
            if (p == reverbPage)
                reverbPower->setBounds (button.getPowerArea());
        }

        layoutChainButtons();
    }

    auto content = bounds;
    const auto controlsWidth = content.getWidth() - 40;
    auto panelsHeight = 0;

    for (auto& panel : pagePanels[(size_t) selectedPage])
        panelsHeight += panel->getPreferredHeight (controlsWidth) + 10;

    controlsArea = content.removeFromBottom (juce::jmin (panelsHeight + 14, content.getHeight() - 200));
    {
        auto controls = controlsArea.reduced (20, 10);
        for (auto& panel : pagePanels[(size_t) selectedPage])
        {
            panel->setBounds (controls.removeFromTop (panel->getPreferredHeight (controlsWidth)));
            controls.removeFromTop (10);
        }
    }

    displayArea = content.reduced (16, 10);
    {
        auto display = displayArea;
        const auto pitch = selectedPage == pitchPage;
        // Pitch page: a taller bar holding the KEYED | CHROMATIC switch right after the title.
        const auto toggleHeight = juce::jlimit (dooblrvocals::ui::SegmentedToggle::minHeight + 2, 40, getHeight() / 16);
        auto bar = display.removeFromTop (pitch ? toggleHeight + 4 : 24);
        graphTitleArea = bar.removeFromLeft (pitch ? 58 : 180);
        if (pitch)
        {
            const auto toggleWidth = juce::jlimit (dooblrvocals::ui::SegmentedToggle::minWidth + 8, 300, bar.getWidth() / 3);
            modeToggle->setBounds (bar.removeFromLeft (toggleWidth).withSizeKeepingCentre (toggleWidth, toggleHeight));
            bar.removeFromLeft (14);
        }
        statusLabel.setBounds (bar.removeFromRight (juce::jmin (pitch ? 220 : 280, bar.getWidth())).withSizeKeepingCentre (
            juce::jmin (pitch ? 220 : 280, bar.getWidth()), 20));
        if (pitch)
        {
            // The learned range (or the learn message) and its reset, between the switch and the status.
            learnResetButton.setBounds (bar.removeFromRight (54).withSizeKeepingCentre (54, 22));
            bar.removeFromRight (6);
            learnLabel.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 20));
            if (audioProcessor.isLearningRange() || learnMessage.isNotEmpty())
                statusLabel.setBounds (statusLabel.getBounds().withWidth (0));
        }
        eqDisplay.getPeakHoldButton().setBounds (bar.withLeft (bar.getRight() - 96).reduced (0, 1));
        display.removeFromTop (4);
        if (pitch)
        {
            keyFollowStrip->setBounds (display.removeFromTop (dooblrvocals::ui::KeyFollowStrip::preferredHeight));
            display.removeFromTop (6);
        }
        pitchDisplay.setBounds (display);
        harmonyDisplay.setBounds (display);
        gateDisplay.setBounds (display);
        eqDisplay.setBounds (display);
        dynamicsDisplay.setBounds (display);
        limiterDisplay.setBounds (display);
        delayDisplay.setBounds (display);
        reverbDisplay.setBounds (display);
    }
}

dooblrvocals::dsp::ChainOrder DooblrVocalsAudioProcessorEditor::chainOrderParameter() const
{
    return dooblrvocals::dsp::chainOrderFromIndex (juce::roundToInt (state.getRawParameterValue ("chain_order")->load()));
}

void DooblrVocalsAudioProcessorEditor::syncChainOrderNow()
{
    if (draggingStage < 0 && chainOrderParameter() != shownOrder)
    {
        shownOrder = chainOrderParameter();
        layoutChainButtons();
    }
}

void DooblrVocalsAudioProcessorEditor::layoutChainButtons()
{
    for (int slot = 0; slot < dooblrvocals::dsp::numChainStages; ++slot)
    {
        const auto stage = shownOrder[(size_t) slot];
        auto& button = pageButtons[(size_t) pageForStage (stage)];
        button.setChainSlot (slot);
        auto bounds = chainSlotBounds[(size_t) slot];

        if (stage == draggingStage)
        {
            const auto top = chainSlotBounds.front().getY(), bottom = chainSlotBounds.back().getY();
            bounds.setY (juce::jlimit (top, bottom, dragY - dragGrabOffset));
            button.toFront (false);
        }

        button.setBounds (bounds);
        powerButtons[(size_t) stage]->setBounds (button.getPowerArea());
    }
}

void DooblrVocalsAudioProcessorEditor::chainHandleDown (int stage, const juce::MouseEvent& e)
{
    shownOrder = chainOrderParameter();
    draggingStage = stage;
    const auto local = e.getEventRelativeTo (this).getPosition();
    dragGrabOffset = local.y - pageButtons[(size_t) pageForStage (stage)].getY();
    dragY = local.y;

    if (auto* p = state.getParameter ("chain_order"))
        p->beginChangeGesture();
}

void DooblrVocalsAudioProcessorEditor::chainHandleDrag (int stage, const juce::MouseEvent& e)
{
    if (stage != draggingStage)
        return;

    dragY = e.getEventRelativeTo (this).getPosition().y;

    const auto first = chainSlotBounds.front();
    const auto pitchY = (float) (chainSlotBounds[1].getY() - first.getY());
    const auto centre = (float) (dragY - dragGrabOffset) + (float) first.getHeight() * 0.5f;
    const auto target = juce::jlimit (0, dooblrvocals::dsp::numChainStages - 1,
                                      (int) std::floor ((centre - (float) first.getY() + 0.5f * (pitchY - (float) first.getHeight())) / pitchY));

    auto order = shownOrder;
    const auto from = (int) (std::find (order.begin(), order.end(), stage) - order.begin());

    if (from != target)
    {
        auto moved = order[(size_t) from];
        if (from < target) std::rotate (order.begin() + from, order.begin() + from + 1, order.begin() + target + 1);
        else               std::rotate (order.begin() + target, order.begin() + from, order.begin() + from + 1);
        order[(size_t) target] = moved;
        shownOrder = order;

        if (auto* p = state.getParameter ("chain_order"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) dooblrvocals::dsp::chainOrderToIndex (order)));
    }

    layoutChainButtons();
}

void DooblrVocalsAudioProcessorEditor::chainHandleUp (int stage, const juce::MouseEvent&)
{
    if (stage != draggingStage)
        return;

    draggingStage = -1;

    if (auto* p = state.getParameter ("chain_order"))
        p->endChangeGesture();

    shownOrder = chainOrderParameter();
    layoutChainButtons();
}
