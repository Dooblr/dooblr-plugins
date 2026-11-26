#include "ControlPanel.h"

#include <cmath>

namespace dooblrchroma::ui
{
    Knob::Knob (const juce::String& cap, const juce::String& id)
        : paramId (id)
    {
        setComponentID (id);
        slider.setComponentID (id);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        caption.setText (cap, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (slider);
        addAndMakeVisible (caption);
    }

    void Knob::resized()
    {
        auto r = getLocalBounds();
        const int capH = juce::jlimit (13, 16, r.getHeight() / 5);
        const int boxH = juce::jlimit (15, 18, r.getHeight() / 5);
        caption.setFont (juce::FontOptions (11.0f));
        caption.setMinimumHorizontalScale (1.0f);
        caption.setJustificationType (juce::Justification::centred);
        caption.setBounds (r.removeFromTop (capH).reduced (1, 0));
        // Text box uses the full cell width so the formatted value is never ellipsized.
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (24, r.getWidth() - 2), boxH);
        slider.setBounds (r);
    }

    juce::Label* Knob::getTextBox() const
    {
        for (auto* c : slider.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (c))
                return label;
        return nullptr;
    }

    juce::Rectangle<int> Knob::getRotaryBounds() const
    {
        auto r = slider.getLocalBounds();
        if (auto* box = getTextBox())
            r.removeFromBottom (box->getHeight());
        const int side = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (side, side);
    }

