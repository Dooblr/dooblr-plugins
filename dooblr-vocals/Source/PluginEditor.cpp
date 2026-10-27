#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"

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
        static const char* names[] { "Pitch", "Harmony", "Gate", "EQ", "Comp", "Limiter" };
        return names[page];
    }

    juce::Colour pageColour (int page)
    {
        return page == DooblrVocalsAudioProcessorEditor::pitchPage     ? Palette::accent
             : page == DooblrVocalsAudioProcessorEditor::harmonyPage ? Palette::master
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

    setComponentID (ids[stage]);
    setTooltip (juce::String (names[stage]) + " on/off");
    setClickingTogglesState (false);
}

void ProcessorPowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto colour = pageColour (stage == pitchStage     ? (int) DooblrVocalsAudioProcessorEditor::pitchPage
                                    : stage == harmonyStage ? (int) DooblrVocalsAudioProcessorEditor::harmonyPage
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

        if (p >= gatePage)
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
    meterSpectrumScratch.resize (1 << 15);
    meterFrameScratch.resize (1 << 11);
    pitchFrameScratch.resize (1 << 11);

    pagePanels[pitchPage].push_back (std::make_unique<ControlPanel> (state, "Correction", pageColour (pitchPage), Specs {
        { "corr_mode", "Mode", K::combo }, { "corr_key", "Key", K::combo }, { "corr_scale", "Scale", K::combo },
        { "retune_speed", "Retune", K::knob }, { "humanize", "Humanize", K::knob }, { "corr_amount", "Amount", K::knob } }));
    pagePanels[pitchPage].push_back (std::make_unique<ControlPanel> (state, "Detector", pageColour (pitchPage), Specs {
        { "input_type", "Range", K::combo }, { "a4_hz", "A4", K::knob }, { "formant", "Formant", K::toggle },
        { "quality", "Quality", K::combo }, { "latency_mode", "Latency", K::combo, 112 } }));
    pagePanels[harmonyPage].push_back (std::make_unique<ControlPanel> (state, "Harmony bus", pageColour (harmonyPage), Specs {
        { "harm_level", "Level", K::knob }, { "harm_formant", "Formant", K::toggle } }));
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

    for (auto& panels : pagePanels)
        for (auto& panel : panels)
            addChildComponent (*panel);

    // Latency readout after the Latency switch: the mode in use and what the host compensates.
    styleLabel (latencyLabel, {}, 12.0f, Palette::textPrimary, juce::Justification::centredLeft);
    latencyLabel.setComponentID ("latencyReadout");
    latencyLabel.setTooltip ("Tracking: ~5 ms, for monitoring while recording (slightly rougher onsets).\n"
                             "Mix: 43 ms look-ahead for clean grains, compensated by the host.\n"
                             "HQ (Quality) overrides both and is always used for offline renders.");
    pagePanels[pitchPage][1]->addExtra (latencyLabel, 130);
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
    meterVBlank = std::make_unique<juce::VBlankAttachment> (this, [this] { pollMeters(); });
}

DooblrVocalsAudioProcessorEditor::~DooblrVocalsAudioProcessorEditor()
{
    meterVBlank.reset();
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

    audioProcessor.getMeterTap().discardAll();
    audioProcessor.getPitchTap().discardAll();
    lastMeterPollMs = juce::Time::getMillisecondCounterHiRes();
    if (selectedPage == pitchPage) pitchDisplay.reset();
    if (selectedPage == gatePage) gateDisplay.reset();
    if (selectedPage == eqPage) eqDisplay.reset();
    if (selectedPage == dynamicsPage) dynamicsDisplay.reset();
    if (selectedPage == limiterPage) limiterDisplay.reset();

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
    setEnabled (pitchPage, "latency_mode", juce::roundToInt (value ("quality")) == 0);

    const auto limiter = juce::roundToInt (value ("lim_mode"));
    setEnabled (limiterPage, "lim_mode", limiter > 0);
    setEnabled (limiterPage, "lim_ceiling", limiter > 0);
    setEnabled (limiterPage, "lim_release", limiter == 2);
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

    if (selectedPage == harmonyPage)
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

void DooblrVocalsAudioProcessorEditor::timerCallback()
{
    updateEnablement();
    refreshPowerButtonsNow();
    syncChainOrderNow();
    updateLatencyLabel();
    if (selectedPage == harmonyPage)
        harmonyDisplay.refresh();

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

    g.setColour (pageColour (selectedPage));
    g.drawText (selectedPage == pitchPage ? "PITCH"
                : selectedPage == harmonyPage ? "HARMONY"
                : selectedPage == gatePage ? "GATE"
                : selectedPage == eqPage ? "OUTPUT EQ"
                : selectedPage == dynamicsPage ? "COMPRESSOR" : "LIMITER",
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
        const auto pitch = juce::jmax (28, (nav.getHeight() - 2 * titleHeight - groupGap) / numPages);
        const auto gap = juce::jlimit (2, 4, pitch / 14);
        correctionTitleArea = nav.removeFromTop (titleHeight);

        for (int p = 0; p < numPages; ++p)
        {
            if (p == gatePage)
            {
                nav.removeFromTop (groupGap);
                fxTitleArea = nav.removeFromTop (titleHeight);
            }

            const auto slot = nav.removeFromTop (pitch).reduced (0, gap);

            if (p >= gatePage)
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
        auto bar = display.removeFromTop (24);
        graphTitleArea = bar.removeFromLeft (180);
        statusLabel.setBounds (bar.removeFromRight (juce::jmin (280, bar.getWidth())).reduced (0, 2));
        eqDisplay.getPeakHoldButton().setBounds (bar.withLeft (bar.getRight() - 96).reduced (0, 1));
        display.removeFromTop (4);
        pitchDisplay.setBounds (display);
        harmonyDisplay.setBounds (display);
        gateDisplay.setBounds (display);
        eqDisplay.setBounds (display);
        dynamicsDisplay.setBounds (display);
        limiterDisplay.setBounds (display);
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
