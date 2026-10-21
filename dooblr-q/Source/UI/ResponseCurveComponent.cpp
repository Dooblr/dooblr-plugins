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
        // Redraw on parameter changes, on new analyzer frames, and continuously while a
        // dynamic band is moving.
        const auto newFrames = updateAnalyzer();

        if (repaintPending.exchange (false) || newFrames > 0 || anyDynamicsActive())
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

    float ResponseCurveComponent::spectrumDbToY (double decibels) const
    {
        const auto area = plotArea();
        return juce::jmap ((float) decibels, spectrumTopDb, spectrumBottomDb, area.getY(), area.getBottom());
    }

    juce::Path ResponseCurveComponent::makeSpectrumPath (const std::vector<float>& binDb, bool closed) const
    {
        const auto area = plotArea();
        const auto sampleRate = processor.getSpectrumAnalyzer().getSampleRate();
        const auto tilt = (double) processor.getAnalyzerViewSettings().tiltDbPerOctave;
        constexpr auto step = 2.0f;
        constexpr auto octaveSpan = 1.0 / 12.0;

        juce::Path path;

        for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
        {
            const auto frequency = xToFrequency (x);
            const auto db = (double) analysis::SpectrumAnalyzer::levelAt (binDb, frequency, sampleRate, octaveSpan)
                            + tilt * std::log2 (frequency / 1000.0);
            const auto y = juce::jlimit (area.getY(), area.getBottom(), spectrumDbToY (db));

            if (x <= area.getX())
                path.startNewSubPath (x, closed ? area.getBottom() : y);

            path.lineTo (x, y);
        }

        if (closed)
        {
            path.lineTo (area.getRight(), area.getBottom());
            path.closeSubPath();
        }

        return path;
    }

    void ResponseCurveComponent::drawSpectrum (juce::Graphics& g)
    {
        const auto& view = processor.getAnalyzerViewSettings();

        if (! view.visible)
            return;

        auto& analyzer = processor.getSpectrumAnalyzer();

        if (view.showPeak && (analyzer.isFrozen() || analyzer.getAccumulatedFrames() > 0))
        {
            // Peak-hold since the last Clear (or of the frozen capture while frozen).
            const auto& peak = analyzer.isFrozen() ? analyzer.getFrozenPeakDb() : analyzer.getPeakDb();
            juce::Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, makeSpectrumPath (peak, false), dashes, 2);
            g.setColour ((analyzer.isFrozen() ? Palette::frozen : Palette::spectrum).withAlpha (0.55f));
            g.fillPath (dashed);
        }

        if (analyzer.isFrozen())
        {
            const auto frozenPath = makeSpectrumPath (analyzer.getFrozenAverageDb(), true);
            g.setColour (Palette::frozen.withAlpha (0.05f));
            g.fillPath (frozenPath);
            g.setColour (Palette::frozen.withAlpha (0.75f));
            g.strokePath (makeSpectrumPath (analyzer.getFrozenAverageDb(), false), juce::PathStrokeType (1.5f));
        }

        const auto livePath = makeSpectrumPath (analyzer.getLiveDb(), true);
        g.setColour (Palette::spectrum.withAlpha (0.11f));
        g.fillPath (livePath);
        g.setColour (Palette::spectrum.withAlpha (0.30f));
        g.strokePath (makeSpectrumPath (analyzer.getLiveDb(), false), juce::PathStrokeType (1.0f));

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

    double ResponseCurveComponent::handleDecibels (const dsp::BandSettings& s, double sampleRate)
    {
        if (s.type == dsp::FilterType::notch)
            return 0.0;

        const auto c = dsp::makeEqCoefficients (s.type, (double) s.frequency, (double) s.q, (double) s.gainDb, sampleRate);
        return juce::Decibels::gainToDecibels (dsp::magnitudeAt (c, (double) s.frequency, sampleRate), -100.0);
    }

    juce::Point<float> ResponseCurveComponent::handlePosition (int bandIndex) const
    {
        const auto settings = processor.getBandSettings (bandIndex);
        const auto db = handleDecibels (settings, processor.getAnalysisSampleRate());
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

            model.coefficients[(size_t) b] = dsp::makeEqCoefficients (s.type, (double) s.frequency, (double) s.q,
                                                                      gain, model.sampleRate);
        }

        return model;
    }

    double ResponseCurveComponent::ResponseModel::decibelsAt (double frequency) const
    {
        auto magnitude = 1.0;

        for (size_t b = 0; b < coefficients.size(); ++b)
            if (enabled[b])
                magnitude *= dsp::magnitudeAt (coefficients[b], frequency, sampleRate);

        return juce::Decibels::gainToDecibels (magnitude, -100.0);
    }

    double ResponseCurveComponent::responseDecibelsAt (double frequency, bool includeDynamics) const
    {
        return makeResponseModel (includeDynamics).decibelsAt (frequency);
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

        const auto anyDynamic = anyDynamicsActive();
        const auto staticPath = makeResponsePath (false);

        juce::Path fill (staticPath);
        fill.lineTo (area.getRight(), decibelsToY (0.0));
        fill.lineTo (area.getX(), decibelsToY (0.0));
        fill.closeSubPath();
        g.setColour (Palette::accent.withAlpha (0.08f));
        g.fillPath (fill);

        g.setColour (anyDynamic ? Palette::textMuted : Palette::accent);
        g.strokePath (staticPath, juce::PathStrokeType (anyDynamic ? 1.2f : 2.0f));

        if (anyDynamic)
        {
            g.setColour (Palette::dynamic);
            g.strokePath (makeResponsePath (true), juce::PathStrokeType (2.0f));
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

            // A tilt's handle sits on its 0 dB pivot, so show the amount next to it.
            if (s.type == dsp::FilterType::tilt)
            {
                const auto text = "Tilt " + juce::String (s.gainDb >= 0.0f ? "+" : "") + juce::String (s.gainDb, 1) + " dB";
                g.setColour ((s.enabled ? colour : Palette::textMuted).withAlpha (0.9f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (text, circle.translated (0.0f, -20.0f).withSizeKeepingCentre (90.0f, 14.0f),
                            juce::Justification::centred);
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

    void ResponseCurveComponent::mouseDown (const juce::MouseEvent& event)
    {
        draggingBand = bandAt (event.position);
        dragEnabledBand = false;

        if (draggingBand < 0)
            return;

        dragOffset = handlePosition (draggingBand) - event.position;
        dragStartGainDb = processor.getBandSettings (draggingBand).gainDb;
        dragStartY = event.position.y;

        if (onBandSelected != nullptr)
            onBandSelected (draggingBand);

        for (const auto* suffix : { params::band::frequency, params::band::gain })
            if (auto* parameter = bandParameter (draggingBand, suffix))
                parameter->beginChangeGesture();
    }

    void ResponseCurveComponent::mouseDrag (const juce::MouseEvent& event)
    {
        if (draggingBand < 0 || ! event.mouseWasDraggedSinceMouseDown())
            return;

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

        const auto scale = handleGainScale (processor.getBandSettings (draggingBand).type);

        if (processor.getBandSettings (draggingBand).type == dsp::FilterType::tilt)
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

    void ResponseCurveComponent::mouseUp (const juce::MouseEvent&)
    {
        if (draggingBand < 0)
            return;

        for (const auto* suffix : { params::band::frequency, params::band::gain })
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
