#include "PluginEditor.h"

#include <cmath>

using namespace dooblrgenerate;
using namespace dooblrgenerate::ui;

namespace
{
    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.0f));
        l.setColour (juce::Label::textColourId, Palette::textMuted);
        l.setJustificationType (juce::Justification::centredLeft);
    }

    juce::String formatRate (double hz)
    {
        return juce::String (hz / 1000.0, 1) + " kHz";
    }
}

DooblrGenerateEditor::DooblrGenerateEditor (DooblrGenerateProcessor& p)
    : AudioProcessorEditor (p), generator (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText (JucePlugin_Name, juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Palette::textPrimary);
    subtitle.setText ("v" + juce::String (JucePlugin_VersionString), juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (13.0f));
    subtitle.setColour (juce::Label::textColourId, Palette::textMuted);
    styleCaption (modelCaption, "Model");
    modelCaption.setJustificationType (juce::Justification::centredRight);
    for (auto* c : { &title, &subtitle, &modelCaption })
        addAndMakeVisible (*c);

    refreshModelBox();
    modelBox.onChange = [this]
    {
        const auto models = gen::ModelCatalog::all (true);
        const auto index = modelBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, (int) models.size()))
            generator.setModel (models[(size_t) index].id);
        refresh();
    };
    addAndMakeVisible (modelBox);

    // --- Prompt bar ---------------------------------------------------------------
    styleCaption (promptCaption, "Prompt");
    styleCaption (seedCaption, "Seed");
    styleCaption (lengthCaption, "Length");
    styleCaption (syncCaption, "Sync");
    for (auto* c : { &promptCaption, &seedCaption, &lengthCaption, &syncCaption })
        addAndMakeVisible (*c);

    prompt.setFont (juce::FontOptions (15.0f));
    prompt.setTextToShowWhenEmpty ("bright clap", Palette::textMuted.withAlpha (0.5f));
    prompt.setIndents (8, 8);
    prompt.setText (generator.getPrompt(), false);
    prompt.onTextChange = [this] { generator.setPrompt (prompt.getText()); refreshJob(); };
    prompt.onReturnKey = [this] { if (generateButton.isEnabled()) generateButton.triggerClick(); };
    addAndMakeVisible (prompt);

    seed.setFont (juce::FontOptions (14.0f));
    seed.setInputRestrictions (10, "0123456789");
    seed.setJustification (juce::Justification::centred);
    seed.setText (juce::String (generator.getSeed()), false);
    seed.onTextChange = [this] { generator.setSeed (seed.getText().getLargeIntValue()); };
    addAndMakeVisible (seed);

    length.setSliderStyle (juce::Slider::LinearBar);
    length.setRange (params::minSeconds, generator.getModelMaxSeconds(), 0.05);
    length.setSkewFactorFromMidPoint (2.0);
    length.setTextValueSuffix (" s");
    length.setNumDecimalPlacesToDisplay (2);
    length.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.35f));
    length.setColour (juce::Slider::backgroundColourId, Palette::panelRaised);
    length.setValue (generator.getSeconds(), juce::dontSendNotification);
    length.onValueChange = [this] { generator.setSeconds (length.getValue()); };
    addAndMakeVisible (length);

    for (int i = 0; i < (int) gen::LengthUnit::nUnits; ++i)
        lengthChoice.addItem (gen::unitName ((gen::LengthUnit) i), i + 1);
    lengthChoice.setSelectedId ((int) generator.getLengthUnit() + 1, juce::dontSendNotification);
    lengthChoice.onChange = [this]
    {
        generator.setLengthUnit (gen::lengthUnitFromInt (lengthChoice.getSelectedId() - 1));
        refreshLengthUi();
    };
    addChildComponent (lengthChoice);

    syncButton.setClickingTogglesState (true);
    syncButton.setTooltip ("Length in bars/beats at the host tempo (or the Tempo knob)");
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        generator.getState(), params::id::sync, syncButton);
    syncButton.onClick = [this] { refreshLengthUi(); resized(); };
    addAndMakeVisible (syncButton);

    generateButton.setColour (juce::TextButton::buttonColourId, Palette::accent);
    generateButton.setColour (juce::TextButton::textColourOffId, Palette::background);
    generateButton.onClick = [this]
    {
        if (generator.getService().isBusy() && generator.getService().jobStatus().kind == gen::JobStatus::Kind::generate)
            generator.getService().cancel();
        else
            generator.generate (false);
        refresh();
    };
    variationButton.onClick = [this]
    {
        if (generator.generate (true))
            seed.setText (juce::String (generator.getSeed()), false);
        refresh();
    };
    addAndMakeVisible (generateButton);
    addAndMakeVisible (variationButton);

    progressBar.setColour (juce::ProgressBar::backgroundColourId, Palette::grid);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, Palette::accent);
    progressBar.setPercentageDisplay (false);
    addAndMakeVisible (progressBar);
    styleCaption (progressLabel, {});
    progressLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (progressLabel);

    // --- Waveform and history -------------------------------------------------------
    addAndMakeVisible (waveform);
    playButton.onClick = [this] { generator.requestPreview(); };
    dragButton.addMouseListener (this, false);
    // Drag the button out to drag the WAV; a plain click shows the file in Finder.
    dragButton.onClick = [this]
    {
        if (const auto sample = generator.getCurrentSample())
            sample->file.revealToUser();
    };
    addAndMakeVisible (playButton);
    addAndMakeVisible (dragButton);
    styleCaption (infoLabel, {});
    infoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (infoLabel);

    historyList.onChoose = [this] (const gen::HistoryEntry& e, bool play)
    {
        juce::String error;
        if (generator.loadHistoryEntry (e.id, error))
        {
            generator.setPrompt (e.prompt);
            generator.setSeed (e.seed);
            prompt.setText (e.prompt, false);
            seed.setText (juce::String (e.seed), false);
            if (play)
                generator.requestPreview();
        }
        else
        {
            copyText = gen::GenerationService::makeReport ("load history", e.model, error);
            details.setText (copyText, false);
        }
        refresh();
    };
    addAndMakeVisible (historyList);

    // --- Playback --------------------------------------------------------------------
    using Spec = ControlPanel::Spec;
    playbackPanel = std::make_unique<ControlPanel> (generator.getState(), "PLAYBACK", Palette::accent, std::vector<Spec> {
        { params::id::tune, "Tune" }, { params::id::fine, "Fine" }, { params::id::start, "Start" },
        { params::id::attack, "Attack" }, { params::id::decay, "Decay" }, { params::id::velocity, "Velocity" },
        { params::id::output, "Output" }, { params::id::tempo, "Tempo" } });
    playbackPanel->setShown (params::id::tempo, false);
    addAndMakeVisible (*playbackPanel);

    // --- Model ---------------------------------------------------------------------------
    statusLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, Palette::textPrimary);
    addAndMakeVisible (statusLabel);

    for (auto* ed : { &details, &tokenEditor })
    {
        ed->setFont (juce::FontOptions (11.0f));
        ed->setColour (juce::TextEditor::backgroundColourId, Palette::background);
        ed->setColour (juce::TextEditor::outlineColourId, Palette::divider);
        addChildComponent (*ed);
    }
    details.setMultiLine (true, true);
    details.setReadOnly (true);
    details.setCaretVisible (false);
    details.setScrollbarsShown (true);
    details.setVisible (true);
    tokenEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    tokenEditor.setTextToShowWhenEmpty ("hf_...", Palette::textMuted.withAlpha (0.5f));

    downloadButton.onClick = [this]
    {
        auto& service = generator.getService();
        if (service.isBusy() && service.jobStatus().kind == gen::JobStatus::Kind::download)
            service.cancel();
        else
            service.startDownload (generator.getModel());
        refresh();
    };
    tokenButton.onClick = [this] { showTokenEntry (true); };
    folderButton.onClick = [this] { chooseFolder(); };
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (copyText); };
    tokenSave.onClick = [this]
    {
        if (generator.getService().saveToken (tokenEditor.getText()))
        {
            tokenEditor.clear();
            showTokenEntry (false);
        }
        refresh();
    };
    tokenCancel.onClick = [this] { tokenEditor.clear(); showTokenEntry (false); };
    for (auto* b : { &downloadButton, &tokenButton, &folderButton, &copyButton })
        addAndMakeVisible (*b);
    addChildComponent (tokenSave);
    addChildComponent (tokenCancel);

    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
    setSize (defaultWidth, defaultHeight);

    refresh();
    startTimerHz (20);
}

