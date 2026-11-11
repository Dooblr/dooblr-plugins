#include "BoardControls.h"

#include "../DSP/SynthEngine.h"
#include "../Parameters.h"

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
                const juce::String& caption, juce::Colour fill, bool largeToUse)
        : large (largeToUse)
    {
        setComponentID (parameterId);
        slider.setComponentID (parameterId);
        slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
        slider.getProperties().set ("maxKnobDiameter", large ? 84.0 : 46.0);
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
            if (large)
            {
                const auto d = juce::jmin (bounds.getWidth(), bounds.getHeight());
                slider.getProperties().set ("maxKnobDiameter", (double) juce::jlimit (74, 92, d));
            }
        }

        //==============================================================================
        namespace
        {
            struct KnobArc
            {
                juce::Point<float> centre;
                float radius = 0, lineWidth = 0, arcRadius = 0;
            };

            KnobArc knobArcGeometry (const juce::Slider& slider, juce::Rectangle<float> bounds)
            {
                bounds = bounds.reduced (4.0f);
                const auto maxDiameter = (float) (double) slider.getProperties().getWithDefault ("maxKnobDiameter", 1.0e6);
                KnobArc g;
                g.radius = juce::jmin (bounds.getWidth(), bounds.getHeight(), juce::jmax (8.0f, maxDiameter - 8.0f)) * 0.5f;
                g.centre = bounds.getCentre();
                g.lineWidth = juce::jmax (2.0f, g.radius * 0.12f);
                g.arcRadius = g.radius - g.lineWidth * 0.5f;
                return g;
            }

            static_assert ((int) CutoffContourIndicator::contourOctaves == 5
                           && (int) dsp::contourOctaves == 5);
        }

        CutoffContourIndicator::CutoffContourIndicator()
        {
            setComponentID ("cutoffContour");
            setInterceptsMouseClicks (false, false);
            setOpaque (false);
            setVisible (false);
        }

        CutoffContourIndicator::~CutoffContourIndicator()
        {
            stopTimer();
            detachListeners();
        }

        void CutoffContourIndicator::detachListeners()
        {
            if (state == nullptr)
                return;
            state->removeParameterListener (params::id::cutoff, this);
            state->removeParameterListener (params::id::contourAmt, this);
            state = nullptr;
        }

        void CutoffContourIndicator::attach (juce::AudioProcessorValueTreeState& s,
                                             juce::Slider& slider,
                                             const std::atomic<float>& env)
        {
            detachListeners();
            state = &s;
            cutoffSlider = &slider;
            envTap = &env;
            state->addParameterListener (params::id::cutoff, this);
            state->addParameterListener (params::id::contourAmt, this);
            keyValid = false;
            refresh();
        }

        void CutoffContourIndicator::parentHierarchyChanged() { parentSizeChanged(); }

        void CutoffContourIndicator::parentSizeChanged()
        {
            if (getParentComponent() != nullptr && cutoffSlider != nullptr)
                setBounds (cutoffSlider->getBounds());
            refresh();
        }

        void CutoffContourIndicator::timerCallback() { refresh(); }

        void CutoffContourIndicator::parameterChanged (const juce::String&, float)
        {
            keyValid = false;
            refresh();
        }

        float CutoffContourIndicator::paramHz() const
        {
            if (state != nullptr)
                if (auto* p = state->getParameter (params::id::cutoff))
                    return p->convertFrom0to1 (p->getValue());
            if (cutoffSlider != nullptr)
                return (float) cutoffSlider->getValue();
            return 1200.0f;
        }

        float CutoffContourIndicator::paramContour() const
        {
            if (state != nullptr)
                if (auto* p = state->getParameter (params::id::contourAmt))
                    return p->convertFrom0to1 (p->getValue());
            return 0.0f;
        }

        float CutoffContourIndicator::angleForHz (float hz) const
        {
            if (cutoffSlider == nullptr)
                return 0.0f;
            const auto rotary = cutoffSlider->getRotaryParameters();
            const auto clamped = juce::jlimit (cutoffSlider->getMinimum(), cutoffSlider->getMaximum(),
                                               (double) (std::isfinite (hz) ? hz : paramHz()));
            const auto proportion = (float) cutoffSlider->valueToProportionOfLength (clamped);
            return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
        }

        CutoffContourIndicator::Picture CutoffContourIndicator::makePicture() const
        {
            Picture p;
            p.width = getWidth();
            p.height = getHeight();
            p.contourAmt = juce::jlimit (0.0f, 1.0f, paramContour());
            p.cutoffHz = paramHz();
            p.liveEnv = envTap != nullptr ? envTap->load (std::memory_order_relaxed) : 0.0f;
            if (! std::isfinite (p.liveEnv))
                p.liveEnv = 0.0f;

            const auto octaves = p.contourAmt * contourOctaves;
            const auto lo = cutoffSlider != nullptr ? (float) cutoffSlider->getMinimum() : 20.0f;
            const auto hi = cutoffSlider != nullptr ? (float) cutoffSlider->getMaximum() : 20000.0f;
            p.rangeEndHz = juce::jlimit (lo, hi, p.cutoffHz * std::exp2 (octaves));
            p.liveHz = juce::jlimit (lo, hi, p.cutoffHz * std::exp2 (octaves * juce::jlimit (0.0f, 1.3f, p.liveEnv)));
            p.rangeVisible = p.contourAmt > 1.0e-4f;
            p.liveVisible = p.rangeVisible && p.liveEnv > 1.0e-3f;

            if (cutoffSlider == nullptr || p.width < 8 || p.height < 8)
                return p;

            const auto geo = knobArcGeometry (*cutoffSlider, getLocalBounds().toFloat());
            p.centre = geo.centre;
            p.ringRadius = geo.arcRadius + geo.lineWidth * 0.42f;
            p.stroke = juce::jmax (1.0f, geo.lineWidth * 0.28f);
            p.startAngle = angleForHz (p.cutoffHz);
            p.endAngle = angleForHz (p.rangeEndHz);
            p.liveAngle = angleForHz (p.liveHz);
            p.livePos = p.centre.getPointOnCircumference (p.ringRadius, p.liveAngle);
            return p;
        }

        CutoffContourIndicator::Key CutoffContourIndicator::makeKey (const Picture& p) const
        {
            return { juce::roundToInt (p.contourAmt * 1000.0f),
                     juce::roundToInt (p.cutoffHz * 10.0f),
                     juce::roundToInt (juce::jlimit (0.0f, 1.3f, p.liveEnv) * 256.0f),
                     p.width, p.height };
        }

        void CutoffContourIndicator::refresh()
        {
            picture = makePicture();
            const auto key = makeKey (picture);
            const auto show = picture.rangeVisible;
            if (isVisible() != show)
                setVisible (show);

            if (show && ! isTimerRunning())
                startTimerHz (30);
            else if (! show && isTimerRunning())
                stopTimer();

            if (keyValid && key == lastKey)
                return;

            lastKey = key;
            keyValid = true;
            lastRepaintBounds = getBounds();
            ++repaintRequests;
            repaint();
        }

        bool CutoffContourIndicator::isRangeVisible() const noexcept { return picture.rangeVisible; }
        bool CutoffContourIndicator::isLiveMarkerVisible() const noexcept { return picture.liveVisible; }
        float CutoffContourIndicator::getContourAmount() const noexcept { return picture.contourAmt; }
        float CutoffContourIndicator::getRangeStartHz() const noexcept { return picture.cutoffHz; }
        float CutoffContourIndicator::getRangeEndHz() const noexcept { return picture.rangeEndHz; }
        float CutoffContourIndicator::getLiveHz() const noexcept { return picture.liveHz; }
        float CutoffContourIndicator::getLiveEnvelope() const noexcept { return picture.liveEnv; }
        float CutoffContourIndicator::getRangeStartAngle() const noexcept { return picture.startAngle; }
        float CutoffContourIndicator::getRangeEndAngle() const noexcept { return picture.endAngle; }
        float CutoffContourIndicator::getLiveAngle() const noexcept { return picture.liveAngle; }
        juce::Point<float> CutoffContourIndicator::getCentre() const noexcept { return picture.centre; }
        float CutoffContourIndicator::getRingRadius() const noexcept { return picture.ringRadius; }
        juce::Point<float> CutoffContourIndicator::getLiveMarkerPosition() const noexcept { return picture.livePos; }

        void CutoffContourIndicator::paint (juce::Graphics& g)
        {
            if (! picture.rangeVisible || cutoffSlider == nullptr)
                return;

            const auto from = picture.startAngle;
            const auto to = picture.endAngle;
            if (std::abs (to - from) > 0.004f)
            {
                juce::Path range;
                range.addCentredArc (picture.centre.x, picture.centre.y, picture.ringRadius, picture.ringRadius,
                                     0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
                g.setColour (Palette::dynamic.withAlpha (0.28f));
                g.strokePath (range, juce::PathStrokeType (picture.stroke, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            if (! picture.liveVisible)
                return;

            const auto glow = juce::jmax (5.0f, picture.stroke * 3.4f);
            g.setColour (Palette::dynamic.withAlpha (0.20f));
            g.fillEllipse (juce::Rectangle<float> (glow, glow).withCentre (picture.livePos));
            g.setColour (Palette::dynamic.withAlpha (0.72f));
            g.fillEllipse (juce::Rectangle<float> (3.2f, 3.2f).withCentre (picture.livePos));
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

    void RotarySwitch::setTickTexts (const juce::StringArray& labels)
    {
        if (ticks == labels)
            return;
        ticks = labels;
        tickLayout = makeTickLayout();
        repaint();
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
                          const juce::String& caption,
                          const juce::String& downLabelToUse, const juce::String& upLabelToUse)
        : downLabel (downLabelToUse), upLabel (upLabelToUse)
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
        const auto labelled = downLabel.isNotEmpty() || upLabel.isNotEmpty();
        if (labelled && area.getHeight() > 36.0f)
        {
            g.setFont (juce::FontOptions (8.0f));
            g.setColour (Palette::textMuted);
            if (upLabel.isNotEmpty())
                g.drawText (upLabel, area.removeFromTop (11.0f), juce::Justification::centred, false);
            if (downLabel.isNotEmpty())
                g.drawText (downLabel, area.removeFromBottom (11.0f), juce::Justification::centred, false);
        }

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
    FilterReadout::FilterReadout (juce::AudioProcessorValueTreeState& s,
                                  const std::atomic<float>& env,
                                  const std::atomic<int>& stage)
        : state (s), envTap (env), stageTap (stage)
    {
        setOpaque (true);
        setComponentID ("filterReadout");
        trail.reserve (48);
        for (auto* id : { params::id::fAttack, params::id::fDecay, params::id::fSustain, params::id::contourAmt })
            state.addParameterListener (id, this);
        startTimerHz (30);
    }

    FilterReadout::~FilterReadout()
    {
        stopTimer();
        for (auto* id : { params::id::fAttack, params::id::fDecay, params::id::fSustain, params::id::contourAmt })
            state.removeParameterListener (id, this);
    }

    float FilterReadout::paramNorm (const char* id) const
    {
        if (auto* p = state.getParameter (id))
            return p->getValue();
        return 0.0f;
    }

    juce::RangedAudioParameter* FilterReadout::paramFor (Handle handle) const
    {
        const char* id = nullptr;
        switch (handle)
        {
            case Handle::attack:  id = params::id::fAttack; break;
            case Handle::decay:   id = params::id::fDecay; break;
            case Handle::sustain: id = params::id::fSustain; break;
            case Handle::amount:  id = params::id::contourAmt; break;
            case Handle::none:    break;
        }
        return id != nullptr ? state.getParameter (id) : nullptr;
    }

    FilterReadout::Geometry FilterReadout::makeGeometry() const
    {
        Geometry g;
        g.plot = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        const auto w = juce::jmax (1.0f, g.plot.getWidth());
        const auto attackN = paramNorm (params::id::fAttack);
        const auto decayN = paramNorm (params::id::fDecay);
        const auto sustainN = paramNorm (params::id::fSustain);
        const auto amountN = paramNorm (params::id::contourAmt);

        g.x0 = g.plot.getX();
        g.xEnd = g.plot.getRight();
        g.y0 = g.plot.getBottom();
        g.yTop = g.plot.getY();
        const auto rise = g.y0 - g.yTop;
        g.xPeak = g.x0 + 0.06f * w + attackN * 0.34f * w;
        g.xSus = g.xPeak + 0.08f * w + decayN * 0.28f * w;
        g.xSus = juce::jmin (g.xSus, g.xEnd - 0.18f * w);
        g.yPeak = g.y0 - amountN * rise;
        g.ySus = g.y0 - amountN * sustainN * rise;
        return g;
    }

    juce::Point<float> FilterReadout::getHandlePosition (Handle handle) const
    {
        const auto g = makeGeometry();
        switch (handle)
        {
            case Handle::attack:  return { g.xPeak, g.y0 };
            case Handle::decay:   return { g.xSus, g.y0 };
            case Handle::sustain: return { g.xSus + 0.28f * (g.xEnd - g.xSus), g.ySus };
            case Handle::amount:  return { g.xPeak, g.yPeak };
            case Handle::none:    break;
        }
        return {};
    }

    FilterReadout::Handle FilterReadout::handleAt (juce::Point<float> p) const
    {
        auto best = Handle::none;
        auto bestDistance = 14.0f;
        for (auto h : { Handle::attack, Handle::decay, Handle::sustain, Handle::amount })
        {
            const auto d = getHandlePosition (h).getDistanceFrom (p);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = h;
            }
        }
        return best;
    }

    void FilterReadout::setHandleFromPoint (Handle handle, juce::Point<float> p)
    {
        auto* param = paramFor (handle);
        if (param == nullptr)
            return;

        const auto g = makeGeometry();
        const auto w = juce::jmax (1.0f, g.plot.getWidth());
        const auto rise = juce::jmax (1.0f, g.y0 - g.yTop);
        float norm = param->getValue();

        switch (handle)
        {
            case Handle::attack:
                norm = (p.x - g.x0 - 0.06f * w) / (0.34f * w);
                break;
            case Handle::decay:
                norm = (p.x - g.xPeak - 0.08f * w) / (0.28f * w);
                break;
            case Handle::sustain:
            {
                const auto amountN = paramNorm (params::id::contourAmt);
                if (amountN < 1.0e-4f)
                    return;
                norm = (g.y0 - p.y) / (amountN * rise);
                break;
            }
            case Handle::amount:
                norm = (g.y0 - p.y) / rise;
                break;
            case Handle::none:
                return;
        }

        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }

    void FilterReadout::beginHandleGesture (Handle handle)
    {
        if (gestureOpen)
            return;
        if (auto* param = paramFor (handle))
        {
            param->beginChangeGesture();
            gestureOpen = true;
        }
    }

    void FilterReadout::endHandleGesture()
    {
        if (! gestureOpen)
            return;
        if (auto* param = paramFor (dragHandle))
            param->endChangeGesture();
        gestureOpen = false;
    }

    void FilterReadout::mouseDown (const juce::MouseEvent& e)
    {
        if (e.getNumberOfClicks() >= 2)
            return;
        dragHandle = handleAt (e.position);
        if (dragHandle == Handle::none)
            return;
        grabOffset = getHandlePosition (dragHandle) - e.position;
        beginHandleGesture (dragHandle);
    }

    void FilterReadout::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragHandle == Handle::none)
            return;
        setHandleFromPoint (dragHandle, e.position + grabOffset);
        refresh();
    }

    void FilterReadout::mouseUp (const juce::MouseEvent&)
    {
        endHandleGesture();
        dragHandle = Handle::none;
    }

    void FilterReadout::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto handle = handleAt (e.position);
        auto* param = paramFor (handle);
        if (param == nullptr)
            return;
        endHandleGesture();
        dragHandle = Handle::none;
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
        refresh();
    }

    void FilterReadout::parameterChanged (const juce::String&, float)
    {
        refresh();
    }

    void FilterReadout::timerCallback() { refresh(); }

    void FilterReadout::resized() { refresh(); }

    void FilterReadout::refresh()
    {
        liveEnv = envTap.load (std::memory_order_relaxed);
        liveStage = stageTap.load (std::memory_order_relaxed);

        const auto g = makeGeometry();
        plot = g.plot;

        const auto rise = juce::jmax (1.0f, g.y0 - g.yTop);
        const auto amountN = paramNorm (params::id::contourAmt);
        const auto sustainN = paramNorm (params::id::fSustain);
        const auto env = juce::jlimit (0.0f, 1.3f, liveEnv);
        float x = g.x0;
        switch (liveStage)
        {
            case 1: // attack
                x = g.x0 + juce::jlimit (0.0f, 1.0f, env) * (g.xPeak - g.x0);
                break;
            case 2: // decay
            {
                const auto span = juce::jmax (1.0e-4f, 1.0f - sustainN);
                const auto t = juce::jlimit (0.0f, 1.0f, (1.0f - env) / span);
                x = g.xPeak + t * (g.xSus - g.xPeak);
                break;
            }
            case 3: // sustain
                x = g.xSus + 0.35f * (g.xEnd - g.xSus);
                break;
            case 4: // release
                x = g.xSus + 0.55f * (g.xEnd - g.xSus);
                break;
            default:
                x = g.x0;
                break;
        }
        const auto y = g.y0 - env * juce::jmax (amountN, 0.15f) * rise;
        playhead = { x, y };
        trail.push_back (playhead);
        if (trail.size() > 48)
            trail.erase (trail.begin());
        if (liveStage == 0 && env <= 1.0e-4f)
            trail.clear();

        repaint();
    }

    void FilterReadout::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        const auto geo = makeGeometry();
        plot = geo.plot;

        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) geo.y0, geo.x0, geo.xEnd);
        g.drawVerticalLine ((int) geo.x0, geo.yTop, geo.y0);

        if (trail.size() > 1)
        {
            juce::Path motion;
            motion.startNewSubPath (trail.front());
            for (size_t i = 1; i < trail.size(); ++i)
                motion.lineTo (trail[i]);
            g.setColour (Palette::dynamic.withAlpha (0.45f));
            g.strokePath (motion, juce::PathStrokeType (1.2f));
        }

        juce::Path shape;
        shape.startNewSubPath (geo.x0, geo.y0);
        shape.lineTo (geo.xPeak, geo.yPeak);
        shape.lineTo (geo.xSus, geo.ySus);
        shape.lineTo (geo.xEnd, geo.ySus);
        shape.lineTo (geo.xEnd, geo.y0);
        g.setColour (Palette::accent);
        g.strokePath (shape, juce::PathStrokeType (1.6f));

        g.setColour (Palette::dynamic);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (playhead));
        g.setColour (Palette::dynamic.withAlpha (0.35f));
        g.drawHorizontalLine ((int) playhead.y, geo.x0, geo.xEnd);

        const auto drawHandle = [&g] (juce::Point<float> p, juce::Colour colour)
        {
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
            g.setColour (Palette::textPrimary.withAlpha (0.9f));
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p), 1.0f);
        };
        drawHandle (getHandlePosition (Handle::attack), Palette::spectrum);
        drawHandle (getHandlePosition (Handle::decay), Palette::spectrum);
        drawHandle (getHandlePosition (Handle::sustain), Palette::accent);
        drawHandle (getHandlePosition (Handle::amount), Palette::dynamic);
    }

    //==============================================================================
    LfoPulse::LfoPulse()
    {
        setComponentID ("lfoPulse");
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
        setVisible (false);
    }

    LfoPulse::~LfoPulse()
    {
        stopTimer();
    }

    void LfoPulse::attach (juce::AudioProcessorValueTreeState& s, const std::atomic<float>& t)
    {
        state = &s;
        tap = &t;
        refresh();
    }

    void LfoPulse::parentHierarchyChanged() { parentSizeChanged(); }

    void LfoPulse::parentSizeChanged()
    {
        if (auto* parent = getParentComponent())
        {
            auto cap = parent->getLocalBounds().removeFromBottom (16);
            setBounds (cap.removeFromRight (12).reduced (2, 3));
        }
    }

    void LfoPulse::refresh()
    {
        const bool on = state != nullptr && state->getParameter (params::id::osc3Lfo) != nullptr
                        && state->getParameter (params::id::osc3Lfo)->getValue() >= 0.5f;
        if (on != active)
        {
            active = on;
            if (on)
                startTimerHz (24);
            else
                stopTimer();
            setVisible (on);
        }

        const auto raw = (tap != nullptr && on) ? tap->load (std::memory_order_relaxed) : 0.0f;
        const auto next = on ? juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * raw) : 0.0f;
        const auto q = (int) std::round (next * 20.0f);
        if (q == lastQ && isVisible() == on)
            return;
        lastQ = q;
        level = next;
        ++repaintRequests;
        repaint();
    }

    void LfoPulse::paint (juce::Graphics& g)
    {
        if (! isVisible() || getWidth() < 2 || getHeight() < 2)
            return;
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::dynamic.withAlpha (0.18f + 0.72f * level));
        g.fillEllipse (bounds);
        g.setColour (Palette::dynamic.withAlpha (0.55f));
        g.drawEllipse (bounds.reduced (0.5f), 1.0f);
    }
}
