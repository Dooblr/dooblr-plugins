#include "ResponseCurveComponent.h"

#include "../PluginProcessor.h"
#include "Theme.h"

namespace dooblrq::ui
{
    namespace
    {
        constexpr double minDisplayFrequency = 20.0;
        constexpr double maxDisplayFrequency = 20000.0;
        constexpr float handleRadius = 8.0f;

        /** dB of handle movement per dB of band gain: the handle sits at a shelf's midpoint. */
        double handleGainScale (dsp::FilterType type)
        {
            switch (type)
            {
                case dsp::FilterType::bell:      return 1.0;
                case dsp::FilterType::lowShelf:
                case dsp::FilterType::highShelf: return 0.5;
                case dsp::FilterType::tilt:      // handle stays at the 0 dB pivot; dragged relatively
                case dsp::FilterType::lowCut:
                case dsp::FilterType::highCut:
                case dsp::FilterType::notch:     break;
            }

            return 0.0;
        }

        /** Gainless types (cuts, notch): the handle's vertical position is the Q. */
        bool verticalIsQ (dsp::FilterType type)
        {
            return type == dsp::FilterType::lowCut || type == dsp::FilterType::highCut || type == dsp::FilterType::notch;
        }
    }

    double ResponseCurveComponent::qFromHandleDecibels (double decibels)
    {
        // A 12 dB/oct cut's level at its cutoff is exactly 20*log10(Q) (analog prototype and
        // the prewarped SVF alike): -3 dB at Q 0.707, 0 dB at Q 1, +6 dB at Q 2. The notch
        // uses the same scale for its handle.
        return juce::jlimit ((double) params::minQ, (double) params::maxQ, std::pow (10.0, decibels / 20.0));
    }

    ResponseCurveComponent::ResponseCurveComponent (DooblrQAudioProcessor& processorToDisplay)
        : processor (processorToDisplay)
    {
        setOpaque (true);

        for (auto* parameter : processor.getParameters())
            parameter->addListener (this);

        startTimerHz (60);
    }

    ResponseCurveComponent::~ResponseCurveComponent()
    {
        stopTimer();

        for (auto* parameter : processor.getParameters())
            parameter->removeListener (this);
    }

    void ResponseCurveComponent::setSelectedBand (int bandIndex)
    {
        selectedBand = bandIndex;
        repaint();
    }

    void ResponseCurveComponent::parameterValueChanged (int, float)
    {
        // May be called on the audio thread (host automation): only flag it there.
        if (juce::MessageManager::existsAndIsCurrentThread())
            repaint();
        else
            repaintPending.store (true);
    }

    void ResponseCurveComponent::timerCallback()
    {
        // Redraw on parameter changes, on new analyzer frames, while the smoothed spectrum
        // is still moving, and continuously while a dynamic band is moving.
        const auto newFrames = updateAnalyzer();
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto dt = lastDisplayTime > 0.0 ? now - lastDisplayTime : 1.0 / 60.0;
        lastDisplayTime = now;

        auto spectrumMoving = false;

        if (processor.isAnalyzerRunning() && processor.getAnalyzerViewSettings().visible)
        {
            updateDisplay (dt);
            spectrumMoving = liveDisplay.getLastMovement() > 0.05f
                             || (processor.getSpectrumAnalyzer().isFrozen() && overlayDisplay.getLastMovement() > 0.02f);
        }

        if (repaintPending.exchange (false) || newFrames > 0 || spectrumMoving || anyDynamicsActive())
            repaint();
    }

    int ResponseCurveComponent::updateAnalyzer()
    {
        if (! processor.isAnalyzerRunning())
            return 0;

        auto& analyzer = processor.getSpectrumAnalyzer();
        analyzer.setSampleRate (processor.getAnalysisSampleRate());
        return analyzer.pull (processor.getAnalyzerFifo());
    }

    void ResponseCurveComponent::updateDisplay (double dtSeconds)
    {
        auto& analyzer = processor.getSpectrumAnalyzer();
        const auto rate = analyzer.getSampleRate();

        liveDisplay.prepare (rate, analysis::SpectrumAnalyzer::numBins);
        overlayDisplay.prepare (rate, analysis::SpectrumAnalyzer::numBins);
        liveDisplay.advance (analyzer.getLiveDb(), dtSeconds, liveRiseSeconds, liveFallSeconds);

        // The overlay is the average since Freeze was engaged (or Clear while frozen): it
        // appears with the first averaged frame (snapped, not faded in from silence) and
        // then glides as the average grows. Nothing is averaged or drawn while not frozen.
        if (! analyzer.isFrozen() || analyzer.getAccumulatedFrames() == 0)
        {
            overlayPrimed = false;
            return;
        }

        if (! overlayPrimed)
        {
            overlayDisplay.snapTo (analyzer.getAverageDb());
            overlayPrimed = true;
        }
        else
        {
            overlayDisplay.advance (analyzer.getAverageDb(), dtSeconds, overlaySeconds, overlaySeconds);
        }
    }