DooblrGenerateEditor::~DooblrGenerateEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DooblrGenerateEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragButton && ! dragFromButtonStarted && e.getDistanceFromDragStart() > 4)
    {
        dragFromButtonStarted = true;
        waveform.dragOut();
    }
}

void DooblrGenerateEditor::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragButton)
        dragFromButtonStarted = false;
}

bool DooblrGenerateEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details_,
                                                                 juce::StringArray& files, bool& canMoveFiles)
{
    const juce::File file (details_.description.toString());
    if (! juce::File::isAbsolutePath (details_.description.toString()) || ! file.existsAsFile())
        return false;

    files.add (file.getFullPathName());
    canMoveFiles = false;
    return true;
}

void DooblrGenerateEditor::refreshModelBox()
{
    modelBox.clear (juce::dontSendNotification);
    const auto models = gen::ModelCatalog::all (true);
    const auto showTest = gen::ModelCatalog::testBackendEnabled() || generator.getModel() == gen::ModelCatalog::testBackend;

    for (size_t i = 0; i < models.size(); ++i)
        if (! models[i].isTest || showTest)
            modelBox.addItem (models[i].displayName, (int) i + 1);

    for (size_t i = 0; i < models.size(); ++i)
        if (models[i].id == generator.getModel())
            modelBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
}

