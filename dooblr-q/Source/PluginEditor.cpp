#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>

namespace params = dooblrq::params;
namespace Palette = dooblrq::ui::Palette;

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
}

//==============================================================================
BandTabSwitch::BandTabSwitch (int bandIndexToUse)
    : bandIndex (bandIndexToUse)
{
    setComponentID ("bandSwitch" + juce::String (bandIndex + 1));
    setTooltip ("Band " + juce::String (bandIndex + 1) + " on/off");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void BandTabSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (down);

    const auto on = getToggleState();
    const auto colour = dooblrq::ui::bandColour (bandIndex);
    const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre ((float) switchWidth, (float) switchHeight).reduced (0.5f);
    const auto radius = pill.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (colour.withAlpha (highlighted ? 1.0f : 0.9f));
        g.fillRoundedRectangle (pill, radius);
    }
    else
    {
        g.setColour (Palette::background.withAlpha (0.6f));
        g.fillRoundedRectangle (pill, radius);
        g.setColour (Palette::textMuted.withAlpha (highlighted ? 0.8f : 0.45f));
        g.drawRoundedRectangle (pill, radius, 1.0f);
    }

    const auto knobSize = pill.getHeight() - 4.0f;
    const auto knob = juce::Rectangle<float> (knobSize, knobSize)
                          .withCentre ({ on ? pill.getRight() - radius : pill.getX() + radius, pill.getCentreY() });
    g.setColour (on ? Palette::background : Palette::textMuted.withAlpha (highlighted ? 0.9f : 0.6f));
    g.fillEllipse (knob);
}

