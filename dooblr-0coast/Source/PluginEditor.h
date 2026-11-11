#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/OutputMeter.h"
#include "UI/PatchView.h"
#include "UI/Theme.h"

namespace dooblr0coast
{
    /**
        One-page hardware-style board in the dooblr look: header, then CTRL / OSC / TIMBRE /
        SLOPE / CONTOUR / DYNAMICS columns with patch jacks. Resize 940x600 .. 2400x1400.
    */
    class Dooblr0CoastEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
    {
    public:
        explicit Dooblr0CoastEditor (Dooblr0CoastProcessor&);
        ~Dooblr0CoastEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        juce::Component* findControl (const juce::String& parameterId) const;
        juce::Rectangle<int> getContentArea() const noexcept { return contentArea; }
        ui::CableLayer& getCableLayer() noexcept { return cableLayer; }
        ui::ScopeView& getScope() noexcept { return scope; }
        const std::vector<std::unique_ptr<ui::Jack>>& getJacks() const { return jacks; }

        static constexpr int defaultWidth = 1080, defaultHeight = 660;
        static constexpr int minWidth = 940, minHeight = 600, maxWidth = 2400, maxHeight = 1400;

    private:
        struct Knob
        {
            juce::Label caption;
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        };

        struct Combo
        {
            juce::Label caption;
            juce::ComboBox box;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        };

        struct Column
        {
            juce::String title;
            juce::Colour colour;
            juce::Rectangle<int> bounds;
        };

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshPresetBox();
        void presetChosen();
        void savePreset();
        void loadPreset();
        void setStatus (const juce::String& text);
        void updateUndoButtons();
        Knob& addKnob (const juce::String& parameterId, const juce::String& caption, juce::Colour colour);
        void addToggle (const juce::String& parameterId, const juce::String& label);
        Combo& addCombo (const juce::String& parameterId, const juce::String& caption, const juce::StringArray& items);
        ui::Jack& addJack (bool output, int index, juce::Colour colour, const juce::String& label);
        void layoutKnob (Knob& knob, juce::Rectangle<int> cell);
        void refreshCables();
        void beginCable (ui::Jack& from);
        void dragCable (ui::Jack& from, juce::Point<int> screen);
        void endCable (ui::Jack& from, juce::Point<int> screen);

        Dooblr0CoastProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;

        juce::Label titleLabel, subtitleLabel, outputCaption, statusLabel;
        juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
        juce::ComboBox presetBox;
        juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        ui::OutputMeter outputMeter;

        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
        std::vector<std::unique_ptr<Combo>> combos;
        std::vector<std::unique_ptr<ui::Jack>> jacks;
        std::vector<juce::Label> columnLabels;
        ui::CableLayer cableLayer;
        ui::ScopeView scope;
        std::array<juce::Rectangle<int>, 6> columns {};
        dsp::Activity activity {};

        juce::Rectangle<int> headerArea, contentArea;

        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::Array<juce::File> userPresetFiles;
        bool updatingPresetBox = false;
        juce::String shownPresetName;
        juce::String transientStatus;
        juce::uint32 transientStatusUntil = 0;
        ui::Jack* draggingFrom = nullptr;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dooblr0CoastEditor)
    };
}
