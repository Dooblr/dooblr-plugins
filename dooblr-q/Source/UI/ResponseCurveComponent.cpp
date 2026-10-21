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
    }

    ResponseCurveComponent::ResponseCurveComponent (DooblrQAudioProcessor& processorToDisplay)
        : processor (processorToDisplay)
    {
        setOpaque (true);
        startTimerHz (30);
    }

    ResponseCurveComponent::~ResponseCurveComponent()
    {
        stopTimer();
    }

    void ResponseCurveComponent::setSelectedBand (int bandIndex)
    {
        selectedBand = bandIndex;
        repaint();
    }

    void ResponseCurveComponent::timerCallback()
    {
        repaint();
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

    juce::Point<float> ResponseCurveComponent::handlePosition (int bandIndex) const
    {
        const auto settings = processor.getBandSettings (bandIndex);
        const auto db = dsp::hasGain (settings.type) ? (double) settings.gainDb : 0.0;
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
        const auto sampleRate = processor.getAnalysisSampleRate();

        std::array<dsp::SvfCoefficients, params::numBands> coefficients;
        std::array<bool, params::numBands> enabled {};

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            enabled[(size_t) b] = s.enabled;

            auto gain = (double) s.gainDb;

            if (includeDynamics && s.dynamicsEnabled && dsp::supportsDynamics (s.type))
                gain += (double) processor.getBandDynamicGainDb (b);

            gain = juce::jlimit ((double) -dsp::DynamicEQBand::maxTotalGainDb,
                                 (double) dsp::DynamicEQBand::maxTotalGainDb, gain);

            coefficients[(size_t) b] = dsp::makeEqCoefficients (s.type, (double) s.frequency, (double) s.q,
                                                                gain, sampleRate);
        }

        juce::Path path;
        const auto step = 2.0f;

        for (auto x = area.getX(); x <= area.getRight() + 0.5f; x += step)
        {
            const auto frequency = xToFrequency (x);
            auto magnitude = 1.0;

            for (int b = 0; b < params::numBands; ++b)
                if (enabled[(size_t) b])
                    magnitude *= dsp::magnitudeAt (coefficients[(size_t) b], frequency, sampleRate);

            const auto db = juce::Decibels::gainToDecibels (magnitude, -100.0);
            const auto y = juce::jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, decibelsToY (db));

            if (x <= area.getX())
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        return path;
    }

    void ResponseCurveComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panel);

        const auto area = plotArea();
        drawGrid (g);

        g.saveState();
        g.reduceClipRegion (area.toNearestInt().expanded (1));

        auto anyDynamic = false;

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            anyDynamic |= s.enabled && s.dynamicsEnabled && dsp::supportsDynamics (s.type);
        }

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
            const auto centre = handlePosition (b);
            const auto circle = juce::Rectangle<float> (handleRadius * 2.0f, handleRadius * 2.0f).withCentre (centre);
            const auto colour = bandColour (b);

            if (s.enabled)
            {
                g.setColour (colour.withAlpha (b == selectedBand ? 1.0f : 0.75f));
                g.fillEllipse (circle);
            }
            else
            {
                g.setColour (colour.withAlpha (0.45f));
                g.drawEllipse (circle, 1.2f);
            }

            if (b == selectedBand)
            {
                g.setColour (Palette::textPrimary);
                g.drawEllipse (circle.expanded (3.0f), 1.2f);
            }

            g.setColour (s.enabled ? Palette::background : colour.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
        }

        g.setColour (Palette::divider);
        g.drawRect (getLocalBounds());
    }

    //==============================================================================
    void ResponseCurveComponent::setParameter (int bandIndex, const char* suffix, float value)
    {
        if (auto* parameter = processor.getParameterState().getParameter (params::bandId (bandIndex, suffix)))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void ResponseCurveComponent::mouseDown (const juce::MouseEvent& event)
    {
        draggingBand = bandAt (event.position);

        if (draggingBand < 0)
            return;

        if (onBandSelected != nullptr)
            onBandSelected (draggingBand);

        auto& state = processor.getParameterState();

        for (const auto* suffix : { params::band::frequency, params::band::gain })
            if (auto* parameter = state.getParameter (params::bandId (draggingBand, suffix)))
                parameter->beginChangeGesture();
    }

    void ResponseCurveComponent::mouseDrag (const juce::MouseEvent& event)
    {
        if (draggingBand < 0)
            return;

        const auto frequency = juce::jlimit ((double) params::minFrequency, (double) params::maxFrequency,
                                             xToFrequency (event.position.x));
        setParameter (draggingBand, params::band::frequency, (float) frequency);

        if (dsp::hasGain (processor.getBandSettings (draggingBand).type))
        {
            const auto gain = juce::jlimit ((double) params::minGainDb, (double) params::maxGainDb,
                                            yToDecibels (event.position.y));
            setParameter (draggingBand, params::band::gain, (float) gain);
        }
    }

    void ResponseCurveComponent::mouseUp (const juce::MouseEvent&)
    {
        if (draggingBand < 0)
            return;

        auto& state = processor.getParameterState();

        for (const auto* suffix : { params::band::frequency, params::band::gain })
            if (auto* parameter = state.getParameter (params::bandId (draggingBand, suffix)))
                parameter->endChangeGesture();

        draggingBand = -1;
    }
}
