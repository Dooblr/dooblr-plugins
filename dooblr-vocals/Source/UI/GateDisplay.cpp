#include "GateDisplay.h"

#include "../DSP/Effects.h"
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

        juce::String formatDb (float db)
        {
            if (db <= -119.0f)
                return "-inf dB";
            const auto rounded = std::abs (db) < 0.05f ? 0.0f : db;
            return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
        }
    }

    GateDisplay::GateDisplay (juce::AudioProcessorValueTreeState& s) : gesture (s)
    {
        setOpaque (true);
        setComponentID ("gateDisplay");
        history.setRange (minDb, maxDb, { -72.0f, -48.0f, -24.0f, -12.0f, 0.0f });
        history.setColours ({ Palette::spectrum.withAlpha (0.35f), Palette::accent, Palette::spectrum.withAlpha (0.5f), Palette::dynamic });
    }

    void GateDisplay::reset()
    {
        history.clear();
        shownInDb = -120.0f;
        shownGrDb = 0.0f;
        framesReceived = 0;
        rebuildBackground();
        rebuildCurve();
        repaint();
    }

    float GateDisplay::getThresholdLineY() const
    {
        return history.dbToY (gesture.get ("gate_threshold"));
    }

    float GateDisplay::dbToTransferX (float db) const noexcept
    {
        return juce::jmap (db, minDb, maxDb, transfer.getX(), transfer.getRight());
    }

    float GateDisplay::dbToTransferY (float db) const noexcept
    {
        return juce::jmap (db, minDb, maxDb, transfer.getBottom(), transfer.getY());
    }

    juce::uint64 GateDisplay::paramHash() const
    {
        return (juce::uint64) juce::roundToInt (gesture.get ("gate_threshold") * 100.0f)
             ^ ((juce::uint64) juce::roundToInt (gesture.get ("gate_range") * 10.0f) << 16)
             ^ ((juce::uint64) (gesture.get ("gate_on") > 0.5f ? 1 : 0) << 32);
    }

    void GateDisplay::rebuildBackground()
    {
        auto area = transfer.toNearestInt();
        if (area.isEmpty())
            return;

        const auto scale = pixelScaleFor (*this);
        background = { juce::Image::ARGB,
                       juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)),
                       juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale)),
                       true, juce::NativeImageType() };
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setColour (Palette::background);
        g.fillAll();
        g.setColour (Palette::grid);
        for (const auto db : { -72.0f, -48.0f, -24.0f, -12.0f, 0.0f })
        {
            const auto x = dbToTransferX (db) - transfer.getX();
            const auto y = dbToTransferY (db) - transfer.getY();
            g.fillRect (x, 0.0f, 1.0f, transfer.getHeight());
            g.fillRect (0.0f, y, transfer.getWidth(), 1.0f);
        }
        lastHash = 0;
    }

    void GateDisplay::rebuildCurve()
    {
        curve.clear();
        const auto threshold = gesture.get ("gate_threshold");
        const auto range = gesture.get ("gate_range");
        const auto on = gesture.get ("gate_on") > 0.5f;
        juce::ignoreUnused (on);

        bool started = false;
        for (int i = 0; i <= 64; ++i)
        {
            const auto inDb = juce::jmap ((float) i, 0.0f, 64.0f, minDb, maxDb);
            const auto g = dsp::Gate::staticGain (inDb, threshold, range);
            const auto outDb = inDb + 20.0f * std::log10 (std::max (g, 1.0e-6f));
            const auto p = juce::Point<float> (dbToTransferX (inDb), dbToTransferY (outDb));
            if (! started) { curve.startNewSubPath (p); started = true; }
            else curve.lineTo (p);
        }
        lastHash = paramHash();
    }

    void GateDisplay::resized()
    {
        auto bounds = getLocalBounds();
        transfer = bounds.removeFromRight (juce::jlimit (140, 220, bounds.getWidth() / 3)).toFloat().reduced (8.0f, 6.0f);
        history.setBounds (bounds.reduced (8, 6), pixelScaleFor (*this));
        rebuildBackground();
        rebuildCurve();
    }

    void GateDisplay::update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds)
    {
        juce::ignoreUnused (elapsedSeconds);
        const auto attack = 1.0f, release = 40.0f * (float) std::max (0.0, elapsedSeconds);

        for (int i = 0; i < numFrames; ++i)
        {
            const auto& f = frames[i];
            shownInDb = std::max (f.gateInDb, shownInDb - release);
            shownInDb = std::max (shownInDb, f.gateInDb * attack);
            shownGrDb = std::min (f.gateGrDb, shownGrDb + release);
            shownGrDb = std::min (shownGrDb, f.gateGrDb);
            history.push ({ f.gateInDb, f.gateOutDb, -120.0f, f.gateGrDb });
            ++framesReceived;
        }

        if (paramHash() != lastHash)
            rebuildCurve();

        history.flush();
        if (numFrames > 0 || paramHash() != lastHash)
            repaint();
    }

    void GateDisplay::paint (juce::Graphics& g)
    {
        g.setColour (Palette::background);
        g.fillAll();
        history.draw (g);

        if (background.isValid())
            g.drawImage (background, transfer);

        const auto on = gesture.get ("gate_on") > 0.5f;
        g.setColour (Palette::dynamic.withAlpha (on ? 1.0f : 0.4f));
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto threshold = gesture.get ("gate_threshold");
        const auto y = history.dbToY (threshold);
        g.setColour (Palette::textPrimary.withAlpha (hover == DragKind::thresholdLine || dragging == DragKind::thresholdLine ? 0.95f : 0.55f));
        g.fillRect ((float) history.getBounds().getX(), y - 0.75f, (float) history.getBounds().getWidth(), 1.5f);

        const auto handle = juce::Point<float> (dbToTransferX (threshold), dbToTransferY (threshold));
        g.setColour (Palette::dynamic);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (handle));

        if (shownInDb > -119.0f)
        {
            const auto gx = dbToTransferX (shownInDb);
            const auto gy = dbToTransferY (shownInDb + shownGrDb);
            g.setColour (Palette::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ gx, gy }));
        }

        readout = on ? "GR " + formatDb (shownGrDb > -0.05f ? 0.0f : shownGrDb) : juce::String ("Gate off");
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (Palette::textPrimary);
        g.drawText (readout, transfer.reduced (6.0f).removeFromTop (16.0f), juce::Justification::topRight, false);
    }

    GateDisplay::DragKind GateDisplay::hitTestDrag (juce::Point<float> p) const
    {
        const auto threshold = gesture.get ("gate_threshold");
        const auto handle = juce::Point<float> (dbToTransferX (threshold), dbToTransferY (threshold));
        if (handle.getDistanceFrom (p) < 10.0f)
            return DragKind::thresholdHandle;
        if (history.getBounds().toFloat().contains (p) && std::abs (p.y - history.dbToY (threshold)) < 7.0f)
            return DragKind::thresholdLine;
        return DragKind::none;
    }

    void GateDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto kind = dragging == DragKind::none ? hitTestDrag (e.position) : dragging;
        if (kind != hover)
        {
            hover = kind;
            setMouseCursor (kind != DragKind::none ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void GateDisplay::mouseDown (const juce::MouseEvent& e)
    {
        dragging = hitTestDrag (e.position);
        if (dragging == DragKind::none)
            return;
        grab = dragging == DragKind::thresholdLine ? history.dbToY (gesture.get ("gate_threshold")) - e.position.y : 0.0f;
        gesture.begin ({ "gate_threshold" });
        repaint();
    }

    void GateDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging == DragKind::none)
            return;

        if (dragging == DragKind::thresholdLine)
            gesture.set ("gate_threshold", juce::jlimit (-80.0f, 0.0f, history.yToDb (e.position.y + grab)));
        else
            gesture.set ("gate_threshold", juce::jlimit (-80.0f, 0.0f, juce::jmap (e.position.x, transfer.getX(), transfer.getRight(), minDb, maxDb)));

        rebuildCurve();
        repaint();
    }

    void GateDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (dragging != DragKind::none)
        {
            gesture.end();
            dragging = DragKind::none;
        }
    }

    void GateDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (dragging == DragKind::none && hover != DragKind::none)
        {
            hover = DragKind::none;
            repaint();
        }
    }
}
