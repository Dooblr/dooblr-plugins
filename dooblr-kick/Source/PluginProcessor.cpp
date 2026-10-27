#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace params = dooblrkick::params;
namespace dsp = dooblrkick::dsp;
namespace presets = dooblrkick::presets;

namespace
{
    const juce::Identifier stateType ("DooblrKick");
    const juce::Identifier stateVersionProperty ("stateVersion");
    const juce::Identifier presetNameProperty ("presetName");
    // 1: v0.1.0. 2: v0.1.1, where the MIDI note no longer changes the synthesised layers and
    //    the click/body/tail "Key Track" switches became sample-only (default off).
    // 3: v0.1.1 sample list. 4: v0.3.0 added the master envelope (menv_*), dry / wet (mix_drywet)
    //    and the chain order (chain_order). Older states have none of them, so they load at their
    //    defaults (flat 100 % envelope, 100 % wet, the v0.2.0 order), which render bit-identically.
    constexpr int stateVersion = 4;

    /** Brings an older saved state / preset tree up to the current stateVersion, in place. */
    void migrateState (juce::ValueTree& state)
    {
        const auto version = (int) state.getProperty (stateVersionProperty, 1);

        if (version < 2)
        {
            // A synth layer's v1 key-track switch is meaningless now (synths ignore the note);
            // clear it so the sample-only switch starts off if the layer is later sampled.
            // Sampled layers keep their switch, so they still follow the note as before.
            for (const auto layer : { dsp::clickLayer, dsp::bodyLayer, dsp::tailLayer })
            {
                const auto source = state.getChildWithProperty ("id", params::layerId (layer, "source"));
                auto keyTrack = state.getChildWithProperty ("id", params::layerId (layer, "keytrack"));

                if (keyTrack.isValid() && (! source.isValid() || (float) source.getProperty ("value") < 0.5f))
                    keyTrack.setProperty ("value", 0.0f, nullptr);
            }
        }

        if (version < 3)
        {
            // 0.1.1 inserted 10 factory samples before "User file": move that choice to its new index
            // so a layer that played a user sample still does (the factory indices 0 .. 10 are unchanged).
            for (int layer = 0; layer < params::numLayers; ++layer)
            {
                if (! params::layerHasSample (layer))
                    continue;

                auto choice = state.getChildWithProperty ("id", params::layerId (layer, "sample"));

                if (choice.isValid() && juce::roundToInt ((float) choice.getProperty ("value")) >= params::legacyUserSampleChoice)
                    choice.setProperty ("value", (float) params::userSampleChoice, nullptr);
            }
        }

        state.setProperty (stateVersionProperty, stateVersion, nullptr);
    }

    juce::Identifier userSampleProperty (int layer)
    {
        return juce::Identifier ("userSample_" + juce::String (params::layerPrefixes[(size_t) layer]));
    }
}

//==============================================================================
DooblrKickAudioProcessor::DooblrKickAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createParameterLayout()),
      settingsReader (parameters),
      undoHistory (*this)
{
    for (auto* parameter : getParameters())
        parameter->addListener (this);
}

DooblrKickAudioProcessor::~DooblrKickAudioProcessor()
{
    stopTimer();

    for (auto* parameter : getParameters())
        parameter->removeListener (this);
}

//==============================================================================
void DooblrKickAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    meterTap.prepare (sampleRate);
    engine.setMeterTap (&meterTap);
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    engine.setSettings (getCurrentSettings());
    auditionSamplesLeft = 0;
    auditionNote = -1;
}

void DooblrKickAudioProcessor::releaseResources()
{
    engine.reset();
}

bool DooblrKickAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void DooblrKickAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    engine.setSettings (getCurrentSettings());

    if (auditionRequested.exchange (false))
    {
        auditionNote = engine.getSettings().play.keyNote;
        engine.noteOn (auditionNote, 1.0f);
        auditionSamplesLeft = (int) (0.4 * getSampleRate());
    }

    if (auditionSamplesLeft > 0)
    {
        auditionSamplesLeft -= buffer.getNumSamples();

        if (auditionSamplesLeft <= 0)
            engine.noteOff (auditionNote);
    }

    engine.process (buffer, midi);
    gainReductionDb.store (engine.getGainReductionDb());
}

//==============================================================================
dsp::KickSettings DooblrKickAudioProcessor::getCurrentSettings() const noexcept
{
    auto settings = settingsReader.read();
    resolveSamples (settings);
    return settings;
}

