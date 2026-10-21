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
        // Redraw on parameter changes, and continuously while a dynamic band is moving.
        if (repaintPending.exchange (false) || anyDynamicsActive())
            repaint();
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

    double ResponseCurveComponent::responseDecibelsAt (double frequency, bool includeDynamics) const
    {
        const auto sampleRate = processor.getAnalysisSampleRate();
        auto magnitude = 1.0;

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);

            if (! s.enabled)
                continue;

            auto gain = (double) s.gainDb;

            if (includeDynamics && s.dynamicsEnabled && dsp::supportsDynamics (s.type))
                gain += (double) processor.getBandDynamicGainDb (b);

            gain = juce::jlimit ((double) -dsp::DynamicEQBand::maxTotalGainDb,
                                 (double) dsp::DynamicEQBand::maxTotalGainDb, gain);

            const auto c = dsp::makeEqCoefficients (s.type, (double) s.frequency, (double) s.q, gain, sampleRate);
            magnitude *= dsp::magnitudeAt (c, frequency, sampleRate);
        }

        return juce::Decibels::gainToDecibels (magnitude, -100.0);
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
        juce::Path path;
        constexpr auto step = 2.0f;

        for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
        {
            const auto db = responseDecibelsAt (xToFrequency (x), includeDynamics);
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

        if (scale > 0.0)
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
