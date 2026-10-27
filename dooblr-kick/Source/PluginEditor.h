#pragma once

#include <array>
#include <functional>
#include <memory>

#include "DSP/ProcessingChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/EnvelopeEditor.h"
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

    std::function<void (const juce::MouseEvent&)> onHandleDown, onHandleDrag, onHandleUp;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int chainSlot = -1;
    bool handleDrag = false;
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
    dooblrkick::ui::WaveformView& getWaveformView() noexcept { return waveform; }
    juce::ComboBox& getPresetBox() noexcept { return presetBox; }
    dooblrkick::ui::EqDisplay& getEqDisplay() noexcept { return eqDisplay; }
    dooblrkick::ui::DynamicsDisplay& getDynamicsDisplay() noexcept { return dynamicsDisplay; }
    dooblrkick::ui::LimiterDisplay& getLimiterDisplay() noexcept { return limiterDisplay; }
    dooblrkick::ui::EnvelopeEditor& getMasterEnvelopeEditor() noexcept { return masterEditor; }
    dooblrkick::ui::MixDisplay& getMixDisplay() noexcept { return mixDisplay; }
    dooblrkick::ui::DriveDisplay& getDriveDisplay() noexcept { return driveDisplay; }
    NavButton& getPageButton (int page) noexcept { return pageButtons[(size_t) page]; }
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

    juce::Label titleLabel, subtitleLabel, versionLabel, outputCaption;
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

    std::array<juce::TextButton, dooblrkick::params::numLayers> loadSampleButtons;
    std::array<juce::Label, dooblrkick::params::numLayers> sampleFileLabels;

    juce::TextButton pitchEnvButton { "Pitch env" }, ampEnvButton { "Amp env" };
    dooblrkick::ui::EnvelopeEditor envelopeEditor;
    dooblrkick::ui::WaveformView waveform;

    // Live views on the EQ / Dynamics / Limiter pages (in place of the envelope editor).
    dooblrkick::ui::EqDisplay eqDisplay;
    dooblrkick::ui::DynamicsDisplay dynamicsDisplay;
    dooblrkick::ui::LimiterDisplay limiterDisplay;

    // v0.3.0: the Envelope page (master envelope + dry / wet) and the Drive page view.
    dooblrkick::ui::EnvelopeEditor masterEditor;
    dooblrkick::ui::MixDisplay mixDisplay;
    dooblrkick::ui::DriveDisplay driveDisplay;
    std::vector<float> meterSpectrumScratch;
    std::vector<dooblrkick::dsp::MeterFrame> meterFrameScratch;
    double lastMeterPollMs = 0.0;
    std::unique_ptr<juce::VBlankAttachment> meterVBlank;

    // Per page: one or two panels (owned; only the selected page's are visible).
    std::array<std::vector<std::unique_ptr<dooblrkick::ui::ControlPanel>>, numPages> pagePanels;

    int selectedPage = 1;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<juce::File> userPresetFiles;
    bool updatingPresetBox = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, dooblrkick::params::numLayers> layerSwitchAttachments, soloAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrKickAudioProcessorEditor)
};
