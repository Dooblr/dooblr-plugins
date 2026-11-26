#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"

#include <cmath>
#include <initializer_list>

namespace dooblrchroma
{
    namespace
    {
        template <typename Ctrl>
        void addTo (juce::Component& parent, Ctrl& c)
        {
            parent.addAndMakeVisible (c);
        }
    }

    DooblrChromaEditor::DooblrChromaEditor (DooblrChromaProcessor& p)
        : juce::AudioProcessorEditor (p), owner (p)
    {
        setLookAndFeel (&lnf);
        setResizable (true, true);
        setResizeLimits (940, 600, 2400, 1400);
        setSize (1080, 660);

        titleLabel.setText ("dooblr-chroma", juce::dontSendNotification);
        titleLabel.setColour (juce::Label::textColourId, ui::Palette::textPrimary);
        titleLabel.setFont (juce::FontOptions (18.0f));
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setInterceptsMouseClicks (false, false);

        versionLabel.setComponentID ("version");
        versionLabel.setText ("v" JucePlugin_VersionString, juce::dontSendNotification);
        versionLabel.setJustificationType (juce::Justification::centredRight);
        versionLabel.setColour (juce::Label::textColourId, ui::Palette::textMuted);

        undo.setComponentID ("undo");
        redo.setComponentID ("redo");
        presetBox.setComponentID ("preset");
        savePreset.setComponentID ("savePreset");
        loadPreset.setComponentID ("loadPreset");
        output.setComponentID (params::id::output);
        outputCaption.setText ("Output", juce::dontSendNotification);
        outputCaption.setJustificationType (juce::Justification::centred);
        output.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        output.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        layerA.setComponentID ("editLayer1");
        layerB.setComponentID ("editLayer2");
        layerA.setClickingTogglesState (true);
        layerB.setClickingTogglesState (true);
        layerA.setRadioGroupId (1001);
        layerB.setRadioGroupId (1001);
        layerA.setToggleState (true, juce::dontSendNotification);

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (versionLabel);
        addAndMakeVisible (undo);
        addAndMakeVisible (redo);
        addAndMakeVisible (presetBox);
        addAndMakeVisible (savePreset);
        addAndMakeVisible (loadPreset);
        addAndMakeVisible (output);
        addAndMakeVisible (outputCaption);
        addAndMakeVisible (layerA);
        addAndMakeVisible (layerB);

        panels = { &mallet, &noise, &mixer, &perform, &resA, &resB, &coupling, &fx };
        for (auto* panel : panels)
            addAndMakeVisible (panel);

        auto bind = [this] (auto& c) { c.attach (owner.getState()); };

        for (auto* k : { &stiff, &malletNoise, &color })
        {
            mallet.addAndMakeVisible (*k);
            bind (*k);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &noiseFilter, &noiseFreq, &noiseQ, &envMode,
                                                                 &envA, &envH, &envD, &envS, &envR, &lfoRate, &lfoDepth })
        {
            noise.addAndMakeVisible (*c);
        }
        bind (noiseFilter); bind (noiseFreq); bind (noiseQ); bind (envMode);
        bind (envA); bind (envH); bind (envD); bind (envS); bind (envR); bind (lfoRate); bind (lfoDepth);

        for (auto* k : { &mixMallet, &mixNoise, &mixDirect })
        {
            mixer.addAndMakeVisible (*k);
            bind (*k);
        }

        for (auto* c : std::initializer_list<juce::Component*> { &unison, &detune, &vibRate, &vibDepth, &glide,
                                                                 &macro1, &macro2, &macro3, &macro4,
                                                                 &layerMode, &splitNote, &layer1Level, &layer2Level, &layer2Trans })
            perform.addAndMakeVisible (*c);
        bind (unison); bind (detune); bind (vibRate); bind (vibDepth); bind (glide);
        bind (macro1); bind (macro2); bind (macro3); bind (macro4);
        bind (layerMode); bind (splitNote); bind (layer1Level); bind (layer2Level); bind (layer2Trans);

