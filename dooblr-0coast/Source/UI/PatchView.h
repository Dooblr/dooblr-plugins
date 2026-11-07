#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/Patch.h"
#include "Theme.h"

namespace dooblr0coast::ui
{
    class Jack final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Jack (bool output, int index, juce::Colour colour, juce::String label);
        bool isOutput() const noexcept { return output; }
        int getIndex() const noexcept { return index; }
        juce::Point<float> centreInParent() const;
        juce::Colour getColour() const noexcept { return colour; }

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        std::function<void (Jack&)> onDragFrom;
        std::function<void (Jack&, juce::Point<int>)> onDragMove;
        std::function<void (Jack&, juce::Point<int>)> onDragEnd;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool output = false;
        int index = 0;
        juce::Colour colour;
        juce::String label;
        bool hot = false;
    };

    class CableLayer final : public juce::Component
    {
    public:
        struct Drawn
        {
            dsp::OutJack from {};
            dsp::InJack to {};
            juce::Point<float> a, b;
            juce::Colour colour;
        };

        CableLayer();
        void setCables (std::vector<Drawn> next);
        void setDrag (juce::Point<float> from, juce::Point<float> to, juce::Colour colour, bool active);
        bool hitTest (int x, int y) override;
        void paint (juce::Graphics&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        std::function<void (dsp::OutJack, dsp::InJack)> onRemove;

    private:
        std::vector<Drawn> cables;
        juce::Point<float> dragFrom, dragTo;
        juce::Colour dragColour { Palette::accent };
        bool dragging = false;
    };

    class ScopeView final : public juce::Component
    {
    public:
        void setSamples (const float* data, int count);
        void paint (juce::Graphics&) override;

    private:
        std::vector<float> samples;
        juce::Image background;
        int lastW = 0, lastH = 0;
    };
}
