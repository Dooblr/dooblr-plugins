#include "PitchDisplay.h"

#include "../DSP/PitchEngine.h"
#include "../Parameters.h"
#include "Theme.h"

#include <cmath>

namespace dooblrvocals::ui
{
    namespace
    {
        float pixelScaleFor (const juce::Component& c)
        {
            auto scale = juce::Component::getApproximateScaleFactorForComponent (&c);

            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (c.getScreenBounds()))
                scale *= (float) display->scale;

            return juce::jlimit (1.0f, 4.0f, scale);
        }

        bool isBlack (int pc) noexcept
        {
            constexpr int black = (1 << 1) | (1 << 3) | (1 << 6) | (1 << 8) | (1 << 10);
            return ((black >> (pc % 12)) & 1) != 0;
        }
    }

    PitchDisplay::PitchDisplay (juce::AudioProcessorValueTreeState& s) : gesture (s)
    {
        setOpaque (true);
        setComponentID ("pitchDisplay");
    }

    void PitchDisplay::reset()
    {
        detectedMidi.fill (0);
        correctedMidi.fill (0);
        voiced.fill (0);
        write = filled = 0;
        shownDetected = shownCorrected = 0;
        readout.clear();
        rebuildBackground();
        repaint();
    }

    float PitchDisplay::midiToY (float midi) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (midi - (float) minMidi) / (float) (maxMidi - minMidi));
        return plot.getBottom() - t * plot.getHeight();
    }

    int PitchDisplay::currentMask() const
    {
        const auto mode = juce::roundToInt (gesture.get ("corr_mode"));
        const auto scale = juce::roundToInt (gesture.get ("corr_scale"));
        const auto key = juce::roundToInt (gesture.get ("corr_key"));
        const auto custom = juce::roundToInt (gesture.get ("corr_mask"));
        return dsp::effectiveMask ((dsp::CorrectionMode) mode, (dsp::ScaleType) scale, key, custom);
    }

    bool PitchDisplay::isCustom() const
    {
        return juce::roundToInt (gesture.get ("corr_mode")) == 0
            && juce::roundToInt (gesture.get ("corr_scale")) == (int) dsp::ScaleType::custom;
    }

    bool PitchDisplay::isChromatic() const
    {
        return juce::roundToInt (gesture.get ("corr_mode")) != 0;
    }

    int PitchDisplay::hitPitchClass (juce::Point<float> p) const
    {
        if (! keyboard.contains (p) && ! plot.contains (p))
            return -1;

        const auto midi = (float) minMidi + (keyboard.getBottom() - p.y) / keyboard.getHeight() * (float) (maxMidi - minMidi);
        const auto note = juce::roundToInt (midi);
        if (note < minMidi || note > maxMidi)
            return -1;
        return ((note % 12) + 12) % 12;
    }

    void PitchDisplay::rebuildBackground()
    {
        const auto bounds = getLocalBounds();
        if (bounds.isEmpty())
            return;

        pixelScale = pixelScaleFor (*this);
        background = { juce::Image::ARGB,
                       juce::jmax (1, juce::roundToInt ((float) bounds.getWidth() * pixelScale)),
                       juce::jmax (1, juce::roundToInt ((float) bounds.getHeight() * pixelScale)),
                       true, juce::NativeImageType() };

        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (pixelScale));
        g.setColour (Palette::background);
        g.fillAll();

        g.setColour (Palette::background);
        g.fillRoundedRectangle (plot, 3.0f);

        const auto mask = currentMask();
        lastMask = mask;
        lastKey = juce::roundToInt (gesture.get ("corr_key"));
        lastScale = juce::roundToInt (gesture.get ("corr_scale"));
        lastMode = juce::roundToInt (gesture.get ("corr_mode"));

        for (int midi = minMidi; midi <= maxMidi; ++midi)
        {
            const auto y = midiToY ((float) midi);
            const auto pc = ((midi % 12) + 12) % 12;
            const auto inScale = ((mask >> pc) & 1) != 0;
            const auto c = midi % 12 == 0;

            g.setColour (c ? Palette::divider : Palette::grid);
            g.fillRect (plot.getX(), y - (c ? 0.75f : 0.4f), plot.getWidth(), c ? 1.5f : 0.8f);

            if (inScale && ! isChromatic())
            {
                g.setColour (Palette::accent.withAlpha (0.06f));
                const auto next = midiToY ((float) midi + 1.0f);
                g.fillRect (plot.getX(), next, plot.getWidth(), y - next);
            }
        }

        // Keyboard.
        for (int midi = minMidi; midi <= maxMidi; ++midi)
        {
            const auto y0 = midiToY ((float) midi + 1.0f);
            const auto y1 = midiToY ((float) midi);
            const auto pc = ((midi % 12) + 12) % 12;
            const auto inScale = ((mask >> pc) & 1) != 0;
            auto key = juce::Rectangle<float> (keyboard.getX(), y0, keyboard.getWidth(), y1 - y0);

            if (isBlack (pc))
            {
                g.setColour (inScale ? Palette::panelRaised.brighter (0.15f) : Palette::background);
                g.fillRect (key);
            }
            else
            {
                g.setColour (inScale ? Palette::panelRaised : Palette::panel);
                g.fillRect (key);
            }

            g.setColour (Palette::grid);
            g.fillRect (key.getX(), key.getBottom() - 0.5f, key.getWidth(), 1.0f);

            if (pc == 0)
            {
                g.setColour (Palette::textMuted);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (params::noteName (midi), key.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    void PitchDisplay::resized()
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.0f, 0.0f);
        keyboard = bounds.removeFromLeft (44.0f);
        bounds.removeFromLeft (4.0f);
        plot = bounds;
        rebuildBackground();
    }

    void PitchDisplay::update (const dsp::PitchFrame* frames, int numFrames, double)
    {
        const auto cols = juce::jmax (1, juce::roundToInt (plot.getWidth()));
        const auto used = juce::jmin (cols, maxColumns);

        for (int i = 0; i < numFrames; ++i)
        {
            const auto& f = frames[i];
            const auto a4 = gesture.get ("a4_hz");
            const auto det = f.voiced && f.detectedHz > 20.0f ? dsp::PitchCorrector::hzToMidi (f.detectedHz, a4) : 0.0f;
            const auto cor = f.voiced && f.correctedHz > 20.0f ? dsp::PitchCorrector::hzToMidi (f.correctedHz, a4) : 0.0f;
            detectedMidi[(size_t) write] = det;
            correctedMidi[(size_t) write] = cor;
            voiced[(size_t) write] = (char) (f.voiced ? 1 : 0);
            write = (write + 1) % used;
            filled = juce::jmin (used, filled + 1);
            if (f.voiced)
            {
                shownDetected = f.detectedHz;
                shownCorrected = f.correctedHz;
            }
            ++framesReceived;
        }

        const auto mask = currentMask();
        if (mask != lastMask || juce::roundToInt (gesture.get ("corr_key")) != lastKey
            || juce::roundToInt (gesture.get ("corr_scale")) != lastScale
            || juce::roundToInt (gesture.get ("corr_mode")) != lastMode
            || ! background.isValid())
            rebuildBackground();

        if (shownDetected > 20.0f)
            readout = params::formatHz (shownDetected)
                    + (shownCorrected > 20.0f ? "  →  " + params::formatHz (shownCorrected) : juce::String());
        else
            readout.clear();

        if (numFrames > 0 || hoverClass >= 0)
            repaint();
    }

    void PitchDisplay::paint (juce::Graphics& g)
    {
        g.setColour (Palette::background);
        g.fillAll();

        if (background.isValid())
            g.drawImage (background, getLocalBounds().toFloat());

        const auto cols = juce::jmax (1, juce::roundToInt (plot.getWidth()));
        const auto used = juce::jmin (cols, maxColumns, filled);
        if (used <= 1)
            return;

        juce::Path detected, corrected;
        auto startedD = false, startedC = false;

        for (int x = 0; x < used; ++x)
        {
            const auto idx = (write - used + x + used * 4) % used;
            const auto px = plot.getX() + (float) x;
            if (voiced[(size_t) idx])
            {
                const auto yD = midiToY (detectedMidi[(size_t) idx]);
                const auto yC = midiToY (correctedMidi[(size_t) idx]);
                if (! startedD) { detected.startNewSubPath (px, yD); startedD = true; }
                else detected.lineTo (px, yD);
                if (! startedC) { corrected.startNewSubPath (px, yC); startedC = true; }
                else corrected.lineTo (px, yC);
            }
            else
            {
                startedD = startedC = false;
            }
        }

        if (startedD || ! detected.isEmpty())
        {
            g.setColour (Palette::spectrum.withAlpha (0.9f));
            g.strokePath (detected, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (startedC || ! corrected.isEmpty())
        {
            g.setColour (Palette::accent);
            g.strokePath (corrected, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (hoverClass >= 0 && isCustom())
        {
            g.setColour (Palette::textPrimary.withAlpha (0.2f));
            for (int midi = minMidi + hoverClass; midi <= maxMidi; midi += 12)
            {
                const auto y0 = midiToY ((float) midi + 1.0f);
                const auto y1 = midiToY ((float) midi);
                g.fillRect (keyboard.getX(), y0, keyboard.getWidth() + plot.getWidth() + 4.0f, y1 - y0);
            }
        }

        if (readout.isNotEmpty())
        {
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (Palette::textPrimary);
            g.drawText (readout, plot.reduced (8.0f, 6.0f).removeFromTop (16.0f), juce::Justification::topRight, false);
        }
    }

    void PitchDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto pc = isCustom() ? hitPitchClass (e.position) : -1;
        if (pc != hoverClass)
        {
            hoverClass = pc;
            setMouseCursor (pc >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void PitchDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (hoverClass >= 0)
        {
            hoverClass = -1;
            repaint();
        }
    }

    void PitchDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (! isCustom())
            return;

        const auto pc = hitPitchClass (e.position);
        if (pc < 0)
            return;

        auto mask = juce::roundToInt (gesture.get ("corr_mask")) & 0x0fff;
        mask ^= (1 << pc);
        if (mask == 0)
            mask = 1 << pc;

        gesture.begin ({ "corr_mask" });
        gesture.set ("corr_mask", (float) mask);
        gesture.end();
        rebuildBackground();
        repaint();
    }
}
