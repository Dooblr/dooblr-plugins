#include "PluginProcessor.h"

#include "Link/RemoteLink.h"

#include <vector>

#include "Parameters.h"
#include "PluginEditor.h"
#include "Randomiser.h"
#include "Presets.h"

namespace dooblrprophet
{
    // Every parameter, by its ID constant (params::id::NAME), cached as a raw atomic pointer.
#define DOOBLRPROPHET_PARAMETERS(X) \
    X(voices) X(voiceMode) X(unisonVoices) X(unisonDetune) X(glide) X(vintage) X(bendRange) X(atFilter) X(atLfo) X(output) \
    X(osc1Mode) X(osc1Coarse) X(osc1Fine) X(osc1Shape) X(osc1PW) X(osc1Saw) X(osc1Tri) X(osc1Pulse) X(osc1Sync) \
    X(osc2Mode) X(osc2Coarse) X(osc2Fine) X(osc2Shape) X(osc2PW) X(osc2Saw) X(osc2Tri) X(osc2Pulse) X(osc2LowFreq) X(osc2KeyTrack) \
    X(mixOsc1) X(mixOsc2) X(mixSub) X(mixNoise) \
    X(lpfCutoff) X(lpfReso) X(lpfDrive) X(lpfEnvAmt) X(lpfKeyTrack) X(hpfCutoff) X(hpfReso) X(hpfEnvAmt) \
    X(fenvAttack) X(fenvDecay) X(fenvSustain) X(fenvRelease) X(fenvVelAmt) \
    X(aenvAttack) X(aenvDecay) X(aenvSustain) X(aenvRelease) X(aenvVelAmt) \
    X(pmodFiltEnv) X(pmodOsc2) X(pmodDestFreq1) X(pmodDestShape1) X(pmodDestFilter) \
    X(lfoWave) X(lfoRate) X(lfoSync) X(lfoDivision) X(lfoInitial) X(lfoWheel) X(lfoNoiseMix) \
    X(lfoDestFreq1) X(lfoDestFreq2) X(lfoDestShape1) X(lfoDestShape2) X(lfoDestLpf) X(lfoDestHpf) X(lfoDestAmp) \
    X(lpfModel) X(fenvSync) \
    X(echoOn) X(echoPreset) X(echoMix) X(echoTime) X(echoSync) X(echoDivision) X(echoFeedback) X(echoPingPong) \
    X(reverbOn) X(reverbPreset) X(reverbMix) X(reverbSize) X(reverbTone)

