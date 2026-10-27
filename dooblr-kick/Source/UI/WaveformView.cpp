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
        rebuildPath();
        repaint();
    }

    void WaveformView::rebuildPath()
    {
        wavePath.clear();
        const auto wave = getLocalBounds().toFloat().reduced (8.0f, 22.0f);

        if (peaks.empty() || wave.getWidth() < 1.0f)
            return;

        // One rectangle per pixel column, from the cached peaks.
        const auto columns = juce::jmax (1, (int) wave.getWidth());
        const auto centreY = wave.getCentreY();
        const auto halfHeight = wave.getHeight() * 0.5f;
        const auto numPeakColumns = (int) peaks.size();
        wavePath.preallocateSpace (columns * 5 * 3);

        for (int x = 0; x < columns; ++x)
        {
            const auto from = (int) ((juce::int64) numPeakColumns * x / columns);
            const auto to = juce::jmax (from + 1, (int) ((juce::int64) numPeakColumns * (x + 1) / columns));
            auto lo = 1.0f, hi = -1.0f;

            for (int i = from; i < juce::jmin (to, numPeakColumns); ++i)
            {
                lo = juce::jmin (lo, peaks[(size_t) i].first);
                hi = juce::jmax (hi, peaks[(size_t) i].second);
            }

            hi = juce::jlimit (-1.0f, 1.0f, hi);
            lo = juce::jlimit (-1.0f, 1.0f, lo);
            wavePath.addRectangle (wave.getX() + (float) x, centreY - hi * halfHeight, 1.0f, juce::jmax (1.0f, (hi - lo) * halfHeight));
        }
    }

    void WaveformView::resized()
    {
        rebuildPath();
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

        g.setColour (Palette::grid);
        g.drawHorizontalLine (juce::roundToInt (wave.getCentreY()), wave.getX(), wave.getRight());

        g.setColour (Palette::accent.withAlpha (0.85f));
        g.fillPath (wavePath);

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
