#include "VerdictPanel.h"

#include "Theme.h"

namespace dooblrsub::ui
{
    VerdictPanel::VerdictPanel()
    {
        setComponentID ("verdict");

        for (int i = 0; i < dsp::maxReasons; ++i)
        {
            auto& row = rows[(size_t) i];
            row.label.setFont (juce::FontOptions (12.0f));
            row.label.setColour (juce::Label::textColourId, Palette::textPrimary);
            row.label.setJustificationType (juce::Justification::centredLeft);
            row.label.setInterceptsMouseClicks (false, false);
            row.fix.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
            row.fix.onClick = [this, i]
            {
                if (onFix && rows[(size_t) i].id != dsp::CheckId::none)
                    onFix (rows[(size_t) i].id);
            };
            reasonList.addAndMakeVisible (row.label);
            reasonList.addAndMakeVisible (row.fix);
            row.label.setVisible (false);
            row.fix.setVisible (false);
        }

        viewport.setViewedComponent (&reasonList, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        fixAll.setComponentID ("fixAll");
        undoFix.setComponentID ("undoFix");
        fixAll.setTooltip ("Apply a conservative fix for every failing or warning check");
        undoFix.setTooltip ("Restore parameters from before the last Fix");
        fixAll.onClick = [this] { if (onFixAll) onFixAll(); };
        undoFix.onClick = [this] { if (onUndo) onUndo(); };
        addAndMakeVisible (fixAll);
        addAndMakeVisible (undoFix);
        fixAll.setEnabled (false);
        undoFix.setEnabled (false);
    }

    void VerdictPanel::setFixHandler (std::function<void (dsp::CheckId)> handler) { onFix = std::move (handler); }
    void VerdictPanel::setFixAllHandler (std::function<void()> handler) { onFixAll = std::move (handler); }
    void VerdictPanel::setUndoFixHandler (std::function<void()> handler) { onUndo = std::move (handler); }

    void VerdictPanel::setUndoEnabled (bool canUndo)
    {
        undoEnabled = canUndo;
        undoFix.setEnabled (canUndo);
    }

    void VerdictPanel::setState (dsp::Verdict v, const dsp::ReasonList& r,
                                 float hz, int latencySamples, double sr)
    {
        verdict = v;
        reasons = r;
        trackedHz = hz;
        latency = latencySamples;
        sampleRate = sr > 1.0 ? sr : 48000.0;
        refreshRows();
        resized();
        repaint();
    }

    void VerdictPanel::refreshRows()
    {
        for (int i = 0; i < dsp::maxReasons; ++i)
        {
            auto& row = rows[(size_t) i];
            const auto show = i < reasons.count;
            row.label.setVisible (show);
            row.fix.setVisible (show);
            if (! show)
            {
                row.id = dsp::CheckId::none;
                continue;
            }

            row.id = reasons.id[i];
            row.label.setText (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + juce::String (reasons.text[i]),
                               juce::dontSendNotification);
            const auto cid = dsp::checkFixComponentId (row.id);
            row.fix.setComponentID (cid);
            row.fix.setTooltip ("Fix " + juce::String (dsp::checkShortName (row.id)));
            // Visible rows are failing or warning; the button is live.
            row.fix.setEnabled (row.id != dsp::CheckId::none);
        }

        fixAll.setEnabled (reasons.count > 0);
        undoFix.setEnabled (undoEnabled);
    }

    void VerdictPanel::resized()
    {
        auto bounds = getLocalBounds().reduced (10, 8);
        const auto badgeH = juce::jlimit (24, 36, bounds.getHeight() / 5);
        bounds.removeFromTop (badgeH);
        bounds.removeFromTop (6);

        auto foot = bounds.removeFromBottom (16);
        juce::ignoreUnused (foot);
        bounds.removeFromBottom (4);
        auto actions = bounds.removeFromBottom (juce::jlimit (22, 26, bounds.getHeight() > 80 ? 24 : 22));
        const auto gap = 6;
        const auto half = (actions.getWidth() - gap) / 2;
        fixAll.setBounds (actions.removeFromLeft (half));
        actions.removeFromLeft (gap);
        undoFix.setBounds (actions);

        bounds.removeFromBottom (6);
        viewport.setBounds (bounds);

        const auto rowH = 20;
        const auto contentH = juce::jmax (bounds.getHeight(), reasons.count * rowH);
        reasonList.setSize (juce::jmax (1, bounds.getWidth() - viewport.getScrollBarThickness()), contentH);

        auto list = reasonList.getLocalBounds();
        for (int i = 0; i < dsp::maxReasons; ++i)
        {
            auto& row = rows[(size_t) i];
            if (! row.fix.isVisible())
            {
                row.label.setBounds ({});
                row.fix.setBounds ({});
                continue;
            }
            auto line = list.removeFromTop (rowH);
            row.fix.setBounds (line.removeFromRight (44).withSizeKeepingCentre (44, 18));
            line.removeFromRight (4);
            row.label.setBounds (line);
        }
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
        auto badge = inner.removeFromTop (juce::jlimit (24.0f, 36.0f, inner.getHeight() * 0.20f));
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (juce::jlimit (13.0f, 18.0f, badge.getHeight() * 0.55f), juce::Font::bold));
        g.drawFittedText (dsp::verdictLabel (verdict), badge.toNearestInt(), juce::Justification::centred, 1);

        if (reasons.count == 0)
        {
            inner.removeFromTop (8.0f);
            inner.removeFromBottom (48.0f);
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Low end looks solid on this target.", inner.toNearestInt(),
                              juce::Justification::topLeft, 3);
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
