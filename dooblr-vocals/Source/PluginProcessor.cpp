#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace params = dooblrvocals::params;
namespace dsp = dooblrvocals::dsp;
namespace presets = dooblrvocals::presets;

namespace
{
    const juce::Identifier stateType ("DooblrVocals");
    const juce::Identifier stateVersionProperty ("stateVersion");
    const juce::Identifier presetNameProperty ("presetName");
    const juce::Identifier noiseProfileProperty ("noiseProfile"); // learned NR profile, base64 floats
}

//==============================================================================
DooblrVocalsAudioProcessor::DooblrVocalsAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Optional Key Follow sidechain (VST3 aux input); off until the host enables it.
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters (*this, nullptr, stateType, params::createParameterLayout()),
      settingsReader (parameters),
      undoHistory (*this)
{
}

DooblrVocalsAudioProcessor::~DooblrVocalsAudioProcessor() = default;

//==============================================================================
void DooblrVocalsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    meterTap.prepare (sampleRate);
    engine.setMeterTap (&meterTap);
    engine.setPitchTap (&pitchTap);
    engine.setLearnTap (&learnTap);
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    learnSampleRate.store (sampleRate);

    // Keep the detected key across re-prepares at the same rate (hosts re-prepare often).
    if (! juce::exactlyEqual (sampleRate, keyDetectorRate))
    {
        keyDetector.prepare (sampleRate);
        keyDetectorRate = sampleRate;
        lockedKey = -1;
        appliedKey.store (-1);
    }
    auto settings = getCurrentSettings();
    settings.nonRealtime = isNonRealtime();
    engine.setSettings (settings);
    updateLatency (settings);
}

void DooblrVocalsAudioProcessor::releaseResources()
{
    engine.reset();
}

void DooblrVocalsAudioProcessor::reset()
{
    engine.reset();
}

bool DooblrVocalsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    const auto mainOk = out == juce::AudioChannelSet::stereo()
                     && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());

    if (! mainOk || layouts.outputBuses.size() > 1 || layouts.inputBuses.size() > 2)
        return false;

    // The Key Follow sidechain may be disabled, mono or stereo.
    if (layouts.inputBuses.size() == 2)
    {
        const auto& sc = layouts.inputBuses.getReference (1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
    }

    return true;
}

