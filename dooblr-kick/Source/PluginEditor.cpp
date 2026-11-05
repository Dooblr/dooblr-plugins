#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"

#include <algorithm>
#include <cmath>

namespace params = dooblrkick::params;
namespace Palette = dooblrkick::ui::Palette;
using dooblrkick::ui::ControlPanel;

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
    constexpr double previewSampleRate = 48000.0;

    const char* pageName (int page)
    {
        static const char* names[] { "Click", "Body", "Sub", "Tail", "Noise", "Envelope", "Drive", "EQ", "Dynamics", "Limiter" };
        return names[page];
    }

    juce::Colour pageColour (int page)
    {
        if (page < params::numLayers) return dooblrkick::ui::bandColour (page);
        return page == params::numLayers ? Palette::master : Palette::dynamic;
    }

    using K = ControlPanel::Kind;
    using Specs = std::vector<ControlPanel::Spec>;

    juce::String lid (int layer, const char* suffix) { return params::layerId (layer, suffix); }

    // v0.3.3 layer pages. Level is the fader at the page's left (not in a panel). Panel 1 ("layer"):
    // Source and the mix / filter controls, the same in both sources. Panel 2 ("sound"): first the
    // source-specific region, a fixed run of slots whose Synth-only and Sample-only members swap
    // in place (a slot with no member for the current source stays empty), then the shared sound
    // controls. So switching Synth / Sample never moves or resizes anything else.
    enum SourceSlot { slotA, slotB, slotC, slotD };

    Specs layerCommonSpecs (int layer)
    {
        Specs specs;

        if (params::layerHasSample (layer))
            specs.push_back ({ lid (layer, "source"), "Source", K::combo });

        for (auto spec : Specs { { lid (layer, "pan"), "Pan", K::knob },
                                 { lid (layer, "start"), "Start", K::knob },
                                 { lid (layer, "send"), "Drive send", K::knob },
                                 { lid (layer, "ftype"), "Filter", K::combo },
                                 { lid (layer, "cutoff"), "Cutoff", K::knob },
                                 { lid (layer, "reso"), "Reso", K::knob } })
            specs.push_back (spec);

        return specs;
    }

    /** The "Load file" cell of a layer's sample controls goes in this slot, before this control. */
    struct LoadCellPlace { int slot; const char* before; };

    LoadCellPlace loadCellPlace (int layer)
    {
        switch (layer)
        {
            case dooblrkick::dsp::clickLayer: return { slotB, "click_smp_penv" };
            case dooblrkick::dsp::bodyLayer:  return { slotD, "body_tune" };
            case dooblrkick::dsp::tailLayer:  return { slotC, "tail_tune" };
            default:                          return { slotC, "noise_smp_penv" };
        }
    }

    // v0.3.6: each layer's level shape and pitch movement live in its Pitch / Amp envelope
    // graph, so the Click's Length and the Sub / Tail / Noise Attack / Hold / Decay / Curve knobs
    // are gone (the amp envelope's points are those times). Every sample layer has a
    // "Smp pitch env" knob: how far the pitch envelope bends the sample's playback rate.
    Specs layerSoundSpecs (int layer)
    {
        switch (layer)
        {
            case dooblrkick::dsp::clickLayer:
                return { { "click_type", "Type", K::combo, 0, 0, slotA }, { "click_sample", "Sample", K::combo, 0, 0, slotA },
                         /* slot B: Load file (sample) */
                         { "click_smp_penv", "Smp pitch env", K::knob, 0, 0, slotC },
                         { "click_pitch", "Pitch", K::knob }, { "click_tone", "Tone", K::knob } };
            case dooblrkick::dsp::bodyLayer:
                return { { "body_wave", "Wave", K::combo, 0, 0, slotA }, { "body_sample", "Sample", K::combo, 0, 0, slotA },
                         { "body_harm", "Harmonics", K::knob, 0, 0, slotB }, { "body_smp_penv", "Smp pitch env", K::knob, 0, 0, slotB },
                         { "body_phase", "Start phase", K::knob, 0, 0, slotC },
                         /* slot D: Load file (sample) */
                         { "body_tune", "Tune", K::knob }, { "body_drive", "Drive", K::knob } };
            case dooblrkick::dsp::subLayer:
                return { { "sub_tune", "Tune", K::knob }, { "sub_fine", "Fine", K::knob }, { "sub_phase", "Start phase", K::knob },
                         { "sub_envamt", "Body pitch", K::knob } };
            case dooblrkick::dsp::tailLayer:
                return { { "tail_wave", "Wave", K::combo, 0, 0, slotA }, { "tail_sample", "Sample", K::combo, 0, 0, slotA },
                         { "tail_spread", "Spread", K::knob, 0, 0, slotB }, { "tail_smp_penv", "Smp pitch env", K::knob, 0, 0, slotB },
                         { "tail_phase", "Start phase", K::knob, 0, 0, slotC }, /* + Load file (sample) */
                         { "tail_tune", "Tune", K::knob }, { "tail_drive", "Drive", K::knob } };
            default:
                return { { "noise_color", "Colour", K::combo, 0, 0, slotA }, { "noise_sample", "Sample", K::combo, 0, 0, slotA },
                         { "noise_width", "Width", K::knob, 0, 0, slotB }, { "noise_tune", "Smp tune", K::knob, 0, 0, slotB },
                         /* slot C: Load file (sample) */
                         { "noise_smp_penv", "Smp pitch env", K::knob, 0, 0, slotD } };
        }
    }

    /** Controls that only matter for one source of a layer with a Synth / Sample switch:
        hidden while the other source is selected (v0.3.2), in their fixed slots (v0.3.3).
        Everything else on the page is shared. */
    struct SourceSpecific { std::vector<const char*> synthOnly, sampleOnly; };

    SourceSpecific sourceSpecificIds (int layer)
    {
        switch (layer)
        {
            case dooblrkick::dsp::clickLayer: return { { "click_type" }, { "click_sample", "click_smp_penv" } };
            case dooblrkick::dsp::bodyLayer:  return { { "body_wave", "body_harm", "body_phase" },
                                                       { "body_sample", "body_smp_penv" } };
            case dooblrkick::dsp::tailLayer:  return { { "tail_wave", "tail_spread", "tail_phase" }, { "tail_sample", "tail_smp_penv" } };
            case dooblrkick::dsp::noiseLayer: return { { "noise_color", "noise_width" }, { "noise_sample", "noise_tune", "noise_smp_penv" } };
            default: return {};
        }
    }

    juce::String timestamp()
    {
        return juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
    }

    juce::String safeFileName (const juce::String& name)
    {
        auto s = juce::File::createLegalFileName (name).replaceCharacter (' ', '-');
        return s.isEmpty() ? juce::String ("kick") : s;
    }
}

