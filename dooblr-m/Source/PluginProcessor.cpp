#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrm
{
#define DOOBLRM_PARAMETERS(X) \
    X(tune) X(glide) X(modMix) X(osc3Control) X(oscMod) X(filterMod) \
    X(osc1Range) X(osc1Wave) X(osc2Range) X(osc2Wave) X(osc2Freq) \
    X(osc3Range) X(osc3Wave) X(osc3Freq) X(osc3Lfo) X(lfoRate) X(lfoSync) X(lfoDivision) X(lfoRetrigger) \
    X(mixOsc1) X(mixOsc1On) X(mixOsc2) X(mixOsc2On) X(mixOsc3) X(mixOsc3On) \
    X(mixExt) X(mixExtOn) X(mixNoise) X(mixNoiseOn) X(noiseType) \
    X(cutoff) X(emphasis) X(contourAmt) X(fAttack) X(fDecay) X(fSustain) X(kybd1) X(kybd2) \
    X(aAttack) X(aDecay) X(aSustain) X(decaySwitch) \
    X(volume) \
    X(notePriority) X(legato) X(bendRange) X(velocityOn) X(output) \
    X(modWheel) X(pitchBend)

    struct DooblrMProcessor::RawParameters
    {
#define DOOBLRM_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLRM_PARAMETERS (DOOBLRM_DECLARE)
#undef DOOBLRM_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLRM_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLRM_PARAMETERS (DOOBLRM_FETCH)
#undef DOOBLRM_FETCH
        }
    };

    DooblrMProcessor::~DooblrMProcessor() = default;

    DooblrMProcessor::DooblrMProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrM", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
    }

    bool DooblrMProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
            && layouts.inputBuses.isEmpty();
    }

    void DooblrMProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void DooblrMProcessor::releaseResources()
    {
        engine.reset();
    }

    dsp::Settings DooblrMProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::Settings s;
        s.tune = f (r.tune);
        s.glide = f (r.glide);
        s.modMix = f (r.modMix);
        s.osc3Control = b (r.osc3Control);
        s.oscMod = b (r.oscMod);
        s.filterMod = b (r.filterMod);

        s.osc1Range = juce::jlimit (0, 5, i (r.osc1Range));
        s.osc1Wave = juce::jlimit (0, 5, i (r.osc1Wave));
        s.osc2Range = juce::jlimit (0, 5, i (r.osc2Range));
        s.osc2Wave = juce::jlimit (0, 5, i (r.osc2Wave));
        s.osc2Freq = f (r.osc2Freq);
        s.osc3Range = juce::jlimit (0, 5, i (r.osc3Range));
        s.osc3Wave = juce::jlimit (0, 5, i (r.osc3Wave));
        s.osc3Freq = f (r.osc3Freq);
        s.osc3Lfo = b (r.osc3Lfo);
        s.lfoRate = f (r.lfoRate);
        s.lfoSync = b (r.lfoSync);
        s.lfoDivision = juce::jlimit (0, 5, i (r.lfoDivision));
        s.lfoRetrigger = b (r.lfoRetrigger);

        s.mixOsc1 = f (r.mixOsc1); s.mixOsc1On = b (r.mixOsc1On);
        s.mixOsc2 = f (r.mixOsc2); s.mixOsc2On = b (r.mixOsc2On);
        s.mixOsc3 = f (r.mixOsc3); s.mixOsc3On = b (r.mixOsc3On);
        s.mixExt  = f (r.mixExt);  s.mixExtOn  = b (r.mixExtOn);
        s.mixNoise = f (r.mixNoise); s.mixNoiseOn = b (r.mixNoiseOn);
        s.noiseType = juce::jlimit (0, 1, i (r.noiseType));

        s.cutoff = f (r.cutoff);
        s.emphasis = f (r.emphasis);
        s.contourAmt = f (r.contourAmt);
        s.filterEnv = { f (r.fAttack), f (r.fDecay), f (r.fSustain), b (r.decaySwitch) };
        s.kybd1 = b (r.kybd1);
        s.kybd2 = b (r.kybd2);

        s.ampEnv = { f (r.aAttack), f (r.aDecay), f (r.aSustain), b (r.decaySwitch) };

        s.volume = f (r.volume);

        s.notePriority = juce::jlimit (0, 1, i (r.notePriority));
        s.legato = b (r.legato);
        s.bendRange = juce::jlimit (0, 12, i (r.bendRange));
        s.velocityOn = b (r.velocityOn);
        s.outputDb = f (r.output);

        s.modWheel = f (r.modWheel);
        s.pitchBend = f (r.pitchBend);
        return s;
    }

    void DooblrMProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        engine.setSettings (readSettings());

        dsp::HostTime time;
        if (auto* head = getPlayHead())
        {
            if (const auto position = head->getPosition())
            {
                if (const auto bpm = position->getBpm())
                    time.bpm = *bpm;
                if (const auto ppq = position->getPpqPosition())
                {
                    time.ppq = *ppq;
                    time.hasPpq = true;
                }
                time.playing = position->getIsPlaying();
            }
        }

        const auto numSamples = buffer.getNumSamples();
        const auto channels = buffer.getNumChannels();
        if (channels == 0)
            return;

        auto* left = buffer.getWritePointer (0);
        auto* right = channels > 1 ? buffer.getWritePointer (1) : left;
        engine.process (left, right, numSamples, midi, time);

        for (int ch = 2; ch < channels; ++ch)
            buffer.clear (ch, 0, numSamples);

        peakTap.push (buffer.getMagnitude (0, 0, numSamples),
                      buffer.getMagnitude (std::min (1, channels - 1), 0, numSamples));
    }

    juce::AudioProcessorEditor* DooblrMProcessor::createEditor()
    {
        return new DooblrMEditor (*this);
    }

    int DooblrMProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrMProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrMProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrMProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrMProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrMProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrMProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrMProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrMProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        for (auto* p : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! tree.getChildWithProperty ("id", ranged->paramID).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", ranged->paramID, nullptr);
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                    tree.appendChild (child, nullptr);
                }
        tree.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrm::DooblrMProcessor();
}
