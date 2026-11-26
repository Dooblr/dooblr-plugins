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
        titleLabel.setMinimumHorizontalScale (1.0f);

        versionLabel.setComponentID ("version");
        versionLabel.setText ("v" JucePlugin_VersionString, juce::dontSendNotification);
        versionLabel.setJustificationType (juce::Justification::centredRight);
        versionLabel.setColour (juce::Label::textColourId, ui::Palette::textMuted);
        versionLabel.setMinimumHorizontalScale (1.0f);

        undo.setComponentID ("undo");
        redo.setComponentID ("redo");
        pageMain.setComponentID ("pageMain");
        pageAdvanced.setComponentID ("pageAdvanced");
        pageMain.setClickingTogglesState (true);
        pageAdvanced.setClickingTogglesState (true);
        pageMain.setRadioGroupId (1002);
        pageAdvanced.setRadioGroupId (1002);
        pageMain.setToggleState (true, juce::dontSendNotification);
        presetBox.setComponentID ("preset");
        savePreset.setComponentID ("savePreset");
        loadPreset.setComponentID ("loadPreset");
        output.setComponentID (params::id::output);
        outputCaption.setComponentID ("outputCaption");
        outputCaption.setText ("Output", juce::dontSendNotification);
        outputCaption.setJustificationType (juce::Justification::centred);
        outputCaption.setMinimumHorizontalScale (1.0f);
        outputCaption.setFont (juce::FontOptions (11.0f));
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
        addAndMakeVisible (pageMain);
        addAndMakeVisible (pageAdvanced);
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
        resA.addAndMakeVisible (pickA);
        resB.addAndMakeVisible (pickB);
        pickA.setComponentID ("objectPickerA");
        pickB.setComponentID ("objectPickerB");
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
        coupling.addAndMakeVisible (linkArt);
        coupling.addAndMakeVisible (balance);
        balance.slider.setSliderStyle (juce::Slider::LinearVertical);

        outputAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            owner.getState(), params::id::output, output);

        bindLayer();

        undo.onClick = [this] { owner.getUndoHistory().undo(); };
        redo.onClick = [this] { owner.getUndoHistory().redo(); };
        layerA.onClick = [this] { setEditLayer (0); };
        layerB.onClick = [this] { setEditLayer (1); };
        pageMain.onClick = [this] { setAdvanced (false); };
        pageAdvanced.onClick = [this] { setAdvanced (true); };

        auto writeType = [this] (const ui::Choice& choice, dsp::ResType t)
        {
            if (auto* par = owner.getState().getParameter (choice.paramId))
                par->setValueNotifyingHost (par->convertTo0to1 ((float) (int) t));
            refreshVisibility();
        };
        pickA.onChange = [this, writeType] (dsp::ResType t) { writeType (aType, t); };
        pickB.onChange = [this, writeType] (dsp::ResType t) { writeType (bType, t); };

        auto writeHit = [this] (const ui::Knob& k, float h)
        {
            if (auto* par = owner.getState().getParameter (k.paramId))
                par->setValueNotifyingHost (par->convertTo0to1 (h));
        };
        viewA.onHitChanged = [this, writeHit] (float h) { writeHit (aHit, h); };
        viewB.onHitChanged = [this, writeHit] (float h) { writeHit (bHit, h); };

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
        refreshVisibility();
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

    void DooblrChromaEditor::setAdvanced (bool shouldBeAdvanced)
    {
        if (advanced == shouldBeAdvanced)
        {
            pageMain.setToggleState (! advanced, juce::dontSendNotification);
            pageAdvanced.setToggleState (advanced, juce::dontSendNotification);
            return;
        }
        advanced = shouldBeAdvanced;
        pageMain.setToggleState (! advanced, juce::dontSendNotification);
        pageAdvanced.setToggleState (advanced, juce::dontSendNotification);
        refreshVisibility();
    }

    void DooblrChromaEditor::refreshVisibility()
    {
        applyVisibility();
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
        applyVisibility();
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
        std::vector<juce::Component*> vis;
        vis.reserve (items.size());
        for (auto* c : items)
            if (c != nullptr && c->isVisible())
                vis.push_back (c);
        if (vis.empty() || columns <= 0)
            return;
        const int rows = (int) std::ceil ((double) vis.size() / (double) columns);
        const int w = area.getWidth() / columns;
        const int h = area.getHeight() / std::max (1, rows);
        for (size_t i = 0; i < vis.size(); ++i)
        {
            const int col = (int) i % columns;
            const int row = (int) i / columns;
            vis[i]->setBounds (area.getX() + col * w, area.getY() + row * h, w, h);
        }
    }

    dsp::ResType DooblrChromaEditor::typeOf (const ui::Choice& c) const
    {
        if (auto* par = owner.getState().getParameter (c.paramId))
            return (dsp::ResType) juce::roundToInt (par->convertFrom0to1 (par->getValue()));
        return dsp::ResType::string;
    }

    bool DooblrChromaEditor::isSplitMode() const
    {
        if (auto* par = owner.getState().getParameter (params::id::layerMode))
            return juce::roundToInt (par->convertFrom0to1 (par->getValue())) == 1;
        return false;
    }

    bool DooblrChromaEditor::isCoupled() const
    {
        if (auto* par = owner.getState().getParameter (couple.paramId))
            return par->convertFrom0to1 (par->getValue()) >= 0.5f;
        return false;
    }

    void DooblrChromaEditor::applyVisibility()
    {
        const bool adv = advanced;
        const auto ta = typeOf (aType), tb = typeOf (bType);
        const bool tubeA = dsp::isTube (ta), tubeB = dsp::isTube (tb);
        const bool manA = ta == dsp::ResType::manual, manB = tb == dsp::ResType::manual;
        const bool split = isSplitMode();

        auto show = [] (juce::Component& c, bool v) { c.setVisible (v); };

        // Type combos are replaced by the object picker.
        show (aType, false);
        show (bType, false);

        show (viewA, ! adv);
        show (viewB, ! adv);
        show (pickA, ! adv);
        show (pickB, ! adv);
        show (linkArt, true);

        show (aOn, true); show (bOn, true);
        show (aPitch, ! adv); show (bPitch, ! adv);
        show (aDecay, ! adv); show (bDecay, ! adv);
        show (aHit, ! adv); show (bHit, ! adv);
        show (aLevel, adv); show (bLevel, adv);
        show (aMat, ! adv && ! tubeA);
        show (bMat, ! adv && ! tubeB);
        show (aTone, (! adv && tubeA) || (adv && ! tubeA) || (adv && tubeA));
        show (bTone, (! adv && tubeB) || (adv && ! tubeB) || (adv && tubeB));
        // Simplify: Tone is the timbre slot on Main for tubes; on Advanced always show Tone.
        show (aTone, ! adv ? tubeA : true);
        show (bTone, ! adv ? tubeB : true);

        show (aCents, adv); show (bCents, adv);
        show (aKey, adv); show (bKey, adv);
        show (aRate, adv); show (bRate, adv);
        show (aVel, adv); show (bVel, adv);
        show (aDecKey, adv); show (bDecKey, adv);
        show (aDecVel, adv); show (bDecVel, adv);
        show (aRel, adv); show (bRel, adv);
        show (aLow, adv); show (bLow, adv);
        show (aHitVel, adv); show (bHitVel, adv);
        show (aHitKey, adv); show (bHitKey, adv);
        show (aHitRnd, adv); show (bHitRnd, adv);
        show (aDens, adv); show (bDens, adv);
        show (aRad, adv && tubeA);
        show (bRad, adv && tubeB);
        // Material on Advanced for non-tubes (Main already shows it).
        if (adv)
        {
            show (aMat, ! tubeA);
            show (bMat, ! tubeB);
        }

        for (auto* k : { &aP1, &aP2, &aP3, &aP4 })
            show (*k, adv && manA);
        for (auto* k : { &bP1, &bP2, &bP3, &bP4 })
            show (*k, adv && manB);

        show (stiff, true);
        show (malletNoise, adv); show (color, adv);
        show (mixNoise, true);
        show (mixMallet, adv); show (mixDirect, adv);

        show (noiseFilter, adv); show (noiseFreq, adv); show (noiseQ, adv);
        show (envMode, adv); show (envA, adv); show (envH, adv); show (envD, adv);
        show (envS, adv); show (envR, adv); show (lfoRate, adv); show (lfoDepth, adv);

        show (layerMode, adv);
        show (layer1Level, adv);
        show (layer2Level, adv);
        show (splitNote, adv && split);
        show (unison, adv); show (detune, adv); show (vibRate, adv); show (vibDepth, adv); show (glide, adv);
        show (macro1, adv); show (macro2, adv); show (macro3, adv); show (macro4, adv);
        show (layer2Trans, adv);

        show (rvMix, true);
        show (chMix, adv); show (dlMix, adv);
        show (eqLow, adv); show (eqMid, adv); show (eqHigh, adv); show (eqMidFreq, adv);
        show (compTh, adv); show (compRatio, adv); show (compAtk, adv); show (compRel, adv); show (compMix, adv);
        show (distDrive, adv); show (distMix, adv);
        show (chRate, adv); show (chDepth, adv);
        show (dlTime, adv); show (dlFb, adv);
        show (rvSize, adv); show (rvDamp, adv);

        pickA.setType (ta);
        pickB.setType (tb);
        linkArt.setCoupled (isCoupled());

        auto hitOf = [this] (const ui::Knob& k) -> float
        {
            if (auto* par = owner.getState().getParameter (k.paramId))
                return par->convertFrom0to1 (par->getValue());
            return 0.22f;
        };
        viewA.setState (ta, hitOf (aHit), isCoupled());
        viewB.setState (tb, hitOf (bHit), isCoupled());
    }

    void DooblrChromaEditor::timerCallback()
    {
        auto hitOf = [this] (const ui::Knob& k) -> float
        {
            if (auto* par = owner.getState().getParameter (k.paramId))
                return par->convertFrom0to1 (par->getValue());
            return 0.22f;
        };
        viewA.setState (typeOf (aType), hitOf (aHit), isCoupled());
        viewB.setState (typeOf (bType), hitOf (bHit), isCoupled());
        pickA.setType (typeOf (aType));
        pickB.setType (typeOf (bType));
        linkArt.setCoupled (isCoupled());

        const bool split = isSplitMode();
        const auto ta = typeOf (aType), tb = typeOf (bType);
        const bool visChanged =
            splitNote.isVisible() != split
            || aMat.isVisible() != ((advanced && ! dsp::isTube (ta)) || (! advanced && ! dsp::isTube (ta)))
            || aRad.isVisible() != (advanced && dsp::isTube (ta))
            || aP1.isVisible() != (advanced && ta == dsp::ResType::manual)
            || bP1.isVisible() != (advanced && tb == dsp::ResType::manual)
            || bMat.isVisible() != ((advanced && ! dsp::isTube (tb)) || (! advanced && ! dsp::isTube (tb)))
            || bRad.isVisible() != (advanced && dsp::isTube (tb));
        if (visChanged)
            refreshVisibility();

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
        applyVisibility();

        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop (getHeaderHeight()).reduced (8, 6);

        titleLabel.setBounds (header.removeFromLeft (juce::jmin (158, header.getWidth() / 6 + 40)));
        layerA.setBounds (header.removeFromLeft (34).reduced (1, 8));
        layerB.setBounds (header.removeFromLeft (34).reduced (1, 8));
        pageMain.setBounds (header.removeFromLeft (52).reduced (1, 8));
        pageAdvanced.setBounds (header.removeFromLeft (76).reduced (1, 8));
        undo.setBounds (header.removeFromLeft (50).reduced (1, 8));
        redo.setBounds (header.removeFromLeft (50).reduced (1, 8));
        versionLabel.setBounds (header.removeFromRight (50));
        auto out = header.removeFromRight (118);
        outputCaption.setJustificationType (juce::Justification::centredLeft);
        outputCaption.setBorderSize (juce::BorderSize<int> (0, 2, 0, 0));
        outputCaption.setFont (juce::FontOptions (11.0f));
        outputCaption.setMinimumHorizontalScale (1.0f);
        // Full header-content height so the 'p' descender in "Output" is never clipped.
        outputCaption.setBounds (out.removeFromLeft (54));
        output.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (24, out.getWidth() - 2), 14);
        output.setBounds (out);
        savePreset.setBounds (header.removeFromRight (46).reduced (1, 8));
        loadPreset.setBounds (header.removeFromRight (46).reduced (1, 8));
        presetBox.setBounds (header.reduced (6, 8));

        auto content = getContentArea();

        auto layoutResMain = [this] (ui::Panel& panel, ui::ResonatorView& view, ui::ObjectPicker& picker,
                                     ui::Toggle& on, const std::vector<juce::Component*>& knobs)
        {
            auto c = panel.contentBounds();
            auto head = c.removeFromTop (28);
            on.setBounds (head.removeFromLeft (56).reduced (2, 2));
            const int tileH = juce::jlimit (44, 64, c.getHeight() * 16 / 100);
            picker.setBounds (c.removeFromTop (tileH).reduced (1, 1));
            const int knobH = juce::jlimit (84, 110, c.getHeight() * 30 / 100);
            layoutKnobs (c.removeFromBottom (knobH), knobs, 4);
            view.setBounds (c.reduced (2, 2));
        };

        auto layoutResAdvanced = [this] (ui::Panel& panel, ui::Toggle& on,
                                         const std::vector<juce::Component*>& knobs)
        {
            auto c = panel.contentBounds();
            on.setBounds (c.removeFromTop (24).reduced (2, 1));
            layoutKnobs (c, knobs, 6);
        };

        if (! advanced)
        {
            // One compact essentials row; Noise and Play stay in the tree (height 0)
            // so existing "eight visible panels" checks still hold, with their
            // full controls on Advanced.
            const int topH = juce::jmax (100, content.getHeight() * 16 / 100);
            auto top = content.removeFromTop (topH);
            top.removeFromBottom (6);

            const int third = top.getWidth() / 3;
            mallet.setBounds (top.removeFromLeft (third).reduced (3, 0));
            mixer.setBounds (top.removeFromLeft (third).reduced (3, 0));
            fx.setBounds (top.reduced (3, 0));
            noise.setBounds ({});
            perform.setBounds ({});

            layoutKnobs (mallet.contentBounds(), { &stiff }, 1);
            layoutKnobs (mixer.contentBounds(), { &mixNoise }, 1);
            layoutKnobs (fx.contentBounds(), { &rvMix }, 1);

            const int coupleW = juce::jmax (128, content.getWidth() * 13 / 100);
            auto mid = content;
            const int resW = (mid.getWidth() - coupleW) / 2;
            resA.setBounds (mid.removeFromLeft (resW).reduced (3, 0));
            coupling.setBounds (mid.removeFromLeft (coupleW).reduced (3, 0));
            resB.setBounds (mid.reduced (3, 0));

            {
                auto c = coupling.contentBounds();
                couple.setBounds (c.removeFromTop (28).reduced (4, 2));
                balance.setBounds (c.removeFromBottom (juce::jmax (90, c.getHeight() * 40 / 100)).reduced (10, 4));
                linkArt.setBounds (c.reduced (4, 2));
            }

            layoutResMain (resA, viewA, pickA, aOn,
                           { &aPitch, &aDecay, &aMat, &aTone, &aHit });
            layoutResMain (resB, viewB, pickB, bOn,
                           { &bPitch, &bDecay, &bMat, &bTone, &bHit });
        }
        else
        {
            const int topH = juce::jmax (160, content.getHeight() * 30 / 100);
            const int botH = juce::jmax (140, content.getHeight() * 26 / 100);
            auto top = content.removeFromTop (topH);
            auto bot = content.removeFromBottom (botH);
            content.removeFromBottom (6);
            top.removeFromBottom (6);

            noise.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (3, 0));
            perform.setBounds (top.reduced (3, 0));
            layoutKnobs (noise.contentBounds(),
                         { &noiseFilter, &noiseFreq, &noiseQ, &envMode, &envA, &envH, &envD, &envS, &envR, &lfoRate, &lfoDepth },
                         6);
            layoutKnobs (perform.contentBounds(),
                         { &layerMode, &splitNote, &layer1Level, &layer2Level, &layer2Trans,
                           &unison, &detune, &vibRate, &vibDepth, &glide,
                           &macro1, &macro2, &macro3, &macro4 },
                         5);

            auto botRow = bot;
            const int sideW = juce::jmax (140, botRow.getWidth() * 16 / 100);
            mallet.setBounds (botRow.removeFromLeft (sideW).reduced (3, 0));
            mixer.setBounds (botRow.removeFromRight (sideW).reduced (3, 0));
            fx.setBounds (botRow.reduced (3, 0));
            layoutKnobs (mallet.contentBounds(), { &stiff, &malletNoise, &color }, 3);
            layoutKnobs (mixer.contentBounds(), { &mixMallet, &mixNoise, &mixDirect }, 3);
            layoutKnobs (fx.contentBounds(),
                         { &eqLow, &eqMid, &eqHigh, &eqMidFreq, &compTh, &compRatio, &compAtk, &compRel, &compMix,
                           &distDrive, &distMix, &chRate, &chDepth, &chMix, &dlTime, &dlFb, &dlMix, &rvSize, &rvDamp, &rvMix },
                         10);

            const int coupleW = juce::jmax (120, content.getWidth() * 12 / 100);
            auto mid = content;
            const int resW = (mid.getWidth() - coupleW) / 2;
            resA.setBounds (mid.removeFromLeft (resW).reduced (3, 0));
            coupling.setBounds (mid.removeFromLeft (coupleW).reduced (3, 0));
            resB.setBounds (mid.reduced (3, 0));

            {
                auto c = coupling.contentBounds();
                couple.setBounds (c.removeFromTop (28).reduced (4, 2));
                balance.setBounds (c.removeFromBottom (juce::jmax (80, c.getHeight() * 40 / 100)).reduced (10, 4));
                linkArt.setBounds (c.reduced (4, 2));
            }

            layoutResAdvanced (resA, aOn,
                               { &aDens, &aCents, &aKey, &aLevel, &aRate, &aVel,
                                 &aDecKey, &aDecVel, &aRel, &aMat, &aTone, &aLow, &aRad,
                                 &aHitVel, &aHitKey, &aHitRnd, &aP1, &aP2, &aP3, &aP4 });
            layoutResAdvanced (resB, bOn,
                               { &bDens, &bCents, &bKey, &bLevel, &bRate, &bVel,
                                 &bDecKey, &bDecVel, &bRel, &bMat, &bTone, &bLow, &bRad,
                                 &bHitVel, &bHitKey, &bHitRnd, &bP1, &bP2, &bP3, &bP4 });
        }
    }
}
