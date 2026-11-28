#include "Views.h"

#include <cmath>

#include "../DSP/Profiles.h"
#include "../DSP/Targets.h"

namespace dooblrmeter::ui
{
    namespace
    {
        const juce::String minusSign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
        const juce::String noValue (juce::CharPointer_UTF8 ("\xe2\x80\x93"));

        juce::Font font (float size, bool bold = false)
        {
            return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        }

        constexpr int titleHeight = 22;

        void drawRow (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const juce::String& value,
                      juce::Colour valueColour = Palette::textPrimary, float size = 12.0f)
        {
            g.setFont (font (size));
            g.setColour (Palette::textMuted);
            g.drawFittedText (label, r, juce::Justification::centredLeft, 1, 0.8f);
            g.setFont (font (size, true));
            g.setColour (valueColour);
            g.drawFittedText (value, r, juce::Justification::centredRight, 1, 0.8f);
        }

        juce::String timeLabel (double s)
        {
            const auto t = (int) std::round (s);
            return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
        }
    }

    juce::String fmt (double v, int decimals, double floor)
    {
        if (! std::isfinite (v) || v <= floor)
            return noValue;
        auto s = decimals > 0 ? juce::String (std::abs (v), decimals) : juce::String (juce::roundToInt (std::abs (v)));
        if (v < 0.0 && ! juce::exactlyEqual (s.getDoubleValue(), 0.0))
            s = minusSign + s;
        return s;
    }

    juce::String signedFmt (double v, int decimals)
    {
        auto s = fmt (v, decimals, -1.0e9);
        return v > 0.0 && juce::String (v, decimals).getDoubleValue() != 0.0 ? "+" + s : s;
    }

    juce::Colour scoreColour (int score)
    {
        return score >= 75 ? Palette::good : (score >= 50 ? Palette::dynamic : Palette::danger);
    }

    // ---------------------------------------------------------------------------------------------
    juce::Rectangle<int> SnapshotView::contentArea() const
    {
        return getLocalBounds().reduced (10, 0).withTrimmedTop (titleHeight).withTrimmedBottom (8);
    }