//==============================================================================
BandSoloButton::BandSoloButton (const juce::String& componentId)
{
    setComponentID (componentId);
    setClickingTogglesState (false); // the editor sets the parameter (see onSoloClick)
    setTooltip ("Solo: hear only this band's region (alt/option-click: solo this band only)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void BandSoloButton::clicked (const juce::ModifierKeys& modifiers)
{
    if (onSoloClick != nullptr)
        onSoloClick (modifiers.isAltDown());
}

void BandSoloButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (down);

    const auto on = getToggleState();
    const auto usable = isEnabled();
    const auto box = getLocalBounds().toFloat().withSizeKeepingCentre ((float) boxWidth, (float) boxHeight).reduced (0.5f);

    if (on && usable)
    {
        g.setColour (Palette::dynamic.withAlpha (highlighted ? 1.0f : 0.92f));
        g.fillRoundedRectangle (box, 3.0f);
    }
    else
    {
        g.setColour (Palette::background.withAlpha (0.6f));
        g.fillRoundedRectangle (box, 3.0f);
        // A Solo left on in a disabled band is shown as a faint amber outline (it is ignored).
        g.setColour (on ? Palette::dynamic.withAlpha (0.35f)
                        : Palette::textMuted.withAlpha (! usable ? 0.2f : (highlighted ? 0.8f : 0.45f)));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
    }

    g.setColour (on && usable ? Palette::background : Palette::textMuted.withAlpha (usable ? 1.0f : 0.35f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("S", box, juce::Justification::centred);
}

//==============================================================================
DooblrQAudioProcessorEditor::DooblrQAudioProcessorEditor (DooblrQAudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      audioProcessor (processorToEdit),
      responseCurve (processorToEdit)
{
    setLookAndFeel (&lookAndFeel);

    styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
    styleLabel (subtitleLabel, "Dynamic parametric EQ", 13.0f, Palette::textMuted, juce::Justification::centredLeft);
    styleLabel (versionLabel, "v" JucePlugin_VersionString, 12.0f, Palette::textMuted, juce::Justification::centredRight);
    versionLabel.setComponentID ("version");
    styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &subtitleLabel, &versionLabel, &outputCaption })
        addAndMakeVisible (label);

    outputSlider.setComponentID (params::outputGain);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
    outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
    addAndMakeVisible (outputSlider);

    stereoLinkButton.setComponentID (params::stereoLink);
    stereoLinkButton.setTooltip ("Detect dynamics from both channels together");
    addAndMakeVisible (stereoLinkButton);

    phaseModeBox.setComponentID (params::phaseMode);
    phaseModeBox.addItemList (params::phaseModeNames(), 1);
    phaseModeBox.setTooltip ("Zero Latency: minimum-phase filters, no delay. Natural Phase: analogue-matched magnitude "
                             "and phase, ~21 ms latency. Linear Phase: no phase shift, ~91 ms latency (at 48 kHz).");
    addAndMakeVisible (phaseModeBox);

    const auto shortcut = [] (const char* mac, const char* other)
    {
       #if JUCE_MAC
        juce::ignoreUnused (other);
        return juce::String (mac);
       #else
        juce::ignoreUnused (mac);
        return juce::String (other);
       #endif
    };

    undoButton.setComponentID ("undo");
    undoButton.setTooltip ("Undo (" + shortcut ("Cmd-Z", "Ctrl-Z") + ")");
    undoButton.onClick = [this] { audioProcessor.getUndoHistory().undo(); updateUndoButtons(); };
    redoButton.setComponentID ("redo");
    redoButton.setTooltip ("Redo (" + shortcut ("Cmd-Shift-Z", "Ctrl-Y") + ")");
    redoButton.onClick = [this] { audioProcessor.getUndoHistory().redo(); updateUndoButtons(); };

    for (auto* button : { &undoButton, &redoButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    audioProcessor.getUndoHistory().getUndoManager().addChangeListener (this);
    updateUndoButtons();

    auto& state = audioProcessor.getParameterState();
    phaseModeAttachment = std::make_unique<ComboBoxAttachment> (state, params::phaseMode, phaseModeBox);
    outputAttachment = std::make_unique<SliderAttachment> (state, params::outputGain, outputSlider);
    stereoLinkAttachment = std::make_unique<ButtonAttachment> (state, params::stereoLink, stereoLinkButton);

    responseCurve.setComponentID ("responseCurve");
    responseCurve.onBandSelected = [this] (int bandIndex) { selectBand (bandIndex); };
    addAndMakeVisible (responseCurve);

    for (int b = 0; b < params::numBands; ++b)
    {
        auto& button = bandButtons[(size_t) b];
        button.setButtonText ("Band " + juce::String (b + 1));
        button.setComponentID ("band" + juce::String (b + 1));
        button.setRadioGroupId (1001);
        button.setClickingTogglesState (true);
        button.onClick = [this, b] { if (bandButtons[(size_t) b].getToggleState()) selectBand (b); };
        addAndMakeVisible (button);

        // On/off switch inside the tab, on its left. It is a child of the tab, so clicks on it
        // toggle the band without selecting the tab; clicks elsewhere on the tab select it.
        auto& bandSwitch = bandSwitches[(size_t) b];
        bandSwitch = std::make_unique<BandTabSwitch> (b);
        button.addAndMakeVisible (*bandSwitch);
        bandSwitchAttachments[(size_t) b] = std::make_unique<ButtonAttachment> (
            state, params::bandId (b, params::band::enabled), *bandSwitch);

        // 0.9.1: Solo 'S' on the tab's right.
        auto& solo = bandSoloButtons[(size_t) b];
        solo = std::make_unique<BandSoloButton> ("bandSolo" + juce::String (b + 1));
        solo->onSoloClick = [this, b] (bool exclusive) { toggleSolo (b, exclusive); };
        button.addAndMakeVisible (*solo);
        auto* soloPointer = solo.get();
        bandSoloAttachments[(size_t) b] = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::bandId (b, params::band::solo)),
            [this, soloPointer] (float value)
            {
                soloPointer->setToggleState (value >= 0.5f, juce::dontSendNotification);
                responseCurve.repaint();
            });
        bandSoloAttachments[(size_t) b]->sendInitialUpdate();
    }

    soloButton.onSoloClick = [this] (bool exclusive) { toggleSolo (selectedBand, exclusive); };
    addAndMakeVisible (soloButton);

    enabledButton.setComponentID (params::band::enabled);
    addAndMakeVisible (enabledButton);

    typeBox.setComponentID (params::band::type);
    typeBox.addItemList (params::filterTypeNames(), 1);
    addAndMakeVisible (typeBox);

    slopeBox.setComponentID (params::band::slope);
    slopeBox.addItemList (params::slopeNames(), 1);
    addAndMakeVisible (slopeBox);

    modelBox.setComponentID (params::band::model);
    modelBox.addItemList (params::modelNames(), 1);
    modelBox.setTooltip ("Analogue model of this band: its filter shapes (Q behaviour, shelf curves, switched "
                         "positions) and a level-dependent output colour. Original behavioural models, not "
                         "hardware captures. Digital = the clean 0.7 filters.");
    addAndMakeVisible (modelBox);

    steppedButton.setComponentID (params::band::stepped);
    addAndMakeVisible (steppedButton);

    dynamicsButton.setComponentID (params::band::dynamicsEnabled);
    addAndMakeVisible (dynamicsButton);

    setUpKnob (frequencyKnob, "Frequency", params::band::frequency);
    setUpKnob (gainKnob, "Gain", params::band::gain);
    // "Width": the band's Q shown and edited as a bandwidth in octaves. The knob works in
    // octaves (clockwise = wider) and writes the existing Q parameter, so sessions are unchanged.
    setUpKnob (qKnob, "Width", params::band::q);
    qKnob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 118, 18);
    {
        juce::NormalisableRange<double> octaves (params::qToOctaves (params::maxQ), params::qToOctaves (params::minQ));
        octaves.setSkewForCentre (1.0);
        qKnob.slider.setNormalisableRange (octaves);
    }
    qKnob.slider.textFromValueFunction = [] (double octaves)
    {
        return juce::String (octaves, 2) + " oct (Q " + juce::String (params::octavesToQ (octaves), 2) + ")";
    };
    qKnob.slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    qKnob.slider.setTooltip ("Bandwidth in octaves (sets the band's Q)");
    qKnob.slider.onDragStart = [this] { if (qAttachment != nullptr) qAttachment->beginGesture(); };
    qKnob.slider.onDragEnd   = [this] { if (qAttachment != nullptr) qAttachment->endGesture(); };
    qKnob.slider.onValueChange = [this]
    {
        if (qAttachment == nullptr)
            return;

        const auto q = (float) params::octavesToQ (qKnob.slider.getValue());

        if (qKnob.slider.isMouseButtonDown())
            qAttachment->setValueAsPartOfGesture (q);
        else
            qAttachment->setValueAsCompleteGesture (q);
    };
    setUpKnob (driveKnob, "Drive", params::band::drive);
    setUpKnob (attenKnob, "Atten", params::band::atten);
    driveKnob.slider.setDoubleClickReturnValue (true, 0.0);
    attenKnob.slider.setDoubleClickReturnValue (true, 0.0);
    setUpKnob (rangeKnob, "Range", params::band::range);
    setUpKnob (thresholdKnob, "Threshold", params::band::threshold);
    setUpKnob (attackKnob, "Attack", params::band::attack);
    setUpKnob (releaseKnob, "Release", params::band::release);

    gainKnob.slider.setDoubleClickReturnValue (true, 0.0);
    rangeKnob.slider.setDoubleClickReturnValue (true, 0.0);
    rangeKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, Palette::dynamic);

    rangeKnob.slider.setTooltip ("Dynamic Range: how far the band moves with level (also the outer ring around "
                                 "Gain, or around Frequency for Low / High Cut)");

    // Added after the knobs so it sits on top; it only takes clicks on the ring itself.
    addAndMakeVisible (rangeRing);
    rangeRing.setTarget (&gainKnob.slider);

    setUpAnalyzerControls();

    selectBand (0);

    // Keyboard focus for Cmd/Ctrl-Z: the editor takes focus when clicked anywhere inside.
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    setSize (defaultWidth, defaultHeight);

    startTimerHz (30);
}

