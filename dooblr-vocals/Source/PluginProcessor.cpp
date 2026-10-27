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
}

//==============================================================================
DooblrVocalsAudioProcessor::DooblrVocalsAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void DooblrVocalsAudioProcessor::updateLatency (const dsp::VocalSettings& s)
{
    const auto samples = engine.latencyFor (s.pitch, s.nonRealtime);

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

    const auto totalNumInput = getTotalNumInputChannels();
    const auto totalNumOutput = getTotalNumOutputChannels();

    for (int ch = totalNumInput; ch < totalNumOutput; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (totalNumInput == 1 && totalNumOutput >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    auto settings = getCurrentSettings();
    settings.nonRealtime = isNonRealtime();
    settings.pitch.learnRange = learnArmed.load (std::memory_order_relaxed);

    // Host tempo for synced delay / pre-delay (120 BPM when the host gives none).
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);
    settings.bpm = hostBpm.load (std::memory_order_relaxed);

    updateLatency (settings);
    engine.setSettings (settings);
    engine.process (buffer, buffer.getNumSamples());
    gainReductionDb.store (engine.getGainReductionDb());
    reverbDuckDb.store (engine.getReverbDuckDb());
    delayDuckDb.store (engine.getDelayDuckDb());

    const auto n = buffer.getNumSamples();
    if (n > 0 && buffer.getNumChannels() > 0)
        outputPeakTap.push (buffer.getMagnitude (0, 0, n),
                            buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, n) : buffer.getMagnitude (0, 0, n));
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
    learnArmed.store (true);
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
