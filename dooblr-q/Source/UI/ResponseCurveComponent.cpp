#include "ResponseCurveComponent.h"

#include "../PluginProcessor.h"
#include "RangeRing.h"
#include "Theme.h"

#include <algorithm>
#include <utility>

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

        // 0.9.2: the hover fades run on the display refresh.
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onVBlank(); });
    }

    ResponseCurveComponent::~ResponseCurveComponent()
    {
        vblank.reset();
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
        // Analogue models change the bell's effective Q (e.g. proportional Q): the handles follow it.
        const auto effectiveQ = (double) s.q * dsp::modelBellQFactor (s.model, s.type, (double) s.gainDb);
        const auto factor = std::pow (2.0, 0.5 * qToOctaves (effectiveQ));
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
        switch (hitAt (position))
        {
            case Hit::widthHandle: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Hit::handle:
            case Hit::soloBadge:
            case Hit::rangeRing:   setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
            case Hit::none:        setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    void ResponseCurveComponent::mouseMove (const juce::MouseEvent& event)
    {
        updateHover (event.position);
        updateCursor (event.position);
    }

    void ResponseCurveComponent::mouseExit (const juce::MouseEvent&)
    {
        clearHover();
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    //==============================================================================
    // 0.9.2: hover ring and blue S.
    bool ResponseCurveComponent::hoverRingAvailable (int b) const
    {
        if (b < 0 || b >= params::numBands)
            return false;

        const auto s = processor.getBandSettings (b);
        return s.enabled && dsp::supportsDynamics (s.type);
    }

    bool ResponseCurveComponent::soloBadgeVisible (int b) const
    {
        if (b < 0 || b >= params::numBands)
            return false;

        return processor.isBandSoloed (b) || (b == hoverBand && processor.getBandSettings (b).enabled);
    }

    juce::Rectangle<float> ResponseCurveComponent::soloBadgeBounds (int b) const
    {
        // On the ring's outer edge at the top right; flipped below / to the left near the plot's
        // top / right edges so it always stays on screen.
        const auto handle = handlePosition (b);
        const auto offset = soloBadgeDistance * std::sqrt (0.5f);
        const auto area = plotArea();
        auto dx = offset, dy = -offset;

        if (handle.y + dy - soloBadgeSize * 0.5f < area.getY())
            dy = offset;
        if (handle.x + dx + soloBadgeSize * 0.5f > area.getRight())
            dx = -offset;

        return juce::Rectangle<float> (soloBadgeSize, soloBadgeSize).withCentre (handle.translated (dx, dy));
    }

    double ResponseCurveComponent::angleForUpwardAmount (double upwardDb) noexcept
    {
        return juce::jlimit (-1.0, 1.0, upwardDb / RangeRing::maxRangeDb) * hoverSweepRadians;
    }

    int ResponseCurveComponent::hoverBandAt (juce::Point<float> position) const
    {
        if (ringBand >= 0)
            return ringBand;

        // Staying on the current band's S keeps its ring (the S sits at the zone's edge).
        if (hoverBand >= 0 && soloBadgeBounds (hoverBand).expanded (2.0f).contains (position)
            && bandAt (position) < 0)
            return hoverBand;

        auto best = -1;
        auto bestDistance = hoverProximity;

        for (int b = 0; b < params::numBands; ++b)
        {
            if (! processor.getBandSettings (b).enabled)
                continue;

            const auto distance = handlePosition (b).getDistanceFrom (position);

            if (distance < bestDistance)
            {
                best = b;
                bestDistance = distance;
            }
        }

        return best;
    }

    void ResponseCurveComponent::updateHover (juce::Point<float> position)
    {
        const auto band = hoverBandAt (position);

        if (band == hoverBand)
            return;

        // Keep each band's fade continuous when it changes direction.
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto rising = b == band;

            if (rising != hoverRising[(size_t) b])
            {
                const auto alpha = getHoverAlpha (b);
                hoverPhase[(size_t) b] = rising ? 1.0f - std::sqrt (std::max (0.0f, 1.0f - alpha)) : alpha;
                hoverRising[(size_t) b] = rising;
            }
        }

        hoverBand = band;
        repaint();
    }

    void ResponseCurveComponent::clearHover()
    {
        if (ringBand >= 0)
            return;

        updateHover ({ -1.0e6f, -1.0e6f });
    }

    float ResponseCurveComponent::getHoverAlpha (int b) const noexcept
    {
        if (b < 0 || b >= params::numBands)
            return 0.0f;

        const auto p = juce::jlimit (0.0f, 1.0f, hoverPhase[(size_t) b]);
        // Fade in: quadratic ease-out (fast start, gentle landing). Fade out: linear.
        return hoverRising[(size_t) b] ? 1.0f - (1.0f - p) * (1.0f - p) : p;
    }

    void ResponseCurveComponent::advanceHoverAnimation (double dt)
    {
        if (! (dt > 0.0))
            return;

        auto changed = false;

        for (int b = 0; b < params::numBands; ++b)
        {
            auto& phase = hoverPhase[(size_t) b];
            const auto before = phase;

            if (hoverRising[(size_t) b])
                phase = std::min (1.0f, phase + (float) (dt / hoverFadeInSeconds));
            else
                phase = std::max (0.0f, phase - (float) (dt / hoverFadeOutSeconds));

            changed = changed || std::abs (phase - before) > 0.0f;
        }

        if (changed)
            repaint();
    }

    void ResponseCurveComponent::onVBlank()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto dt = lastVBlankTime > 0.0 ? juce::jlimit (0.0, 0.05, now - lastVBlankTime) : 1.0 / 60.0;
        lastVBlankTime = now;
        advanceHoverAnimation (dt);
    }

    ResponseCurveComponent::Hit ResponseCurveComponent::hitAt (juce::Point<float> position, int* bandOut) const
    {
        const auto result = [bandOut] (Hit hit, int band)
        {
            if (bandOut != nullptr)
                *bandOut = band;
            return hit;
        };

        // Handles first (any band), so neither a ring nor an S ever steals a handle's clicks.
        if (widthHandleAt (position) != 0)
            return result (Hit::widthHandle, selectedBand);

        if (const auto band = bandAt (position); band >= 0)
            return result (Hit::handle, band);

        // The S: the hovered band's first, then any soloed band's (always visible).
        const auto hover = hoverBandAt (position);

        if (hover >= 0 && soloBadgeVisible (hover) && soloBadgeBounds (hover).expanded (1.0f).contains (position))
            return result (Hit::soloBadge, hover);

        for (int b = 0; b < params::numBands; ++b)
            if (processor.isBandSoloed (b) && soloBadgeBounds (b).expanded (1.0f).contains (position))
                return result (Hit::soloBadge, b);

        // The ring belongs to the nearest band only. (A drag that starts on an S still reaches
        // the ring under it: see mouseDown / mouseDrag.)
        if (const auto ring = ringBandAt (position); ring >= 0)
            return result (Hit::rangeRing, ring);

        return result (Hit::none, -1);
    }

    int ResponseCurveComponent::ringBandAt (juce::Point<float> position) const
    {
        const auto hover = hoverBandAt (position);

        if (hover < 0 || ! hoverRingAvailable (hover))
            return -1;

        const auto distance = handlePosition (hover).getDistanceFrom (position);
        return distance >= hoverRingInnerHit && distance <= hoverRingOuterHit ? hover : -1;
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
                std::abs (dsp::analogResponseAt (dsp::makeModelPrototype (s.model, s.type, s.slope, (double) s.q,
                                                                          (double) s.gainDb, (double) s.attenDb),
                                                 (double) s.frequency, (double) s.frequency)), -100.0);

        dsp::BandCoefficients c;
        dsp::makeModelCoefficients (s.model, s.type, s.slope, (double) s.frequency, (double) s.q, (double) s.gainDb,
                                    (double) s.attenDb, sampleRate, c);
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
            auto frequency = (double) s.frequency;

            if (includeDynamics && s.dynamicsEnabled && dsp::supportsDynamics (s.type))
            {
                const auto dynamicDb = (double) processor.getBandDynamicGainDb (b);

                if (dsp::isCut (s.type)) // 0.9.0: the cutoff moves, as in the DSP
                    frequency = dsp::DynamicEQBand::dynamicCutFrequency (s.type, frequency, dynamicDb);
                else
                    gain += dynamicDb;
            }

            gain = juce::jlimit ((double) -dsp::DynamicEQBand::maxTotalGainDb,
                                 (double) dsp::DynamicEQBand::maxTotalGainDb, gain);

            model.prototypes[(size_t) b] = dsp::makeModelPrototype (s.model, s.type, s.slope, (double) s.q, gain, (double) s.attenDb);
            dsp::makeModelCoefficients (s.model, s.type, s.slope, frequency, (double) s.q, gain,
                                        (double) s.attenDb, model.sampleRate, model.coefficients[(size_t) b]);
            model.frequencies[(size_t) b] = frequency;
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

    ResponseCurveComponent::CutMarker ResponseCurveComponent::cutMarker (int bandIndex) const
    {
        CutMarker marker;

        if (bandIndex < 0 || bandIndex >= params::numBands)
            return marker;

        const auto s = processor.getBandSettings (bandIndex);

        if (! s.enabled || ! s.dynamicsEnabled || ! dsp::isCut (s.type) || std::abs (s.rangeDb) < 1.0e-3f)
            return marker;

        auto live = (double) processor.getBandDynamicGainDb (bandIndex);
        if (! std::isfinite (live))
            live = 0.0;

        marker.visible = true;
        marker.staticHz = (double) s.frequency;
        marker.liveHz = dsp::DynamicEQBand::dynamicCutFrequency (s.type, marker.staticHz, live);
        marker.limitHz = dsp::DynamicEQBand::dynamicCutFrequency (s.type, marker.staticHz, (double) s.rangeDb);
        return marker;
    }

    void ResponseCurveComponent::drawHoverRing (juce::Graphics& g, int b, float alpha)
    {
        const auto centre = handlePosition (b);
        const auto s = processor.getBandSettings (b);
        const auto sign = dsp::DynamicEQBand::upwardSign (s.type);
        const auto amount = sign * (double) s.rangeDb;
        const auto sweep = (float) hoverSweepRadians;
        const auto active = s.dynamicsEnabled;

        const auto arc = [&] (float from, float to)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, hoverRingRadius, hoverRingRadius, 0.0f, from, to, true);
            return p;
        };
        const juce::PathStrokeType stroke (hoverRingWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Dark backing so the ring reads over the analyser, then the +/-135 degree track.
        g.setColour (Palette::background.withAlpha (0.6f * alpha));
        g.strokePath (arc (-sweep, sweep), juce::PathStrokeType (hoverRingWidth + 3.0f, juce::PathStrokeType::curved,
                                                                  juce::PathStrokeType::rounded));
        g.setColour (Palette::textMuted.withAlpha ((active ? 0.45f : 0.28f) * alpha));
        g.strokePath (arc (-sweep, sweep), stroke);

        // Zero at 12 o'clock.
        g.setColour (Palette::textPrimary.withAlpha (0.7f * alpha));
        g.drawLine (centre.x, centre.y - hoverRingRadius - hoverRingWidth * 0.5f - 1.5f,
                    centre.x, centre.y - hoverRingRadius + hoverRingWidth * 0.5f + 1.5f, 1.2f);

        // The excursion: counter-clockwise = downward compression, clockwise = upward expansion.
        const auto angle = (float) angleForUpwardAmount (amount);

        if (std::abs (amount) > 0.05)
        {
            const auto colour = amount < 0.0 ? Palette::compression : Palette::expansion;
            g.setColour (colour.withAlpha ((active ? 1.0f : 0.45f) * alpha));
            g.strokePath (arc (std::min (0.0f, angle), std::max (0.0f, angle)), stroke);

            const auto tip = centre.getPointOnCircumference (hoverRingRadius, angle);
            g.setColour (Palette::textPrimary.withAlpha ((active ? 1.0f : 0.6f) * alpha));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (tip));
        }

        // What the band is doing right now (dynamics on).
        if (active)
        {
            const auto live = sign * (double) processor.getBandDynamicGainDb (b);

            if (std::abs (live) > 0.05)
            {
                const auto dot = centre.getPointOnCircumference (hoverRingRadius, (float) angleForUpwardAmount (live));
                g.setColour (bandColour (b).brighter (0.4f).withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
                g.setColour (Palette::background.withAlpha (alpha));
                g.drawEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot), 1.0f);
            }
        }

        // The amount, only while it is being dragged.
        if (ringBand == b && ringDragMode != 0)
        {
            const auto text = std::abs (amount) < 0.05 ? juce::String ("Range 0.0 dB")
                                                        : juce::String (amount < 0.0 ? "Downward " : "Upward ")
                                                              + juce::String (std::abs (amount), 1) + " dB";
            auto box = juce::Rectangle<float> (128.0f, 18.0f).withCentre (centre.translated (0.0f, hoverRingOuterHit + 12.0f));

            if (box.getBottom() > plotArea().getBottom())
                box = box.withCentre (centre.translated (0.0f, -hoverRingOuterHit - 12.0f));

            g.setColour (Palette::panelRaised.withAlpha (0.92f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (amount < -0.05 ? Palette::compression : (amount > 0.05 ? Palette::expansion : Palette::textPrimary));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    void ResponseCurveComponent::drawSoloBadge (juce::Graphics& g, int b, float alpha)
    {
        const auto badge = soloBadgeBounds (b);
        const auto soloed = processor.isBandSoloed (b);

        if (soloed)
        {
            g.setColour (Palette::solo.withAlpha (alpha));
            g.fillRoundedRectangle (badge, 4.5f);
            g.setColour (juce::Colours::white.withAlpha (alpha));
        }
        else
        {
            g.setColour (Palette::background.withAlpha (0.85f * alpha));
            g.fillRoundedRectangle (badge, 4.5f);
            g.setColour (Palette::solo.withAlpha (alpha));
            g.drawRoundedRectangle (badge.reduced (0.75f), 4.0f, 1.5f);
        }

        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("S", badge, juce::Justification::centred);
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

        // 0.9.1 Solo: dim the plot and light up each soloed band's region (its region filter's
        // magnitude per column), with the region filter's own response outlined.
        // One read of the Solo state per paint (it can change from the host mid-paint), so each
        // band's transparency layer is always begun and ended in pairs.
        std::array<bool, params::numBands> soloed {};
        for (int b = 0; b < params::numBands; ++b)
            soloed[(size_t) b] = processor.isBandSoloed (b);
        const auto anySolo = std::find (soloed.begin(), soloed.end(), true) != soloed.end();
        const auto dimmed = [&] (int b) { return anySolo && ! soloed[(size_t) b]; };

        if (anySolo)
        {
            g.saveState();
            g.reduceClipRegion (area.toNearestInt());
            g.setColour (Palette::background.withAlpha (0.38f));
            g.fillRect (area);

            const auto sampleRate = processor.getAnalysisSampleRate();

            for (int b = 0; b < params::numBands; ++b)
            {
                if (! soloed[(size_t) b])
                    continue;

                const auto region = processor.getSoloRegion (b);
                const auto colour = bandColour (b);
                juce::Path outline;

                for (auto x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const auto magnitude = dsp::soloMagnitudeAt (region, xToFrequency (x), sampleRate);
                    const auto db = 20.0 * std::log10 (std::max (magnitude, 1.0e-6));
                    const auto y = juce::jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, decibelsToY (db));

                    if (magnitude > 0.02)
                    {
                        g.setColour (colour.withAlpha ((float) (0.26 * magnitude)));
                        g.fillRect (x, area.getY(), 2.0f, area.getHeight());
                    }

                    if (x <= area.getX())
                        outline.startNewSubPath (x, y);
                    else
                        outline.lineTo (x, y);
                }

                const float dashes[] { 3.0f, 3.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (colour.withAlpha (0.8f));
                g.fillPath (dashed);
            }

            g.restoreState();
        }

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
                const auto colour = bandColour (run.band).withMultipliedAlpha (dimmed (run.band) ? 0.3f : 1.0f);
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

        // Dynamic cuts: static cutoff (dashed), full-Range limit (faint) and live cutoff with an arrow.
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto marker = cutMarker (b);

            if (! marker.visible)
                continue;

            const auto colour = bandColour (b).withMultipliedAlpha (dimmed (b) ? 0.3f : 1.0f);
            const auto selected = b == selectedBand;
            const auto staticX = frequencyToX (marker.staticHz);
            const auto liveX = frequencyToX (marker.liveHz);
            const auto limitX = frequencyToX (marker.limitHz);
            const auto top = area.getY() + 4.0f, bottom = area.getBottom() - 4.0f;
            const auto arrowY = area.getY() + 14.0f + (float) (b % 3) * 9.0f;

            const float dashes[] { 4.0f, 4.0f };
            g.setColour (colour.withAlpha (selected ? 0.75f : 0.45f));
            g.drawDashedLine ({ staticX, top, staticX, bottom }, dashes, 2, 1.2f);

            g.setColour (colour.withAlpha (selected ? 0.35f : 0.2f));
            g.drawLine (limitX, top, limitX, bottom, 1.0f);
            g.drawArrow ({ staticX, arrowY, limitX, arrowY }, 1.0f, 6.0f, 6.0f);

            if (std::abs (liveX - staticX) > 0.75f)
            {
                g.setColour (colour);
                g.drawLine (liveX, top, liveX, bottom, 1.8f);
                g.drawArrow ({ staticX, arrowY, liveX, arrowY }, 2.0f, 8.0f, 8.0f);
            }
        }

        g.restoreState();

        // 0.9.2: hover rings sit under the handles.
        for (int b = 0; b < params::numBands; ++b)
            if (const auto alpha = getHoverAlpha (b); alpha > 0.005f && hoverRingAvailable (b))
                drawHoverRing (g, b, alpha);

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            const auto circle = juce::Rectangle<float> (handleRadius * 2.0f, handleRadius * 2.0f)
                                    .withCentre (handlePosition (b));
            const auto colour = bandColour (b);

            // Solo: bands that are not soloed are drawn faded. The layer is clipped to what this
            // handle draws (circle, selection outline, Tilt / Q label) so it stays cheap.
            const auto dim = dimmed (b);

            if (dim)
            {
                const auto label = circle.translated (0.0f, -20.0f).withSizeKeepingCentre (90.0f, 14.0f);
                g.saveState();
                g.reduceClipRegion (circle.expanded (5.0f).getUnion (label.expanded (2.0f)).getSmallestIntegerContainer());
                g.beginTransparencyLayer (0.35f);
            }

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

            if (dim)
            {
                g.endTransparencyLayer();
                g.restoreState();
            }
        }

        // 0.9.2: the blue S (Solo) on top: always for soloed bands, fading with the ring on hover.
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto alpha = processor.isBandSoloed (b) ? 1.0f
                                                          : (processor.getBandSettings (b).enabled ? getHoverAlpha (b) : 0.0f);
            if (alpha > 0.005f)
                drawSoloBadge (g, b, alpha);
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
        const auto names = params::slopeNames();

        menu.addSectionHeader ("Band " + juce::String (bandIndex + 1) + " Slope");

        const auto slopeUsed = dsp::usesSlope (s.model);
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], slopeUsed, (int) s.slope == i);

        if (! slopeUsed)
            menu.addItem (999, "(Slope is set by the analogue model)", false, false);

        // 0.8.0: model and Stepped.
        menu.addSectionHeader ("Model");
        const auto models = params::modelNames();
        for (int m = 0; m < models.size(); ++m)
            menu.addItem (modelMenuBase + m, models[m], true, (int) s.model == m);

        const auto stepped = processor.getParameterState().getRawParameterValue (params::bandId (bandIndex, params::band::stepped));
        menu.addSeparator();
        menu.addItem (steppedMenuId, "Stepped (switched positions)", dsp::hasSteps (s.model, s.type),
                      stepped != nullptr && stepped->load() >= 0.5f);

        return menu;
    }

    void ResponseCurveComponent::handleBandMenuResult (int bandIndex, int result)
    {
        if (result >= modelMenuBase && result < modelMenuBase + dsp::numModels)
            setBandChoice (bandIndex, params::band::model, (float) (result - modelMenuBase));
        else if (result == steppedMenuId)
        {
            const auto* stepped = processor.getParameterState().getRawParameterValue (params::bandId (bandIndex, params::band::stepped));
            setBandChoice (bandIndex, params::band::stepped, stepped != nullptr && stepped->load() >= 0.5f ? 0.0f : 1.0f);
        }
        else if (result >= 1 && result <= dsp::numSlopes)
            setBandSlope (bandIndex, (dsp::Slope) (result - 1));
    }

    void ResponseCurveComponent::setBandChoice (int bandIndex, const char* suffix, float value)
    {
        if (auto* parameter = bandParameter (bandIndex, suffix))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }

        repaint();
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
                                                              safeThis->handleBandMenuResult (band, result);
                                                      });
            }

            return;
        }

        // 0.9.2: the blue S and the hover ring (handles keep priority, see hitAt).
        updateHover (event.position);
        ringBand = -1;
        soloPressBand = -1;

        {
            int band = -1;
            const auto hit = hitAt (event.position, &band);

            if (hit == Hit::soloBadge || hit == Hit::rangeRing)
            {
                draggingBand = -1;
                widthDragSide = 0;
                qDrag = false;

                // 0.9.3: the S is a click target only. Nothing toggles on the press: a release
                // without a drag toggles Solo (mouseUp); a drag goes to the ring under the S, if
                // there is one, so an always-visible soloed S never swallows a ring drag.
                // The S sits on its band's ring edge: a drag from it edits the ring under the
                // pointer (a neighbour's, if one is there), otherwise the S's own band's ring.
                auto ring = hit == Hit::rangeRing ? band : ringBandAt (event.position);

                if (ring < 0 && hit == Hit::soloBadge && hoverRingAvailable (band))
                    ring = band;

                if (hit == Hit::soloBadge)
                {
                    soloPressBand = band;
                    soloPressAlt = event.mods.isAltDown();
                    soloPressPosition = event.position;
                }

                if (ring >= 0)
                {
                    const auto s = processor.getBandSettings (ring);
                    ringBand = ring;
                    ringDragMode = 0;
                    ringDownPosition = ringLastPosition = event.position;
                    ringAmount = dsp::DynamicEQBand::upwardSign (s.type) * (double) s.rangeDb;
                    ringGesture = false;
                    ringEngagedDynamics = false;

                    if (hit == Hit::rangeRing && onBandSelected != nullptr)
                        onBandSelected (ring);
                }

                repaint();
                return;
            }
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
        // A press on the S that moves is a drag, not a click: it never toggles Solo.
        if (soloPressBand >= 0 && (event.mouseWasDraggedSinceMouseDown()
                                   || event.position.getDistanceFrom (soloPressPosition) > soloClickSlop))
            soloPressBand = -1;

        if (soloPressBand >= 0)
            return;

        if (ringBand >= 0)
        {
            const auto centre = handlePosition (ringBand);

            if (ringDragMode == 0)
            {
                const auto mode = RangeRing::chooseDragMode (centre, ringDownPosition, event.position);

                if (mode == RangeRing::DragMode::undecided)
                    return;

                ringDragMode = mode == RangeRing::DragMode::vertical ? 1 : 2;
                ringLastPosition = ringDownPosition; // the first few pixels count too

                if (auto* range = bandParameter (ringBand, params::band::range))
                {
                    range->beginChangeGesture();
                    ringGesture = true;
                }
            }

            const auto fine = event.mods.isShiftDown();
            const auto delta = ringDragMode == 1
                                   ? RangeRing::verticalDeltaDb ((double) (event.position.y - ringLastPosition.y), fine)
                                   : RangeRing::rotaryDeltaDb (RangeRing::clockwiseAngle (centre, ringLastPosition, event.position),
                                                               2.0 * hoverSweepRadians, fine);
            ringLastPosition = event.position;
            ringAmount = juce::jlimit (RangeRing::minRangeDb, RangeRing::maxRangeDb, ringAmount + delta);

            const auto s = processor.getBandSettings (ringBand);
            const auto rangeDb = dsp::DynamicEQBand::upwardSign (s.type) * ringAmount;

            // Off 0 switches Dynamic on; its gesture stays open until mouse-up, so the switch and
            // the Range change undo together. Back at 0 it stays on.
            if (! s.dynamicsEnabled && ! ringEngagedDynamics && std::abs (rangeDb) > 1.0e-3)
            {
                if (auto* dynamics = bandParameter (ringBand, params::band::dynamicsEnabled))
                {
                    dynamics->beginChangeGesture();
                    dynamics->setValueNotifyingHost (1.0f);
                    ringEngagedDynamics = true;
                }
            }

            setBandParameter (ringBand, params::band::range, (float) rangeDb);

            if (onBandSelected != nullptr)
                onBandSelected (ringBand);

            repaint();
            return;
        }

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

    void ResponseCurveComponent::endRingDrag()
    {
        if (ringBand < 0)
            return;

        // Dynamic's gesture opened inside the Range one: close it first, then the Range one,
        // so the outermost end commits the single undo step.
        if (ringEngagedDynamics)
            if (auto* dynamics = bandParameter (ringBand, params::band::dynamicsEnabled))
                dynamics->endChangeGesture();

        if (ringGesture)
            if (auto* range = bandParameter (ringBand, params::band::range))
                range->endChangeGesture();

        ringBand = -1;
        ringDragMode = 0;
        ringGesture = ringEngagedDynamics = false;
    }

    void ResponseCurveComponent::mouseUp (const juce::MouseEvent& event)
    {
        if (const auto band = std::exchange (soloPressBand, -1);
            band >= 0 && ! event.mouseWasDraggedSinceMouseDown()
                && event.position.getDistanceFrom (soloPressPosition) <= soloClickSlop)
        {
            if (onSoloClicked != nullptr)
                onSoloClicked (band, soloPressAlt);
            else if (auto* solo = bandParameter (band, params::band::solo))
            {
                solo->beginChangeGesture();
                solo->setValueNotifyingHost (solo->getValue() >= 0.5f ? 0.0f : 1.0f);
                solo->endChangeGesture();
            }

            repaint();
        }

        if (ringBand >= 0)
        {
            endRingDrag();
            updateHover (event.position);
            updateCursor (event.position);
            repaint();
            return;
        }

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
        // 0.9.2: double-click on the ring resets its band's Range to 0 (Dynamic stays as it is).
        if (int ring = -1; hitAt (event.position, &ring) == Hit::rangeRing)
        {
            endRingDrag();

            if (auto* range = bandParameter (ring, params::band::range))
            {
                range->beginChangeGesture();
                range->setValueNotifyingHost (range->convertTo0to1 (0.0f));
                range->endChangeGesture();
            }

            if (onBandSelected != nullptr)
                onBandSelected (ring);

            repaint();
            return;
        }

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
