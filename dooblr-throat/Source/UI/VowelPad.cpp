#include "VowelPad.h"
#include "../DSP/Vowels.h"

namespace dooblrthroat::ui
{
    VowelPad::VowelPad (juce::AudioProcessorValueTreeState& s,
                        const juce::String& height, const juce::String& back, const juce::String& vowel)
        : state (s), heightId (height), backId (back), vowelId (vowel)
    {
        setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        startTimerHz (24);
    }

    VowelPad::~VowelPad() { stopTimer(); }

    juce::Point<float> VowelPad::markerPos() const
    {
        auto valueOf = [this] (const juce::String& id) -> float
        {
            if (auto* p = state.getParameter (id))
                return p->convertFrom0to1 (p->getValue());
            return 0.5f;
        };

        auto h = juce::jlimit (0.0f, 1.0f, valueOf (heightId));
        auto b = juce::jlimit (0.0f, 1.0f, valueOf (backId));
        const auto vowel = juce::roundToInt (valueOf (vowelId));
        if (vowel > 0 && vowel <= (int) dsp::N_VOWELS)
        {
            h = (float) dsp::VOWELS[(size_t) vowel - 1].height;
            b = (float) dsp::VOWELS[(size_t) vowel - 1].back;
        }
        auto bounds = getLocalBounds().toFloat().reduced (10.0f);
        return { bounds.getX() + b * bounds.getWidth(),
                 bounds.getY() + h * bounds.getHeight() };
    }

    void VowelPad::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        auto pad = bounds.reduced (10.0f);
        g.setColour (Palette::grid);
        for (int i = 1; i < 4; ++i)
        {
            const float x = pad.getX() + pad.getWidth() * (float) i / 4.0f;
            const float y = pad.getY() + pad.getHeight() * (float) i / 4.0f;
            g.drawLine (x, pad.getY(), x, pad.getBottom(), 1.0f);
            g.drawLine (pad.getX(), y, pad.getRight(), y, 1.0f);
        }

        g.setColour (Palette::textMuted);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("front", pad.removeFromBottom (0).withY (bounds.getBottom() - 12).withHeight (12).withWidth (pad.getWidth() * 0.4f),
                    juce::Justification::centredLeft, false);
        // axis labels
        g.drawText ("front", juce::Rectangle<float> (pad.getX(), bounds.getBottom() - 14, 40, 12), juce::Justification::centredLeft);
        g.drawText ("back", juce::Rectangle<float> (bounds.getRight() - 44, bounds.getBottom() - 14, 36, 12), juce::Justification::centredRight);
        g.drawText ("close", juce::Rectangle<float> (4, pad.getY(), 40, 12), juce::Justification::centredLeft);
        g.drawText ("open", juce::Rectangle<float> (4, pad.getBottom() - 12, 40, 12), juce::Justification::centredLeft);

        // calibrated vowel dots
        auto inner = getLocalBounds().toFloat().reduced (10.0f);
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (size_t i = 0; i < dsp::N_VOWELS; ++i)
        {
            const auto& v = dsp::VOWELS[i];
            const auto x = inner.getX() + (float) v.back * inner.getWidth();
            const auto y = inner.getY() + (float) v.height * inner.getHeight();
            g.setColour (Palette::spectrum.withAlpha (0.7f));
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }

        const auto m = markerPos();
        g.setColour (Palette::accent);
        g.fillEllipse (m.x - 6.0f, m.y - 6.0f, 12.0f, 12.0f);
        g.setColour (Palette::textPrimary);
        g.drawEllipse (m.x - 6.0f, m.y - 6.0f, 12.0f, 12.0f, 1.2f);
    }

    void VowelPad::applyAt (juce::Point<float> pos, bool gestureStart, bool gestureEnd)
    {
        auto pad = getLocalBounds().toFloat().reduced (10.0f);
        const auto back = juce::jlimit (0.0f, 1.0f, (pos.x - pad.getX()) / juce::jmax (1.0f, pad.getWidth()));
        const auto height = juce::jlimit (0.0f, 1.0f, (pos.y - pad.getY()) / juce::jmax (1.0f, pad.getHeight()));

        auto* h = state.getParameter (heightId);
        auto* b = state.getParameter (backId);
        auto* v = state.getParameter (vowelId);
        if (h == nullptr || b == nullptr)
            return;

        if (gestureStart)
        {
            h->beginChangeGesture();
            b->beginChangeGesture();
            if (v != nullptr) v->beginChangeGesture();
        }

        h->setValueNotifyingHost (h->convertTo0to1 (height));
        b->setValueNotifyingHost (b->convertTo0to1 (back));
        if (v != nullptr)
            v->setValueNotifyingHost (v->convertTo0to1 (0.0f)); // Pad

        if (gestureEnd)
        {
            h->endChangeGesture();
            b->endChangeGesture();
            if (v != nullptr) v->endChangeGesture();
        }
        repaint();
    }

    void VowelPad::mouseDown (const juce::MouseEvent& e)
    {
        dragging = true;
        applyAt (e.position, true, false);
    }

    void VowelPad::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging)
            applyAt (e.position, false, false);
    }

    void VowelPad::mouseUp (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;
        dragging = false;
        applyAt (e.position, false, true);
    }

    void VowelPad::timerCallback()
    {
        if (! dragging)
            repaint();
    }
}
