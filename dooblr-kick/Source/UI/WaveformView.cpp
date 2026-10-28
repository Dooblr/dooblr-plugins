#include "WaveformView.h"

#include "Theme.h"

#include <cmath>

namespace dooblrkick::ui
{
    namespace
    {
        constexpr juce::uint32 minRequestIntervalMs = 30; // debounce: at most ~33 renders/s
    }

    WaveformView::WaveformView (JobFactory factoryToUse, ChangeCounter counterToUse, double renderSampleRate)
        : juce::Thread ("dooblr-kick preview"),
          factory (std::move (factoryToUse)), counter (std::move (counterToUse)), sampleRate (renderSampleRate)
    {
        setComponentID ("waveform");
        setTooltip ("Rendered kick at the Key. Click to audition.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setOpaque (true);
        requestedCount = shownCount = counter();
        upper.reserve ((size_t) maxColumns);
        lower.reserve ((size_t) maxColumns);
        bandPath.preallocateSpace (maxColumns * 2 * 5 + 16);
        edgePath.preallocateSpace (maxColumns * 2 * 5 + 16);
        startThread (juce::Thread::Priority::low);
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { poll(); });
    }

    WaveformView::~WaveformView()
    {
        vblank.reset();
        cancelPendingUpdate();
        signalThreadShouldExit();
        wake.signal();
        stopThread (10000);
    }

    void WaveformView::setStatus (const juce::String& text)
    {
        status = text;
        statusTime = juce::Time::getMillisecondCounter();
        repaint();
    }

    void WaveformView::refreshNow()
    {
        const auto count = counter();
        requestedCount = count;

        if (auto job = factory())
            apply (analyse (job(), sampleRate, count));
    }

    bool WaveformView::waitForBackgroundRender (int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        for (;;)
        {
            {
                const std::lock_guard<std::mutex> guard (lock);

                if (! busy && pendingJob == nullptr)
                    break;
            }

            if (juce::Time::getMillisecondCounter() > deadline)
                return false;

            juce::Thread::sleep (1);
        }

        handleAsyncUpdate();
        return true;
    }

    //==============================================================================
    void WaveformView::poll()
    {
        const auto now = juce::Time::getMillisecondCounter();

        if (status.isNotEmpty() && now - statusTime > 6000)
        {
            status.clear();
            repaint (getLocalBounds().removeFromBottom (22));
        }

        const auto count = counter();

        if (count == requestedCount || now - lastRequestTime < minRequestIntervalMs)
            return;

        auto job = factory();

        if (job == nullptr)
            return;

        requestedCount = count;
        lastRequestTime = now;

        {
            const std::lock_guard<std::mutex> guard (lock);
            pendingJob = std::move (job); // replaces any job the worker has not started yet
            pendingCount = count;
        }

        wake.signal();
    }

    void WaveformView::run()
    {
        while (! threadShouldExit())
        {
            RenderJob job;
            juce::uint32 count = 0;

            {
                const std::lock_guard<std::mutex> guard (lock);
                job = std::move (pendingJob);
                pendingJob = nullptr;
                count = pendingCount;
                busy = job != nullptr;
            }

            if (job == nullptr)
            {
                wake.wait (100);
                continue;
            }

            auto result = analyse (job(), sampleRate, count);
            job = nullptr; // releases what the job captured (e.g. a sample-lifetime guard)

            {
                const std::lock_guard<std::mutex> guard (lock);
                readyResult = std::move (result);
                ready = true;
                busy = false;
            }

            triggerAsyncUpdate();
        }
    }

    void WaveformView::handleAsyncUpdate()
    {
        Result result;

        {
            const std::lock_guard<std::mutex> guard (lock);

            if (! ready)
                return;

            result = std::move (readyResult);
            ready = false;
        }

        ++backgroundRenders;
        apply (std::move (result));
    }

    //==============================================================================
    WaveformView::Result WaveformView::analyse (const juce::AudioBuffer<float>& rendered, double sr, juce::uint32 count)
    {
        Result r;
        r.count = count;

        // Trim the trailing silence (below -80 dBFS) for display.
        auto last = 0;
        auto peak = 0.0f;

        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
        {
            const auto* data = rendered.getReadPointer (ch);

            for (int i = 0; i < rendered.getNumSamples(); ++i)
            {
                const auto a = std::abs (data[i]);
                peak = juce::jmax (peak, a);

                if (a > 1.0e-4f)
                    last = juce::jmax (last, i);
            }
        }

        r.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        r.lengthMs = (float) (1000.0 * (last + 1) / sr);

        const auto n = juce::jmax (1, last + 1);

        if (rendered.getNumSamples() <= 1 || rendered.getNumChannels() == 0)
            return r;

        const auto* data = rendered.getReadPointer (0);
        const auto columns = juce::jmin (numPeaks, n);
        r.peaks.reserve ((size_t) columns);

        for (int x = 0; x < columns; ++x)
        {
            const auto from = (int) ((juce::int64) n * x / columns);
            const auto to = juce::jmax (from + 1, (int) ((juce::int64) n * (x + 1) / columns));
            auto lo = 1.0f, hi = -1.0f;

            for (int i = from; i < juce::jmin (to, n); ++i)
            {
                lo = juce::jmin (lo, data[i]);
                hi = juce::jmax (hi, data[i]);
            }

            r.peaks.emplace_back (lo, hi);
        }

        return r;
    }