//==============================================================================
LayerTabSwitch::LayerTabSwitch (int layerIndex) : layer (layerIndex)
{
    setComponentID (params::layerId (layer, "on"));
    setTooltip (juce::String (params::layerNames[(size_t) layer]) + " layer on/off");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LayerTabSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (down);

    const auto on = getToggleState();
    const auto colour = dooblrkick::ui::bandColour (layer);
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

SoloButton::SoloButton (int layerIndex) : layer (layerIndex)
{
    setComponentID (params::layerId (layer, "solo"));
    setTooltip ("Solo " + juce::String (params::layerNames[(size_t) layer]));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SoloButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (down);
    const auto on = getToggleState();
    const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 16.0f);

    g.setColour (on ? Palette::dynamic : Palette::background.withAlpha (0.6f));
    g.fillRoundedRectangle (box, 3.0f);

    if (! on)
    {
        g.setColour (Palette::textMuted.withAlpha (highlighted ? 0.8f : 0.45f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
    }

    g.setColour (on ? Palette::background : Palette::textMuted);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("S", box, juce::Justification::centred);
    juce::ignoreUnused (layer);
}

//==============================================================================
ProcessorPowerButton::ProcessorPowerButton (int stageIndex)
    : juce::Button ("power"), stage (stageIndex)
{
    static constexpr const char* ids[] = { "power_drive", "power_eq", "power_dynamics", "power_limiter" };
    static constexpr const char* names[] = { "Drive", "EQ", "Dynamics (transient shaper + compressor)", "Limiter" };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    if (stage == envelopeStage)
    {
        // Bound to menv_on by a ButtonAttachment: a click toggles it (one undo step).
        setComponentID ("power_envelope");
        setTooltip ("Master envelope on/off (off: bypassed, the hit plays unshaped)");
        setClickingTogglesState (true);
        return;
    }

    setComponentID (ids[stage]);
    setTooltip (juce::String (names[stage]) + " on/off");
    setClickingTogglesState (false); // the editor applies the change to the parameters
}

void ProcessorPowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto colour = pageColour (stage == envelopeStage ? (int) DooblrKickAudioProcessorEditor::envelopePage
                                                           : DooblrKickAudioProcessorEditor::pageForStage (stage));
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

    // Power glyph: an open ring with a stem through the gap.
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

    if (chainSlot < 0)
    {
        // A tab with a power toggle but no chain slot (Envelope): toggle, then the label; dimmed while off.
        auto& lf = getLookAndFeel();
        const auto on = getToggleState();
        lf.drawButtonBackground (g, *this, findColour (on ? buttonOnColourId : buttonColourId), highlighted, down);

        if (! processorOn)
        {
            g.setColour (Palette::background.withAlpha (0.35f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        }

        auto area = getLocalBounds().reduced (6, 0);
        area.removeFromLeft (3 + ProcessorPowerButton::size + 5);
        g.setColour (findColour (on ? textColourOnId : textColourOffId).withMultipliedAlpha (processorOn ? 1.0f : 0.45f));
        g.setFont (juce::FontOptions (juce::jlimit (12.0f, 15.0f, (float) getHeight() * 0.40f)));
        g.drawFittedText (getButtonText(), area, juce::Justification::centredLeft, 1, 0.8f);
        return;
    }

    auto& lf = getLookAndFeel();
    const auto on = getToggleState();
    lf.drawButtonBackground (g, *this, findColour (on ? buttonOnColourId : buttonColourId), highlighted || handleDrag, down || handleDrag);

    auto area = getLocalBounds().reduced (6, 0);
    // A processor that is switched off draws its tab dimmed (it can still be selected).
    const auto textColour = findColour (on ? textColourOnId : textColourOffId).withMultipliedAlpha (processorOn ? 1.0f : 0.45f);
    const auto fontSize = juce::jlimit (12.0f, 15.0f, (float) getHeight() * 0.40f);

    if (! processorOn)
    {
        g.setColour (Palette::background.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }

    // Position in the chain.
    const auto badge = area.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
    g.setColour (on ? textColour.withAlpha (0.25f) : Palette::background.withAlpha (0.7f));
    g.fillEllipse (badge);
    g.setColour (textColour.withAlpha (0.9f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (chainSlot + 1), badge, juce::Justification::centred);

    // Hamburger handle.
    const auto handle = getHandleArea().toFloat().withSizeKeepingCentre (12.0f, 10.0f);
    g.setColour (textColour.withAlpha (highlighted || handleDrag ? 0.95f : 0.55f));
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (handle.getX(), handle.getY() + 4.0f * (float) i, handle.getWidth(), 1.6f, 0.8f);

    area.removeFromRight (20);
    area.removeFromLeft (3 + ProcessorPowerButton::size + 5); // the power toggle (a child component)
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
DooblrKickAudioProcessorEditor::DooblrKickAudioProcessorEditor (DooblrKickAudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      audioProcessor (processorToEdit),
      state (processorToEdit.getParameterState()),
      envelopeEditor (processorToEdit.getParameterState(), params::Envelope::pitch),
      waveform ([&processorToEdit] { return processorToEdit.makePreviewJob (previewSampleRate); },
                [&processorToEdit] { return processorToEdit.getChangeCount(); }, previewSampleRate),
      eqDisplay (processorToEdit.getParameterState()),
      dynamicsDisplay (processorToEdit.getParameterState()),
      limiterDisplay (processorToEdit.getParameterState()),
      masterEditor (processorToEdit.getParameterState(), params::Envelope::master),
      outputMeter (&processorToEdit.getOutputPeakTap()),
      mixDisplay (processorToEdit.getParameterState()),
      driveDisplay (processorToEdit.getParameterState())
{
    setLookAndFeel (&lookAndFeel);

    styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
    // v0.3.4: the subtitle is the build's version (was a "kick designer" tag line).
    styleLabel (subtitleLabel, "v" JucePlugin_VersionString, 13.0f, Palette::textMuted, juce::Justification::centredLeft);
    subtitleLabel.setComponentID ("version");
    styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &subtitleLabel, &outputCaption })
        addAndMakeVisible (label);

    addAndMakeVisible (outputMeter);

    // Header.
    undoButton.setComponentID ("undo");
    redoButton.setComponentID ("redo");
    undoButton.setTooltip ("Undo (Cmd-Z)");
    redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
    undoButton.onClick = [this] { audioProcessor.getUndoHistory().undo(); updateUndoButtons(); };
    redoButton.onClick = [this] { audioProcessor.getUndoHistory().redo(); updateUndoButtons(); };

    presetBox.setComponentID ("preset");
    presetBox.setTooltip ("Factory and user presets (user presets live in ~/Music/dooblr-kick/Presets)");
    presetBox.onChange = [this] { presetChosen(); };

    saveButton.setComponentID ("savePreset");
    saveButton.setTooltip ("Save the current kick as a user preset file (.dkick)");
    saveButton.onClick = [this] { savePreset(); };
    loadButton.setComponentID ("loadPreset");
    loadButton.setTooltip ("Load a user preset file (.dkick)");
    loadButton.onClick = [this] { loadPreset(); };
    randomButton.setComponentID ("randomise");
    randomButton.setTooltip ("Randomise the kick (one undo step)");
    randomButton.onClick = [this]
    {
        juce::Random random;
        audioProcessor.randomise (random);
        refreshPresetBox();
    };
    exportButton.setComponentID ("exportWav");
    exportButton.setTooltip ("Render one hit at the Key to a 24-bit WAV; choose where to save it");
    exportButton.onClick = [this] { exportWav(); };
    playButton.setComponentID ("play");
    playButton.setTooltip ("Play");
    playButton.onClick = [this] { audioProcessor.triggerAudition(); };

    outputSlider.setComponentID ("output_gain");
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
    outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);

    for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &presetBox,
                                (juce::Component*) &saveButton, (juce::Component*) &loadButton, (juce::Component*) &randomButton,
                                (juce::Component*) &exportButton, (juce::Component*) &playButton, (juce::Component*) &outputSlider })
        addAndMakeVisible (c);

    // Navigation: layer tabs (with on switch and solo) and FX pages.
    for (int p = 0; p < numPages; ++p)
    {
        auto& button = pageButtons[(size_t) p];
        button.setButtonText (pageName (p));
        button.setComponentID ("page_" + juce::String (pageName (p)));
        button.setRadioGroupId (2001);
        button.setClickingTogglesState (true);
        button.onClick = [this, p] { if (pageButtons[(size_t) p].getToggleState()) selectPage (p); };
        addAndMakeVisible (button);

        if (p < params::numLayers)
        {
            auto& layerSwitch = layerSwitches[(size_t) p];
            layerSwitch = std::make_unique<LayerTabSwitch> (p);
            button.addAndMakeVisible (*layerSwitch);
            layerSwitchAttachments[(size_t) p] = std::make_unique<ButtonAttachment> (state, params::layerId (p, "on"), *layerSwitch);

            auto& solo = soloButtons[(size_t) p];
            solo = std::make_unique<SoloButton> (p);
            button.addAndMakeVisible (*solo);
            soloAttachments[(size_t) p] = std::make_unique<ButtonAttachment> (state, params::layerId (p, "solo"), *solo);
        }

        if (p == envelopePage)
        {
            // v0.3.3: the master envelope's on / off lives on its tab, like the processors'.
            envelopePower = std::make_unique<ProcessorPowerButton> (ProcessorPowerButton::envelopeStage);
            button.setHasPowerToggle (true);
            button.addAndMakeVisible (*envelopePower);
            envelopePowerAttachment = std::make_unique<ButtonAttachment> (state, "menv_on", *envelopePower);
            envelopePower->onStateChange = [this] { pageButtons[(size_t) envelopePage].setProcessorOn (envelopePower->getToggleState()); };
        }

        if (p >= drivePage)
        {
            const auto stage = p - drivePage;
            button.setTooltip (juce::String (pageName (p)) + " - drag the handle to move it in the processing chain");
            button.onHandleDown = [this, stage] (const juce::MouseEvent& e) { chainHandleDown (stage, e); };
            button.onHandleDrag = [this, stage] (const juce::MouseEvent& e) { chainHandleDrag (stage, e); };
            button.onHandleUp = [this, stage] (const juce::MouseEvent& e) { chainHandleUp (stage, e); };

            // The tab's power toggle: a child of the tab (moves with it, takes its own clicks).
            auto& power = powerButtons[(size_t) stage];
            power = std::make_unique<ProcessorPowerButton> (stage);
            power->onClick = [this, stage] { setStageOn (stage, ! isStageOn (stage)); };
            button.addAndMakeVisible (*power);
        }
    }

    shownOrder = chainOrderParameter();

    // Envelope editor (the selected layer's pitch / amp envelope) and the rendered waveform.
    // v0.3.5: one sliding switch for the Pitch / Amp view; the graph cross-fades.
    // v0.3.6: the switch is on every layer page (each layer has both envelopes).
    envelopeToggle.setComponentID ("envelopeView");
    envelopeToggle.setTooltip ("Show this layer's pitch or amp envelope");
    envelopeToggle.onChange = [this]
    {
        if (selectedPage >= params::numLayers)
            return;

        envelopeEditor.startCrossfade();
        showEnvelope (params::layerEnvelope (selectedPage, envelopeToggle.isRight()));
    };
    addAndMakeVisible (envelopeToggle);
    addAndMakeVisible (envelopeEditor);

    // v0.3.5: the envelope / waveform divider.
    splitter.setComponentID ("splitter");
    splitter.onDragStart = [this] { splitDragStartWidth = envelopeEditor.getWidth(); };
    splitter.onDrag = [this] (int dx)
    {
        const auto displayWidth = juce::jmax (1, displayArea.getWidth());
        setEnvelopeSplit ((float) envelopeWidthFor (displayWidth, (float) (splitDragStartWidth + dx) / (float) displayWidth) / (float) displayWidth);
    };
    splitter.onReset = [this] { setEnvelopeSplit (DooblrKickAudioProcessor::defaultEnvelopeSplit); };
    addAndMakeVisible (splitter);
    envelopeEditor.setPlayheadTap (&audioProcessor.getPlayheadTap());
    masterEditor.setPlayheadTap (&audioProcessor.getPlayheadTap());

    waveform.onClick = [this] { audioProcessor.triggerAudition(); };
    addAndMakeVisible (waveform);

    addChildComponent (eqDisplay);
    addChildComponent (eqDisplay.getPeakHoldButton());
    addChildComponent (dynamicsDisplay);
    addChildComponent (limiterDisplay);
    addChildComponent (masterEditor);
    addChildComponent (mixDisplay);
    addChildComponent (driveDisplay);
    meterSpectrumScratch.resize (1 << 15);
    meterFrameScratch.resize (1 << 11);

    // Pages.
    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        const auto name = juce::String (params::layerNames[(size_t) layer]);
        auto& panels = pagePanels[(size_t) layer];
        panels.push_back (std::make_unique<ControlPanel> (state, name + " layer", pageColour (layer), layerCommonSpecs (layer)));
        panels.push_back (std::make_unique<ControlPanel> (state, name + " sound", pageColour (layer), layerSoundSpecs (layer)));
    }

    // v0.3.3: knobs for the master envelope (the selected point's Time / Level / Curve, plus Time
    // Scale and Depth), then Play and the mix in one row. On / off: the Envelope tab's power toggle.
    // v0.3.7: Time Scale is a large knob, twice the diameter of the others.
    // v0.3.8: Time Scale host name is "Time Scale"; readout shows 1.00x (multiplies all menv times).
    pagePanels[envelopePage].push_back (std::make_unique<ControlPanel> (state, "Master envelope", pageColour (envelopePage), Specs {
        { "menv_tscale", "Time scale", K::knob, 0, 0, -1, true }, { "menv_depth", "Depth", K::knob } }));
    pagePanels[envelopePage].push_back (std::make_unique<ControlPanel> (state, "Play & mix", pageColour (envelopePage), Specs {
        { "root_note", "Key", K::knob },
        { "vel_sens", "Velocity", K::knob }, { "gate", "Gate", K::toggle }, { "release", "Release", K::knob },
        { "mix_drywet", "Dry/Wet", K::knob }, { "chain_order", "Chain order", K::combo, 256 } }));
    pagePanels[drivePage].push_back (std::make_unique<ControlPanel> (state, "Drive", pageColour (drivePage), Specs {
        { "dist_type", "Type", K::combo }, { "dist_drive", "Drive", K::knob },
        { "dist_tone", "Tone", K::knob }, { "dist_bits", "Bits", K::knob }, { "dist_rate", "Rate", K::knob },
        { "dist_out", "Output", K::knob }, { "dist_mix", "Mix", K::knob } }));
    pagePanels[eqPage].push_back (std::make_unique<ControlPanel> (state, "EQ", pageColour (eqPage), Specs {
        { "eq_low_freq", "Low freq", K::knob }, { "eq_low_gain", "Low gain", K::knob },
        { "eq_mid_freq", "Mid freq", K::knob }, { "eq_mid_gain", "Mid gain", K::knob }, { "eq_mid_q", "Mid Q", K::knob },
        { "eq_high_freq", "High freq", K::knob }, { "eq_high_gain", "High gain", K::knob } }));
    pagePanels[dynamicsPage].push_back (std::make_unique<ControlPanel> (state, "Transient shaper", pageColour (dynamicsPage), Specs {
        { "shape_attack", "Attack", K::knob }, { "shape_sustain", "Sustain", K::knob } }));
    pagePanels[dynamicsPage].push_back (std::make_unique<ControlPanel> (state, "Compressor", pageColour (dynamicsPage), Specs {
        { "comp_threshold", "Threshold", K::knob }, { "comp_ratio", "Ratio", K::knob },
        { "comp_attack", "Attack", K::knob }, { "comp_release", "Release", K::knob }, { "comp_makeup", "Makeup", K::knob } }));
    pagePanels[outputPage].push_back (std::make_unique<ControlPanel> (state, "Limiter", pageColour (outputPage), Specs {
        { "lim_mode", "Mode", K::combo, 0, 1 }, { "lim_ceiling", "Ceiling", K::knob }, { "lim_release", "Release", K::knob },
        { "output_gain", "Gain", K::knob } }));

    for (auto& panels : pagePanels)
        for (auto& panel : panels)
            addChildComponent (*panel);

    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        if (! params::layerHasSample (layer))
            continue;

        auto& button = loadSampleButtons[(size_t) layer];
        button.setButtonText ("Load file...");
        button.setComponentID (lid (layer, "loadSample"));
        button.setTooltip ("Load a WAV / AIFF for this layer (selects the \"User file\" sample)");
        button.onClick = [this, layer] { loadSampleFile (layer); };

        auto& label = sampleFileLabels[(size_t) layer];
        styleLabel (label, "(no file loaded)", 11.0f, Palette::textMuted, juce::Justification::centredLeft);
        label.setComponentID (lid (layer, "sampleFile"));

        // v0.3.3: button over file name in one cell of the sound panel's sample slots.
        auto& cell = loadSampleCells[(size_t) layer];
        cell.addAndMakeVisible (button);
        cell.addAndMakeVisible (label);
        cell.onResize = [&cell, &button, &label]
        {
            auto area = cell.getLocalBounds().withTrimmedTop (18);
            button.setBounds (area.removeFromTop (26).reduced (0, 1));
            area.removeFromTop (2);
            label.setBounds (area.removeFromTop (18));
        };
        const auto place = loadCellPlace (layer);
        pagePanels[(size_t) layer].back()->addExtra (cell, ControlPanel::comboWidth, place.slot, place.before, true);
    }

    envelopePointKnobs = std::make_unique<dooblrkick::ui::EnvelopePointKnobs> (state, pageColour (envelopePage));
    pagePanels[envelopePage].front()->addExtra (*envelopePointKnobs, dooblrkick::ui::EnvelopePointKnobs::preferredWidth, -1, "menv_tscale", true);
    envelopePointKnobs->onPointChange = [this] (int point) { masterEditor.setSelectedNode (point); };
    masterEditor.onSelectedNodeChange = [this] (int point) { envelopePointKnobs->setPoint (point); };

    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        levelFaders[(size_t) layer] = std::make_unique<dooblrkick::ui::LevelFader> (state, lid (layer, "level"), pageColour (layer));
        addChildComponent (*levelFaders[(size_t) layer]);

        // v0.3.5: Key Track, a vertical switch under the fader, the same on every layer and in
        // both sources (it replaces the sample-only toggle; same parameter IDs).
        auto& key = keyTrackSwitches[(size_t) layer];
        key = std::make_unique<dooblrkick::ui::KeyTrackSwitch> (pageColour (layer));
        key->setComponentID (lid (layer, "keytrack"));
        key->setTitle (juce::String (params::layerNames[(size_t) layer]) + " Key Track");
        key->setTooltip (layer == dooblrkick::dsp::noiseLayer
                             ? "Key track: the MIDI note transposes the noise sample relative to the Key (the synth noise has no pitch)"
                             : "Key track: the MIDI note transposes this layer relative to the Key (off: every note plays the same pitch)");
        addChildComponent (*key);
        keyTrackAttachments[(size_t) layer] = std::make_unique<ButtonAttachment> (state, lid (layer, "keytrack"), *key);
    }

    // The Limiter panel's mode box offers Clipper / Limiter; Off is the Limiter tab's toggle.
    if (auto* modeBox = dynamic_cast<juce::ComboBox*> (pagePanels[outputPage].front()->findControl ("lim_mode")))
    {
        modeBox->setTextWhenNothingSelected ("Off");
        modeBox->setTooltip ("Limiter mode (switch the limiter on / off with the Limiter tab's toggle)");
    }

    // A source change (switch, automation, preset, undo, Random) re-lays the layer page at once.
    for (int layer = 0; layer < params::numLayers; ++layer)
        if (params::layerHasSample (layer))
            if (auto* source = state.getParameter (lid (layer, "source")))
                sourceWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*source, [this] (float) { updateEnablement(); }, nullptr));

    refreshPowerButtonsNow();
    outputAttachment = std::make_unique<SliderAttachment> (state, "output_gain", outputSlider);

    refreshPresetBox();
    selectPage (dooblrkick::dsp::bodyLayer);
    updateUndoButtons();
    audioProcessor.getUndoHistory().getUndoManager().addChangeListener (this);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (940, 600, 2400, 1400);
    setSize (1080, 660);
    waveform.refreshNow();
    startTimerHz (15);
    meterVBlank = std::make_unique<juce::VBlankAttachment> (this, [this] { pollMeters(); });
}

