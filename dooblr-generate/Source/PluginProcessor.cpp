#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    auto s = getSettings();
    s.setProperty (params::setting::prompt, "", nullptr);
    s.setProperty (params::setting::seed, (juce::int64) 1234, nullptr);
    s.setProperty (params::setting::seconds, params::defaultSeconds, nullptr);
    s.setProperty (params::setting::model, gen::ModelCatalog::stableAudioOpenSmall, nullptr);
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

bool DooblrGenerateProcessor::generate (bool variation)
{
    if (getPrompt().trim().isEmpty() || service.isBusy())
        return false;

    if (variation)
        setSeed (randomSeed());

    gen::GenerateRequest r;
    r.prompt = getPrompt();
    r.model = getModel();
    r.seed = getSeed();
    const auto* model = gen::ModelCatalog::find (r.model);
    r.seconds = juce::jmin (getSeconds(), model != nullptr ? model->maxSeconds : params::maxSeconds);
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
    const auto* sample = slot.acquire();
    const auto settings = readSettings();

    if (previewRequested.exchange (false))
        sampler.noteOn (engine::Sampler::rootNote, 1.0f, sample, settings);

    sampler.process (buffer, midi, sample, settings);
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

    if (id.isNotEmpty() && loadHistoryEntry (id, error))
        return;

    if (wav.existsAsFile())
        loadFile (wav, error);
}

juce::AudioProcessorEditor* DooblrGenerateProcessor::createEditor()
{
    return new DooblrGenerateEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrGenerateProcessor();
}
