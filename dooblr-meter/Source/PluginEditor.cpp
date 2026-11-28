#include "PluginEditor.h"

#include "DSP/Profiles.h"
#include "DSP/Targets.h"

namespace dooblrmeter
{
    namespace id = params::id;
    namespace Palette = ui::Palette;

    namespace
    {
        constexpr int margin = 12, gap = 8, headerHeight = 58;

        void styleLabel (juce::Label& label, const juce::String& text, float size, juce::Colour colour,
                         juce::Justification just, bool bold = false)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
            label.setColour (juce::Label::textColourId, colour);
            label.setJustificationType (just);
            label.setInterceptsMouseClicks (false, false);
            label.setMinimumHorizontalScale (0.7f);
        }

        int choiceIndex (juce::AudioProcessorValueTreeState& state, const char* pid)
        {
            return juce::roundToInt (state.getRawParameterValue (pid)->load());
        }

        void setChoice (juce::AudioProcessorValueTreeState& state, const char* pid, int index)
        {
            if (auto* p = state.getParameter (pid))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
                p->endChangeGesture();
            }
        }
    }

    DooblrMeterEditor::DooblrMeterEditor (DooblrMeterProcessor& p)
        : juce::AudioProcessorEditor (p), owner (p), state (p.getState())
    {
        setLookAndFeel (&lookAndFeel);
        surface.setComponentID ("surface");
        surface.painter = [this] (juce::Graphics& g) { paintSurface (g); };
        addAndMakeVisible (surface);

        styleLabel (titleLabel, JucePlugin_Name, 22.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
        styleLabel (subtitleLabel, "v" JucePlugin_VersionString "  loudness & translation meter", 11.0f, Palette::textMuted, juce::Justification::centredLeft);
        subtitleLabel.setComponentID ("version");
        styleLabel (statusLabel, {}, 11.0f, Palette::dynamic, juce::Justification::centredLeft, true);
        statusLabel.setComponentID ("status");
        for (auto* l : { &titleLabel, &subtitleLabel, &statusLabel })
            surface.addAndMakeVisible (l);

        for (auto* tab : { &mainTab, &advancedTab })
        {
            tab->setClickingTogglesState (false);
            tab->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
            surface.addAndMakeVisible (tab);
        }
        mainTab.setComponentID ("pageMain");
        advancedTab.setComponentID ("pageAdvanced");
        mainTab.onClick = [this] { showPage (0); };
        advancedTab.onClick = [this] { showPage (1); };

        styleLabel (targetCaption, "TARGET", 10.0f, Palette::textMuted, juce::Justification::bottomLeft, true);
        styleLabel (curveCaption, "CURVE", 10.0f, Palette::textMuted, juce::Justification::bottomLeft, true);
        styleLabel (listenCaption, "LISTEN", 10.0f, Palette::textMuted, juce::Justification::bottomLeft, true);
        styleLabel (modeCaption, "MODE", 10.0f, Palette::textMuted, juce::Justification::bottomLeft, true);
        for (auto* l : { &targetCaption, &curveCaption, &listenCaption, &modeCaption })
            surface.addAndMakeVisible (l);

        setupCombo (targetBox, id::target, dsp::targets::names(),
                    "Delivery target: the loudness and true-peak ceiling the findings and predicted gain are measured against");
        setupCombo (curveBox, id::curve, dsp::curves::names(), "Reference curve: the tonal balance the spectrum and findings compare with");
        setupCombo (listenBox, id::listen, dsp::profiles::listenNames(),
                    "Listen: hear the mix through a playback-system model (level matched). Always Off when a session loads");

        for (auto* b : { &liveButton, &songButton })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.45f));
            surface.addAndMakeVisible (b);
        }
        liveButton.setComponentID ("modeLive");
        songButton.setComponentID ("modeSong");
        liveButton.setTooltip ("Live: measure everything since Reset, in playing order");
        songButton.setTooltip ("Song: measure by song position while the transport plays; replaying a section replaces it");
        liveButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        songButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        liveButton.onClick = [this] { setChoice (state, id::mode, 0); };
        songButton.onClick = [this] { setChoice (state, id::mode, 1); };

        resetButton.setComponentID ("reset");
        resetButton.setTooltip ("Reset: clear the measurements (both Live and Song)");
        resetButton.onClick = [this] { owner.resetMeasurements(); selectedFinding = -1; };
        surface.addAndMakeVisible (resetButton);

        // Advanced settings.
        styleLabel (settingsTitle, "SETTINGS", 11.0f, Palette::textMuted, juce::Justification::centredLeft, true);
        surface.addChildComponent (settingsTitle);
        styleLabel (customLufsCaption, "Custom loudness", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        styleLabel (customTpCaption, "Custom ceiling", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        styleLabel (historyCaption, "History span", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        styleLabel (slopeCaption, "Spectrum slope", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        styleLabel (averagingCaption, "Spectrum averaging", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        styleLabel (sensitivityCaption, "Findings sensitivity", 11.5f, Palette::textMuted, juce::Justification::centredLeft);
        for (auto* l : { &customLufsCaption, &customTpCaption, &historyCaption, &slopeCaption, &averagingCaption, &sensitivityCaption })
            surface.addChildComponent (l);

        setupSlider (customLufsSlider, id::customLufs, "Custom target loudness (used when Target is Custom)");
        setupSlider (customTpSlider, id::customTp, "Custom true-peak ceiling (used when Target is Custom)");
        setupCombo (historyBox, id::historySpan, params::historySpanNames(), "Time span of the loudness history");
        setupCombo (slopeBox, id::slope, params::slopeNames(), "Display tilt of the spectrum (0 dB/oct: pink noise reads flat)");
        setupCombo (averagingBox, id::averaging, params::averagingNames(), "Averaging time of the live spectrum (Infinite: since Reset)");
        setupCombo (sensitivityBox, id::sensitivity, params::sensitivityNames(), "How strict the findings are");
        setupToggle (matchToggle, id::listenMatch, "Level match the Listen audition to the dry mix");
        setupToggle (monoToggle, id::monoAudition, "Mono audition: hear the mix summed to mono. Always Off when a session loads");
        setupToggle (freezeToggle, id::freeze, "Freeze the live spectrum and the vectorscope (measuring continues)");

        translation.onTileClicked = [this] (int profileIndex) { toggleListen (profileIndex); };
        findings.onSelect = [this] (int index) { selectedFinding = index; timerCallback(); };

        for (juce::Component* c : { (juce::Component*) &readout, (juce::Component*) &history, (juce::Component*) &spectrum,
                                    (juce::Component*) &stereo, (juce::Component*) &translation, (juce::Component*) &findings })
            surface.addAndMakeVisible (c);
        for (juce::Component* c : { (juce::Component*) &delivery, (juce::Component*) &metrics, (juce::Component*) &histogram })
            surface.addChildComponent (c);

        readout.setComponentID ("readout");
        history.setComponentID ("history");
        spectrum.setComponentID ("spectrum");
        stereo.setComponentID ("stereo");
        translation.setComponentID ("translation");
        findings.setComponentID ("findings");
        delivery.setComponentID ("delivery");
        metrics.setComponentID ("metrics");
        histogram.setComponentID ("histogram");

        showPage (0);
        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);
        timerCallback();
        startTimerHz (30);
    }

    DooblrMeterEditor::~DooblrMeterEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void DooblrMeterEditor::setupCombo (juce::ComboBox& box, const char* pid, const juce::StringArray& items, const juce::String& tip)
    {
        box.setComponentID (pid);
        box.addItemList (items, 1);
        box.setTooltip (tip);
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, pid, box));
        surface.addChildComponent (box);
    }

    void DooblrMeterEditor::setupToggle (juce::ToggleButton& button, const char* pid, const juce::String& tip)
    {
        button.setComponentID (pid);
        button.setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid, button));
        surface.addChildComponent (button);
    }

    void DooblrMeterEditor::setupSlider (juce::Slider& slider, const char* pid, const juce::String& tip)
    {
        slider.setComponentID (pid);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 20);
        slider.setColour (juce::Slider::trackColourId, Palette::accent);
        slider.setColour (juce::Slider::textBoxOutlineColourId, Palette::divider);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelRaised);
        slider.setTooltip (tip);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, slider));
        if (auto* param = state.getParameter (pid))
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        surface.addChildComponent (slider);
    }

    void DooblrMeterEditor::toggleListen (int profileIndex)
    {
        const auto current = choiceIndex (state, id::listen) - 1;
        setChoice (state, id::listen, current == profileIndex ? 0 : profileIndex + 1);
    }

    void DooblrMeterEditor::showPage (int newPage)
    {
        page = juce::jlimit (0, 1, newPage);
        const bool main = page == 0;
        mainTab.setToggleState (main, juce::dontSendNotification);
        advancedTab.setToggleState (! main, juce::dontSendNotification);

        // Header controls are on both pages.
        for (juce::Component* c : { (juce::Component*) &targetBox, (juce::Component*) &curveBox, (juce::Component*) &listenBox })
            c->setVisible (true);

        for (juce::Component* c : { (juce::Component*) &readout, (juce::Component*) &history, (juce::Component*) &spectrum,
                                    (juce::Component*) &stereo, (juce::Component*) &translation, (juce::Component*) &findings })
            c->setVisible (main);
        for (juce::Component* c : { (juce::Component*) &delivery, (juce::Component*) &metrics, (juce::Component*) &histogram,
                                    (juce::Component*) &settingsTitle, (juce::Component*) &customLufsCaption, (juce::Component*) &customTpCaption,
                                    (juce::Component*) &historyCaption, (juce::Component*) &slopeCaption, (juce::Component*) &averagingCaption,
                                    (juce::Component*) &sensitivityCaption, (juce::Component*) &customLufsSlider, (juce::Component*) &customTpSlider,
                                    (juce::Component*) &historyBox, (juce::Component*) &slopeBox, (juce::Component*) &averagingBox,
                                    (juce::Component*) &sensitivityBox, (juce::Component*) &matchToggle, (juce::Component*) &monoToggle,
                                    (juce::Component*) &freezeToggle })
            c->setVisible (! main);
        resized();
        repaint();
    }

    ui::ViewContext DooblrMeterEditor::makeContext() const
    {
        const auto settings = owner.readSettings();
        ui::ViewContext c;
        c.targetIndex = settings.target;
        c.target = dsp::targets::get (settings.target, settings.customLufs, settings.customCeiling);
        c.slopeDbPerOct = params::slopeDbPerOctave (choiceIndex (state, id::slope));
        c.listenProfile = choiceIndex (state, id::listen) - 1;
        c.selectedFinding = selectedFinding;
        return c;
    }

    void DooblrMeterEditor::timerCallback()
    {
        auto snap = owner.getAnalyzer().getSnapshot();
        if (snap != nullptr && selectedFinding >= (int) snap->findings.size())
            selectedFinding = -1;
        const auto ctx = makeContext();

        const bool song = choiceIndex (state, id::mode) == 1;
        liveButton.setToggleState (! song, juce::dontSendNotification);
        songButton.setToggleState (song, juce::dontSendNotification);

        const bool custom = ctx.targetIndex == dsp::targets::customIndex;
        ui::setDimmed (customLufsSlider, ! custom);
        ui::setDimmed (customTpSlider, ! custom);
        ui::setDimmed (customLufsCaption, ! custom);
        ui::setDimmed (customTpCaption, ! custom);
        ui::setDimmed (matchToggle, ctx.listenProfile < 0);

        juce::String status;
        if (ctx.listenProfile >= 0)
            status = "LISTENING: " + juce::String (dsp::profiles::get (ctx.listenProfile).name)
                   + (statusInline && state.getRawParameterValue (id::listenMatch)->load() > 0.5f ? " (level matched)" : "");
        if (state.getRawParameterValue (id::monoAudition)->load() > 0.5f)
            status = status.isEmpty() ? juce::String ("LISTENING: mono") : status + " + mono";
        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setVisible (status.isNotEmpty());
        subtitleLabel.setVisible (statusInline || status.isEmpty());

        lastSnapshot = snap;

        if (page == 0)
        {
            for (ui::SnapshotView* v : { (ui::SnapshotView*) &readout, (ui::SnapshotView*) &history, (ui::SnapshotView*) &spectrum,
                                         (ui::SnapshotView*) &stereo, (ui::SnapshotView*) &translation })
                v->update (snap, ctx);
            findings.update (snap, ctx);
        }
        else
        {
            for (ui::SnapshotView* v : { (ui::SnapshotView*) &delivery, (ui::SnapshotView*) &metrics, (ui::SnapshotView*) &histogram })
                v->update (snap, ctx);
        }
    }

    void DooblrMeterEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
    }

    void DooblrMeterEditor::paintSurface (juce::Graphics& g)
    {
        auto header = surface.getLocalBounds().removeFromTop (headerHeight);
        g.setColour (Palette::panel);
        g.fillRect (header);
        g.setColour (Palette::accent);
        g.fillRect (juce::Rectangle<int> (margin + 2, 12, 3, headerHeight - 24));
        g.setColour (Palette::divider);
        g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) surface.getWidth());
        if (page == 1)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (settingsPanel.toFloat(), 6.0f);
        }
    }

    void DooblrMeterEditor::resized()
    {
        // Base size 1080x660; larger windows scale the whole surface (1x .. 2x).
        surfaceScale = juce::jlimit (1.0f, 2.0f, std::min ((float) getWidth() / 1080.0f, (float) getHeight() / 660.0f));
        surface.setTransform ({});
        surface.setBounds (0, 0, juce::roundToInt ((float) getWidth() / surfaceScale), juce::roundToInt ((float) getHeight() / surfaceScale));
        surface.setTransform (juce::AffineTransform::scale (surfaceScale));

        auto area = surface.getLocalBounds();
        auto header = area.removeFromTop (headerHeight).reduced (margin, 0);

        // Left: brand and page tabs.
        auto brand = header.removeFromLeft (std::max (150, header.getWidth() / 7)).withTrimmedLeft (12);
        titleLabel.setBounds (brand.withHeight (28).withY (6));
        subtitleLabel.setBounds (brand.withY (35).withHeight (16));
        header.removeFromLeft (gap);
        auto tabs = header.removeFromLeft (156).withSizeKeepingCentre (156, 26);
        mainTab.setBounds (tabs.removeFromLeft (72));
        tabs.removeFromLeft (8);
        advancedTab.setBounds (tabs.removeFromLeft (76));
        header.removeFromLeft (gap * 2);

        // Right: Reset, Mode, Listen, Curve, Target (captions above).
        const auto controlRow = [&] (juce::Rectangle<int> r) { return r.withY (24).withHeight (26); };
        const auto captionRow = [&] (juce::Rectangle<int> r) { return r.withY (6).withHeight (17); };
        auto right = header;
        resetButton.setBounds (controlRow (right.removeFromRight (62)));
        right.removeFromRight (gap);
        auto mode = right.removeFromRight (110);
        modeCaption.setBounds (captionRow (mode));
        auto modeRow = controlRow (mode);
        liveButton.setBounds (modeRow.removeFromLeft (55));
        songButton.setBounds (modeRow);
        right.removeFromRight (gap);
        auto listen = right.removeFromRight (120);
        listenCaption.setBounds (captionRow (listen));
        listenBox.setBounds (controlRow (listen));
        right.removeFromRight (gap);
        auto curve = right.removeFromRight (116);
        curveCaption.setBounds (captionRow (curve));
        curveBox.setBounds (controlRow (curve));
        right.removeFromRight (gap);
        auto target = right.removeFromRight (std::min (160, std::max (130, right.getWidth())));
        targetCaption.setBounds (captionRow (target));
        targetBox.setBounds (controlRow (target));
        right.removeFromRight (gap);
        statusInline = right.getWidth() >= 140;
        if (statusInline)
            statusLabel.setBounds (right.withY (6).withHeight (44));
        else
            statusLabel.setBounds (subtitleLabel.getBounds());

        // The bottom edge leaves room for the resize corner.
        auto body = area.reduced (margin).withTrimmedTop (gap - margin + 4).withTrimmedBottom (8);
        const auto W = surface.getWidth();

        if (page == 0)
        {
            auto findingsArea = body.removeFromBottom (juce::jlimit (110, 330, body.getHeight() * 22 / 100));
            body.removeFromBottom (gap);
            findings.setBounds (findingsArea);

            auto left = body.removeFromLeft (juce::jlimit (196, 400, W * 19 / 100));
            body.removeFromLeft (gap);
            auto rightCol = body.removeFromRight (juce::jlimit (210, 560, W * 22 / 100));
            body.removeFromRight (gap);

            const auto readoutH = std::max (236, left.getHeight() * 54 / 100);
            readout.setBounds (left.removeFromTop (readoutH));
            left.removeFromTop (gap);
            stereo.setBounds (left);

            translation.setBounds (rightCol);

            auto hist = body.removeFromTop (body.getHeight() * 44 / 100);
            history.setBounds (hist);
            body.removeFromTop (gap);
            spectrum.setBounds (body);
        }
        else
        {
            auto settings = body.removeFromLeft (juce::jlimit (280, 520, W * 28 / 100));
            body.removeFromLeft (gap);
            auto metricsArea = body.removeFromRight (juce::jlimit (300, 640, W * 32 / 100));
            body.removeFromRight (gap);
            metrics.setBounds (metricsArea);
            delivery.setBounds (body.removeFromTop (body.getHeight() * 58 / 100));
            body.removeFromTop (gap);
            histogram.setBounds (body);

            settingsPanel = settings;
            auto s = settings.reduced (10, 0).withTrimmedBottom (8);
            settingsTitle.setBounds (s.removeFromTop (22));
            const auto rowH = juce::jlimit (28, 44, s.getHeight() / 10);
            const auto captionW = juce::jlimit (110, 200, s.getWidth() * 42 / 100);
            const auto row = [&] (juce::Label& caption, juce::Component& control)
            {
                auto r = s.removeFromTop (rowH);
                caption.setBounds (r.removeFromLeft (captionW));
                control.setBounds (r.withSizeKeepingCentre (r.getWidth(), std::min (26, r.getHeight() - 4)));
            };
            row (customLufsCaption, customLufsSlider);
            row (customTpCaption, customTpSlider);
            row (historyCaption, historyBox);
            row (slopeCaption, slopeBox);
            row (averagingCaption, averagingBox);
            row (sensitivityCaption, sensitivityBox);
            s.removeFromTop (6);
            for (auto* t : { &matchToggle, &monoToggle, &freezeToggle })
                t->setBounds (s.removeFromTop (rowH).withHeight (std::min (26, rowH)));
        }
    }
}