DooblrKickAudioProcessorEditor::~DooblrKickAudioProcessorEditor()
{
    meterVBlank.reset();
    sourceWatchers.clear();
    audioProcessor.getUndoHistory().getUndoManager().removeChangeListener (this);

    // The panels hold the load cells as (unowned) children; drop the panels first.
    for (auto& panels : pagePanels)
        panels.clear();
    for (auto& fader : levelFaders)
        fader.reset();
    for (auto& a : keyTrackAttachments)
        a.reset();
    masterEditor.onSelectedNodeChange = nullptr;
    envelopePointKnobs.reset();

    setLookAndFeel (nullptr);
}

//==============================================================================
void DooblrKickAudioProcessorEditor::selectPage (int page)
{
    selectedPage = juce::jlimit (0, numPages - 1, page);
    pageButtons[(size_t) selectedPage].setToggleState (true, juce::dontSendNotification);

    for (int p = 0; p < numPages; ++p)
        for (auto& panel : pagePanels[(size_t) p])
            panel->setVisible (p == selectedPage);

    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        levelFaders[(size_t) layer]->setVisible (layer == selectedPage);
        keyTrackSwitches[(size_t) layer]->setVisible (layer == selectedPage);
    }

    const auto visual = showsVisual();
    envelopeEditor.setVisible (! visual);
    // Only the Body has a pitch envelope: the switch is offered there alone.
    envelopeToggle.setVisible (selectedPage < params::numLayers);
    bindEnvelopeToPage();
    masterEditor.setVisible (selectedPage == envelopePage);
    mixDisplay.setVisible (selectedPage == envelopePage);
    driveDisplay.setVisible (selectedPage == drivePage);
    eqDisplay.setVisible (selectedPage == eqPage);
    eqDisplay.getPeakHoldButton().setVisible (selectedPage == eqPage);
    dynamicsDisplay.setVisible (selectedPage == dynamicsPage);
    limiterDisplay.setVisible (selectedPage == outputPage);

    if (visual)
    {
        // Start the view from now (drop what queued up while it was hidden).
        audioProcessor.getMeterTap().discardAll();
        lastMeterPollMs = juce::Time::getMillisecondCounterHiRes();
        if (selectedPage == eqPage) eqDisplay.reset();
        if (selectedPage == dynamicsPage) dynamicsDisplay.reset();
        if (selectedPage == outputPage) limiterDisplay.reset();
        if (selectedPage == envelopePage) mixDisplay.reset();
        if (selectedPage == drivePage) driveDisplay.reset();
    }

    updateSampleFileLabel();
    updateEnablement();
    resized();
    repaint();
}

