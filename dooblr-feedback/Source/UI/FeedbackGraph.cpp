#include "FeedbackGraph.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "DisplayBallistics.h"
#include "Theme.h"

namespace dooblrfeedback::ui
{
    namespace
    {
        constexpr float logSpan = 6.907755279f; // ln (20000 / 20)

        juce::String timeText (juce::int64 ms)
        {
            return ms > 0 ? juce::Time (ms).formatted ("%H:%M:%S") : juce::String ("-");
        }

        juce::String cutText (float cut)
        {
            return params::formatCut (cut, true);
        }

        /** Same filters (for the curve): frequency, cut, Q. Labels do not follow this. */
        bool sameCurve (const dsp::NotchArray& a, const dsp::NotchArray& b)
        {
            for (size_t i = 0; i < a.size(); ++i)
            {
                const auto& x = a[i];
                const auto& y = b[i];
                if (x.active != y.active) return false;
                if (! x.active) continue;
                if (x.id != y.id || x.fixed != y.fixed || std::abs (x.freq - y.freq) > 0.01f
                    || std::abs (x.cutDb - y.cutDb) > 0.01f || std::abs (x.q - y.q) > 0.01f)
                    return false;
            }
            return true;
        }

        float easeOut (double elapsed, double span)
        {
            const auto u = (float) juce::jlimit (0.0, 1.0, elapsed / span);
            return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
        }

        juce::Rectangle<float> lerp (juce::Rectangle<float> a, juce::Rectangle<float> b, float t)
        {
            return { a.getX() + (b.getX() - a.getX()) * t, a.getY() + (b.getY() - a.getY()) * t,
                     a.getWidth() + (b.getWidth() - a.getWidth()) * t, a.getHeight() + (b.getHeight() - a.getHeight()) * t };
        }

        juce::Path padlock (juce::Rectangle<float> box, bool closed)
        {
            juce::Path p;
            const auto body = juce::Rectangle<float> (box.getWidth() * 0.62f, box.getHeight() * 0.42f)
                                  .withCentre ({ box.getCentreX(), box.getY() + box.getHeight() * 0.66f });
            p.addRoundedRectangle (body, 1.2f);
            const auto r = body.getWidth() * 0.32f;
            const auto cx = closed ? body.getCentreX() : body.getCentreX() + r * 0.9f;
            juce::Path shackle;
            shackle.addCentredArc (cx, body.getY(), r, r * 1.15f, 0.0f, -juce::MathConstants<float>::halfPi,
                                   juce::MathConstants<float>::halfPi, true);
            p.addPath (shackle);
            return p;
        }
    }