void DooblrGenerateEditor::showTokenEntry (bool show)
{
    tokenEntryShown = show;
    tokenEditor.setVisible (show);
    tokenSave.setVisible (show);
    tokenCancel.setVisible (show);
    details.setVisible (! show);
    for (auto* b : { &downloadButton, &tokenButton, &folderButton, &copyButton })
        b->setVisible (! show);
    if (show)
        tokenEditor.grabKeyboardFocus();
    resized();
    refresh();
}

void DooblrGenerateEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder with the model files",
                                                   generator.getService().modelFolder (generator.getModel()));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder.isDirectory())
                                  generator.getService().setModelFolder (generator.getModel(), folder);
                              refresh();
                          });
}

//==============================================================================
void DooblrGenerateEditor::refreshLengthUi()
{
    const auto syncOn = generator.isSyncOn();
    const auto modelMax = generator.getModelMaxSeconds();
    length.setRange (params::minSeconds, modelMax, 0.05);
    if (length.getValue() > modelMax)
        length.setValue (modelMax, juce::sendNotificationSync);

    length.setVisible (! syncOn);
    lengthChoice.setVisible (syncOn);
    if (syncOn)
        lengthChoice.setSelectedId ((int) generator.getLengthUnit() + 1, juce::dontSendNotification);

    if (playbackPanel != nullptr)
        playbackPanel->setShown (params::id::tempo, syncOn && ! generator.isUsingHostTempo());

    if (syncOn)
    {
        const auto plan = generator.getBeatPlan();
        juce::String caption = gen::unitName (plan.fitted);
        caption << " · " << juce::String (plan.bpm, 1) << " BPM";
        if (! plan.usingHostTempo)
            caption << " (manual)";
        if (plan.capped)
            caption << " · capped";
        lengthCaption.setText (caption, juce::dontSendNotification);
        lengthChoice.setTooltip (plan.capped
            ? juce::String (gen::unitName (plan.requested)) + " does not fit; using "
              + gen::unitName (plan.fitted) + " (" + juce::String (modelMax, 1) + " s). No time-stretch."
            : "Length in bars/beats. Output is trimmed or padded to the grid (no time-stretch).");
    }
    else
    {
        lengthCaption.setText ("Length", juce::dontSendNotification);
        length.setTooltip ("Length in seconds, up to " + juce::String (modelMax, 1) + " s for this model");
    }
}

void DooblrGenerateEditor::timerCallback()
{
    refreshSample();
    refreshJob();
    refreshModel();
    refreshLengthUi();
}