void DooblrKickAudioProcessorEditor::showEnvelope (params::Envelope envelope)
{
    // A layer's view choice (each layer page remembers its own while other pages are shown).
    const auto layer = params::envelopeLayer (envelope);

    if (layer < 0)
        return;

    layerShowsAmp[(size_t) layer] = params::isAmpEnvelope (envelope);
    bindEnvelopeToPage();
    repaint();
}

void DooblrKickAudioProcessorEditor::bindEnvelopeToPage()
{
    // v0.3.5 fix: each layer page shows its own envelope. v0.3.6: every layer has a Pitch and
    // an Amp envelope, picked with the switch (remembered per layer).
    if (selectedPage >= params::numLayers)
        return;

    const auto showAmp = layerShowsAmp[(size_t) selectedPage];
    const auto wanted = params::layerEnvelope (selectedPage, showAmp);

    if (envelopeToggle.isRight() != showAmp) // leave a running slide alone
        envelopeToggle.setRight (showAmp, false, envelopeToggle.isShowing());

    envelopeToggle.setColours (Palette::dynamic, selectedPage == dooblrkick::dsp::bodyLayer ? Palette::accent
                                                                                            : dooblrkick::ui::bandColour (selectedPage));

    if (envelopeEditor.getEnvelope() != wanted)
        envelopeEditor.setEnvelope (wanted);
}

