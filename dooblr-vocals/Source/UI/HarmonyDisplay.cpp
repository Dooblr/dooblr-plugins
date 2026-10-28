#include "HarmonyDisplay.h"

#include <optional>

#include "../Parameters.h"
#include "Theme.h"

namespace dooblrvocals::ui
{
    namespace
    {
        using APVTS = juce::AudioProcessorValueTreeState;

        float plain (APVTS& state, const juce::String& id)
        {
            auto* p = state.getParameter (id);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        }
    }

    //==============================================================================
    class HarmonyDisplay::Strip final : public juce::Component
    {
    public:
        Strip (APVTS& s, int voiceIndex) : state (s), voice (voiceIndex)
        {
            const auto id = [this] (const char* suffix) { return params::harmonyId (voice, suffix); };
            setComponentID ("harmonyVoice" + juce::String (voice + 1));

            onButton.setButtonText ("On");
            onButton.setComponentID (id ("on"));
            onButton.setTooltip ("Voice " + juce::String (voice + 1) + " on/off");
            onAttachment = std::make_unique<APVTS::ButtonAttachment> (state, id ("on"), onButton);
            addAndMakeVisible (onButton);

            modeBox.setComponentID (id ("mode"));
            modeBox.addItemList (state.getParameter (id ("mode"))->getAllValueStrings(), 1);
            modeBox.setTooltip ("Scale: diatonic steps in the plugin key and scale. Fixed: semitones.");
            modeAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, id ("mode"), modeBox);
            addAndMakeVisible (modeBox);

            const char* suffixes[] = { "degree", "semis", "level", "pan", "detune", "delay" };
            const char* captions[] = { "Interval", "Interval", "Level", "Pan", "Detune", "Delay" };

            for (size_t k = 0; k < knobs.size(); ++k)
            {
                auto& knob = knobs[k];
                auto* parameter = state.getParameter (id (suffixes[k]));
                knob.caption = captions[k];
                knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobCell, 16);
                knob.slider.setComponentID (id (suffixes[k]));
                knob.slider.setColour (juce::Slider::rotarySliderFillColourId, Palette::master);
                knob.slider.getProperties().set ("maxKnobDiameter", 40.0);
                knob.slider.setTooltip (parameter->getName (64));
                knob.attachment = std::make_unique<APVTS::SliderAttachment> (state, id (suffixes[k]), knob.slider);
                knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
                knob.slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
                addAndMakeVisible (knob.slider);
            }