    float ResponseCurveComponent::spectrumDbToY (double decibels) const
    {
        const auto area = plotArea();
        return juce::jmap ((float) decibels, spectrumTopDb, spectrumBottomDb, area.getY(), area.getBottom());
    }

    juce::Path ResponseCurveComponent::makeSmoothPath (const std::vector<juce::Point<float>>& points, bool closed) const
    {
        // Quadratic segments through the midpoints: a smooth curve without polyline corners.
        juce::Path path;

        if (points.size() < 2)
            return path;

        const auto area = plotArea();

        if (closed)
        {
            path.startNewSubPath (points.front().x, area.getBottom());
            path.lineTo (points.front());
        }
        else
        {
            path.startNewSubPath (points.front());
        }

        for (size_t i = 1; i + 1 < points.size(); ++i)
        {
            const auto mid = (points[i] + points[i + 1]) * 0.5f;
            path.quadraticTo (points[i], mid);
        }

        path.lineTo (points.back());

        if (closed)
        {
            path.lineTo (points.back().x, area.getBottom());
            path.closeSubPath();
        }

        return path;
    }

    juce::Path ResponseCurveComponent::makeSpectrumPath (const std::vector<float>& binDb, bool closed) const
    {
        // Peak-hold line: the maximum within +/- 1/24 octave (not smoothed, so tone peaks show).
        const auto area = plotArea();
        const auto sampleRate = processor.getSpectrumAnalyzer().getSampleRate();
        const auto tilt = (double) processor.getAnalyzerViewSettings().tiltDbPerOctave;
        constexpr auto step = 1.5f;
        constexpr auto octaveSpan = 1.0 / 12.0;

        std::vector<juce::Point<float>> points;

        for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
        {
            const auto frequency = xToFrequency (x);
            const auto db = (double) analysis::SpectrumAnalyzer::levelAt (binDb, frequency, sampleRate, octaveSpan)
                            + tilt * std::log2 (frequency / 1000.0);
            points.emplace_back (x, juce::jlimit (area.getY(), area.getBottom(), spectrumDbToY (db)));
        }

        return makeSmoothPath (points, closed);
    }

    juce::Path ResponseCurveComponent::makeDisplayPath (const analysis::SpectrumDisplay& display, bool closed) const
    {
        const auto area = plotArea();
        const auto tilt = (double) processor.getAnalyzerViewSettings().tiltDbPerOctave;
        const auto& levels = display.getLevels();

        std::vector<juce::Point<float>> points;
        points.reserve (levels.size());

        for (int i = 0; i < display.getNumPoints(); ++i)
        {
            const auto frequency = display.getFrequency (i);
            const auto db = (double) levels[(size_t) i] + tilt * std::log2 (frequency / 1000.0);
            points.emplace_back (frequencyToX (frequency), juce::jlimit (area.getY(), area.getBottom(), spectrumDbToY (db)));
        }

        return makeSmoothPath (points, closed);
    }

    void ResponseCurveComponent::drawSpectrum (juce::Graphics& g)
    {
        const auto& view = processor.getAnalyzerViewSettings();

        if (! view.visible)
            return;

        auto& analyzer = processor.getSpectrumAnalyzer();

        if (view.showPeak && analyzer.getPeakFrames() > 0)
        {
            // Peak-hold since the last Clear.
            const auto& peak = analyzer.getPeakDb();
            juce::Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, makeSpectrumPath (peak, false), dashes, 2);
            g.setColour ((analyzer.isFrozen() ? Palette::frozen : Palette::spectrum).withAlpha (0.55f));
            g.fillPath (dashed);
        }