void DooblrGenerateEditor::refreshSample()
{
    const auto serial = generator.getSampleSerial();
    const auto sample = generator.getCurrentSample();

    if (serial != lastSampleSerial)
    {
        lastSampleSerial = serial;
        waveform.setSample (sample);

        if (sample != nullptr)
        {
            juce::String info;
            if (sample->isTest)
                info << "TEST BACKEND (not AI)  ";
            if (sample->prompt.isNotEmpty())
                info << "\"" << sample->prompt << "\"  seed " << sample->seed << "  ";
            info << juce::String (sample->lengthSeconds(), 2) << " s  " << formatRate (sample->sampleRate) << "  "
                 << (sample->audio.getNumChannels() > 1 ? "stereo" : "mono");
            if (sample->model.isNotEmpty())
                info << "  " << shortModelName (sample->model);
            info << "  " << sample->file.getFileName();
            const auto align = generator.getAlignMessage();
            if (align.isNotEmpty())
                info << "  " << align;
            infoLabel.setText (info, juce::dontSendNotification);
            infoLabel.setColour (juce::Label::textColourId, sample->isTest ? Palette::dynamic : Palette::textMuted);
        }
        else
        {
            infoLabel.setText ({}, juce::dontSendNotification);
        }
    }

    waveform.setStartFraction (generator.getState().getRawParameterValue (params::id::start)->load() / 100.0f);
    historyList.setCurrentId (sample != nullptr ? sample->historyId : juce::String());
    playButton.setEnabled (sample != nullptr);
    dragButton.setEnabled (sample != nullptr && sample->file.existsAsFile());

    if (generator.getHistory().size() != lastHistorySize || serial != lastSampleSerial)
    {
        lastHistorySize = generator.getHistory().size();
        historyList.setEntries (generator.getHistory().entries());
    }
}

void DooblrGenerateEditor::refreshJob()
{
    auto& service = generator.getService();
    const auto job = service.jobStatus();
    const auto running = job.result == gen::JobStatus::Result::running;
    const auto generating = running && job.kind == gen::JobStatus::Kind::generate;
    const auto modelReady = service.modelStatus (generator.getModel()).state == gen::ModelState::ready;

    generateButton.setButtonText (generating ? "Cancel" : "Generate");
    generateButton.setEnabled (generating || (! running && modelReady && generator.getPrompt().trim().isNotEmpty()));
    variationButton.setEnabled (! running && modelReady && generator.getPrompt().trim().isNotEmpty());
    variationButton.setTooltip ("New seed, then generate");

    juce::String text;
    if (job.kind == gen::JobStatus::Kind::generate)
    {
        const auto elapsed = (double) ((job.finishedMs > 0 ? job.finishedMs : juce::Time::currentTimeMillis()) - job.startedMs) / 1000.0;
        switch (job.result)
        {
            case gen::JobStatus::Result::running:
                progress = juce::jmax (0.02, (double) job.fraction);
                text << "Generating: " << job.stage << "  " << juce::String (elapsed, 1) << " s";
                break;
            case gen::JobStatus::Result::succeeded:
                progress = 1.0;
                text << "Generated in " << juce::String (elapsed, 1) << " s";
                break;
            case gen::JobStatus::Result::failed:
                progress = 0.0;
                text << (job.errorCode == "runtime" ? "Python runtime broken: run scripts/setup-runtime.sh (details in MODEL; Copy copies them)"
                                                    : "Generation failed (details in MODEL; Copy copies them)");
                break;
            case gen::JobStatus::Result::cancelled:
                progress = 0.0;
                text << "Cancelled";
                break;
            case gen::JobStatus::Result::none: break;
        }
    }
    else if (job.kind == gen::JobStatus::Kind::download && running)
    {
        progress = juce::jmax (0.02, (double) job.fraction);
        text << "Downloading model  " << job.message;
    }
    else
    {
        progress = 0.0;
    }

    progressBar.setVisible (running || job.result == gen::JobStatus::Result::succeeded);
    progressLabel.setText (text, juce::dontSendNotification);
    progressLabel.setColour (juce::Label::textColourId,
                             job.result == gen::JobStatus::Result::failed ? juce::Colour (0xffe0574f) : Palette::textMuted);
    lastJobSerial = job.serial;
    lastJobResult = job.result;
}