DooblrQAudioProcessorEditor::~DooblrQAudioProcessorEditor()
{
    removeMouseListener (this);
    audioProcessor.getUndoHistory().getUndoManager().removeChangeListener (this);
    // No editor, no analysis: the audio thread stops feeding the FIFO.
    audioProcessor.setAnalyzerRunning (false);
    stopTimer();
    setLookAndFeel (nullptr);
}

void DooblrQAudioProcessorEditor::setUpAnalyzerControls()
{
    auto& view = audioProcessor.getAnalyzerViewSettings();
    auto& analyzer = audioProcessor.getSpectrumAnalyzer();

    spectrumButton.setComponentID ("spectrum");
    spectrumButton.setToggleState (view.visible, juce::dontSendNotification);
    spectrumButton.onClick = [this, &view]
    {
        view.visible = spectrumButton.getToggleState();
        audioProcessor.setAnalyzerRunning (view.visible);
        updateAnalyzerControls();
        responseCurve.repaint();
    };

    sourceBox.setComponentID ("analyzerSource");
    sourceBox.addItem ("Post-EQ", 1);
    sourceBox.addItem ("Pre-EQ", 2);
    sourceBox.setSelectedId (audioProcessor.getAnalyzerSource() == DooblrQAudioProcessor::AnalyzerSource::preEq ? 2 : 1,
                             juce::dontSendNotification);
    sourceBox.setTooltip ("Analyze the signal before or after the EQ");
    sourceBox.onChange = [this]
    {
        audioProcessor.setAnalyzerSource (sourceBox.getSelectedId() == 2 ? DooblrQAudioProcessor::AnalyzerSource::preEq
                                                                         : DooblrQAudioProcessor::AnalyzerSource::postEq);
    };

    static constexpr float tilts[] { 0.0f, 3.0f, 4.5f };
    tiltBox.setComponentID ("analyzerTilt");
    tiltBox.addItem ("Tilt 0 dB/oct", 1);
    tiltBox.addItem ("Tilt 3 dB/oct", 2);
    tiltBox.addItem ("Tilt 4.5 dB/oct", 3);
    for (int i = 0; i < (int) std::size (tilts); ++i)
        if (juce::approximatelyEqual (tilts[i], view.tiltDbPerOctave))
            tiltBox.setSelectedId (i + 1, juce::dontSendNotification);
    tiltBox.setTooltip ("Display slope around 1 kHz, so typical music reads roughly flat");
    tiltBox.onChange = [this, &view]
    {
        view.tiltDbPerOctave = tilts[(size_t) juce::jlimit (0, 2, tiltBox.getSelectedId() - 1)];
        responseCurve.repaint();
    };

    peakButton.setComponentID ("analyzerPeak");
    peakButton.setToggleState (view.showPeak, juce::dontSendNotification);
    peakButton.setTooltip ("Show the peak-hold line since the last Clear");
    peakButton.onClick = [this, &view]
    {
        view.showPeak = peakButton.getToggleState();
        responseCurve.repaint();
    };

    freezeButton.setComponentID ("freeze");
    freezeButton.setClickingTogglesState (true);
    freezeButton.setToggleState (analyzer.isFrozen(), juce::dontSendNotification);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, dooblrq::ui::Palette::frozen.withAlpha (0.35f));
    freezeButton.setTooltip ("Start averaging the spectrum from now and show the average as an overlay; "
                             "it keeps updating while frozen. Turning Freeze off stops and clears it");
    freezeButton.onClick = [this, &analyzer]
    {
        // Freeze on starts a fresh average; Freeze off stops and discards it.
        if (freezeButton.getToggleState())
            analyzer.freeze();
        else
            analyzer.unfreeze();

        updateAnalyzerControls();
        responseCurve.repaint();
    };

    clearButton.setComponentID ("clear");
    clearButton.setTooltip ("Reset the peak hold and, while frozen, restart the average from zero");
    clearButton.onClick = [this, &analyzer]
    {
        analyzer.clear();
        updateAnalyzerControls();
        responseCurve.repaint();
    };

    styleLabel (analyzerReadout, {}, 12.0f, Palette::textMuted, juce::Justification::centredRight);
    // Fixed-width digits so the counting readout does not jitter.
    analyzerReadout.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain)));
    analyzerReadout.setMinimumHorizontalScale (1.0f);
    analyzerReadout.setComponentID ("analyzerReadout");

    for (auto* component : std::initializer_list<juce::Component*> { &spectrumButton, &sourceBox, &tiltBox, &peakButton,
                                                                      &freezeButton, &clearButton, &analyzerReadout })
        addAndMakeVisible (component);

    audioProcessor.setAnalyzerRunning (view.visible);
    updateAnalyzerControls();
}