void DooblrKickAudioProcessor::resolveSamples (dsp::KickSettings& s) const noexcept
{
    const auto resolve = [this] (int layer, dsp::SampleSource& source)
    {
        source.data = source.index == params::userSampleChoice ? userSlots[(size_t) layer].current.load()
                                                               : factoryBank->get (source.index);
    };

    resolve (dsp::clickLayer, s.click.sample);
    resolve (dsp::bodyLayer, s.body.sample);
    resolve (dsp::tailLayer, s.tail.sample);
    resolve (dsp::noiseLayer, s.noise.sample);
}

juce::AudioBuffer<float> DooblrKickAudioProcessor::renderPreview (double sampleRate) const
{
    const auto settings = getCurrentSettings();
    return dsp::KickEngine::renderOneShot (settings, sampleRate, settings.play.keyNote, 1.0f);
}

std::function<juce::AudioBuffer<float>()> DooblrKickAudioProcessor::makePreviewJob (double sampleRate)
{
    const auto settings = getCurrentSettings();

    // Counted while the job exists, so retired user samples are not freed under a render.
    auto inFlight = previewJobsInFlight;
    inFlight->fetch_add (1);
    auto guard = std::shared_ptr<void> (nullptr, [inFlight] (void*) { inFlight->fetch_sub (1); });

    return [settings, sampleRate, guard]
    {
        return dsp::KickEngine::renderOneShot (settings, sampleRate, settings.play.keyNote, 1.0f);
    };
}

juce::File DooblrKickAudioProcessor::getDefaultExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-kick");
}

bool DooblrKickAudioProcessor::exportWav (const juce::File& file, double sampleRate, juce::String& error) const
{
    const auto audio = renderPreview (sampleRate);

    if (! file.getParentDirectory().createDirectory())
    {
        error = "cannot create " + file.getParentDirectory().getFullPathName();
        return false;
    }

    file.deleteFile();
    auto stream = file.createOutputStream();

    if (stream == nullptr)
    {
        error = "cannot write " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> output (stream.release());
    auto writer = wav.createWriterFor (output, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (audio.getNumChannels())
                                                   .withBitsPerSample (24));

    if (writer == nullptr)
    {
        error = "cannot create a WAV writer";
        return false;
    }

    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
    {
        error = "write failed";
        return false;
    }

    return true;
}

//==============================================================================
void DooblrKickAudioProcessor::loadFactoryPreset (int index, bool asGesture)
{
    const auto& list = presets::getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    presets::apply (parameters, presets::resolve (parameters, list[(size_t) index].values), asGesture);
    currentProgram = index;
    presetName = list[(size_t) index].name;
    ++changeCount;
}

void DooblrKickAudioProcessor::randomise (juce::Random& random)
{
    presets::apply (parameters, presets::randomise (parameters, random), true);
    presetName = "Random";
    ++changeCount;
}

bool DooblrKickAudioProcessor::saveUserPreset (const juce::File& file, juce::String& error)
{
    auto state = createStateTree();
    state.setProperty (presetNameProperty, file.getFileNameWithoutExtension(), nullptr);
    const auto xml = state.createXml();

    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
    {
        error = "cannot write " + file.getFullPathName();
        return false;
    }

    presetName = file.getFileNameWithoutExtension();
    return true;
}

bool DooblrKickAudioProcessor::loadUserPreset (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
    {
        error = "not a dooblr-kick preset";
        return false;
    }

    auto state = juce::ValueTree::fromXml (*xml);
    migrateState (state);
    presets::ValueList overrides;

    for (const auto& child : state)
        if (child.hasType ("PARAM") && parameters.getParameter (child.getProperty ("id").toString()) != nullptr)
            overrides.emplace_back (child.getProperty ("id").toString(), (float) child.getProperty ("value"));

    presets::apply (parameters, presets::resolve (parameters, overrides), true);
    restoreUserSamples (state);
    presetName = file.getFileNameWithoutExtension();
    ++changeCount;
    return true;
}

//==============================================================================
bool DooblrKickAudioProcessor::loadUserSample (int layer, const juce::File& file, juce::String& error)
{
    if (! juce::isPositiveAndBelow (layer, params::numLayers) || ! params::layerHasSample (layer))
    {
        error = "this layer has no sample source";
        return false;
    }

    auto data = dooblrkick::samples::loadAudioFile (file, error);

    if (data == nullptr)
        return false;

    auto& slot = userSlots[(size_t) layer];
    slot.current.store (data.get());
    retire (std::move (slot.owned));
    slot.owned = std::move (data);
    slot.path = file.getFullPathName();
    ++changeCount;
    return true;
}

void DooblrKickAudioProcessor::clearUserSample (int layer)
{
    if (! juce::isPositiveAndBelow (layer, params::numLayers))
        return;

    auto& slot = userSlots[(size_t) layer];
    slot.current.store (nullptr);
    retire (std::move (slot.owned));
    slot.path.clear();
    ++changeCount;
}

juce::String DooblrKickAudioProcessor::getUserSamplePath (int layer) const
{
    return juce::isPositiveAndBelow (layer, params::numLayers) ? userSlots[(size_t) layer].path : juce::String();
}

const dsp::SampleData* DooblrKickAudioProcessor::getUserSample (int layer) const noexcept
{
    return juce::isPositiveAndBelow (layer, params::numLayers) ? userSlots[(size_t) layer].current.load() : nullptr;
}

void DooblrKickAudioProcessor::retire (std::unique_ptr<dsp::SampleData> data)
{
    // The audio thread may still be rendering the old sample for the current block, so it is
    // freed by the timer at least two seconds later instead of here.
    if (data == nullptr)
        return;

    retired.push_back ({ std::move (data), juce::Time::getMillisecondCounter() });
    startTimer (500);
}

void DooblrKickAudioProcessor::timerCallback()
{
    if (previewJobsInFlight->load() > 0)
        return; // a background preview may still be reading a retired sample

    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const Retired& r) { return now - r.time >= 2000; }),
                   retired.end());

    if (retired.empty())
        stopTimer();
}

