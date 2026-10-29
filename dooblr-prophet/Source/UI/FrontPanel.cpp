#include "FrontPanel.h"

#include <cmath>

#include "Theme.h"

namespace dooblrprophet::ui
{
    namespace
    {
        /** Text and fixed rows grow a little with the cell width, so the 2400 px panel is not
            all small type in big boxes. 1 at the 940 / 1080 px sizes' cell widths. */
        float scaleForUnit (float unit) { return juce::jlimit (1.0f, 1.45f, unit / 62.0f); }

        juce::Font titleFont (float scale) { return juce::Font (juce::FontOptions (11.5f * scale, juce::Font::bold).withKerningFactor (0.06f)); }
    }

    Section::Section (juce::AudioProcessorValueTreeState& state, const juce::String& titleToUse, juce::Colour colourToUse,
                      int columnsToUse, const std::vector<Row>& rowSpecs)
        : title (titleToUse), colour (colourToUse), columns (juce::jmax (1, columnsToUse))
    {
        setComponentID ("section_" + title);

        for (const auto& rowSpec : rowSpecs)
        {
            std::vector<Item> row;

            for (const auto& cell : rowSpec)
            {
                Item item;
                item.cell = cell;
                auto* parameter = cell.kind == Kind::extra ? nullptr : state.getParameter (cell.parameterId);
                jassert (cell.kind == Kind::extra ? cell.extra != nullptr : parameter != nullptr);

                switch (cell.kind)
                {
                    case Kind::knob:
                    {
                        if (parameter == nullptr) continue;
                        auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                        slider->setColour (juce::Slider::rotarySliderFillColourId, colour);
                        slider->setTooltip (parameter->getName (64));
                        item.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, cell.parameterId, *slider);

                        // Double-click resets; Shift + drag is a fine, velocity-free adjustment.
                        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                            slider->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));

                        slider->setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);

                        // Tunes span 72 semitones: about 10 px per semitone for a plain drag.
                        if (parameter->getLabel() == "st")
                            slider->setMouseDragSensitivity (720);

                        item.control = std::move (slider);
                        item.caption = std::make_unique<juce::Label>();
                        item.caption->setText (cell.caption, juce::dontSendNotification);
                        item.caption->setColour (juce::Label::textColourId, Palette::textMuted);
                        item.caption->setJustificationType (juce::Justification::centred);
                        item.caption->setMinimumHorizontalScale (0.8f);
                        item.caption->setBorderSize ({});
                        item.caption->setInterceptsMouseClicks (false, false);
                        break;
                    }
                    case Kind::toggle:
                    {
                        if (parameter == nullptr) continue;
                        auto button = std::make_unique<juce::ToggleButton> (cell.caption);
                        button->setTooltip (parameter->getName (64));
                        button->setColour (juce::ToggleButton::tickColourId, colour);
                        item.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, cell.parameterId, *button);
                        item.control = std::move (button);
                        break;
                    }
                    case Kind::combo:
                    {
                        if (parameter == nullptr) continue;
                        auto box = std::make_unique<juce::ComboBox>();
                        box->setTooltip (parameter->getName (64));
                        box->addItemList (parameter->getAllValueStrings(), 1);
                        item.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, cell.parameterId, *box);
                        item.control = std::move (box);
                        item.caption = std::make_unique<juce::Label>();
                        item.caption->setText (cell.caption, juce::dontSendNotification);
                        item.caption->setColour (juce::Label::textColourId, Palette::textMuted);
                        item.caption->setJustificationType (juce::Justification::centredLeft);
                        item.caption->setBorderSize ({});
                        item.caption->setInterceptsMouseClicks (false, false);
                        break;
                    }
                    case Kind::extra:
                        if (cell.extra == nullptr) continue;
                        addAndMakeVisible (*cell.extra);
                        break;
                }

                if (item.control != nullptr)
                {
                    item.control->setComponentID (cell.parameterId);
                    addAndMakeVisible (*item.control);
                }

                if (item.caption != nullptr)
                    addAndMakeVisible (*item.caption);

                row.push_back (std::move (item));
            }

            if (! row.empty())
                rows.push_back (std::move (row));
        }
    }

    Section::~Section()
    {
        // Attachments go before their controls.
        for (auto& row : rows)
            for (auto& item : row)
            {
                item.sliderAttachment.reset();
                item.buttonAttachment.reset();
                item.comboAttachment.reset();
            }

        titleSwitch.reset();
    }

    SegmentedToggle& Section::setTitleSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                                              const juce::StringArray& labels)
    {
        titleSwitch = std::make_unique<SegmentedToggle> (state, parameterId, labels, colour);
        titleSwitch->setTooltip (state.getParameter (parameterId)->getName (64));

        const juce::Font font (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.06f));
        float widest = 0.0f;
        for (const auto& label : labels)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, label));
        titleSwitchWidth = labels.size() * (juce::roundToInt (widest) + 16);

        addAndMakeVisible (*titleSwitch);
        resized();
        return *titleSwitch;
    }

    Section::Kind Section::rowKind (const std::vector<Item>& row)
    {
        for (const auto& item : row)
            if (item.cell.kind == Kind::knob)
                return Kind::knob;

        for (const auto& item : row)
            if (item.cell.kind == Kind::combo)
                return Kind::combo;

        return row.empty() ? Kind::extra : row.front().cell.kind;
    }

    int Section::fixedRowHeight (Kind kind)
    {
        switch (kind)
        {
            case Kind::toggle: return toggleRowHeight;
            case Kind::combo:  return comboRowHeight;
            case Kind::extra:  return extraRowHeight;
            case Kind::knob:   break;
        }

        return 0;
    }

    int Section::getNumKnobRows() const
    {
        int n = 0;
        for (const auto& row : rows)
            if (rowKind (row) == Kind::knob)
                ++n;
        return n;
    }

    int Section::getPreferredHeight (int knobRowHeight) const
    {
        const auto scale = scaleForUnit ((float) (getWidth() - 2 * padding) / (float) columns);
        auto height = juce::roundToInt (titleHeight * scale) + padding;

        for (const auto& row : rows)
        {
            const auto kind = rowKind (row);
            height += (kind == Kind::knob ? knobRowHeight : juce::roundToInt ((float) fixedRowHeight (kind) * scale)) + rowGap;
        }

        return height;
    }

    juce::Rectangle<int> Section::getTitleBounds() const
    {
        const auto scale = scaleForUnit ((float) (getWidth() - 2 * padding) / (float) columns);
        const auto line = getLocalBounds().reduced (padding, 0).removeFromTop (juce::roundToInt (titleHeight * scale));
        const auto width = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont (scale), title.toUpperCase())) + 2;
        return line.withWidth (juce::jmin (width, line.getWidth()));
    }

    void Section::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        const auto scale = scaleForUnit ((float) (getWidth() - 2 * padding) / (float) columns);
        const auto titleArea = getTitleBounds();
        g.setColour (colour);
        g.setFont (titleFont (scale));
        g.drawText (title.toUpperCase(), titleArea, juce::Justification::centredLeft, false);

        // A short colour bar under the title, and a hairline across the section.
        g.fillRect ((float) padding, (float) titleArea.getBottom() - 1.0f, (float) juce::jmin (28, titleArea.getWidth()), 2.0f);
        g.setColour (Palette::divider);
        g.fillRect ((float) padding + 30.0f, (float) titleArea.getBottom() - 0.5f, (float) getWidth() - 2.0f * padding - 30.0f, 1.0f);
    }

    void Section::resized()
    {
        const auto unit = (float) (getWidth() - 2 * padding) / (float) columns;
        const auto scale = scaleForUnit (unit);

        auto area = getLocalBounds().reduced (padding, 0);
        auto titleLine = area.removeFromTop (juce::roundToInt (titleHeight * scale));
        area.removeFromBottom (padding);

        if (titleSwitch != nullptr)
        {
            const auto titleRight = getTitleBounds().getRight() + 10;
            const auto width = juce::jmin (juce::roundToInt ((float) titleSwitchWidth * scale), titleLine.getRight() - titleRight);
            titleSwitch->setBounds (titleLine.removeFromRight (juce::jmax (0, width))
                                        .withSizeKeepingCentre (juce::jmax (0, width), juce::roundToInt (19.0f * scale)));
        }

        int fixed = 0, knobRows = 0;
        for (const auto& row : rows)
        {
            const auto kind = rowKind (row);
            fixed += rowGap + (kind == Kind::knob ? 0 : juce::roundToInt ((float) fixedRowHeight (kind) * scale));
            knobRows += kind == Kind::knob ? 1 : 0;
        }

        const auto knobRowHeight = knobRows > 0 ? juce::jmax (0, (area.getHeight() - fixed) / knobRows) : 0;

        for (auto& row : rows)
        {
            area.removeFromTop (rowGap);
            const auto kind = rowKind (row);
            const auto rowHeight = kind == Kind::knob ? knobRowHeight : juce::roundToInt ((float) fixedRowHeight (kind) * scale);
            const auto rowArea = area.removeFromTop (rowHeight);
            auto x = (float) rowArea.getX();

            for (auto& item : row)
            {
                const auto left = juce::roundToInt (x);
                x += item.cell.span * unit;
                const juce::Rectangle<int> cell (left, rowArea.getY(), juce::roundToInt (x) - left, rowHeight);

                switch (item.cell.kind)
                {
                    case Kind::knob:
                    {
                        auto* slider = static_cast<juce::Slider*> (item.control.get());
                        const auto cellScale = scaleForUnit ((float) cell.getWidth());
                        const auto captionHeight = juce::roundToInt (14.0f * cellScale);
                        const auto valueHeight = juce::roundToInt (15.0f * cellScale);
                        const auto knobSize = juce::jlimit (0, juce::roundToInt (88.0f * cellScale),
                                                            juce::jmin (cell.getWidth() - 4, rowHeight - captionHeight - valueHeight - 2));
                        const auto block = captionHeight + knobSize + valueHeight;
                        const auto top = cell.getY() + (rowHeight - block) / 2;

                        item.caption->setFont (juce::FontOptions (12.0f * cellScale));
                        item.caption->setBounds (cell.getX(), top, cell.getWidth(), captionHeight);
                        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, cell.getWidth() - 4, valueHeight);
                        slider->setBounds (cell.getX(), top + captionHeight, cell.getWidth(), knobSize + valueHeight);
                        break;
                    }
                    case Kind::toggle:
                        item.control->getProperties().set ("toggleScale", scale);
                        item.control->setBounds (cell.reduced (2, 1));
                        break;
                    case Kind::combo:
                    {
                        auto c = cell.reduced (3, 0);
                        item.caption->setFont (juce::FontOptions (12.0f * scale));
                        item.caption->setBounds (c.removeFromTop (juce::roundToInt (15.0f * scale)));
                        item.control->setBounds (c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), juce::roundToInt (24.0f * scale))));
                        break;
                    }
                    case Kind::extra:
                        item.cell.extra->setBounds (cell.reduced (2, 1));
                        break;
                }
            }
        }
    }

    juce::Component* Section::findControl (const juce::String& parameterId) const
    {
        if (titleSwitch != nullptr && titleSwitch->getComponentID() == parameterId)
            return titleSwitch.get();

        for (const auto& row : rows)
            for (const auto& item : row)
                if (item.control != nullptr && item.cell.parameterId == parameterId)
                    return item.control.get();

        return nullptr;
    }
}