void DooblrQAudioProcessorEditor::updateAnalyzerControls()
{
    const auto& view = audioProcessor.getAnalyzerViewSettings();
    auto& analyzer = audioProcessor.getSpectrumAnalyzer();

    freezeButton.setToggleState (analyzer.isFrozen(), juce::dontSendNotification);

    for (auto* component : std::initializer_list<juce::Component*> { &sourceBox, &tiltBox, &peakButton,
                                                                      &freezeButton, &clearButton })
        component->setEnabled (view.visible);

    juce::String text;

    if (! view.visible)
        text = "Analyzer off";
    else if (analyzer.isFrozen())
        text = "Frozen: " + juce::String (analyzer.getAccumulatedSeconds(), 1) + " s";
    else
        text = "Live";

    if (analyzerReadout.getText() != text) // only repaint when the 0.1 s text changes
        analyzerReadout.setText (text, juce::dontSendNotification);
}

//==============================================================================
bool DooblrQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier; // Cmd on macOS, Ctrl elsewhere
    const auto shift = juce::ModifierKeys::shiftModifier;
    auto& history = audioProcessor.getUndoHistory();

    if (key == juce::KeyPress ('z', command, 0))
    {
        history.undo();
        updateUndoButtons();
        return true; // claim it, so the host does not also undo
    }

    if (key == juce::KeyPress ('z', command | shift, 0) || key == juce::KeyPress ('y', command, 0))
    {
        history.redo();
        updateUndoButtons();
        return true;
    }

    return false; // everything else goes to the host
}

void DooblrQAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    // Called for clicks on any child too (mouse listener). Leave text entry focused.
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr)
        grabKeyboardFocus();
}

void DooblrQAudioProcessorEditor::updateUndoButtons()
{
    const auto& history = audioProcessor.getUndoHistory();
    undoButton.setEnabled (history.canUndo());
    redoButton.setEnabled (history.canRedo());
}

void DooblrQAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateUndoButtons();
}

void DooblrQAudioProcessorEditor::setUpKnob (Knob& knob, const juce::String& caption, const char* parameterSuffix)
{
    knob.slider.setComponentID (parameterSuffix);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    addAndMakeVisible (knob.slider);

    styleLabel (knob.caption, caption, 12.0f, Palette::textMuted, juce::Justification::centred);
    addAndMakeVisible (knob.caption);
}

