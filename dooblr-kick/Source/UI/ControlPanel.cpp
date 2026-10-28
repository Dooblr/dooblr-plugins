#include "ControlPanel.h"

#include "Theme.h"
#include "../Parameters.h"

#include <algorithm>
#include <map>

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
                    box->setTooltip (parameter->getName (64));

                    if (spec.hiddenLeadingChoices <= 0)
                    {
                        box->addItemList (parameter->getAllValueStrings(), 1);
                        item.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, spec.parameterId, *box);
                    }
                    else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                    {
                        // Item id k + 1 is choice hidden + k.
                        const auto hidden = spec.hiddenLeadingChoices;
                        const auto choices = parameter->getAllValueStrings();
                        for (int i = hidden; i < choices.size(); ++i)
                            box->addItem (choices[i], i - hidden + 1);

                        auto* rawBox = box.get();
                        item.choiceAttachment = std::make_unique<juce::ParameterAttachment> (*ranged, [rawBox, hidden] (float value)
                        {
                            const auto index = juce::roundToInt (value);
                            rawBox->setSelectedId (index >= hidden ? index - hidden + 1 : 0, juce::dontSendNotification);
                        });
                        auto* attachment = item.choiceAttachment.get();
                        box->onChange = [rawBox, attachment, hidden]
                        {
                            if (const auto id = rawBox->getSelectedId(); id > 0)
                                attachment->setValueAsCompleteGesture ((float) (id - 1 + hidden));
                        };
                        item.choiceAttachment->sendInitialUpdate();
                    }

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
            item.choiceAttachment.reset();
        }
    }

    void ControlPanel::addExtra (juce::Component& component, int width, int slot, const juce::String& insertBefore, bool fullCell)
    {
        Item item;
        item.extra = &component;
        item.extraFullCell = fullCell;
        item.width = width;
        item.spec.slot = slot;
        addAndMakeVisible (component);

        auto position = items.end();
        if (insertBefore.isNotEmpty())
            position = std::find_if (items.begin(), items.end(), [&] (const Item& i) { return i.control != nullptr && i.spec.parameterId == insertBefore; });

        items.insert (position, std::move (item));
        resized();
    }

    bool ControlPanel::setItemShown (Item& item, bool shouldBeShown)
    {
        if (item.shown == shouldBeShown)
            return false;

        item.shown = shouldBeShown;
        resized();
        return true;
    }

    bool ControlPanel::setShown (const juce::String& parameterId, bool shouldBeShown)
    {
        for (auto& item : items)
            if (item.control != nullptr && item.spec.parameterId == parameterId)
                return setItemShown (item, shouldBeShown);

        return false;
    }

    bool ControlPanel::setShown (juce::Component& extra, bool shouldBeShown)
    {
        for (auto& item : items)
            if (item.extra == &extra)
                return setItemShown (item, shouldBeShown);

        return false;
    }

    bool ControlPanel::isShown (const juce::String& parameterId) const
    {
        for (const auto& item : items)
            if (item.control != nullptr && item.spec.parameterId == parameterId)
                return item.shown;

        return false;
    }

    juce::Component* ControlPanel::findControl (const juce::String& parameterId) const
    {
        for (const auto& item : items)
            if (item.control != nullptr && item.spec.parameterId == parameterId)
                return item.control.get();

        return nullptr;
    }

    std::vector<juce::Rectangle<int>> ControlPanel::layoutCells (int width, std::vector<std::pair<int, juce::Rectangle<int>>>* slots) const
    {
        constexpr int gap = 6, minKnobWidth = 56;

        // Units of the flow: each shown unslotted item, and each slot (always, shown or not).
        struct Unit { bool knob = true; int width = 0; int slot = -1; juce::Rectangle<int> bounds; };
        std::vector<Unit> units;
        std::vector<int> unitOf (items.size(), -1);
        std::map<int, int> slotUnits;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const auto isKnob = item.extra == nullptr && item.spec.kind == Kind::knob;
            const auto w = isKnob ? (int) knobWidth : item.width;

            if (item.spec.slot >= 0)
            {
                if (auto found = slotUnits.find (item.spec.slot); found != slotUnits.end())
                {
                    auto& unit = units[(size_t) found->second];
                    unit.knob = unit.knob && isKnob;
                    unit.width = std::max (unit.width, w);
                    unitOf[i] = found->second;
                    continue;
                }

                slotUnits[item.spec.slot] = (int) units.size();
            }
            else if (! item.shown)
            {
                continue; // hidden and unslotted: takes no space
            }

            unitOf[i] = (int) units.size();
            units.push_back ({ isKnob, w, item.spec.slot, {} });
        }

        // Narrow windows: if slimmer knobs (down to 56 px) keep the panel on one row, use them
        // rather than wrapping onto a second row.
        int fixedWidth = 0, knobs = 0;
        for (const auto& unit : units)
        {
            knobs += unit.knob ? 1 : 0;
            fixedWidth += unit.knob ? 0 : unit.width;
        }
        fixedWidth += gap * std::max (0, (int) units.size() - 1);
        auto knobW = (int) knobWidth;
        if (knobs > 0 && fixedWidth + knobs * knobW > width && fixedWidth + knobs * minKnobWidth <= width)
            knobW = (width - fixedWidth) / knobs;

        int x = 0, y = titleHeight;
        for (auto& unit : units)
        {
            const auto w = unit.knob ? knobW : unit.width;

            if (x > 0 && x + w > width)
            {
                x = 0;
                y += knobHeight + gap;
            }

            unit.bounds = { x, y, w, knobHeight };
            x += w + gap;

            if (slots != nullptr && unit.slot >= 0)
                slots->emplace_back (unit.slot, unit.bounds);
        }

        std::vector<juce::Rectangle<int>> cells;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (unitOf[i] < 0)
            {
                cells.emplace_back();
                continue;
            }

            const auto& unit = units[(size_t) unitOf[i]];
            const auto& item = items[i];
            const auto isKnob = item.extra == nullptr && item.spec.kind == Kind::knob;
            // A member narrower than its slot sits at the slot's left (knobs: centred).
            const auto w = isKnob ? (unit.knob ? unit.bounds.getWidth() : std::min ((int) knobWidth, unit.bounds.getWidth())) : std::min (item.width, unit.bounds.getWidth());
            cells.push_back (isKnob ? unit.bounds.withSizeKeepingCentre (w, unit.bounds.getHeight()) : unit.bounds.withWidth (w));
        }

        return cells;
    }

    juce::Rectangle<int> ControlPanel::getSlotBounds (int slot) const
    {
        std::vector<std::pair<int, juce::Rectangle<int>>> slots;
        layoutCells (getWidth(), &slots);

        for (const auto& [index, bounds] : slots)
            if (index == slot)
                return bounds;

        return {};
    }

    int ControlPanel::getPreferredHeight (int width) const
    {
        int bottom = titleHeight;
        const auto cells = layoutCells (width);

        for (size_t i = 0; i < cells.size(); ++i)
            if (items[i].shown || items[i].spec.slot >= 0)
                bottom = std::max (bottom, cells[i].getBottom());

        return bottom;
    }

    void ControlPanel::resized()
    {
        const auto cells = layoutCells (getWidth());

        for (size_t i = 0; i < items.size(); ++i)
        {
            auto& item = items[i];
            auto cell = cells[i];

            for (auto* component : { item.extra, item.control.get(), (juce::Component*) item.caption.get() })
                if (component != nullptr)
                    component->setVisible (item.shown);

            if (! item.shown)
                continue;

            if (item.extra != nullptr)
            {
                item.extra->setBounds (item.extraFullCell ? cell : cell.withTrimmedTop (18).withHeight (26));
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
        // Fixed slots (a layer's source-specific controls): a faint backdrop marks the region
        // that swaps between Synth and Sample.
        std::vector<std::pair<int, juce::Rectangle<int>>> slots;
        layoutCells (getWidth(), &slots);
        if (! slots.empty())
        {
            auto region = slots.front().second;
            for (const auto& slot : slots)
                if (slot.second.getY() == region.getY())
                    region = region.getUnion (slot.second);

            g.setColour (colour.withAlpha (0.06f));
            g.fillRoundedRectangle (region.toFloat().expanded (3.0f, 0.0f), 5.0f);
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (16), juce::Justification::centredLeft);
    }

    //==============================================================================
    LevelFader::LevelFader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, juce::Colour colour)
    {
        setComponentID (parameterId + "_fader");

        caption.setText ("Level", juce::dontSendNotification);
        caption.setFont (juce::FontOptions (12.0f));
        caption.setColour (juce::Label::textColourId, Palette::textMuted);
        caption.setJustificationType (juce::Justification::centred);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);

        slider.setComponentID (parameterId);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, width, 18);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.getProperties().set ("faderScale", true); // v0.3.5: labelled taper marks

        if (auto* parameter = state.getParameter (parameterId))
        {
            slider.setTooltip (parameter->getName (64));
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
            slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        }

        slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        addAndMakeVisible (slider);
    }

    LevelFader::~LevelFader()
    {
        attachment.reset();
    }

    void LevelFader::resized()
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);
        slider.setBounds (area);
    }

    //==============================================================================
    EnvelopePointKnobs::EnvelopePointKnobs (juce::AudioProcessorValueTreeState& stateToUse, juce::Colour colour)
        : state (stateToUse)
    {
        setComponentID ("menv_point_knobs");
        count = state.getRawParameterValue (params::envelopeCountId (params::Envelope::master));

        caption.setText ("Point", juce::dontSendNotification);
        pointLabel.setComponentID ("menv_point_label");
        for (auto* label : { &caption, &pointLabel })
        {
            label->setFont (juce::FontOptions (12.0f));
            label->setColour (juce::Label::textColourId, label == &caption ? Palette::textMuted : Palette::textPrimary);
            label->setJustificationType (juce::Justification::centred);
            label->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*label);
        }

        previous.setComponentID ("menv_point_prev");
        next.setComponentID ("menv_point_next");
        previous.setTooltip ("Previous envelope point");
        next.setTooltip ("Next envelope point");
        previous.onClick = [this] { setPoint (point - 1); if (onPointChange) onPointChange (point); };
        next.onClick = [this] { setPoint (point + 1); if (onPointChange) onPointChange (point); };
        addAndMakeVisible (previous);
        addAndMakeVisible (next);

        const char* names[] { "Time", "Level", "Curve" };
        const char* ids[] { "menv_knob_time", "menv_knob_level", "menv_knob_curve" };

        for (size_t k = 0; k < knobs.size(); ++k)
        {
            auto& knob = knobs[k];
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, ControlPanel::knobWidth, 16);
            knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
            knob.getProperties().set ("maxKnobDiameter", 46.0);
            knob.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            knob.setComponentID (ids[k]);
            addAndMakeVisible (knob);

            knobCaptions[k].setText (names[k], juce::dontSendNotification);
            knobCaptions[k].setFont (juce::FontOptions (12.0f));
            knobCaptions[k].setColour (juce::Label::textColourId, Palette::textMuted);
            knobCaptions[k].setJustificationType (juce::Justification::centred);
            knobCaptions[k].setInterceptsMouseClicks (false, false);
            addAndMakeVisible (knobCaptions[k]);
        }

        point = -1;
        setPoint (1);

        // The point count can change under us (graph edits, undo, presets, automation).
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
        {
            if (getNumPoints() != shownCount)
                setPoint (point);
        });
    }

    EnvelopePointKnobs::~EnvelopePointKnobs()
    {
        for (auto& a : attachments)
            a.reset();
    }

    int EnvelopePointKnobs::getNumPoints() const
    {
        return juce::jlimit (2, params::maxNodes, juce::roundToInt (count->load()));
    }

    void EnvelopePointKnobs::setPoint (int newPoint)
    {
        const auto numPoints = getNumPoints();
        newPoint = juce::jlimit (0, numPoints - 1, newPoint);
        const auto changed = newPoint != point;
        point = newPoint;
        shownCount = numPoints;

        if (changed)
        {
            const juce::String wanted[] { point > 0 ? params::nodeTimeId (params::Envelope::master, point) : juce::String(),
                                          params::nodeValueId (params::Envelope::master, point),
                                          point > 0 ? params::nodeCurveId (params::Envelope::master, point - 1) : juce::String() };

            for (size_t k = 0; k < knobs.size(); ++k)
            {
                attachments[k].reset();
                boundIds[k] = wanted[k];
                auto& knob = knobs[k];

                if (auto* parameter = wanted[k].isNotEmpty() ? state.getParameter (wanted[k]) : nullptr)
                {
                    attachments[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, wanted[k], knob);
                    knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
                    knob.setTooltip (parameter->getName (64));
                    knob.setEnabled (true);
                }
                else
                {
                    knob.setEnabled (false);
                    knob.setTooltip (k == 0 ? "The first point is fixed at 0 ms" : "The first point has no segment before it");
                    knob.setValue (0.0, juce::dontSendNotification);
                }
            }
        }

        previous.setEnabled (point > 0);
        next.setEnabled (point < numPoints - 1);
        refreshPointLabel();
    }

    void EnvelopePointKnobs::refreshPointLabel()
    {
        pointLabel.setText (juce::String (point + 1) + " of " + juce::String (shownCount), juce::dontSendNotification);
    }

    void EnvelopePointKnobs::resized()
    {
        auto area = getLocalBounds();
        auto selector = area.removeFromLeft (120);
        area.removeFromLeft (6);
        caption.setBounds (selector.removeFromTop (16));
        auto row = selector.removeFromTop (28).reduced (0, 1);
        previous.setBounds (row.removeFromLeft (28));
        next.setBounds (row.removeFromRight (28));
        pointLabel.setBounds (row);

        for (size_t k = 0; k < knobs.size(); ++k)
        {
            auto cell = area.removeFromLeft (ControlPanel::knobWidth);
            area.removeFromLeft (6);
            knobCaptions[k].setBounds (cell.removeFromTop (16));
            knobs[k].setBounds (cell);
        }
    }

    void EnvelopePointKnobs::paint (juce::Graphics& g)
    {
        // The knobs follow the graph's selected point: a hairline groups them with the selector.
        const auto x = 120.0f + 3.0f;
        g.setColour (Palette::divider);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 18.0f, 1.0f, (float) getHeight() - 26.0f));
    }
}

