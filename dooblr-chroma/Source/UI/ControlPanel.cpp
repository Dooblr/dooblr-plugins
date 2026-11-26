#include "ControlPanel.h"

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
        caption.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
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
        caption.setBounds (r.removeFromTop (14));
        box.setBounds (r.reduced (2, 2));
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

    ResonatorView::ResonatorView()
    {
        setComponentID ("resonatorView");
        setOpaque (false);
    }

    void ResonatorView::setState (dsp::ResType t, float hitPosition, bool isCoupled)
    {
        type = t;
        hit = juce::jlimit (0.0f, 1.0f, hitPosition);
        coupled = isCoupled;
        repaint();
    }

    juce::Point<float> ResonatorView::getHitMarker() const { return marker; }

    void ResonatorView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        auto body = r.reduced (10.0f, 12.0f);
        g.setColour (Palette::spectrum.withAlpha (0.9f));

        juce::Point<float> border, centre;
        switch (type)
        {
            case dsp::ResType::string:
            {
                const float y = body.getCentreY();
                g.drawLine (body.getX(), y, body.getRight(), y, 1.6f);
                border = { body.getX(), y };
                centre = { body.getCentreX(), y };
                break;
            }
            case dsp::ResType::bar:
            case dsp::ResType::marimba:
            {
                auto bar = body.withSizeKeepingCentre (body.getWidth(), juce::jmin (18.0f, body.getHeight() * 0.35f));
                g.drawRoundedRectangle (bar, 3.0f, 1.6f);
                border = { bar.getX(), bar.getCentreY() };
                centre = { bar.getCentreX(), bar.getCentreY() };
                break;
            }
            case dsp::ResType::plate:
            {
                g.drawRoundedRectangle (body, 2.0f, 1.4f);
                border = body.getTopLeft();
                centre = body.getCentre();
                break;
            }
            case dsp::ResType::drumhead:
            {
                g.drawEllipse (body, 1.6f);
                border = { body.getX(), body.getCentreY() };
                centre = body.getCentre();
                break;
            }
            case dsp::ResType::membrane:
            {
                g.drawRoundedRectangle (body, 4.0f, 1.4f);
                border = body.getTopLeft();
                centre = body.getCentre();
                break;
            }
            case dsp::ResType::openTube:
            case dsp::ResType::closedTube:
            {
                auto tube = body.withSizeKeepingCentre (body.getWidth(), juce::jmin (22.0f, body.getHeight() * 0.4f));
                g.drawRoundedRectangle (tube, 8.0f, 1.6f);
                if (type == dsp::ResType::closedTube)
                {
                    g.drawLine (tube.getX(), tube.getY(), tube.getX(), tube.getBottom(), 2.0f);
                    border = { tube.getX(), tube.getCentreY() };
                }
                else
                    border = { tube.getX(), tube.getCentreY() };
                centre = { tube.getCentreX(), tube.getCentreY() };
                break;
            }
            case dsp::ResType::manual:
            {
                for (int i = 0; i < 4; ++i)
                {
                    const float x = body.getX() + body.getWidth() * (0.15f + 0.23f * (float) i);
                    g.fillEllipse (x - 3.5f, body.getCentreY() - 3.5f, 7.0f, 7.0f);
                }
                border = { body.getX(), body.getCentreY() };
                centre = body.getCentre();
                break;
            }
        }

        marker = border + (centre - border) * hit;
        g.setColour (Palette::accent);
        g.fillEllipse (marker.x - 4.5f, marker.y - 4.5f, 9.0f, 9.0f);
        if (coupled)
        {
            g.setColour (Palette::dynamic.withAlpha (0.85f));
            g.drawEllipse (centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f, 1.2f);
        }
    }
}