void DooblrQAudioProcessorEditor::toggleSolo (int bandIndex, bool exclusive)
{
    if (bandIndex < 0 || bandIndex >= params::numBands || ! audioProcessor.getBandSettings (bandIndex).enabled)
        return;

    auto& state = audioProcessor.getParameterState();
    const auto soloParameter = [&state] (int b) { return state.getParameter (params::bandId (b, params::band::solo)); };
    const auto isOn = [&] (int b) { return soloParameter (b)->getValue() >= 0.5f; };

    std::array<float, params::numBands> target {};
    for (int b = 0; b < params::numBands; ++b)
        target[(size_t) b] = isOn (b) ? 1.0f : 0.0f;

    if (exclusive)
    {
        auto onlyThis = isOn (bandIndex);
        for (int b = 0; b < params::numBands; ++b)
            if (b != bandIndex && isOn (b))
                onlyThis = false;

        for (int b = 0; b < params::numBands; ++b)
            target[(size_t) b] = (b == bandIndex && ! onlyThis) ? 1.0f : 0.0f;
    }
    else
    {
        target[(size_t) bandIndex] = isOn (bandIndex) ? 0.0f : 1.0f;
    }

    // Nested gestures: the undo history records the whole change as one step.
    std::vector<juce::RangedAudioParameter*> changed;
    for (int b = 0; b < params::numBands; ++b)
        if (! juce::exactlyEqual (target[(size_t) b], isOn (b) ? 1.0f : 0.0f))
            changed.push_back (soloParameter (b));

    for (auto* parameter : changed)
        parameter->beginChangeGesture();
    for (int b = 0; b < params::numBands; ++b)
        if (auto* parameter = soloParameter (b); std::find (changed.begin(), changed.end(), parameter) != changed.end())
            parameter->setValueNotifyingHost (target[(size_t) b]);
    for (auto* parameter : changed)
        parameter->endChangeGesture();

    responseCurve.repaint();
}

//==============================================================================
void DooblrQAudioProcessorEditor::selectBand (int bandIndex)
{
    selectedBand = juce::jlimit (0, params::numBands - 1, bandIndex);

    // Drop the old bindings before attaching the controls to the new band.
    enabledAttachment.reset();
    soloAttachment.reset();
    dynamicsAttachment.reset();
    typeAttachment.reset();
    slopeAttachment.reset();
    modelAttachment.reset();
    steppedAttachment.reset();
    driveAttachment.reset();
    attenAttachment.reset();
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    rangeAttachment.reset();
    rangeRing.attach (nullptr);
    thresholdAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();

    auto& state = audioProcessor.getParameterState();
    const auto id = [this] (const char* suffix) { return params::bandId (selectedBand, suffix); };

    enabledAttachment   = std::make_unique<ButtonAttachment>   (state, id (params::band::enabled), enabledButton);
    soloAttachment      = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (id (params::band::solo)),
        [this] (float value) { soloButton.setToggleState (value >= 0.5f, juce::dontSendNotification); });
    soloAttachment->sendInitialUpdate();
    typeAttachment      = std::make_unique<ComboBoxAttachment> (state, id (params::band::type), typeBox);
    slopeAttachment     = std::make_unique<ComboBoxAttachment> (state, id (params::band::slope), slopeBox);
    frequencyAttachment = std::make_unique<SliderAttachment>   (state, id (params::band::frequency), frequencyKnob.slider);
    gainAttachment      = std::make_unique<SliderAttachment>   (state, id (params::band::gain), gainKnob.slider);
    modelAttachment     = std::make_unique<ComboBoxAttachment> (state, id (params::band::model), modelBox);
    steppedAttachment   = std::make_unique<ButtonAttachment>   (state, id (params::band::stepped), steppedButton);
    driveAttachment     = std::make_unique<SliderAttachment>   (state, id (params::band::drive), driveKnob.slider);
    attenAttachment     = std::make_unique<SliderAttachment>   (state, id (params::band::atten), attenKnob.slider);

    // With Stepped on, the Frequency / Gain texts show the switched position actually used.
    const auto snappedText = [this] (const char* suffix)
    {
        auto* parameter = audioProcessor.getParameterState().getParameter (params::bandId (selectedBand, suffix));
        const auto isFrequency = juce::String (suffix) == params::band::frequency;
        return [this, parameter, isFrequency] (double value)
        {
            const auto settings = audioProcessor.getBandSettings (selectedBand); // already snapped
            const auto* stepped = audioProcessor.getParameterState().getRawParameterValue (params::bandId (selectedBand, params::band::stepped));
            auto shown = (float) value;
            if (stepped != nullptr && stepped->load() >= 0.5f && dooblrq::dsp::isAnalog (settings.model))
                shown = isFrequency ? dooblrq::dsp::snapFrequency (settings.model, settings.type, shown)
                                    : dooblrq::dsp::snapGain (settings.model, settings.type, shown);
            return parameter->getText (parameter->convertTo0to1 (shown), 0);
        };
    };
    frequencyKnob.slider.textFromValueFunction = snappedText (params::band::frequency);
    gainKnob.slider.textFromValueFunction      = snappedText (params::band::gain);
    frequencyKnob.slider.updateText();
    gainKnob.slider.updateText();
    lastStepKey = {};
    qAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (id (params::band::q)),
        [this] (float q) { qKnob.slider.setValue (params::qToOctaves ((double) q), juce::dontSendNotification); });
    qAttachment->sendInitialUpdate();
    dynamicsAttachment  = std::make_unique<ButtonAttachment>   (state, id (params::band::dynamicsEnabled), dynamicsButton);
    rangeAttachment     = std::make_unique<SliderAttachment>   (state, id (params::band::range), rangeKnob.slider);
    thresholdAttachment = std::make_unique<SliderAttachment>   (state, id (params::band::threshold), thresholdKnob.slider);
    attackAttachment    = std::make_unique<SliderAttachment>   (state, id (params::band::attack), attackKnob.slider);
    releaseAttachment   = std::make_unique<SliderAttachment>   (state, id (params::band::release), releaseKnob.slider);
    rangeRing.attach (state.getParameter (id (params::band::range)));

    bandButtons[(size_t) selectedBand].setToggleState (true, juce::dontSendNotification);
    responseCurve.setSelectedBand (selectedBand);

    const auto colour = dooblrq::ui::bandColour (selectedBand);
    for (auto* knob : { &frequencyKnob, &gainKnob, &qKnob, &driveKnob, &attenKnob })
        knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);

    updateBandIndicators();
    repaint();
}