bool DooblrVocalsAudioProcessor::isSidechainConnected() const noexcept
{
    const auto* bus = getBus (true, 1);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

DooblrVocalsAudioProcessor::KeyFollowStatus DooblrVocalsAudioProcessor::getKeyFollowStatus() const noexcept
{
    KeyFollowStatus st;
    st.detectedKey = keyDetector.getKey();
    st.candidateKey = keyDetector.getCandidate();
    st.confidence = keyDetector.getConfidence();
    st.activeSource = keyDetector.getActiveSource();
    st.sidechainConnected = isSidechainConnected();
    st.sidechainSignal = keyDetector.hasSidechainSignal();
    st.appliedKey = appliedKey.load (std::memory_order_relaxed);
    return st;
}

void DooblrVocalsAudioProcessor::applyKeyFollow (juce::AudioBuffer<float>& buffer, dsp::VocalSettings& s) noexcept
{
    const auto n = buffer.getNumSamples();
    const auto follow = settingsReader.readKeyFollow();

    // The sidechain bus is read only through the current layout (a disabled bus has no channels).
    const float* scPtrs[2] {};
    int scChannels = 0;
    if (isSidechainConnected())
    {
        const auto first = getChannelIndexInProcessBlockBuffer (true, 1, 0);
        scChannels = juce::jmin (2, getBus (true, 1)->getNumberOfChannels());
        if (first + scChannels > buffer.getNumChannels())
            scChannels = 0;
        for (int ch = 0; ch < scChannels; ++ch)
            scPtrs[ch] = buffer.getReadPointer (first + ch);
    }

    const float* vocalPtrs[2] {};
    const auto vocalChannels = juce::jmin (2, getMainBusNumInputChannels(), buffer.getNumChannels());
    for (int ch = 0; ch < vocalChannels; ++ch)
        vocalPtrs[ch] = buffer.getReadPointer (ch);

    dsp::KeyDetector::Settings ks;
    ks.sensitivity = follow.sensitivity;
    ks.source = (dsp::KeySource) follow.source;
    ks.a4Hz = s.pitch.a4Hz > 0.0f ? s.pitch.a4Hz : 440.0f;
    keyDetector.process (scChannels > 0 ? scPtrs : nullptr, scChannels, vocalPtrs, vocalChannels, n, ks);

    if (! follow.enabled)
    {
        lockedKey = -1;
        appliedKey.store (-1, std::memory_order_relaxed);
        return;
    }

    auto key = keyDetector.getKey();
    if (follow.locked)
    {
        if (lockedKey < 0)
            lockedKey = key; // locks to whatever was being applied (or the first detection)
        key = lockedKey;
    }
    else
    {
        lockedKey = key;
    }

    appliedKey.store (key, std::memory_order_relaxed);
    if (key < 0)
        return; // nothing detected yet: the manual Key / Scale stay in charge

    // Internal override only: corr_key / corr_scale are never written, so automation is untouched.
    s.pitch.key = dsp::keyTonic (key);
    s.pitch.scale = dsp::keyIsMinor (key) ? dsp::ScaleType::minor : dsp::ScaleType::major;
}

void DooblrVocalsAudioProcessor::updateLatency (const dsp::VocalSettings& s)
{
    const auto samples = engine.latencyFor (s); // pitch look-ahead + noise reducer STFT (0 when off)

    if (samples != reportedLatency)
    {
        reportedLatency = samples;
        setLatencySamples (samples);
    }
}

void DooblrVocalsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    auto settings = getCurrentSettings();

    // Key Follow listens first: the sidechain channels may share buffer slots with the main
    // output (e.g. a mono main input), and the vocal tap must be the dry input.
    applyKeyFollow (buffer, settings);

    const auto mainIn = getMainBusNumInputChannels();
    const auto mainOut = getMainBusNumOutputChannels();
    auto mainBus = getBusBuffer (buffer, false, 0);

    if (mainIn == 1 && mainOut >= 2)
        mainBus.copyFrom (1, 0, mainBus, 0, 0, mainBus.getNumSamples());
    else
        for (int ch = juce::jmax (0, mainIn); ch < mainOut; ++ch)
            mainBus.clear (ch, 0, mainBus.getNumSamples());

    settings.nonRealtime = isNonRealtime();
    settings.pitch.learnRange = learnArmed.load (std::memory_order_relaxed);
    settings.noiseReducer.learn = noiseLearnArmed.load (std::memory_order_relaxed);

    // Host tempo for synced delay / pre-delay (120 BPM when the host gives none).
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);
    settings.bpm = hostBpm.load (std::memory_order_relaxed);

    updateLatency (settings);
    engine.setSettings (settings);
    engine.process (mainBus, mainBus.getNumSamples());
    gainReductionDb.store (engine.getGainReductionDb());
    for (int band = 0; band < 3; ++band)
        eqDynamicGainDb[(size_t) band].store (engine.getEqDynamicGainDb (band), std::memory_order_relaxed);

    if (settings.pitch.learnRange)
    {
        // Learn Range progress for the editor: voiced time by the learner's own confidence test.
        const auto estimate = engine.getLastPitch();
        const auto block = (juce::int64) mainBus.getNumSamples();

        if (estimate.voiced && estimate.hz > 20.0f && estimate.confidence >= dooblrvocals::RangeLearner::minConfidence)
        {
            learnVoicedSamples.fetch_add (block, std::memory_order_relaxed);
            learnSamplesSinceVoice.store (0, std::memory_order_relaxed);
        }
        else
        {
            learnSamplesSinceVoice.fetch_add (block, std::memory_order_relaxed);
        }
    }
    reverbDuckDb.store (engine.getReverbDuckDb());
    delayDuckDb.store (engine.getDelayDuckDb());

    const auto n = mainBus.getNumSamples();
    if (n > 0 && mainBus.getNumChannels() > 0)
        outputPeakTap.push (mainBus.getMagnitude (0, 0, n),
                            mainBus.getNumChannels() > 1 ? mainBus.getMagnitude (1, 0, n) : mainBus.getMagnitude (0, 0, n));
}

double DooblrVocalsAudioProcessor::getTailLengthSeconds() const
{
    // The reverb and delay tails (only when they are on); otherwise the short pitch-engine tail.
    const auto s = settingsReader.read();
    auto tail = 0.1;

    if (s.reverb.enabled)
        tail = std::max (tail, (double) s.reverb.decaySeconds * 1.2 + 0.25 + (double) s.reverb.preDelayMs * 0.001);

    if (s.delay.enabled)
    {
        const auto bpm = hostBpm.load();
        const auto longest = std::max (dooblrvocals::dsp::VocalDelay::delaySamplesFor (s.delay, 0, bpm, 48000.0),
                                       dooblrvocals::dsp::VocalDelay::delaySamplesFor (s.delay, 1, bpm, 48000.0)) / 48000.0;
        const auto fb = std::clamp ((double) s.delay.feedback, 0.0, (double) dooblrvocals::dsp::VocalDelay::maxFeedback);
        const auto repeats = fb > 0.001 ? std::log (0.001) / std::log (fb) : 1.0; // until -60 dB
        tail = std::max (tail, std::min (30.0, longest * (1.0 + repeats)));
    }

    return tail;
}

//==============================================================================
void DooblrVocalsAudioProcessor::startRangeLearn()
{
    rangeLearner.clear();
    learnTap.discardAll();
    learnVoicedSamples.store (0);
    learnSamplesSinceVoice.store (1 << 30);
    learnArmed.store (true);
}

DooblrVocalsAudioProcessor::LearnProgress DooblrVocalsAudioProcessor::getLearnProgress() const noexcept
{
    LearnProgress out;
    const auto rate = std::max (1.0, learnSampleRate.load (std::memory_order_relaxed));
    out.voicedSeconds = (float) ((double) learnVoicedSamples.load (std::memory_order_relaxed) / rate);
    out.progress = std::clamp (out.voicedSeconds / learnTargetSeconds, 0.0f, 1.0f);
    out.hearingVoice = (double) learnSamplesSinceVoice.load (std::memory_order_relaxed) < learnVoiceHoldSeconds * rate;
    return out;
}