void DooblrKickAudioProcessor::restoreUserSamples (const juce::ValueTree& state)
{
    for (int layer = 0; layer < params::numLayers; ++layer)
    {
        if (! params::layerHasSample (layer))
            continue;

        const auto path = state.getProperty (userSampleProperty (layer)).toString();

        if (path.isEmpty())
        {
            clearUserSample (layer);
            continue;
        }

        juce::String error;

        if (! loadUserSample (layer, juce::File (path), error))
        {
            // Keep the path so saving again does not lose it; the layer plays silence meanwhile.
            clearUserSample (layer);
            userSlots[(size_t) layer].path = path;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* DooblrKickAudioProcessor::createEditor()
{
    return new DooblrKickAudioProcessorEditor (*this);
}

bool DooblrKickAudioProcessor::hasEditor() const { return true; }

const juce::String DooblrKickAudioProcessor::getName() const { return JucePlugin_Name; }

bool DooblrKickAudioProcessor::acceptsMidi() const   { return true; }
bool DooblrKickAudioProcessor::producesMidi() const  { return false; }
bool DooblrKickAudioProcessor::isMidiEffect() const  { return false; }

double DooblrKickAudioProcessor::getTailLengthSeconds() const { return 0.1; }

int DooblrKickAudioProcessor::getNumPrograms() { return (int) presets::getFactoryPresets().size(); }
int DooblrKickAudioProcessor::getCurrentProgram() { return currentProgram; }
void DooblrKickAudioProcessor::setCurrentProgram (int index) { loadFactoryPreset (index, false); }

const juce::String DooblrKickAudioProcessor::getProgramName (int index)
{
    const auto& list = presets::getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String();
}

void DooblrKickAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
juce::ValueTree DooblrKickAudioProcessor::createStateTree()
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, stateVersion, nullptr);
    state.setProperty (presetNameProperty, presetName, nullptr);

    for (int layer = 0; layer < params::numLayers; ++layer)
        if (params::layerHasSample (layer))
            state.setProperty (userSampleProperty (layer), userSlots[(size_t) layer].path, nullptr);

    return state;
}

void DooblrKickAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void DooblrKickAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    migrateState (state);

    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto child = state.getChild (i);

        if (child.hasType ("PARAM") && parameters.getParameter (child.getProperty ("id").toString()) == nullptr)
            state.removeChild (i, nullptr);
    }

    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            if (! state.getChildWithProperty ("id", ranged->paramID).isValid())
            {
                juce::ValueTree child ("PARAM");
                child.setProperty ("id", ranged->paramID, nullptr);
                child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                state.appendChild (child, nullptr);
            }
        }
    }

    restoreUserSamples (state);
    presetName = state.getProperty (presetNameProperty, "Init").toString();
    parameters.replaceState (state);
    undoHistory.clear();
    ++changeCount;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrKickAudioProcessor();
}