void DooblrQAudioProcessorEditor::updateBandIndicators()
{
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto settings = audioProcessor.getBandSettings (b);
        auto& button = bandButtons[(size_t) b];
        const auto colour = dooblrq::ui::bandColour (b);

        button.setColour (juce::TextButton::textColourOffId, settings.enabled ? colour : Palette::textMuted);
        button.setColour (juce::TextButton::textColourOnId, settings.enabled ? colour.brighter (0.3f) : Palette::textPrimary);

        // A disabled band cannot be soloed (its Solo is ignored by the DSP too).
        bandSoloButtons[(size_t) b]->setEnabled (settings.enabled);
    }

    soloButton.setEnabled (audioProcessor.getBandSettings (selectedBand).enabled);

    // Controls that have no audible effect for the current settings are dimmed but stay usable.
    const auto settings = audioProcessor.getBandSettings (selectedBand);
    const auto gainUsed = dooblrq::dsp::hasGain (settings.type);
    const auto dynamicsUsed = settings.dynamicsEnabled && dooblrq::dsp::supportsDynamics (settings.type);

    gainKnob.slider.setAlpha (gainUsed ? 1.0f : 0.4f);
    dynamicsButton.setAlpha (dooblrq::dsp::supportsDynamics (settings.type) ? 1.0f : 0.4f);

    for (auto* knob : { &rangeKnob, &thresholdKnob, &attackKnob, &releaseKnob })
        knob->slider.setAlpha (dynamicsUsed ? 1.0f : 0.4f);

    updateRangeRing (settings);

    // Slope works for every type; the tooltip says what it does for this one.
    using T = dooblrq::dsp::FilterType;
    const auto slopeTip = [&]() -> juce::String
    {
        switch (settings.type)
        {
            case T::lowCut: case T::highCut:
                return "Filter slope: 12 to 60 dB/oct (2nd to 10th order Butterworth), or Brickwall "
                       "(32nd order, about 192 dB/oct). The corner stays at -3 dB; Width/Q adds resonance.";
            case T::bell:
                return "Bell slope: steeper skirts and a flatter top at higher settings (Butterworth band "
                       "shelf, order 2 to 10, Brickwall 32). Centre stays at the gain, the Width edges at half the gain.";
            case T::notch:
                return "Notch slope: steeper edges and a wider, flatter rejection band at higher settings "
                       "(Butterworth band-stop, order 2 to 10, Brickwall 32). -3 dB stays at the Width edges.";
            case T::lowShelf: case T::highShelf: case T::tilt:
                break;
        }
        return "Transition slope: steeper at higher settings (Butterworth shelving, order 2 to 10, Brickwall 32). "
               "The midpoint (Tilt: the pivot) stays put.";
    };
    slopeBox.setEnabled (true);
    slopeBox.setTooltip (slopeTip());
    updateModelControls (settings);
}

void DooblrQAudioProcessorEditor::updateRangeRing (const dooblrq::dsp::BandSettings& settings)
{
    namespace dsp = dooblrq::dsp;
    const auto cut = dsp::isCut (settings.type);
    auto* target = cut ? &frequencyKnob.slider : &gainKnob.slider;

    if (rangeRing.getTarget() != target)
        rangeRing.setTarget (target);

    rangeRing.setRingColour (dooblrq::ui::bandColour (selectedBand));
    rangeRing.setMovesCutoff (cut, settings.type == dsp::FilterType::highCut);
    rangeRing.setEnabled (settings.dynamicsEnabled && dsp::supportsDynamics (settings.type));

    const auto type = settings.type;
    const auto frequency = (double) settings.frequency, gain = (double) settings.gainDb;
    rangeRing.setMapping ([cut, type, frequency, gain] (double dynamicDb)
    {
        return cut ? dsp::DynamicEQBand::dynamicCutFrequency (type, frequency, dynamicDb) : gain + dynamicDb;
    });
    rangeRing.setLiveAmount (settings.dynamicsEnabled ? (double) audioProcessor.getBandDynamicGainDb (selectedBand) : 0.0);
}

