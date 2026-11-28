#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Views.h"

namespace dooblrmeter
{
    /** Main page: loudness readouts, history, spectrum, stereo, translation and findings.
        Advanced page: the remaining settings, delivery targets, every metric and the
        short-term distribution. Resizable 940x600 .. 2400x1400 (default 1080x660). */
    class DooblrMeterEditor final : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit DooblrMeterEditor (DooblrMeterProcessor&);
        ~DooblrMeterEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** 0 Main, 1 Advanced. */
        void showPage (int page);
        int getPage() const noexcept { return page; }
        /** Pulls the latest snapshot now (tests; the timer does this 30 times a second). */
        void refreshNow() { timerCallback(); }

    private:
        void timerCallback() override;
        void setupCombo (juce::ComboBox&, const char* pid, const juce::StringArray& items, const juce::String& tip);
        void setupToggle (juce::ToggleButton&, const char* pid, const juce::String& tip);
        void setupSlider (juce::Slider&, const char* pid, const juce::String& tip);
        void toggleListen (int profile);
        ui::ViewContext makeContext() const;

        DooblrMeterProcessor& owner;
        juce::AudioProcessorValueTreeState& state;
        ui::DooblrLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 600 };

        /** Holds every control and view. Above the base size it is laid out at the base size and
            scaled up uniformly (up to 2x), so text and controls grow with the window. */
        struct Surface final : juce::Component
        {
            std::function<void (juce::Graphics&)> painter;
            void paint (juce::Graphics& g) override { if (painter) painter (g); }
        };
        Surface surface;
        float surfaceScale = 1.0f;
        void paintSurface (juce::Graphics&);

        int page = 0;
        bool statusInline = true; // false when the header is too narrow: the status replaces the subtitle
        juce::Rectangle<int> settingsPanel;
        int selectedFinding = -1;
        ui::SnapshotPtr lastSnapshot;

        juce::Label titleLabel, subtitleLabel, statusLabel;
        juce::TextButton mainTab { "Main" }, advancedTab { "Advanced" }, resetButton { "Reset" };
        juce::Label targetCaption, curveCaption, listenCaption, modeCaption;
        juce::ComboBox targetBox, curveBox, listenBox;
        juce::TextButton liveButton { "Live" }, songButton { "Song" };

        // Advanced settings.
        juce::Label settingsTitle;
        juce::Label customLufsCaption, customTpCaption, historyCaption, slopeCaption, averagingCaption, sensitivityCaption;
        juce::Slider customLufsSlider, customTpSlider;
        juce::ComboBox historyBox, slopeBox, averagingBox, sensitivityBox;
        juce::ToggleButton matchToggle { "Level match" }, monoToggle { "Mono" }, freezeToggle { "Freeze" };

        ui::ReadoutView readout;
        ui::HistoryView history;
        ui::SpectrumView spectrum;
        ui::StereoView stereo;
        ui::TranslationView translation;
        ui::FindingsView findings;
        ui::DeliveryView delivery;
        ui::MetricsView metrics;
        ui::HistogramView histogram;

        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrMeterEditor)
    };
}
