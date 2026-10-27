#pragma once

#include <array>
#include <functional>
#include <memory>

#include "DSP/ProcessingChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/EnvelopeEditor.h"
#include "UI/OutputMeter.h"
#include "UI/OutputVisuals.h"
#include "UI/ProcessingVisuals.h"
#include "UI/Theme.h"
#include "UI/WaveformView.h"

class DooblrKickAudioProcessor;

/** Pill on/off switch drawn inside a layer tab (same style as dooblr-q's band switch). */
class LayerTabSwitch final : public juce::ToggleButton
{
public:
    explicit LayerTabSwitch (int layerIndex);
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    static constexpr int switchWidth = 24, switchHeight = 13;

private:
    int layer;
};

/** Small "S" solo button for a layer tab. */
class SoloButton final : public juce::ToggleButton
{
public:
    explicit SoloButton (int layerIndex);
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    int layer;
};

/**
    Power toggle on a processing tab (Drive, EQ, Dynamics, Limiter): the only place a processor
    is switched on or off. It is a child of its tab, so it moves with the tab when the chain is
    reordered, and it takes its own clicks, so toggling never selects the tab.
    v0.3.3: the Envelope tab has one too (stage -1, "power_envelope", bound to menv_on).
*/
class ProcessorPowerButton final : public juce::Button
{
public:
    explicit ProcessorPowerButton (int stageIndex);
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    int getStage() const noexcept { return stage; }

    static constexpr int size = 20;
    static constexpr int envelopeStage = -1;

private:
    int stage;
};

/**
    A tab in the left column. Processing-chain tabs (slot >= 0) also show their position
    in the chain and a hamburger handle: dragging the handle reorders the chain, a click
    anywhere else selects the page as usual.
*/
class NavButton final : public juce::TextButton
{
public:
    void setChainSlot (int slot);
    int getChainSlot() const noexcept { return chainSlot; }
    juce::Rectangle<int> getHandleArea() const;
    bool isDraggingHandle() const noexcept { return handleDrag; }
    /** Tabs with a power toggle that are not in the chain (v0.3.3: Envelope). */
    void setHasPowerToggle (bool has);
    /** Processing / Envelope tabs: draws the tab dimmed while it is off (it stays selectable). */
    void setProcessorOn (bool on);
    bool isProcessorOn() const noexcept { return processorOn; }
    /** Where the power toggle sits (left of the label, after the chain position). */
    juce::Rectangle<int> getPowerArea() const;

    std::function<void (const juce::MouseEvent&)> onHandleDown, onHandleDrag, onHandleUp;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int chainSlot = -1;
    bool handleDrag = false, processorOn = true, hasPowerToggle = false;
};

class DooblrKickAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             private juce::ChangeListener
{
public:
    explicit DooblrKickAudioProcessorEditor (DooblrKickAudioProcessor&);
    ~DooblrKickAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Pages: 0..4 = layers (Click, Body, Sub, Tail, Noise), then Envelope (master envelope, dry / wet
        and the Play controls), and the four processing stages Drive, EQ, Dynamics, Limiter. The
        processing tabs are listed in the current chain order. */
    enum Page { envelopePage = dooblrkick::params::numLayers, drivePage, eqPage, dynamicsPage, outputPage, numPages };
    static int pageForStage (int stage) noexcept { return drivePage + stage; }
    void selectPage (int page);
    int getSelectedPage() const noexcept { return selectedPage; }
    void showEnvelope (dooblrkick::params::Envelope envelope);

    // For tests.
    dooblrkick::ui::EnvelopeEditor& getEnvelopeEditor() noexcept { return envelopeEditor; }
    dooblrkick::ui::LevelFader& getLevelFader (int layer) noexcept { return *levelFaders[(size_t) layer]; }
    dooblrkick::ui::EnvelopePointKnobs& getEnvelopePointKnobs() noexcept { return *envelopePointKnobs; }
    /** A page's control panels (layer pages: "layer", then "sound"). */
    const std::vector<std::unique_ptr<dooblrkick::ui::ControlPanel>>& getPanels (int page) const noexcept { return pagePanels[(size_t) page]; }
    dooblrkick::ui::WaveformView& getWaveformView() noexcept { return waveform; }
    juce::ComboBox& getPresetBox() noexcept { return presetBox; }
    dooblrkick::ui::EqDisplay& getEqDisplay() noexcept { return eqDisplay; }
    dooblrkick::ui::DynamicsDisplay& getDynamicsDisplay() noexcept { return dynamicsDisplay; }
    dooblrkick::ui::LimiterDisplay& getLimiterDisplay() noexcept { return limiterDisplay; }
    dooblrkick::ui::EnvelopeEditor& getMasterEnvelopeEditor() noexcept { return masterEditor; }
    dooblrkick::ui::OutputMeter& getOutputMeter() noexcept { return outputMeter; }
    juce::Rectangle<int> getHeaderArea() const noexcept { return headerArea; }
    static constexpr int meterWidth = 20;
    dooblrkick::ui::MixDisplay& getMixDisplay() noexcept { return mixDisplay; }
    dooblrkick::ui::DriveDisplay& getDriveDisplay() noexcept { return driveDisplay; }
    NavButton& getPageButton (int page) noexcept { return pageButtons[(size_t) page]; }
    /** The power toggle on a processing stage's tab (stage = dsp chain stage index). */
    ProcessorPowerButton& getPowerButton (int stage) noexcept { return *powerButtons[(size_t) stage]; }
    /** The master envelope's on / off toggle on the Envelope tab (v0.3.3, bound to menv_on). */
    ProcessorPowerButton& getEnvelopePowerButton() noexcept { return *envelopePower; }
    /** Whether a processing stage is on: Drive / EQ by their switch, Dynamics if the shaper or
        the compressor is on, Limiter unless its mode is Off. */
    bool isStageOn (int stage) const;
    /** What the tab power toggle does (one undo step). Dynamics off remembers which of the shaper
        and compressor were on and on restores them (both if none remembered); Limiter off
        remembers Clipper / Limiter and on restores it (Limiter if none remembered). */
    void setStageOn (int stage, bool on);
    /** Re-reads the processors' on state into the tab toggles (normally on the timer). */
    void refreshPowerButtonsNow();
    /** Shows the source-specific layer controls for each layer's Synth / Sample source (v0.3.2;
        normally on a source change or the timer). */
    void refreshLayerSourceLayoutNow() { updateEnablement(); }
    /** The chain order the left column currently shows (stage per slot). */
    dooblrkick::dsp::ChainOrder getShownChainOrder() const noexcept { return shownOrder; }
    /** Reads the processor's meter tap into the visible page's view (normally every vblank). */
    void pollMetersNow() { readMeters(); }
    /** Follows a chain order changed by undo, automation or a preset (the timer calls this). */
    void syncChainOrderNow();
    bool exportCurrentKick (juce::File& writtenFile, juce::String& error);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateUndoButtons();
    void updateEnablement();
    void refreshPresetBox();
    void presetChosen();
    void savePreset();
    void loadPreset();
    void loadSampleFile (int layer);
    void exportWav();
    void updateSampleFileLabel();
    void pollMeters();
    void readMeters();
    bool showsVisual() const noexcept { return selectedPage >= envelopePage; } // pages with a live view fed by the meter tap
    dooblrkick::dsp::ChainOrder chainOrderParameter() const;
    void layoutChainButtons();
    void chainHandleDown (int stage, const juce::MouseEvent&);
    void chainHandleDrag (int stage, const juce::MouseEvent&);
    void chainHandleUp (int stage, const juce::MouseEvent&);

    DooblrKickAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& state;
    dooblrkick::ui::DooblrLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Rectangle<int> headerArea, navArea, displayArea, controlsArea;
    juce::Rectangle<int> layerTitleArea, fxTitleArea, envelopeTitleArea;

    juce::Label titleLabel, subtitleLabel, outputCaption;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, loadButton { "Load" }, randomButton { "Random" },
                     exportButton { "Export WAV" }, playButton { "Play" };
    juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    std::array<NavButton, numPages> pageButtons;
    std::array<juce::Rectangle<int>, dooblrkick::dsp::numChainStages> chainSlotBounds;
    dooblrkick::dsp::ChainOrder shownOrder { 0, 1, 2, 3 };
    int draggingStage = -1, dragGrabOffset = 0, dragY = 0;
    std::array<std::unique_ptr<LayerTabSwitch>, dooblrkick::params::numLayers> layerSwitches;
    std::array<std::unique_ptr<SoloButton>, dooblrkick::params::numLayers> soloButtons;
    std::array<std::unique_ptr<ProcessorPowerButton>, dooblrkick::dsp::numChainStages> powerButtons;
    std::unique_ptr<ProcessorPowerButton> envelopePower;

    std::array<juce::TextButton, dooblrkick::params::numLayers> loadSampleButtons;
    std::array<juce::Label, dooblrkick::params::numLayers> sampleFileLabels;
    /** v0.3.3: a layer's Load button over its file name, one fixed cell among the sample controls. */
    struct StackCell final : public juce::Component
    {
        std::function<void()> onResize;
        void resized() override { if (onResize) onResize(); }
    };
    std::array<StackCell, dooblrkick::params::numLayers> loadSampleCells;
    std::array<std::unique_ptr<dooblrkick::ui::LevelFader>, dooblrkick::params::numLayers> levelFaders;
    std::unique_ptr<dooblrkick::ui::EnvelopePointKnobs> envelopePointKnobs;

    juce::TextButton pitchEnvButton { "Pitch env" }, ampEnvButton { "Amp env" };
    dooblrkick::ui::EnvelopeEditor envelopeEditor;
    dooblrkick::ui::WaveformView waveform;

    // Live views on the EQ / Dynamics / Limiter pages (in place of the envelope editor).
    dooblrkick::ui::EqDisplay eqDisplay;
    dooblrkick::ui::DynamicsDisplay dynamicsDisplay;
    dooblrkick::ui::LimiterDisplay limiterDisplay;

    // v0.3.0: the Envelope page (master envelope + dry / wet) and the Drive page view.
    dooblrkick::ui::EnvelopeEditor masterEditor;
    dooblrkick::ui::OutputMeter outputMeter;
    dooblrkick::ui::MixDisplay mixDisplay;
    dooblrkick::ui::DriveDisplay driveDisplay;
    std::vector<float> meterSpectrumScratch;
    std::vector<dooblrkick::dsp::MeterFrame> meterFrameScratch;
    double lastMeterPollMs = 0.0;
    std::unique_ptr<juce::VBlankAttachment> meterVBlank;

    // Per page: one or two panels (owned; only the selected page's are visible).
    std::array<std::vector<std::unique_ptr<dooblrkick::ui::ControlPanel>>, numPages> pagePanels;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> sourceWatchers;

    int selectedPage = 1;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<juce::File> userPresetFiles;
    bool updatingPresetBox = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, dooblrkick::params::numLayers> layerSwitchAttachments, soloAttachments;
    std::unique_ptr<ButtonAttachment> envelopePowerAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrKickAudioProcessorEditor)
};