void DooblrQAudioProcessorEditor::updateModelControls (const dooblrq::dsp::BandSettings& settings)
{
    namespace dsp = dooblrq::dsp;
    const auto modelName = juce::String (dsp::modelName (settings.model));
    const auto analog = dsp::isAnalog (settings.model);

    // Slope: the analogue models have their own filter orders, so the choice is disabled there.
    if (! dsp::usesSlope (settings.model))
    {
        slopeBox.setEnabled (false);
        slopeBox.setTooltip ("Slope is set by the " + modelName + " model (its own filter orders). "
                             "Choose the Digital model for 12 to 60 dB/oct and Brickwall.");
    }

    const auto stepsAvailable = dsp::hasSteps (settings.model, settings.type);
    steppedButton.setEnabled (stepsAvailable);
    steppedButton.setTooltip (stepsAvailable
        ? "Stepped: snap Frequency" + juce::String (dsp::gainSteps (settings.model, settings.type).empty() ? "" : " and Gain")
              + " to the " + modelName + "'s switched positions (the handle snaps too)"
        : (analog ? "The " + modelName + " has no switched positions for this filter type (continuous controls)"
                  : juce::String ("Stepped needs an analogue model with switched positions")));

    const auto* stepped = audioProcessor.getParameterState().getRawParameterValue (params::bandId (selectedBand, params::band::stepped));
    const auto steppedOn = stepped != nullptr && stepped->load() >= 0.5f && stepsAvailable;
    const auto steps = dsp::frequencySteps (settings.model, settings.type);
    const auto fixed = steppedOn && steps.size == 1;
    frequencyKnob.slider.setEnabled (! fixed);
    frequencyKnob.slider.setTooltip (fixed ? "Fixed at " + frequencyKnob.slider.getTextFromValue (steps.values[0])
                                                 + " on the " + modelName + " (Stepped)"
                                           : juce::String());

    driveKnob.slider.setEnabled (analog);
    driveKnob.slider.setTooltip (analog ? "Drive into the " + modelName + " colour stage (transformer / amplifier "
                                          "saturation, level-dependent; 0 dB = nominal level)"
                                        : juce::String ("Drive applies to the analogue models' colour stage (Digital is clean)"));

    const auto attenUsed = dsp::usesAtten (settings.model, settings.type);
    attenKnob.slider.setEnabled (attenUsed);
    attenKnob.slider.setTooltip (attenUsed ? juce::String ("Pultec Atten: cut at the same frequency as the boost. Boost + Atten "
                                                           "together gives the shelf with a dip above it (low) or a "
                                                           "cut shelf (high).")
                                           : juce::String ("Atten is for the Pultec EQP-1A model with a Low or High Shelf"));

    const auto key = juce::String ((int) settings.model) + "/" + juce::String ((int) settings.type) + "/"
                     + (steppedOn ? "1" : "0");
    if (key != lastStepKey)
    {
        lastStepKey = key;
        frequencyKnob.slider.updateText();
        gainKnob.slider.updateText();
    }
}

void DooblrQAudioProcessorEditor::timerCallback()
{
    updateBandIndicators();
    updateAnalyzerControls();
    updateUndoButtons();
}

//==============================================================================
void DooblrQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::panel);
    g.fillRect (headerArea);
    g.fillRect (controlsArea);

    g.setColour (Palette::divider);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
    g.fillRect (controlsArea.getX(), controlsArea.getY(), controlsArea.getWidth(), 1);
    g.fillRect (dynamicsSectionArea.getX() - 10, controlsArea.getY() + 14, 1, controlsArea.getHeight() - 28);

    g.setColour (Palette::accent);
    g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (dooblrq::ui::bandColour (selectedBand));
    g.drawText ("BAND " + juce::String (selectedBand + 1), eqSectionArea.withHeight (16), juce::Justification::centredLeft);
    g.setColour (Palette::dynamic);
    g.drawText ("DYNAMICS", dynamicsSectionArea.withHeight (16), juce::Justification::centredLeft);
}

void DooblrQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    headerArea = bounds.removeFromTop (60);
    {
        auto header = headerArea.reduced (32, 10);
        auto titleColumn = header.removeFromLeft (172);
        titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
        subtitleLabel.setBounds (titleColumn);

        header.removeFromLeft (8);
        undoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
        header.removeFromLeft (4);
        redoButton.setBounds (header.removeFromLeft (50).withSizeKeepingCentre (50, 24));
        header.removeFromLeft (12);

        versionLabel.setBounds (header.removeFromRight (52));
        header.removeFromRight (12);
        // The output slider takes what is left after the fixed-size controls.
        const auto sliderWidth = juce::jlimit (100, 200, header.getWidth() - (52 + 12 + 52 + 12 + 112 + 8 + 136));
        outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
        outputCaption.setBounds (header.removeFromRight (52));
        header.removeFromRight (12);
        stereoLinkButton.setBounds (header.removeFromRight (112).withSizeKeepingCentre (112, 24));
        header.removeFromRight (8);
        phaseModeBox.setBounds (header.removeFromRight (136).withSizeKeepingCentre (136, 26));
    }

    toolbarArea = bounds.removeFromTop (38);
    {
        auto toolbar = toolbarArea.reduced (16, 6);
        spectrumButton.setBounds (toolbar.removeFromLeft (104));
        toolbar.removeFromLeft (6);
        sourceBox.setBounds (toolbar.removeFromLeft (100));
        toolbar.removeFromLeft (8);
        tiltBox.setBounds (toolbar.removeFromLeft (140));

        clearButton.setBounds (toolbar.removeFromRight (64));
        toolbar.removeFromRight (6);
        freezeButton.setBounds (toolbar.removeFromRight (76));
        toolbar.removeFromRight (10);
        peakButton.setBounds (toolbar.removeFromRight (70));
        toolbar.removeFromRight (6);
        analyzerReadout.setBounds (toolbar.removeFromRight (150));
    }

    controlsArea = bounds.removeFromBottom (200);
    auto bandRow = bounds.removeFromBottom (40).reduced (16, 6);
    const auto buttonWidth = bandRow.getWidth() / params::numBands;
    for (size_t b = 0; b < bandButtons.size(); ++b)
    {
        auto& button = bandButtons[b];
        button.setBounds (bandRow.removeFromLeft (buttonWidth).reduced (3, 0));
        bandSwitches[b]->setBounds (6, (button.getHeight() - BandTabSwitch::switchHeight - 6) / 2,
                                    BandTabSwitch::switchWidth + 6, BandTabSwitch::switchHeight + 6);
        bandSoloButtons[b]->setBounds (button.getWidth() - 28, 0, 24, button.getHeight());
    }

    responseCurve.setBounds (bounds.reduced (16, 10));

    auto controls = controlsArea.reduced (20, 12);
    // The Width readout ("1.90 oct (Q 0.73)") is the longest, so its column gets more room;
    // the Dynamics section only has one switch, so its left column is narrower.
    const auto switchColumnWidth = 120, dynamicsSwitchWidth = 100;
    constexpr double widthColumnWeight = 1.7;
    const auto unit = (double) (controls.getWidth() - switchColumnWidth - dynamicsSwitchWidth - 24) / (8.0 + widthColumnWeight);

    eqSectionArea = controls.removeFromLeft (switchColumnWidth + juce::roundToInt (unit * (4.0 + widthColumnWeight)));
    controls.removeFromLeft (24);
    dynamicsSectionArea = controls;

    const auto layoutSection = [this, unit] (juce::Rectangle<int> area, int switchWidth,
                                                                std::initializer_list<juce::Component*> leftColumn,
                                                                std::initializer_list<Knob*> knobs)
    {
        area.removeFromTop (20);
        auto switches = area.removeFromLeft (switchWidth).reduced (0, 4);
        for (auto* control : leftColumn)
        {
            control->setBounds (switches.removeFromTop (24).withTrimmedRight (12));
            switches.removeFromTop (6);
        }

        auto x = (double) area.getX();
        for (auto* knob : knobs)
        {
            const auto right = x + unit * (knob == &qKnob ? widthColumnWeight : 1.0);
            auto column = area.withLeft (juce::roundToInt (x)).withRight (juce::roundToInt (right));
            x = right;
            knob->caption.setBounds (column.removeFromTop (18));
            knob->slider.setBounds (column.reduced (4, 0));
            knob->slider.getProperties().set ("maxKnobDiameter", unit - 8.0);
            knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knob->slider.getWidth(), 18);
            for (auto* child : knob->slider.getChildren())   // at small sizes squeeze rather than truncate
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    label->setMinimumHorizontalScale (0.8f);
        }
    };

    layoutSection (eqSectionArea, switchColumnWidth, { &enabledButton, &typeBox, &slopeBox, &modelBox, &steppedButton },
                   { &frequencyKnob, &gainKnob, &qKnob, &driveKnob, &attenKnob });
    {
        auto row = enabledButton.getBounds();
        soloButton.setBounds (row.removeFromRight (24));
        enabledButton.setBounds (row.withTrimmedRight (4));
    }
    layoutSection (dynamicsSectionArea, dynamicsSwitchWidth, { &dynamicsButton }, { &rangeKnob, &thresholdKnob, &attackKnob, &releaseKnob });
    rangeRing.placeAround();
}
