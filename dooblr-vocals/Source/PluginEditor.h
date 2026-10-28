#pragma once

#include <array>
#include <memory>
#include <vector>

#include "DSP/ProcessingChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/FxDisplays.h"
#include "UI/GateDisplay.h"
#include "UI/HarmonyDisplay.h"
#include "UI/KeyFollowStrip.h"
#include "UI/OutputMeter.h"
#include "UI/OutputVisuals.h"
#include "UI/PitchDisplay.h"
#include "UI/Theme.h"

class DooblrVocalsAudioProcessor;

class ProcessorPowerButton final : public juce::Button
{
public:
    explicit ProcessorPowerButton (int stageIndex);
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    int getStage() const noexcept { return stage; }

    static constexpr int size = 20;
    static constexpr int pitchStage = -1, harmonyStage = -2, delayStage = -3, reverbStage = -4;

private:
    int stage;
};

class NavButton final : public juce::TextButton
{
public:
    void setChainSlot (int slot);
    int getChainSlot() const noexcept { return chainSlot; }
    juce::Rectangle<int> getHandleArea() const;
    bool isDraggingHandle() const noexcept { return handleDrag; }
    void setHasPowerToggle (bool has);
    void setProcessorOn (bool on);
    bool isProcessorOn() const noexcept { return processorOn; }
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

class DooblrVocalsAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer,
                                               private juce::ChangeListener
{
public:
    explicit DooblrVocalsAudioProcessorEditor (DooblrVocalsAudioProcessor&);
    ~DooblrVocalsAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    enum Page { pitchPage = 0, harmonyPage, gatePage, eqPage, dynamicsPage, limiterPage, delayPage, reverbPage, numPages };
    static int pageForStage (int stage) noexcept { return gatePage + stage; }
    void selectPage (int page);
    int getSelectedPage() const noexcept { return selectedPage; }

    const std::vector<std::unique_ptr<dooblrvocals::ui::ControlPanel>>& getPanels (int page) const noexcept
    {
        return pagePanels[(size_t) page];
    }

    juce::ComboBox& getPresetBox() noexcept { return presetBox; }
    dooblrvocals::ui::EqDisplay& getEqDisplay() noexcept { return eqDisplay; }
    dooblrvocals::ui::DynamicsDisplay& getDynamicsDisplay() noexcept { return dynamicsDisplay; }
    dooblrvocals::ui::LimiterDisplay& getLimiterDisplay() noexcept { return limiterDisplay; }
    dooblrvocals::ui::GateDisplay& getGateDisplay() noexcept { return gateDisplay; }
    dooblrvocals::ui::PitchDisplay& getPitchDisplay() noexcept { return pitchDisplay; }
    dooblrvocals::ui::HarmonyDisplay& getHarmonyDisplay() noexcept { return harmonyDisplay; }
    dooblrvocals::ui::ReverbDisplay& getReverbDisplay() noexcept { return reverbDisplay; }
    dooblrvocals::ui::DelayDisplay& getDelayDisplay() noexcept { return delayDisplay; }
    dooblrvocals::ui::FxRoutingView& getRoutingView (int page) noexcept { return page == delayPage ? delayRouting : reverbRouting; }
    juce::ComboBox& getSectionPresetBox (int page) noexcept { return page == delayPage ? delayPresetBox : reverbPresetBox; }
    dooblrvocals::ui::SegmentedToggle& getModeToggle() noexcept { return *modeToggle; }
    dooblrvocals::ui::KeyFollowStrip& getKeyFollowStrip() noexcept { return *keyFollowStrip; }
    juce::TextButton& getLearnButton() noexcept { return learnButton; }
    juce::Label& getLearnLabel() noexcept { return learnLabel; }
    juce::TextButton& getLearnResetButton() noexcept { return learnResetButton; }
    /** The Learn Range button: start, or stop and apply. */
    void toggleRangeLearn();
    void refreshLearnUiNow() { updateLearnUi(); }
    /** One VBlank frame of the Learn Range pulse at a given time (ms); for tests. */
    void pulseLearnNow (double nowMs) { updateLearnPulse (nowMs); }
    void refreshEnablementNow() { updateEnablement(); updateLearnUi(); }
    juce::Label& getLatencyLabel() noexcept { return latencyLabel; }
    void refreshLatencyLabelNow() { updateLatencyLabel(); }
    dooblrvocals::ui::OutputMeter& getOutputMeter() noexcept { return outputMeter; }
    juce::Rectangle<int> getHeaderArea() const noexcept { return headerArea; }
    juce::Label& getStatusLabel() noexcept { return statusLabel; }

    static constexpr int meterWidth = 20;
    NavButton& getPageButton (int page) noexcept { return pageButtons[(size_t) page]; }
    ProcessorPowerButton& getPowerButton (int stage) noexcept { return *powerButtons[(size_t) stage]; }
    ProcessorPowerButton& getPitchPowerButton() noexcept { return *pitchPower; }
    ProcessorPowerButton& getHarmonyPowerButton() noexcept { return *harmonyPower; }
    ProcessorPowerButton& getFxPowerButton (int page) noexcept { return page == delayPage ? *delayPower : *reverbPower; }

    bool isStageOn (int stage) const;
    void setStageOn (int stage, bool on);
    void refreshPowerButtonsNow();
    dooblrvocals::dsp::ChainOrder getShownChainOrder() const noexcept { return shownOrder; }
    void pollMetersNow() { readMeters(); }
    void syncChainOrderNow();
    /** Key Follow readout, followed-key displays and Key / Scale dimming now (the timer does this at 15 Hz). */
    void refreshKeyFollowNow() { updateKeyFollowUi(); updateEnablement(); }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateUndoButtons();
    void updateEnablement();
    void refreshPresetBox();
    void presetChosen();
    void savePreset();
    void loadPreset();
    void pollMeters();
    void readMeters();
    dooblrvocals::dsp::ChainOrder chainOrderParameter() const;
    void layoutChainButtons();
    void chainHandleDown (int stage, const juce::MouseEvent&);
    void chainHandleDrag (int stage, const juce::MouseEvent&);
    void chainHandleUp (int stage, const juce::MouseEvent&);
    void setStatus (const juce::String& text);
    void updateLatencyLabel();
    void updateLearnUi();
    void updateLearnPulse (double nowMs);
    void sectionPresetChosen (int page);

    DooblrVocalsAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& state;
    dooblrvocals::ui::DooblrLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Rectangle<int> headerArea, navArea, displayArea, controlsArea;
    juce::Rectangle<int> correctionTitleArea, fxTitleArea, timeFxTitleArea, graphTitleArea;

    juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel, latencyLabel;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, loadButton { "Load" }, randomButton { "Random" };
    juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    std::array<NavButton, numPages> pageButtons;
    std::array<juce::Rectangle<int>, dooblrvocals::dsp::numChainStages> chainSlotBounds;
    dooblrvocals::dsp::ChainOrder shownOrder { 0, 1, 2, 3 };
    int draggingStage = -1, dragGrabOffset = 0, dragY = 0;
    std::array<std::unique_ptr<ProcessorPowerButton>, dooblrvocals::dsp::numChainStages> powerButtons;
    std::unique_ptr<ProcessorPowerButton> pitchPower, harmonyPower, delayPower, reverbPower;

    dooblrvocals::ui::PitchDisplay pitchDisplay;
    dooblrvocals::ui::HarmonyDisplay harmonyDisplay;
    dooblrvocals::ui::GateDisplay gateDisplay;
    dooblrvocals::ui::EqDisplay eqDisplay;
    dooblrvocals::ui::DynamicsDisplay dynamicsDisplay;
    dooblrvocals::ui::LimiterDisplay limiterDisplay;
    dooblrvocals::ui::DelayDisplay delayDisplay;
    dooblrvocals::ui::ReverbDisplay reverbDisplay;
    dooblrvocals::ui::FxRoutingView delayRouting, reverbRouting;
    juce::ComboBox delayPresetBox, reverbPresetBox;
    std::unique_ptr<dooblrvocals::ui::SegmentedToggle> modeToggle; // corr_mode: KEYED | CHROMATIC (pitch page bar)
    std::unique_ptr<dooblrvocals::ui::KeyFollowStrip> keyFollowStrip; // key_follow / key_lock / key_sens / key_source (pitch page, under the bar)
    void updateKeyFollowUi();
    juce::TextButton learnButton { "Learn Range" }, learnResetButton { "Reset" };
    juce::Label learnLabel;
    juce::String learnMessage;
    dooblrvocals::ui::OutputMeter outputMeter;
    std::vector<float> meterSpectrumScratch;
    std::vector<dooblrvocals::dsp::MeterFrame> meterFrameScratch;
    std::vector<dooblrvocals::dsp::PitchFrame> pitchFrameScratch;
    double lastMeterPollMs = 0.0;
    std::unique_ptr<juce::VBlankAttachment> meterVBlank;

    std::array<std::vector<std::unique_ptr<dooblrvocals::ui::ControlPanel>>, numPages> pagePanels;

    int selectedPage = pitchPage;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<juce::File> userPresetFiles;
    bool updatingPresetBox = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ButtonAttachment> pitchPowerAttachment, harmonyPowerAttachment, delayPowerAttachment, reverbPowerAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrVocalsAudioProcessorEditor)
};