void DooblrGenerateEditor::refreshModel()
{
    auto& service = generator.getService();
    const auto modelId = generator.getModel();
    const auto status = service.modelStatus (modelId);
    const auto job = service.jobStatus();
    const auto* model = gen::ModelCatalog::find (modelId);

    juce::Colour dot = Palette::textMuted;
    switch (status.state)
    {
        case gen::ModelState::ready:          dot = juce::Colour (0xff5fbf8a); break;
        case gen::ModelState::downloading:    dot = Palette::spectrum; break;
        case gen::ModelState::needsAccess:    dot = Palette::dynamic; break;
        case gen::ModelState::notInstalled:   dot = Palette::textMuted; break;
        case gen::ModelState::runtimeMissing:
        case gen::ModelState::error:          dot = juce::Colour (0xffe0574f); break;
    }

    statusLabel.setText (status.text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, dot);

    // The details box shows the latest generation error for this model, else the model's own status.
    juce::String text;
    if (job.result == gen::JobStatus::Result::failed && job.model == modelId && job.kind == gen::JobStatus::Kind::generate)
        text = job.error;
    else if (status.details.isNotEmpty())
        text = status.details;
    else if (model != nullptr)
        text = model->licence;

    if (status.state == gen::ModelState::ready && model != nullptr && text == status.details)
        text = model->licence + "\n" + status.details;

    if (text != copyText)
    {
        copyText = text;
        details.setText (text, false);
    }

    const auto downloading = status.state == gen::ModelState::downloading;
    downloadButton.setButtonText (downloading ? "Cancel" : "Download");
    downloadButton.setEnabled (model != nullptr && ! model->isTest
                               && (downloading || (! service.isBusy() && status.state != gen::ModelState::ready
                                                   && status.state != gen::ModelState::runtimeMissing)));
    tokenButton.setEnabled (model != nullptr && model->gated);
    folderButton.setEnabled (model != nullptr && ! model->isTest && ! downloading);
    copyButton.setEnabled (copyText.isNotEmpty());
}

//==============================================================================
void DooblrGenerateEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour colour) const
{
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (text, area.reduced (12, 6).removeFromTop (16), juce::Justification::centredLeft);
}

void DooblrGenerateEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::panel);
    g.fillRect (headerArea);
    g.setColour (Palette::accent);
    g.fillRect (headerArea.withHeight (3));
    g.setColour (Palette::divider);
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

    paintPanel (g, historyArea, "HISTORY", Palette::textMuted);
    paintPanel (g, modelArea, "MODEL", Palette::textMuted);
}

