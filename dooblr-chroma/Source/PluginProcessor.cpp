#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrchroma
{
#define DOOBLRCHROMA_PARAMETERS(X) \
    X(malletStiffness) X(malletNoise) X(malletColor) X(mixMallet) X(mixNoise) X(mixDirect) \
    X(noiseFilter) X(noiseFreq) X(noiseQ) X(envMode) X(envAttack) X(envHold) \
    X(envDecay) X(envSustain) X(envRelease) X(lfoRate) X(lfoDepth) X(unisonCount) \
    X(unisonDetune) X(vibratoRate) X(vibratoDepth) X(glide) X(macro1) X(macro2) \
    X(macro3) X(macro4) X(layerMode) X(splitNote) X(layer1Level) X(layer2Level) \
    X(layer2Transpose) X(output) X(eqLow) X(eqMid) X(eqHigh) X(eqMidFreq) \
    X(compThreshold) X(compRatio) X(compAttack) X(compRelease) X(compMix) X(distDrive) \
    X(distMix) X(chorusRate) X(chorusDepth) X(chorusMix) X(delayTime) X(delayFeedback) \
    X(delayMix) X(reverbSize) X(reverbDamp) X(reverbMix) X(l1_couple) X(l1_balance) \
    X(l2_couple) X(l2_balance) X(l1_a_on) X(l1_a_type) X(l1_a_density) X(l1_a_pitch) \
    X(l1_a_cents) X(l1_a_key) X(l1_a_pLevel) X(l1_a_pRate) X(l1_a_pVel) X(l1_a_decay) \
    X(l1_a_decayKey) X(l1_a_decayVel) X(l1_a_release) X(l1_a_material) X(l1_a_tone) X(l1_a_lowCut) \
    X(l1_a_radius) X(l1_a_hit) X(l1_a_hitVel) X(l1_a_hitKey) X(l1_a_hitRnd) X(l1_a_part1) \
    X(l1_a_part2) X(l1_a_part3) X(l1_a_part4) X(l1_b_on) X(l1_b_type) X(l1_b_density) \
    X(l1_b_pitch) X(l1_b_cents) X(l1_b_key) X(l1_b_pLevel) X(l1_b_pRate) X(l1_b_pVel) \
    X(l1_b_decay) X(l1_b_decayKey) X(l1_b_decayVel) X(l1_b_release) X(l1_b_material) X(l1_b_tone) \
    X(l1_b_lowCut) X(l1_b_radius) X(l1_b_hit) X(l1_b_hitVel) X(l1_b_hitKey) X(l1_b_hitRnd) \
    X(l1_b_part1) X(l1_b_part2) X(l1_b_part3) X(l1_b_part4) X(l2_a_on) X(l2_a_type) \
    X(l2_a_density) X(l2_a_pitch) X(l2_a_cents) X(l2_a_key) X(l2_a_pLevel) X(l2_a_pRate) \
    X(l2_a_pVel) X(l2_a_decay) X(l2_a_decayKey) X(l2_a_decayVel) X(l2_a_release) X(l2_a_material) \
    X(l2_a_tone) X(l2_a_lowCut) X(l2_a_radius) X(l2_a_hit) X(l2_a_hitVel) X(l2_a_hitKey) \
    X(l2_a_hitRnd) X(l2_a_part1) X(l2_a_part2) X(l2_a_part3) X(l2_a_part4) X(l2_b_on) \
    X(l2_b_type) X(l2_b_density) X(l2_b_pitch) X(l2_b_cents) X(l2_b_key) X(l2_b_pLevel) \
    X(l2_b_pRate) X(l2_b_pVel) X(l2_b_decay) X(l2_b_decayKey) X(l2_b_decayVel) X(l2_b_release) \
    X(l2_b_material) X(l2_b_tone) X(l2_b_lowCut) X(l2_b_radius) X(l2_b_hit) X(l2_b_hitVel) \
    X(l2_b_hitKey) X(l2_b_hitRnd) X(l2_b_part1) X(l2_b_part2) X(l2_b_part3) X(l2_b_part4)

    struct DooblrChromaProcessor::RawParameters
    {
#define DOOBLRCHROMA_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLRCHROMA_PARAMETERS (DOOBLRCHROMA_DECLARE)
#undef DOOBLRCHROMA_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLRCHROMA_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLRCHROMA_PARAMETERS (DOOBLRCHROMA_FETCH)
#undef DOOBLRCHROMA_FETCH
        }
    };

    DooblrChromaProcessor::~DooblrChromaProcessor() = default;

    DooblrChromaProcessor::DooblrChromaProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrChroma", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
    }

    bool DooblrChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
            && layouts.inputBuses.isEmpty();
    }

    void DooblrChromaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        setLatencySamples (0);
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void DooblrChromaProcessor::releaseResources()
    {
        engine.reset();
    }

    dsp::Settings DooblrChromaProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::Settings s;
        s.malletStiffness = f (r.malletStiffness);
        s.malletNoise = f (r.malletNoise);
        s.malletColor = f (r.malletColor);
        s.mixMallet = f (r.mixMallet);
        s.mixNoise = f (r.mixNoise);
        s.mixDirect = f (r.mixDirect);
        s.noiseFilter = i (r.noiseFilter);
        s.noiseFreq = f (r.noiseFreq);
        s.noiseQ = f (r.noiseQ);
        s.envMode = i (r.envMode);
        s.envAttack = f (r.envAttack);
        s.envHold = f (r.envHold);
        s.envDecay = f (r.envDecay);
        s.envSustain = f (r.envSustain);
        s.envRelease = f (r.envRelease);
        s.lfoRate = f (r.lfoRate);
        s.lfoDepth = f (r.lfoDepth);
        s.unisonDetune = f (r.unisonDetune);
        s.vibratoRate = f (r.vibratoRate);
        s.vibratoDepth = f (r.vibratoDepth);
        s.glide = f (r.glide);
        s.macro1 = f (r.macro1);
        s.macro2 = f (r.macro2);
        s.macro3 = f (r.macro3);
        s.macro4 = f (r.macro4);
        s.layerMode = i (r.layerMode);
        s.splitNote = f (r.splitNote);
        s.unisonCount = juce::jlimit (1, 4, i (r.unisonCount) + 1);
        s.outputDb = f (r.output);
        s.eqLow = f (r.eqLow); s.eqMid = f (r.eqMid); s.eqHigh = f (r.eqHigh); s.eqMidFreq = f (r.eqMidFreq);
        s.compThreshold = f (r.compThreshold); s.compRatio = f (r.compRatio);
        s.compAttack = f (r.compAttack); s.compRelease = f (r.compRelease); s.compMix = f (r.compMix);
        s.distDrive = f (r.distDrive); s.distMix = f (r.distMix);
        s.chorusRate = f (r.chorusRate); s.chorusDepth = f (r.chorusDepth); s.chorusMix = f (r.chorusMix);
        s.delayTime = f (r.delayTime); s.delayFeedback = f (r.delayFeedback); s.delayMix = f (r.delayMix);
        s.reverbSize = f (r.reverbSize); s.reverbDamp = f (r.reverbDamp); s.reverbMix = f (r.reverbMix);

        s.layers[0].couple = b (r.l1_couple);
        s.layers[0].balance = f (r.l1_balance);
        s.layers[0].level = f (r.layer1Level);
        s.layers[0].transpose = 0.0f;
        s.layers[0].a.on = b (r.l1_a_on);
        s.layers[0].a.type = (dsp::ResType) juce::jlimit (0, dsp::kNumResTypes - 1, i (r.l1_a_type));
        s.layers[0].a.density = juce::jlimit (0, 3, i (r.l1_a_density));
        s.layers[0].a.pitch = f (r.l1_a_pitch);
        s.layers[0].a.cents = f (r.l1_a_cents);
        s.layers[0].a.key = f (r.l1_a_key);
        s.layers[0].a.pLevel = f (r.l1_a_pLevel);
        s.layers[0].a.pRate = f (r.l1_a_pRate);
        s.layers[0].a.pVel = f (r.l1_a_pVel);
        s.layers[0].a.decay = f (r.l1_a_decay);
        s.layers[0].a.decayKey = f (r.l1_a_decayKey);
        s.layers[0].a.decayVel = f (r.l1_a_decayVel);
        s.layers[0].a.release = f (r.l1_a_release);
        s.layers[0].a.material = f (r.l1_a_material);
        s.layers[0].a.tone = f (r.l1_a_tone);
        s.layers[0].a.lowCut = f (r.l1_a_lowCut);
        s.layers[0].a.radius = f (r.l1_a_radius);
        s.layers[0].a.hit = f (r.l1_a_hit);
        s.layers[0].a.hitVel = f (r.l1_a_hitVel);
        s.layers[0].a.hitKey = f (r.l1_a_hitKey);
        s.layers[0].a.hitRnd = f (r.l1_a_hitRnd);
        s.layers[0].a.partials[0] = f (r.l1_a_part1);
        s.layers[0].a.partials[1] = f (r.l1_a_part2);
        s.layers[0].a.partials[2] = f (r.l1_a_part3);
        s.layers[0].a.partials[3] = f (r.l1_a_part4);
        s.layers[0].b.on = b (r.l1_b_on);
        s.layers[0].b.type = (dsp::ResType) juce::jlimit (0, dsp::kNumResTypes - 1, i (r.l1_b_type));
        s.layers[0].b.density = juce::jlimit (0, 3, i (r.l1_b_density));
        s.layers[0].b.pitch = f (r.l1_b_pitch);
        s.layers[0].b.cents = f (r.l1_b_cents);
        s.layers[0].b.key = f (r.l1_b_key);
        s.layers[0].b.pLevel = f (r.l1_b_pLevel);
        s.layers[0].b.pRate = f (r.l1_b_pRate);
        s.layers[0].b.pVel = f (r.l1_b_pVel);
        s.layers[0].b.decay = f (r.l1_b_decay);
        s.layers[0].b.decayKey = f (r.l1_b_decayKey);
        s.layers[0].b.decayVel = f (r.l1_b_decayVel);
        s.layers[0].b.release = f (r.l1_b_release);
        s.layers[0].b.material = f (r.l1_b_material);
        s.layers[0].b.tone = f (r.l1_b_tone);
        s.layers[0].b.lowCut = f (r.l1_b_lowCut);
        s.layers[0].b.radius = f (r.l1_b_radius);
        s.layers[0].b.hit = f (r.l1_b_hit);
        s.layers[0].b.hitVel = f (r.l1_b_hitVel);
        s.layers[0].b.hitKey = f (r.l1_b_hitKey);
        s.layers[0].b.hitRnd = f (r.l1_b_hitRnd);
        s.layers[0].b.partials[0] = f (r.l1_b_part1);
        s.layers[0].b.partials[1] = f (r.l1_b_part2);
        s.layers[0].b.partials[2] = f (r.l1_b_part3);
        s.layers[0].b.partials[3] = f (r.l1_b_part4);
        s.layers[1].couple = b (r.l2_couple);
        s.layers[1].balance = f (r.l2_balance);
        s.layers[1].level = f (r.layer2Level);
        s.layers[1].transpose = f (r.layer2Transpose);
        s.layers[1].a.on = b (r.l2_a_on);
        s.layers[1].a.type = (dsp::ResType) juce::jlimit (0, dsp::kNumResTypes - 1, i (r.l2_a_type));
        s.layers[1].a.density = juce::jlimit (0, 3, i (r.l2_a_density));
        s.layers[1].a.pitch = f (r.l2_a_pitch);
        s.layers[1].a.cents = f (r.l2_a_cents);
        s.layers[1].a.key = f (r.l2_a_key);
        s.layers[1].a.pLevel = f (r.l2_a_pLevel);
        s.layers[1].a.pRate = f (r.l2_a_pRate);
        s.layers[1].a.pVel = f (r.l2_a_pVel);
        s.layers[1].a.decay = f (r.l2_a_decay);
        s.layers[1].a.decayKey = f (r.l2_a_decayKey);
        s.layers[1].a.decayVel = f (r.l2_a_decayVel);
        s.layers[1].a.release = f (r.l2_a_release);
        s.layers[1].a.material = f (r.l2_a_material);
        s.layers[1].a.tone = f (r.l2_a_tone);
        s.layers[1].a.lowCut = f (r.l2_a_lowCut);
        s.layers[1].a.radius = f (r.l2_a_radius);
        s.layers[1].a.hit = f (r.l2_a_hit);
        s.layers[1].a.hitVel = f (r.l2_a_hitVel);
        s.layers[1].a.hitKey = f (r.l2_a_hitKey);
        s.layers[1].a.hitRnd = f (r.l2_a_hitRnd);
        s.layers[1].a.partials[0] = f (r.l2_a_part1);
        s.layers[1].a.partials[1] = f (r.l2_a_part2);
        s.layers[1].a.partials[2] = f (r.l2_a_part3);
        s.layers[1].a.partials[3] = f (r.l2_a_part4);
        s.layers[1].b.on = b (r.l2_b_on);
        s.layers[1].b.type = (dsp::ResType) juce::jlimit (0, dsp::kNumResTypes - 1, i (r.l2_b_type));
        s.layers[1].b.density = juce::jlimit (0, 3, i (r.l2_b_density));
        s.layers[1].b.pitch = f (r.l2_b_pitch);
        s.layers[1].b.cents = f (r.l2_b_cents);
        s.layers[1].b.key = f (r.l2_b_key);
        s.layers[1].b.pLevel = f (r.l2_b_pLevel);
        s.layers[1].b.pRate = f (r.l2_b_pRate);
        s.layers[1].b.pVel = f (r.l2_b_pVel);
        s.layers[1].b.decay = f (r.l2_b_decay);
        s.layers[1].b.decayKey = f (r.l2_b_decayKey);
        s.layers[1].b.decayVel = f (r.l2_b_decayVel);
        s.layers[1].b.release = f (r.l2_b_release);
        s.layers[1].b.material = f (r.l2_b_material);
        s.layers[1].b.tone = f (r.l2_b_tone);
        s.layers[1].b.lowCut = f (r.l2_b_lowCut);
        s.layers[1].b.radius = f (r.l2_b_radius);
        s.layers[1].b.hit = f (r.l2_b_hit);
        s.layers[1].b.hitVel = f (r.l2_b_hitVel);
        s.layers[1].b.hitKey = f (r.l2_b_hitKey);
        s.layers[1].b.hitRnd = f (r.l2_b_hitRnd);
        s.layers[1].b.partials[0] = f (r.l2_b_part1);
        s.layers[1].b.partials[1] = f (r.l2_b_part2);
        s.layers[1].b.partials[2] = f (r.l2_b_part3);
        s.layers[1].b.partials[3] = f (r.l2_b_part4);
        return s;
    }

    void DooblrChromaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        engine.setSettings (readSettings());
        engine.process (buffer, midi);
    }

    juce::AudioProcessorEditor* DooblrChromaProcessor::createEditor()
    {
        return new DooblrChromaEditor (*this);
    }

    int DooblrChromaProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrChromaProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (index < 0 || index >= (int) all.size())
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        presetChanged.sendChangeMessage();
    }

    const juce::String DooblrChromaProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        if (index < 0 || index >= (int) all.size())
            return {};
        return all[(size_t) index].name;
    }

    juce::String DooblrChromaProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName", "Init").toString();
    }

    void DooblrChromaProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrChromaProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return {};
        setPresetName (name);
        return name;
    }

    juce::String DooblrChromaProcessor::loadUserPreset (const juce::File& file)
    {
        const auto name = presets::load (state, file);
        if (name.isNotEmpty())
            setPresetName (name);
        return name;
    }

    void DooblrChromaProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrChromaProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new dooblrchroma::DooblrChromaProcessor();
}
