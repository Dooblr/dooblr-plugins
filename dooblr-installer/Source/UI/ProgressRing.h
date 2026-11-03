#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblr::installer::ui
{
    /** One progress wheel for the whole install: a ring that fills with the overall progress, the
        percentage in the middle, and a short arc that keeps turning while work is going on (so a slow
        download or the password prompt never looks frozen). When finished it shows a tick, "!" when some items
        failed, a cross when nothing could be installed, and "Stopped" after Cancel. */
    class ProgressRing final : public juce::Component, private juce::Timer
    {
    public:
        enum class State { idle, busy, done, problems, failed, cancelled };

        ProgressRing();
        ~ProgressRing() override;

        void setProgress (double p);          // 0..1
        double getProgress() const noexcept { return progress; }
        void setState (State);
        State getState() const noexcept { return state; }

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        double progress = 0.0;
        float spin = 0.0f;
        State state = State::idle;
    };
}