            // Interval knobs step in whole degrees / semitones: about 12 px per step.
            knobs[0].slider.setMouseDragSensitivity (340);
            knobs[1].slider.setMouseDragSensitivity (580);
            refresh();
        }

        ~Strip() override
        {
            onAttachment.reset();
            modeAttachment.reset();
            for (auto& knob : knobs)
                knob.attachment.reset();
        }

        void refresh (int keyOverride = -1)
        {
            const auto id = [this] (const char* suffix) { return params::harmonyId (voice, suffix); };
            fixed = juce::roundToInt (plain (state, id ("mode"))) == 1;
            const auto voiceOn = plain (state, id ("on")) > 0.5f;
            const auto busOn = plain (state, "harm_on") > 0.5f;

            const auto changed = knobs[0].slider.isVisible() == fixed;
            knobs[0].slider.setVisible (! fixed);
            knobs[1].slider.setVisible (fixed);
            if (changed)
                resized();

            // Inactive voices are drawn in muted colours (they stay editable). Not setAlpha:
            // a translucent component renders through an offscreen layer, which made this
            // page about ten times slower to paint than the others.
            const auto active = voiceOn && busOn;
            if (active != coloursActive)
            {
                coloursActive = active;
                for (auto& knob : knobs)
                {
                    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, active ? Palette::master : Palette::master.withAlpha (0.4f));
                    knob.slider.setColour (juce::Slider::textBoxTextColourId, active ? Palette::textPrimary : Palette::textMuted);
                    knob.slider.setColour (juce::Slider::textBoxOutlineColourId,
                                           getLookAndFeel().findColour (juce::Slider::textBoxOutlineColourId).withMultipliedAlpha (active ? 1.0f : 0.5f));
                }
                modeBox.setColour (juce::ComboBox::textColourId, active ? Palette::textPrimary : Palette::textMuted);
                modeBox.setColour (juce::ComboBox::arrowColourId, active ? Palette::textPrimary : Palette::textMuted);
            }

            juce::String text;
            if (fixed)
            {
                const auto semis = juce::roundToInt (plain (state, id ("semis")));
                text = (semis > 0 ? "+" : "") + juce::String (semis) + " semitones";
            }
            else
            {
                const auto degrees = juce::roundToInt (plain (state, id ("degree")));
                const auto custom = keyOverride < 0
                                 && juce::roundToInt (plain (state, "corr_scale")) == (int) dsp::ScaleType::custom;
                const auto keyIndex = keyOverride >= 0 ? keyOverride % 12 : juce::roundToInt (plain (state, "corr_key"));
                const auto scaleIndex = keyOverride >= 0 ? (keyOverride >= 12 ? (int) dsp::ScaleType::minor : (int) dsp::ScaleType::major)
                                                         : juce::roundToInt (plain (state, "corr_scale"));
                text = params::degreeName (degrees) + " in "
                       + (custom ? juce::String ("custom scale")
                                 : params::keyChoices()[keyIndex] + " " + params::scaleChoices()[scaleIndex]);
            }

            const auto newPan = plain (state, id ("pan"));
            if (text != intervalText || std::abs (newPan - pan) > 1.0e-6f || active != shownActive)
            {
                intervalText = text;
                pan = newPan;
                shownActive = active;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat();
            g.setColour (Palette::panelRaised.withAlpha (shownActive ? 1.0f : 0.6f));
            g.fillRoundedRectangle (area, 6.0f);
            g.setColour (Palette::master.withAlpha (shownActive ? 1.0f : 0.35f));
            g.fillRoundedRectangle (area.removeFromLeft (3.0f).reduced (0.0f, 6.0f), 1.5f);

            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (shownActive ? Palette::master : Palette::textMuted);
            g.drawText ("VOICE " + juce::String (voice + 1), titleArea, juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (12.0f));
            g.setColour (Palette::textMuted);
            for (size_t k = 0; k < knobs.size(); ++k)
                if (knobs[k].slider.isVisible())
                    g.drawText (knobs[k].caption, knobs[k].captionArea, juce::Justification::centred);

            if (readoutArea.getWidth() < 60)
                return;

            auto r = readoutArea.toFloat();
            g.setColour (shownActive ? Palette::textPrimary : Palette::textMuted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (intervalText, r.removeFromTop (r.getHeight() * 0.5f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            // Pan position: L - C - R track with the voice's dot.
            auto track = r.withSizeKeepingCentre (r.getWidth(), 14.0f).reduced (12.0f, 0.0f);
            g.setColour (Palette::divider);
            g.fillRoundedRectangle (track.withSizeKeepingCentre (track.getWidth(), 2.0f), 1.0f);
            g.fillRect (juce::Rectangle<float> (1.0f, 8.0f).withCentre (track.getCentre()));
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (Palette::textMuted);
            g.drawText ("L", juce::Rectangle<float> (12.0f, 14.0f).withCentre ({ track.getX() - 8.0f, track.getCentreY() }), juce::Justification::centred);
            g.drawText ("R", juce::Rectangle<float> (12.0f, 14.0f).withCentre ({ track.getRight() + 8.0f, track.getCentreY() }), juce::Justification::centred);
            const auto x = track.getX() + (pan + 1.0f) * 0.5f * track.getWidth();
            g.setColour (Palette::master.withAlpha (shownActive ? 1.0f : 0.4f));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, track.getCentreY() }));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 4);
            area.removeFromLeft (2);
            auto header = area.removeFromLeft (headerWidth - 12);
            titleArea = header.removeFromTop (16);
            const auto rowH = juce::jmin (26, (header.getHeight() - 4) / 2);
            onButton.setBounds (header.removeFromTop (rowH));
            header.removeFromTop (4);
            modeBox.setBounds (header.removeFromTop (rowH).withWidth (juce::jmin (header.getWidth(), 104)));
            area.removeFromLeft (8);

            const auto cell = [&]
            {
                auto c = area.removeFromLeft (knobCell);
                area.removeFromLeft (4);
                return c;
            };

            const auto place = [] (Knob& knob, juce::Rectangle<int> c)
            {
                knob.captionArea = c.removeFromTop (14);
                knob.slider.setBounds (c);
            };

            const auto intervalCell = cell();
            place (knobs[0], intervalCell);
            place (knobs[1], intervalCell);
            for (size_t k = 2; k < knobs.size(); ++k)
                place (knobs[k], cell());

            area.removeFromLeft (8);
            readoutArea = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 220), juce::jmin (area.getHeight(), 52));
            readoutArea.setX (area.getX());
        }

    private:
        struct Knob
        {
            juce::Slider slider;
            std::unique_ptr<APVTS::SliderAttachment> attachment;
            juce::String caption;
            juce::Rectangle<int> captionArea;
        };

        APVTS& state;
        const int voice;
        juce::ToggleButton onButton;
        juce::ComboBox modeBox;
        std::array<Knob, 6> knobs;
        std::unique_ptr<APVTS::ButtonAttachment> onAttachment;
        std::unique_ptr<APVTS::ComboBoxAttachment> modeAttachment;
        juce::Rectangle<int> titleArea, readoutArea;
        juce::String intervalText;
        float pan = 0.0f;
        bool fixed = false, shownActive = true;
        std::optional<bool> coloursActive;
    };

    //==============================================================================
    HarmonyDisplay::HarmonyDisplay (APVTS& s) : state (s)
    {
        setComponentID ("harmonyDisplay");
        for (int v = 0; v < numVoices; ++v)
        {
            strips[(size_t) v] = std::make_unique<Strip> (state, v);
            addAndMakeVisible (*strips[(size_t) v]);
        }
    }

    HarmonyDisplay::~HarmonyDisplay() = default;

    void HarmonyDisplay::refresh()
    {
        for (auto& strip : strips)
            strip->refresh (followedKey);
    }

    void HarmonyDisplay::setFollowedKey (int key)
    {
        key = (key >= 0 && key < 24) ? key : -1;
        if (key != followedKey)
        {
            followedKey = key;
            refresh();
        }
    }

    juce::Component* HarmonyDisplay::findControl (const juce::String& parameterId) const
    {
        for (auto& strip : strips)
            for (auto* child : strip->getChildren())
                if (child->getComponentID() == parameterId)
                    return child;
        return nullptr;
    }

    void HarmonyDisplay::paint (juce::Graphics&) {}

    void HarmonyDisplay::resized()
    {
        auto area = getLocalBounds();
        constexpr int gap = 6;
        const auto h = (area.getHeight() - gap * (numVoices - 1)) / numVoices;

        for (auto& strip : strips)
        {
            strip->setBounds (area.removeFromTop (h));
            area.removeFromTop (gap);
        }
    }
}