    void SnapshotView::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        auto header = getLocalBounds().reduced (10, 0).removeFromTop (titleHeight);
        g.setColour (Palette::textMuted);
        g.setFont (font (11.0f, true));
        g.drawText (title, header, juce::Justification::centredLeft);
        if (const auto note = headerNote(); note.isNotEmpty())
        {
            g.setFont (font (11.0f));
            g.drawFittedText (note, header.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (font (11.0f, true), title) + 12),
                              juce::Justification::centredRight, 1, 0.75f);
        }
        paintContent (g, contentArea());
    }

    // ---------------------------------------------------------------------------------------------
    void ReadoutView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        const auto& L = s.loudness;

        auto big = area.removeFromTop (std::min (46, area.getHeight() / 5));
        g.setColour (Palette::textPrimary);
        g.setFont (font ((float) big.getHeight() * 0.8f, true));
        const auto integrated = L.hasIntegrated ? fmt (L.integrated, 1) : noValue;
        g.drawFittedText (integrated, big.withTrimmedRight (76), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (Palette::textMuted);
        g.setFont (font (11.0f, true));
        g.drawFittedText ("LUFS\nINTEGRATED", big.removeFromRight (76), juce::Justification::centredRight, 2, 0.7f);

        const auto gain = L.hasIntegrated ? dsp::targets::predictedGainDb (ctx.target, (float) L.integrated, s.truePeakMax) : 0.0f;
        auto line = area.removeFromTop (18);
        g.setFont (font (11.5f));
        g.setColour (Palette::textMuted);
        const auto targetName = dsp::targets::names()[ctx.targetIndex];
        g.drawFittedText (targetName + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + (L.hasIntegrated ? signedFmt (gain, 1) + " dB" : noValue),
                          line, juce::Justification::centredLeft, 1, 0.7f);
        area.removeFromTop (4);

        const auto rows = 8;
        const auto rowH = std::max (14, std::min (20, area.getHeight() / rows));
        const auto overCeiling = s.truePeakMax > ctx.target.ceiling;
        drawRow (g, area.removeFromTop (rowH), "Short-term", fmt (s.shortTerm, 1) + " LUFS");
        drawRow (g, area.removeFromTop (rowH), "Momentary", fmt (s.momentary, 1) + " LUFS");
        drawRow (g, area.removeFromTop (rowH), "Loudness range", L.hasLra ? fmt (L.lra, 1) + " LU" : noValue);
        drawRow (g, area.removeFromTop (rowH), "True peak", fmt (s.truePeakMax, 1) + " dBTP", overCeiling ? Palette::danger : Palette::textPrimary);
        drawRow (g, area.removeFromTop (rowH), "Sample peak", fmt (s.samplePeakMax, 1) + " dBFS");
        drawRow (g, area.removeFromTop (rowH), "PLR", L.hasIntegrated ? fmt (s.plr, 1) + " dB" : noValue,
                 L.hasIntegrated && s.plr < 7.0f ? Palette::dynamic : Palette::textPrimary);
        drawRow (g, area.removeFromTop (rowH), "PSR", s.shortTerm > -100.0f ? fmt (s.psr, 1) + " dB" : noValue);
        drawRow (g, area.removeFromTop (rowH), "RMS (3 s)", fmt (s.rmsShort, 1) + " dBFS");
    }

    // ---------------------------------------------------------------------------------------------
    juce::String HistoryView::headerNote() const
    {
        if (snap == nullptr)
            return {};
        return juce::String (snap->songMode ? "Song" : "Live") + "  " + timeLabel (snap->analysedSeconds) + " measured";
    }

    void HistoryView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        auto plot = area.withTrimmedLeft (30).withTrimmedBottom (16).toFloat();
        constexpr float top = 0.0f, bottom = -42.0f;
        const auto yOf = [&] (double lufs) { return plot.getY() + (float) ((top - lufs) / (top - bottom)) * plot.getHeight(); };
        const auto t0 = s.historyT0, t1 = std::max (s.historyT0 + 1.0, s.historyT1);
        const auto xOf = [&] (double t) { return plot.getX() + (float) ((t - t0) / (t1 - t0)) * plot.getWidth(); };

        g.setFont (font (10.0f));
        for (int db = 0; db >= -42; db -= 6)
        {
            const auto y = yOf (db);
            g.setColour (Palette::grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (Palette::textMuted);
            g.drawText (fmt (db, 0, -1.0e9), juce::Rectangle<float> ((float) area.getX(), y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
        }
        const double spanSec = t1 - t0;
        const double step = spanSec <= 40 ? 5 : spanSec <= 90 ? 10 : spanSec <= 240 ? 30 : spanSec <= 700 ? 60 : spanSec <= 1800 ? 300 : 600;
        for (double t = std::ceil (t0 / step) * step; t <= t1 + 1.0e-6; t += step)
        {
            const auto x = xOf (t);
            g.setColour (Palette::grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            g.drawText (timeLabel (t), juce::Rectangle<float> (x - 22, plot.getBottom() + 2, 44, 12), juce::Justification::centred);
        }

        // Target band (normalising targets only).
        if (ctx.target.policy != dsp::DeliveryTarget::Policy::none)
        {
            g.setColour (Palette::accent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (ctx.target.lufs + 1.0), plot.getWidth(), yOf (ctx.target.lufs - 1.0) - yOf (ctx.target.lufs + 1.0)));
        }

        // Selected finding span.
        if (ctx.selectedFinding >= 0 && ctx.selectedFinding < (int) s.findings.size())
        {
            const auto& f = s.findings[(size_t) ctx.selectedFinding];
            if (f.t0 >= 0.0)
            {
                const auto x0 = juce::jlimit (plot.getX(), plot.getRight(), xOf (f.t0));
                const auto x1 = juce::jlimit (plot.getX(), plot.getRight(), xOf (std::max (f.t1, f.t0 + 0.4)));
                g.setColour (Palette::frozen.withAlpha (0.16f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), std::max (2.0f, x1 - x0), plot.getHeight()));
            }
        }

        const auto drawSeries = [&] (const std::vector<float>& v, juce::Colour c, float thickness)
        {
            juce::Path p;
            bool open = false;
            const auto count = (int) v.size();
            for (int i = 0; i < count; ++i)
            {
                const auto value = v[(size_t) i];
                if (value <= -100.0f)
                {
                    open = false;
                    continue;
                }
                const auto x = plot.getX() + plot.getWidth() * ((float) i + 0.5f) / (float) count;
                const auto y = juce::jlimit (plot.getY(), plot.getBottom(), yOf (value));
                if (! open)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
                open = true;
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        drawSeries (s.historyMomentary, Palette::momentary.withAlpha (0.55f), 1.0f);
        drawSeries (s.historyShortTerm, Palette::accent, 1.8f);

        if (s.loudness.hasIntegrated)
        {
            const auto y = yOf (s.loudness.integrated);
            juce::Path dash;
            dash.startNewSubPath (plot.getX(), y);
            dash.lineTo (plot.getRight(), y);
            juce::Path dashed;
            const float pattern[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, dash, pattern, 2);
            g.setColour (Palette::textPrimary.withAlpha (0.8f));
            g.fillPath (dashed);
        }

        g.setColour (Palette::danger);
        for (const auto t : s.overTimes)
            if (t >= t0 && t <= t1)
                g.fillRect (juce::Rectangle<float> (xOf (t) - 1.0f, plot.getY(), 2.0f, 6.0f));
    }

    // ---------------------------------------------------------------------------------------------
    juce::String SpectrumView::headerNote() const
    {
        return "1/3 octave   curve " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + "3 dB";
    }

    void SpectrumView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        auto plot = area.withTrimmedLeft (30).withTrimmedBottom (16).toFloat();
        const auto lf = std::log (20.0), hf = std::log (20000.0);
        const auto xOf = [&] (double hz) { return plot.getX() + (float) ((std::log (hz) - lf) / (hf - lf)) * plot.getWidth(); };
        const auto tilt = [&] (double hz) { return ctx.slopeDbPerOct * std::log2 (hz / 1000.0); };

        // Vertical range: follows the long-term level (or the live level) so curves stay in view.
        double ref = -30.0;
        if (s.ltasValid)
        {
            double sum = 0.0;
            for (int b = 0; b < dsp::bands::count; ++b)
                sum += s.curveDb[(size_t) b] + tilt (dsp::bands::centre (b));
            ref = sum / dsp::bands::count;
        }
        else if (! s.liveSpectrum.empty())
        {
            double peak = -150.0;
            for (size_t j = 0; j < s.liveSpectrum.size(); ++j)
                peak = std::max (peak, s.liveSpectrum[j] + tilt (dsp::spectrumHz()[j]));
            if (peak > -140.0)
                ref = peak - 12.0;
        }
        const auto topDb = std::ceil ((ref + 24.0) / 6.0) * 6.0, rangeDb = 60.0;
        const auto yOf = [&] (double db) { return plot.getY() + (float) ((topDb - db) / rangeDb) * plot.getHeight(); };

        g.setFont (font (10.0f));
        for (double db = topDb; db >= topDb - rangeDb; db -= 12.0)
        {
            g.setColour (Palette::grid);
            g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
            g.setColour (Palette::textMuted);
            g.drawText (fmt (db, 0, -1.0e9), juce::Rectangle<float> ((float) area.getX(), yOf (db) - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
        }
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto x = xOf (hz);
            g.setColour (Palette::grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), juce::Justification::centred);
        }

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        if (s.ltasValid)
        {
            // Problem bands: outside the tolerance.
            for (int b = 0; b < dsp::bands::count; ++b)
            {
                const auto d = s.deviationDb[(size_t) b];
                if (std::abs (d) <= dsp::curves::tolerance)
                    continue;
                const auto x0 = xOf (std::max (20.0, dsp::bands::lowerEdge (b))), x1 = xOf (std::min (20000.0, dsp::bands::upperEdge (b)));
                g.setColour ((d > 0 ? Palette::danger : Palette::momentary).withAlpha (0.13f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));
            }

            // Tolerance band and curve.
            juce::Path band, curve;
            for (int b = 0; b < dsp::bands::count; ++b)
            {
                const auto hz = dsp::bands::centre (b);
                const auto x = xOf (juce::jlimit (20.0, 20000.0, hz));
                const auto y = yOf (s.curveDb[(size_t) b] + tilt (hz));
                if (b == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            for (int b = 0; b < dsp::bands::count; ++b)
            {
                const auto hz = dsp::bands::centre (b);
                const auto p = juce::Point<float> (xOf (juce::jlimit (20.0, 20000.0, hz)), yOf (s.curveDb[(size_t) b] + tilt (hz) + dsp::curves::tolerance));
                if (b == 0) band.startNewSubPath (p); else band.lineTo (p);
            }
            for (int b = dsp::bands::count - 1; b >= 0; --b)
            {
                const auto hz = dsp::bands::centre (b);
                band.lineTo (xOf (juce::jlimit (20.0, 20000.0, hz)), yOf (s.curveDb[(size_t) b] + tilt (hz) - dsp::curves::tolerance));
            }
            band.closeSubPath();
            g.setColour (Palette::textMuted.withAlpha (0.10f));
            g.fillPath (band);
            juce::Path dashed;
            const float pattern[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve, pattern, 2);
            g.setColour (Palette::textMuted.withAlpha (0.9f));
            g.fillPath (dashed);
        }

        // Selected finding band.
        if (ctx.selectedFinding >= 0 && ctx.selectedFinding < (int) s.findings.size())
        {
            const auto& f = s.findings[(size_t) ctx.selectedFinding];
            if (f.fHi > f.fLo)
            {
                const auto x0 = xOf (std::max (20.0f, f.fLo)), x1 = xOf (std::min (20000.0f, f.fHi));
                g.setColour (Palette::frozen.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));
                g.setColour (Palette::frozen.withAlpha (0.8f));
                g.drawRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()), 1.0f);
            }
        }

        // Live spectrum.
        if (! s.liveSpectrum.empty())
        {
            const auto& hz = dsp::spectrumHz();
            juce::Path line, fill;
            for (size_t j = 0; j < s.liveSpectrum.size(); ++j)
            {
                const auto p = juce::Point<float> (xOf (hz[j]), juce::jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yOf (s.liveSpectrum[j] + tilt (hz[j]))));
                if (j == 0) { line.startNewSubPath (p); fill.startNewSubPath (p.x, plot.getBottom()); fill.lineTo (p); }
                else { line.lineTo (p); fill.lineTo (p); }
            }
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setColour (Palette::spectrum.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (Palette::spectrum.withAlpha (0.6f));
            g.strokePath (line, juce::PathStrokeType (1.1f));
        }

        // Long-term spectrum.
        if (s.ltasValid)
        {
            juce::Path p;
            for (int b = 0; b < dsp::bands::count; ++b)
            {
                const auto hz = dsp::bands::centre (b);
                const auto pt = juce::Point<float> (xOf (juce::jlimit (20.0, 20000.0, hz)), yOf (s.ltasDb[(size_t) b] + tilt (hz)));
                if (b == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (Palette::dynamic);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.restoreState();
    }

    // ---------------------------------------------------------------------------------------------
    void StereoView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        auto bars = area.removeFromBottom (std::min (78, area.getHeight() / 2));
        const auto side = std::min (area.getWidth(), area.getHeight());
        auto scopeArea = area.withSizeKeepingCentre (side, side).toFloat();
        const auto c = scopeArea.getCentre();
        const auto r = (float) side * 0.5f - 2.0f;

        g.setColour (Palette::grid);
        g.drawEllipse (scopeArea.reduced (2.0f), 1.0f);
        g.drawLine (c.x, c.y - r, c.x, c.y + r, 1.0f);
        g.drawLine (c.x - r, c.y, c.x + r, c.y, 1.0f);
        g.drawLine (c.x - r * 0.707f, c.y - r * 0.707f, c.x + r * 0.707f, c.y + r * 0.707f, 0.6f);
        g.drawLine (c.x + r * 0.707f, c.y - r * 0.707f, c.x - r * 0.707f, c.y + r * 0.707f, 0.6f);
        g.setColour (Palette::textMuted);
        g.setFont (font (10.0f, true));
        g.drawText ("L", juce::Rectangle<float> (c.x - r * 0.72f - 12, c.y - r * 0.72f - 12, 12, 12), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (c.x + r * 0.72f, c.y - r * 0.72f - 12, 12, 12), juce::Justification::centred);

        // Auto-scaled by the RMS radius so quiet material stays visible (display only).
        double sum = 0.0;
        const auto pairs = s.scope.size() / 2;
        for (size_t i = 0; i < pairs; ++i)
            sum += (double) s.scope[2 * i] * s.scope[2 * i] + (double) s.scope[2 * i + 1] * s.scope[2 * i + 1];
        const auto rms = pairs > 0 ? std::sqrt (sum / (double) pairs) : 0.0;
        const auto scale = rms > 1.0e-5 ? (float) (0.32 / rms) * r : 0.0f;
        if (scale > 0.0f)
        {
            g.setColour (Palette::accent.withAlpha (0.55f));
            for (size_t i = 0; i < pairs; ++i)
            {
                const auto L = s.scope[2 * i], R = s.scope[2 * i + 1];
                auto x = (R - L) * 0.7071f * scale, y = (L + R) * 0.7071f * scale;
                const auto len = std::sqrt (x * x + y * y);
                if (len > r)
                {
                    x *= r / len;
                    y *= r / len;
                }
                g.fillRect (c.x + x, c.y - y, 1.4f, 1.4f);
            }
        }

        // Correlation: overall, then the three bands.
        bars.removeFromTop (6);
        const auto barRow = [&] (juce::Rectangle<int> row, const juce::String& label, float value, bool main)
        {
            auto labelArea = row.removeFromLeft (40);
            auto valueArea = row.removeFromRight (34);
            g.setColour (Palette::textMuted);
            g.setFont (font (10.5f, main));
            g.drawText (label, labelArea, juce::Justification::centredLeft);
            g.setColour (value < 0.0f ? Palette::danger : Palette::textPrimary);
            g.drawText (signedFmt (value, 2), valueArea, juce::Justification::centredRight);
            auto track = row.reduced (4, row.getHeight() / 2 - (main ? 3 : 2)).toFloat();
            g.setColour (Palette::divider);
            g.fillRoundedRectangle (track, 2.0f);
            const auto mid = track.getCentreX();
            const auto x = mid + value * track.getWidth() * 0.5f;
            g.setColour (value < 0.0f ? Palette::danger : Palette::accent);
            g.fillRect (juce::Rectangle<float> (std::min (mid, x), track.getY(), std::abs (x - mid), track.getHeight()));
            g.setColour (Palette::textMuted);
            g.fillRect (juce::Rectangle<float> (mid - 0.5f, track.getY() - 2, 1.0f, track.getHeight() + 4));
        };
        const auto rowH = bars.getHeight() / 5;
        barRow (bars.removeFromTop (rowH), "CORR", s.correlation, true);
        barRow (bars.removeFromTop (rowH), "LOW", s.bandCorrelation[0], false);
        barRow (bars.removeFromTop (rowH), "MID", s.bandCorrelation[1], false);
        barRow (bars.removeFromTop (rowH), "HIGH", s.bandCorrelation[2], false);
        drawRow (g, bars, "Width S/M", fmt (s.widthDb, 1) + " dB", Palette::textPrimary, 10.5f);
    }

    // ---------------------------------------------------------------------------------------------
    juce::Rectangle<int> TranslationView::tileBounds (int index) const
    {
        auto area = contentArea();
        const bool twoColumns = area.getWidth() >= 300;
        const int cols = twoColumns ? 2 : 1, rows = dsp::profiles::count / cols;
        const auto w = area.getWidth() / cols, h = area.getHeight() / rows;
        const auto col = index % cols, row = index / cols;
        return juce::Rectangle<int> (area.getX() + col * w, area.getY() + row * h, w, h).reduced (2);
    }

    void TranslationView::paintContent (juce::Graphics& g, juce::Rectangle<int>)
    {
        for (int i = 0; i < dsp::profiles::count; ++i)
        {
            const auto r = tileBounds (i);
            const auto& profile = dsp::profiles::get (i);
            const bool valid = snap != nullptr && snap->translationValid;
            const auto& t = valid ? snap->translation[(size_t) i] : dsp::TranslationResult();
            const bool listening = ctx.listenProfile == i;

            g.setColour (listening ? Palette::accent.withAlpha (0.18f) : Palette::panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            if (listening)
            {
                g.setColour (Palette::accent);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.2f);
            }

            auto inner = r.reduced (7, 4);
            auto top = inner.removeFromTop (std::min (18, inner.getHeight() / 2));
            g.setColour (Palette::textPrimary);
            g.setFont (font (12.0f, true));
            g.drawFittedText (profile.shortName, top.withTrimmedRight (36), juce::Justification::centredLeft, 1, 0.75f);
            g.setColour (valid ? scoreColour (t.score) : Palette::textMuted);
            g.setFont (font (15.0f, true));
            g.drawText (valid ? juce::String (t.score) : noValue, top.removeFromRight (36), juce::Justification::centredRight);

            auto bar = inner.removeFromTop (5).reduced (0, 1).toFloat();
            g.setColour (Palette::divider);
            g.fillRoundedRectangle (bar, 1.5f);
            if (valid)
            {
                g.setColour (scoreColour (t.score));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) t.score / 100.0f), 1.5f);
            }

            if (valid && ! t.reasons.isEmpty() && inner.getHeight() >= 11)
            {
                g.setColour (Palette::textMuted);
                g.setFont (font (10.5f));
                const auto lines = juce::jlimit (1, 3, inner.getHeight() / 12);
                g.drawFittedText (t.reasons[0], inner, juce::Justification::topLeft, lines, 0.85f);
            }
        }
    }

    void TranslationView::mouseUp (const juce::MouseEvent& e)
    {
        for (int i = 0; i < dsp::profiles::count; ++i)
            if (tileBounds (i).contains (e.getPosition()) && onTileClicked)
                onTileClicked (i);
    }

    // ---------------------------------------------------------------------------------------------
    class FindingsView::List final : public juce::Component
    {
    public:
        explicit List (FindingsView& o) : owner (o) {}

        int layout (int width)
        {
            rows.clear();
            int y = 0;
            if (owner.snap != nullptr)
                for (const auto& f : owner.snap->findings)
                {
                    const auto textW = std::max (100, width - 86);
                    const auto h = 22 + textHeight (f.detail, textW, 11.5f) + textHeight ("Try: " + f.fix, textW, 11.5f) + 8;
                    rows.push_back ({ y, h });
                    y += h + 4;
                }
            return std::max (y, 1);
        }

        void paint (juce::Graphics& g) override
        {
            if (owner.snap == nullptr)
                return;
            const auto& findings = owner.snap->findings;
            if (findings.empty())
            {
                if (owner.snap->analysedSeconds >= 8.0)
                {
                    g.setColour (Palette::textMuted);
                    g.setFont (font (12.0f));
                    g.drawText ("Nothing flagged so far.", getLocalBounds().withHeight (22).reduced (6, 0), juce::Justification::centredLeft);
                }
                return;
            }
            for (size_t i = 0; i < findings.size() && i < rows.size(); ++i)
            {
                const auto& f = findings[i];
                auto r = juce::Rectangle<int> (0, rows[i].first, getWidth(), rows[i].second);
                const bool selected = (int) i == owner.ctx.selectedFinding;
                g.setColour (selected ? Palette::frozen.withAlpha (0.14f) : Palette::panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 4.0f);

                const auto colour = f.severity == dsp::Finding::Severity::fix ? Palette::danger
                                  : f.severity == dsp::Finding::Severity::watch ? Palette::dynamic : Palette::momentary;
                auto pill = juce::Rectangle<int> (r.getX() + 8, r.getY() + 5, 58, 16);
                g.setColour (colour.withAlpha (0.22f));
                g.fillRoundedRectangle (pill.toFloat(), 8.0f);
                g.setColour (colour);
                g.setFont (font (10.0f, true));
                g.drawText (dsp::severityName (f.severity), pill, juce::Justification::centred);

                auto text = r.withTrimmedLeft (78).withTrimmedRight (8).withTrimmedTop (4);
                g.setColour (Palette::textPrimary);
                g.setFont (font (12.5f, true));
                g.drawFittedText (f.title, text.removeFromTop (18), juce::Justification::centredLeft, 1, 0.8f);
                const auto w = text.getWidth();
                g.setFont (font (11.5f));
                g.setColour (Palette::textPrimary.withAlpha (0.85f));
                const auto dh = textHeight (f.detail, w, 11.5f);
                g.drawFittedText (f.detail, text.removeFromTop (dh), juce::Justification::topLeft, 8, 1.0f);
                g.setColour (Palette::textMuted);
                g.drawFittedText ("Try: " + f.fix, text.removeFromTop (textHeight ("Try: " + f.fix, w, 11.5f)), juce::Justification::topLeft, 8, 1.0f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (size_t i = 0; i < rows.size(); ++i)
                if (e.y >= rows[i].first && e.y < rows[i].first + rows[i].second && owner.onSelect)
                    owner.onSelect ((int) i == owner.ctx.selectedFinding ? -1 : (int) i);
        }

    private:
        static int textHeight (const juce::String& text, int width, float size)
        {
            juce::AttributedString a;
            a.append (text, font (size));
            juce::TextLayout layout;
            layout.createLayout (a, (float) width);
            return (int) std::ceil (layout.getHeight()) + 2;
        }

        FindingsView& owner;
        std::vector<std::pair<int, int>> rows;
    };

    FindingsView::FindingsView()
        : list (std::make_unique<List> (*this))
    {
        viewport.setViewedComponent (list.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
    }

    FindingsView::~FindingsView() = default;

    void FindingsView::update (SnapshotPtr s, const ViewContext& c)
    {
        const bool changedFindings = snap == nullptr || s == nullptr || s->findings.size() != snap->findings.size()
                                  || (! s->findings.empty() && s->findings.front().detail != snap->findings.front().detail)
                                  || c.selectedFinding != ctx.selectedFinding;
        snap = std::move (s);
        ctx = c;
        if (changedFindings)
            resized();
        list->repaint();
        repaint (getLocalBounds().removeFromTop (titleHeight));
    }

    void FindingsView::paint (juce::Graphics& g)
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (Palette::textMuted);
        g.setFont (font (11.0f, true));
        auto header = getLocalBounds().reduced (10, 0).removeFromTop (titleHeight);
        int fixes = 0, watches = 0;
        if (snap != nullptr)
            for (const auto& f : snap->findings)
            {
                fixes += f.severity == dsp::Finding::Severity::fix ? 1 : 0;
                watches += f.severity == dsp::Finding::Severity::watch ? 1 : 0;
            }
        g.drawText ("FINDINGS", header, juce::Justification::centredLeft);
        if (snap != nullptr && ! snap->findings.empty())
        {
            g.setFont (font (11.0f));
            g.drawText (juce::String (fixes) + " fix, " + juce::String (watches) + " watch, "
                            + juce::String ((int) snap->findings.size() - fixes - watches) + " info",
                        header, juce::Justification::centredRight);
        }
    }

    void FindingsView::resized()
    {
        viewport.setBounds (getLocalBounds().reduced (8, 0).withTrimmedTop (titleHeight).withTrimmedBottom (6));
        const auto w = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
        list->setSize (w, list->layout (w));
    }

    // ---------------------------------------------------------------------------------------------
    void DeliveryView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        const auto names = dsp::targets::names();
        const int rowsN = dsp::targets::count + 1;
        const auto rowH = std::max (16, std::min (24, area.getHeight() / rowsN));
        const float cols[] = { 0.0f, 0.30f, 0.43f, 0.56f, 0.80f, 1.0f };
        const auto cell = [&] (juce::Rectangle<int> row, int c) {
            return juce::Rectangle<int> (row.getX() + (int) (cols[c] * (float) row.getWidth()), row.getY(),
                                         (int) ((cols[c + 1] - cols[c]) * (float) row.getWidth()), row.getHeight()).reduced (3, 0);
        };
        auto header = area.removeFromTop (rowH);
        g.setColour (Palette::textMuted);
        g.setFont (font (10.5f, true));
        const char* heads[] = { "TARGET", "LUFS", "CEILING", "PLAYBACK", "CHANGE" };
        for (int c = 0; c < 5; ++c)
            g.drawFittedText (heads[c], cell (header, c), c == 0 || c == 3 ? juce::Justification::centredLeft : juce::Justification::centredRight, 1, 0.7f);

        for (int i = 0; i < dsp::targets::count; ++i)
        {
            const auto t = dsp::targets::get (i, ctx.targetIndex == dsp::targets::customIndex ? ctx.target.lufs : -14.0f,
                                              ctx.targetIndex == dsp::targets::customIndex ? ctx.target.ceiling : -1.0f);
            const auto tt = i == ctx.targetIndex ? ctx.target : t;
            auto row = area.removeFromTop (rowH);
            if (i == ctx.targetIndex)
            {
                g.setColour (Palette::accent.withAlpha (0.14f));
                g.fillRect (row);
            }
            g.setFont (font (11.5f, i == ctx.targetIndex));
            g.setColour (Palette::textPrimary);
            g.drawFittedText (names[i], cell (row, 0), juce::Justification::centredLeft, 1, 0.7f);
            g.drawFittedText (fmt (tt.lufs, 1, -1.0e9), cell (row, 1), juce::Justification::centredRight, 1, 0.7f);
            g.drawFittedText (fmt (tt.ceiling, 1, -1.0e9), cell (row, 2), juce::Justification::centredRight, 1, 0.7f);
            g.setColour (Palette::textMuted);
            g.drawFittedText (dsp::targets::policyText (tt.policy), cell (row, 3), juce::Justification::centredLeft, 1, 0.6f);
            g.setColour (Palette::textPrimary);
            juce::String change = noValue;
            if (s.loudness.hasIntegrated)
            {
                const auto gain = dsp::targets::predictedGainDb (tt, (float) s.loudness.integrated, s.truePeakMax);
                change = signedFmt (gain, 1) + " dB";
                if (s.truePeakMax + gain > tt.ceiling && tt.policy != dsp::DeliveryTarget::Policy::none)
                    g.setColour (Palette::danger);
            }
            g.drawFittedText (change, cell (row, 4), juce::Justification::centredRight, 1, 0.7f);
        }
    }

    // ---------------------------------------------------------------------------------------------
    void MetricsView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& s = *snap;
        const auto& L = s.loudness;
        struct Item { juce::String label, value; bool bad; };
        const std::vector<Item> items {
            { "Integrated", L.hasIntegrated ? fmt (L.integrated, 1) + " LUFS" : noValue, false },
            { "Loudness range", L.hasLra ? fmt (L.lra, 1) + " LU" : noValue, false },
            { "Short-term / momentary", fmt (s.shortTerm, 1) + " / " + fmt (s.momentary, 1), false },
            { "True peak (max)", fmt (s.truePeakMax, 1) + " dBTP", s.truePeakMax > ctx.target.ceiling },
            { "Sample peak (max)", fmt (s.samplePeakMax, 1) + " dBFS", false },
            { "PLR / median PSR", (L.hasIntegrated ? fmt (s.plr, 1) : noValue) + " / " + fmt (s.medianPsr, 1, 0.0) + " dB", L.hasIntegrated && s.plr < 7.0f },
            { "RMS (3 s)", fmt (s.rmsShort, 1) + " dBFS", false },
            { "Correlation (programme)", signedFmt (s.correlationLong, 2), s.correlationLong < 0.0f },
            { "Low / mid / high corr.", signedFmt (s.bandCorrelationLong[0], 2) + " / " + signedFmt (s.bandCorrelationLong[1], 2) + " / "
                                            + signedFmt (s.bandCorrelationLong[2], 2),
              s.bandCorrelationLong[0] < 0.0f || s.bandCorrelationLong[1] < 0.0f || s.bandCorrelationLong[2] < 0.0f },
            { "Width (side vs mid)", fmt (s.sideToMidDb, 1) + " dB", s.sideToMidDb > 0.0f },
            { "Side below 120 Hz", fmt (s.sideLowToMidLowDb, 1) + " dB", s.sideLowToMidLowDb > -12.0f && s.sideLowToMidLowDb > -100.0f },
            { "Mono sum loss", fmt (s.monoLossDb, 1, -1.0e9) + " dB", s.monoLossDb < -1.5f },
            { "Balance (L vs R)", signedFmt (s.balanceDb, 1) + " dB", std::abs (s.balanceDb) > 1.5f },
            { "DC offset L / R", fmt (s.dcLeftDb, 1, -140.0) + " / " + fmt (s.dcRightDb, 1, -140.0) + " dBFS", std::max (s.dcLeftDb, s.dcRightDb) > -60.0f },
            { "Clip runs / TP overs", juce::String (s.clipRuns) + " / " + juce::String (s.overRuns), s.clipRuns > 0 || s.overRuns > 0 },
            { "Measured", timeLabel (s.analysedSeconds) + (s.droppedChunks > 0 ? "  (" + juce::String (s.droppedChunks) + " analysis gaps)" : juce::String()), false },
        };
        const auto rowH = std::max (14, std::min (22, area.getHeight() / (int) items.size()));
        for (const auto& it : items)
            drawRow (g, area.removeFromTop (rowH), it.label, it.value, it.bad ? Palette::danger : Palette::textPrimary, 11.5f);
    }

    // ---------------------------------------------------------------------------------------------
    void HistogramView::paintContent (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (snap == nullptr)
            return;
        const auto& h = snap->stHistogram;
        auto plot = area.withTrimmedBottom (14).withTrimmedLeft (4).toFloat();
        int maxCount = 0;
        for (auto v : h)
            maxCount = std::max (maxCount, v);
        constexpr int lo = -42; // draw -42..0 LUFS
        const auto bins = -lo;
        const auto w = plot.getWidth() / (float) bins;
        g.setFont (font (10.0f));
        for (int db = lo; db <= 0; db += 6)
        {
            const auto x = plot.getX() + (float) (db - lo) * w;
            g.setColour (Palette::grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            g.drawText (fmt (db, 0, -1.0e9), juce::Rectangle<float> (x - 15, plot.getBottom() + 1, 30, 12), juce::Justification::centred);
        }
        if (maxCount <= 0)
            return;
        for (int i = 0; i < bins; ++i)
        {
            const auto count = h[(size_t) (60 + lo + i)];
            if (count <= 0)
                continue;
            const auto bh = plot.getHeight() * (float) count / (float) maxCount;
            const auto lufs = (float) (lo + i) + 0.5f;
            g.setColour ((std::abs (lufs - ctx.target.lufs) <= 1.0f ? Palette::accent : Palette::spectrum).withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (plot.getX() + (float) i * w + 1.0f, plot.getBottom() - bh, w - 2.0f, bh));
        }
    }
}
