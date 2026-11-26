#include "ReverbDisplay.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "Theme.h"

namespace dooblrreverb::ui
{
    using namespace params;

    namespace
    {
        constexpr float logSpan = 6.907755279f; // ln (20000 / 20)
        constexpr float spectrumTopDb = 0.0f, spectrumFloorDb = -90.0f, tiltDbPerOctave = 3.0f;

        // Ballistics (90 % times, as dooblr-soothe's display): quick rise, slower fall.
        constexpr double riseSeconds = 0.06, fallSeconds = 0.5;
        // No audio for this long (transport stopped): the spectrum eases down to the floor.
        constexpr double quietSeconds = 0.2;

        double coefficientFor (double dt, double t90)
        {
            return 1.0 - std::exp (-dt * 2.302585093 / t90);
        }

        juce::String signedDb (double db)
        {
            return (db >= 0.05 ? "+" : "") + formatValue (db, 1) + " dB";
        }

        juce::String percent (double p)
        {
            return formatValue (p, p < 100.0 ? 1 : 0) + " %";
        }
    }

    ReverbDisplay::ReverbDisplay (DooblrReverbProcessor& p)
        : processor (p), state (p.getState())
    {
        setOpaque (true);
        setComponentID ("display");

        for (auto* b : { &decayButton, &eqButton })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
            addAndMakeVisible (*b);
        }
        decayButton.setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
        eqButton.setColour (juce::TextButton::buttonOnColourId, Palette::dynamic.withAlpha (0.45f));
        decayButton.setTooltip ("Double-click the display to add a Decay Rate EQ band");
        eqButton.setTooltip ("Double-click the display to add a Post EQ band");
        decayButton.onClick = [this] { setEditLayer (Layer::decay); };
        eqButton.onClick = [this] { setEditLayer (Layer::eq); };
        decayButton.setToggleState (true, juce::dontSendNotification);

