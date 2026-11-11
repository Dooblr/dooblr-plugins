#include "BoardControls.h"

#include <cmath>
#include <functional>

namespace dooblrm::ui
{
    namespace
    {
        constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.15f;
        constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.85f;
        constexpr int slideThreshold = 4;

        void forEachBatToggle (juce::Component& root, const std::function<void (BatToggle&)>& fn)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* t = dynamic_cast<BatToggle*> (&c))
                    fn (*t);
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    walk (*c.getChildComponent (i));
            };
            walk (root);
        }

        void styleCaption (juce::Label& label, const juce::String& text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::FontOptions (11.0f));
            label.setColour (juce::Label::textColourId, Palette::textMuted);
            label.setJustificationType (juce::Justification::centred);
            label.setInterceptsMouseClicks (false, false);
            // JUCE's default 5 px side padding ellipsizes "Mod Mix" in a ~36 px cell.
            label.setBorderSize ({ 0, 1, 0, 1 });
            label.setMinimumHorizontalScale (1.0f);
        }
    }

    //==============================================================================
    SectionPanel::SectionPanel (juce::String titleToUse, juce::Colour accentToUse)
        : title (std::move (titleToUse)), accent (accentToUse)
    {
        setInterceptsMouseClicks (false, true);
    }

    void SectionPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (accent);
        g.fillRect (bounds.getX() + 8.0f, bounds.getY() + 6.0f, 3.0f, 10.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().removeFromTop (titleHeight).withTrimmedLeft (16),
                    juce::Justification::centredLeft, false);
    }

    juce::Rectangle<int> SectionPanel::contentBounds() const
    {
        return getLocalBounds().reduced (6, 4).withTrimmedTop (titleHeight);
    }

    //==============================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                const juce::String& caption, juce::Colour fill)
    {
        setComponentID (parameterId);
        slider.setComponentID (parameterId);
        slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
        slider.getProperties().set ("maxKnobDiameter", 46.0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, fill);
        if (auto* p = state.getParameter (parameterId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
        styleCaption (captionLabel, caption);
        addAndMakeVisible (slider);
        addAndMakeVisible (captionLabel);
    }

    void Knob::resized()
    {
        auto bounds = getLocalBounds();
        captionLabel.setBounds (bounds.removeFromBottom (16));
        slider.setBounds (bounds);
    }

    //==============================================================================
    RotarySwitch::RotarySwitch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                                const juce::String& caption, const juce::StringArray& tickLabels)
        : ticks (tickLabels)
    {
        setComponentID (parameterId);
        slider.setComponentID (parameterId);
        slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
        slider.getProperties().set ("maxKnobDiameter", 46.0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, Palette::spectrum);
        slider.setSliderSnapsToMousePosition (true);
        if (auto* p = state.getParameter (parameterId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
        styleCaption (captionLabel, caption);
        addAndMakeVisible (slider);
        addAndMakeVisible (captionLabel);
    }

    juce::Font RotarySwitch::tickFont() const
    {
        return juce::Font (juce::FontOptions (ticks.size() <= 2 ? 9.0f : 8.0f));
    }

    std::vector<RotarySwitch::TickLabel> RotarySwitch::makeTickLayout() const
    {
        std::vector<TickLabel> out;
        const auto clip = getLocalBounds().withTrimmedBottom (captionHeight).toFloat().reduced (1.0f);
        if (clip.getWidth() < 8.0f || clip.getHeight() < 8.0f || ticks.isEmpty())
            return out;

        const auto font = tickFont();
        const auto centre = clip.getCentre();
        const int n = ticks.size();

        // Two positions (Priority Low/Last, noise White/Pink): sit at 9 o'clock / 3 o'clock
        // so they cannot fall into the caption under the knob.
        if (n == 2)
        {
            const auto h = 11.0f;
            const auto y = centre.y - h * 0.5f;
            for (int i = 0; i < 2; ++i)
            {
                const auto w = juce::jmax (8.0f, juce::GlyphArrangement::getStringWidth (font, ticks[i]) + 1.0f);
                auto box = juce::Rectangle<float> (w, h);
                if (i == 0)
                    box.setPosition (clip.getX(), y);
                else
                    box.setPosition (clip.getRight() - w, y);
                out.push_back ({ ticks[i], box.constrainedWithin (clip) });
            }
            return out;
        }

        const auto knobR = juce::jmin (clip.getWidth(), clip.getHeight()) * 0.28f;
        const auto labelR = knobR + 9.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto t = n == 1 ? 0.5f : (float) i / (float) (n - 1);
            const auto mathAngle = rotaryStart + t * (rotaryEnd - rotaryStart) - juce::MathConstants<float>::halfPi;
            const auto p = centre + juce::Point<float> (std::cos (mathAngle), std::sin (mathAngle)) * labelR;
            const auto w = juce::jmax (8.0f, juce::GlyphArrangement::getStringWidth (font, ticks[i]) + 1.0f);
            auto box = juce::Rectangle<float> (w, 10.0f);
            // Side ticks grow toward the knob so "Shk" / "2'" / "PW" stay inside the cell.
            if (std::abs (p.x - centre.x) < 6.0f)
                box.setCentre (p);
            else if (p.x < centre.x)
                box.setPosition (p.x, p.y - 5.0f);
            else
                box.setPosition (p.x - w, p.y - 5.0f);
            out.push_back ({ ticks[i], box.constrainedWithin (clip) });
        }
        return out;
    }

    void RotarySwitch::resized()
    {
        auto bounds = getLocalBounds();
        captionLabel.setBounds (bounds.removeFromBottom (captionHeight));
        const auto insetX = ticks.size() <= 2 ? 20 : 10;
        const auto insetY = ticks.size() <= 2 ? 2 : 8;
        slider.setBounds (bounds.reduced (insetX, insetY));
        const auto knobBox = slider.getBounds();
        slider.getProperties().set ("maxKnobDiameter",
            (double) juce::jmin (34, juce::jmin (knobBox.getWidth(), knobBox.getHeight()) - 2));
        tickLayout = makeTickLayout();
    }

    void RotarySwitch::paint (juce::Graphics& g)
    {
        if (tickLayout.empty())
            tickLayout = makeTickLayout();

        g.setFont (tickFont());
        g.setColour (Palette::textMuted);
        for (const auto& tick : tickLayout)
            g.drawText (tick.text, tick.bounds, juce::Justification::centred, false);
    }

    //==============================================================================
    BatToggle::BatToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                          const juce::String& caption)
    {
        setComponentID (parameterId);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        parameter = state.getParameter (parameterId);
        button.setComponentID (parameterId);
        button.setButtonText ({});
        button.setClickingTogglesState (true);
        button.setInterceptsMouseClicks (false, false);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, button);
        button.onStateChange = [this] { repaint(); };
        styleCaption (captionLabel, caption);
        addAndMakeVisible (button);
        addAndMakeVisible (captionLabel);
        button.setColour (juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
        button.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::transparentBlack);
        button.setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
    }

    void BatToggle::resized()
    {
        auto bounds = getLocalBounds();
        captionLabel.setBounds (bounds.removeFromBottom (16));
        button.setBounds (bounds);
    }

    void BatToggle::setOnFromSlide (bool on)
    {
        if (parameter == nullptr)
            return;
        const auto target = on ? 1.0f : 0.0f;
        if (std::abs (parameter->getValue() - target) < 1.0e-6f)
            return;
        if (! gestureOpen)
        {
            parameter->beginChangeGesture();
            gestureOpen = true;
        }
        parameter->setValueNotifyingHost (target);
        repaint();
    }

    void BatToggle::endSlideGesture()
    {
        if (! gestureOpen || parameter == nullptr)
            return;
        parameter->endChangeGesture();
        gestureOpen = false;
    }

    void BatToggle::applySlideTo (BatToggle* other)
    {
        if (other == nullptr)
            return;
        other->setOnFromSlide (slideOn);
        if (! other->gestureOpen)
            return;
        for (auto* existing : slideGroup)
            if (existing == other)
                return;
        slideGroup.push_back (other);
    }

    void BatToggle::mouseDown (const juce::MouseEvent&)
    {
        sliding = false;
        slideGroup.clear();
        stateAtDown = parameter != nullptr && parameter->getValue() >= 0.5f;
    }

    void BatToggle::mouseDrag (const juce::MouseEvent& e)
    {
        const auto dx = (float) e.getDistanceFromDragStartX();
        const auto dy = (float) e.getDistanceFromDragStartY();
        if (! sliding)
        {
            if (std::abs (dx) < (float) slideThreshold && std::abs (dy) < (float) slideThreshold)
                return;
            sliding = true;
            if (std::abs (dy) >= std::abs (dx))
                slideOn = dy < 0.0f;
            else
                slideOn = ! stateAtDown;
        }

        auto* root = getTopLevelComponent();
        if (root == nullptr)
        {
            applySlideTo (this);
            return;
        }

        const auto cur = root->getLocalPoint (this, e.position);
        const auto down = root->getLocalPoint (this, e.getMouseDownPosition().toFloat());
        const juce::Line<float> stroke (down, cur);
        forEachBatToggle (*root, [this, root, cur, stroke] (BatToggle& t)
        {
            const auto bounds = root->getLocalArea (&t, t.getLocalBounds().toFloat());
            if (bounds.contains (cur) || bounds.intersects (stroke))
                applySlideTo (&t);
        });
    }

    void BatToggle::mouseUp (const juce::MouseEvent& e)
    {
        if (! sliding)
        {
            if (parameter != nullptr && getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu())
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
                parameter->endChangeGesture();
                repaint();
            }
        }
        else
        {
            for (auto* t : slideGroup)
                t->endSlideGesture();
            endSlideGesture();
        }

        slideGroup.clear();
        sliding = false;
    }

    void BatToggle::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().withTrimmedBottom (16).toFloat().reduced (4.0f);
        const auto track = area.withSizeKeepingCentre (juce::jmin (18.0f, area.getWidth()),
                                                       juce::jmin (36.0f, area.getHeight()));
        g.setColour (Palette::divider);
        g.fillRoundedRectangle (track, 4.0f);
        g.setColour (Palette::grid.brighter (0.25f));
        g.drawRoundedRectangle (track, 4.0f, 1.0f);

        const auto on = button.getToggleState();
        auto knob = juce::Rectangle<float> (track.getWidth() - 4.0f, 12.0f);
        knob.setCentre (track.getCentreX(), on ? track.getY() + 9.0f : track.getBottom() - 9.0f);
        g.setColour (on ? Palette::accent : Palette::panelRaised);
        g.fillRoundedRectangle (knob, 3.0f);
        g.setColour (Palette::textPrimary.withAlpha (on ? 0.9f : 0.45f));
        g.fillRect (knob.withSizeKeepingCentre (8.0f, 1.5f));
    }

    //==============================================================================
    Wheel::Wheel (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                  const juce::String& caption, bool springReturn)
        : spring (springReturn)
    {
        setComponentID (parameterId);
        slider.setComponentID (parameterId);
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::trackColourId, springReturn ? Palette::master : Palette::accent);
        if (auto* p = state.getParameter (parameterId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
        if (spring)
        {
            slider.onDragEnd = [this]
            {
                slider.setValue (0.0, juce::sendNotificationSync);
            };
        }
        styleCaption (captionLabel, caption);
        addAndMakeVisible (slider);
        addAndMakeVisible (captionLabel);
    }

    void Wheel::resized()
    {
        auto bounds = getLocalBounds();
        captionLabel.setBounds (bounds.removeFromBottom (16));
        slider.setBounds (bounds.reduced (6, 4));
    }

    void Wheel::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().withTrimmedBottom (16).toFloat().reduced (8.0f, 4.0f);
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (area, 6.0f, 1.0f);
    }

    //==============================================================================
    ScopeView::ScopeView (dsp::ScopeTap* t) : tap (t)
    {
        setOpaque (true);
        setComponentID ("scope");
        samples.resize (256);
    }

    void ScopeView::resized() { rebuildBackground(); }

    void ScopeView::rebuildBackground()
    {
        if (getWidth() <= 2 || getHeight() <= 2)
            return;
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        const auto scale = juce::jmax (1, display != nullptr ? (int) display->scale : 1);
        background = juce::Image (juce::Image::RGB, getWidth() * scale, getHeight() * scale, true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        g.fillAll (Palette::background);
        g.setColour (Palette::grid);
        const auto w = (float) getWidth(), h = (float) getHeight();
        g.drawHorizontalLine ((int) (h * 0.5f), 0.0f, w);
        g.drawVerticalLine ((int) (w * 0.5f), 0.0f, h);
    }

    void ScopeView::refresh()
    {
        if (tap == nullptr)
            return;
        tap->copyLatest (samples.data(), (int) samples.size());
        float hash = 0;
        for (int i = 0; i < (int) samples.size(); i += 8)
            hash += samples[(size_t) i];
        if (std::abs (hash - lastHash) > 1.0e-5f)
        {
            lastHash = hash;
            repaint();
        }
    }

    void ScopeView::paint (juce::Graphics& g)
    {
        if (background.isValid())
            g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
        else
            g.fillAll (Palette::background);

        if (samples.empty())
            return;

        juce::Path p;
        const auto w = (float) getWidth(), h = (float) getHeight();
        const auto n = (int) samples.size();
        for (int i = 0; i < n; ++i)
        {
            const auto x = w * (float) i / (float) juce::jmax (1, n - 1);
            const auto y = h * 0.5f - samples[(size_t) i] * h * 0.42f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (Palette::accent);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    //==============================================================================
    FilterReadout::FilterReadout()
    {
        setOpaque (true);
        setComponentID ("filterReadout");
    }

    void FilterReadout::set (float cutoffHz, float emphasisAmount)
    {
        if (std::abs (cutoffHz - cutoff) < 0.5f && std::abs (emphasisAmount - emphasis) < 0.002f)
            return;
        cutoff = cutoffHz;
        emphasis = emphasisAmount;
        rebuild();
        repaint();
    }

    void FilterReadout::resized() { rebuild(); }

    void FilterReadout::rebuild()
    {
        plot = getLocalBounds().toFloat().reduced (4.0f);
        if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
            return;

        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        const auto sx = juce::jmax (1, display != nullptr ? (int) display->scale : 1);
        background = juce::Image (juce::Image::RGB, (int) plot.getWidth() * sx, (int) plot.getHeight() * sx, true);
        juce::Graphics bg (background);
        bg.addTransform (juce::AffineTransform::scale ((float) sx));
        bg.fillAll (Palette::background);
        bg.setColour (Palette::grid);
        for (auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto x = (float) (std::log (hz / 20.0) / std::log (20000.0 / 20.0) * (plot.getWidth()));
            bg.fillRect (x, 0.0f, 1.0f, plot.getHeight());
        }

        // Magnitude of a 4-pole ladder H(s) = 1 / ((1 + s/wc)^4 + k), k = 4.15 * emphasis.
        curve.clear();
        const auto k = 4.15f * juce::jlimit (0.0f, 1.0f, emphasis);
            const auto wc = dooblrm::dsp::twoPi * juce::jmax (10.0f, cutoff);
        float peakDb = -120.0f, peakX = 0, peakY = 0;
        const int n = juce::jmax (2, (int) plot.getWidth());
        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / (float) (n - 1);
            const auto hz = 20.0 * std::pow (20000.0 / 20.0, (double) t);
            const auto w = dooblrm::dsp::twoPi * (float) hz;
            const auto sOver = w / wc;
            // |1+j r|^4 = (1+r^2)^2
            const auto pole = 1.0f + sOver * sOver;
            const auto magLin = 1.0f / std::sqrt (pole * pole * pole * pole + k * k);
            const auto db = juce::jlimit (-36.0f, 24.0f, 20.0f * std::log10 (juce::jmax (1.0e-6f, magLin)));
            const auto x = plot.getX() + t * plot.getWidth();
            const auto y = plot.getY() + juce::jmap (db, -36.0f, 24.0f, plot.getHeight(), 0.0f);
            if (i == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
            if (db > peakDb)
            {
                peakDb = db;
                peakX = x;
                peakY = y;
            }
        }
        handle = { peakX, peakY };
    }

    void FilterReadout::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        if (background.isValid())
            g.drawImage (background, plot, juce::RectanglePlacement::stretchToFit);
        g.setColour (Palette::accent);
        g.strokePath (curve, juce::PathStrokeType (1.4f));
        g.setColour (Palette::textPrimary);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (handle));
    }
}
