#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "Randomiser.h"

namespace dooblrbass
{
#define DOOBLRBASS_PARAMETERS(X) \
    X(osc1Wave) X(osc1Octave) X(osc1PW) \
    X(osc2Wave) X(osc2Octave) X(osc2Tune) X(osc2PW) \
    X(osc3Wave) X(osc3Octave) X(osc3Tune) X(osc3LoFreq) X(osc3KeyTrack) \
    X(subOctave) \
    X(mixOsc1) X(mixOsc2) X(mixOsc3) X(mixSub) X(mixNoise) X(mixDrive) X(mixOverload) \
    X(lpfCutoff) X(lpfReso) X(lpfEnvAmt) X(lpfKeyTrack) \
    X(fenvAttack) X(fenvDecay) X(fenvSustain) X(fenvRelease) X(fenvVelAmt) \
    X(aenvAttack) X(aenvDecay) X(aenvSustain) X(aenvRelease) X(sharedDecay) \
    X(glide) X(legato) X(notePriority) X(bendRange) X(output) \
    X(lfoWave) X(lfoRate) X(lfoSync) X(lfoDivision) X(lfoToPitch) X(lfoToFilter) X(lfoWheel)

    struct DooblrBassProcessor::RawParameters
    {
#define DOOBLRBASS_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLRBASS_PARAMETERS (DOOBLRBASS_DECLARE)
#undef DOOBLRBASS_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLRBASS_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLRBASS_PARAMETERS (DOOBLRBASS_FETCH)
#undef DOOBLRBASS_FETCH
        }
    };

    DooblrBassProcessor::~DooblrBassProcessor() = default;

    DooblrBassProcessor::DooblrBassProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrBass", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
    }

    bool DooblrBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.inputBuses.isEmpty();
    }

    void DooblrBassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void DooblrBassProcessor::releaseResources()
    {
        engine.reset();
    }

    dsp::Settings DooblrBassProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::Settings s;
        s.osc1Wave = (dsp::Wave) juce::jlimit (0, 1, i (r.osc1Wave));
        s.osc2Wave = (dsp::Wave) juce::jlimit (0, 1, i (r.osc2Wave));
        s.osc3Wave = (dsp::Osc3Wave) juce::jlimit (0, 2, i (r.osc3Wave));
        s.osc1Octave = juce::jlimit (0, 3, i (r.osc1Octave));
        s.osc2Octave = juce::jlimit (0, 3, i (r.osc2Octave));
        s.osc3Octave = juce::jlimit (0, 3, i (r.osc3Octave));
        s.osc1PW = f (r.osc1PW);
        s.osc2PW = f (r.osc2PW);
        s.osc2Tune = f (r.osc2Tune);
        s.osc3Tune = f (r.osc3Tune);
        s.osc3LoFreq = b (r.osc3LoFreq);
        s.osc3KeyTrack = b (r.osc3KeyTrack);
        s.subOctave = juce::jlimit (0, 1, i (r.subOctave));

        s.mixOsc1 = f (r.mixOsc1);
        s.mixOsc2 = f (r.mixOsc2);
        s.mixOsc3 = f (r.mixOsc3);
        s.mixSub = f (r.mixSub);
        s.mixNoise = f (r.mixNoise);
        s.mixDrive = f (r.mixDrive);
        s.mixOverload = f (r.mixOverload);

        s.lpfCutoff = f (r.lpfCutoff);
        s.lpfReso = f (r.lpfReso);
        s.lpfEnvAmt = f (r.lpfEnvAmt);
        s.lpfKeyTrack = f (r.lpfKeyTrack);

        s.filterEnv = { f (r.fenvAttack), f (r.fenvDecay), f (r.fenvSustain), f (r.fenvRelease), f (r.fenvVelAmt) };
        s.ampEnv = { f (r.aenvAttack), f (r.aenvDecay), f (r.aenvSustain), f (r.aenvRelease), 0.0f };
        s.sharedDecay = b (r.sharedDecay);

        s.glide = f (r.glide);
        s.legato = b (r.legato);
        s.notePriority = (dsp::NotePriority) juce::jlimit (0, 1, i (r.notePriority));
        s.bendRange = i (r.bendRange);
        s.outputDb = f (r.output);

        s.lfoWave = (dsp::LfoWave) juce::jlimit (0, 4, i (r.lfoWave));
        s.lfoRate = f (r.lfoRate);
        s.lfoSync = b (r.lfoSync);
        s.lfoSyncQuarters = params::lfoDivisionQuarters (i (r.lfoDivision));
        s.lfoToPitch = f (r.lfoToPitch);
        s.lfoToFilter = f (r.lfoToFilter);
        s.lfoWheel = f (r.lfoWheel);
        return s;
    }

    void DooblrBassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    juce::AudioProcessorEditor* DooblrBassProcessor::createEditor()
    {
        return new DooblrBassEditor (*this);
    }

    int DooblrBassProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrBassProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrBassProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrBassProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrBassProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrBassProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrBassProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    int DooblrBassProcessor::randomisePatch (juce::Random& random)
    {
        struct NameAction final : juce::UndoableAction
        {
            NameAction (DooblrBassProcessor& o, juce::String b, juce::String a)
                : owner (o), before (std::move (b)), after (std::move (a)) {}
            bool perform() override { owner.setPresetName (after); return true; }
            bool undo() override { owner.setPresetName (before); return true; }
            int getSizeInUnits() override { return 1; }
            DooblrBassProcessor& owner;
            juce::String before, after;
        };

        const auto previousName = getPresetName();
        const auto changed = randomiser::apply (state, random);

        if (changed > 0)
            undoHistory.appendToLastTransaction (std::make_unique<NameAction> (*this, previousName, "Random"));
        else
            setPresetName ("Random");

        return changed;
    }

    void DooblrBassProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrBassProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new dooblrbass::DooblrBassProcessor();
}
