#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Generation/BeatSync.h"
#include "Generation/SourceAudio.h"

using namespace dooblrgenerate;

DooblrGenerateProcessor::DooblrGenerateProcessor() : DooblrGenerateProcessor (gen::Paths::standard()) {}

DooblrGenerateProcessor::DooblrGenerateProcessor (gen::Paths p)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      paths (std::move (p)),
      state (*this, nullptr, "DooblrGenerate", params::createLayout()),
      history (paths),
      service (paths, history, [this] (const gen::HistoryEntry& e) { onGenerated (e); })
{
    output = state.getRawParameterValue (params::id::output);
    tune = state.getRawParameterValue (params::id::tune);
    fine = state.getRawParameterValue (params::id::fine);
    start = state.getRawParameterValue (params::id::start);
    attack = state.getRawParameterValue (params::id::attack);
    decay = state.getRawParameterValue (params::id::decay);
    velocity = state.getRawParameterValue (params::id::velocity);
    sync = state.getRawParameterValue (params::id::sync);
    tempo = state.getRawParameterValue (params::id::tempo);

    auto s = getSettings();
    s.setProperty (params::setting::prompt, "", nullptr);
    s.setProperty (params::setting::seed, (juce::int64) 1234, nullptr);
    s.setProperty (params::setting::seconds, params::defaultSeconds, nullptr);
    s.setProperty (params::setting::model, gen::ModelCatalog::stableAudioOpenSmall, nullptr);
    s.setProperty (params::setting::lengthUnit, (int) gen::LengthUnit::bar1, nullptr);
}

DooblrGenerateProcessor::~DooblrGenerateProcessor()
{
    service.cancel();
}

double DooblrGenerateProcessor::getSeconds()
{
    return juce::jlimit (params::minSeconds, params::maxSeconds,
                         (double) getSettings().getProperty (params::setting::seconds, params::defaultSeconds));
}

juce::String DooblrGenerateProcessor::getModel()
{
    const auto m = getSettings()[params::setting::model].toString();
    return gen::ModelCatalog::find (m) != nullptr ? m : juce::String (gen::ModelCatalog::stableAudioOpenSmall);
}

void DooblrGenerateProcessor::setPrompt (const juce::String& t) { getSettings().setProperty (params::setting::prompt, t.substring (0, 400), nullptr); }
void DooblrGenerateProcessor::setSeed (juce::int64 s) { getSettings().setProperty (params::setting::seed, juce::jlimit ((juce::int64) 0, (juce::int64) 2147483646, s), nullptr); }
void DooblrGenerateProcessor::setSeconds (double s) { getSettings().setProperty (params::setting::seconds, juce::jlimit (params::minSeconds, params::maxSeconds, s), nullptr); }
void DooblrGenerateProcessor::setModel (const juce::String& m)
{
    if (gen::ModelCatalog::find (m) != nullptr)
        getSettings().setProperty (params::setting::model, m, nullptr);
}

juce::int64 DooblrGenerateProcessor::randomSeed()
{
    return (juce::int64) juce::Random::getSystemRandom().nextInt (2147483647);
}

float DooblrGenerateProcessor::getVariationAmount() const
{
    if (auto* p = state.getRawParameterValue (params::id::variationAmount))
        return p->load();
    return 50.0f;
}

bool DooblrGenerateProcessor::isSyncOn() const
{
    return sync != nullptr && sync->load() >= 0.5f;
}

float DooblrGenerateProcessor::getTempoBpm() const
{
    return tempo != nullptr ? tempo->load() : params::defaultTempoBpm;
}

double DooblrGenerateProcessor::getEffectiveBpm() const
{
    const auto host = hostBpm.load();
    return host > 1.0 ? host : (double) getTempoBpm();
}

bool DooblrGenerateProcessor::isUsingHostTempo() const
{
    return isSyncOn() && hostBpm.load() > 1.0;
}

int DooblrGenerateProcessor::getBeatsPerBar() const
{
    return juce::jmax (1, hostNumerator.load());
}

gen::LengthUnit DooblrGenerateProcessor::getLengthUnit()
{
    return gen::lengthUnitFromInt ((int) getSettings().getProperty (params::setting::lengthUnit,
                                                                    (int) gen::LengthUnit::bar1));
}

void DooblrGenerateProcessor::setLengthUnit (gen::LengthUnit unit)
{
    getSettings().setProperty (params::setting::lengthUnit, (int) gen::lengthUnitFromInt ((int) unit), nullptr);
}

double DooblrGenerateProcessor::getModelMaxSeconds()
{
    const auto* model = gen::ModelCatalog::find (getModel());
    const auto modelMax = model != nullptr ? model->maxSeconds : params::maxSeconds;
    return juce::jmin (params::maxSeconds, modelMax);
}

