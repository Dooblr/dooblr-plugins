#include "VerdictPanel.h"

#include "Theme.h"

namespace dooblrsub::ui
{
    void VerdictPanel::setState (dsp::Verdict v, const dsp::ReasonList& r,
                                 float hz, int latencySamples, double sr)
    {
        verdict = v;
        reasons = r;
        trackedHz = hz;
        latency = latencySamples;
        sampleRate = sr > 1.0 ? sr : 48000.0;
        repaint();
    }

    void VerdictPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto colour = verdict == dsp::Verdict::works ? juce::Colour (0xff5fbf8a)
                          : verdict == dsp::Verdict::check ? Palette::dynamic
                                                           : juce::Colour (0xffe0574f);

        auto inner = bounds.reduced (10.0f, 8.0f);
        auto badge = inner.removeFromTop (juce::jlimit (28.0f, 40.0f, inner.getHeight() * 0.28f));
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (juce::jlimit (13.0f, 18.0f, badge.getHeight() * 0.55f), juce::Font::bold));
        g.drawFittedText (dsp::verdictLabel (verdict), badge.toNearestInt(), juce::Justification::centred, 1);

        inner.removeFromTop (8.0f);
        g.setFont (juce::FontOptions (12.0f));
        if (reasons.count == 0)
        {
            g.setColour (Palette::textMuted);
            g.drawFittedText ("Low end looks solid on this target.", inner.toNearestInt(),
                              juce::Justification::topLeft, 3);
        }
        else
        {
            g.setColour (Palette::textPrimary);
            juce::String list;
            for (int i = 0; i < reasons.count; ++i)
            {
                if (i) list += "\n";
                list += juce::String::fromUTF8 ("\xe2\x80\xa2 ") + juce::String (reasons.text[i]);
            }
            g.drawFittedText (list, inner.toNearestInt(), juce::Justification::topLeft, 8);
        }

        auto foot = getLocalBounds().removeFromBottom (18).reduced (10, 0);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (10.0f));
        juce::String info;
        if (trackedHz > 1.0f)
            info += juce::String (trackedHz, 1) + " Hz   ";
        info += juce::String (latency) + " smp  "
             + juce::String (1000.0 * (double) latency / sampleRate, 1) + " ms";
        g.drawFittedText (info, foot, juce::Justification::centredRight, 1);
    }
}