        if (analyzer.isFrozen() && overlayPrimed)
        {
            // The average since Freeze was engaged, still accumulating while frozen.
            g.setColour (Palette::frozen.withAlpha (0.05f));
            g.fillPath (makeDisplayPath (overlayDisplay, true));
            g.setColour (Palette::frozen.withAlpha (0.8f));
            g.strokePath (makeDisplayPath (overlayDisplay, false),
                          juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (Palette::spectrum.withAlpha (0.11f));
        g.fillPath (makeDisplayPath (liveDisplay, true));
        g.setColour (Palette::spectrum.withAlpha (0.32f));
        g.strokePath (makeDisplayPath (liveDisplay, false),
                      juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The analyzer has its own scale (dBFS, before tilt at 1 kHz); label it on the right.
        const auto area = plotArea();
        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::spectrum.withAlpha (0.45f));

        for (const auto db : { -30.0f, -60.0f })
            g.drawText (juce::String (juce::roundToInt (db)) + " dBFS",
                        juce::Rectangle<float> (area.getRight() - 52.0f, spectrumDbToY (db) - 12.0f, 48.0f, 11.0f),
                        juce::Justification::centredRight);
    }

    bool ResponseCurveComponent::anyDynamicsActive() const
    {
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);

            if (s.enabled && s.dynamicsEnabled && dsp::supportsDynamics (s.type))
                return true;
        }

