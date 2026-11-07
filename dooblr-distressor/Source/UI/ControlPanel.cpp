#include "ControlPanel.h"

#include "Theme.h"

#include <algorithm>
#include <map>

namespace dooblrdistressor::ui
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
                    // Sized to its caption (pill 30 + text + padding), so long rows stay on one line.
                    const auto textWidth = juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.0f), spec.caption);
                    item.width = juce::jlimit (72, (int) toggleWidth, (int) std::ceil (textWidth) + 30 + 12 + 6);
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

    namespace
    {
        class SegmentedToggleAccessibility final : public juce::AccessibilityHandler
        {
        public:
            explicit SegmentedToggleAccessibility (SegmentedToggle& t)
                : juce::AccessibilityHandler (t, juce::AccessibilityRole::toggleButton,
                                              juce::AccessibilityActions()
                                                  .addAction (juce::AccessibilityActionType::toggle, [&t] { t.selectNext(); })
                                                  .addAction (juce::AccessibilityActionType::press, [&t] { t.selectNext(); })),
                  toggle (t)
            {
            }

            juce::AccessibleState getCurrentState() const override
            {
                auto s = juce::AccessibilityHandler::getCurrentState().withCheckable();
                // Two segments: checked = the second one. More: not a checkable on / off.
                if (toggle.getNumSegments() != 2)
                    return juce::AccessibilityHandler::getCurrentState();
                return toggle.getSelectedIndex() == 1 ? s.withChecked() : s;
            }

            juce::String getHelp() const override { return toggle.getSegmentText (toggle.getSelectedIndex()); }

        private:
            SegmentedToggle& toggle;
        };
    }

    SegmentedToggle::SegmentedToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                                      const juce::StringArray& segmentLabels, juce::Colour segmentColour)
        : labels (segmentLabels), colour (segmentColour)
    {
        setComponentID (parameterId);
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (labels.joinIntoString (" / "));

        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (parameterId)))
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float value)
            {
                const auto index = juce::jlimit (0, juce::jmax (0, labels.size() - 1), juce::roundToInt (value));
                if (index != selected)
                {
                    selected = index;
                    repaint();
                    if (auto* h = getAccessibilityHandler())
                        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
                }
            }, state.undoManager);
            attachment->sendInitialUpdate();
        }
    }

    SegmentedToggle::~SegmentedToggle()
    {
        attachment.reset();
    }

    juce::Rectangle<int> SegmentedToggle::getSegmentBounds (int index) const
    {
        const auto area = getLocalBounds();
        const auto n = juce::jmax (1, labels.size());
        const auto x0 = area.getWidth() * index / n, x1 = area.getWidth() * (index + 1) / n;
        return { x0, 0, x1 - x0, area.getHeight() };
    }

    int SegmentedToggle::segmentAt (juce::Point<int> position) const
    {
        if (! getLocalBounds().contains (position))
            return -1;

        for (int i = 0; i < labels.size(); ++i)
            if (getSegmentBounds (i).contains (position))
                return i;

        return -1;
    }

    void SegmentedToggle::select (int index)
    {
        if (attachment == nullptr || index < 0 || index >= labels.size() || index == selected || ! isEnabled())
            return;

        attachment->setValueAsCompleteGesture ((float) index);
    }

    void SegmentedToggle::selectNext()
    {
        if (labels.size() > 1)
            select ((selected + 1) % labels.size());
    }

    void SegmentedToggle::setHovered (int index)
    {
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    void SegmentedToggle::mouseMove (const juce::MouseEvent& e) { setHovered (segmentAt (e.getPosition())); }
    void SegmentedToggle::mouseExit (const juce::MouseEvent&) { setHovered (-1); }

    void SegmentedToggle::mouseDown (const juce::MouseEvent& e)
    {
        pressed = segmentAt (e.getPosition());
        setHovered (pressed);
        repaint();
    }

    void SegmentedToggle::mouseDrag (const juce::MouseEvent& e)
    {
        // While pressed, only the pressed segment shows as hot (and only while over it).
        const auto at = segmentAt (e.getPosition());
        setHovered (at == pressed ? at : -1);
    }

    void SegmentedToggle::mouseUp (const juce::MouseEvent& e)
    {
        const auto at = segmentAt (e.getPosition());
        const auto target = pressed;
        pressed = -1;
        setHovered (at);
        repaint();

        if (target >= 0 && at == target)
            select (target);
    }

    bool SegmentedToggle::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::leftKey)  { select (selected - 1); return true; }
        if (key == juce::KeyPress::rightKey) { select (selected + 1); return true; }
        if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        {
            selectNext();
            return true;
        }
        return false;
    }

    std::unique_ptr<juce::AccessibilityHandler> SegmentedToggle::createAccessibilityHandler()
    {
        return std::make_unique<SegmentedToggleAccessibility> (*this);
    }

    void SegmentedToggle::paint (juce::Graphics& g)
    {
        const auto enabled = ! isDimmed (*this);
        const auto track = getLocalBounds().toFloat().reduced (0.5f);
        const auto radius = juce::jmin (8.0f, track.getHeight() * 0.3f);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (track, radius);
        g.setColour (hasKeyboardFocus (false) ? colour.withAlpha (0.9f) : Palette::grid.brighter (0.35f));
        g.drawRoundedRectangle (track, radius, 1.0f);

        const auto fontHeight = juce::jlimit (13.0f, 17.0f, (float) getHeight() * 0.40f);
        g.setFont (juce::FontOptions (fontHeight, juce::Font::bold).withKerningFactor (0.06f));

        for (int i = 0; i < labels.size(); ++i)
        {
            const auto segment = getSegmentBounds (i).toFloat().reduced (3.0f);
            const auto active = i == selected;
            const auto hot = enabled && i == hovered;
            const auto down = hot && i == pressed;

            if (active)
            {
                g.setColour (colour.withAlpha (enabled ? (hot ? 1.0f : 0.92f) : 0.4f));
                g.fillRoundedRectangle (segment, radius - 2.0f);
                g.setColour (Palette::background);
            }
            else
            {
                if (hot)
                {
                    g.setColour (colour.withAlpha (down ? 0.30f : 0.16f));
                    g.fillRoundedRectangle (segment, radius - 2.0f);
                }

                g.setColour (hot ? Palette::textPrimary : Palette::textMuted.withAlpha (0.7f));
            }

            g.drawFittedText (labels[i], segment.toNearestInt(), juce::Justification::centred, 1, 0.75f);
        }
    }
}
