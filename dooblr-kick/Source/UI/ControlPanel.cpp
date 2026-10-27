#include "ControlPanel.h"

#include "Theme.h"

namespace dooblrkick::ui
{
    ControlPanel::ControlPanel (juce::AudioProcessorValueTreeState& state, const juce::String& titleToUse,
                                juce::Colour colourToUse, const std::vector<Spec>& specs)
        : title (titleToUse), colour (colourToUse)
    {
        setComponentID ("panel_" + title);

        for (const auto& spec : specs)
        {
            auto* parameter = state.getParameter (spec.parameterId);
            jassert (parameter != nullptr);

            if (parameter == nullptr)
                continue;

            Item item;
            item.spec = spec;

            switch (spec.kind)
            {
                case Kind::knob:
                {
                    auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                    slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth, 16);
                    slider->setColour (juce::Slider::rotarySliderFillColourId, colour);
                    slider->getProperties().set ("maxKnobDiameter", 46.0);
                    slider->setTooltip (parameter->getName (64));
                    item.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, spec.parameterId, *slider);

                    // Double-click resets; Shift + drag is a fine, velocity-free adjustment.
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                        slider->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));

                    slider->setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);

                    // Tunes span 72 semitones: about 10 px per semitone for a plain drag.
                    if (parameter->getLabel() == "st")
                        slider->setMouseDragSensitivity (720);

                    item.control = std::move (slider);
                    item.width = knobWidth;

                    item.caption = std::make_unique<juce::Label>();
                    item.caption->setText (spec.caption, juce::dontSendNotification);
                    item.caption->setFont (juce::FontOptions (12.0f));
                    item.caption->setColour (juce::Label::textColourId, Palette::textMuted);
                    item.caption->setJustificationType (juce::Justification::centred);
                    item.caption->setInterceptsMouseClicks (false, false);
                    break;
                }
                case Kind::toggle:
                {
                    auto button = std::make_unique<juce::ToggleButton> (spec.caption);
                    button->setTooltip (parameter->getName (64));
                    item.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, spec.parameterId, *button);
                    item.control = std::move (button);
                    item.width = toggleWidth;
                    break;
                }
                case Kind::combo:
                {
                    auto box = std::make_unique<juce::ComboBox>();
                    box->addItemList (parameter->getAllValueStrings(), 1);
                    box->setTooltip (parameter->getName (64));
                    item.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, spec.parameterId, *box);
                    item.control = std::move (box);
                    item.width = spec.width > 0 ? spec.width : comboWidth;

                    item.caption = std::make_unique<juce::Label>();
                    item.caption->setText (spec.caption, juce::dontSendNotification);
                    item.caption->setFont (juce::FontOptions (12.0f));
                    item.caption->setColour (juce::Label::textColourId, Palette::textMuted);
                    item.caption->setJustificationType (juce::Justification::centredLeft);
                    item.caption->setInterceptsMouseClicks (false, false);
                    break;
                }
            }

            item.control->setComponentID (spec.parameterId);
            addAndMakeVisible (*item.control);

            if (item.caption != nullptr)
                addAndMakeVisible (*item.caption);

            items.push_back (std::move (item));
        }
    }

    ControlPanel::~ControlPanel()
    {
        // Attachments go before their controls.
        for (auto& item : items)
        {
            item.sliderAttachment.reset();
            item.buttonAttachment.reset();
            item.comboAttachment.reset();
        }
    }

    void ControlPanel::addExtra (juce::Component& component, int width)
    {
        Item item;
        item.extra = &component;
        item.width = width;
        addAndMakeVisible (component);
        items.push_back (std::move (item));
        resized();
    }

    juce::Component* ControlPanel::findControl (const juce::String& parameterId) const
    {
        for (const auto& item : items)
            if (item.control != nullptr && item.spec.parameterId == parameterId)
                return item.control.get();

        return nullptr;
    }

    std::vector<juce::Rectangle<int>> ControlPanel::layoutCells (int width) const
    {
        std::vector<juce::Rectangle<int>> cells;
        int x = 0, y = titleHeight;
        constexpr int gap = 6, minKnobWidth = 56;

        // Narrow windows: if slimmer knobs (down to 56 px) keep the panel on one row, use them
        // rather than wrapping onto a second row.
        int fixedWidth = 0, knobs = 0;
        for (const auto& item : items)
        {
            const auto isKnob = item.extra == nullptr && item.spec.kind == Kind::knob;
            knobs += isKnob ? 1 : 0;
            fixedWidth += isKnob ? 0 : item.width;
        }
        fixedWidth += gap * std::max (0, (int) items.size() - 1);
        auto knobW = (int) knobWidth;
        if (knobs > 0 && fixedWidth + knobs * knobW > width && fixedWidth + knobs * minKnobWidth <= width)
            knobW = (width - fixedWidth) / knobs;

        for (const auto& item : items)
        {
            const auto w = item.extra == nullptr && item.spec.kind == Kind::knob ? knobW : item.width;

            if (x > 0 && x + w > width)
            {
                x = 0;
                y += knobHeight + gap;
            }

            cells.emplace_back (x, y, w, knobHeight);
            x += w + gap;
        }

        return cells;
    }

    int ControlPanel::getPreferredHeight (int width) const
    {
        const auto cells = layoutCells (width);
        return cells.empty() ? titleHeight : cells.back().getBottom();
    }

    void ControlPanel::resized()
    {
        const auto cells = layoutCells (getWidth());

        for (size_t i = 0; i < items.size(); ++i)
        {
            auto& item = items[i];
            auto cell = cells[i];

            if (item.extra != nullptr)
            {
                item.extra->setBounds (cell.withTrimmedTop (18).withHeight (26));
                continue;
            }

            switch (item.spec.kind)
            {
                case Kind::knob:
                    item.caption->setBounds (cell.removeFromTop (16));
                    item.control->setBounds (cell);
                    break;
                case Kind::combo:
                    item.caption->setBounds (cell.removeFromTop (16));
                    item.control->setBounds (cell.removeFromTop (28).reduced (0, 1));
                    break;
                case Kind::toggle:
                    item.control->setBounds (cell.withTrimmedTop (18).withHeight (26));
                    break;
            }
        }
    }

    void ControlPanel::paint (juce::Graphics& g)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (16), juce::Justification::centredLeft);
    }
}