        return false;
    }

    //==============================================================================
    juce::Rectangle<float> ResponseCurveComponent::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (12.0f, 10.0f).withTrimmedLeft (24.0f).withTrimmedBottom (14.0f);
    }

    float ResponseCurveComponent::frequencyToX (double frequency) const
    {
        const auto area = plotArea();
        const auto proportion = std::log (frequency / minDisplayFrequency) / std::log (maxDisplayFrequency / minDisplayFrequency);
        return area.getX() + (float) proportion * area.getWidth();
    }

    double ResponseCurveComponent::xToFrequency (float x) const
    {
        const auto area = plotArea();
        const auto proportion = (double) ((x - area.getX()) / area.getWidth());
        return minDisplayFrequency * std::pow (maxDisplayFrequency / minDisplayFrequency, proportion);
    }

    float ResponseCurveComponent::decibelsToY (double decibels) const
    {
        const auto area = plotArea();
        return juce::jmap ((float) decibels, displayRangeDb, -displayRangeDb, area.getY(), area.getBottom());
    }

    double ResponseCurveComponent::yToDecibels (float y) const
    {
        const auto area = plotArea();
        return (double) juce::jmap (y, area.getY(), area.getBottom(), displayRangeDb, -displayRangeDb);
    }

    double ResponseCurveComponent::qToOctaves (double q)       { return params::qToOctaves (q); }
    double ResponseCurveComponent::octavesToQ (double octaves) { return params::octavesToQ (octaves); }

    bool ResponseCurveComponent::showsAnalogResponse() const
    {
        const auto* mode = processor.getParameterState().getRawParameterValue (params::phaseMode);
        return mode != nullptr && juce::roundToInt (mode->load()) != (int) params::PhaseMode::zeroLatency;
    }

    bool ResponseCurveComponent::widthHandlesVisible() const
    {
        return selectedBand >= 0 && processor.getBandSettings (selectedBand).enabled;
    }

    std::pair<juce::Point<float>, juce::Point<float>> ResponseCurveComponent::widthHandlePositions (int bandIndex) const
    {
        const auto s = processor.getBandSettings (bandIndex);
        const auto factor = std::pow (2.0, 0.5 * qToOctaves ((double) s.q));
        const auto model = makeResponseModel (false);
        const auto at = [&] (double frequency)
        {
            const auto f = juce::jlimit (minDisplayFrequency, maxDisplayFrequency, frequency);
            const auto db = juce::jlimit ((double) -displayRangeDb, (double) displayRangeDb, model.decibelsAt (f));
            return juce::Point<float> (frequencyToX (f), decibelsToY (db));
        };

        return { at ((double) s.frequency / factor), at ((double) s.frequency * factor) };
    }

    int ResponseCurveComponent::widthHandleAt (juce::Point<float> position) const
    {
        if (! widthHandlesVisible())
            return 0;

        const auto [lower, upper] = widthHandlePositions (selectedBand);
        const auto radius = handleRadius * 1.1f;

        if (upper.getDistanceFrom (position) < radius)
            return 1;

        if (lower.getDistanceFrom (position) < radius)
            return -1;

        return 0;
    }

    void ResponseCurveComponent::updateCursor (juce::Point<float> position)
    {
        if (widthHandleAt (position) != 0)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (bandAt (position) >= 0)
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void ResponseCurveComponent::mouseMove (const juce::MouseEvent& event)
    {
        updateCursor (event.position);
    }

    void ResponseCurveComponent::mouseExit (const juce::MouseEvent&)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void ResponseCurveComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        // Scroll over a band handle (or its bandwidth handles) to change its Q.
        auto band = bandAt (event.position);
        if (band < 0 && widthHandleAt (event.position) != 0)
            band = selectedBand;

        if (band < 0)
        {
            Component::mouseWheelMove (event, wheel);
            return;
        }

        const auto delta = (double) (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
        const auto q = juce::jlimit ((double) params::minQ, (double) params::maxQ,
                                     (double) processor.getBandSettings (band).q * std::pow (2.0, delta * 2.0));

        if (auto* parameter = bandParameter (band, params::band::q))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) q));
            parameter->endChangeGesture();
        }

        repaint();
    }

    double ResponseCurveComponent::handleDecibels (const dsp::BandSettings& s, double sampleRate, bool analog)
    {
        if (s.type == dsp::FilterType::notch) // its centre is -inf: place the handle by Q instead
            return 20.0 * std::log10 (std::max ((double) s.q, 1.0e-3));

        // With any slope the level at the band frequency is the corner level (cuts: -3 dB
        // scaled by the resonance, i.e. 20 log10 Q) or half the gain (shelves), so the handle
        // stays on the curve for every slope.
        if (analog)
            return juce::Decibels::gainToDecibels (
                std::abs (dsp::analogResponseAt (dsp::makeBandPrototype (s.type, s.slope, (double) s.q, (double) s.gainDb),
                                                 (double) s.frequency, (double) s.frequency)), -100.0);

        const auto c = dsp::makeBandCoefficients (s.type, s.slope, (double) s.frequency, (double) s.q, (double) s.gainDb, sampleRate);
        return juce::Decibels::gainToDecibels (dsp::magnitudeAt (c, (double) s.frequency, sampleRate), -100.0);
    }

    juce::Point<float> ResponseCurveComponent::handlePosition (int bandIndex) const
    {
        const auto settings = processor.getBandSettings (bandIndex);
        const auto db = handleDecibels (settings, processor.getAnalysisSampleRate(), showsAnalogResponse());
        return { frequencyToX ((double) settings.frequency),
                 decibelsToY (juce::jlimit ((double) -displayRangeDb, (double) displayRangeDb, db)) };
    }

    int ResponseCurveComponent::bandAt (juce::Point<float> position) const
    {
        auto best = -1;
        auto bestDistance = handleRadius * 1.8f;

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto distance = handlePosition (b).getDistanceFrom (position);

            if (distance < bestDistance)
            {
                best = b;
                bestDistance = distance;
            }
        }

        return best;
    }

    ResponseCurveComponent::ResponseModel ResponseCurveComponent::makeResponseModel (bool includeDynamics) const
    {
        ResponseModel model;
        model.sampleRate = processor.getAnalysisSampleRate();
        model.analog = showsAnalogResponse();

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            model.enabled[(size_t) b] = s.enabled;

            if (! s.enabled)
                continue;

            auto gain = (double) s.gainDb;

            if (includeDynamics && s.dynamicsEnabled && dsp::supportsDynamics (s.type))
                gain += (double) processor.getBandDynamicGainDb (b);

            gain = juce::jlimit ((double) -dsp::DynamicEQBand::maxTotalGainDb,
                                 (double) dsp::DynamicEQBand::maxTotalGainDb, gain);

            model.prototypes[(size_t) b] = dsp::makeBandPrototype (s.type, s.slope, (double) s.q, gain);
            model.coefficients[(size_t) b] = dsp::makeBandCoefficients (s.type, s.slope, (double) s.frequency,
                                                                        (double) s.q, gain, model.sampleRate);
            model.frequencies[(size_t) b] = (double) s.frequency;
        }

        return model;
    }

    double ResponseCurveComponent::ResponseModel::decibelsAt (double frequency) const
    {
        auto magnitude = 1.0;

        for (size_t b = 0; b < coefficients.size(); ++b)
            if (enabled[b])
                magnitude *= analog ? std::abs (dsp::analogResponseAt (prototypes[b], frequencies[b], frequency))
                                    : dsp::magnitudeAt (coefficients[b], frequency, sampleRate);

        return juce::Decibels::gainToDecibels (magnitude, -100.0);
    }

    double ResponseCurveComponent::ResponseModel::bandDecibelsAt (size_t b, double frequency) const
    {
        if (! enabled[b])
            return 0.0;

        const auto magnitude = analog ? std::abs (dsp::analogResponseAt (prototypes[b], frequencies[b], frequency))
                                      : dsp::magnitudeAt (coefficients[b], frequency, sampleRate);
        return juce::Decibels::gainToDecibels (magnitude, -100.0);
    }

    double ResponseCurveComponent::responseDecibelsAt (double frequency, bool includeDynamics) const
    {
        return makeResponseModel (includeDynamics).decibelsAt (frequency);
    }

    bool ResponseCurveComponent::bandDynamicsMoving (const dsp::BandSettings& s, int b) const
    {
        return s.enabled && s.dynamicsEnabled && dsp::supportsDynamics (s.type)
            && std::abs (processor.getBandDynamicGainDb (b)) > 1.0e-3f;
    }

    double ResponseCurveComponent::bandDynamicDeltaDecibelsAt (int bandIndex, double frequency) const
    {
        if (! bandDynamicsMoving (processor.getBandSettings (bandIndex), bandIndex))
            return 0.0;

        const auto b = (size_t) bandIndex;
        return makeResponseModel (true).bandDecibelsAt (b, frequency) - makeResponseModel (false).bandDecibelsAt (b, frequency);
    }

    std::vector<ResponseCurveComponent::DynamicRun> ResponseCurveComponent::computeDynamicRuns (int onlyBand) const
    {
        std::vector<DynamicRun> runs;
        std::array<bool, params::numBands> moving {};
        auto any = false;

        for (int b = 0; b < params::numBands; ++b)
        {
            moving[(size_t) b] = (onlyBand < 0 || b == onlyBand) && bandDynamicsMoving (processor.getBandSettings (b), b);
            any = any || moving[(size_t) b];
        }

        if (! any)
            return runs; // idle dynamics cost nothing beyond the gain reads

        // Two models per paint; each band's delta only evaluates that band's own filter.
        const auto area = plotArea();
        const auto withDynamics = makeResponseModel (true);
        const auto withoutDynamics = makeResponseModel (false);
        constexpr auto step = 2.0f;

        for (int b = 0; b < params::numBands; ++b)
        {
            if (! moving[(size_t) b])
                continue;

            DynamicRun current;
            current.band = b;

            for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
            {
                const auto frequency = xToFrequency (x);
                const auto delta = withDynamics.bandDecibelsAt ((size_t) b, frequency)
                                 - withoutDynamics.bandDecibelsAt ((size_t) b, frequency);

                if (std::abs (delta) > dynamicRegionThresholdDb)
                {
                    current.points.emplace_back (x, delta);
                }
                else if (! current.points.empty())
                {
                    runs.push_back (std::move (current));
                    current = {};
                    current.band = b;
                }
            }

            if (! current.points.empty())
                runs.push_back (std::move (current));
        }

        return runs;
    }

    std::vector<juce::Range<double>> ResponseCurveComponent::dynamicRegions (int bandIndex) const
    {
        std::vector<juce::Range<double>> regions;

        for (const auto& run : computeDynamicRuns (bandIndex))
            regions.emplace_back (xToFrequency (run.points.front().first), xToFrequency (run.points.back().first));

        return regions;
    }

    //==============================================================================
    void ResponseCurveComponent::drawGrid (juce::Graphics& g) const
    {
        const auto area = plotArea();

        g.setFont (juce::FontOptions (10.5f));

        for (const auto frequency : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto x = frequencyToX (frequency);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

            const auto text = frequency >= 1000.0 ? juce::String (juce::roundToInt (frequency / 1000.0)) + "k"
                                                  : juce::String (juce::roundToInt (frequency));
            g.setColour (Palette::textMuted);
            g.drawText (text, juce::Rectangle<float> (x - 20.0f, area.getBottom() + 1.0f, 40.0f, 12.0f),
                        juce::Justification::centred);
        }

        for (int db = -18; db <= 18; db += 6)
        {
            const auto y = decibelsToY (db);
            g.setColour (db == 0 ? Palette::divider.brighter (0.2f) : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

            g.setColour (Palette::textMuted);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (area.getX() - 30.0f, y - 6.0f, 26.0f, 12.0f),
                        juce::Justification::centredRight);
        }
    }

    juce::Path ResponseCurveComponent::makeResponsePath (bool includeDynamics) const
    {
        const auto area = plotArea();
        const auto model = makeResponseModel (includeDynamics);
        juce::Path path;
        constexpr auto step = 2.0f;

        for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
        {
            const auto db = model.decibelsAt (xToFrequency (x));
            const auto y = juce::jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, decibelsToY (db));

            // Path::isEmpty() stays true after a lone move-to, so track the first point explicitly.
            if (x <= area.getX())
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        return path;
    }

    void ResponseCurveComponent::paint (juce::Graphics& g)
    {
        repaintPending.store (false);

        g.fillAll (Palette::panel);

        const auto area = plotArea();

        // Analyzer first, so the grid, EQ curve and handles stay on top and readable.
        {
            g.saveState();
            g.reduceClipRegion (area.toNearestInt());
            drawSpectrum (g);
            g.restoreState();
        }

        drawGrid (g);

        g.saveState();
        g.reduceClipRegion (area.toNearestInt().expanded (1));

        const auto staticPath = makeResponsePath (false);

        juce::Path fill (staticPath);
        fill.lineTo (area.getRight(), decibelsToY (0.0));
        fill.lineTo (area.getX(), decibelsToY (0.0));
        fill.closeSubPath();
        g.setColour (Palette::accent.withAlpha (0.08f));
        g.fillPath (fill);

        g.setColour (Palette::accent);
        g.strokePath (staticPath, juce::PathStrokeType (2.0f));

        // Dynamic activity, per band and only where that band is being pushed: fill between the
        // static curve and the static curve moved by this band's own dynamic change, in the
        // band's colour, then the live (combined) curve over the same span.
        if (const auto runs = computeDynamicRuns(); ! runs.empty())
        {
            const auto staticModel = makeResponseModel (false);
            const auto liveModel = makeResponseModel (true);
            const auto clampY = [&] (double db)
            {
                return juce::jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, decibelsToY (db));
            };

            for (const auto& run : runs)
            {
                const auto colour = bandColour (run.band);
                juce::Path region, live;
                std::vector<float> baseY;
                baseY.reserve (run.points.size());

                for (size_t i = 0; i < run.points.size(); ++i)
                {
                    const auto [x, delta] = run.points[i];
                    const auto frequency = xToFrequency (x);
                    const auto base = staticModel.decibelsAt (frequency);
                    baseY.push_back (clampY (base));

                    if (i == 0)
                    {
                        region.startNewSubPath (x, clampY (base + delta));
                        live.startNewSubPath (x, clampY (liveModel.decibelsAt (frequency)));
                    }
                    else
                    {
                        region.lineTo (x, clampY (base + delta));
                        live.lineTo (x, clampY (liveModel.decibelsAt (frequency)));
                    }
                }

                for (auto i = run.points.size(); i-- > 0;)
                    region.lineTo (run.points[i].first, baseY[i]);

                region.closeSubPath();
                g.setColour (colour.withAlpha (0.32f));
                g.fillPath (region);
                g.setColour (colour);
                g.strokePath (live, juce::PathStrokeType (1.8f));
            }
        }

        g.restoreState();

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            const auto circle = juce::Rectangle<float> (handleRadius * 2.0f, handleRadius * 2.0f)
                                    .withCentre (handlePosition (b));
            const auto colour = bandColour (b);

            if (s.enabled)
            {
                g.setColour (colour.withAlpha (b == selectedBand ? 1.0f : 0.8f));
                g.fillEllipse (circle);
                g.setColour (Palette::background);
            }
            else
            {
                // Inactive: grey, hollow, so it reads clearly as "off".
                g.setColour (Palette::panel);
                g.fillEllipse (circle);
                g.setColour (Palette::textMuted.withAlpha (0.7f));
                g.drawEllipse (circle.reduced (0.5f), 1.0f);
                g.setColour (Palette::textMuted.withAlpha (0.8f));
            }

            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (juce::String (b + 1), circle, juce::Justification::centred);

            if (b == selectedBand)
            {
                g.setColour (s.enabled ? Palette::textPrimary : Palette::textMuted);
                g.drawEllipse (circle.expanded (3.0f), 1.2f);
            }

            // A tilt's handle sits on its 0 dB pivot, and a notch's height is its Q (its centre is
            // -inf), so show the amount next to them.
            if (s.type == dsp::FilterType::tilt || s.type == dsp::FilterType::notch)
            {
                const auto text = s.type == dsp::FilterType::tilt
                                      ? "Tilt " + juce::String (s.gainDb >= 0.0f ? "+" : "") + juce::String (s.gainDb, 1) + " dB"
                                      : "Q " + juce::String (s.q, 2);
                g.setColour ((s.enabled ? colour : Palette::textMuted).withAlpha (0.9f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (text, circle.translated (0.0f, -20.0f).withSizeKeepingCentre (90.0f, 14.0f),
                            juce::Justification::centred);
            }
        }

        // Bandwidth handles of the selected band, and a readout while they are dragged.
        if (widthHandlesVisible())
        {
            const auto [lower, upper] = widthHandlePositions (selectedBand);
            const auto colour = bandColour (selectedBand);
            const auto s = processor.getBandSettings (selectedBand);

            g.setColour (colour.withAlpha (0.35f));
            const float dashes[] { 2.0f, 3.0f };
            g.drawDashedLine ({ lower, upper }, dashes, 2, 1.0f);

            for (const auto& p : { lower, upper })
            {
                const auto pill = juce::Rectangle<float> (7.0f, 16.0f).withCentre (p);
                g.setColour (colour.withAlpha (0.9f));
                g.fillRoundedRectangle (pill, 3.0f);
                g.setColour (Palette::background);
                g.drawRoundedRectangle (pill, 3.0f, 1.0f);
            }

            if (widthDragSide != 0 || qDrag)
            {
                const auto anchor = widthDragSide < 0 ? lower : (widthDragSide > 0 ? upper : handlePosition (selectedBand));
                const auto text = "Width " + juce::String (qToOctaves ((double) s.q), 2) + " oct (Q " + juce::String (s.q, 2) + ")";
                const auto box = juce::Rectangle<float> (150.0f, 18.0f).withCentre (anchor.translated (0.0f, -22.0f));
                g.setColour (Palette::panelRaised.withAlpha (0.92f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (Palette::textPrimary);
                g.setFont (juce::FontOptions (11.5f));
                g.drawText (text, box, juce::Justification::centred);
            }
        }

        g.setColour (Palette::divider);
        g.drawRect (getLocalBounds());
    }

    //==============================================================================
    juce::RangedAudioParameter* ResponseCurveComponent::bandParameter (int bandIndex, const char* suffix) const
    {
        return processor.getParameterState().getParameter (params::bandId (bandIndex, suffix));
    }

    void ResponseCurveComponent::setBandParameter (int bandIndex, const char* suffix, float value)
    {
        if (auto* parameter = bandParameter (bandIndex, suffix))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    juce::PopupMenu ResponseCurveComponent::createSlopeMenu (int bandIndex) const
    {
        juce::PopupMenu menu;
        const auto s = processor.getBandSettings (bandIndex);
        const auto applies = dsp::slopeApplies (s.type);
        const auto names = params::slopeNames();

        menu.addSectionHeader ("Band " + juce::String (bandIndex + 1) + " Slope");

        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], applies, applies && (int) s.slope == i);

        if (! applies)
        {
            menu.addSeparator();
            menu.addItem (-1, "Slope applies to Cut, Shelf and Tilt types", false, false);
        }

        return menu;
    }

    void ResponseCurveComponent::setBandSlope (int bandIndex, dsp::Slope slope)
    {
        if (auto* parameter = bandParameter (bandIndex, params::band::slope))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) (int) slope));
            parameter->endChangeGesture();
        }

        repaint();
    }

    void ResponseCurveComponent::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            draggingBand = -1;
            widthDragSide = 0;
            qDrag = false;

            if (const auto band = bandAt (event.position); band >= 0)
            {
                if (onBandSelected != nullptr)
                    onBandSelected (band);

                juce::Component::SafePointer<ResponseCurveComponent> safeThis (this);
                createSlopeMenu (band).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                                                .withMousePosition(),
                                                      [safeThis, band] (int result)
                                                      {
                                                          if (safeThis != nullptr && result > 0)
                                                              safeThis->setBandSlope (band, (dsp::Slope) (result - 1));
                                                      });
            }

            return;
        }

        widthDragSide = widthHandleAt (event.position);
        qDrag = false;

        if (widthDragSide != 0)
        {
            draggingBand = selectedBand;
            const auto [lower, upper] = widthHandlePositions (selectedBand);
            dragOffset = (widthDragSide > 0 ? upper : lower) - event.position;

            if (auto* parameter = bandParameter (draggingBand, params::band::q))
                parameter->beginChangeGesture();

            return;
        }

        draggingBand = bandAt (event.position);
        dragEnabledBand = false;

        if (draggingBand >= 0 && event.mods.isAltDown())
        {
            qDrag = true;
            dragStartQ = processor.getBandSettings (draggingBand).q;
            dragStartY = event.position.y;

            if (onBandSelected != nullptr)
                onBandSelected (draggingBand);

            if (auto* parameter = bandParameter (draggingBand, params::band::q))
                parameter->beginChangeGesture();

            return;
        }

        if (draggingBand < 0)
            return;

        dragOffset = handlePosition (draggingBand) - event.position;
        dragStartGainDb = processor.getBandSettings (draggingBand).gainDb;
        dragStartQ = processor.getBandSettings (draggingBand).q;
        dragStartY = event.position.y;
        // Horizontal is always frequency; vertical is gain, or Q for gainless types.
        dragVerticalSuffix = verticalIsQ (processor.getBandSettings (draggingBand).type) ? params::band::q : params::band::gain;

        if (onBandSelected != nullptr)
            onBandSelected (draggingBand);

        for (const auto* suffix : { params::band::frequency, dragVerticalSuffix })
            if (auto* parameter = bandParameter (draggingBand, suffix))
                parameter->beginChangeGesture();
    }

    void ResponseCurveComponent::mouseDrag (const juce::MouseEvent& event)
    {
        if (draggingBand < 0 || ! event.mouseWasDraggedSinceMouseDown())
            return;

        if (widthDragSide != 0)
        {
            // Distance from the centre in octaves is half the bandwidth; the other handle mirrors.
            const auto centre = (double) processor.getBandSettings (draggingBand).frequency;
            const auto frequency = xToFrequency ((event.position + dragOffset).x);
            const auto halfOctaves = std::max (1.0e-3, widthDragSide * std::log2 (frequency / centre));
            const auto q = juce::jlimit ((double) params::minQ, (double) params::maxQ, octavesToQ (2.0 * halfOctaves));
            setBandParameter (draggingBand, params::band::q, (float) q);
            repaint();
            return;
        }

        if (qDrag)
        {
            // Option/alt-drag: up = narrower (higher Q), 48 px per octave of Q.
            const auto q = juce::jlimit ((double) params::minQ, (double) params::maxQ,
                                         (double) dragStartQ * std::pow (2.0, (double) (dragStartY - event.position.y) / 48.0));
            setBandParameter (draggingBand, params::band::q, (float) q);
            repaint();
            return;
        }

        // Moving a disabled band's handle switches it on, so the curve follows the handle.
        if (! dragEnabledBand && ! processor.getBandSettings (draggingBand).enabled)
        {
            if (auto* enabled = bandParameter (draggingBand, params::band::enabled))
            {
                enabled->beginChangeGesture();
                enabled->setValueNotifyingHost (1.0f);
                enabled->endChangeGesture();
            }

            dragEnabledBand = true;
        }

        const auto target = event.position + dragOffset;

        const auto frequency = juce::jlimit ((double) params::minFrequency, (double) params::maxFrequency,
                                             xToFrequency (target.x));
        setBandParameter (draggingBand, params::band::frequency, (float) frequency);

        const auto type = processor.getBandSettings (draggingBand).type;
        const auto scale = handleGainScale (type);

        if (verticalIsQ (type))
        {
            // Cuts and notch: up = higher Q (a resonant peak at a cut's cutoff, a narrower
            // notch), down = lower Q. The handle height is 20*log10(Q), so moving d dB scales Q
            // by 10^(d/20) and the handle stays under the pointer. Relative to the start, so a
            // purely horizontal drag never changes Q (even beyond the display's +-24 dB).
            const auto delta = yToDecibels (event.position.y) - yToDecibels (dragStartY);
            const auto q = qFromHandleDecibels (20.0 * std::log10 ((double) dragStartQ) + delta);
            setBandParameter (draggingBand, params::band::q, (float) q);
        }
        else if (type == dsp::FilterType::tilt)
        {
            // A tilt's handle stays on its 0 dB pivot; vertical movement changes the tilt amount.
            // Moving by d dB on the scale moves the high end by d dB, i.e. the total tilt by 2d.
            const auto delta = yToDecibels (event.position.y) - yToDecibels (dragStartY);
            const auto gain = juce::jlimit ((double) params::minGainDb, (double) params::maxGainDb,
                                            (double) dragStartGainDb + 2.0 * delta);
            setBandParameter (draggingBand, params::band::gain, (float) gain);
        }
        else if (scale > 0.0)
        {
            const auto gain = juce::jlimit ((double) params::minGainDb, (double) params::maxGainDb,
                                            yToDecibels (target.y) / scale);
            setBandParameter (draggingBand, params::band::gain, (float) gain);
        }

        repaint();
    }

    void ResponseCurveComponent::mouseUp (const juce::MouseEvent& event)
    {
        if (draggingBand < 0)
            return;

        if (widthDragSide != 0 || qDrag)
        {
            if (auto* parameter = bandParameter (draggingBand, params::band::q))
                parameter->endChangeGesture();

            widthDragSide = 0;
            qDrag = false;
            draggingBand = -1;
            updateCursor (event.position);
            repaint();
            return;
        }

        for (const auto* suffix : { params::band::frequency, dragVerticalSuffix })
            if (auto* parameter = bandParameter (draggingBand, suffix))
                parameter->endChangeGesture();

        draggingBand = -1;
    }

    void ResponseCurveComponent::mouseDoubleClick (const juce::MouseEvent& event)
    {
        const auto band = bandAt (event.position);

        if (band < 0)
            return;

        if (auto* enabled = bandParameter (band, params::band::enabled))
        {
            enabled->beginChangeGesture();
            enabled->setValueNotifyingHost (enabled->getValue() >= 0.5f ? 0.0f : 1.0f);
            enabled->endChangeGesture();
        }

        repaint();
    }
}
