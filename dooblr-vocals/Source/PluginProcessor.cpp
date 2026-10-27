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
    updateLatency (settings);
    engine.setSettings (settings);
    engine.process (buffer, buffer.getNumSamples());
    gainReductionDb.store (engine.getGainReductionDb());

    const auto n = buffer.getNumSamples();
    if (n > 0 && buffer.getNumChannels() > 0)
        outputPeakTap.push (buffer.getMagnitude (0, 0, n),
                            buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, n) : buffer.getMagnitude (0, 0, n));
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