    FeedbackGraph::FeedbackGraph (DooblrFeedbackProcessor& p) : processor (p)
    {
        setOpaque (true);
        setComponentID ("graph");
        markers.reserve (dsp::maxNotches);
        labels.reserve (2 * dsp::maxNotches);
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { poll(); });
        refreshNotches();
    }

    FeedbackGraph::~FeedbackGraph() = default;

    //==============================================================================
    bool FeedbackGraph::poll()
    {
        auto dirty = false;
        auto fresh = false;
        if (processor.getEngine().getDisplay().read (target))
        {
            hasFrame = true;
            fresh = true;
        }

        const auto nowMs = clockMs();
        const auto dt = lastTickMs > 0.0 ? std::clamp ((nowMs - lastTickMs) * 0.001, 0.0, 0.25) : 0.0;
        lastTickMs = nowMs;
        if (hasFrame && (fresh || settling || ! shownValid))
            dirty = advanceDisplay (dt) || fresh;

        notches = processor.getNotches(); // ringing / hits / levels: labels pick these up calmly below
        if (easeDrawnNotches (dt))
        {
            rebuildCurve();
            dirty = true;
        }

        if (updateLabels (nowMs)) layoutMarkers (nowMs, false);
        if (animateLabels (nowMs)) dirty = true;
        buildMarkers();

        if (dirty) repaint();
        return dirty;
    }

    double FeedbackGraph::clockMs() const noexcept
    {
        return testClockMs >= 0.0 ? testClockMs : juce::Time::getMillisecondCounterHiRes();
    }

    bool FeedbackGraph::advanceDisplay (double dtSeconds)
    {
        if (! hasFrame) return false;
        if (! shownValid)
        {
            snapDisplay();
            return true;
        }
        const auto times = analyzerTimes (analyzerSpeedFromIndex (processor.getAnalyzerSpeed()));
        constexpr auto n = (std::size_t) dsp::DisplayFrame::numPoints;
        auto biggest = applyBallistics (target.input.data(), frame.input.data(), n, dtSeconds, times);
        biggest = std::max (biggest, applyBallistics (target.output.data(), frame.output.data(), n, dtSeconds, times));
        frame.binHz = target.binHz;
        frame.serial = target.serial;
        settling = biggest > 0.01f;
        if (biggest <= 0.0f) return false;
        rebuildSpectrumPaths();
        return true;
    }

    void FeedbackGraph::snapDisplay()
    {
        if (! hasFrame) return;
        frame = target;
        shownValid = true;
        settling = false;
        rebuildSpectrumPaths();
        repaint();
    }

    void FeedbackGraph::refreshNotches()
    {
        const auto nowMs = clockMs();
        notches = processor.getNotches();
        snapDrawnNotches();
        if (updateLabels (nowMs)) layoutMarkers (nowMs, false);
        animateLabels (nowMs);
        buildMarkers();
        rebuildCurve();
        repaint();
    }

    void FeedbackGraph::setHoveredNotch (int notchId)
    {
        if (notchId == tableHoverNotch) return;
        tableHoverNotch = notchId;
        repaint();
    }

    FeedbackGraph::Label* FeedbackGraph::labelFor (int notchId) noexcept
    {
        for (auto& l : labels)
            if (l.id == notchId && ! l.gone) return &l;
        return nullptr;
    }

    void FeedbackGraph::setSelectedNotch (int notchId)
    {
        if (notchId == selectedNotch) return;
        selectedNotch = notchId;
        repaint();
    }

    const FeedbackGraph::Marker* FeedbackGraph::markerFor (int notchId) const noexcept
    {
        for (const auto& m : markers)
            if (m.id == notchId) return &m;
        return nullptr;
    }

    const dsp::Notch* FeedbackGraph::notchById (int notchId) const noexcept
    {
        for (const auto& n : notches)
            if (n.active && n.id == notchId) return &n;
        return nullptr;
    }

    const dsp::Notch* FeedbackGraph::drawnById (int notchId) const noexcept
    {
        for (const auto& n : drawnNotches)
            if (n.active && n.id == notchId) return &n;
        return nullptr;
    }

    // The drawn bank eases toward the real one (time-based, so the same at any frame rate): a
    // new notch grows in from 0 dB, a cleared or freed one shrinks back to 0 dB, and depth,
    // frequency and Q changes glide (one-pole, notchEaseSec). Cuts ease on the drawn scale
    // (a full notch's 120 dB is drawn as the line under 30 dB), then land exactly.
    bool FeedbackGraph::easeDrawnNotches (double dtSeconds)
    {
        if (sameCurve (drawnNotches, notches))
        {
            drawnNotches = notches; // flags, hits, levels: no redraw needed for the curve
            return false;
        }
        const auto k = (float) (1.0 - std::exp (-std::max (0.0, dtSeconds) / notchEaseSec));
        constexpr auto drawCap = cutRangeDb + 1.0f;
        for (size_t i = 0; i < notches.size(); ++i)
        {
            const auto& t = notches[i];
            auto& d = drawnNotches[i];
            if (t.active)
            {
                if (! d.active || d.id != t.id)
                {
                    d = t;
                    d.cutDb = 0.0f;
                }
                const auto cut = std::min (d.cutDb, drawCap), freq = d.freq, q = d.q;
                const auto target = std::min (t.cutDb, drawCap);
                d = t;
                d.cutDb = cut + (target - cut) * k;
                d.freq = freq * std::pow (t.freq / std::max (1.0f, freq), k);
                d.q = q * std::pow (t.q / std::max (0.01f, q), k);
                if (std::abs (d.cutDb - target) < 0.02f && std::abs (d.freq - t.freq) < 0.01f && std::abs (d.q - t.q) < 0.01f)
                    d = t;
            }
            else if (d.active)
            {
                d.cutDb = std::min (d.cutDb, drawCap) * (1.0f - k);
                if (d.cutDb < 0.02f) d = dsp::Notch {};
            }
        }
        return true;
    }

    //==============================================================================
    float FeedbackGraph::xForFrequency (float f) const noexcept
    {
        const auto t = std::log (juce::jlimit (minHz, maxHz, f) / minHz) / logSpan;
        return plot.getX() + t * plot.getWidth();
    }

    float FeedbackGraph::frequencyForX (float x) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / std::max (1.0f, plot.getWidth()));
        return minHz * std::exp (t * logSpan);
    }

    float FeedbackGraph::yForLevel (float dbfs) const noexcept
    {
        return juce::jmap (juce::jlimit (spectrumFloorDb, 0.0f, dbfs), spectrumFloorDb, 0.0f, plot.getBottom(), plot.getY());
    }

    float FeedbackGraph::yForCut (float cutDb) const noexcept
    {
        // 0 .. cutRangeDb over the scale, and a full notch (infinite cut) on its own line below.
        if (cutDb > cutRangeDb + 0.5f) return plot.getBottom() - 6.0f;
        return juce::jmap (juce::jlimit (0.0f, cutRangeDb, cutDb), 0.0f, cutRangeDb, baselineY, plot.getBottom() - 20.0f);
    }

    void FeedbackGraph::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (4.0f);
        area.removeFromTop (18.0f);
        area.removeFromRight (30.0f);
        area.removeFromLeft (2.0f);
        area.removeFromBottom (2.0f);
        plot = area;
        baselineY = plot.getY() + juce::jlimit (70.0f, 210.0f, plot.getHeight() * 0.45f);
        background = {};
        const auto nowMs = clockMs();
        updateLabels (nowMs);
        layoutMarkers (nowMs, true);
        animateLabels (nowMs);
        buildMarkers();
        rebuildCurve();
        rebuildSpectrumPaths();
    }

    // Labels -----------------------------------------------------------------------------------
    // Returns true when the layout must change: a notch was added or removed, a label wants to
    // open or close, or a notch moved out from under its label / dot. Cut, level, ringing and hit
    // changes never relayout (the handle follows the cut; the numbers refresh calmly).
    bool FeedbackGraph::updateLabels (double nowMs)
    {
        auto relayout = false;
        for (auto& l : labels)
            if (! l.gone && notchById (l.id) == nullptr)
            {
                l.gone = true;
                l.goneMs = nowMs;
                l.want = false;
                relayout = true;
            }
        labels.erase (std::remove_if (labels.begin(), labels.end(),
                                      [nowMs] (const Label& l) { return l.gone && nowMs - l.goneMs > fadeMs; }),
                      labels.end());

        for (const auto& n : notches)
        {
            if (! n.active) continue;
            auto* l = labelFor (n.id);
            if (l == nullptr)
            {
                Label fresh;
                fresh.id = n.id;
                fresh.bornMs = fresh.lastActiveMs = nowMs;
                fresh.lastCut = n.cutDb;
                fresh.shown = n;
                setLabelText (fresh);
                fresh.valueMs = nowMs;
                labels.push_back (fresh);
                l = &labels.back();
                relayout = true;
            }
            // Active: ringing, or being deepened (a re-trigger or the closed loop).
            if (n.ringing || n.cutDb > l->lastCut + 0.25f) l->lastActiveMs = nowMs;
            l->lastCut = n.cutDb;
            const auto hovered = n.id == hoverNotch || n.id == tableHoverNotch;
            if (hovered) l->hoverUntilMs = nowMs + hoverLingerMs;
            const auto want = n.id == selectedNotch || hovered || nowMs < l->hoverUntilMs || nowMs - l->lastActiveMs < activeHoldMs;
            if (want != l->want) { l->want = want; relayout = true; }

            if (nowMs - l->valueMs >= valueRefreshMs || n.fixed != l->shown.fixed || n.hits != l->shown.hits)
            {
                if (n.cutDb != l->shown.cutDb || n.q != l->shown.q || n.freq != l->shown.freq || n.hits != l->shown.hits || n.fixed != l->shown.fixed)
                    ++valueRefreshes;
                l->shown = n;
                setLabelText (*l);
                l->valueMs = nowMs;
            }

            // A notch that moved out from under its label (or far from its dot) is a real collision.
            if (l->placed)
            {
                const auto cx = xForFrequency (n.freq);
                if (l->open ? (cx < l->to.getX() + 6.0f || cx > l->to.getRight() - 6.0f)
                            : std::abs (cx - l->dotTo.getCentreX()) > 2.5f * pinRadius)
                    relayout = true;
            }
            else
                relayout = true;
        }

        // Numbers: 1.. in frequency order, as in the table.
        std::array<std::pair<float, int>, dsp::maxNotches> order {};
        int count = 0;
        for (const auto& n : notches)
            if (n.active) order[(size_t) count++] = { n.freq, n.id };
        std::sort (order.begin(), order.begin() + count);
        for (int k = 0; k < count; ++k)
            if (auto* l = labelFor (order[(size_t) k].second)) l->number = k + 1;
        return relayout;
    }

    void FeedbackGraph::layoutMarkers (double nowMs, bool snap)
    {
        if (plot.isEmpty()) return;
        ++layoutCount;

        const auto laneTop = plot.getY() + 4.0f;
        const auto laneBottom = baselineY - 2.0f * pinRadius - 8.0f;
        const auto rows = juce::jlimit (0, 4, (int) std::floor ((laneBottom - laneTop + 4.0f) / (chipHeight + 4.0f)));
        const auto left = plot.getX() + 2.0f, right = plot.getRight() - 2.0f;
        constexpr float gap = 4.0f, openingMargin = 6.0f; // opening needs a little more room (hysteresis)
        const auto laneY = [&] (int r) { return laneTop + (float) r * (chipHeight + 4.0f); };

        std::vector<Label*> live;
        for (auto& l : labels)
            if (! l.gone && notchById (l.id) != nullptr) live.push_back (&l);
        std::sort (live.begin(), live.end(), [this] (const Label* a, const Label* b) { return notchById (a->id)->freq < notchById (b->id)->freq; });

        std::vector<juce::Rectangle<float>> taken;
        const auto fits = [&] (juce::Rectangle<float> r, float margin)
        {
            if (r.getX() < left || r.getRight() > right) return false;
            const auto grown = r.expanded (gap + margin, 0.0f);
            for (const auto& t : taken)
                if (t.intersects (grown)) return false;
            return true;
        };
        const auto place = [&] (Label& l, juce::Rectangle<float> target, int lane)
        {
            taken.push_back (target);
            if (! l.open || snap || ! l.placed)
            {
                l.from = target;                       // appears in place (fades in)
                if (! l.open) l.openChangedMs = snap ? nowMs - fadeMs : nowMs;
            }
            else if (target != l.to)
            {
                l.from = l.drawn;                      // glides from where it is
                l.moveStartMs = nowMs;
            }
            l.to = target;
            l.lane = lane;
            l.open = true;
        };

        std::vector<Label*> pending;
        // 1. Open labels that still want to be open stay exactly where they are when they can.
        for (auto* l : live)
        {
            if (! l->want) continue;
            const auto cx = xForFrequency (notchById (l->id)->freq);
            if (! snap && l->open && l->placed && l->lane >= 0 && l->lane < rows
                && cx >= l->to.getX() + 6.0f && cx <= l->to.getRight() - 6.0f && fits (l->to, 0.0f))
                place (*l, l->to, l->lane);
            else
                pending.push_back (l);
        }
        // 2. The rest in frequency order: their old lane first, then top down; sliding right of the
        //    marker a little (never so far the label points elsewhere).
        for (auto* l : pending)
        {
            const auto cx = xForFrequency (notchById (l->id)->freq);
            const auto wanted = juce::jlimit (left, std::max (left, right - chipWidth), cx - 0.5f * chipWidth);
            const auto margin = l->open && ! snap ? 0.0f : openingMargin;
            auto done = false;
            for (int attempt = -1; attempt < rows && ! done; ++attempt)
            {
                const auto r = attempt < 0 ? l->lane : attempt;
                if (r < 0 || r >= rows || (attempt >= 0 && r == l->lane)) continue;
                for (auto x = wanted; x - wanted <= 0.6f * chipWidth && x + chipWidth <= right; )
                {
                    const juce::Rectangle<float> rect (x, laneY (r), chipWidth, chipHeight);
                    if (fits (rect, margin) && cx >= x + 6.0f && cx <= x + chipWidth - 6.0f) { place (*l, rect, r); done = true; break; }
                    auto next = x + 1.0f;
                    for (const auto& t : taken)
                        if (t.getY() == rect.getY() && t.getRight() + gap + margin > x && t.getX() < x + chipWidth + gap + margin)
                            next = std::max (next, t.getRight() + gap + margin);
                    x = next;
                }
            }
            if (! done && l->open)
            {
                l->open = false;                       // no room: closes to a dot (fades out in place)
                l->openChangedMs = snap ? nowMs - fadeMs : nowMs;
            }
        }
        for (auto* l : live)
            if (! l->want && l->open)
            {
                l->open = false;
                l->openChangedMs = snap ? nowMs - fadeMs : nowMs;
            }

        // 3. Dots for closed labels: one row just above the baseline, in frequency order, nudged
        //    apart so none overlap (a 1-D sweep right, then back left from the edge).
        std::vector<Label*> dots;
        for (auto* l : live) if (! l->open) dots.push_back (l);
        const auto d = 2.0f * pinRadius, dotGap = 2.0f;
        std::vector<float> xs (dots.size());
        for (size_t i = 0; i < dots.size(); ++i)
        {
            const auto ideal = juce::jlimit (left, right - d, xForFrequency (notchById (dots[i]->id)->freq) - pinRadius);
            xs[i] = i == 0 ? ideal : std::max (ideal, xs[i - 1] + d + dotGap);
        }
        for (size_t i = dots.size(); i-- > 0;)
            xs[i] = std::min (xs[i], i + 1 < dots.size() ? xs[i + 1] - d - dotGap : right - d);
        for (size_t i = 0; i < dots.size(); ++i)
        {
            auto& l = *dots[i];
            const juce::Rectangle<float> rect (xs[i], baselineY - d - 4.0f, d, d);
            if (snap || ! l.placed || l.dotDrawn.isEmpty()) l.dotFrom = rect;
            else if (rect != l.dotTo) { l.dotFrom = l.dotDrawn; l.moveStartMs = nowMs; }
            l.dotTo = rect;
        }
        for (auto* l : live)
        {
            if (l->open && l->dotTo.isEmpty())
                l->dotTo = l->dotFrom = juce::Rectangle<float> (d, d).withCentre ({ xForFrequency (notchById (l->id)->freq), baselineY - pinRadius - 4.0f });
            l->placed = true;
        }
        if (snap)
            for (auto* l : live) l->moveStartMs = nowMs - moveMs;
    }

    void FeedbackGraph::setLabelText (Label& l)
    {
        const auto& v = l.shown;
        l.line1 = params::formatFrequency (v.freq) + "  " + params::noteName (v.freq);
        l.line2 = cutText (v.cutDb) + "  Q " + params::formatValue (v.q, 0);
        l.line3 = juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (v.hits) + "  " + timeText (v.detectedMs)
                + (v.fixed ? "  FIX" : "  FLT");
    }

    bool FeedbackGraph::animateLabels (double nowMs)
    {
        auto moving = false;
        for (auto& l : labels)
        {
            const auto move = easeOut (nowMs - l.moveStartMs, moveMs);
            l.drawn = lerp (l.from, l.to, move);
            l.dotDrawn = lerp (l.dotFrom, l.dotTo, move);
            const auto fade = easeOut (nowMs - l.openChangedMs, fadeMs);
            l.labelAlpha = l.open ? fade : (l.labelAlpha > 0.0f ? std::min (l.labelAlpha, 1.0f - fade) : 0.0f);
            const auto born = easeOut (nowMs - l.bornMs, fadeMs);
            l.markerAlpha = l.gone ? 1.0f - easeOut (nowMs - l.goneMs, fadeMs) : born;
            l.dotAlpha = (1.0f - l.labelAlpha) * l.markerAlpha;
            if (move < 1.0f || fade < 1.0f || born < 1.0f || l.gone) moving = true;
        }
        animating = moving;
        return moving;
    }

    void FeedbackGraph::buildMarkers()
    {
        markers.clear();
        for (const auto& l : labels)
        {
            const auto* n = notchById (l.id);
            if (l.gone || n == nullptr) continue;
            Marker m;
            m.id = l.id;
            m.number = l.number;
            m.compact = ! l.open;
            m.lane = l.open ? l.lane : -1;
            m.chip = l.open ? l.drawn : l.dotDrawn;
            m.target = l.open ? l.to : l.dotTo;
            const auto* drawn = drawnById (l.id);
            const auto& h = drawn != nullptr ? *drawn : *n;
            m.handle = { xForFrequency (h.freq), yForCut (h.cutDb) };
            if (l.open)
            {
                auto buttons = m.chip.withTrimmedLeft (chipWidth - 22.0f).reduced (2.0f, 3.0f);
                m.lockButton = buttons.removeFromTop (buttons.getHeight() * 0.5f).reduced (0.0f, 1.0f);
                m.clearButton = buttons.reduced (0.0f, 1.0f);
            }
            markers.push_back (m);
        }
        std::sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.number < b.number; });
    }

    //==============================================================================
    void FeedbackGraph::smoothDisplay (const float* in, float* out, float binHz) noexcept
    {
        constexpr int n = dsp::DisplayFrame::numPoints, maxRadius = 12;
        const auto spacingOct = std::log2 (dsp::DisplayFrame::maxHz / dsp::DisplayFrame::minHz) / (float) (n - 1);
        for (int p = 0; p < n; ++p)
        {
            const auto f = dsp::DisplayFrame::pointFrequency (p);
            const auto sigmaOct = binHz > 0.0f ? std::min (1.0f / 6.0f, 0.5f * std::log2 (1.0f + binHz / f)) : 0.0f;
            const auto sigma = sigmaOct / spacingOct;
            if (sigma < 0.33f)
            {
                out[p] = in[p];
                continue;
            }
            const auto radius = std::min (maxRadius, (int) std::ceil (3.0f * sigma));
            float sum = 0.0f, weights = 0.0f;
            for (int j = -radius; j <= radius; ++j)
            {
                const auto w = std::exp (-0.5f * (float) (j * j) / (sigma * sigma));
                sum += w * in[juce::jlimit (0, n - 1, p + j)];
                weights += w;
            }
            out[p] = sum / weights;
        }
    }

    void FeedbackGraph::addSmoothCurve (juce::Path& path, const float* xs, const float* ys, int n, bool startNewSubPath)
    {
        if (n <= 0) return;
        if (startNewSubPath) path.startNewSubPath (xs[0], ys[0]);
        else                 path.lineTo (xs[0], ys[0]);
        const auto secant = [&] (int i) { const auto h = xs[i + 1] - xs[i]; return h > 1.0e-6f ? (ys[i + 1] - ys[i]) / h : 0.0f; };
        const auto tangent = [&] (int i)
        {
            if (n < 2) return 0.0f;
            if (i == 0) return secant (0);
            if (i == n - 1) return secant (n - 2);
            const auto a = secant (i - 1), b = secant (i);
            return a * b <= 0.0f ? 0.0f : 2.0f * a * b / (a + b);
        };
        auto m0 = tangent (0);
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto m1 = tangent (i + 1);
            const auto h = (xs[i + 1] - xs[i]) / 3.0f;
            path.cubicTo (xs[i] + h, ys[i] + m0 * h, xs[i + 1] - h, ys[i + 1] - m1 * h, xs[i + 1], ys[i + 1]);
            m0 = m1;
        }
    }

    void FeedbackGraph::rebuildSpectrumPaths()
    {
        inputPath.clear();
        outputPath.clear();
        if (! hasFrame || plot.isEmpty()) return;

        constexpr int n = dsp::DisplayFrame::numPoints;
        smoothDisplay (frame.input.data(), drawnInput.data(), frame.binHz);
        smoothDisplay (frame.output.data(), drawnOutput.data(), frame.binHz);
        for (int i = 0; i < n; ++i)
        {
            pointX[(size_t) i] = xForFrequency (dsp::DisplayFrame::pointFrequency (i));
            inputY[(size_t) i] = yForLevel (drawnInput[(size_t) i]);
            outputY[(size_t) i] = yForLevel (drawnOutput[(size_t) i]);
        }
        inputPath.startNewSubPath (plot.getX(), plot.getBottom());
        addSmoothCurve (inputPath, pointX.data(), inputY.data(), n, false);
        inputPath.lineTo (plot.getRight(), plot.getBottom());
        inputPath.closeSubPath();
        addSmoothCurve (outputPath, pointX.data(), outputY.data(), n, true);
    }

    void FeedbackGraph::rebuildCurve()
    {
        // Both paths are cleared and refilled in place (no reallocation once grown).
        curveFill.clear();
        curveEdge.clear();
        if (plot.isEmpty()) return;
        const auto steps = std::max (2, (int) plot.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const auto y = yForCut (-dsp::FeedbackEngine::bankResponseDb (drawnNotches, frequencyForX (x)));
            if (i == 0) { curveEdge.startNewSubPath (x, y); curveFill.startNewSubPath (x, y); }
            else        { curveEdge.lineTo (x, y); curveFill.lineTo (x, y); }
        }
        curveFill.lineTo (plot.getRight(), baselineY);
        curveFill.lineTo (plot.getX(), baselineY);
        curveFill.closeSubPath();
    }

    void FeedbackGraph::drawBackground()
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt ((float) getWidth() * scale)),
                                  std::max (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        g.fillAll (Palette::panel);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (plot, 4.0f);
        g.setFont (juce::FontOptions (11.0f));

        for (float f : { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f })
        {
            const auto x = xForFrequency (f);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getY() - 17.0f, 40.0f, 14.0f).toNearestInt(), juce::Justification::centred);
        }

        // Cut scale (right edge) below the 0 dB baseline.
        for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f })
        {
            const auto y = yForCut (db);
            g.setColour (db == 0.0f ? Palette::divider.brighter (0.25f) : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (Palette::textMuted);
            const auto text = db == 0.0f ? juce::String ("0") : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String ((int) db);
            g.drawText (text, juce::Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 28.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft);
        }
        {
            // Full notches (Max cut at infinity) sit on their own line at the bottom.
            const auto y = yForCut (dsp::FeedbackEngine::fullCutDb);
            const auto dashLengths = std::array<float, 2> { 2.0f, 4.0f };
            g.setColour (Palette::grid);
            g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashLengths.data(), (int) dashLengths.size(), 1.0f);
            g.setColour (Palette::textMuted);
            g.drawText (params::formatCut (dsp::FeedbackEngine::fullCutDb, true).upToFirstOccurrenceOf (" ", false, false),
                        juce::Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 28.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    //==============================================================================
    void FeedbackGraph::paint (juce::Graphics& g)
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        if (! background.isValid() || std::abs (scale - backgroundScale) > 1.0e-4f)
            drawBackground();
        g.drawImage (background, getLocalBounds().toFloat());

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());
            if (hasFrame)
            {
                g.setColour (Palette::spectrum.withAlpha (0.10f));
                g.fillPath (inputPath);
                g.setColour (Palette::spectrum.withAlpha (0.28f));
                g.strokePath (inputPath, juce::PathStrokeType (1.0f));
                g.setColour (Palette::textPrimary.withAlpha (0.45f));
                g.strokePath (outputPath, juce::PathStrokeType (1.1f));
            }
            g.setColour (Palette::dynamic.withAlpha (0.24f));
            g.fillPath (curveFill);
            g.setColour (Palette::dynamic.withAlpha (0.9f));
            g.strokePath (curveEdge, juce::PathStrokeType (1.4f));
        }

        const auto dash = std::array<float, 2> { 3.0f, 3.0f };
        for (const auto& l : labels)
        {
            if (l.markerAlpha <= 0.001f) continue;
            const auto* live = notchById (l.id);
            const auto& n = live != nullptr ? *live : l.shown;   // a cleared notch fades out as it was
            const auto colour = n.fixed ? Palette::accent : Palette::dynamic;
            const auto selected = l.id == selectedNotch, hovered = l.id == hoverNotch || l.id == tableHoverNotch;
            const auto* drawn = drawnById (l.id);
            const auto& hn = drawn != nullptr ? *drawn : n;
            const juce::Point<float> handle { xForFrequency (hn.freq), yForCut (hn.cutDb) };
            const auto alpha = l.markerAlpha;

            // Leader line from the label (or dot) down to the handle at the notch's depth.
            const auto drawLeader = [&] (juce::Rectangle<float> from, float a)
            {
                if (a <= 0.001f) return;
                auto& line = scratchLine;
                line.clear();
                const auto startX = juce::jlimit (from.getX() + 4.0f, from.getRight() - 4.0f, handle.x);
                line.startNewSubPath (startX, from.getBottom());
                if (std::abs (startX - handle.x) > 0.5f) line.lineTo (handle.x, std::min (handle.y, from.getBottom() + 6.0f));
                line.lineTo (handle);
                scratchDashed.clear();
                juce::PathStrokeType (1.0f).createDashedStroke (scratchDashed, line, dash.data(), (int) dash.size());
                g.setColour (colour.withAlpha ((selected || hovered ? 0.9f : 0.5f) * a));
                g.fillPath (scratchDashed);
            };
            drawLeader (l.drawn, l.labelAlpha * alpha);
            drawLeader (l.dotDrawn, l.dotAlpha);

            auto& diamond = scratchDiamond;
            diamond.clear();
            const auto r = selected || hovered ? 5.5f : 4.5f;
            diamond.addQuadrilateral (handle.x, handle.y - r, handle.x + r, handle.y, handle.x, handle.y + r, handle.x - r, handle.y);
            g.setColour (colour.withAlpha (alpha));
            g.fillPath (diamond);

            if (l.dotAlpha > 0.001f && ! l.dotDrawn.isEmpty())
            {
                g.setColour (colour.withAlpha ((selected || hovered ? 0.95f : 0.7f) * l.dotAlpha));
                g.fillEllipse (l.dotDrawn);
                g.setColour (Palette::background.withAlpha (l.dotAlpha));
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (juce::String (l.number), l.dotDrawn.toNearestInt(), juce::Justification::centred);
            }

            const auto la = l.labelAlpha * alpha;
            if (la <= 0.001f) continue;
            const auto chip = l.drawn;
            const auto& v = l.shown; // calm numbers
            g.setColour (Palette::panelRaised.withAlpha (0.94f * la));
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (colour.withAlpha ((selected ? 1.0f : (n.ringing ? 0.9f : 0.55f)) * la));
            g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, selected ? 1.8f : 1.0f);
            g.fillRect (chip.getX() + 1.0f, chip.getY() + 5.0f, 2.5f, chip.getHeight() - 10.0f);

            auto text = chip.withTrimmedRight (22.0f).reduced (7.0f, 3.0f).withTrimmedLeft (1.0f);
            const auto lineHeight = text.getHeight() / 3.0f;
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (Palette::textPrimary.withAlpha (la));
            juce::ignoreUnused (v);
            g.drawText (l.line1, text.removeFromTop (lineHeight).toNearestInt(), juce::Justification::centredLeft, false);
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (colour.brighter (0.2f).withAlpha (la));
            g.drawText (l.line2, text.removeFromTop (lineHeight).toNearestInt(), juce::Justification::centredLeft, false);
            g.setColour (Palette::textMuted.withAlpha (la));
            g.drawText (l.line3, text.toNearestInt(), juce::Justification::centredLeft, false);

            // Lock (fixed / floating) and clear buttons.
            auto buttons = chip.withTrimmedLeft (chipWidth - 22.0f).reduced (2.0f, 3.0f);
            const auto lockButton = buttons.removeFromTop (buttons.getHeight() * 0.5f).reduced (0.0f, 1.0f);
            const auto clearButton = buttons.reduced (0.0f, 1.0f);
            const auto overLock = hovering && lockButton.contains (hoverPoint);
            const auto overClear = hovering && clearButton.contains (hoverPoint);
            if (overLock || overClear)
            {
                g.setColour (Palette::divider.brighter (0.2f).withAlpha (la));
                g.fillRoundedRectangle (overLock ? lockButton : clearButton, 3.0f);
            }
            g.setColour ((n.fixed ? Palette::accent : Palette::textMuted).withAlpha (la));
            const auto lockBox = lockButton.withSizeKeepingCentre (11.0f, 12.0f);
            if (n.fixed) g.fillPath (padlock (lockBox, true));
            else         g.strokePath (padlock (lockBox, false), juce::PathStrokeType (1.2f));
            g.setColour ((overClear ? Palette::textPrimary : Palette::textMuted).withAlpha (la));
            const auto x = clearButton.withSizeKeepingCentre (8.0f, 8.0f);
            g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
            g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
        }


        if (hovering)
        {
            const auto text = readoutText();
            if (text.isNotEmpty())
            {
                const auto font = juce::Font (juce::FontOptions (11.5f));
                const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
                const auto chip = juce::Rectangle<float> (plot.getRight() - w - 6.0f, plot.getBottom() - 24.0f, w, 18.0f);
                g.setColour (Palette::panelRaised.withAlpha (0.92f));
                g.fillRoundedRectangle (chip, 4.0f);
                g.setFont (font);
                g.setColour (Palette::textPrimary);
                g.drawText (text, chip.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    juce::String FeedbackGraph::readoutText() const
    {
        if (const auto* n = notchById (hoverNotch))
        {
            const auto* m = markerFor (hoverNotch);
            return "#" + juce::String (m != nullptr ? m->number : 0) + "  " + params::formatValue (n->freq, 1) + " Hz  "
                 + params::noteWithCents (n->freq) + "  cut " + params::formatCut (n->cutDb, false) + " (most "
                 + params::formatCut (n->neededDb, false) + ")  Q " + params::formatValue (n->q, 1) + "  "
                 + juce::String (n->hits) + (n->hits == 1 ? " hit  " : " hits  ") + timeText (n->detectedMs)
                 + (n->fixed ? "  fixed" : "  floating") + (n->ringing ? "  ringing" : "");
        }
        if (! plot.contains (hoverPoint)) return {};
        const auto f = frequencyForX (hoverPoint.x);
        auto text = params::formatFrequency (f) + "  " + params::noteWithCents (f);
        if (hasFrame)
        {
            const auto t = std::log (f / dsp::DisplayFrame::minHz) / logSpan * (float) (dsp::DisplayFrame::numPoints - 1);
            const auto i = juce::jlimit (0, dsp::DisplayFrame::numPoints - 1, juce::roundToInt (t));
            text << "  " << params::formatValue (drawnOutput[(size_t) i], 0) << " dBFS";
        }
        return text;
    }

    //==============================================================================
    int FeedbackGraph::notchAt (juce::Point<float> position) const
    {
        for (auto it = markers.rbegin(); it != markers.rend(); ++it)
            if (it->chip.expanded (it->compact ? 3.0f : 0.0f).contains (position))
                return it->id;
        for (const auto& m : markers)
            if (m.handle.getDistanceFrom (position) <= 7.0f)
                return m.id;
        return 0;
    }

    void FeedbackGraph::mouseMove (const juce::MouseEvent& e)
    {
        hovering = true;
        hoverPoint = e.position;
        hoverNotch = notchAt (e.position);
        auto overButton = false;
        for (const auto& m : markers)
            if (! m.compact && (m.lockButton.contains (e.position) || m.clearButton.contains (e.position))) overButton = true;
        setMouseCursor (overButton ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void FeedbackGraph::mouseExit (const juce::MouseEvent&)
    {
        hovering = false;
        hoverNotch = 0;
        repaint();
    }

    void FeedbackGraph::mouseDown (const juce::MouseEvent& e)
    {
        const auto id = notchAt (e.position);
        if (id == 0) return;
        const auto* m = markerFor (id);
        const auto* n = notchById (id);
        if (m == nullptr || n == nullptr) return;

        if (e.mods.isPopupMenu() || (! m->compact && m->clearButton.contains (e.position)))
        {
            processor.clearNotch (id);
            if (selectedNotch == id) selectedNotch = 0;
            hoverNotch = 0;
        }
        else if (! m->compact && m->lockButton.contains (e.position))
            processor.setNotchFixed (id, ! n->fixed);
        else
        {
            selectedNotch = id;
            if (onNotchSelected) onNotchSelected (id);
        }
        refreshNotches();
    }
}
