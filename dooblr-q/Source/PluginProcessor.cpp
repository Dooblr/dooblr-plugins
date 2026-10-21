#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace params = dooblrq::params;
namespace dsp = dooblrq::dsp;

//==============================================================================
DooblrQAudioProcessor::DooblrQAudioProcessor()
    : AudioProcessor (createBusesProperties()),
      parameters (*this, nullptr, stateType, params::createParameterLayout())
{
    const auto pointer = [this] (const juce::String& id)
    {
        auto* value = parameters.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };

    for (int b = 0; b < params::numBands; ++b)
    {
        auto& p = bandParameters[(size_t) b];
        p.enabled         = pointer (params::bandId (b, params::band::enabled));
        p.type            = pointer (params::bandId (b, params::band::type));
        p.frequency       = pointer (params::bandId (b, params::band::frequency));
        p.gain            = pointer (params::bandId (b, params::band::gain));
        p.q               = pointer (params::bandId (b, params::band::q));
        p.dynamicsEnabled = pointer (params::bandId (b, params::band::dynamicsEnabled));
        p.range           = pointer (params::bandId (b, params::band::range));
        p.threshold       = pointer (params::bandId (b, params::band::threshold));
        p.attack          = pointer (params::bandId (b, params::band::attack));
        p.release         = pointer (params::bandId (b, params::band::release));
    }

    outputGainDb = pointer (params::outputGain);
    stereoLink   = pointer (params::stereoLink);
}

juce::AudioProcessor::BusesProperties DooblrQAudioProcessor::createBusesProperties()
{
    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

//==============================================================================
dsp::BandSettings DooblrQAudioProcessor::getBandSettings (int bandIndex) const noexcept
{
    const auto& p = bandParameters[(size_t) bandIndex];
    const auto load = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed); };

    dsp::BandSettings s;
    s.enabled         = load (p.enabled) >= 0.5f;
    s.type            = (dsp::FilterType) juce::jlimit (0, dsp::numFilterTypes - 1, juce::roundToInt (load (p.type)));
    s.frequency       = load (p.frequency);
    s.gainDb          = load (p.gain);
    s.q               = load (p.q);
    s.dynamicsEnabled = load (p.dynamicsEnabled) >= 0.5f;
    s.rangeDb         = load (p.range);
    s.thresholdDb     = load (p.threshold);
    s.attackMs        = load (p.attack);
    s.releaseMs       = load (p.release);
    return s;
}

void DooblrQAudioProcessor::updateBandSettings (bool snap) noexcept
{
    for (int b = 0; b < params::numBands; ++b)
        bands[(size_t) b].setSettings (getBandSettings (b), snap);

    const auto gain = juce::Decibels::decibelsToGain (outputGainDb->load (std::memory_order_relaxed));

    if (snap)
        outputGain.setCurrentAndTargetValue (gain);
    else
        outputGain.setTargetValue (gain);
}

float DooblrQAudioProcessor::getBandDynamicGainDb (int bandIndex) const noexcept
{
    return bands[(size_t) bandIndex].getDynamicGainDb();
}

void DooblrQAudioProcessor::setAnalyzerRunning (bool shouldRun) noexcept
{
    if (shouldRun && ! analyzerRunning.load())
        analyzerFifo.discardAll(); // drop audio left over from an earlier session

    analyzerRunning.store (shouldRun, std::memory_order_relaxed);
}

double DooblrQAudioProcessor::getAnalysisSampleRate() const noexcept
{
    const auto rate = preparedSampleRate.load();
    return rate > 0.0 ? rate : 48000.0;
}

//==============================================================================
void DooblrQAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused (maximumExpectedSamplesPerBlock);

    preparedSampleRate.store (sampleRate);

    for (auto& band : bands)
        band.prepare (sampleRate);

    outputGain.reset (sampleRate, 0.03);
    updateBandSettings (true);

    for (auto& band : bands)
        band.reset();
}

void DooblrQAudioProcessor::releaseResources()
{
}

void DooblrQAudioProcessor::reset()
{
    for (auto& band : bands)
        band.reset();
}

bool DooblrQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    // An EQ processes each channel in place, so input and output must match.
    return layouts.getMainInputChannelSet() == mainOutput;
}

//==============================================================================
void DooblrQAudioProcessor::clearOutputOnlyChannels (juce::AudioBuffer<float>& buffer) const noexcept
{
    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

void DooblrQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    clearOutputOnlyChannels (buffer);

    const auto numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(),
                                         dsp::DynamicEQBand::maxChannels);

    if (numChannels <= 0)
        return;

    // Analyzer feed: a lock-free, allocation-free copy into the FIFO, only while an editor shows it.
    const auto analyse = analyzerRunning.load (std::memory_order_relaxed);
    const auto analysePre = analyse && getAnalyzerSource() == AnalyzerSource::preEq;

    if (analysePre)
        analyzerFifo.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    updateBandSettings (false);

    const auto linked = stereoLink->load (std::memory_order_relaxed) >= 0.5f;
    auto* const* channels = buffer.getArrayOfWritePointers();

    for (int start = 0; start < numSamples; start += coefficientUpdateInterval)
    {
        const auto length = juce::jmin (coefficientUpdateInterval, numSamples - start);

        for (auto& band : bands)
            band.process (channels, numChannels, start, length, linked);
    }

    if (outputGain.isSmoothing())
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const auto gain = outputGain.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][n] *= gain;
        }
    }
    else if (! juce::exactlyEqual (outputGain.getTargetValue(), 1.0f))
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply (channels[ch], outputGain.getTargetValue(), numSamples);
    }

    if (analyse && ! analysePre)
        analyzerFifo.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
}

void DooblrQAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Zero latency, so bypass is an exact pass-through of the input.
    juce::ignoreUnused (midiMessages);
    clearOutputOnlyChannels (buffer);

    // Keep the analyzer moving while bypassed (input and output are identical here).
    const auto numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(),
                                         dsp::DynamicEQBand::maxChannels);

    if (analyzerRunning.load (std::memory_order_relaxed) && numChannels > 0 && buffer.getNumSamples() > 0)
        analyzerFifo.push (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* DooblrQAudioProcessor::createEditor()
{
    return new DooblrQAudioProcessorEditor (*this);
}

bool DooblrQAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String DooblrQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DooblrQAudioProcessor::acceptsMidi() const   { return false; }
bool DooblrQAudioProcessor::producesMidi() const  { return false; }
bool DooblrQAudioProcessor::isMidiEffect() const  { return false; }

double DooblrQAudioProcessor::getTailLengthSeconds() const
{
    // Narrow, low-frequency IIR bands ring briefly after the input stops.
    return 0.5;
}

//==============================================================================
// Some hosts misbehave if a plugin reports zero programs, so expose a single one.
int DooblrQAudioProcessor::getNumPrograms()                                 { return 1; }
int DooblrQAudioProcessor::getCurrentProgram()                              { return 0; }
void DooblrQAudioProcessor::setCurrentProgram (int)                         {}
const juce::String DooblrQAudioProcessor::getProgramName (int)              { return {}; }
void DooblrQAudioProcessor::changeProgramName (int, const juce::String&)    {}

//==============================================================================
void DooblrQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, params::stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DooblrQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // Newer, unknown state versions are still loaded: parameters are matched by ID,
    // and IDs this version does not know are ignored by the value tree state.
    parameters.replaceState (state);
}

//==============================================================================
// Entry point used by the JUCE plugin wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrQAudioProcessor();
}