    struct DooblrProphetProcessor::RawParameters
    {
#define DOOBLRPROPHET_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLRPROPHET_PARAMETERS (DOOBLRPROPHET_DECLARE)
#undef DOOBLRPROPHET_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLRPROPHET_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLRPROPHET_PARAMETERS (DOOBLRPROPHET_FETCH)
#undef DOOBLRPROPHET_FETCH
        }
    };

    DooblrProphetProcessor::DooblrProphetProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrProphet", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        remoteLink = std::make_unique<link::RemoteLink> (*this);
    }

    DooblrProphetProcessor::~DooblrProphetProcessor() = default;

    bool DooblrProphetProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.inputBuses.isEmpty();
    }

    void DooblrProphetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
        fx.prepare (sampleRate);
    }

    void DooblrProphetProcessor::releaseResources()
    {
        engine.reset();
        fx.reset();
    }

    dsp::Settings DooblrProphetProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::Settings s;
        s.voices = i (r.voices);
        s.mode = (dsp::VoiceMode) juce::jlimit (0, 2, i (r.voiceMode));
        s.unisonVoices = i (r.unisonVoices);
        s.unisonDetune = f (r.unisonDetune);
        s.glide = f (r.glide);
        s.vintage = f (r.vintage);
        s.bendRange = i (r.bendRange);
        s.atFilter = f (r.atFilter);
        s.atLfo = f (r.atLfo);
        s.outputDb = f (r.output);

        s.osc1 = { i (r.osc1Mode) == 1, i (r.osc1Coarse), f (r.osc1Fine), f (r.osc1Shape), f (r.osc1PW), b (r.osc1Saw), b (r.osc1Tri), b (r.osc1Pulse) };
        s.osc2 = { i (r.osc2Mode) == 1, i (r.osc2Coarse), f (r.osc2Fine), f (r.osc2Shape), f (r.osc2PW), b (r.osc2Saw), b (r.osc2Tri), b (r.osc2Pulse) };
        s.sync = b (r.osc1Sync);
        s.osc2LowFreq = b (r.osc2LowFreq);
        s.osc2KeyTrack = b (r.osc2KeyTrack);
        s.mixOsc1 = f (r.mixOsc1);
        s.mixOsc2 = f (r.mixOsc2);
        s.mixSub = f (r.mixSub);
        s.mixNoise = f (r.mixNoise);

        s.lpfCutoff = f (r.lpfCutoff);
        s.lpfReso = f (r.lpfReso);
        s.lpfDrive = f (r.lpfDrive);
        s.lpfEnvAmt = f (r.lpfEnvAmt);
        s.lpfKeyTrack = f (r.lpfKeyTrack);
        s.hpfCutoff = f (r.hpfCutoff);
        s.hpfReso = f (r.hpfReso);
        s.hpfEnvAmt = f (r.hpfEnvAmt);
        s.lpfModel = juce::jlimit (0, 1, i (r.lpfModel));

        s.filterEnv = { f (r.fenvAttack), f (r.fenvDecay), f (r.fenvSustain), f (r.fenvRelease), f (r.fenvVelAmt) };
        s.ampEnv = { f (r.aenvAttack), f (r.aenvDecay), f (r.aenvSustain), f (r.aenvRelease), f (r.aenvVelAmt) };
        s.filterEnvSync = b (r.fenvSync);

        s.pmodFiltEnv = f (r.pmodFiltEnv);
        s.pmodOsc2 = f (r.pmodOsc2);
        s.pmodFreq1 = b (r.pmodDestFreq1);
        s.pmodShape1 = b (r.pmodDestShape1);
        s.pmodFilter = b (r.pmodDestFilter);

        s.lfoWave = (dsp::LfoWave) juce::jlimit (0, 4, i (r.lfoWave));
        s.lfoRate = f (r.lfoRate);
        s.lfoSync = b (r.lfoSync);
        s.lfoSyncQuarters = params::lfoDivisionQuarters (i (r.lfoDivision));
        s.lfoInitial = f (r.lfoInitial);
        s.lfoWheel = f (r.lfoWheel);
        s.lfoNoiseMix = f (r.lfoNoiseMix);
        s.lfoFreq1 = b (r.lfoDestFreq1);
        s.lfoFreq2 = b (r.lfoDestFreq2);
        s.lfoShape1 = b (r.lfoDestShape1);
        s.lfoShape2 = b (r.lfoDestShape2);
        s.lfoLpf = b (r.lfoDestLpf);
        s.lfoHpf = b (r.lfoDestHpf);
        s.lfoAmp = b (r.lfoDestAmp);

        s.fx.echoOn = b (r.echoOn);
        s.fx.echoPreset = juce::jlimit (0, dsp::numEchoPresets - 1, i (r.echoPreset));
        s.fx.echoMix = f (r.echoMix);
        s.fx.echoTimeMs = f (r.echoTime);
        s.fx.echoSync = b (r.echoSync);
        s.fx.echoDivision = i (r.echoDivision);
        s.fx.echoFeedback = f (r.echoFeedback);
        s.fx.echoPingPong = b (r.echoPingPong);
        s.fx.reverbOn = b (r.reverbOn);
        s.fx.reverbPreset = juce::jlimit (0, dsp::numReverbPresets - 1, i (r.reverbPreset));
        s.fx.reverbMix = f (r.reverbMix);
        s.fx.reverbSize = f (r.reverbSize);
        s.fx.reverbTone = f (r.reverbTone);
        return s;
    }

    void DooblrProphetProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        // The Max for Live device's pairing token (three channel-16 CCs): a lock-free scan.
        remoteLink->getPairing().scan (midi);

        const auto settings = readSettings();
        engine.setSettings (settings);

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

        if (channels > 1)
        {
            engine.process (left, right, numSamples, midi, time);
        }
        else
        {
            // Mono bus: render into the one channel (the engine's output is mono-compatible).
            engine.process (left, left, numSamples, midi, time);
        }

        // The global FX stage: Echo, then Reverb (a bypass when both are off).
        hostBpm.store (time.bpm, std::memory_order_relaxed);
        fx.process (left, right, numSamples, settings.fx, time.bpm);

        for (int ch = 2; ch < channels; ++ch)
            buffer.clear (ch, 0, numSamples);

        const auto peakLeft = buffer.getMagnitude (0, 0, numSamples);
        const auto peakRight = buffer.getMagnitude (std::min (1, channels - 1), 0, numSamples);
        peakTap.push (peakLeft, peakRight);
        remoteLink->getPeakTap().push (peakLeft, peakRight); // the device's meter: its own consumer
    }

    juce::AudioProcessorEditor* DooblrProphetProcessor::createEditor()
    {
        return new DooblrProphetEditor (*this);
    }

    int DooblrProphetProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrProphetProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrProphetProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrProphetProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrProphetProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrProphetProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    int DooblrProphetProcessor::randomisePatch (juce::Random& random)
    {
        // The name change joins the parameters' undo step, so one undo brings back both.
        struct NameAction final : juce::UndoableAction
        {
            NameAction (DooblrProphetProcessor& o, juce::String b, juce::String a) : owner (o), before (std::move (b)), after (std::move (a)) {}
            bool perform() override { owner.setPresetName (after); return true; }
            bool undo() override { owner.setPresetName (before); return true; }
            int getSizeInUnits() override { return 1; }
            DooblrProphetProcessor& owner;
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

    void DooblrProphetProcessor::applyFxPreset (int module, int index)
    {
        namespace id = params::id;
        std::vector<std::pair<const char*, float>> values;

        if (module == 0)
        {
            index = juce::jlimit (0, dsp::numEchoPresets - 1, index);
            const auto& e = dsp::echoPresets()[(size_t) index];
            values = { { id::echoPreset, (float) index }, { id::echoMix, e.mix }, { id::echoTime, e.timeMs }, { id::echoSync, e.sync ? 1.0f : 0.0f },
                       { id::echoDivision, (float) e.division }, { id::echoFeedback, e.feedback }, { id::echoPingPong, e.pingPong ? 1.0f : 0.0f } };
        }
        else
        {
            index = juce::jlimit (0, dsp::numReverbPresets - 1, index);
            const auto& v = dsp::reverbPresets()[(size_t) index];
            values = { { id::reverbPreset, (float) index }, { id::reverbMix, v.mix }, { id::reverbSize, v.size }, { id::reverbTone, v.tone } };
        }

        // Nested gestures: the undo history records one step for the whole preset.
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;
        for (const auto& [pid, value] : values)
            if (auto* param = state.getParameter (pid))
                changes.emplace_back (param, param->convertTo0to1 (value));

        for (auto& c : changes) c.first->beginChangeGesture();
        for (auto& c : changes) c.first->setValueNotifyingHost (c.second);
        for (auto& c : changes) c.first->endChangeGesture();
    }

    juce::String DooblrProphetProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrProphetProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrProphetProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        // Parameters added after the state was saved (e.g. Filter Env Sync in 0.1.4) take their
        // defaults instead of keeping whatever the current patch had.
        for (auto* p : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! tree.getChildWithProperty ("id", ranged->paramID).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", ranged->paramID, nullptr);
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                    tree.appendChild (child, nullptr);
                }
        // stateVersion 1 is the first format; future versions migrate here before replacing.
        tree.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrprophet::DooblrProphetProcessor();
}