    void WaveformView::apply (Result&& result)
    {
        peaks = std::move (result.peaks);
        peakDb = result.peakDb;
        lengthMs = result.lengthMs;
        shownCount = result.count;
        infoText = juce::String (juce::roundToInt (lengthMs)) + " ms   peak " + juce::String (peakDb, 1) + " dBFS";
        graphDirty = true;
        repaint();
    }

    namespace
    {
        // Quadratic segments through the midpoints of consecutive points: smooth (C1) and never
        // outside the hull of the column points, so peaks are not exaggerated.
        void addEdge (juce::Path& path, const std::vector<juce::Point<float>>& points, bool reverse, bool startNewSubPath)
        {
            const auto n = (int) points.size();

            if (n == 0)
                return;

            const auto at = [&] (int i) { return points[(size_t) (reverse ? n - 1 - i : i)]; };

            if (startNewSubPath)
                path.startNewSubPath (at (0));
            else
                path.lineTo (at (0));

            // One vertex per physical pixel column is already finer than any curve fit could
            // show (a quadratic between column midpoints differs by well under a pixel), and
            // straight segments keep the renderer from subdividing curves on every paint.
            for (int i = 1; i < n; ++i)
                path.lineTo (at (i));
        }
    }

    void WaveformView::rebuildPath (float scale)
    {
        bandPath.clear();
        edgePath.clear();
        upper.clear();
        lower.clear();
        graphDirty = false;
        graphScale = scale;
        graphColumns = 0;
        ++graphRebuilds;

        const auto wave = getLocalBounds().toFloat().reduced (8.0f, 22.0f);

        if (peaks.empty() || wave.getWidth() < 1.0f || wave.getHeight() < 1.0f)
            return;

        // One min/max pair per physical pixel column, at the column centre (sub-pixel x).
        const auto columns = juce::jlimit (2, maxColumns, (int) std::ceil (wave.getWidth() * scale));
        const auto centreY = wave.getCentreY();
        const auto halfHeight = wave.getHeight() * 0.5f;
        const auto columnWidth = wave.getWidth() / (float) columns;
        const auto minThickness = 0.5f / scale; // half a physical pixel, so the band never vanishes
        const auto numPeakColumns = (int) peaks.size();

        for (int x = 0; x < columns; ++x)
        {
            const auto from = juce::jmin (numPeakColumns - 1, (int) ((juce::int64) numPeakColumns * x / columns));
            const auto to = juce::jmax (from + 1, (int) ((juce::int64) numPeakColumns * (x + 1) / columns));
            auto lo = 1.0f, hi = -1.0f;

            for (int i = from; i < juce::jmin (to, numPeakColumns); ++i)
            {
                lo = juce::jmin (lo, peaks[(size_t) i].first);
                hi = juce::jmax (hi, peaks[(size_t) i].second);
            }

            hi = juce::jlimit (-1.0f, 1.0f, hi);
            lo = juce::jlimit (-1.0f, hi, lo);
            const auto px = wave.getX() + ((float) x + 0.5f) * columnWidth;
            auto top = centreY - hi * halfHeight, bottom = centreY - lo * halfHeight;

            if (bottom - top < minThickness)
            {
                const auto mid = (top + bottom) * 0.5f;
                top = mid - minThickness * 0.5f;
                bottom = mid + minThickness * 0.5f;
            }

            upper.emplace_back (px, top);
            lower.emplace_back (px, bottom);
        }

        graphColumns = columns;

        // Filled band: the upper edge left to right, then the lower edge back.
        addEdge (bandPath, upper, false, true);
        addEdge (bandPath, lower, true, false);
        bandPath.closeSubPath();

        // The two edges, stroked on top of the fill.
        addEdge (edgePath, upper, false, true);
        addEdge (edgePath, lower, false, true);
    }

    void WaveformView::resized()
    {
        graphDirty = true;
    }

    void WaveformView::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick != nullptr)
            onClick();
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        // Opaque: the editor behind it is Palette::background as well.
        g.fillAll (Palette::background);

        const auto area = getLocalBounds().toFloat();
        const auto wave = area.reduced (8.0f, 22.0f);

        // The pixel scale this context really draws at (2 on Retina, or a snapshot's scale).
        const auto scale = juce::jlimit (1.0f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

        if (graphDirty || ! juce::approximatelyEqual (scale, graphScale))
            rebuildPath (scale); // reuses the reserved storage: no allocation

        // Centre line: one physical pixel, at the true (unrounded) centre.
        g.setColour (Palette::grid);
        g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getCentreY() - 0.5f / scale, wave.getWidth(), 1.0f / scale));

        g.setColour (Palette::accent.withAlpha (0.55f));
        g.fillPath (bandPath);
        g.setColour (Palette::accent.withAlpha (0.95f));
        // On macOS the CoreGraphics context strokes natively (no stroked outline is built here).
        g.strokePath (edgePath, juce::PathStrokeType (1.25f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (Palette::textMuted);
        const auto info = area.reduced (8.0f, 4.0f).removeFromTop (14.0f);
        g.drawText ("OUTPUT  (click to play)", info, juce::Justification::centredLeft);
        g.drawText (infoText, info, juce::Justification::centredRight);

        if (status.isNotEmpty())
        {
            g.setColour (Palette::textPrimary);
            g.drawFittedText (status, area.reduced (8.0f, 4.0f).removeFromBottom (14.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        }
    }
}
