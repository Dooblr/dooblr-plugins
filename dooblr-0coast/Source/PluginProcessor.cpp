#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblr0coast
{
#define DOOBLR0COAST_PARAMETERS(X) \
    X(pitch) X(fine) X(portamento) X(linFm) \
    X(overtone) X(overtoneCv) X(multiply) X(multiplyCv) \
    X(slopeCycle) X(slopeRise) X(slopeFall) X(slopeResponse) \
    X(contourOnset) X(contourSustain) X(contourDecay) X(contourResponse) \
    X(balance) X(balanceCv) X(dynamics) X(level) \
    X(arpOn) X(arpType) X(legato) X(midiBCv) X(midiBGate) X(clockDiv) \
    X(mathAmt) X(midiBLfoRate) X(bendRange) X(output)

    struct Dooblr0CoastProcessor::RawParameters
    {
#define DOOBLR0COAST_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLR0COAST_PARAMETERS (DOOBLR0COAST_DECLARE)
#undef DOOBLR0COAST_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLR0COAST_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLR0COAST_PARAMETERS (DOOBLR0COAST_FETCH)
#undef DOOBLR0COAST_FETCH
        }
    };

    Dooblr0CoastProcessor::~Dooblr0CoastProcessor() = default;

    Dooblr0CoastProcessor::Dooblr0CoastProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "Dooblr0Coast", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
    }

    bool Dooblr0CoastProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.inputBuses.isEmpty();
    }

    void Dooblr0CoastProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void Dooblr0CoastProcessor::releaseResources()
    {
        engine.reset();
    }

    dsp::Settings Dooblr0CoastProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::Settings s;
        s.pitch = f (r.pitch);
        s.fine = f (r.fine);
        s.portamento = f (r.portamento);
        s.linFm = f (r.linFm);
        s.overtone = f (r.overtone);
        s.overtoneCv = f (r.overtoneCv);
        s.multiply = f (r.multiply);
        s.multiplyCv = f (r.multiplyCv);
        s.slopeCycle = b (r.slopeCycle);
        s.slopeRise = f (r.slopeRise);
        s.slopeFall = f (r.slopeFall);
        s.slopeResponse = f (r.slopeResponse);
        s.contourOnset = f (r.contourOnset);
        s.contourSustain = f (r.contourSustain);
        s.contourDecay = f (r.contourDecay);
        s.contourResponse = f (r.contourResponse);
        s.balance = f (r.balance);
        s.balanceCv = f (r.balanceCv);
        s.dynamics = f (r.dynamics);
        s.level = f (r.level);
        s.arpOn = b (r.arpOn);
        s.arpType = juce::jlimit (0, 1, i (r.arpType));
        s.legato = b (r.legato);
        s.midiBCv = juce::jlimit (0, 3, i (r.midiBCv));
        s.midiBGate = juce::jlimit (0, 3, i (r.midiBGate));
        s.clockDiv = juce::jlimit (0, 4, i (r.clockDiv));
        s.mathAmt = f (r.mathAmt);
        s.midiBLfoRate = f (r.midiBLfoRate);
        s.bendRange = juce::jlimit (0, 12, i (r.bendRange));
        s.outputDb = f (r.output);
        return s;
    }

    void Dooblr0CoastProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
        hostBpm.store (time.bpm, std::memory_order_relaxed);

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

    juce::AudioProcessorEditor* Dooblr0CoastProcessor::createEditor()
    {
        return new Dooblr0CoastEditor (*this);
    }

    int Dooblr0CoastProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void Dooblr0CoastProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        engine.clearCables();
        for (const auto& [from, to] : all[(size_t) index].cables)
            engine.connect ((dsp::OutJack) from, (dsp::InJack) to);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
        cablesChanged.sendChangeMessage();
    }

    const juce::String Dooblr0CoastProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String Dooblr0CoastProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void Dooblr0CoastProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String Dooblr0CoastProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name, engine.cablesToTree()))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String Dooblr0CoastProcessor::loadUserPreset (const juce::File& file)
    {
        juce::ValueTree cables ("CABLES");
        const auto error = presets::load (state, file, cables);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        engine.cablesFromTree (cables);
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        cablesChanged.sendChangeMessage();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void Dooblr0CoastProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        const auto existing = copy.getChildWithName ("CABLES");
        if (existing.isValid())
            copy.removeChild (existing, nullptr);
        copy.appendChild (engine.cablesToTree(), nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void Dooblr0CoastProcessor::setStateInformation (const void* data, int sizeInBytes)
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
        engine.cablesFromTree (tree);
        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
        cablesChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblr0coast::Dooblr0CoastProcessor();
}
