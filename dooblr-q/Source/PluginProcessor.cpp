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
        p.slope           = pointer (params::bandId (b, params::band::slope));
        p.model           = pointer (params::bandId (b, params::band::model));
        p.stepped         = pointer (params::bandId (b, params::band::stepped));
        p.drive           = pointer (params::bandId (b, params::band::drive));
        p.atten           = pointer (params::bandId (b, params::band::atten));
    }

    outputGainDb = pointer (params::outputGain);
    stereoLink   = pointer (params::stereoLink);
    phaseMode    = pointer (params::phaseMode);
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
    s.slope           = (dsp::Slope) juce::jlimit (0, dsp::numSlopes - 1, juce::roundToInt (load (p.slope)));
    s.model           = (dsp::Model) juce::jlimit (0, dsp::numModels - 1, juce::roundToInt (load (p.model)));
    s.driveDb         = load (p.drive);
    s.attenDb         = dsp::usesAtten (s.model, s.type) ? load (p.atten) : 0.0f;

    // Stepped: switched models snap to their documented positions (DSP, curve and handles).
    if (load (p.stepped) >= 0.5f && dsp::isAnalog (s.model))
    {
        s.frequency = dsp::snapFrequency (s.model, s.type, s.frequency);
        s.gainDb    = dsp::snapGain (s.model, s.type, s.gainDb);
    }
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

    // Natural / Linear phase resources, sized for the longest mode at this rate.
    firEngine.prepare (sampleRate);
    detectionScratch.setSize (2, firChunkSize);
    dryScratch.setSize (2, firChunkSize);
    const auto maxLatency = latencyFor (params::PhaseMode::linearPhase, sampleRate);
    for (auto& ring : dryRing)
        ring.assign ((size_t) maxLatency + 1, 0.0f);
    dryRingPosition = 0;

    applyPhaseMode (juce::roundToInt (phaseMode->load()), false);
}

int DooblrQAudioProcessor::latencyFor (params::PhaseMode mode, double sampleRate) noexcept
{
    using Engine = dsp::FirEqEngine;

    switch (mode)
    {
        case params::PhaseMode::naturalPhase: return Engine::latencySamples (Engine::Mode::natural, sampleRate);
        case params::PhaseMode::linearPhase:  return Engine::latencySamples (Engine::Mode::linear, sampleRate);
        case params::PhaseMode::zeroLatency:  break;
    }

    return 0;
}

void DooblrQAudioProcessor::applyPhaseMode (int mode, bool fadeIn) noexcept
{
    mode = juce::jlimit (0, 2, mode);
    activePhaseMode.store (mode, std::memory_order_relaxed);
    const auto phase = (params::PhaseMode) mode;
    const auto rate = getAnalysisSampleRate();

    if (phase != params::PhaseMode::zeroLatency)
    {
        firEngine.setMode (phase == params::PhaseMode::linearPhase ? dsp::FirEqEngine::Mode::linear
                                                                   : dsp::FirEqEngine::Mode::natural);
        updateKernel (true);
    }

    for (auto& ring : dryRing)
        std::fill (ring.begin(), ring.end(), 0.0f);
    dryRingPosition = 0;

    // Hosts are told about the new latency; JUCE forwards this to the host asynchronously.
    const auto latency = latencyFor (phase, rate);
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    switchFadeRemaining = fadeIn ? switchFadeLength : 0;
}

void DooblrQAudioProcessor::updateKernel (bool immediate) noexcept
{
    std::array<dsp::KernelBand, params::numBands> target {};

    for (int b = 0; b < params::numBands; ++b)
    {
        const auto s = getBandSettings (b);
        auto& k = target[(size_t) b];
        k.enabled = s.enabled;
        k.type = s.type;
        k.frequency = (double) s.frequency;
        k.q = (double) s.q;
        k.slope = s.slope;
        k.model = s.model;
        k.attenDb = (double) s.attenDb;

        // Dynamics: the band's detector runs on a copy of the input (see processFirBlock).
        // With stereo link off, the louder channel's gain is used for both.
        auto gain = (double) s.gainDb;
        if (s.dynamicsEnabled && dsp::supportsDynamics (s.type))
        {
            const auto dynamicDb = (double) bands[(size_t) b].getDynamicGainDb();

            if (dsp::isCut (s.type)) // 0.9.0: dynamic cuts move their cutoff
                k.frequency = dsp::DynamicEQBand::dynamicCutFrequency (s.type, k.frequency, dynamicDb);
            else
                gain += dynamicDb;
        }
        k.gainDb = juce::jlimit (-(double) dsp::DynamicEQBand::maxTotalGainDb, (double) dsp::DynamicEQBand::maxTotalGainDb, gain);
    }

    const auto same = [] (const dsp::KernelBand& a, const dsp::KernelBand& b)
    {
        if (a.enabled != b.enabled)
            return false;
        if (! a.enabled)
            return true;
        return a.type == b.type && a.slope == b.slope && a.model == b.model && std::abs (a.attenDb - b.attenDb) < 0.01
               && std::abs (a.gainDb - b.gainDb) < 0.01
               && std::abs (std::log (a.frequency / b.frequency)) < 1.0e-5 && std::abs (std::log (a.q / b.q)) < 1.0e-5;
    };

    auto changed = false;
    for (size_t b = 0; b < target.size(); ++b)
        changed = changed || ! same (target[b], appliedKernel[b]);

    if (! immediate && (! changed || samplesSinceKernel < kernelUpdateInterval * dsp::FirEqEngine::rateFactor (getAnalysisSampleRate())))
        return;

    firEngine.setTarget (target, immediate);
    appliedKernel = target;
    samplesSinceKernel = 0;
}

