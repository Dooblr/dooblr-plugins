#pragma once

#include "PluginProcessor.h"
#include "Parameters.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"

#include <memory>
#include <vector>

namespace dooblrchroma
{
    class DooblrChromaEditor final : public juce::AudioProcessorEditor,
                                    private juce::Timer,
                                    private juce::ChangeListener
    {
    public:
        explicit DooblrChromaEditor (DooblrChromaProcessor&);
        ~DooblrChromaEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getHeaderHeight() const;
        juce::Rectangle<int> getContentArea() const;
        const std::vector<ui::Panel*>& getPanels() const noexcept { return panels; }
        ui::ResonatorView& getViewA() noexcept { return viewA; }
        ui::ResonatorView& getViewB() noexcept { return viewB; }
        int getEditLayer() const noexcept { return editLayer; }
        void setEditLayer (int layer);
        juce::String getAPitchParamId() const { return aPitch.paramId; }

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void rebuildPresetMenu();
        void bindLayer();
        void layoutKnobs (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int columns);
        void refreshDimmed();

        DooblrChromaProcessor& owner;
        ui::DooblrLookAndFeel lnf;

        juce::TextButton undo { "Undo" }, redo { "Redo" };
        juce::ComboBox presetBox;
        juce::TextButton savePreset { "Save" }, loadPreset { "Load" };
        juce::Slider output { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label outputCaption, versionLabel, titleLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttach;
        juce::TextButton layerA { "L1" }, layerB { "L2" };

        ui::Panel mallet { "Mallet" }, noise { "Noise" }, mixer { "Mixer" }, perform { "Play" };
        ui::Panel resA { "Resonator A" }, resB { "Resonator B" }, coupling { "Coupling" }, fx { "Effects" };

        ui::ResonatorView viewA, viewB;

        // Global exciters / mixer / play / fx — always bound
        ui::Knob stiff { "Stiffness", params::id::malletStiffness };
        ui::Knob malletNoise { "Noise", params::id::malletNoise };
        ui::Knob color { "Color", params::id::malletColor };
        ui::Choice noiseFilter { "Filter", params::id::noiseFilter };
        ui::Knob noiseFreq { "Freq", params::id::noiseFreq };
        ui::Knob noiseQ { "Q / Width", params::id::noiseQ };
        ui::Choice envMode { "Env", params::id::envMode };
        ui::Knob envA { "Attack", params::id::envAttack };
        ui::Knob envH { "Hold", params::id::envHold };
        ui::Knob envD { "Decay", params::id::envDecay };
        ui::Knob envS { "Sustain", params::id::envSustain };
        ui::Knob envR { "Release", params::id::envRelease };
        ui::Knob lfoRate { "LFO Rate", params::id::lfoRate };
        ui::Knob lfoDepth { "LFO Depth", params::id::lfoDepth };
        ui::Knob mixMallet { "Mallet", params::id::mixMallet };
        ui::Knob mixNoise { "Noise", params::id::mixNoise };
        ui::Knob mixDirect { "Direct", params::id::mixDirect };
        ui::Choice unison { "Unison", params::id::unisonCount };
        ui::Knob detune { "Detune", params::id::unisonDetune };
        ui::Knob vibRate { "Vib Rate", params::id::vibratoRate };
        ui::Knob vibDepth { "Vib Depth", params::id::vibratoDepth };
        ui::Knob glide { "Glide", params::id::glide };
        ui::Knob macro1 { "Macro 1", params::id::macro1 };
        ui::Knob macro2 { "Macro 2", params::id::macro2 };
        ui::Knob macro3 { "Macro 3", params::id::macro3 };
        ui::Knob macro4 { "Macro 4", params::id::macro4 };
        ui::Choice layerMode { "Layers", params::id::layerMode };
        ui::Knob splitNote { "Split", params::id::splitNote };
        ui::Knob layer1Level { "L1 Level", params::id::layer1Level };
        ui::Knob layer2Level { "L2 Level", params::id::layer2Level };
        ui::Knob layer2Trans { "L2 Trans", params::id::layer2Transpose };

        ui::Knob eqLow { "Low", params::id::eqLow };
        ui::Knob eqMid { "Mid", params::id::eqMid };
        ui::Knob eqHigh { "High", params::id::eqHigh };
        ui::Knob eqMidFreq { "Mid Freq", params::id::eqMidFreq };
        ui::Knob compTh { "Thresh", params::id::compThreshold };
        ui::Knob compRatio { "Ratio", params::id::compRatio };
        ui::Knob compAtk { "Attack", params::id::compAttack };
        ui::Knob compRel { "Release", params::id::compRelease };
        ui::Knob compMix { "Comp Mix", params::id::compMix };
        ui::Knob distDrive { "Drive", params::id::distDrive };
        ui::Knob distMix { "Dist Mix", params::id::distMix };
        ui::Knob chRate { "Rate", params::id::chorusRate };
        ui::Knob chDepth { "Depth", params::id::chorusDepth };
        ui::Knob chMix { "Chorus", params::id::chorusMix };
        ui::Knob dlTime { "Time", params::id::delayTime };
        ui::Knob dlFb { "Feedback", params::id::delayFeedback };
        ui::Knob dlMix { "Delay", params::id::delayMix };
        ui::Knob rvSize { "Size", params::id::reverbSize };
        ui::Knob rvDamp { "Damp", params::id::reverbDamp };
        ui::Knob rvMix { "Reverb", params::id::reverbMix };

        // Per-selected-layer resonator controls (rebound)
        ui::Toggle aOn { "A", "l1_a_on" }, bOn { "B", "l1_b_on" };
        ui::Choice aType { "Type", "l1_a_type" }, bType { "Type", "l1_b_type" };
        ui::Choice aDens { "Density", "l1_a_density" }, bDens { "Density", "l1_b_density" };
        ui::Knob aPitch { "Pitch", "l1_a_pitch" }, bPitch { "Pitch", "l1_b_pitch" };
        ui::Knob aCents { "Cents", "l1_a_cents" }, bCents { "Cents", "l1_b_cents" };
        ui::Knob aKey { "Key", "l1_a_key" }, bKey { "Key", "l1_b_key" };
        ui::Knob aLevel { "Level", "l1_a_pLevel" }, bLevel { "Level", "l1_b_pLevel" };
        ui::Knob aRate { "Rate", "l1_a_pRate" }, bRate { "Rate", "l1_b_pRate" };
        ui::Knob aVel { "Vel", "l1_a_pVel" }, bVel { "Vel", "l1_b_pVel" };
        ui::Knob aDecay { "Decay", "l1_a_decay" }, bDecay { "Decay", "l1_b_decay" };
        ui::Knob aDecKey { "Dec Key", "l1_a_decayKey" }, bDecKey { "Dec Key", "l1_b_decayKey" };
        ui::Knob aDecVel { "Dec Vel", "l1_a_decayVel" }, bDecVel { "Dec Vel", "l1_b_decayVel" };
        ui::Knob aRel { "Release", "l1_a_release" }, bRel { "Release", "l1_b_release" };
        ui::Knob aMat { "Material", "l1_a_material" }, bMat { "Material", "l1_b_material" };
        ui::Knob aTone { "Tone", "l1_a_tone" }, bTone { "Tone", "l1_b_tone" };
        ui::Knob aLow { "Low Cut", "l1_a_lowCut" }, bLow { "Low Cut", "l1_b_lowCut" };
        ui::Knob aRad { "Radius", "l1_a_radius" }, bRad { "Radius", "l1_b_radius" };
        ui::Knob aHit { "Hit", "l1_a_hit" }, bHit { "Hit", "l1_b_hit" };
        ui::Knob aHitVel { "Hit Vel", "l1_a_hitVel" }, bHitVel { "Hit Vel", "l1_b_hitVel" };
        ui::Knob aHitKey { "Hit Key", "l1_a_hitKey" }, bHitKey { "Hit Key", "l1_b_hitKey" };
        ui::Knob aHitRnd { "Hit Rnd", "l1_a_hitRnd" }, bHitRnd { "Hit Rnd", "l1_b_hitRnd" };
        ui::Knob aP1 { "Part 1", "l1_a_part1" }, aP2 { "Part 2", "l1_a_part2" }, aP3 { "Part 3", "l1_a_part3" }, aP4 { "Part 4", "l1_a_part4" };
        ui::Knob bP1 { "Part 1", "l1_b_part1" }, bP2 { "Part 2", "l1_b_part2" }, bP3 { "Part 3", "l1_b_part3" }, bP4 { "Part 4", "l1_b_part4" };
        ui::Toggle couple { "Coupled", "l1_couple" };
        ui::Knob balance { "Balance", "l1_balance" };

        std::vector<ui::Panel*> panels;
        int editLayer = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrChromaEditor)
    };
}