gen::BeatPlan DooblrGenerateProcessor::getBeatPlan()
{
    auto plan = gen::makeBeatPlan (getLengthUnit(), getEffectiveBpm(), getBeatsPerBar(), getModelMaxSeconds());
    plan.usingHostTempo = isUsingHostTempo();
    return plan;
}

void DooblrGenerateProcessor::setHostTransportForTests (double bpm, int numerator, int denominator)
{
    hostBpm.store (bpm > 1.0 ? bpm : 0.0);
    hostNumerator.store (juce::jmax (1, numerator));
    hostDenominator.store (juce::jmax (1, denominator));
}

bool DooblrGenerateProcessor::currentModelSupportsSourceVariation()
{
    return gen::modelSupportsSourceVariation (getModel());
}

bool DooblrGenerateProcessor::generate (bool variation)
{
    if (service.isBusy())
        return false;

    const auto* model = gen::ModelCatalog::find (getModel());
    const auto source = sourceSlot.current();
    const bool varySource = variation && source != nullptr && source->file.existsAsFile();

    if (varySource && (model == nullptr || ! model->supportsSourceVariation))
        return false;

    if (getPrompt().trim().isEmpty() && ! varySource)
        return false;

    if (variation)
        setSeed (randomSeed());

    gen::GenerateRequest r;
    r.model = getModel();
    r.seed = getSeed();
    const auto cap = getModelMaxSeconds();
    alignMessage = {};

    if (varySource)
    {
        r.initAudio = source->file;
        r.initNoiseLevel = juce::jlimit (0.0f, 1.0f, getVariationAmount() / 100.0f);
        r.seconds = juce::jmin (source->lengthSeconds(), cap);
        r.prompt = isSyncOn() ? gen::appendTempoToPrompt (getPrompt(), getEffectiveBpm()) : getPrompt();
        if (isSyncOn())
            r.bpm = getEffectiveBpm();
    }
    else if (isSyncOn())
    {
        const auto plan = getBeatPlan();
        r.seconds = plan.seconds;
        r.alignToGrid = true;
        r.bpm = plan.bpm;
        r.prompt = gen::appendTempoToPrompt (getPrompt(), plan.bpm);
        alignMessage = "Padded/trimmed to the grid (no time-stretch)";
        if (plan.capped)
            alignMessage << " · " << gen::unitName (plan.requested) << " → " << gen::unitName (plan.fitted)
                         << " (" << juce::String (cap, 1) << " s)";
        if (! plan.usingHostTempo)
            alignMessage << " · manual tempo";
    }
    else
    {
        r.seconds = juce::jmin (getSeconds(), cap);
        r.prompt = getPrompt();
    }

    return service.startGenerate (r);
}

void DooblrGenerateProcessor::publish (std::shared_ptr<engine::Sample> sample)
{
    slot.publish (std::move (sample));
    ++sampleSerial;
}

void DooblrGenerateProcessor::onGenerated (const gen::HistoryEntry& e)
{
    // Worker thread: load the WAV here (not on the audio or message thread) and hand it over.
    juce::String error;
    if (auto sample = engine::Sample::load (e.wav, error))
    {
        sample->historyId = e.id;
        sample->prompt = e.prompt;
        sample->model = e.model;
        sample->seed = e.seed;
        sample->isTest = e.isTest;
        publish (std::move (sample));
    }
}

bool DooblrGenerateProcessor::loadHistoryEntry (const juce::String& id, juce::String& error)
{
    gen::HistoryEntry e;
    if (! history.find (id, e))
    {
        error = "Not in the history: " + id;
        return false;
    }

    auto sample = engine::Sample::load (e.wav, error);
    if (sample == nullptr)
        return false;

    sample->historyId = e.id;
    sample->prompt = e.prompt;
    sample->model = e.model;
    sample->seed = e.seed;
    sample->isTest = e.isTest;
    publish (std::move (sample));
    return true;
}

bool DooblrGenerateProcessor::loadFile (const juce::File& file, juce::String& error)
{
    auto sample = engine::Sample::load (file, error);
    if (sample == nullptr)
        return false;

    publish (std::move (sample));
    return true;
}

