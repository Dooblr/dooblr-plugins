#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
OctaEQAudioProcessor::OctaEQAudioProcessor()
    : AudioProcessor (createBusesProperties())
{
}

juce::AudioProcessor::BusesProperties OctaEQAudioProcessor::createBusesProperties()
{
    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

//==============================================================================
void OctaEQAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Pass-through holds no sample-rate or block-size dependent state yet. The EQ
    // milestone will allocate and reset its filters here, never in processBlock.
    juce::ignoreUnused (sampleRate, maximumExpectedSamplesPerBlock);
}

void OctaEQAudioProcessor::releaseResources()
{
}

bool OctaEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    // An EQ processes each channel in place, so input and output must match.
    return layouts.getMainInputChannelSet() == mainOutput;
}

//==============================================================================
void OctaEQAudioProcessor::passThrough (juce::AudioBuffer<float>& buffer) const noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    // Channels [0, numInputChannels) already hold the input and are passed through
    // untouched. Output-only channels contain undefined data and must be cleared.
    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

void OctaEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    passThrough (buffer);
}

void OctaEQAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // The plugin reports zero latency, so bypass is an exact pass-through too.
    juce::ignoreUnused (midiMessages);
    passThrough (buffer);
}

//==============================================================================
juce::AudioProcessorEditor* OctaEQAudioProcessor::createEditor()
{
    return new OctaEQAudioProcessorEditor (*this);
}

bool OctaEQAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String OctaEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool OctaEQAudioProcessor::acceptsMidi() const   { return false; }
bool OctaEQAudioProcessor::producesMidi() const  { return false; }
bool OctaEQAudioProcessor::isMidiEffect() const  { return false; }

double OctaEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
// Some hosts misbehave if a plugin reports zero programs, so expose a single one.
int OctaEQAudioProcessor::getNumPrograms()                                 { return 1; }
int OctaEQAudioProcessor::getCurrentProgram()                              { return 0; }
void OctaEQAudioProcessor::setCurrentProgram (int)                         {}
const juce::String OctaEQAudioProcessor::getProgramName (int)              { return {}; }
void OctaEQAudioProcessor::changeProgramName (int, const juce::String&)    {}

//==============================================================================
void OctaEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // A versioned ValueTree, so parameters can be added later without breaking
    // sessions saved by this milestone.
    juce::ValueTree state (stateType);
    state.setProperty (stateVersionProperty, currentStateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OctaEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    if (! state.hasType (stateType))
        return;

    lastRestoredStateVersion = state.getProperty (stateVersionProperty, 0);
}

//==============================================================================
// Entry point used by the JUCE plugin wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OctaEQAudioProcessor();
}