    void Knob::attach (juce::AudioProcessorValueTreeState& state)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    }

    void Knob::detach() { attachment.reset(); }

    Choice::Choice (const juce::String& cap, const juce::String& id)
        : paramId (id)
    {
        setComponentID (id);
        box.setComponentID (id);
        caption.setText (cap, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (box);
        addAndMakeVisible (caption);
    }

    void Choice::resized()
    {
        auto r = getLocalBounds();
        const int capH = juce::jlimit (13, 16, r.getHeight() / 5);
        caption.setFont (juce::FontOptions (11.0f));
        caption.setMinimumHorizontalScale (1.0f);
        caption.setJustificationType (juce::Justification::centred);
        caption.setBounds (r.removeFromTop (capH).reduced (1, 0));
        box.setBounds (r.reduced (2, 1));
    }

    void Choice::attach (juce::AudioProcessorValueTreeState& state)
    {
        if (auto* p = state.getParameter (paramId))
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                box.clear (juce::dontSendNotification);
                box.addItemList (choice->choices, 1);
            }
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
    }

    void Choice::detach() { attachment.reset(); }

    Toggle::Toggle (const juce::String& cap, const juce::String& id)
        : paramId (id)
    {
        setComponentID (id);
        button.setComponentID (id);
        button.setButtonText (cap);
        addAndMakeVisible (button);
    }

    void Toggle::resized() { button.setBounds (getLocalBounds()); }
    void Toggle::attach (juce::AudioProcessorValueTreeState& state)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    }
    void Toggle::detach() { attachment.reset(); }

    Panel::Panel (juce::String titleToUse) : title (std::move (titleToUse))
    {
        setComponentID ("panel_" + title);
        setOpaque (false);
    }

    void Panel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (title, getLocalBounds().removeFromTop (18).reduced (8, 0), juce::Justification::centredLeft, false);
    }

    void Panel::resized() {}

    juce::Rectangle<int> Panel::contentBounds() const
    {
        return getLocalBounds().reduced (6, 4).withTrimmedTop (16);
    }

    juce::Component* Panel::findControl (const juce::String& paramId) const
    {
        for (auto* c : getChildren())
            if (c->getComponentID() == paramId || c->findChildWithID (paramId) != nullptr)
                return c->getComponentID() == paramId ? c : c->findChildWithID (paramId);
        return findChildWithID (paramId);
    }


    namespace
    {
        struct Axis
        {
            juce::Point<float> border, centre;
        };

        juce::Rectangle<float> objectBody (juce::Rectangle<float> bounds)
        {
            return bounds.reduced (3.0f).reduced (10.0f, 8.0f);
        }

        Axis axisFor (dsp::ResType type, juce::Rectangle<float> body)
        {
            Axis a;
            switch (type)
            {
                case dsp::ResType::string:
                {
                    const float y = body.getCentreY();
                    a.border = { body.getX() + 14.0f, y };
                    a.centre = { body.getCentreX(), y + juce::jmin (10.0f, body.getHeight() * 0.18f) * 0.5f };
                    break;
                }
                case dsp::ResType::bar:
                case dsp::ResType::marimba:
                {
                    const float barH = juce::jlimit (8.0f, type == dsp::ResType::marimba ? 18.0f : 22.0f,
                                                     body.getHeight() * (type == dsp::ResType::marimba ? 0.22f : 0.28f));
                    const float y = type == dsp::ResType::marimba
                                        ? body.getY() + body.getHeight() * 0.38f
                                        : body.getCentreY();
                    const float w = body.getWidth() * (type == dsp::ResType::marimba ? 0.9f : 0.92f);
                    a.border = { body.getCentreX() - w * 0.5f, y };
                    a.centre = { body.getCentreX(), y };
                    juce::ignoreUnused (barH);
                    break;
                }
                case dsp::ResType::plate:
                {
                    const float h = juce::jmin (body.getHeight() * 0.78f, body.getWidth() * 0.62f);
                    const float w = juce::jmin (body.getWidth() * 0.72f, h * 1.25f);
                    auto plate = juce::Rectangle<float> (w, h).withCentre (body.getCentre());
                    a.border = plate.getTopLeft();
                    a.centre = plate.getCentre();
                    break;
                }
                case dsp::ResType::drumhead:
                {
                    const float s = juce::jmin (body.getWidth(), body.getHeight()) - 4.0f;
                    auto drum = body.withSizeKeepingCentre (s, s).reduced (8.0f);
                    a.border = { drum.getX(), drum.getCentreY() };
                    a.centre = drum.getCentre();
                    break;
                }
                case dsp::ResType::membrane:
                {
                    auto skin = body.reduced (4.0f).reduced (6.0f);
                    a.border = skin.getTopLeft();
                    a.centre = skin.getCentre();
                    break;
                }
                case dsp::ResType::openTube:
                case dsp::ResType::closedTube:
                {
                    auto tube = juce::Rectangle<float> (body.getWidth() * 0.86f,
                                                       juce::jlimit (12.0f, 28.0f, body.getHeight() * 0.42f))
                                    .withCentre (body.getCentre());
                    a.border = { tube.getX(), tube.getCentreY() };
                    a.centre = { tube.getCentreX(), tube.getCentreY() };
                    break;
                }
                case dsp::ResType::manual:
                    a.border = { body.getX() + 4.0f, body.getBottom() - 4.0f - body.getHeight() * 0.3f };
                    a.centre = { body.getCentreX(), body.getY() + body.getHeight() * 0.4f };
                    break;
            }
            return a;
        }

        float hitFromPoint (const Axis& a, juce::Point<float> p)
        {
            const auto d = a.centre - a.border;
            const float len2 = d.x * d.x + d.y * d.y;
            if (len2 < 1.0f)
                return 0.0f;
            return juce::jlimit (0.0f, 1.0f, ((p.x - a.border.x) * d.x + (p.y - a.border.y) * d.y) / len2);
        }

        void paintExciter (juce::Graphics& g, juce::Point<float> hit, juce::Rectangle<float> body)
        {
            const float headR = juce::jlimit (4.0f, 7.0f, body.getHeight() * 0.075f);
            const float stickLen = juce::jlimit (14.0f, 32.0f, body.getHeight() * 0.28f);
            const float candidates[] = { -0.95f, -2.2f, 0.95f, 2.2f, -1.57f, 1.57f };
            float angle = candidates[0];
            const auto inner = body.reduced (6.0f);
            for (float a : candidates)
            {
                const auto end = hit + juce::Point<float> (std::cos (a), std::sin (a)) * stickLen;
                if (inner.contains (end))
                {
                    angle = a;
                    break;
                }
            }
            const auto end = hit + juce::Point<float> (std::cos (angle), std::sin (angle)) * stickLen;
            g.setColour (Palette::textPrimary.withAlpha (0.85f));
            g.drawLine (hit.x, hit.y, end.x, end.y, juce::jmax (1.4f, headR * 0.28f));
            g.fillEllipse (end.x - 2.0f, end.y - 2.0f, 4.0f, 4.0f);
            g.setColour (Palette::accent.withAlpha (0.28f));
            g.fillEllipse (hit.x - headR - 3.0f, hit.y - headR - 3.0f, (headR + 3.0f) * 2.0f, (headR + 3.0f) * 2.0f);
            g.setColour (Palette::accent);
            g.fillEllipse (hit.x - headR, hit.y - headR, headR * 2.0f, headR * 2.0f);
            g.setColour (Palette::textPrimary.withAlpha (0.9f));
            g.drawEllipse (hit.x - headR, hit.y - headR, headR * 2.0f, headR * 2.0f, 1.1f);
        }

        void paintObject (juce::Graphics& g, juce::Rectangle<float> body, dsp::ResType type,
                          float hit, bool coupled, bool drawExciter, juce::Point<float>* markerOut)
        {
            const auto stroke = Palette::spectrum;
            const auto fill = Palette::spectrum.withAlpha (0.18f);
            const auto ink = Palette::textPrimary.withAlpha (0.85f);
            const auto axis = axisFor (type, body);
            const auto hitPt = axis.border + (axis.centre - axis.border) * hit;
            if (markerOut != nullptr)
                *markerOut = hitPt;

            auto drawCouple = [&] (juce::Point<float> p)
            {
                if (! coupled)
                    return;
                g.setColour (Palette::dynamic.withAlpha (0.9f));
                g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 1.4f);
            };

            switch (type)
            {
                case dsp::ResType::string:
                {
                    const float y = body.getCentreY();
                    const float postW = juce::jmax (4.0f, juce::jmin (6.0f, body.getWidth() * 0.04f));
                    const float postH = juce::jmin (body.getHeight() * 0.7f, 36.0f);
                    auto leftPost = juce::Rectangle<float> (postW, postH).withCentre ({ body.getX() + 8.0f, y });
                    auto rightPost = juce::Rectangle<float> (postW, postH).withCentre ({ body.getRight() - 8.0f, y });
                    g.setColour (stroke);
                    g.fillRoundedRectangle (leftPost, 1.5f);
                    g.fillRoundedRectangle (rightPost, 1.5f);
                    g.setColour (stroke.withAlpha (0.35f));
                    juce::Path ghost;
                    ghost.startNewSubPath (leftPost.getRight(), y - 4.0f);
                    ghost.quadraticTo (body.getCentreX(), y - 8.0f, rightPost.getX(), y - 4.0f);
                    g.strokePath (ghost, juce::PathStrokeType (1.0f));
                    juce::Path str;
                    const float sag = juce::jmin (10.0f, body.getHeight() * 0.18f);
                    str.startNewSubPath (leftPost.getRight(), y);
                    str.quadraticTo (body.getCentreX(), y + sag, rightPost.getX(), y);
                    g.setColour (ink);
                    g.strokePath (str, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }
                case dsp::ResType::bar:
                {
                    const float barH = juce::jlimit (8.0f, 22.0f, body.getHeight() * 0.28f);
                    auto bar = juce::Rectangle<float> (body.getWidth() * 0.92f, barH).withCentre (body.getCentre());
                    g.setColour (stroke.withAlpha (0.85f));
                    const float sW = 8.0f, sH = barH + 10.0f;
                    juce::Path lSup, rSup;
                    lSup.addTriangle (bar.getX() + 14.0f, bar.getBottom() + 8.0f,
                                      bar.getX() + 14.0f - sW * 0.6f, bar.getY() + sH,
                                      bar.getX() + 14.0f + sW * 0.6f, bar.getY() + sH);
                    rSup.addTriangle (bar.getRight() - 14.0f, bar.getBottom() + 8.0f,
                                      bar.getRight() - 14.0f - sW * 0.6f, bar.getY() + sH,
                                      bar.getRight() - 14.0f + sW * 0.6f, bar.getY() + sH);
                    g.fillPath (lSup);
                    g.fillPath (rSup);
                    g.setColour (fill);
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (bar, 3.0f, 1.6f);
                    g.setColour (Palette::textPrimary.withAlpha (0.18f));
                    g.fillRoundedRectangle (bar.reduced (3.0f, barH * 0.35f).withY (bar.getY() + 2.0f), 2.0f);
                    break;
                }
                case dsp::ResType::marimba:
                {
                    const float barH = juce::jlimit (8.0f, 18.0f, body.getHeight() * 0.22f);
                    auto bar = juce::Rectangle<float> (body.getWidth() * 0.9f, barH)
                                   .withCentre ({ body.getCentreX(), body.getY() + body.getHeight() * 0.38f });
                    juce::Path under;
                    under.startNewSubPath (bar.getX() + 8.0f, bar.getBottom());
                    under.quadraticTo (bar.getCentreX(), bar.getBottom() + juce::jmin (16.0f, body.getHeight() * 0.22f),
                                       bar.getRight() - 8.0f, bar.getBottom());
                    g.setColour (stroke.withAlpha (0.35f));
                    g.strokePath (under, juce::PathStrokeType (1.6f));
                    auto tube = juce::Rectangle<float> (bar.getWidth() * 0.16f, juce::jmin (22.0f, body.getHeight() * 0.32f))
                                    .withCentre ({ bar.getCentreX(), bar.getBottom() + juce::jmin (16.0f, body.getHeight() * 0.22f) });
                    g.setColour (fill);
                    g.fillRoundedRectangle (tube, 4.0f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (tube, 4.0f, 1.3f);
                    g.setColour (fill);
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (bar, 3.0f, 1.6f);
                    break;
                }
                case dsp::ResType::plate:
                {
                    const float h = juce::jmin (body.getHeight() * 0.78f, body.getWidth() * 0.62f);
                    const float w = juce::jmin (body.getWidth() * 0.72f, h * 1.25f);
                    auto plate = juce::Rectangle<float> (w, h).withCentre (body.getCentre());
                    g.setColour (Palette::divider);
                    g.fillRoundedRectangle (plate.translated (4.0f, 5.0f), 4.0f);
                    g.setColour (fill);
                    g.fillRoundedRectangle (plate, 4.0f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (plate, 4.0f, 1.8f);
                    g.setColour (Palette::textPrimary.withAlpha (0.12f));
                    g.fillRoundedRectangle (plate.reduced (3.0f).removeFromTop (plate.getHeight() * 0.22f), 3.0f);
                    g.setColour (stroke.withAlpha (0.45f));
                    g.drawRoundedRectangle (plate.reduced (7.0f), 3.0f, 1.0f);
                    const float lug = juce::jlimit (2.2f, 3.6f, plate.getWidth() * 0.025f);
                    const auto inset = plate.reduced (8.0f);
                    g.setColour (stroke);
                    for (auto p : { inset.getTopLeft(), inset.getTopRight(), inset.getBottomLeft(), inset.getBottomRight() })
                        g.fillEllipse (p.x - lug, p.y - lug, lug * 2.0f, lug * 2.0f);
                    break;
                }
                case dsp::ResType::drumhead:
                {
                    const float s = juce::jmin (body.getWidth(), body.getHeight()) - 4.0f;
                    auto drum = body.withSizeKeepingCentre (s, s).reduced (8.0f);
                    g.setColour (stroke.withAlpha (0.5f));
                    g.drawEllipse (drum.expanded (4.0f), 3.0f);
                    g.setColour (fill);
                    g.fillEllipse (drum);
                    g.setColour (stroke);
                    g.drawEllipse (drum, 1.6f);
                    g.setColour (stroke);
                    const auto mid = drum.getCentre();
                    const float orad = drum.getWidth() * 0.5f;
                    for (int i = 0; i < 8; ++i)
                    {
                        const float ang = (float) i * 3.14159265f * 0.25f;
                        const auto p = mid + juce::Point<float> (std::cos (ang), std::sin (ang)) * orad;
                        g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
                    }
                    break;
                }
                case dsp::ResType::membrane:
                {
                    auto frame = body.reduced (4.0f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (frame, 3.0f, 2.0f);
                    g.drawRoundedRectangle (frame.reduced (4.0f), 2.0f, 1.2f);
                    auto skin = frame.reduced (6.0f);
                    g.setColour (fill);
                    g.fillRoundedRectangle (skin, 2.0f);
                    g.setColour (stroke.withAlpha (0.25f));
                    g.drawEllipse (skin.withSizeKeepingCentre (skin.getWidth() * 0.6f, skin.getHeight() * 0.45f), 1.0f);
                    break;
                }
                case dsp::ResType::openTube:
                case dsp::ResType::closedTube:
                {
                    const float h = juce::jlimit (12.0f, 28.0f, body.getHeight() * 0.42f);
                    auto tube = juce::Rectangle<float> (body.getWidth() * 0.86f, h).withCentre (body.getCentre());
                    g.setColour (fill);
                    g.fillRoundedRectangle (tube, h * 0.5f);
                    g.setColour (stroke);
                    g.drawRoundedRectangle (tube, h * 0.5f, 1.6f);
                    auto leftEnd = juce::Rectangle<float> (h * 0.45f, h).withCentre ({ tube.getX() + 2.0f, tube.getCentreY() });
                    auto rightEnd = juce::Rectangle<float> (h * 0.45f, h).withCentre ({ tube.getRight() - 2.0f, tube.getCentreY() });
                    if (type == dsp::ResType::closedTube)
                    {
                        g.setColour (stroke);
                        g.fillEllipse (leftEnd);
                    }
                    else
                    {
                        g.setColour (Palette::panelRaised);
                        g.fillEllipse (leftEnd.reduced (2.0f));
                        g.setColour (stroke);
                        g.drawEllipse (leftEnd.reduced (2.0f), 1.3f);
                    }
                    g.setColour (Palette::panelRaised);
                    g.fillEllipse (rightEnd.reduced (2.0f));
                    g.setColour (stroke);
                    g.drawEllipse (rightEnd.reduced (2.0f), 1.3f);
                    break;
                }
                case dsp::ResType::manual:
                {
                    const float maxH = body.getHeight() * 0.75f;
                    const float widths = body.getWidth() / 5.5f;
                    const float heights[4] = { 0.45f, 0.7f, 0.55f, 0.85f };
                    const float x0 = body.getX() + widths * 0.6f;
                    g.setColour (stroke.withAlpha (0.4f));
                    g.drawLine (body.getX(), body.getBottom() - 4.0f, body.getRight(), body.getBottom() - 4.0f, 1.0f);
                    for (int i = 0; i < 4; ++i)
                    {
                        auto bar = juce::Rectangle<float> (widths * 0.7f, maxH * heights[i]);
                        bar.setCentre ({ x0 + widths * (float) i, 0.0f });
                        bar.setBottom (body.getBottom() - 4.0f);
                        g.setColour (fill);
                        g.fillRoundedRectangle (bar, 2.0f);
                        g.setColour (stroke);
                        g.drawRoundedRectangle (bar, 2.0f, 1.2f);
                    }
                    break;
                }
            }

            if (drawExciter)
                paintExciter (g, hitPt, body);
            else
            {
                const float rad = juce::jlimit (2.0f, 4.0f, body.getHeight() * 0.08f);
                g.setColour (Palette::accent);
                g.fillEllipse (hitPt.x - rad, hitPt.y - rad, rad * 2.0f, rad * 2.0f);
            }
            drawCouple (axis.centre);
        }
    }

    ResonatorView::ResonatorView()
    {
        setComponentID ("resonatorView");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void ResonatorView::setState (dsp::ResType t, float hitPosition, bool isCoupled)
    {
        type = t;
        hit = juce::jlimit (0.0f, 1.0f, hitPosition);
        coupled = isCoupled;
        repaint();
    }

    juce::Point<float> ResonatorView::getHitMarker() const { return marker; }

    float ResonatorView::hitAt (juce::Point<float> p) const
    {
        return hitFromPoint (axisFor (type, objectBody (getLocalBounds().toFloat())), p);
    }

    void ResonatorView::dragTo (juce::Point<float> p)
    {
        const float h = hitAt (p);
        if (std::abs (h - hit) < 1.0e-5f)
            return;
        hit = h;
        if (onHitChanged)
            onHitChanged (hit);
        repaint();
    }

    void ResonatorView::mouseDown (const juce::MouseEvent& e)
    {
        dragging = true;
        dragTo (e.position);
    }

    void ResonatorView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging)
            dragTo (e.position);
    }

    void ResonatorView::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
    }

    void ResonatorView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        juce::Path clip;
        clip.addRoundedRectangle (r, 5.0f);
        g.reduceClipRegion (clip);
        paintObject (g, objectBody (getLocalBounds().toFloat()), type, hit, coupled, true, &marker);
    }

    ObjectPicker::ObjectPicker()
    {
        setComponentID ("objectPicker");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void ObjectPicker::setType (dsp::ResType t)
    {
        if (type == t)
            return;
        type = t;
        repaint();
    }

    juce::Rectangle<int> ObjectPicker::tileBounds (int index) const
    {
        auto r = getLocalBounds();
        const int cols = r.getWidth() >= 252 ? 9 : (r.getWidth() >= 160 ? 5 : 3);
        const int rows = (dsp::kNumResTypes + cols - 1) / cols;
        const int w = r.getWidth() / cols;
        const int h = r.getHeight() / juce::jmax (1, rows);
        return { r.getX() + (index % cols) * w, r.getY() + (index / cols) * h, w, h };
    }

    int ObjectPicker::indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < dsp::kNumResTypes; ++i)
            if (tileBounds (i).contains (p))
                return i;
        return -1;
    }

    void ObjectPicker::resized() { repaint(); }

    void ObjectPicker::mouseMove (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.getPosition());
        if (i != hover) { hover = i; repaint(); }
    }

    void ObjectPicker::mouseExit (const juce::MouseEvent&)
    {
        if (hover != -1) { hover = -1; repaint(); }
    }

    void ObjectPicker::mouseDown (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.getPosition());
        if (i < 0)
            return;
        const auto t = (dsp::ResType) i;
        if (t == type)
            return;
        type = t;
        if (onChange)
            onChange (type);
        repaint();
    }

    void ObjectPicker::paint (juce::Graphics& g)
    {
        for (int i = 0; i < dsp::kNumResTypes; ++i)
        {
            auto tile = tileBounds (i).toFloat().reduced (2.0f);
            const bool selected = (int) type == i;
            g.setColour (selected ? Palette::accent.withAlpha (0.22f)
                                  : (i == hover ? Palette::panelRaised.brighter (0.08f) : Palette::panelRaised));
            g.fillRoundedRectangle (tile, 4.0f);
            g.setColour (selected ? Palette::accent : Palette::divider);
            g.drawRoundedRectangle (tile, 4.0f, selected ? 1.6f : 1.0f);
            juce::Point<float> unused;
            paintObject (g, tile.reduced (3.0f, 2.0f), (dsp::ResType) i, 0.22f, false, false, &unused);
        }
    }

    CouplingArt::CouplingArt()
    {
        setComponentID ("couplingArt");
        setInterceptsMouseClicks (false, false);
    }

    void CouplingArt::setCoupled (bool shouldBeCoupled)
    {
        if (coupled == shouldBeCoupled)
            return;
        coupled = shouldBeCoupled;
        repaint();
    }

    void CouplingArt::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f, 4.0f);
        const auto mid = r.getCentre();
        const float nodeR = juce::jlimit (5.0f, 9.0f, r.getWidth() * 0.12f);
        const auto left = juce::Point<float> (r.getX() + nodeR + 2.0f, mid.y);
        const auto right = juce::Point<float> (r.getRight() - nodeR - 2.0f, mid.y);

        juce::Path spring;
        spring.startNewSubPath (left);
        const int coils = 7;
        const float span = right.x - left.x;
        const float amp = juce::jmin (r.getHeight() * 0.35f, 14.0f);
        for (int i = 1; i <= coils; ++i)
        {
            const float x = left.x + span * (float) i / (float) coils;
            const float y = mid.y + ((i % 2 == 0) ? amp : -amp);
            spring.lineTo (x, i == coils ? mid.y : y);
        }

        g.setColour (coupled ? Palette::accent.withAlpha (0.95f) : Palette::divider);
        g.strokePath (spring, juce::PathStrokeType (coupled ? 2.0f : 1.2f));
        g.setColour (coupled ? Palette::accent : Palette::textMuted);
        g.fillEllipse (left.x - nodeR, left.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.fillEllipse (right.x - nodeR, right.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.setColour (Palette::panel);
        g.fillEllipse (left.x - nodeR + 3.0f, left.y - nodeR + 3.0f, (nodeR - 3.0f) * 2.0f, (nodeR - 3.0f) * 2.0f);
        g.fillEllipse (right.x - nodeR + 3.0f, right.y - nodeR + 3.0f, (nodeR - 3.0f) * 2.0f, (nodeR - 3.0f) * 2.0f);
    }
}