void DooblrVocalsAudioProcessor::pollRangeLearn()
{
    for (;;)
    {
        const auto n = learnTap.pop (learnScratch.data(), (int) learnScratch.size());
        if (n <= 0)
            break;
        if (learnArmed.load())
            rangeLearner.add (learnScratch.data(), n);
    }
}

dooblrvocals::LearnedRange DooblrVocalsAudioProcessor::stopRangeLearn()
{
    pollRangeLearn();
    learnArmed.store (false);
    learnTap.discardAll();
    const auto result = rangeLearner.compute();

    if (result.valid)
        applyLearnedRange (result);

    return result;
}

void DooblrVocalsAudioProcessor::cancelRangeLearn()
{
    learnArmed.store (false);
    learnTap.discardAll();
    rangeLearner.clear();
}

void DooblrVocalsAudioProcessor::applyLearnedRange (const dooblrvocals::LearnedRange& range)
{
    if (! range.valid)
        return;

    presets::apply (parameters, { { "range_custom", 1.0f },
                                  { "range_low", range.searchLowHz },
                                  { "range_high", range.searchHighHz },
                                  { "input_type", (float) (int) range.type } }, true);
}

void DooblrVocalsAudioProcessor::resetLearnedRange()
{
    presets::ValueList values;
    for (const auto* id : { "range_custom", "range_low", "range_high" })
        if (auto* p = parameters.getParameter (id))
            values.emplace_back (id, p->convertFrom0to1 (p->getDefaultValue()));
    presets::apply (parameters, values, true);
}

//==============================================================================
void DooblrVocalsAudioProcessor::loadFactoryPreset (int index, bool asGesture)
{
    const auto& list = presets::getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    presets::apply (parameters, presets::resolve (parameters, list[(size_t) index].values), asGesture);
    currentProgram = index;
    presetName = list[(size_t) index].name;
}

void DooblrVocalsAudioProcessor::randomise (juce::Random& random)
{
    presets::apply (parameters, presets::randomise (parameters, random), true);
    presetName = "Random";
}

bool DooblrVocalsAudioProcessor::saveUserPreset (const juce::File& file, juce::String& error)
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

bool DooblrVocalsAudioProcessor::loadUserPreset (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
    {
        error = "not a dooblr-vocals preset";
        return false;
    }

    auto state = juce::ValueTree::fromXml (*xml);
    presets::ValueList overrides;

    for (const auto& child : state)
        if (child.hasType ("PARAM") && parameters.getParameter (child.getProperty ("id").toString()) != nullptr)
            overrides.emplace_back (child.getProperty ("id").toString(), (float) child.getProperty ("value"));

    presets::apply (parameters, presets::resolve (parameters, overrides), true);
    presetName = file.getFileNameWithoutExtension();
    return true;
}

int DooblrVocalsAudioProcessor::getNumPrograms() { return (int) presets::getFactoryPresets().size(); }

void DooblrVocalsAudioProcessor::setCurrentProgram (int index) { loadFactoryPreset (index, false); }

const juce::String DooblrVocalsAudioProcessor::getProgramName (int index)
{
    const auto& list = presets::getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String();
}

juce::ValueTree DooblrVocalsAudioProcessor::createStateTree()
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, params::stateVersion, nullptr);
    state.setProperty (presetNameProperty, presetName, nullptr);

    // The learned noise profile travels with the session (not with presets).
    if (engine.getNoiseReducer().hasProfile())
    {
        std::vector<float> profile;
        engine.getNoiseReducer().copyProfile (profile);
        juce::MemoryBlock block (profile.data(), profile.size() * sizeof (float));
        state.setProperty (noiseProfileProperty, block.toBase64Encoding(), nullptr);
    }
    else
    {
        state.removeProperty (noiseProfileProperty, nullptr);
    }
    return state;
}

void DooblrVocalsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void DooblrVocalsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

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

    presetName = state.getProperty (presetNameProperty, "Init").toString();

    {
        juce::MemoryBlock block;
        const auto expected = (size_t) dsp::NoiseReducer::profileSize * sizeof (float);
        if (state.hasProperty (noiseProfileProperty)
            && block.fromBase64Encoding (state.getProperty (noiseProfileProperty).toString()) && block.getSize() == expected)
        {
            std::vector<float> profile ((size_t) dsp::NoiseReducer::profileSize);
            std::memcpy (profile.data(), block.getData(), expected);
            engine.getNoiseReducer().setProfile (profile);
        }
        else
        {
            engine.getNoiseReducer().clearProfile();
        }
        state.removeProperty (noiseProfileProperty, nullptr);
    }

    parameters.replaceState (state);
    undoHistory.clear();
}

juce::AudioProcessorEditor* DooblrVocalsAudioProcessor::createEditor()
{
    return new DooblrVocalsAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrVocalsAudioProcessor();
}
