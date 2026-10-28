#include "KeyFollowStrip.h"

#include "../Parameters.h"
#include "Theme.h"

namespace dooblrvocals::ui
{
    namespace
    {
        juce::String keyText (int k)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            if (k < 0 || k >= 24)
                return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
            return juce::String (names[k % 12]) + (k < 12 ? " major" : " minor");
        }
    }

    KeyFollowStrip::KeyFollowStrip (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        setComponentID ("keyFollowStrip");

        followButton.setComponentID ("key_follow");
        followButton.setTooltip ("Key Follow: detect the song's key (from the sidechain, or the vocal) and use it for "
                                 "Keyed correction and the harmony scale. Key / Scale stay as the fallback; automation "
                                 "is never overwritten.");
        followAttachment = std::make_unique<ButtonAttachment> (state, "key_follow", followButton);
        addAndMakeVisible (followButton);

        lockButton.setComponentID ("key_lock");
        lockButton.setTooltip ("Lock: keep the key that is being applied now (analysis continues for the readout)");
        lockAttachment = std::make_unique<ButtonAttachment> (state, "key_lock", lockButton);
        addAndMakeVisible (lockButton);

        sensSlider.setComponentID ("key_sens");
        sensSlider.setTooltip ("Key Follow sensitivity: low = steady (needs more evidence to change key), "
                               "high = responsive");
        sensSlider.setColour (juce::Slider::trackColourId, Palette::accent);
        sensSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
        sensSlider.setColour (juce::Slider::thumbColourId, Palette::textPrimary);
        sensAttachment = std::make_unique<SliderAttachment> (state, "key_sens", sensSlider);
        addAndMakeVisible (sensSlider);

        sourceBox.setComponentID ("key_source");
        sourceBox.setTooltip ("Listen to: Auto (the sidechain when it has signal, else the vocal), Sidechain or Vocal");
        sourceBox.addItemList (params::keySourceChoices(), 1);
        sourceAttachment = std::make_unique<ComboAttachment> (state, "key_source", sourceBox);
        addAndMakeVisible (sourceBox);
    }

    KeyFollowStrip::~KeyFollowStrip() = default;

    bool KeyFollowStrip::isOn() const
    {
        const auto* v = state.getRawParameterValue ("key_follow");
        return v != nullptr && v->load() > 0.5f;
    }

    int KeyFollowStrip::sourceChoice() const
    {
        const auto* v = state.getRawParameterValue ("key_source");
        return v != nullptr ? juce::roundToInt (v->load()) : 0;
    }

    void KeyFollowStrip::refresh (const KeyFollowView& v)
    {
        const auto changed = v.detectedKey != view.detectedKey || v.candidateKey != view.candidateKey
                          || v.appliedKey != view.appliedKey || std::abs (v.confidence - view.confidence) > 0.005f
                          || v.activeSource != view.activeSource || v.sidechainConnected != view.sidechainConnected
                          || v.sidechainSignal != view.sidechainSignal;
        view = v;
        const auto on = isOn();
        lockButton.setEnabled (on);
        if (changed)
            repaint();
        else
            repaint (statusArea);
    }

    juce::String KeyFollowStrip::getKeyText() const
    {
        const auto locked = isOn() && view.appliedKey >= 0 && view.appliedKey != view.detectedKey;
        return keyText (locked ? view.appliedKey : view.detectedKey);
    }

    juce::String KeyFollowStrip::getStatusText() const
    {
        const auto source = sourceChoice();
        if (source == 1 && ! view.sidechainConnected)
            return "No sidechain: enable it in the host";
        if (view.activeSource == 0)
            return view.detectedKey >= 0 ? "Holding (no signal)" : "Waiting for audio";
        const juce::String from = view.activeSource == 1 ? "sidechain" : "vocal";
        if (! isOn())
            return "Off: detecting from " + from;
        if (view.detectedKey < 0)
            return "Listening to " + from;
        return "Following " + from;
    }

    void KeyFollowStrip::resized()
    {
        auto r = getLocalBounds();
        followButton.setBounds (r.removeFromLeft (112).reduced (0, 1));
        r.removeFromLeft (6);
        readoutArea = r.removeFromLeft (98);
        barArea = r.removeFromLeft (56).withSizeKeepingCentre (56, 4);
        r.removeFromLeft (14);
        lockButton.setBounds (r.removeFromLeft (72).reduced (0, 1));
        r.removeFromLeft (8);
        sensCaptionArea = r.removeFromLeft (34);
        sensSlider.setBounds (r.removeFromLeft (92).reduced (0, 4));
        r.removeFromLeft (10);
        sourceBox.setBounds (r.removeFromLeft (100).withSizeKeepingCentre (100, 22));
        r.removeFromLeft (10);
        statusArea = r;
    }

    void KeyFollowStrip::paint (juce::Graphics& g)
    {
        const auto on = isOn();
        const auto following = on && view.appliedKey >= 0;
        const auto locked = on && view.appliedKey >= 0 && view.appliedKey != view.detectedKey;

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        // Detected key: accent while it drives the correction, muted otherwise.
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (following ? Palette::accent : view.detectedKey >= 0 ? Palette::textPrimary : Palette::textMuted);
        g.drawFittedText (getKeyText() + (locked ? juce::String (" *") : juce::String()), readoutArea,
                          juce::Justification::centredLeft, 1);

        // Confidence bar.
        g.setColour (Palette::divider);
        g.fillRoundedRectangle (barArea.toFloat(), 2.0f);
        const auto conf = juce::jlimit (0.0f, 1.0f, view.detectedKey >= 0 ? view.confidence : 0.0f);
        g.setColour ((following ? Palette::accent : Palette::textMuted).withAlpha (0.9f));
        g.fillRoundedRectangle (barArea.toFloat().withWidth ((float) barArea.getWidth() * conf), 2.0f);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (Palette::textMuted);
        g.drawText ("SENS", sensCaptionArea, juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Palette::textMuted);
        g.drawFittedText (getStatusText(), statusArea, juce::Justification::centredRight, 1);
    }
}