bool DooblrGenerateProcessor::loadSourceFile (const juce::File& file, juce::String& error)
{
    const auto dest = paths.source().getChildFile ("src-" + juce::Uuid().toDashedString() + ".wav");
    auto loaded = gen::SourceAudio::loadAndStore (file, dest, error);
    if (loaded == nullptr)
    {
        sourceMessage = error;
        sourceMessageError = true;
        return false;
    }

    if (const auto previous = sourceSlot.current(); previous != nullptr
        && previous->file.getParentDirectory() == paths.source()
        && previous->file != dest)
        previous->file.deleteFile();

    auto sample = std::make_shared<engine::Sample>();
    sample->audio = std::move (loaded->audio);
    sample->sampleRate = loaded->sampleRate;
    sample->file = loaded->file;
    sourceName = loaded->originalName;
    sourceTrimmed = loaded->trimmed;
    sourceSlot.publish (std::move (sample));
    ++sourceSerial;

    sourceMessage = "Loaded " + sourceName;
    if (sourceTrimmed)
        sourceMessage << " (trimmed to " << juce::String (gen::SourceAudio::maxSeconds, 1) << " s)";
    sourceMessageError = false;
    error = sourceMessage;
    return true;
}

void DooblrGenerateProcessor::clearSource()
{
    if (const auto previous = sourceSlot.current(); previous != nullptr
        && previous->file.getParentDirectory() == paths.source())
        previous->file.deleteFile();

    sourceSlot.publish (nullptr);
    ++sourceSerial;
    sourceName = {};
    sourceTrimmed = false;
    sourceMessage = {};
    sourceMessageError = false;
}

//==============================================================================
void DooblrGenerateProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampler.prepare (sampleRate, samplesPerBlock);
}

void DooblrGenerateProcessor::releaseResources() {}

bool DooblrGenerateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet().isDisabled();
}

engine::PlaybackSettings DooblrGenerateProcessor::readSettings() const noexcept
{
    engine::PlaybackSettings s;
    s.outputDb = output->load();
    s.tuneSemitones = juce::roundToInt (tune->load());
    s.fineCents = fine->load();
    s.startPercent = start->load();
    s.attackMs = attack->load();
    s.decayMs = decay->load();
    s.velocityPercent = velocity->load();
    return s;
}

void DooblrGenerateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm && *bpm > 1.0)
                hostBpm.store (*bpm);
            else
                hostBpm.store (0.0);

            if (const auto sig = position->getTimeSignature())
            {
                hostNumerator.store (juce::jmax (1, sig->numerator));
                hostDenominator.store (juce::jmax (1, sig->denominator));
            }
        }
    }

    const auto* sample = slot.acquire();
    const auto settings = readSettings();

    const auto* source = sourceSlot.acquire();

    if (sourcePreviewRequested.exchange (false))
    {
        auto preview = settings;
        preview.startPercent = 0.0f;
        preview.tuneSemitones = 0;
        preview.fineCents = 0.0f;
        preview.attackMs = 0.0f;
        preview.decayMs = params::decayOffMs;
        sampler.noteOn (engine::Sampler::rootNote, 1.0f, source, preview);
    }

    if (previewRequested.exchange (false))
        sampler.noteOn (engine::Sampler::rootNote, 1.0f, sample, settings);

    sampler.process (buffer, midi, sample, settings, source);
}

//==============================================================================
void DooblrGenerateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("stateVersion", params::stateVersion, nullptr);
    auto settings = copy.getOrCreateChildWithName (params::setting::tree, nullptr);

    if (const auto current = slot.current())
    {
        settings.setProperty (params::setting::currentWav, current->file.getFullPathName(), nullptr);
        settings.setProperty (params::setting::currentId, current->historyId, nullptr);
    }
    else
    {
        settings.removeProperty (params::setting::currentWav, nullptr);
        settings.removeProperty (params::setting::currentId, nullptr);
    }

    settings.removeProperty (params::setting::sourceWav, nullptr);
    settings.removeProperty (params::setting::sourceName, nullptr);
    settings.removeProperty (params::setting::sourceTrimmed, nullptr);

    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void DooblrGenerateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    state.replaceState (tree);

    auto settings = getSettings();
    const auto wav = juce::File (settings[params::setting::currentWav].toString());
    const auto id = settings[params::setting::currentId].toString();
    juce::String error;

    if (! (id.isNotEmpty() && loadHistoryEntry (id, error)) && wav.existsAsFile())
        loadFile (wav, error);

    // 0.3.0: the SOURCE drop zone is gone. Ignore a stored source path so old sets load cleanly
    // without a hidden audio-to-audio source.
    clearSource();
    settings.removeProperty (params::setting::sourceWav, nullptr);
    settings.removeProperty (params::setting::sourceName, nullptr);
    settings.removeProperty (params::setting::sourceTrimmed, nullptr);
}

juce::AudioProcessorEditor* DooblrGenerateProcessor::createEditor()
{
    return new DooblrGenerateEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrGenerateProcessor();
}
