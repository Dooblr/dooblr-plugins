#pragma once

#include <functional>
#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/SystemCheck.h"

namespace dooblrsub::ui
{
    class VerdictPanel final : public juce::Component
    {
    public:
        VerdictPanel();
        ~VerdictPanel() override = default;

        void setState (dsp::Verdict verdict, const dsp::ReasonList& reasons,
                       float trackedHz, int latencySamples, double sampleRate);
        void setUndoEnabled (bool canUndo);

        void setFixHandler (std::function<void (dsp::CheckId)> onFix);
        void setFixAllHandler (std::function<void()> onFixAll);
        void setUndoFixHandler (std::function<void()> onUndo);

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::TextButton& getFixAllButton() noexcept { return fixAll; }
        juce::TextButton& getUndoFixButton() noexcept { return undoFix; }

    private:
        struct Row
        {
            juce::Label label;
            juce::TextButton fix { "Fix" };
            dsp::CheckId id = dsp::CheckId::none;
        };

        dsp::Verdict verdict = dsp::Verdict::works;
        dsp::ReasonList reasons;
        float trackedHz = 0.0f;
        int latency = 0;
        double sampleRate = 48000.0;
        bool undoEnabled = false;

        std::array<Row, dsp::maxReasons> rows;
        juce::TextButton fixAll { "Fix All" }, undoFix { "Undo Fix" };
        juce::Component reasonList;
        juce::Viewport viewport;
        std::function<void (dsp::CheckId)> onFix;
        std::function<void()> onFixAll, onUndo;

        void refreshRows();
    };
}
