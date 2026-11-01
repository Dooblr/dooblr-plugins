#include "OutputVisuals.h"

#include <algorithm>
#include <cmath>

#include "../DSP/Effects.h"
#include "../Parameters.h"
#include "OutputMeter.h"
#include "Theme.h"

namespace dooblrvocals::ui
{
    namespace
    {
        constexpr float silentDb = -119.0f;

        /** Physical pixels per logical pixel where the component is shown (cached images). */
        float pixelScaleFor (const juce::Component& c)
        {
            auto scale = juce::Component::getApproximateScaleFactorForComponent (&c);

            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (c.getScreenBounds()))
                scale *= (float) display->scale;

            return juce::jlimit (1.0f, 4.0f, scale);
        }

        juce::Image makeImage (juce::Rectangle<int> size, float scale)
        {
            return { juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) size.getWidth() * scale)),
                     juce::jmax (1, juce::roundToInt ((float) size.getHeight() * scale)), true, juce::NativeImageType() };
        }

        juce::String formatDb (float db, int decimals = 1)
        {
            if (db <= silentDb)
                return "-inf dB";

            const auto rounded = std::abs (db) < 0.05f ? 0.0f : db;
            return (rounded > 0.0f ? "+" : "") + juce::String (rounded, decimals) + " dB";
        }

        juce::uint64 mixHash (juce::uint64 hash, float value)
        {
            return hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (value * 1000.0f);
        }

        void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                        juce::Justification justification, juce::Colour colour, float size = 10.0f)
        {
            g.setFont (juce::FontOptions (size));
            g.setColour (colour);
            g.drawText (text, area, justification, false);
        }
    }

    //==============================================================================
    void ParameterGesture::begin (std::initializer_list<const char*> ids)
    {
        end();

        for (const auto* id : ids)
            if (auto* p = param (id))
            {
                p->beginChangeGesture();
                active.push_back (p);
            }
    }

    void ParameterGesture::end()
    {
        for (auto* p : active)
            p->endChangeGesture();

        active.clear();
    }

    juce::RangedAudioParameter* ParameterGesture::param (const char* id) const
    {
        return state.getParameter (id);
    }

    void ParameterGesture::set (const char* id, float plainValue)
    {
        if (auto* p = param (id))
        {
            const auto normalised = p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plainValue));

            if (std::abs (normalised - p->getValue()) > 1.0e-7f)
                p->setValueNotifyingHost (normalised);
        }
    }

    float ParameterGesture::get (const char* id) const
    {
        if (auto* value = state.getRawParameterValue (id))
            return value->load();

        return 0.0f;
    }

    //==============================================================================
    void HistoryStrip::setRange (float newMin, float newMax, std::initializer_list<float> gridDb)
    {
        minDb = newMin;
        maxDb = newMax;
        grid.assign (gridDb.begin(), gridDb.end());
    }

    void HistoryStrip::setBounds (juce::Rectangle<int> area, float pixelScale)
    {
        if (area == bounds && std::abs (pixelScale - scale) < 1.0e-3f && image.isValid())
            return;

        bounds = area;
        scale = pixelScale;
        image = area.isEmpty() ? juce::Image() : makeImage (area, scale);
        clear();
    }

    void HistoryStrip::clear()
    {
        numPending = 0;
        last = {};
        silentColumns = 1 << 30;

        if (! image.isValid())
            return;

        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setColour (Palette::background);
        g.fillRect (0, 0, bounds.getWidth(), bounds.getHeight());
        g.setColour (Palette::grid);

        for (auto db : grid)
            g.fillRect (0.0f, std::floor (dbToY (db) - (float) bounds.getY()), (float) bounds.getWidth(), 1.0f);
    }

    void HistoryStrip::push (const Column& column) noexcept
    {
        if (numPending == (int) pending.size())
        {
            std::move (pending.begin() + 1, pending.end(), pending.begin());
            --numPending;
        }

        pending[(size_t) numPending++] = column;
    }

    float HistoryStrip::dbToY (float db) const noexcept
    {
        const auto t = (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb);
        return (float) bounds.getY() + t * (float) bounds.getHeight();
    }

    float HistoryStrip::yToDb (float y) const noexcept
    {
        const auto t = (y - (float) bounds.getY()) / (float) juce::jmax (1, bounds.getHeight());
        return maxDb - t * (maxDb - minDb);
    }

    bool HistoryStrip::flush()
    {
        if (numPending == 0 || ! image.isValid())
            return false;

        const auto width = bounds.getWidth();
        const auto count = juce::jmin (numPending, width);
        const auto first = numPending - count;
        auto silent = true;

        for (int i = first; i < numPending; ++i)
        {
            const auto& c = pending[(size_t) i];
            silent = silent && c.fillDb <= silentDb && c.lineDb <= silentDb && c.secondaryDb <= silentDb && c.grDb > -0.05f;
        }

        if (silent && silentColumns >= width)
        {
            numPending = 0;
            last = pending[(size_t) (first + count - 1)];
            return false; // the visible history is silence already: nothing moves
        }

        const auto shift = juce::roundToInt ((float) count * scale);

        if (shift < image.getWidth())
            image.moveImageSection (0, 0, shift, 0, image.getWidth() - shift, image.getHeight());

        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        auto previous = first > 0 ? pending[(size_t) first - 1] : last;

        for (int i = 0; i < count; ++i)
        {
            const auto& c = pending[(size_t) (first + i)];
            drawColumn (g, width - count + i, c, previous);
            const auto columnSilent = c.fillDb <= silentDb && c.lineDb <= silentDb && c.secondaryDb <= silentDb && c.grDb > -0.05f;
            silentColumns = columnSilent ? juce::jmin (1 << 30, silentColumns + 1) : 0;
            previous = c;
        }

        last = previous;
        numPending = 0;
        return true;
    }

    void HistoryStrip::drawColumn (juce::Graphics& g, int x, const Column& c, const Column& p) const
    {
        const auto h = (float) bounds.getHeight();
        const auto top = (float) bounds.getY();
        const auto fx = (float) x;
        auto y = [this, top] (float db) { return dbToY (db) - top; };

        g.setColour (Palette::background);
        g.fillRect (fx, 0.0f, 1.0f, h);
        g.setColour (Palette::grid);

        for (auto db : grid)
            g.fillRect (fx, std::floor (y (db)), 1.0f, 1.0f);

        if (c.fillDb > minDb)
        {
            const auto yf = y (c.fillDb);
            g.setColour (colours.fill);
            g.fillRect (fx, yf, 1.0f, h - yf);
        }

        auto segment = [&] (float a, float b, float thickness)
        {
            const auto y0 = std::min (a, b), y1 = std::max (a, b);
            g.fillRect (fx, y0 - thickness * 0.5f, 1.0f, y1 - y0 + thickness);
        };

        if (c.secondaryDb > minDb || p.secondaryDb > minDb)
        {
            g.setColour (colours.secondary);
            segment (y (p.secondaryDb), y (c.secondaryDb), 1.0f);
        }

        if (c.lineDb > minDb || p.lineDb > minDb)
        {
            g.setColour (colours.line);
            segment (y (p.lineDb), y (c.lineDb), 1.6f);
        }

        const auto range = maxDb - minDb;
        const auto grY = juce::jlimit (0.0f, h, -c.grDb / range * h);
        const auto previousGrY = juce::jlimit (0.0f, h, -p.grDb / range * h);

        if (grY > 0.25f)
        {
            g.setColour (colours.gr.withMultipliedAlpha (0.35f));
            g.fillRect (fx, 0.0f, 1.0f, grY);
        }

        if (grY > 0.25f || previousGrY > 0.25f)
        {
            g.setColour (colours.gr);
            segment (previousGrY, grY, 1.6f);
        }
    }

    void HistoryStrip::draw (juce::Graphics& g) const
    {
        if (image.isValid())
            g.drawImage (image, bounds.toFloat());
    }

    //==============================================================================
    const std::array<EqDisplay::BandIds, EqDisplay::numBands> EqDisplay::bandIds {
        BandIds { "eq_low_freq", "eq_low_gain", nullptr },
        BandIds { "eq_mid_freq", "eq_mid_gain", "eq_mid_q" },
        BandIds { "eq_high_freq", "eq_high_gain", nullptr },
    };

    namespace
    {
        constexpr float minHz = 20.0f, maxHz = 20000.0f, gainRange = 24.0f, spectrumTopDb = 0.0f, spectrumBottomDb = -96.0f;
        const char* bandNames[] { "Low", "Mid", "High" };
    }

    EqDisplay::EqDisplay (juce::AudioProcessorValueTreeState& s) : gesture (s)
    {
        setOpaque (true);
        setComponentID ("eqDisplay");
        peakHoldButton.setComponentID ("eqPeakHold");
        peakHoldButton.setTooltip ("Hold the loudest level per frequency (switch off to clear)");
        peakHoldButton.onClick = [this]
        {
            if (! peakHoldButton.getToggleState())
                analyzer.clearPeakHold();

            spectrumDirty = true;
            rebuildSpectrum();
            repaint();
        };
    }

    void EqDisplay::reset()
    {
        analyzer.reset();
        analyzer.clearPeakHold();
        spectrumDirty = true;
        rebuildSpectrum();
        repaint();
    }

    float EqDisplay::freqToX (float hz) const noexcept
    {
        return plot.getX() + plot.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
    }

    float EqDisplay::xToFreq (float x) const noexcept
    {
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth())));
    }

    float EqDisplay::gainToY (float db) const noexcept
    {
        return plot.getCentreY() - db / gainRange * plot.getHeight() * 0.5f;
    }

    float EqDisplay::yToGain (float y) const noexcept
    {
        return (plot.getCentreY() - y) / juce::jmax (1.0f, plot.getHeight() * 0.5f) * gainRange;
    }

    float EqDisplay::spectrumDbToY (float db) const noexcept
    {
        const auto t = (spectrumTopDb - juce::jlimit (spectrumBottomDb, spectrumTopDb, db)) / (spectrumTopDb - spectrumBottomDb);
        return plot.getY() + t * plot.getHeight();
    }

    juce::Point<float> EqDisplay::getHandlePosition (int band) const
    {
        const auto& ids = bandIds[(size_t) band];
        return { freqToX (gesture.get (ids.freq)), gainToY (gesture.get (ids.gain)) };
    }

    int EqDisplay::hitBand (juce::Point<float> p) const
    {
        auto best = -1;
        auto bestDistance = 12.0f;

        for (int b = 0; b < numBands; ++b)
        {
            const auto distance = getHandlePosition (b).getDistanceFrom (p);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = b;
            }
        }

        return best;
    }

    juce::uint64 EqDisplay::paramHash() const
    {
        juce::uint64 hash = 17;

        for (const auto* id : { "eq_on", "eq_low_freq", "eq_low_gain", "eq_mid_freq", "eq_mid_gain", "eq_mid_q", "eq_high_freq", "eq_high_gain",
                                "eq_hpf_on", "eq_hpf_freq", "eq_hpf_slope", "eq_low_dyn_on", "eq_mid_dyn_on", "eq_high_dyn_on" })
            hash = mixHash (hash, gesture.get (id));

        return hash;
    }

    void EqDisplay::resized()
    {
        plot = getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (30.0f).withTrimmedTop (6.0f).withTrimmedBottom (18.0f);
        const auto columns = juce::jmax (2, (int) plot.getWidth() + 1);
        columnFreqs.resize ((size_t) columns);
        columnLevels.resize ((size_t) columns);

        for (int i = 0; i < columns; ++i)
            columnFreqs[(size_t) i] = xToFreq (plot.getX() + (float) i);

        rebuildBackground();
        rebuildCurve();
        spectrumDirty = true;
        rebuildSpectrum();
    }

    void EqDisplay::rebuildBackground()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;

        const auto scale = pixelScaleFor (*this);
        background = makeImage (getLocalBounds(), scale);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (Palette::background);

        g.setColour (Palette::grid);
        const float freqs[] { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

        for (auto f : freqs)
        {
            const auto x = std::round (freqToX (f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            drawLabel (g, f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                       { x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f }, juce::Justification::centred, Palette::textMuted);
        }

        for (auto db : { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f })
        {
            const auto y = std::round (gainToY (db));
            g.setColour (db == 0.0f ? Palette::divider : Palette::grid);
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            drawLabel (g, (db > 0 ? "+" : "") + juce::String ((int) db), { 0.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f },
                       juce::Justification::centredRight, Palette::textMuted);
        }

        for (auto db : { 0.0f, -24.0f, -48.0f, -72.0f, -96.0f })
            drawLabel (g, juce::String ((int) db), { plot.getRight() + 3.0f, juce::jlimit (0.0f, (float) getHeight() - 14.0f, spectrumDbToY (db) - 7.0f), 27.0f, 14.0f },
                       juce::Justification::centredLeft, Palette::spectrum.withAlpha (0.55f), 9.0f);

        drawLabel (g, "dB", { 0.0f, (float) getHeight() - 16.0f, plot.getX() - 6.0f, 14.0f }, juce::Justification::centredRight, Palette::textMuted, 9.0f);
        drawLabel (g, "dBFS", { plot.getRight() + 1.0f, (float) getHeight() - 16.0f, 29.0f, 14.0f }, juce::Justification::centredLeft,
                   Palette::spectrum.withAlpha (0.55f), 9.0f);
    }

    void EqDisplay::rebuildCurve()
    {
        lastHash = paramHash();
        const auto rate = analyzer.getSampleRate();
        dsp::Biquad lowFilter, midFilter, highFilter;
        lowFilter.setParameters (dsp::Biquad::Type::lowShelf, gesture.get ("eq_low_freq"), 0.707, gesture.get ("eq_low_gain"), rate);
        midFilter.setParameters (dsp::Biquad::Type::peak, gesture.get ("eq_mid_freq"), gesture.get ("eq_mid_q"), gesture.get ("eq_mid_gain"), rate);
        highFilter.setParameters (dsp::Biquad::Type::highShelf, gesture.get ("eq_high_freq"), 0.707, gesture.get ("eq_high_gain"), rate);
        const auto hpfOn = gesture.get ("eq_hpf_on") > 0.5f;
        dsp::HighPass highPass;
        highPass.setParameters (gesture.get ("eq_hpf_freq"), juce::roundToInt (gesture.get ("eq_hpf_slope")), rate);

        curveLine.clear();
        curveFill.clear();
        curveFill.startNewSubPath (plot.getX(), gainToY (0.0f));

        for (size_t i = 0; i < columnFreqs.size(); ++i)
        {
            const auto f = (double) columnFreqs[i];
            const auto magnitude = lowFilter.magnitudeAt (f, rate) * midFilter.magnitudeAt (f, rate) * highFilter.magnitudeAt (f, rate)
                                 * (hpfOn ? highPass.magnitudeAt (f, rate) : 1.0);
            const auto db = (float) (20.0 * std::log10 (std::max (1.0e-9, magnitude)));
            const auto point = juce::Point<float> (plot.getX() + (float) i, gainToY (juce::jlimit (-30.0f, 30.0f, db)));

            if (i == 0) curveLine.startNewSubPath (point);
            else        curveLine.lineTo (point);

            curveFill.lineTo (point);
        }

        curveFill.lineTo (plot.getX() + (float) columnFreqs.size() - 1.0f, gainToY (0.0f));
        curveFill.closeSubPath();

        // Live dynamic response (as dooblr-q draws it): the static curve with each dynamic band's
        // current gain offset, shaded against the static curve.
        dynamicLine.clear();
        dynamicFill.clear();
        const bool dynOn[] { gesture.get ("eq_low_dyn_on") > 0.5f, gesture.get ("eq_mid_dyn_on") > 0.5f, gesture.get ("eq_high_dyn_on") > 0.5f };

        if (! (dynOn[0] || dynOn[1] || dynOn[2]))
            return;

        const auto withDynamic = [this] (const char* gainId, int band)
        {
            return juce::jlimit (-dsp::eqdynamics::maxTotalGainDb, dsp::eqdynamics::maxTotalGainDb, gesture.get (gainId) + shownDynamicDb[(size_t) band]);
        };
        dsp::Biquad lowLive, midLive, highLive;
        lowLive.setParameters (dsp::Biquad::Type::lowShelf, gesture.get ("eq_low_freq"), 0.707, withDynamic ("eq_low_gain", 0), rate);
        midLive.setParameters (dsp::Biquad::Type::peak, gesture.get ("eq_mid_freq"), gesture.get ("eq_mid_q"), withDynamic ("eq_mid_gain", 1), rate);
        highLive.setParameters (dsp::Biquad::Type::highShelf, gesture.get ("eq_high_freq"), 0.707, withDynamic ("eq_high_gain", 2), rate);
        std::vector<juce::Point<float>> staticPoints;
        staticPoints.reserve (columnFreqs.size());

        for (size_t i = 0; i < columnFreqs.size(); ++i)
        {
            const auto f = (double) columnFreqs[i];
            const auto hp = hpfOn ? highPass.magnitudeAt (f, rate) : 1.0;
            const auto live = lowLive.magnitudeAt (f, rate) * midLive.magnitudeAt (f, rate) * highLive.magnitudeAt (f, rate) * hp;
            const auto still = lowFilter.magnitudeAt (f, rate) * midFilter.magnitudeAt (f, rate) * highFilter.magnitudeAt (f, rate) * hp;
            const auto toY = [this] (double m) { return gainToY (juce::jlimit (-30.0f, 30.0f, (float) (20.0 * std::log10 (std::max (1.0e-9, m))))); };
            const auto point = juce::Point<float> (plot.getX() + (float) i, toY (live));
            staticPoints.push_back ({ point.x, toY (still) });

            if (i == 0) { dynamicLine.startNewSubPath (point); dynamicFill.startNewSubPath (point); }
            else        { dynamicLine.lineTo (point); dynamicFill.lineTo (point); }
        }

        for (auto it = staticPoints.rbegin(); it != staticPoints.rend(); ++it)
            dynamicFill.lineTo (*it);
        dynamicFill.closeSubPath();
    }

    void EqDisplay::rebuildSpectrum()
    {
        spectrumDirty = false;
        spectrumFill.clear();
        spectrumLine.clear();
        peakLine.clear();

        if (columnFreqs.size() < 2)
            return;

        const auto span = std::log2 (columnFreqs[1] / columnFreqs[0]);
        const auto& live = analyzer.getLiveDb();

        if (! analyzer.isSilent())
        {
            spectrumFill.startNewSubPath (plot.getX(), plot.getBottom());

            for (size_t i = 0; i < columnFreqs.size(); ++i)
            {
                const auto point = juce::Point<float> (plot.getX() + (float) i, spectrumDbToY (analyzer.levelAt (live, columnFreqs[i], span)));
                spectrumFill.lineTo (point);

                if (i == 0) spectrumLine.startNewSubPath (point);
                else        spectrumLine.lineTo (point);
            }

            spectrumFill.lineTo (plot.getX() + (float) columnFreqs.size() - 1.0f, plot.getBottom());
            spectrumFill.closeSubPath();
        }

        if (peakHoldButton.getToggleState())
        {
            const auto& peak = analyzer.getPeakDb();

            for (size_t i = 0; i < columnFreqs.size(); ++i)
            {
                const auto point = juce::Point<float> (plot.getX() + (float) i, spectrumDbToY (analyzer.levelAt (peak, columnFreqs[i], span)));

                if (i == 0) peakLine.startNewSubPath (point);
                else        peakLine.lineTo (point);
            }
        }
    }

    void EqDisplay::update (const float* samples, int numSamples, double sampleRate, double elapsedSeconds)
    {
        auto changed = false;

        if (sampleRate > 0.0 && std::abs (sampleRate - analyzer.getSampleRate()) > 0.5)
        {
            analyzer.setSampleRate (sampleRate);
            analyzer.reset();
            rebuildCurve();
            changed = true;
        }

        const auto wasSilent = analyzer.isSilent();
        const auto frames = numSamples > 0 ? analyzer.push (samples, numSamples) : 0;
        analyzer.decay (elapsedSeconds);

        if (frames > 0 || ! wasSilent || spectrumDirty)
        {
            rebuildSpectrum();
            changed = true;
        }

        // Live dynamic EQ gain: redraw the dynamic curve when any band has moved by 0.05 dB or more.
        auto dynamicMoved = false;
        for (int band = 0; band < numBands; ++band)
        {
            const auto live = dynamicGainSource != nullptr ? dynamicGainSource (band) : 0.0f;
            if (std::abs (live - shownDynamicDb[(size_t) band]) >= 0.05f || (live == 0.0f && shownDynamicDb[(size_t) band] != 0.0f))
            {
                shownDynamicDb[(size_t) band] = live;
                dynamicMoved = true;
            }
        }

        if (paramHash() != lastHash || dynamicMoved)
        {
            rebuildCurve();
            updateReadout (dragBand >= 0 ? dragBand : hoverBand);
            changed = true;
        }

        if (changed)
            repaint();
    }

    void EqDisplay::updateReadout (int band)
    {
        if (band < 0)
        {
            readout.clear();
            return;
        }

        const auto& ids = bandIds[(size_t) band];
        readout = juce::String (bandNames[band]) + "  " + params::formatHz (gesture.get (ids.freq)) + "  " + formatDb (gesture.get (ids.gain));

        if (ids.q != nullptr)
            readout << "  Q " << juce::String (gesture.get (ids.q), 2);
    }

    void EqDisplay::paint (juce::Graphics& g)
    {
        if (background.isValid())
            g.drawImage (background, getLocalBounds().toFloat());
        else
            g.fillAll (Palette::background);

        const auto on = gesture.get ("eq_on") > 0.5f;

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());

            if (! spectrumFill.isEmpty())
            {
                g.setColour (Palette::spectrum.withAlpha (0.16f));
                g.fillPath (spectrumFill);
                g.setColour (Palette::spectrum.withAlpha (0.55f));
                g.strokePath (spectrumLine, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            if (! peakLine.isEmpty())
            {
                g.setColour (Palette::frozen.withAlpha (0.6f));
                g.strokePath (peakLine, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const auto colour = on ? Palette::accent : Palette::textMuted.withAlpha (0.6f);
            g.setColour (colour.withAlpha (on ? 0.12f : 0.06f));
            g.fillPath (curveFill);
            g.setColour (colour);
            g.strokePath (curveLine, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (on && ! dynamicLine.isEmpty())
            {
                g.setColour (Palette::dynamic.withAlpha (0.18f));
                g.fillPath (dynamicFill);
                g.setColour (Palette::dynamic);
                g.strokePath (dynamicLine, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto p = getHandlePosition (b);
            const auto active = b == hoverBand || b == dragBand;
            const auto colour = on ? bandColour (b) : Palette::textMuted;
            const auto radius = active ? 8.0f : 7.0f;
            g.setColour (Palette::background);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 3.0f, radius * 2.0f + 3.0f).withCentre (p));
            g.setColour (active ? Palette::textPrimary : colour);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
            drawLabel (g, juce::String::charToString (bandNames[b][0]), juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p),
                       juce::Justification::centred, Palette::background, 10.0f);
        }

        const auto text = plot.reduced (8.0f, 5.0f);

        if (readout.isNotEmpty())
            drawLabel (g, readout, text, juce::Justification::topLeft, Palette::textPrimary, 11.0f);
    }

    void EqDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto band = hitBand (e.position);

        if (band != hoverBand)
        {
            hoverBand = band;
            updateReadout (band);
            setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void EqDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (dragBand >= 0)
            return;

        setMouseCursor (juce::MouseCursor::NormalCursor);

        if (hoverBand >= 0)
        {
            hoverBand = -1;
            readout.clear();
            repaint();
        }
    }

    namespace
    {
        juce::Point<float> grabOffset;
    }

    void EqDisplay::mouseDown (const juce::MouseEvent& e)
    {
        dragBand = hitBand (e.position);

        if (dragBand < 0)
            return;

        const auto& ids = bandIds[(size_t) dragBand];
        altDrag = e.mods.isAltDown() && ids.q != nullptr;
        dragStartQ = ids.q != nullptr ? gesture.get (ids.q) : 1.0f;
        grabOffset = getHandlePosition (dragBand) - e.position;

        // Editing never switches the EQ on: that is the EQ tab's power toggle.
        if (altDrag) gesture.begin ({ ids.q });
        else         gesture.begin ({ ids.freq, ids.gain });

        hoverBand = dragBand;
        updateReadout (dragBand);
        repaint();
    }

    void EqDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragBand < 0)
            return;

        const auto& ids = bandIds[(size_t) dragBand];

        if (altDrag)
        {
            // Up = narrower (higher Q): one octave of Q per 48 px.
            gesture.set (ids.q, dragStartQ * std::exp2 (-(float) e.getDistanceFromDragStartY() / 48.0f));
        }
        else
        {
            const auto target = e.position + grabOffset;
            gesture.set (ids.freq, xToFreq (target.x));
            gesture.set (ids.gain, juce::jlimit (-gainRange, gainRange, yToGain (target.y)));
        }

        rebuildCurve();
        updateReadout (dragBand);
        repaint();
    }

    void EqDisplay::mouseUp (const juce::MouseEvent& e)
    {
        gesture.end();
        dragBand = -1;
        hoverBand = getLocalBounds().toFloat().contains (e.position) ? hitBand (e.position) : -1;
        updateReadout (hoverBand);
        setMouseCursor (hoverBand >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto band = hitBand (e.position);

        if (band < 0)
            return;

        gesture.begin ({ bandIds[(size_t) band].gain });
        gesture.set (bandIds[(size_t) band].gain, 0.0f);
        gesture.end();
        rebuildCurve();
        updateReadout (band);
        repaint();
    }

    void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const auto band = hitBand (e.position);

        if (band < 0 || bandIds[(size_t) band].q == nullptr)
        {
            Component::mouseWheelMove (e, wheel); // shelves have a fixed Q: let the host / parent scroll
            return;
        }

        const auto* qId = bandIds[(size_t) band].q;
        gesture.begin ({ qId });
        gesture.set (qId, gesture.get (qId) * std::exp2 (wheel.deltaY * 2.0f));
        gesture.end();
        rebuildCurve();
        updateReadout (band);
        repaint();
    }

    //==============================================================================
    namespace
    {
        constexpr float dynLowDb = -60.0f, dynHighDb = 6.0f;
    }

    DynamicsDisplay::DynamicsDisplay (juce::AudioProcessorValueTreeState& s) : gesture (s)
    {
        setOpaque (true);
        setComponentID ("dynamicsDisplay");
        history.setRange (dynLowDb, dynHighDb, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f });
        history.setColours ({ Palette::spectrum.withAlpha (0.30f), Palette::accent, Palette::textMuted.withAlpha (0.8f), Palette::dynamic });
    }

    void DynamicsDisplay::reset()
    {
        history.clear();
        shownInDb = -120.0f;
        shownGrDb = 0.0f;
        repaint();
    }

    float DynamicsDisplay::dbToTransferX (float db) const noexcept
    {
        return transfer.getX() + (db - dynLowDb) / (dynHighDb - dynLowDb) * transfer.getWidth();
    }

    float DynamicsDisplay::dbToTransferY (float db) const noexcept
    {
        return transfer.getBottom() - (db - dynLowDb) / (dynHighDb - dynLowDb) * transfer.getHeight();
    }

    float DynamicsDisplay::transferXToDb (float x) const noexcept
    {
        return dynLowDb + (x - transfer.getX()) / juce::jmax (1.0f, transfer.getWidth()) * (dynHighDb - dynLowDb);
    }

    float DynamicsDisplay::transferYToDb (float y) const noexcept
    {
        return dynLowDb + (transfer.getBottom() - y) / juce::jmax (1.0f, transfer.getHeight()) * (dynHighDb - dynLowDb);
    }

    juce::Point<float> DynamicsDisplay::transferPoint (float inDb, float outDb) const noexcept
    {
        return { dbToTransferX (inDb), dbToTransferY (outDb) };
    }

    juce::Point<float> DynamicsDisplay::getThresholdHandlePosition() const
    {
        const auto t = gesture.get ("comp_threshold");
        return transferPoint (t, t + dsp::Compressor::staticReductionDb (t, t, gesture.get ("comp_ratio"), gesture.get ("comp_knee"))
                                  + gesture.get ("comp_makeup"));
    }

    juce::uint64 DynamicsDisplay::paramHash() const
    {
        juce::uint64 hash = 31;

        for (const auto* id : { "comp_on", "comp_threshold", "comp_ratio", "comp_makeup", "comp_knee" })
            hash = mixHash (hash, gesture.get (id));

        return hash;
    }

    void DynamicsDisplay::resized()
    {
        auto area = getLocalBounds();
        const auto side = juce::jmax (60, juce::jmin (area.getHeight() - 18, area.getWidth() * 2 / 5));
        auto right = area.removeFromRight (side + 30);
        transfer = right.removeFromTop (side + 4).withTrimmedLeft (30).withTrimmedTop (4).toFloat();
        area.removeFromRight (14);
        history.setBounds (area.withTrimmedLeft (30).withTrimmedTop (4).withTrimmedBottom (14), pixelScaleFor (*this));
        rebuildBackground();
        rebuildCurve();
    }

    void DynamicsDisplay::rebuildBackground()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;

        const auto scale = pixelScaleFor (*this);
        background = makeImage (getLocalBounds(), scale);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));
        drawBackgroundLayer (g);
    }

    void DynamicsDisplay::drawBackgroundLayer (juce::Graphics& g) const
    {
        g.fillAll (Palette::background);

        const auto h = history.getBounds().toFloat();

        for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
            drawLabel (g, juce::String ((int) db), { 0.0f, history.dbToY (db) - 7.0f, h.getX() - 5.0f, 14.0f },
                       juce::Justification::centredRight, Palette::textMuted);

        drawLabel (g, "dBFS", { 0.0f, h.getBottom() - 12.0f, h.getX() - 5.0f, 14.0f }, juce::Justification::centredRight, Palette::textMuted, 9.0f);
        // No help caption under the history (design system section 7).

        g.setColour (Palette::panel);
        g.fillRect (transfer);

        for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (db == 0.0f ? Palette::divider : Palette::grid);
            g.fillRect (std::round (dbToTransferX (db)), transfer.getY(), 1.0f, transfer.getHeight());
            g.fillRect (transfer.getX(), std::round (dbToTransferY (db)), transfer.getWidth(), 1.0f);
            drawLabel (g, juce::String ((int) db), { transfer.getX() - 29.0f, dbToTransferY (db) - 7.0f, 25.0f, 14.0f },
                       juce::Justification::centredRight, Palette::textMuted, 9.0f);
            drawLabel (g, juce::String ((int) db), { dbToTransferX (db) - 14.0f, transfer.getBottom() + 1.0f, 28.0f, 12.0f },
                       juce::Justification::centred, Palette::textMuted, 9.0f);
        }

        // Unity line.
        juce::Path unity;
        unity.startNewSubPath (transferPoint (dynLowDb, dynLowDb));
        unity.lineTo (transferPoint (dynHighDb, dynHighDb));
        g.setColour (Palette::textMuted.withAlpha (0.35f));
        const float dashes[] { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (unity, unity, dashes, 2);
        g.fillPath (unity);
    }

    void DynamicsDisplay::rebuildCurve()
    {
        lastHash = paramHash();
        const auto threshold = gesture.get ("comp_threshold"), ratio = gesture.get ("comp_ratio"), makeup = gesture.get ("comp_makeup");
        const auto knee = gesture.get ("comp_knee");
        curve.clear();
        kneeCurve.clear();

        // The knee region (threshold +/- knee / 2) is drawn again on top, brighter, so its width is visible.
        if (knee > 0.0f)
            for (int step = 0; step <= 32; ++step)
            {
                const auto in = threshold - 0.5f * knee + knee * (float) step / 32.0f;
                const auto point = transferPoint (in, in + dsp::Compressor::staticReductionDb (in, threshold, ratio, knee) + makeup);

                if (step == 0) kneeCurve.startNewSubPath (point);
                else           kneeCurve.lineTo (point);
            }

        for (int step = 0; step <= (int) std::lround ((dynHighDb - dynLowDb) / 0.5f); ++step)
        {
            const auto in = dynLowDb + 0.5f * (float) step;
            const auto point = transferPoint (in, in + dsp::Compressor::staticReductionDb (in, threshold, ratio, knee) + makeup);

            if (step == 0) curve.startNewSubPath (point);
            else           curve.lineTo (point);
        }
    }

    juce::Rectangle<int> DynamicsDisplay::dotBounds() const
    {
        if (shownInDb < dynLowDb)
            return {};

        const auto p = transferPoint (shownInDb, shownInDb + shownGrDb + gesture.get ("comp_makeup"));
        return juce::Rectangle<float> (14.0f, 14.0f).withCentre (p).getSmallestIntegerContainer();
    }

    void DynamicsDisplay::update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds)
    {
        auto batchIn = -120.0f, batchGr = 0.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto& f = frames[i];
            history.push ({ f.compressorInDb, f.dynamicsOutDb, f.dynamicsInDb, f.compressorGrDb });
            batchIn = std::max (batchIn, f.compressorInDb);
            batchGr = std::min (batchGr, f.compressorGrDb);
        }

        framesReceived += numFrames;

        if (history.flush())
            repaint (history.getBounds());

        // Dot ballistics: instant attack, 40 dB/s release.
        const auto oldDot = dotBounds();
        const auto release = (float) (40.0 * elapsedSeconds);
        shownInDb = std::max (batchIn, shownInDb - release);
        shownGrDb = std::min (batchGr, std::min (0.0f, shownGrDb + release));

        if (paramHash() != lastHash)
        {
            rebuildCurve();
            repaint (transfer.getSmallestIntegerContainer().expanded (8));
            repaint (history.getBounds());
        }

        const auto newDot = dotBounds();

        if (newDot != oldDot)
            repaint (oldDot.getUnion (newDot));

        auto text = gesture.get ("comp_on") > 0.5f ? "GR " + formatDb (shownGrDb > -0.05f ? 0.0f : shownGrDb) : juce::String ("Compressor off");

        if (text != readout)
        {
            readout = text;
            repaint (transfer.getSmallestIntegerContainer().withHeight (34));
        }
    }

    void DynamicsDisplay::paint (juce::Graphics& g)
    {
        if (background.isValid())
            g.drawImage (background, getLocalBounds().toFloat());
        else
            g.fillAll (Palette::background);

        history.draw (g);

        const auto on = gesture.get ("comp_on") > 0.5f;
        const auto threshold = gesture.get ("comp_threshold");
        const auto h = history.getBounds().toFloat();
        const auto active = hover == DragKind::thresholdLine || dragging == DragKind::thresholdLine;

        // Threshold on the history (compressor input level).
        const auto ty = history.dbToY (threshold);
        g.setColour ((on ? Palette::dynamic : Palette::textMuted).withAlpha (active ? 1.0f : 0.75f));
        g.fillRect (h.getX(), ty - (active ? 1.0f : 0.5f), h.getWidth(), active ? 2.0f : 1.0f);
        drawLabel (g, "threshold " + formatDb (threshold), { h.getRight() - 130.0f, ty - 15.0f, 126.0f, 14.0f },
                   juce::Justification::centredRight, on ? Palette::dynamic : Palette::textMuted);

        // Legend.
        auto legend = juce::Rectangle<float> (h.getX() + 6.0f, h.getBottom() - 16.0f, 70.0f, 14.0f);
        struct LegendItem { const char* name; juce::Colour colour; float advance; };
        const LegendItem legendItems[] = { { "comp in", Palette::spectrum, 56.0f }, { "out", Palette::accent, 56.0f },
                                           { "pre-shaper", Palette::textMuted, 74.0f }, { "GR", Palette::dynamic, 56.0f } };
        for (const auto& [name, colour, advance] : legendItems)
        {
            g.setColour (colour);
            g.fillRect (legend.getX(), legend.getCentreY() - 1.0f, 8.0f, 2.0f);
            drawLabel (g, name, legend.withTrimmedLeft (11.0f), juce::Justification::centredLeft, Palette::textMuted, 9.5f);
            legend.translate (advance, 0.0f);
        }

        // Transfer curve.
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (transfer.toNearestInt());
            g.setColour (on ? Palette::dynamic : Palette::textMuted.withAlpha (0.6f));
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (! kneeCurve.isEmpty())
            {
                g.setColour ((on ? Palette::textPrimary : Palette::textMuted).withAlpha (0.6f));
                g.strokePath (kneeCurve, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const auto handle = getThresholdHandlePosition();
            const auto handleActive = hover == DragKind::thresholdHandle || dragging == DragKind::thresholdHandle;
            g.setColour (handleActive ? Palette::textPrimary : (on ? Palette::dynamic : Palette::textMuted));
            g.fillEllipse (juce::Rectangle<float> (handleActive ? 12.0f : 10.0f, handleActive ? 12.0f : 10.0f).withCentre (handle));

            if (shownInDb >= dynLowDb)
            {
                const auto p = transferPoint (shownInDb, shownInDb + shownGrDb + gesture.get ("comp_makeup"));
                g.setColour (Palette::textPrimary.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p));
                g.setColour (Palette::textPrimary);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
            }
        }

        const auto t = transfer.reduced (6.0f, 4.0f);
        drawLabel (g, readout, t, juce::Justification::topLeft, on ? Palette::textPrimary : Palette::textMuted, 11.0f);
        drawLabel (g, on ? juce::String (gesture.get ("comp_ratio"), 1) + ":1  knee " + juce::String (juce::roundToInt (gesture.get ("comp_knee"))) + " dB"
                      : juce::String ("off (Dynamics tab)"),
                   t.withTrimmedTop (14.0f), juce::Justification::topLeft, Palette::textMuted, 10.0f);
        drawLabel (g, "in", { transfer.getRight() - 20.0f, transfer.getBottom() - 15.0f, 16.0f, 12.0f }, juce::Justification::centredRight,
                   Palette::textMuted, 9.0f);
    }

    DynamicsDisplay::DragKind DynamicsDisplay::hitTestDrag (juce::Point<float> p) const
    {
        if (getThresholdHandlePosition().getDistanceFrom (p) < 11.0f)
            return DragKind::thresholdHandle;

        const auto h = history.getBounds().toFloat();

        if (h.contains (p) && std::abs (p.y - history.dbToY (gesture.get ("comp_threshold"))) < 7.0f)
            return DragKind::thresholdLine;

        if (transfer.contains (p) && p.x > getThresholdHandlePosition().x + 8.0f)
            return DragKind::ratio;

        return DragKind::none;
    }

    void DynamicsDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto kind = hitTestDrag (e.position);

        if (kind != hover)
        {
            hover = kind;
            setMouseCursor (kind == DragKind::thresholdLine ? juce::MouseCursor::UpDownResizeCursor
                            : kind == DragKind::thresholdHandle ? juce::MouseCursor::LeftRightResizeCursor
                            : kind == DragKind::ratio ? juce::MouseCursor::DraggingHandCursor
                                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void DynamicsDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (dragging != DragKind::none)
            return;

        setMouseCursor (juce::MouseCursor::NormalCursor);

        if (hover != DragKind::none)
        {
            hover = DragKind::none;
            repaint();
        }
    }

    namespace
    {
        float dynamicsGrab = 0.0f;
    }

    void DynamicsDisplay::startDrag (DragKind kind)
    {
        dragging = kind;

        if (kind == DragKind::none)
            return;

        // Editing never switches the compressor on: that is the Dynamics tab's power toggle.
        gesture.begin ({ kind == DragKind::ratio ? "comp_ratio" : "comp_threshold" });
    }

    void DynamicsDisplay::mouseDown (const juce::MouseEvent& e)
    {
        const auto kind = hitTestDrag (e.position);
        dynamicsGrab = kind == DragKind::thresholdLine ? history.dbToY (gesture.get ("comp_threshold")) - e.position.y
                     : kind == DragKind::thresholdHandle ? getThresholdHandlePosition().x - e.position.x : 0.0f;
        startDrag (kind);
        repaint();
    }

    void DynamicsDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        switch (dragging)
        {
            case DragKind::thresholdLine:
                gesture.set ("comp_threshold", juce::jlimit (-48.0f, 0.0f, history.yToDb (e.position.y + dynamicsGrab)));
                break;

            case DragKind::thresholdHandle:
                gesture.set ("comp_threshold", juce::jlimit (-48.0f, 0.0f, transferXToDb (e.position.x + dynamicsGrab)));
                break;

            case DragKind::ratio:
            {
                // The curve above the knee passes through the mouse: out = T + (in - T) / ratio.
                const auto threshold = gesture.get ("comp_threshold");
                const auto in = transferXToDb (e.position.x);
                const auto out = transferYToDb (e.position.y) - gesture.get ("comp_makeup");

                if (in > threshold + 1.0f)
                    gesture.set ("comp_ratio", out <= threshold ? 20.0f : juce::jlimit (1.0f, 20.0f, (in - threshold) / std::max (0.05f, out - threshold)));
                break;
            }

            case DragKind::none:
                return;
        }

        rebuildCurve();
        repaint();
    }

    void DynamicsDisplay::mouseUp (const juce::MouseEvent& e)
    {
        gesture.end();
        dragging = DragKind::none;
        hover = DragKind::none; // let mouseMove pick the hover and its cursor afresh
        if (getLocalBounds().toFloat().contains (e.position))
            mouseMove (e);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    //==============================================================================
    namespace
    {
        constexpr float limLowDb = -36.0f, limHighDb = 12.0f;
    }

    LimiterMeter::LimiterMeter()
    {
        setComponentID ("limiterMeter");
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
    }

    void LimiterMeter::reset()
    {
        level = hold = minDb;
        gr = 0.0f;
        holdAge = 0.0;
        repaint();
    }

    void LimiterMeter::setCeiling (float ceilingDb, bool limiterOn)
    {
        ceiling = ceilingDb;
        on = limiterOn;
    }

    void LimiterMeter::push (float outDb, float grDb, double elapsedSeconds)
    {
        // Same ballistics as the header's output meter.
        const auto dt = juce::jlimit (0.0, 0.25, elapsedSeconds);
        outDb = juce::jlimit (minDb, maxDb, outDb);
        level = std::max (outDb, std::max (minDb, level - (float) (OutputMeter::releaseDbPerSecond * dt)));

        holdAge += dt * 1000.0;
        if (outDb >= hold)
        {
            hold = outDb;
            holdAge = 0.0;
        }
        else if (holdAge > OutputMeter::holdMs)
        {
            hold = std::max (level, std::max (minDb, hold - (float) (OutputMeter::holdDecayDbPerSecond * dt)));
        }

        gr = std::min (std::min (0.0f, grDb), std::min (0.0f, gr + (float) (12.0 * dt)));
        if (gr > -0.05f)
            gr = 0.0f;

        const auto h = pictureHash();
        if (h != lastHash)
        {
            lastHash = h;
            repaint();
        }
    }

    int LimiterMeter::pictureHash() const
    {
        // Quantised to half-pixels of the bars, so an unchanged picture never repaints.
        const auto bar = getLevelBar();
        const auto px = [&bar] (float db) { return juce::roundToInt ((db - minDb) / (maxDb - minDb) * bar.getHeight() * 2.0f); };
        return (((px (level) * 4099 + px (hold)) * 4099 + juce::roundToInt (gr * 8.0f)) * 4099 + px (ceiling)) * 2 + (on ? 1 : 0);
    }

    juce::Rectangle<float> LimiterMeter::getLevelBar() const
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (20.0f);
        return { r.getX() + 24.0f, r.getY(), 24.0f, r.getHeight() };
    }

    juce::Rectangle<float> LimiterMeter::getGrBar() const
    {
        const auto l = getLevelBar();
        return { l.getRight() + 10.0f, l.getY(), 12.0f, l.getHeight() };
    }

    float LimiterMeter::dbToY (float db) const
    {
        const auto bar = getLevelBar();
        return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, bar.getBottom(), bar.getY());
    }

    void LimiterMeter::resized()
    {
        rebuildStaticLayer();
    }

    void LimiterMeter::rebuildStaticLayer()
    {
        staticLayer = {};
        if (getWidth() <= 0 || getHeight() <= 0)
            return;

        const auto scale = pixelScaleFor (*this);
        staticLayer = makeImage (getLocalBounds(), scale);
        staticScale = scale;
        juce::Graphics g (staticLayer);
        g.addTransform (juce::AffineTransform::scale (scale));
        drawStaticLayer (g);
    }

    void LimiterMeter::drawStaticLayer (juce::Graphics& g) const
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        const auto bar = getLevelBar();
        const auto grBar = getGrBar();

        // dB scale (left of the level bar) and gain-reduction scale (right of the GR bar).
        for (auto db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f })
        {
            const auto y = dbToY (db);
            g.setColour (Palette::grid);
            g.fillRect (bar.getX() - 4.0f, y - 0.5f, 3.0f, 1.0f);
            drawLabel (g, (db > 0 ? "+" : "") + juce::String ((int) db), { bar.getX() - 32.0f, y - 6.0f, 27.0f, 12.0f },
                       juce::Justification::centredRight, Palette::textMuted, 9.5f);
        }
        for (auto grDb : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
        {
            const auto y = grBar.getY() + grBar.getHeight() * grDb / grRangeDb;
            g.setColour (Palette::grid);
            g.fillRect (grBar.getRight() + 1.0f, y - 0.5f, 3.0f, 1.0f);
            drawLabel (g, juce::String ((int) grDb), { grBar.getRight() + 5.0f, y - 6.0f, 20.0f, 12.0f },
                       juce::Justification::centredLeft, Palette::textMuted, 9.0f);
        }

        // The empty bars and their captions.
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bar, 2.0f);
        g.fillRoundedRectangle (grBar, 2.0f);
        drawLabel (g, "OUT", { bar.getX() - 8.0f, bar.getBottom() + 4.0f, bar.getWidth() + 16.0f, 12.0f },
                   juce::Justification::centred, Palette::textMuted, 9.0f);
        drawLabel (g, "GR", { grBar.getX() - 8.0f, bar.getBottom() + 4.0f, grBar.getWidth() + 16.0f, 12.0f },
                   juce::Justification::centred, Palette::textMuted, 9.0f);
    }

    void LimiterMeter::paint (juce::Graphics& g)
    {
        // The scales, empty bars and captions come from the image cached on resize.
        if (! staticLayer.isValid() || ! juce::approximatelyEqual (staticScale, pixelScaleFor (*this)))
            rebuildStaticLayer();
        if (staticLayer.isValid())
            g.drawImage (staticLayer, getLocalBounds().toFloat());

        const auto bar = getLevelBar();
        const auto grBar = getGrBar();

        if (level > minDb + 0.01f)
        {
            // The lit part as solid runs: each pixel row takes its zone colour (green, a short blend
            // into yellow at -6 dBFS, into red at 0) and equal neighbours merge into one rect, so the
            // usual picture is a handful of fills and nothing is allocated.
            const auto top = dbToY (level);
            const auto left = bar.getX() + 1.0f, right = bar.getRight() - 1.0f;
            const auto colourAt = [&] (float rowTop, float rowBottom)
            {
                const auto midY = (rowTop + rowBottom) * 0.5f;
                return OutputMeter::colourFor (juce::jmap (midY, bar.getBottom(), bar.getY(), minDb, maxDb));
            };

            auto runBottom = bar.getBottom();
            auto runColour = colourAt (juce::jmax (top, runBottom - 1.0f), runBottom);

            for (auto y = bar.getBottom(); y > top; y -= 1.0f)
            {
                const auto rowTop = juce::jmax (top, y - 1.0f);
                const auto c = colourAt (rowTop, y);
                if (c != runColour)
                {
                    g.setColour (runColour);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, y, right, runBottom));
                    runBottom = y;
                    runColour = c;
                }
            }
            g.setColour (runColour);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, top, right, runBottom));
        }

        g.setColour (Palette::textMuted.withAlpha (0.5f));
        g.fillRect (bar.getX(), dbToY (0.0f) - 0.5f, bar.getWidth(), 1.0f);

        if (hold > minDb + 0.5f)
        {
            g.setColour (OutputMeter::colourFor (hold).brighter (0.2f));
            g.fillRect (bar.getX(), dbToY (hold) - 1.0f, bar.getWidth(), 2.0f);
        }

        // Ceiling across the level bar (dashed when the limiter is off).
        const auto cy = dbToY (ceiling);
        const auto ceilingColour = on ? Palette::frozen : Palette::textMuted;
        if (on)
        {
            g.setColour (ceilingColour);
            g.fillRect (bar.getX() - 3.0f, cy - 0.75f, bar.getWidth() + 6.0f, 1.5f);
        }
        else
        {
            g.setColour (ceilingColour.withAlpha (0.7f));
            for (auto x = bar.getX() - 3.0f; x < bar.getRight() + 3.0f; x += 6.0f)
                g.fillRect (x, cy - 0.5f, 3.0f, 1.0f);
        }

        // Gain reduction from the top.
        if (on && gr < 0.0f)
        {
            const auto depth = grBar.getHeight() * juce::jmin (1.0f, -gr / grRangeDb);
            g.setColour (Palette::dynamic);
            g.fillRoundedRectangle (grBar.withHeight (std::max (1.5f, depth)).reduced (1.0f, 0.0f), 1.5f);
        }

        // Peak-hold and GR values above the bars (the captions below are in the static layer).
        drawLabel (g, hold > minDb + 0.5f ? formatDb (hold) : juce::String ("-inf"), { bar.getX() - 14.0f, 4.0f, bar.getWidth() + 28.0f, 14.0f },
                   juce::Justification::centred, Palette::accent, 10.0f);
        drawLabel (g, on ? juce::String (gr, 1) : juce::String ("-"), { grBar.getX() - 8.0f, 4.0f, grBar.getWidth() + 22.0f, 14.0f },
                   juce::Justification::centred, on ? Palette::dynamic : Palette::textMuted, 10.0f);
    }

    //==============================================================================
    LimiterDisplay::LimiterDisplay (juce::AudioProcessorValueTreeState& s) : gesture (s)
    {
        setOpaque (true);
        setComponentID ("limiterDisplay");
        history.setRange (limLowDb, limHighDb, { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f });
        history.setColours ({ Palette::spectrum.withAlpha (0.30f), Palette::accent, Palette::textMuted, Palette::dynamic });
        addAndMakeVisible (meter);
        updateReadouts();
    }

    void LimiterDisplay::reset()
    {
        history.clear();
        meter.reset();
        shownGrDb = 0.0f;
        shownOutDb = -120.0f;
        updateReadouts();
        repaint();
    }

    juce::uint64 LimiterDisplay::paramHash() const
    {
        return mixHash (mixHash (7, gesture.get ("lim_mode")), gesture.get ("lim_ceiling"));
    }

    float LimiterDisplay::getCeilingY() const
    {
        return history.dbToY (gesture.get ("lim_ceiling"));
    }

    void LimiterDisplay::resized()
    {
        auto area = getLocalBounds();
        readoutArea = area.removeFromRight (112).withTrimmedLeft (12);
        meter.setBounds (readoutArea);
        history.setBounds (area.withTrimmedLeft (30).withTrimmedTop (4).withTrimmedBottom (14), pixelScaleFor (*this));
        rebuildBackground();
    }

    void LimiterDisplay::rebuildBackground()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;

        const auto scale = pixelScaleFor (*this);
        background = makeImage (getLocalBounds(), scale);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));
        drawBackgroundLayer (g);
    }

    void LimiterDisplay::drawBackgroundLayer (juce::Graphics& g) const
    {
        g.fillAll (Palette::background);
        const auto h = history.getBounds().toFloat();

        for (auto db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
            drawLabel (g, (db > 0 ? "+" : "") + juce::String ((int) db), { 0.0f, history.dbToY (db) - 7.0f, h.getX() - 5.0f, 14.0f },
                       juce::Justification::centredRight, Palette::textMuted);

        drawLabel (g, "dBFS", { 0.0f, h.getBottom() - 12.0f, h.getX() - 5.0f, 14.0f }, juce::Justification::centredRight, Palette::textMuted, 9.0f);
        // No help caption under the history (design system section 7): the legend names the traces.
    }

    void LimiterDisplay::updateReadouts()
    {
        const auto mode = juce::roundToInt (gesture.get ("lim_mode"));
        const auto ceilingDb = gesture.get ("lim_ceiling");
        const auto text = formatDb (ceilingDb);
        meter.setCeiling (ceilingDb, mode != 0);
        if (text != ceilingText)
        {
            ceilingText = text;
            meter.repaint();
        }
    }

    void LimiterDisplay::update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds)
    {
        auto batchGr = 0.0f, batchOut = -120.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto& f = frames[i];
            history.push ({ f.limiterInDb, f.limiterOutDb, -120.0f, f.limiterGrDb });
            batchGr = std::min (batchGr, f.limiterGrDb);
            batchOut = std::max (batchOut, f.limiterOutDb);
        }

        framesReceived += numFrames;

        if (history.flush())
            repaint (history.getBounds());

        // Readout ballistics: instant, then 1 s hold-ish release (12 dB/s GR, 20 dB/s peak).
        shownGrDb = std::min (batchGr, std::min (0.0f, shownGrDb + (float) (12.0 * elapsedSeconds)));
        shownOutDb = std::max (batchOut, shownOutDb - (float) (20.0 * elapsedSeconds));

        if (paramHash() != lastHash)
        {
            lastHash = paramHash();
            repaint (history.getBounds().expanded (2));
        }

        updateReadouts();
        meter.push (batchOut, batchGr, elapsedSeconds);
    }

    void LimiterDisplay::paint (juce::Graphics& g)
    {
        if (background.isValid())
            g.drawImage (background, getLocalBounds().toFloat());
        else
            g.fillAll (Palette::background);

        history.draw (g);

        const auto mode = juce::roundToInt (gesture.get ("lim_mode"));
        const auto on = mode != 0;
        const auto h = history.getBounds().toFloat();
        const auto y = getCeilingY();
        const auto active = hover || dragging;
        const auto colour = on ? Palette::frozen : Palette::textMuted;

        if (on)
        {
            g.setColour (colour.withAlpha (active ? 1.0f : 0.85f));
            g.fillRect (h.getX(), y - (active ? 1.0f : 0.75f), h.getWidth(), active ? 2.0f : 1.5f);
        }
        else
        {
            g.setColour (colour.withAlpha (0.7f));
            for (auto x = h.getX(); x < h.getRight(); x += 8.0f)
                g.fillRect (x, y - 0.5f, 4.0f, 1.0f);
        }

        drawLabel (g, (on ? "ceiling " : "ceiling (limiter off) ") + ceilingText,
                   { h.getRight() - 260.0f, y - 15.0f, 256.0f, 14.0f }, juce::Justification::centredRight, colour);

        auto legend = juce::Rectangle<float> (h.getX() + 6.0f, h.getBottom() - 16.0f, 60.0f, 14.0f);
        for (const auto& [name, c] : { std::pair { "in", Palette::spectrum }, std::pair { "out", Palette::accent },
                                       std::pair { "GR", Palette::dynamic } })
        {
            g.setColour (c);
            g.fillRect (legend.getX(), legend.getCentreY() - 1.0f, 8.0f, 2.0f);
            drawLabel (g, name, legend.withTrimmedLeft (11.0f), juce::Justification::centredLeft, Palette::textMuted, 9.5f);
            legend.translate (44.0f, 0.0f);
        }
    }

    void LimiterDisplay::mouseMove (const juce::MouseEvent& e)
    {
        const auto over = history.getBounds().toFloat().expanded (0.0f, 6.0f).contains (e.position) && std::abs (e.position.y - getCeilingY()) < 7.0f;

        if (over != hover)
        {
            hover = over;
            setMouseCursor (over ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            repaint (history.getBounds().expanded (2));
        }
    }

    void LimiterDisplay::mouseExit (const juce::MouseEvent&)
    {
        if (dragging)
            return;

        setMouseCursor (juce::MouseCursor::NormalCursor);

        if (hover)
        {
            hover = false;
            repaint (history.getBounds().expanded (2));
        }
    }

    namespace
    {
        float limiterGrab = 0.0f;
    }

    void LimiterDisplay::mouseDown (const juce::MouseEvent& e)
    {
        mouseMove (e);

        if (! hover)
            return;

        dragging = true;
        limiterGrab = getCeilingY() - e.position.y;
        // Editing never switches the limiter on: that is the Limiter tab's power toggle.
        gesture.begin ({ "lim_ceiling" });

        updateReadouts();
        repaint();
    }

    void LimiterDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        gesture.set ("lim_ceiling", juce::jlimit (-24.0f, 0.0f, history.yToDb (e.position.y + limiterGrab)));
        updateReadouts();
        repaint (history.getBounds().expanded (2, 16));
    }

    void LimiterDisplay::mouseUp (const juce::MouseEvent& e)
    {
        gesture.end();
        dragging = false;
        hover = false;
        if (getLocalBounds().toFloat().contains (e.position))
            mouseMove (e);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}