        for (size_t i = 0; i < window.size(); ++i)
            window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
        for (int i = 0; i < numPoints; ++i)
            pointFreq[(size_t) i] = minFreq * std::exp (logSpan * (float) i / (float) (numPoints - 1));
        for (auto* path : { &decayPath, &decayFill, &effectivePath, &eqPath, &eqFill, &postPath, &postFill, &prePath })
            path->preallocateSpace (4 * 1200);
        for (auto* a : { &post, &pre })
        {
            a->queue.assign ((size_t) queueSize, 0.0f);
            a->reset();
        }

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { poll(); });
    }

    ReverbDisplay::~ReverbDisplay() = default;

    void ReverbDisplay::setEditLayer (Layer layer)
    {
        editLayer = layer;
        decayButton.setToggleState (layer == Layer::decay, juce::dontSendNotification);
        eqButton.setToggleState (layer == Layer::eq, juce::dontSendNotification);
        invalidateOverlay();
    }

    void ReverbDisplay::invalidateOverlay()
    {
        overlayDirty = true;
        repaint (overlayArea().toNearestInt());
    }

    void ReverbDisplay::Analyzer::reset()
    {
        queueRead = queueCount = 0;
        history.fill (0.0f);
        writePos = 0;
        target.fill (spectrumFloorDb);
        levels.fill (spectrumFloorDb);
        carry = backlog = burst = quiet = 0.0;
        valid = false;
    }

    void ReverbDisplay::Analyzer::push (const float* samples, int n)
    {
        // Overflow (the display stalled): the oldest samples go.
        for (int i = 0; i < n; ++i)
        {
            if (queueCount == queueSize)
            {
                queueRead = (queueRead + 1) & (queueSize - 1);
                --queueCount;
            }
            queue[(size_t) ((queueRead + queueCount) & (queueSize - 1))] = samples[i];
            ++queueCount;
        }
    }

    //==============================================================================
    bool ReverbDisplay::poll()
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const auto dt = lastTickMs > 0.0 ? (nowMs - lastTickMs) * 0.001 : 1.0 / 60.0;
        lastTickMs = nowMs;
        return tick (dt);
    }

    bool ReverbDisplay::tick (double dt)
    {
        dt = std::clamp (dt, 0.0, 0.1);
        bool curvesChanged = false, spectrumChanged = false;

        const auto now = processor.readSettings();
        const auto changed = ! settingsValid || ! juce::exactlyEqual (now.space, settings.space)
                          || ! juce::exactlyEqual (now.decayRate, settings.decayRate)
                          || ! juce::exactlyEqual (now.brightness, settings.brightness) || now.decay != settings.decay || now.eq != settings.eq;
        settings = now;
        settingsValid = true;
        if (changed)
        {
            rebuildCurves();
            curvesChanged = true;
        }

        // Analyzer. The taps are drained every frame (discarded while off, so no stale audio
        // waits for the analyzer to come back on).
        const auto mode = processor.getAnalyzerMode();
        auto& postTap = processor.getPostTap();
        auto& preTap = processor.getPreTap();
        if (mode != analyzerShown)
        {
            analyzerShown = mode;
            post.reset();
            pre.reset();
            spectrumChanged = true;
        }
        if (mode == 0)
        {
            postTap.discardAll();
            preTap.discardAll();
        }
        else
        {
            const auto postIn = postTap.drain ([this] (const float* d, int n) { post.push (d, n); });
            spectrumChanged = advance (post, dt, postIn) || spectrumChanged;
            if (mode > 1)
            {
                const auto preIn = preTap.drain ([this] (const float* d, int n) { pre.push (d, n); });
                spectrumChanged = advance (pre, dt, preIn) || spectrumChanged;
            }
            else
            {
                preTap.discardAll();
            }
        }

        if (spectrumChanged)
            rebuildSpectrum();
        if (curvesChanged)
            invalidateOverlay();
        else if (spectrumChanged)
            repaint (plot.getSmallestIntegerContainer());
        return curvesChanged || spectrumChanged;
    }

    bool ReverbDisplay::advance (Analyzer& a, double dt, int burst)
    {
        const auto sr = std::max (8000.0, processor.getCurrentSampleRate());

        // Take the samples that belong to this frame, so every frame shows a fresh spectrum
        // whatever the host block size. A slow servo keeps about one host block (the largest
        // recent burst) in reserve: enough that a frame never runs dry between blocks, with the
        // consumption rate nudged by a few percent rather than jumping.
        a.burst = std::max ((double) burst, a.burst * std::exp (-dt / 2.0));
        const auto reserve = std::min (a.burst, sr * 0.1);
        a.backlog += (a.queueCount - a.backlog) * coefficientFor (dt, 0.5);
        auto want = dt * sr + a.carry + (a.backlog - reserve) * std::min (1.0, dt / 0.5);
        if (a.queueCount > reserve + sr * 0.25)   // far behind (a stall, or polled without time): catch up
            want = a.queueCount - reserve;
        want = std::max (0.0, want);
        const auto take = std::min (a.queueCount, (int) want);
        a.carry = take < a.queueCount ? std::clamp (want - take, 0.0, 1.0) : 0.0;

        if (take > 0)
        {
            const auto first = std::min (take, queueSize - a.queueRead);
            writeHistory (a, a.queue.data() + a.queueRead, first);
            if (take > first)
                writeHistory (a, a.queue.data(), take - first);
            a.queueRead = (a.queueRead + take) & (queueSize - 1);
            a.queueCount -= take;
            a.quiet = 0.0;
            analyse (a);
        }
        else
        {
            a.quiet += dt;
            if (a.quiet > quietSeconds)
                a.target.fill (spectrumFloorDb);
        }
        if (! a.valid)
            return false;

        // Time-based ballistics with the real frame time: smooth at any frame rate.
        const auto rise = (float) coefficientFor (dt, riseSeconds), fall = (float) coefficientFor (dt, fallSeconds);
        bool moved = false;
        for (int i = 0; i < numPoints; ++i)
        {
            auto& level = a.levels[(size_t) i];
            const auto t = a.target[(size_t) i];
            const auto step = (t - level) * (t > level ? rise : fall);
            level += step;
            moved = moved || std::abs (step) > 1.0e-3f;
        }
        return moved; // at rest (silence on the floor) nothing repaints
    }

    void ReverbDisplay::writeHistory (Analyzer& a, const float* samples, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            a.history[(size_t) a.writePos] = samples[i];
            a.writePos = (a.writePos + 1) & (fftSize - 1);
        }
    }

    void ReverbDisplay::feedAnalyzerForTest (const float* postSamples, const float* preSamples, int numSamples)
    {
        for (auto [a, samples] : { std::pair { &post, postSamples }, std::pair { &pre, preSamples } })
            if (samples != nullptr)
            {
                writeHistory (*a, samples, numSamples);
                analyse (*a);
                a->levels = a->target;
            }
        rebuildSpectrum();
        repaint();
    }

    void ReverbDisplay::analyse (Analyzer& a)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = a.history[(size_t) ((a.writePos + i) & (fftSize - 1))] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const auto sr = std::max (8000.0, processor.getCurrentSampleRate());
        const auto binHz = (float) (sr / fftSize);
        if (! juce::exactlyEqual (binsForRate, sr))
        {
            binsForRate = sr;
            const auto ratio = std::pow (maxFreq / minFreq, 0.5f / (float) (numPoints - 1));
            for (int p = 0; p < numPoints; ++p)
            {
                const auto f = pointFreq[(size_t) p];
                binLo[(size_t) p] = std::max (1, (int) std::ceil (f / ratio / binHz));
                binHi[(size_t) p] = std::min (fftSize / 2 - 1, (int) std::floor (f * ratio / binHz));
            }
        }

        // Each point is the mean power of its bins (about 1/30 octave wide): steadier than the
        // loudest bin, and honest for noise. Below a bin per point, interpolate.
        const auto norm = 4.0f / (float) fftSize; // Hann (coherent gain 0.5), one-sided
        for (int p = 0; p < numPoints; ++p)
        {
            const auto f = pointFreq[(size_t) p];
            const auto lo = binLo[(size_t) p], hi = binHi[(size_t) p];
            float power = 0.0f;
            if (hi >= lo)
            {
                for (int b = lo; b <= hi; ++b)
                    power += fftData[(size_t) b] * fftData[(size_t) b];
                power /= (float) (hi - lo + 1);
            }
            else
            {
                const auto pos = juce::jlimit (1.0f, (float) (fftSize / 2 - 2), f / binHz);
                const auto i = (int) pos;
                const auto t = pos - (float) i;
                const auto mag = fftData[(size_t) i] + t * (fftData[(size_t) i + 1] - fftData[(size_t) i]);
                power = mag * mag;
            }
            const auto db = 10.0f * std::log10 (std::max (power * norm * norm, 1.0e-20f)) + tiltDbPerOctave * std::log2 (f / 1000.0f);
            a.target[(size_t) p] = std::max (spectrumFloorDb, db);
        }
        if (! a.valid)
        {
            a.levels = a.target;
            a.valid = true;
        }
    }

    //==============================================================================
    float ReverbDisplay::xForFrequency (float f) const noexcept
    {
        const auto t = std::log (juce::jlimit (minFreq, maxFreq, f) / minFreq) / logSpan;
        return plot.getX() + t * plot.getWidth();
    }

    float ReverbDisplay::frequencyForX (float x) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / std::max (1.0f, plot.getWidth()));
        return minFreq * std::exp (t * logSpan);
    }

    float ReverbDisplay::yForDecayLog2 (float l) const noexcept
    {
        const auto inset = handleRadius + 2.0f;
        return juce::jmap (juce::jlimit (-scaleOctaves, scaleOctaves, l), scaleOctaves, -scaleOctaves,
                           plot.getY() + inset, plot.getBottom() - inset);
    }

    float ReverbDisplay::decayLog2ForY (float y) const noexcept
    {
        const auto inset = handleRadius + 2.0f;
        return juce::jlimit (-scaleOctaves, scaleOctaves,
                             juce::jmap (y, plot.getY() + inset, plot.getBottom() - inset, scaleOctaves, -scaleOctaves));
    }

    juce::String ReverbDisplay::freqId (Layer l, int b) const { return l == Layer::decay ? decayFreq (b) : eqFreq (b); }
    juce::String ReverbDisplay::valueId (Layer l, int b) const { return l == Layer::decay ? decayRateId (b) : eqGain (b); }
    juce::String ReverbDisplay::qId (Layer l, int b) const { return l == Layer::decay ? decayQ (b) : eqQ (b); }

    bool ReverbDisplay::bandActive (Layer l, int b) const
    {
        return l == Layer::decay ? settings.decay[(size_t) b].active : settings.eq[(size_t) b].active;
    }

    bool ReverbDisplay::bandUsesValue (Layer l, int b) const
    {
        return l == Layer::decay ? decayShapeUsesRate (settings.decay[(size_t) b].shape) : eqShapeUsesGain (settings.eq[(size_t) b].shape);
    }

    juce::Point<float> ReverbDisplay::getHandlePosition (Layer l, int band) const
    {
        band = juce::jlimit (0, 5, band);
        if (l == Layer::decay)
        {
            const auto& b = settings.decay[(size_t) band];
            const auto y = decayShapeUsesRate (b.shape) ? yForDecayLog2 (std::log2 (b.rate / 100.0f))
                                                        : yForDecayLog2 ((float) dsp::decayBandLog2 (b, b.freq));
            return { xForFrequency (b.freq), y };
        }
        const auto& b = settings.eq[(size_t) band];
        float db = 0.0f;
        if (eqShapeUsesGain (b.shape)) db = b.gain;
        return { xForFrequency (b.freq), yForDb (db) };
    }

    std::pair<ReverbDisplay::Layer, int> ReverbDisplay::handleAt (juce::Point<float> position) const
    {
        std::pair<Layer, int> best { editLayer, -1 };
        float bestDistance = handleRadius + 4.0f;
        // The edit layer wins ties (checked last with <=).
        for (auto layer : { editLayer == Layer::decay ? Layer::eq : Layer::decay, editLayer })
            for (int b = 0; b < 6; ++b)
            {
                if (! bandActive (layer, b))
                    continue;
                const auto d = getHandlePosition (layer, b).getDistanceFrom (position);
                if (d <= bestDistance)
                {
                    best = { layer, b };
                    bestDistance = d;
                }
            }
        return best;
    }

    //==============================================================================
    void ReverbDisplay::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (30).reduced (8, 4);
        decayButton.setBounds (top.removeFromLeft (118));
        top.removeFromLeft (6);
        eqButton.setBounds (top.removeFromLeft (78));

        auto p = area.toFloat();
        p.removeFromLeft (44.0f);   // decay % labels
        p.removeFromRight (40.0f);  // dB labels
        p.removeFromBottom (20.0f); // frequency labels
        p.removeFromTop (2.0f);
        plot = p;
        background = {};
        overlayDirty = true;
        rebuildCurves();
        rebuildSpectrum();
    }

    void ReverbDisplay::drawBackground()
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        backgroundScale = scale;
        background = juce::Image (juce::Image::RGB, std::max (1, juce::roundToInt ((float) getWidth() * scale)),
                                  std::max (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        g.fillAll (Palette::panel);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (plot, 4.0f);
        g.setFont (juce::FontOptions (11.0f));

        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto x = xForFrequency (f);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f).toNearestInt(),
                        juce::Justification::centred);
        }

        // One grid line per octave of decay; the left labels in % (accent), the right in dB (dynamic).
        for (int o = -2; o <= 2; ++o)
        {
            const auto y = yForDecayLog2 ((float) o);
            g.setColour (o == 0 ? Palette::divider.brighter (0.25f) : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (Palette::accent.withAlpha (0.85f));
            const auto pct = 100.0 * std::pow (2.0, o);
            g.drawText (formatValue (pct, pct < 100.0 ? (pct < 50.0 ? 0 : 0) : 0) + "%",
                        juce::Rectangle<float> (plot.getX() - 44.0f, y - 7.0f, 40.0f, 14.0f).toNearestInt(), juce::Justification::centredRight);
            g.setColour (Palette::dynamic.withAlpha (0.85f));
            const auto db = (float) o * dbPerOctave;
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<float> (plot.getRight() + 5.0f, y - 7.0f, 34.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft);
        }
        g.setColour (Palette::textMuted);
        g.drawText ("dB", juce::Rectangle<float> (plot.getRight() + 5.0f, plot.getBottom() + 3.0f, 34.0f, 14.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    void ReverbDisplay::rebuildCurves()
    {
        for (auto* path : { &decayPath, &decayFill, &effectivePath, &eqPath, &eqFill })
            path->clear();
        if (plot.isEmpty() || ! settingsValid)
            return;

        const auto sr = processor.getCurrentSampleRate();
        const auto base = dsp::ReverbEngine::decaySecondsFor (settings.space, settings.decayRate);
        const auto steps = std::max (2, (int) plot.getWidth() / 2);
        const auto centre = yForDecayLog2 (0.0f);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const auto f = frequencyForX (x);
            const auto dy = yForDecayLog2 ((float) dsp::decayCurveLog2 (settings.decay, f));
            const auto ey = yForDecayLog2 ((float) std::log2 (dsp::ReverbEngine::targetDecaySeconds (settings, f) / base));
            const auto qy = yForDb ((float) dsp::eqCurveDb (settings.eq, f, sr));
            if (i == 0)
            {
                decayPath.startNewSubPath (x, dy);
                effectivePath.startNewSubPath (x, ey);
                eqPath.startNewSubPath (x, qy);
            }
            else
            {
                decayPath.lineTo (x, dy);
                effectivePath.lineTo (x, ey);
                eqPath.lineTo (x, qy);
            }
        }

        decayFill = decayPath;
        decayFill.lineTo (plot.getRight(), centre);
        decayFill.lineTo (plot.getX(), centre);
        decayFill.closeSubPath();
        eqFill = eqPath;
        eqFill.lineTo (plot.getRight(), centre);
        eqFill.lineTo (plot.getX(), centre);
        eqFill.closeSubPath();
    }

    void ReverbDisplay::addSmoothCurveTo (juce::Path& path) const
    {
        // Monotone cubic (harmonic-mean tangents): smooth, no overshoot (as dooblr-soothe).
        const auto* xs = pointX.data();
        const auto* ys = pointY.data();
        const int n = numPoints;
        path.startNewSubPath (xs[0], ys[0]);
        const auto secant = [&] (int i) { const auto h = xs[i + 1] - xs[i]; return h > 1.0e-6f ? (ys[i + 1] - ys[i]) / h : 0.0f; };
        const auto tangent = [&] (int i)
        {
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

    void ReverbDisplay::rebuildSpectrum()
    {
        postPath.clear();
        postFill.clear();
        prePath.clear();
        if (plot.isEmpty())
            return;

        const auto yLevel = [this] (float db)
        {
            return juce::jmap (juce::jlimit (spectrumFloorDb, spectrumTopDb, db), spectrumFloorDb, spectrumTopDb, plot.getBottom(), plot.getY());
        };
        for (int i = 0; i < numPoints; ++i)
            pointX[(size_t) i] = xForFrequency (pointFreq[(size_t) i]);

        // Light smoothing across neighbouring points (about 1/6 octave), so the drawn spectrum
        // reads as a shape rather than FFT grain (design: smooth log spectrum).
        const auto smooth = [] (const std::array<float, numPoints>& in, std::array<float, numPoints>& out)
        {
            static constexpr float w[] { 0.06f, 0.24f, 0.40f, 0.24f, 0.06f };
            for (int i = 0; i < numPoints; ++i)
            {
                float sum = 0.0f;
                for (int k = -2; k <= 2; ++k)
                    sum += w[k + 2] * in[(size_t) juce::jlimit (0, numPoints - 1, i + k)];
                out[(size_t) i] = sum;
            }
        };

        if (post.valid && analyzerShown > 0)
        {
            smooth (post.levels, smoothed);
            for (int i = 0; i < numPoints; ++i)
                pointY[(size_t) i] = yLevel (smoothed[(size_t) i]);
            addSmoothCurveTo (postPath);
            postFill = postPath;
            postFill.lineTo (plot.getRight(), plot.getBottom());
            postFill.lineTo (plot.getX(), plot.getBottom());
            postFill.closeSubPath();
        }
        if (pre.valid && analyzerShown > 1)
        {
            smooth (pre.levels, smoothed);
            for (int i = 0; i < numPoints; ++i)
                pointY[(size_t) i] = yLevel (smoothed[(size_t) i]);
            prePath.clear();
            addSmoothCurveTo (prePath);
        }
    }

    void ReverbDisplay::paint (juce::Graphics& g)
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        if (! background.isValid() || ! juce::exactlyEqual (scale, backgroundScale))
            drawBackground();
        if (overlayDirty || ! overlay.isValid() || ! juce::exactlyEqual (scale, overlayScale))
            drawOverlay();
        g.drawImage (background, getLocalBounds().toFloat());

        // The spectrum, the only thing that changes every frame, between the two cached images.
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        if (! prePath.isEmpty())
        {
            g.setColour (Palette::spectrum.withAlpha (0.25f));
            g.strokePath (prePath, juce::PathStrokeType (1.0f));
        }
        if (! postPath.isEmpty())
        {
            g.setColour (Palette::spectrum.withAlpha (0.12f));
            g.fillPath (postFill);
            g.setColour (Palette::spectrum.withAlpha (0.55f));
            g.strokePath (postPath, juce::PathStrokeType (1.1f));
        }
        g.restoreState();

        g.drawImage (overlay, overlayArea());
    }

    juce::Rectangle<float> ReverbDisplay::overlayArea() const
    {
        return plot.expanded (handleRadius + 4.0f).getIntersection (getLocalBounds().toFloat()).getSmallestIntegerContainer().toFloat();
    }

    void ReverbDisplay::drawOverlay()
    {
        // Curves, handles and the readout: transparent, over the spectrum; redrawn only when they change.
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        const auto area = overlayArea();
        overlayScale = scale;
        overlayDirty = false;
        overlay = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (area.getWidth() * scale)),
                               std::max (1, juce::roundToInt (area.getHeight() * scale)), true);
        juce::Graphics g (overlay);
        g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        // Post EQ under the Decay Rate EQ; the layer being edited is drawn brighter.
        const auto eqOn = editLayer == Layer::eq;
        g.setColour (Palette::dynamic.withAlpha (eqOn ? 0.14f : 0.07f));
        g.fillPath (eqFill);
        g.setColour (Palette::dynamic.withAlpha (eqOn ? 1.0f : 0.6f));
        g.strokePath (eqPath, juce::PathStrokeType (eqOn ? 2.0f : 1.4f));

        g.setColour (Palette::accent.withAlpha (0.35f));
        g.strokePath (effectivePath, juce::PathStrokeType (1.0f));
        g.setColour (Palette::accent.withAlpha (eqOn ? 0.07f : 0.14f));
        g.fillPath (decayFill);
        g.setColour (Palette::accent.withAlpha (eqOn ? 0.6f : 1.0f));
        g.strokePath (decayPath, juce::PathStrokeType (eqOn ? 1.4f : 2.0f));
        g.restoreState();

        // Handles: dots for bells / notches, pills for shelves and cuts; numbered per layer.
        for (auto layer : { editLayer == Layer::decay ? Layer::eq : Layer::decay, editLayer })
        {
            const auto colour = layer == Layer::decay ? Palette::accent : Palette::dynamic;
            const auto editing = layer == editLayer;
            for (int b = 0; b < 6; ++b)
            {
                if (! bandActive (layer, b))
                    continue;
                bool pill = false;
                if (layer == Layer::decay)
                {
                    const auto s = settings.decay[(size_t) b].shape;
                    pill = s == DecayShape::lowShelf || s == DecayShape::highShelf;
                }
                else
                {
                    const auto s = settings.eq[(size_t) b].shape;
                    pill = s != EqShape::bell;
                }
                const auto c = getHandlePosition (layer, b);
                const auto hot = (layer == hoverLayer && b == hoverBand) || (layer == dragLayer && b == dragBand);
                const auto r = handleRadius + (hot ? 1.5f : 0.0f);
                const auto box = pill ? juce::Rectangle<float> (2.0f * (r - 2.0f), 2.0f * (r + 3.0f)).withCentre (c)
                                      : juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
                const auto corner = box.getWidth() * 0.5f;
                g.setColour (editing ? colour : colour.withAlpha (0.4f));
                g.fillRoundedRectangle (box, corner);
                g.setColour (colour.brighter (0.3f).withAlpha (editing ? 1.0f : 0.6f));
                g.drawRoundedRectangle (box, corner, 1.2f);
                g.setColour (editing ? Palette::background : Palette::textPrimary.withAlpha (0.8f));
                g.setFont (juce::FontOptions (pill ? 9.0f : 10.0f, juce::Font::bold));
                g.drawText (juce::String (b + 1), box.toNearestInt(), juce::Justification::centred);
            }
        }

        if (hovering || dragBand >= 0)
        {
            const auto text = readoutText();
            if (text.isNotEmpty())
            {
                const auto font = juce::Font (juce::FontOptions (11.5f));
                const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
                const auto chip = juce::Rectangle<float> (plot.getRight() - w - 6.0f, plot.getY() + 6.0f, w, 18.0f);
                g.setColour (Palette::panelRaised.withAlpha (0.92f));
                g.fillRoundedRectangle (chip, 4.0f);
                g.setFont (font);
                g.setColour (Palette::textPrimary);
                g.drawText (text, chip.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    juce::String ReverbDisplay::readoutText() const
    {
        const auto band = dragBand >= 0 ? dragBand : hoverBand;
        const auto layer = dragBand >= 0 ? dragLayer : hoverLayer;
        if (band >= 0)
        {
            if (layer == Layer::decay)
            {
                const auto& b = settings.decay[(size_t) band];
                auto text = "Decay " + juce::String (band + 1) + "  " + decayShapeNames()[(int) b.shape] + "  " + formatFrequency (b.freq);
                if (decayShapeUsesRate (b.shape))
                    text << "  " << percent (b.rate);
                text << "  Q " << formatValue (b.q, 2) << "  ("
                     << formatSeconds (dsp::ReverbEngine::targetDecaySeconds (settings, b.freq)) << ")";
                return text;
            }
            const auto& b = settings.eq[(size_t) band];
            auto text = "EQ " + juce::String (band + 1) + "  " + eqShapeNames()[(int) b.shape] + "  " + formatFrequency (b.freq);
            if (eqShapeUsesGain (b.shape))
                text << "  " << signedDb (b.gain);
            if (eqShapeIsCut (b.shape))
                text << "  " << slopeNames()[b.slope];
            text << "  Q " << formatValue (b.q, 2);
            return text;
        }

        if (! plot.contains (hoverPosition))
            return {};
        const auto f = frequencyForX (hoverPosition.x);
        return formatFrequency (f) + "  decay " + formatSeconds (dsp::ReverbEngine::targetDecaySeconds (settings, f))
             + "  EQ " + signedDb (dsp::eqCurveDb (settings.eq, f, processor.getCurrentSampleRate()));
    }

    //==============================================================================
    void ReverbDisplay::setParam (const juce::String& pid, float value)
    {
        if (auto* p = state.getParameter (pid))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void ReverbDisplay::beginGesture (const juce::String& pid)
    {
        if (auto* p = state.getParameter (pid))
            p->beginChangeGesture();
    }

    void ReverbDisplay::endGesture (const juce::String& pid)
    {
        if (auto* p = state.getParameter (pid))
            p->endChangeGesture();
    }

    void ReverbDisplay::mouseMove (const juce::MouseEvent& e)
    {
        hovering = true;
        hoverPosition = e.position;
        const auto [layer, band] = handleAt (e.position);
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        hoverLayer = layer;
        hoverBand = band;
        invalidateOverlay();
    }

    void ReverbDisplay::mouseExit (const juce::MouseEvent&)
    {
        hovering = false;
        hoverBand = -1;
        invalidateOverlay();
    }

    void ReverbDisplay::mouseDown (const juce::MouseEvent& e)
    {
        poll();
        const auto [layer, band] = handleAt (e.position);
        if (band < 0)
        {
            if (e.mods.isPopupMenu() && plot.contains (e.position))
                showAddMenu (e.position);
            return;
        }

        setEditLayer (layer);
        if (e.mods.isPopupMenu())
        {
            showBandMenu (layer, band);
            return;
        }

        dragLayer = layer;
        dragBand = band;
        dragMoved = false;
        dragPosition = getHandlePosition (layer, band);
        lastDragMouse = e.position;
    }

    void ReverbDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragBand < 0)
            return;

        if (! dragMoved)
        {
            if (e.getDistanceFromDragStart() < 2)
                return;
            dragMoved = true;
            beginGesture (freqId (dragLayer, dragBand));
            beginGesture (valueId (dragLayer, dragBand));
        }

        const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        dragPosition += (e.position - lastDragMouse) * fine;
        lastDragMouse = e.position;

        setParam (freqId (dragLayer, dragBand), frequencyForX (dragPosition.x));
        if (bandUsesValue (dragLayer, dragBand))
        {
            const auto l = decayLog2ForY (dragPosition.y);
            setParam (valueId (dragLayer, dragBand), dragLayer == Layer::decay ? 100.0f * std::exp2 (l) : l * dbPerOctave);
        }
        hoverPosition = e.position;
        poll();
    }

    void ReverbDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (dragBand >= 0 && dragMoved)
        {
            endGesture (freqId (dragLayer, dragBand));
            endGesture (valueId (dragLayer, dragBand));
        }
        dragBand = -1;
        dragMoved = false;
        invalidateOverlay();
    }

    void ReverbDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        const auto [layer, band] = handleAt (e.position);
        if (band >= 0)
            removeBand (layer, band);
        else if (plot.contains (e.position))
            addBandAt (editLayer, e.position);
    }

    void ReverbDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const auto [layer, band] = handleAt (e.position);
        if (band < 0)
            return;
        const auto delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        if (delta == 0.0f)
            return;
        const auto current = layer == Layer::decay ? settings.decay[(size_t) band].q : settings.eq[(size_t) band].q;
        const auto q = juce::jlimit (minQ, maxQ, current * std::pow (2.0f, delta * (e.mods.isShiftDown() ? 0.5f : 2.0f)));
        beginGesture (qId (layer, band));
        setParam (qId (layer, band), q);
        endGesture (qId (layer, band));
        poll();
    }

    void ReverbDisplay::addBandAt (Layer layer, juce::Point<float> position)
    {
        poll();
        int band = -1;
        for (int b = 0; b < 6 && band < 0; ++b)
            if (! bandActive (layer, b))
                band = b;
        if (band < 0)
            return;

        setEditLayer (layer);
        const auto f = frequencyForX (position.x);
        const auto t = (position.x - plot.getX()) / std::max (1.0f, plot.getWidth());
        const auto l = decayLog2ForY (position.y);

        if (layer == Layer::decay)
        {
            const auto shape = t < 0.12f ? DecayShape::lowShelf : (t > 0.88f ? DecayShape::highShelf : DecayShape::bell);
            const juce::String ids[] { decayActive (band), decayShape (band), decayFreq (band), decayRateId (band), decayQ (band) };
            for (auto& pid : ids) beginGesture (pid);
            setParam (decayActive (band), 1.0f);
            setParam (decayShape (band), (float) (int) shape);
            setParam (decayFreq (band), f);
            setParam (decayRateId (band), 100.0f * std::exp2 (l));
            setParam (decayQ (band), shape == DecayShape::bell ? 1.0f : 0.7f);
            for (auto& pid : ids) endGesture (pid);
        }
        else
        {
            const auto shape = t < 0.08f ? EqShape::lowCut : (t > 0.92f ? EqShape::highCut : EqShape::bell);
            const juce::String ids[] { eqActive (band), eqShape (band), eqFreq (band), eqGain (band), eqQ (band) };
            for (auto& pid : ids) beginGesture (pid);
            setParam (eqActive (band), 1.0f);
            setParam (eqShape (band), (float) (int) shape);
            setParam (eqFreq (band), f);
            setParam (eqGain (band), eqShapeUsesGain (shape) ? l * dbPerOctave : 0.0f);
            setParam (eqQ (band), shape == EqShape::bell ? 1.0f : 0.71f);
            for (auto& pid : ids) endGesture (pid);
        }
        poll();
    }

    void ReverbDisplay::removeBand (Layer layer, int band)
    {
        const auto pid = layer == Layer::decay ? decayActive (band) : eqActive (band);
        beginGesture (pid);
        setParam (pid, 0.0f);
        endGesture (pid);
        hoverBand = -1;
        poll();
    }

    void ReverbDisplay::showAddMenu (juce::Point<float> position)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Add Decay Rate EQ band here");
        menu.addItem (2, "Add Post EQ band here");
        juce::Component::SafePointer<ReverbDisplay> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (juce::roundToInt (position.x), juce::roundToInt (position.y), 1, 1))),
                            [safe, position] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->addBandAt (result == 1 ? Layer::decay : Layer::eq, position);
                            });
    }

    void ReverbDisplay::showBandMenu (Layer layer, int band)
    {
        juce::PopupMenu menu;
        const auto names = layer == Layer::decay ? decayShapeNames() : eqShapeNames();
        const auto current = layer == Layer::decay ? (int) settings.decay[(size_t) band].shape : (int) settings.eq[(size_t) band].shape;
        for (int s = 0; s < names.size(); ++s)
            menu.addItem (1 + s, names[s], true, s == current);
        if (layer == Layer::eq)
        {
            menu.addSeparator();
            const auto cut = eqShapeIsCut (settings.eq[(size_t) band].shape);
            const auto slopes = slopeNames();
            for (int s = 0; s < slopes.size(); ++s)
                menu.addItem (50 + s, slopes[s], cut, cut && s == settings.eq[(size_t) band].slope);
        }
        menu.addSeparator();
        menu.addItem (101, "Remove band");

        const auto node = getHandlePosition (layer, band);
        juce::Component::SafePointer<ReverbDisplay> safe (this);
        const auto numShapes = names.size();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (juce::roundToInt (node.x), juce::roundToInt (node.y), 1, 1))),
                            [safe, layer, band, numShapes] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                auto& self = *safe;
                                if (result >= 1 && result <= numShapes)
                                {
                                    const auto pid = layer == Layer::decay ? decayShape (band) : eqShape (band);
                                    self.beginGesture (pid);
                                    self.setParam (pid, (float) (result - 1));
                                    self.endGesture (pid);
                                }
                                else if (result >= 50 && result < 53)
                                {
                                    self.beginGesture (eqSlope (band));
                                    self.setParam (eqSlope (band), (float) (result - 50));
                                    self.endGesture (eqSlope (band));
                                }
                                else if (result == 101)
                                    self.removeBand (layer, band);
                                self.poll();
                            });
    }
}