juce::String DooblrKickAudioProcessorEditor::getEnvelopeTitle() const
{
    if (selectedPage < params::numLayers)
        return juce::String (params::layerNames[(size_t) selectedPage]).toUpperCase()
               + (layerShowsAmp[(size_t) selectedPage] ? " AMP ENVELOPE" : " PITCH ENVELOPE");
    return {};
}

void DooblrKickAudioProcessorEditor::updateSampleFileLabel()
{
    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        if (! params::layerHasSample (layer))
            continue;

        const auto path = audioProcessor.getUserSamplePath (layer);
        auto& label = sampleFileLabels[(size_t) layer];
        const auto text = path.isEmpty() ? juce::String ("(no file loaded)") : juce::File (path).getFileName();

        if (label.getText() != text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setTooltip (path);
        }
    }
}

void DooblrKickAudioProcessorEditor::updateEnablement()
{
    // From the parameter itself, not the raw value: a parameter listener (the source watchers)
    // runs before the state's raw value has been updated.
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

    bool layoutChanged = false;

    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        // Noise Key Track only acts on a noise sample (the synth noise has no pitch).
        if (layer == dooblrkick::dsp::noiseLayer)
            keyTrackSwitches[(size_t) layer]->setEnabled (value (lid (layer, "source")) > 0.5f);

        const auto filterOn = value (lid (layer, "ftype")) > 0.5f;
        setEnabled (layer, lid (layer, "cutoff"), filterOn);
        setEnabled (layer, lid (layer, "reso"), filterOn);

        if (! params::layerHasSample (layer))
            continue;

        // v0.3.2: the controls of the other source are hidden (not just disabled) and the
        // rest reflow; the hidden parameters keep their values.
        const auto sampled = value (lid (layer, "source")) > 0.5f;
        const auto ids = sourceSpecificIds (layer);

        for (auto& panel : pagePanels[(size_t) layer])
        {
            for (const auto* id : ids.synthOnly)
                layoutChanged = panel->setShown (id, ! sampled) || layoutChanged;
            for (const auto* id : ids.sampleOnly)
                layoutChanged = panel->setShown (id, sampled) || layoutChanged;
            layoutChanged = panel->setShown (loadSampleCells[(size_t) layer], sampled) || layoutChanged;
        }
    }

    // Panel heights follow what is shown.
    if (layoutChanged && ! getBounds().isEmpty())
        resized();

    const auto crush = juce::roundToInt (value ("dist_type")) == 4;
    setEnabled (drivePage, "dist_bits", crush);
    setEnabled (drivePage, "dist_rate", crush);
    setEnabled (envelopePage, "release", value ("gate") > 0.5f);

    const auto limiter = juce::roundToInt (value ("lim_mode"));
    setEnabled (outputPage, "lim_mode", limiter > 0);
    setEnabled (outputPage, "lim_ceiling", limiter > 0);
    setEnabled (outputPage, "lim_release", limiter == 2);
}

//==============================================================================
namespace
{
    // Remembered (in the plugin state) while a stage is off, for the tab toggle to restore.
    const juce::Identifier dynamicsResumeProperty { "powerResumeDynamics" }; // bit 0 shaper, bit 1 compressor
    const juce::Identifier limiterResumeProperty { "powerResumeLimiter" };   // lim_mode index (1 or 2)
}

