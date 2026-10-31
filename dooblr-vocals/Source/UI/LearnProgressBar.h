#pragma once

#include <algorithm>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace dooblrvocals::ui
{
    /**
        Learn Range progress (pitch page bar, left of the learn status line): a slim rounded
        bar (6 px, radius 3) on a `divider` track. The fill is the voiced fraction of the take.
        - listening: `frozen` (the armed Learn colour), at half alpha while no voice is heard
        - done: `accent`, full
        - failed / timed out: `dynamic`, at the fraction that was reached
        Repaints only when what is drawn changes. Mouse-transparent.
    */
    class LearnProgressBar final : public juce::Component
    {
    public:
        enum class State { listening, done, failed };

        LearnProgressBar()
        {
            setComponentID ("learnProgress");
            setInterceptsMouseClicks (false, false);
        }

        void set (float newFraction, State newState, bool newHearing)
        {
            // 1/256 steps: an unchanged picture never repaints.
            const auto f = std::round (std::clamp (newFraction, 0.0f, 1.0f) * 256.0f) / 256.0f;
            if (juce::exactlyEqual (f, fraction) && newState == state && newHearing == hearing)
                return;
            fraction = f;
            state = newState;
            hearing = newHearing;
            repaint();
        }

        float getFraction() const noexcept { return fraction; }
        State getState() const noexcept { return state; }

        void paint (juce::Graphics& g) override
        {
            const auto bar = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 6.0f);
            g.setColour (Palette::divider);
            g.fillRoundedRectangle (bar, 3.0f);

            const auto colour = state == State::done   ? Palette::accent
                              : state == State::failed ? Palette::dynamic
                                                       : Palette::frozen.withAlpha (hearing ? 1.0f : 0.5f);
            const auto width = bar.getWidth() * (state == State::done ? 1.0f : fraction);

            if (width > 0.5f)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, width)), 3.0f);
            }
        }

    private:
        float fraction = 0.0f;
        State state = State::listening;
        bool hearing = false;
    };
}