void DooblrGenerateEditor::resized()
{
    auto area = getLocalBounds();

    // Header
    headerArea = area.removeFromTop (60);
    {
        auto h = headerArea.reduced (32, 10);
        auto titleColumn = h.removeFromLeft (220);
        title.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
        subtitle.setBounds (titleColumn);
        const auto boxWidth = juce::jmin (330, h.getWidth() / 2);
        modelBox.setBounds (h.removeFromRight (boxWidth).withSizeKeepingCentre (boxWidth, 26));
        modelCaption.setBounds (h.removeFromRight (60));
    }

    area.reduce (20, 12);

    // Prompt bar: captions above the fields.
    {
        auto bar = area.removeFromTop (54);
        auto captions = bar.removeFromTop (18);
        bar = bar.withHeight (34);
        const auto buttonWidth = 112;
        auto variationArea = bar.removeFromRight (buttonWidth);
        bar.removeFromRight (8);
        auto generateArea = bar.removeFromRight (buttonWidth);
        bar.removeFromRight (16);
        // Pill is 30 px (see DooblrLookAndFeel::drawToggleButton); rest is the "Sync" label.
        const auto syncTextW = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (13.0f)),
                                                                       syncButton.getButtonText());
        const auto syncWidth = juce::jmax (72, (int) std::ceil (syncTextW) + 30 + 12 + 6);
        auto syncArea = bar.removeFromRight (syncWidth);
        bar.removeFromRight (8);
        auto comboFont = lengthChoice.getLookAndFeel().getComboBoxFont (lengthChoice);
        float longestUnit = 0.0f;
        for (int i = 0; i < lengthChoice.getNumItems(); ++i)
            longestUnit = juce::jmax (longestUnit, juce::GlyphArrangement::getStringWidth (comboFont, lengthChoice.getItemText (i)));
        const auto lengthWidth = juce::jlimit (130, 260, juce::jmax (getWidth() / 7, (int) std::ceil (longestUnit) + 28));
        auto lengthArea = bar.removeFromRight (lengthWidth);
        bar.removeFromRight (10);
        auto seedArea = bar.removeFromRight (juce::jlimit (96, 160, getWidth() / 10));
        bar.removeFromRight (10);

        prompt.setBounds (bar);
        seed.setBounds (seedArea);
        length.setBounds (lengthArea);
        lengthChoice.setBounds (lengthArea);
        syncButton.setBounds (syncArea);
        generateButton.setBounds (generateArea);
        variationButton.setBounds (variationArea);
        promptCaption.setBounds (captions.withX (bar.getX()).withWidth (bar.getWidth()));
        seedCaption.setBounds (captions.withX (seedArea.getX()).withWidth (seedArea.getWidth()));
        lengthCaption.setBounds (captions.withX (lengthArea.getX()).withWidth (lengthArea.getWidth()));
        syncCaption.setBounds (captions.withX (syncArea.getX()).withWidth (syncArea.getWidth()));

        area.removeFromTop (6);
        auto progressRow = area.removeFromTop (18);
        progressBar.setBounds (progressRow.removeFromLeft (juce::jmin (240, progressRow.getWidth() / 4)).withSizeKeepingCentre (juce::jmin (240, progressRow.getWidth() / 4), 4));
        progressRow.removeFromLeft (10);
        progressLabel.setBounds (progressRow);
        area.removeFromTop (8);
    }

    // Bottom row: playback knobs (left) and the model panel (right).
    const auto sideWidth = juce::jlimit (300, 560, getWidth() * 30 / 100);
    const auto bottomHeight = juce::jmax (playbackPanel->getPreferredHeight (area.getWidth() - sideWidth - 12) + 12, 150);
    auto bottom = area.removeFromBottom (bottomHeight);
    area.removeFromBottom (12);

    modelArea = bottom.removeFromRight (sideWidth);
    bottom.removeFromRight (12);
    playbackArea = bottom;
    playbackPanel->setBounds (playbackArea);

    {
        auto m = modelArea.reduced (12, 6);
        m.removeFromTop (18);
        auto statusRow = m.removeFromTop (22);
        statusLabel.setBounds (statusRow);
        m.removeFromTop (4);
        auto buttons = m.removeFromTop (26);

        if (tokenEntryShown)
        {
            tokenCancel.setBounds (buttons.removeFromRight (70));
            buttons.removeFromRight (6);
            tokenSave.setBounds (buttons.removeFromRight (60));
            buttons.removeFromRight (6);
            tokenEditor.setBounds (buttons);
        }
        else
        {
            const auto w = (buttons.getWidth() - 18) / 4;
            for (auto* b : { &downloadButton, &tokenButton, &folderButton })
            {
                b->setBounds (buttons.removeFromLeft (w));
                buttons.removeFromLeft (6);
            }
            copyButton.setBounds (buttons);
        }

        m.removeFromTop (6);
        details.setBounds (m);
    }

    // Middle: waveform and history.
    historyArea = area.removeFromRight (sideWidth);
    historyList.setBounds (historyArea.reduced (8, 6).withTrimmedTop (20));
    area.removeFromRight (12);
    waveArea = area;

    auto under = area.removeFromBottom (30);
    area.removeFromBottom (8);
    waveform.setBounds (area);
    playButton.setBounds (under.removeFromLeft (72).withSizeKeepingCentre (72, 26));
    under.removeFromLeft (8);
    dragButton.setBounds (under.removeFromLeft (96).withSizeKeepingCentre (96, 26));
    under.removeFromLeft (12);
    infoLabel.setBounds (under);
}