bool DooblrKickAudioProcessorEditor::isStageOn (int stage) const
{
    const auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    switch (stage)
    {
        case dooblrkick::dsp::driveStage:    return value ("dist_on") > 0.5f;
        case dooblrkick::dsp::eqStage:       return value ("eq_on") > 0.5f;
        case dooblrkick::dsp::dynamicsStage: return value ("shape_on") > 0.5f || value ("comp_on") > 0.5f;
        case dooblrkick::dsp::limiterStage:  return juce::roundToInt (value ("lim_mode")) != 0;
        default: return false;
    }
}

void DooblrKickAudioProcessorEditor::setStageOn (int stage, bool on)
{
    dooblrkick::ui::ParameterGesture gesture (state); // one gesture: one undo step

    switch (stage)
    {
        case dooblrkick::dsp::driveStage:
            gesture.begin ({ "dist_on" });
            gesture.set ("dist_on", on ? 1.0f : 0.0f);
            break;
        case dooblrkick::dsp::eqStage:
            gesture.begin ({ "eq_on" });
            gesture.set ("eq_on", on ? 1.0f : 0.0f);
            break;
        case dooblrkick::dsp::dynamicsStage:
        {
            gesture.begin ({ "shape_on", "comp_on" });

            if (on)
            {
                auto bits = (int) state.state.getProperty (dynamicsResumeProperty, 3);
                bits = (bits & 3) == 0 ? 3 : (bits & 3);
                gesture.set ("shape_on", (bits & 1) != 0 ? 1.0f : 0.0f);
                gesture.set ("comp_on", (bits & 2) != 0 ? 1.0f : 0.0f);
            }
            else
            {
                const auto bits = (gesture.get ("shape_on") > 0.5f ? 1 : 0) | (gesture.get ("comp_on") > 0.5f ? 2 : 0);
                if (bits != 0)
                    state.state.setProperty (dynamicsResumeProperty, bits, nullptr);
                gesture.set ("shape_on", 0.0f);
                gesture.set ("comp_on", 0.0f);
            }
            break;
        }
        case dooblrkick::dsp::limiterStage:
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

void DooblrKickAudioProcessorEditor::refreshPowerButtonsNow()
{
    for (int stage = 0; stage < dooblrkick::dsp::numChainStages; ++stage)
    {
        const auto on = isStageOn (stage);
        powerButtons[(size_t) stage]->setToggleState (on, juce::dontSendNotification);
        pageButtons[(size_t) pageForStage (stage)].setProcessorOn (on);
    }

    if (envelopePower != nullptr)
        pageButtons[(size_t) envelopePage].setProcessorOn (envelopePower->getToggleState());
}

//==============================================================================
void DooblrKickAudioProcessorEditor::refreshPresetBox()
{
    const juce::ScopedValueSetter<bool> guard (updatingPresetBox, true);
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");

    const auto& factory = dooblrkick::presets::getFactoryPresets();

    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, factoryIdBase + i);

    userPresetFiles.clear();
    const auto folder = dooblrkick::presets::getUserPresetFolder();

    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + dooblrkick::presets::userPresetExtension);
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

void DooblrKickAudioProcessorEditor::presetChosen()
{
    if (updatingPresetBox)
        return;

    const auto id = presetBox.getSelectedId();

    if (id >= userIdBase && id - userIdBase < (int) userPresetFiles.size())
    {
        juce::String error;

        if (! audioProcessor.loadUserPreset (userPresetFiles[(size_t) (id - userIdBase)], error))
            waveform.setStatus ("Could not load preset: " + error);
    }
    else if (id >= factoryIdBase)
    {
        audioProcessor.loadFactoryPreset (id - factoryIdBase, true);
    }

    selectPage (selectedPage);
}

void DooblrKickAudioProcessorEditor::savePreset()
{
    const auto folder = dooblrkick::presets::getUserPresetFolder();
    folder.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Save dooblr-kick preset",
                                                       folder.getChildFile (safeFileName (audioProcessor.getPresetName()) + dooblrkick::presets::userPresetExtension),
                                                       juce::String ("*") + dooblrkick::presets::userPresetExtension);
    juce::Component::SafePointer<DooblrKickAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || chooser.getResult() == juce::File())
                                      return;

                                  auto file = chooser.getResult().withFileExtension (dooblrkick::presets::userPresetExtension);
                                  juce::String error;

                                  if (safeThis->audioProcessor.saveUserPreset (file, error))
                                      safeThis->waveform.setStatus ("Saved " + file.getFullPathName());
                                  else
                                      safeThis->waveform.setStatus ("Could not save preset: " + error);

                                  safeThis->refreshPresetBox();
                              });
}

void DooblrKickAudioProcessorEditor::loadPreset()
{
    const auto folder = dooblrkick::presets::getUserPresetFolder();
    fileChooser = std::make_unique<juce::FileChooser> ("Load dooblr-kick preset", folder.isDirectory() ? folder : juce::File(),
                                                       juce::String ("*") + dooblrkick::presets::userPresetExtension);
    juce::Component::SafePointer<DooblrKickAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                      return;

                                  juce::String error;

                                  if (! safeThis->audioProcessor.loadUserPreset (chooser.getResult(), error))
                                      safeThis->waveform.setStatus ("Could not load preset: " + error);

                                  safeThis->refreshPresetBox();
                                  safeThis->selectPage (safeThis->selectedPage);
                              });
}

void DooblrKickAudioProcessorEditor::loadSampleFile (int layer)
{
    if (layer >= params::numLayers || ! params::layerHasSample (layer))
        return;

    fileChooser = std::make_unique<juce::FileChooser> ("Load a sample for the " + juce::String (params::layerNames[(size_t) layer]) + " layer",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff");
    juce::Component::SafePointer<DooblrKickAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis, layer] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || ! chooser.getResult().existsAsFile())
                                      return;

                                  juce::String error;

                                  if (! safeThis->audioProcessor.loadUserSample (layer, chooser.getResult(), error))
                                  {
                                      safeThis->waveform.setStatus ("Could not load sample: " + error);
                                      return;
                                  }

                                  dooblrkick::presets::apply (safeThis->state,
                                                              { { lid (layer, "source"), 1.0f },
                                                                { lid (layer, "sample"), (float) params::userSampleChoice } },
                                                              true);
                                  safeThis->updateSampleFileLabel();
                                  safeThis->waveform.setStatus ("Loaded " + chooser.getResult().getFileName());
                              });
}

bool DooblrKickAudioProcessorEditor::exportCurrentKick (juce::File& writtenFile, juce::String& error)
{
    // A timestamped file in the default folder (no dialog; for tests / scripted use).
    const auto folder = DooblrKickAudioProcessor::getDefaultExportFolder();
    writtenFile = folder.getNonexistentChildFile (safeFileName (audioProcessor.getPresetName()) + "_" + timestamp(), ".wav", false);
    const auto sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 48000.0;
    return audioProcessor.exportWav (writtenFile, sampleRate, error);
}

bool DooblrKickAudioProcessorEditor::exportToFile (const juce::File& chosen, juce::File& written, juce::String& error)
{
    const auto sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 48000.0;
    const auto ok = audioProcessor.exportTo (chosen, sampleRate, written, error);
    waveform.setStatus (ok ? "Exported " + written.getFullPathName() : "Export failed: " + error);
    return ok;
}