        for (auto* k : { &eqLow, &eqMid, &eqHigh, &eqMidFreq, &compTh, &compRatio, &compAtk, &compRel, &compMix,
                         &distDrive, &distMix, &chRate, &chDepth, &chMix, &dlTime, &dlFb, &dlMix, &rvSize, &rvDamp, &rvMix })
        {
            fx.addAndMakeVisible (*k);
            bind (*k);
        }

        resA.addAndMakeVisible (viewA);
        resB.addAndMakeVisible (viewB);
        for (auto* c : std::initializer_list<juce::Component*> {
                 &aOn, &aType, &aDens, &aPitch, &aCents, &aKey, &aLevel, &aRate, &aVel,
                 &aDecay, &aDecKey, &aDecVel, &aRel, &aMat, &aTone, &aLow, &aRad, &aHit,
                 &aHitVel, &aHitKey, &aHitRnd, &aP1, &aP2, &aP3, &aP4 })
            resA.addAndMakeVisible (*c);
        for (auto* c : std::initializer_list<juce::Component*> {
                 &bOn, &bType, &bDens, &bPitch, &bCents, &bKey, &bLevel, &bRate, &bVel,
                 &bDecay, &bDecKey, &bDecVel, &bRel, &bMat, &bTone, &bLow, &bRad, &bHit,
                 &bHitVel, &bHitKey, &bHitRnd, &bP1, &bP2, &bP3, &bP4 })
            resB.addAndMakeVisible (*c);

        coupling.addAndMakeVisible (couple);
        coupling.addAndMakeVisible (balance);

        outputAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            owner.getState(), params::id::output, output);

        bindLayer();

        undo.onClick = [this] { owner.getUndoHistory().undo(); };
        redo.onClick = [this] { owner.getUndoHistory().redo(); };
        layerA.onClick = [this] { setEditLayer (0); };
        layerB.onClick = [this] { setEditLayer (1); };

        rebuildPresetMenu();
        presetBox.onChange = [this]
        {
            const int idx = presetBox.getSelectedId() - 1;
            const auto& fac = presets::factory();
            if (idx >= 0 && idx < (int) fac.size())
            {
                owner.setCurrentProgram (idx);
                rebuildPresetMenu();
            }
        };
        savePreset.onClick = [this]
        {
            const auto dir = presets::userDirectory();
            dir.createDirectory();
            auto name = owner.getPresetName();
            if (name.isEmpty() || name == "Init")
                name = "User";
            juce::File file = dir.getChildFile (name + presets::fileExtension);
            int n = 2;
            while (file.existsAsFile())
                file = dir.getChildFile (name + "-" + juce::String (n++) + presets::fileExtension);
            owner.saveUserPreset (file);
            rebuildPresetMenu();
        };
        loadPreset.onClick = [this]
        {
            const auto files = presets::userPresets();
            if (files.isEmpty())
                return;
            owner.loadUserPreset (files.getFirst());
            rebuildPresetMenu();
        };

        owner.presetChanged.addChangeListener (this);
        startTimerHz (24);
    }

    DooblrChromaEditor::~DooblrChromaEditor()
    {
        owner.presetChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void DooblrChromaEditor::setEditLayer (int layer)
    {
        editLayer = juce::jlimit (0, 1, layer);
        layerA.setToggleState (editLayer == 0, juce::dontSendNotification);
        layerB.setToggleState (editLayer == 1, juce::dontSendNotification);
        bindLayer();
        resized();
    }

    void DooblrChromaEditor::bindLayer()
    {
        const juce::String prefix = editLayer == 0 ? "l1" : "l2";
        auto rebindK = [this, &prefix] (ui::Knob& k, const juce::String& suffix)
        {
            k.detach();
            k.paramId = prefix + suffix;
            k.setComponentID (k.paramId);
            k.slider.setComponentID (k.paramId);
            k.attach (owner.getState());
        };
        auto rebindC = [this, &prefix] (ui::Choice& c, const juce::String& suffix)
        {
            c.detach();
            c.paramId = prefix + suffix;
            c.setComponentID (c.paramId);
            c.box.setComponentID (c.paramId);
            c.attach (owner.getState());
        };
        auto rebindT = [this, &prefix] (ui::Toggle& t, const juce::String& suffix)
        {
            t.detach();
            t.paramId = prefix + suffix;
            t.setComponentID (t.paramId);
            t.button.setComponentID (t.paramId);
            t.attach (owner.getState());
        };

        rebindT (aOn, "_a_on"); rebindC (aType, "_a_type"); rebindC (aDens, "_a_density");
        rebindK (aPitch, "_a_pitch"); rebindK (aCents, "_a_cents"); rebindK (aKey, "_a_key");
        rebindK (aLevel, "_a_pLevel"); rebindK (aRate, "_a_pRate"); rebindK (aVel, "_a_pVel");
        rebindK (aDecay, "_a_decay"); rebindK (aDecKey, "_a_decayKey"); rebindK (aDecVel, "_a_decayVel");
        rebindK (aRel, "_a_release"); rebindK (aMat, "_a_material"); rebindK (aTone, "_a_tone");
        rebindK (aLow, "_a_lowCut"); rebindK (aRad, "_a_radius"); rebindK (aHit, "_a_hit");
        rebindK (aHitVel, "_a_hitVel"); rebindK (aHitKey, "_a_hitKey"); rebindK (aHitRnd, "_a_hitRnd");
        rebindK (aP1, "_a_part1"); rebindK (aP2, "_a_part2"); rebindK (aP3, "_a_part3"); rebindK (aP4, "_a_part4");

        rebindT (bOn, "_b_on"); rebindC (bType, "_b_type"); rebindC (bDens, "_b_density");
        rebindK (bPitch, "_b_pitch"); rebindK (bCents, "_b_cents"); rebindK (bKey, "_b_key");
        rebindK (bLevel, "_b_pLevel"); rebindK (bRate, "_b_pRate"); rebindK (bVel, "_b_pVel");
        rebindK (bDecay, "_b_decay"); rebindK (bDecKey, "_b_decayKey"); rebindK (bDecVel, "_b_decayVel");
        rebindK (bRel, "_b_release"); rebindK (bMat, "_b_material"); rebindK (bTone, "_b_tone");
        rebindK (bLow, "_b_lowCut"); rebindK (bRad, "_b_radius"); rebindK (bHit, "_b_hit");
        rebindK (bHitVel, "_b_hitVel"); rebindK (bHitKey, "_b_hitKey"); rebindK (bHitRnd, "_b_hitRnd");
        rebindK (bP1, "_b_part1"); rebindK (bP2, "_b_part2"); rebindK (bP3, "_b_part3"); rebindK (bP4, "_b_part4");

        rebindT (couple, "_couple");
        rebindK (balance, "_balance");
        refreshDimmed();
    }

    void DooblrChromaEditor::rebuildPresetMenu()
    {
        presetBox.clear (juce::dontSendNotification);
        int i = 1;
        for (const auto& f : presets::factory())
            presetBox.addItem (f.name, i++);
        const auto name = owner.getPresetName();
        bool found = false;
        for (int id = 1; id < i; ++id)
            if (presetBox.getItemText (id - 1) == name)
            {
                presetBox.setSelectedId (id, juce::dontSendNotification);
                found = true;
                break;
            }
        if (! found)
        {
            presetBox.addItem (name, i);
            presetBox.setSelectedId (i, juce::dontSendNotification);
        }
    }

    void DooblrChromaEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        rebuildPresetMenu();
    }

    int DooblrChromaEditor::getHeaderHeight() const { return 60; }

    juce::Rectangle<int> DooblrChromaEditor::getContentArea() const
    {
        return getLocalBounds().withTrimmedTop (getHeaderHeight()).reduced (8, 6);
    }

    void DooblrChromaEditor::layoutKnobs (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int columns)
    {
        if (items.empty() || columns <= 0)
            return;
        const int rows = (int) std::ceil ((double) items.size() / (double) columns);
        const int w = area.getWidth() / columns;
        const int h = area.getHeight() / std::max (1, rows);
        for (size_t i = 0; i < items.size(); ++i)
        {
            const int col = (int) i % columns;
            const int row = (int) i / columns;
            items[i]->setBounds (area.getX() + col * w, area.getY() + row * h, w, h);
        }
    }

    void DooblrChromaEditor::refreshDimmed()
    {
        auto typeOf = [this] (const ui::Choice& c) -> dsp::ResType
        {
            if (auto* p = owner.getState().getParameter (c.paramId))
                return (dsp::ResType) juce::roundToInt (p->convertFrom0to1 (p->getValue()));
            return dsp::ResType::string;
        };
        const auto ta = typeOf (aType), tb = typeOf (bType);
        ui::setDimmed (aMat, dsp::isTube (ta));
        ui::setDimmed (aRad, ! dsp::isTube (ta));
        ui::setDimmed (bMat, dsp::isTube (tb));
        ui::setDimmed (bRad, ! dsp::isTube (tb));
        const bool manA = ta == dsp::ResType::manual;
        const bool manB = tb == dsp::ResType::manual;
        for (auto* k : { &aP1, &aP2, &aP3, &aP4 })
            ui::setDimmed (*k, ! manA);
        for (auto* k : { &bP1, &bP2, &bP3, &bP4 })
            ui::setDimmed (*k, ! manB);
        const bool split = [&] {
            if (auto* p = owner.getState().getParameter (params::id::layerMode))
                return juce::roundToInt (p->convertFrom0to1 (p->getValue())) == 1;
            return false;
        }();
        ui::setDimmed (splitNote, ! split);
    }

    void DooblrChromaEditor::timerCallback()
    {
        auto hitOf = [this] (const ui::Knob& k) -> float
        {
            if (auto* p = owner.getState().getParameter (k.paramId))
                return p->convertFrom0to1 (p->getValue());
            return 0.22f;
        };
        auto typeOf = [this] (const ui::Choice& c) -> dsp::ResType
        {
            if (auto* p = owner.getState().getParameter (c.paramId))
                return (dsp::ResType) juce::roundToInt (p->convertFrom0to1 (p->getValue()));
            return dsp::ResType::string;
        };
        const bool coupled = [&] {
            if (auto* p = owner.getState().getParameter (couple.paramId))
                return p->convertFrom0to1 (p->getValue()) >= 0.5f;
            return false;
        }();
        viewA.setState (typeOf (aType), hitOf (aHit), coupled);
        viewB.setState (typeOf (bType), hitOf (bHit), coupled);
        refreshDimmed();
        undo.setEnabled (owner.getUndoHistory().canUndo());
        redo.setEnabled (owner.getUndoHistory().canRedo());
    }

    void DooblrChromaEditor::paint (juce::Graphics& g)
    {
        g.fillAll (ui::Palette::background);
        auto header = getLocalBounds().removeFromTop (getHeaderHeight());
        g.setColour (ui::Palette::panel);
        g.fillRect (header);
        g.setColour (ui::Palette::divider);
        g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
    }

    void DooblrChromaEditor::resized()
    {
        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop (getHeaderHeight()).reduced (8, 6);

        titleLabel.setBounds (header.removeFromLeft (168));
        layerA.setBounds (header.removeFromLeft (36).reduced (2, 8));
        layerB.setBounds (header.removeFromLeft (36).reduced (2, 8));
        undo.setBounds (header.removeFromLeft (52).reduced (2, 8));
        redo.setBounds (header.removeFromLeft (52).reduced (2, 8));
        versionLabel.setBounds (header.removeFromRight (56));
        auto out = header.removeFromRight (70);
        outputCaption.setBounds (out.removeFromTop (12));
        output.setBounds (out);
        savePreset.setBounds (header.removeFromRight (48).reduced (2, 8));
        loadPreset.setBounds (header.removeFromRight (48).reduced (2, 8));
        presetBox.setBounds (header.reduced (6, 8));

        auto content = getContentArea();
        const int topH = juce::jmax (148, content.getHeight() * 26 / 100);
        const int fxH  = juce::jmax (150, content.getHeight() * 24 / 100);
        auto top = content.removeFromTop (topH);
        auto fxRow = content.removeFromBottom (fxH);
        content.removeFromBottom (6);
        top.removeFromBottom (6);

        const int malletW = juce::jmax (150, top.getWidth() * 16 / 100);
        const int mixerW  = juce::jmax (140, top.getWidth() * 14 / 100);
        const int noiseW  = juce::jmax (260, top.getWidth() * 36 / 100);
        mallet.setBounds (top.removeFromLeft (malletW).reduced (3, 0));
        mixer.setBounds (top.removeFromRight (mixerW).reduced (3, 0));
        perform.setBounds (top.removeFromRight (juce::jmax (200, top.getWidth() - noiseW)).reduced (3, 0));
        noise.setBounds (top.reduced (3, 0));

        layoutKnobs (mallet.contentBounds(), { &stiff, &malletNoise, &color }, 3);
        layoutKnobs (mixer.contentBounds(), { &mixMallet, &mixNoise, &mixDirect }, 3);
        layoutKnobs (noise.contentBounds(),
                     { &noiseFilter, &noiseFreq, &noiseQ, &envMode, &envA, &envH, &envD, &envS, &envR, &lfoRate, &lfoDepth },
                     4);
        layoutKnobs (perform.contentBounds(),
                     { &unison, &detune, &vibRate, &vibDepth, &glide,
                       &macro1, &macro2, &macro3, &macro4,
                       &layerMode, &splitNote, &layer1Level, &layer2Level, &layer2Trans },
                     5);

        const int coupleW = juce::jmax (120, content.getWidth() * 12 / 100);
        auto mid = content;
        coupling.setBounds (mid.removeFromLeft (coupleW).reduced (3, 0));
        // split remaining mid equally
        const int resW = mid.getWidth() / 2;
        resA.setBounds (mid.removeFromLeft (resW).reduced (3, 0));
        resB.setBounds (mid.reduced (3, 0));

        {
            auto c = coupling.contentBounds();
            couple.setBounds (c.removeFromTop (28).reduced (4, 2));
            balance.setBounds (c.reduced (8, 4));
        }

        auto layoutRes = [this] (ui::Panel& panel, ui::ResonatorView& view, ui::Toggle& on, ui::Choice& type,
                                 ui::Choice& dens, const std::vector<juce::Component*>& knobs)
        {
            auto c = panel.contentBounds();
            const int topRow = juce::jmax (52, c.getHeight() * 22 / 100);
            auto head = c.removeFromTop (topRow);
            on.setBounds (head.removeFromLeft (48).reduced (2, 4));
            type.setBounds (head.removeFromLeft (juce::jmax (90, head.getWidth() / 3)).reduced (2, 0));
            dens.setBounds (head.removeFromLeft (juce::jmax (70, head.getWidth() / 2)).reduced (2, 0));
            // leftover of head plus a dedicated strip for the view
            const int viewH = juce::jmax (56, c.getHeight() * 28 / 100);
            view.setBounds (c.removeFromTop (viewH).reduced (2, 2));
            layoutKnobs (c, knobs, 6);
        };

        layoutRes (resA, viewA, aOn, aType, aDens,
                   { &aPitch, &aCents, &aKey, &aLevel, &aRate, &aVel,
                     &aDecay, &aDecKey, &aDecVel, &aRel, &aMat, &aTone,
                     &aLow, &aRad, &aHit, &aHitVel, &aHitKey, &aHitRnd,
                     &aP1, &aP2, &aP3, &aP4 });
        layoutRes (resB, viewB, bOn, bType, bDens,
                   { &bPitch, &bCents, &bKey, &bLevel, &bRate, &bVel,
                     &bDecay, &bDecKey, &bDecVel, &bRel, &bMat, &bTone,
                     &bLow, &bRad, &bHit, &bHitVel, &bHitKey, &bHitRnd,
                     &bP1, &bP2, &bP3, &bP4 });

        fx.setBounds (fxRow.reduced (3, 0));
        layoutKnobs (fx.contentBounds(),
                     { &eqLow, &eqMid, &eqHigh, &eqMidFreq, &compTh, &compRatio, &compAtk, &compRel, &compMix,
                       &distDrive, &distMix, &chRate, &chDepth, &chMix, &dlTime, &dlFb, &dlMix, &rvSize, &rvDamp, &rvMix },
                     10);
    }
}