void DooblrQAudioProcessor::processFirBlock (float* const* channels, int numChannels, int numSamples,
                                             bool linked, bool outputDry) noexcept
{
    const auto latency = getLatencySamples();
    const auto ringSize = (int) dryRing[0].size();

    for (int start = 0; start < numSamples; start += firChunkSize)
    {
        const auto length = juce::jmin (firChunkSize, numSamples - start);
        float* chunk[2] { channels[0] + start, numChannels > 1 ? channels[1] + start : nullptr };

        // 1. Dynamics: run the band detectors (the zero-latency filters) on a copy of the
        //    input, so each band's dynamic gain is measured exactly as in Zero Latency.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            detectionScratch.copyFrom (ch, 0, chunk[ch], length);
            dryScratch.copyFrom (ch, 0, chunk[ch], length);
        }

        auto* const* detection = detectionScratch.getArrayOfWritePointers();
        for (int sub = 0; sub < length; sub += coefficientUpdateInterval)
            for (auto& band : bands)
                band.process (detection, numChannels, sub, juce::jmin (coefficientUpdateInterval, length - sub), linked, false);

        // 2. Rebuild the kernel if the target moved (rate-limited, crossfaded).
        samplesSinceKernel += length;
        updateKernel (false);

        // 3. Dry path delayed by the latency (used when bypassed, kept aligned always).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* ring = dryRing[(size_t) ch].data();
            auto* dry = dryScratch.getWritePointer (ch);
            auto position = dryRingPosition;

            for (int i = 0; i < length; ++i)
            {
                ring[position] = dry[i];
                dry[i] = ring[(position - latency + ringSize) % ringSize];
                position = (position + 1) % ringSize;
            }
        }

        dryRingPosition = (dryRingPosition + length) % ringSize;

        // 4. FIR convolution, per channel with the shared kernel.
        firEngine.process (chunk, numChannels, length);

        if (outputDry)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (chunk[ch], dryScratch.getReadPointer (ch), length);
        }
        else
        {
            // 5. Model colour (0.8.0): the nonlinear stage of each analogue band, in band order,
            //    on the EQ'd output (its linear shape is already in the kernel). No latency added.
            for (auto& band : bands)
                band.processColour (chunk, numChannels, 0, length);
        }
    }
}

void DooblrQAudioProcessor::applySwitchFade (float* const* channels, int numChannels, int numSamples) noexcept
{
    // After a phase-mode switch the output jumps by the latency difference; fade it in.
    for (int i = 0; i < numSamples && switchFadeRemaining > 0; ++i, --switchFadeRemaining)
    {
        const auto gain = 1.0f - (float) switchFadeRemaining / (float) switchFadeLength;

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

void DooblrQAudioProcessor::releaseResources()
{
}

void DooblrQAudioProcessor::reset()
{
    for (auto& band : bands)
        band.reset();

    firEngine.reset();

    for (auto& ring : dryRing)
        std::fill (ring.begin(), ring.end(), 0.0f);
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

    if (const auto requested = juce::roundToInt (phaseMode->load (std::memory_order_relaxed));
        requested != activePhaseMode.load (std::memory_order_relaxed))
        applyPhaseMode (requested, true);

    if (activePhaseMode.load (std::memory_order_relaxed) == (int) params::PhaseMode::zeroLatency)
    {
        for (int start = 0; start < numSamples; start += coefficientUpdateInterval)
        {
            const auto length = juce::jmin (coefficientUpdateInterval, numSamples - start);

            for (auto& band : bands)
                band.process (channels, numChannels, start, length, linked);
        }
    }
    else
    {
        processFirBlock (channels, numChannels, numSamples, linked, false);
    }

    applySwitchFade (channels, numChannels, numSamples);

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
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    clearOutputOnlyChannels (buffer);

    const auto numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(),
                                         dsp::DynamicEQBand::maxChannels);

    if (const auto requested = juce::roundToInt (phaseMode->load (std::memory_order_relaxed));
        requested != activePhaseMode.load (std::memory_order_relaxed))
        applyPhaseMode (requested, false);

    // Zero Latency: bypass is an exact pass-through of the input. Natural/Linear phase: the
    // input is delayed by the reported latency, so bypass stays time-aligned, and the EQ keeps
    // running underneath so un-bypassing does not start from stale state.
    if (activePhaseMode.load (std::memory_order_relaxed) != (int) params::PhaseMode::zeroLatency
        && numChannels > 0 && buffer.getNumSamples() > 0)
    {
        updateBandSettings (false);
        processFirBlock (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples(),
                         stereoLink->load (std::memory_order_relaxed) >= 0.5f, true);
    }

    // Keep the analyzer moving while bypassed.

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

    // Saved parameters this version does not have (e.g. the per-band stereo bN_width that
    // only existed in 0.4.0) are dropped, so they are neither applied nor saved again.
    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto child = state.getChild (i);

        if (child.hasType ("PARAM") && parameters.getParameter (child.getProperty ("id").toString()) == nullptr)
            state.removeChild (i, nullptr);
    }

    // Parameters missing from the saved state (added in a later version, e.g. phase mode in
    // 0.4.0) are reset to their defaults rather than keeping whatever this instance had
    // before, so an old session sounds the way it was saved.
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

    // Newer, unknown state versions are still loaded: parameters are matched by ID,
    // and IDs this version does not know are ignored by the value tree state.
    parameters.replaceState (state);
    undoHistory.clear(); // a loaded state starts a fresh history
}

//==============================================================================
// Entry point used by the JUCE plugin wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DooblrQAudioProcessor();
}