void DooblrKickAudioProcessorEditor::exportWav()
{
    // v0.3.5: Export asks where to save (an async native dialog; a plugin must never block).
    exportChooser = std::make_unique<juce::FileChooser> ("Export the kick as a WAV", audioProcessor.getExportStartFile(), "*.wav");
    juce::Component::SafePointer<DooblrKickAudioProcessorEditor> safeThis (this);

    exportChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || chooser.getResult() == juce::File())
                                      return; // cancelled

                                  juce::File written;
                                  juce::String error;
                                  safeThis->exportToFile (chooser.getResult(), written, error);
                              });
}

int DooblrKickAudioProcessorEditor::envelopeWidthFor (int displayWidth, float ratio) noexcept
{
    const auto available = displayWidth - dooblrkick::ui::SplitterBar::width;
    const auto r = std::isfinite (ratio) ? juce::jlimit (DooblrKickAudioProcessor::minEnvelopeSplit, DooblrKickAudioProcessor::maxEnvelopeSplit, ratio)
                                         : DooblrKickAudioProcessor::defaultEnvelopeSplit;
    const auto wanted = juce::roundToInt ((float) displayWidth * r);

    if (available < minEnvelopeWidth + minWaveformWidth)
        return juce::jmax (0, juce::roundToInt ((float) available * r)); // too small for both minimums: share

    return juce::jlimit (minEnvelopeWidth, available - minWaveformWidth, wanted);
}

void DooblrKickAudioProcessorEditor::setEnvelopeSplit (float ratio)
{
    const auto before = audioProcessor.getEnvelopeSplit();
    audioProcessor.setEnvelopeSplit (ratio);

    if (! juce::approximatelyEqual (before, audioProcessor.getEnvelopeSplit()))
    {
        resized();
        repaint (displayArea);
    }
}

void DooblrKickAudioProcessorEditor::pollMeters()
{
    if (! isShowing())
    {
        audioProcessor.getMeterTap().discardAll(); // nothing on screen: drop the meter data
        return;
    }

    readMeters();
}

void DooblrKickAudioProcessorEditor::readMeters()
{
    auto& tap = audioProcessor.getMeterTap();

    if (! showsVisual())
    {
        tap.discardAll(); // keep the FIFOs from filling with stale data while nothing is shown
        return;
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jlimit (0.0, 0.1, (now - lastMeterPollMs) * 0.001);
    lastMeterPollMs = now;

    if (selectedPage == eqPage)
    {
        tap.popFrames (meterFrameScratch.data(), (int) meterFrameScratch.size());
        const auto n = tap.popSpectrum (meterSpectrumScratch.data(), (int) meterSpectrumScratch.size());
        eqDisplay.update (meterSpectrumScratch.data(), n, tap.getSampleRate(), elapsed);
        return;
    }

    while (tap.popSpectrum (meterSpectrumScratch.data(), (int) meterSpectrumScratch.size()) > 0) {}
    const auto frames = tap.popFrames (meterFrameScratch.data(), (int) meterFrameScratch.size());

    if (selectedPage == envelopePage)
        mixDisplay.update (meterFrameScratch.data(), frames, elapsed);
    else if (selectedPage == drivePage)
        driveDisplay.update (meterFrameScratch.data(), frames, elapsed);
    else if (selectedPage == dynamicsPage)
        dynamicsDisplay.update (meterFrameScratch.data(), frames, elapsed);
    else
        limiterDisplay.update (meterFrameScratch.data(), frames, elapsed);
}

//==============================================================================
void DooblrKickAudioProcessorEditor::timerCallback()
{
    updateEnablement();
    refreshPowerButtonsNow();

    syncChainOrderNow();

    if (presetBox.getText() != audioProcessor.getPresetName() && ! presetBox.isPopupActive())
        refreshPresetBox();

    updateSampleFileLabel();
}

void DooblrKickAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateUndoButtons();
}

void DooblrKickAudioProcessorEditor::updateUndoButtons()
{
    auto& history = audioProcessor.getUndoHistory();
    undoButton.setEnabled (history.canUndo());
    redoButton.setEnabled (history.canRedo());
}

bool DooblrKickAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
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

//==============================================================================
void DooblrKickAudioProcessorEditor::paint (juce::Graphics& g)
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
    g.drawText ("LAYERS", layerTitleArea, juce::Justification::centredLeft);
    g.drawText ("PROCESSING", fxTitleArea, juce::Justification::centredLeft);
    if (showsVisual())
    {
        g.setColour (selectedPage == envelopePage ? Palette::master : Palette::dynamic);
        g.drawText (selectedPage == envelopePage ? "MASTER ENVELOPE  +  DRY / WET"
                    : selectedPage == drivePage  ? "DRIVE"
                    : selectedPage == eqPage     ? "OUTPUT EQ"
                    : selectedPage == dynamicsPage ? "COMPRESSOR" : "LIMITER",
                    envelopeTitleArea, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (envelopeEditor.getCurveColour());
        g.drawText (getEnvelopeTitle(), envelopeTitleArea, juce::Justification::centredLeft);
    }
}

void DooblrKickAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    headerArea = bounds.removeFromTop (60);
    {
        // Fixed-size buttons; the title, preset menu and output slider share what is left, so the
        // header fits from the minimum width (940 px) up without overlapping.
        auto header = headerArea.reduced (32, 10);
        // v0.3.4: the version moved into the subtitle; its old slot holds the output meter
        // (narrower), and the space saved goes to the Output slider first.
        constexpr int fixedLeft = 8 + 50 + 4 + 50 + 12 + 4 + 46 + 4 + 46 + 8 + 64, fixedRight = meterWidth + 10 + 48 + 4 + 82 + 12, captionWidth = 52;
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
        header.removeFromRight (10);
        playButton.setBounds (header.removeFromRight (30).withSizeKeepingCentre (30, 24));
        header.removeFromRight (4);
        exportButton.setBounds (header.removeFromRight (64).withSizeKeepingCentre (64, 24));
        header.removeFromRight (12);
        outputSlider.setBounds (header.removeFromRight (sliderWidth).withSizeKeepingCentre (sliderWidth, 24));
        outputCaption.setBounds (header.removeFromRight (52));
    }

    navArea = bounds.removeFromLeft (150);
    {
        // The tabs share the column's height (at least 28 px each), so they fill it at any size.
        auto nav = navArea.reduced (12, 12);
        constexpr int titleHeight = 20, groupGap = 10;
        const auto pitch = juce::jmax (28, (nav.getHeight() - 2 * titleHeight - groupGap) / numPages);
        const auto gap = juce::jlimit (2, 4, pitch / 14);
        layerTitleArea = nav.removeFromTop (titleHeight);

        for (int p = 0; p < numPages; ++p)
        {
            if (p == envelopePage)
            {
                nav.removeFromTop (groupGap);
                fxTitleArea = nav.removeFromTop (titleHeight);
            }

            const auto slot = nav.removeFromTop (pitch).reduced (0, gap);

            if (p >= drivePage)
            {
                chainSlotBounds[(size_t) (p - drivePage)] = slot;
                continue;
            }

            auto& button = pageButtons[(size_t) p];
            button.setBounds (slot);

            if (p < params::numLayers)
            {
                layerSwitches[(size_t) p]->setBounds (6, (button.getHeight() - LayerTabSwitch::switchHeight - 6) / 2,
                                                      LayerTabSwitch::switchWidth + 6, LayerTabSwitch::switchHeight + 6);
                soloButtons[(size_t) p]->setBounds (button.getWidth() - 26, 0, 24, button.getHeight());
            }

            if (p == envelopePage)
                envelopePower->setBounds (button.getPowerArea());
        }

        layoutChainButtons();
    }

    // Controls: the selected page's panels, each at its preferred height. Layer pages: the Level
    // fader takes the left edge, as tall as the panels (v0.3.3).
    auto content = bounds;
    const auto isLayer = selectedPage < params::numLayers;
    constexpr int faderColumn = dooblrkick::ui::LevelFader::width + 12;
    const auto controlsWidth = content.getWidth() - 40 - (isLayer ? faderColumn : 0);
    auto panelsHeight = 0;

    for (auto& panel : pagePanels[(size_t) selectedPage])
        panelsHeight += panel->getPreferredHeight (controlsWidth) + 10;

    controlsArea = content.removeFromBottom (juce::jmin (panelsHeight + 14, content.getHeight() - 200));
    {
        auto controls = controlsArea.reduced (20, 10);

        if (isLayer)
        {
            auto column = controls.removeFromLeft (faderColumn);
            column = column.withWidth (dooblrkick::ui::LevelFader::width).withTrimmedBottom (juce::jmax (0, column.getHeight() - panelsHeight + 10));
            // v0.3.5: the Key Track switch under the fader.
            keyTrackSwitches[(size_t) selectedPage]->setBounds (column.removeFromBottom (dooblrkick::ui::KeyTrackSwitch::height));
            column.removeFromBottom (4);
            levelFaders[(size_t) selectedPage]->setBounds (column);
        }

        for (auto& panel : pagePanels[(size_t) selectedPage])
        {
            panel->setBounds (controls.removeFromTop (panel->getPreferredHeight (controlsWidth)));
            controls.removeFromTop (10);
        }
    }

    displayArea = content.reduced (16, 10);
    {
        auto display = displayArea;
        // v0.3.5: the envelope / waveform split is the user's (the divider), clamped so neither clips.
        auto envelopeArea = display.removeFromLeft (envelopeWidthFor (display.getWidth(), audioProcessor.getEnvelopeSplit()));
        splitter.setBounds (display.removeFromLeft (dooblrkick::ui::SplitterBar::width));

        auto bar = envelopeArea.removeFromTop (24);
        envelopeTitleArea = bar.removeFromLeft (180);
        eqDisplay.getPeakHoldButton().setBounds (bar.withLeft (bar.getRight() - 96).reduced (0, 1));
        envelopeToggle.setBounds (bar.removeFromRight (dooblrkick::ui::SlideToggle::preferredWidth)
                                      .withSizeKeepingCentre (dooblrkick::ui::SlideToggle::preferredWidth, dooblrkick::ui::SlideToggle::preferredHeight));
        envelopeArea.removeFromTop (4);
        envelopeEditor.setBounds (envelopeArea);
        driveDisplay.setBounds (envelopeArea);
        {
            auto master = envelopeArea;
            const auto mixHeight = juce::jlimit (72, 180, master.getHeight() * 2 / 5);
            mixDisplay.setBounds (master.removeFromBottom (mixHeight));
            master.removeFromBottom (6);
            masterEditor.setBounds (master);
        }
        eqDisplay.setBounds (envelopeArea);
        dynamicsDisplay.setBounds (envelopeArea);
        limiterDisplay.setBounds (envelopeArea);

        display.removeFromTop (28);
        waveform.setBounds (display);
    }
}

//==============================================================================
// Processing-chain order in the left column.

dooblrkick::dsp::ChainOrder DooblrKickAudioProcessorEditor::chainOrderParameter() const
{
    return dooblrkick::dsp::chainOrderFromIndex (juce::roundToInt (state.getRawParameterValue ("chain_order")->load()));
}

void DooblrKickAudioProcessorEditor::syncChainOrderNow()
{
    // The chain order can change from undo, automation or a preset: follow it.
    if (draggingStage < 0 && chainOrderParameter() != shownOrder)
    {
        shownOrder = chainOrderParameter();
        layoutChainButtons();
    }
}

void DooblrKickAudioProcessorEditor::layoutChainButtons()
{
    for (int slot = 0; slot < dooblrkick::dsp::numChainStages; ++slot)
    {
        const auto stage = shownOrder[(size_t) slot];
        auto& button = pageButtons[(size_t) pageForStage (stage)];
        button.setChainSlot (slot);
        auto bounds = chainSlotBounds[(size_t) slot];

        if (stage == draggingStage)
        {
            // The dragged tab follows the mouse, within the chain's slots.
            const auto top = chainSlotBounds.front().getY(), bottom = chainSlotBounds.back().getY();
            bounds.setY (juce::jlimit (top, bottom, dragY - dragGrabOffset));
            button.toFront (false);
        }

        button.setBounds (bounds);
        powerButtons[(size_t) stage]->setBounds (button.getPowerArea());
    }
}

void DooblrKickAudioProcessorEditor::chainHandleDown (int stage, const juce::MouseEvent& e)
{
    shownOrder = chainOrderParameter();
    draggingStage = stage;
    const auto local = e.getEventRelativeTo (this).getPosition();
    dragGrabOffset = local.y - pageButtons[(size_t) pageForStage (stage)].getY();
    dragY = local.y;

    if (auto* p = state.getParameter ("chain_order"))
        p->beginChangeGesture(); // one undo step for the whole drag
}

void DooblrKickAudioProcessorEditor::chainHandleDrag (int stage, const juce::MouseEvent& e)
{
    if (stage != draggingStage)
        return;

    dragY = e.getEventRelativeTo (this).getPosition().y;

    // The slot under the dragged tab's centre.
    const auto first = chainSlotBounds.front();
    const auto pitchY = (float) (chainSlotBounds[1].getY() - first.getY());
    const auto centre = (float) (dragY - dragGrabOffset) + (float) first.getHeight() * 0.5f;
    const auto target = juce::jlimit (0, dooblrkick::dsp::numChainStages - 1,
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
            p->setValueNotifyingHost (p->convertTo0to1 ((float) dooblrkick::dsp::chainOrderToIndex (order)));
    }

    layoutChainButtons();
}

void DooblrKickAudioProcessorEditor::chainHandleUp (int stage, const juce::MouseEvent&)
{
    if (stage != draggingStage)
        return;

    draggingStage = -1;

    if (auto* p = state.getParameter ("chain_order"))
        p->endChangeGesture();

    shownOrder = chainOrderParameter();
    layoutChainButtons();
}
