#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrl
{
    using namespace params;

    struct DooblrLProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            threshold     = get (id::threshold);
            attack        = get (id::attack);
            release       = get (id::release);
            knee          = get (id::knee);
            enhance       = get (id::enhance);
            style         = get (id::style);
            oversampling  = get (id::oversampling);
            lookahead     = get (id::lookahead);
            transientLink = get (id::transientLink);
            releaseLink   = get (id::releaseLink);
            inputGain     = get (id::inputGain);
            ceiling       = get (id::ceiling);
            unityGain     = get (id::unityGain);
            delta         = get (id::delta);
            autoComp      = get (id::autoComp);
            dither        = get (id::dither);
            shaping       = get (id::shaping);
            lufsTarget    = get (id::lufsTarget);
            bypass        = get (id::bypass);
        }

        std::atomic<float> *threshold, *attack, *release, *knee, *enhance, *style, *oversampling,
            *lookahead, *transientLink, *releaseLink, *inputGain, *ceiling, *unityGain, *delta,
            *autoComp, *dither, *shaping, *lufsTarget, *bypass;
    };

    DooblrLProcessor::DooblrLProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrL", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        engine.setSettings (readSettings());
        engine.prepare (44100.0, maxChunk, (int) std::lround (raw->oversampling->load()));
        setLatencySamples (engine.latencySamples());
    }

    DooblrLProcessor::~DooblrLProcessor() = default;

    bool DooblrLProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }

    dsp::Settings DooblrLProcessor::readSettings() const
    {
        dsp::Settings s;
        s.thresholdDb = raw->threshold->load();
        s.attackMs = raw->attack->load();
        s.releaseMs = raw->release->load();
        s.kneeDb = raw->knee->load();
        s.enhance = raw->enhance->load();
        s.style = juce::jlimit (0, params::numStyles - 1, (int) std::lround (raw->style->load()));
        s.oversamplingIndex = juce::jlimit (0, params::numOversampling - 1, (int) std::lround (raw->oversampling->load()));
        s.lookaheadMs = raw->lookahead->load();
        s.transientLink = raw->transientLink->load() * 0.01f;
        s.releaseLink = raw->releaseLink->load() * 0.01f;
        s.inputGainDb = raw->inputGain->load();
        s.ceilingDb = raw->ceiling->load();
        s.unityGain = raw->unityGain->load() > 0.5f;
        s.delta = raw->delta->load() > 0.5f;
        s.autoComp = raw->autoComp->load() > 0.5f;
        s.dither = juce::jlimit (0, params::numDither - 1, (int) std::lround (raw->dither->load()));
        s.shaping = juce::jlimit (0, params::numShaping - 1, (int) std::lround (raw->shaping->load()));
        s.lufsTarget = juce::jlimit (0, params::numLufsTargets - 1, (int) std::lround (raw->lufsTarget->load()));
        s.bypass = raw->bypass->load() > 0.5f;
        return s;
    }

    void DooblrLProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        const auto s = readSettings();
        engine.setSettings (s);
        engine.prepare (sampleRate, std::min (samplesPerBlock, maxChunk), s.oversamplingIndex);
        setLatencySamples (engine.latencySamples());
    }

    void DooblrLProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        auto main = getBusBuffer (buffer, false, 0);
        const auto numChannels = std::min (2, main.getNumChannels());
        const auto numSamples = buffer.getNumSamples();
        for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
        if (numChannels == 0 || numSamples == 0)
            return;

        engine.setSettings (readSettings());

        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const auto n = std::min (maxChunk, numSamples - start);
            float* io[2] {};
            for (int ch = 0; ch < numChannels; ++ch)
                io[ch] = main.getWritePointer (ch, start);
            engine.process (io, numChannels, n);
        }

        const auto lat = engine.latencySamples();
        if (lat != getLatencySamples())
            setLatencySamples (lat);
    }

    juce::AudioProcessorParameter* DooblrLProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrLProcessor::createEditor()
    {
        return new DooblrLEditor (*this);
    }

    int DooblrLProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrLProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrLProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrLProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrLProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrLProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrLProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrLProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrLProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        migrateParameterTree (tree, (int) tree.getProperty ("stateVersion", 1));
        tree.setProperty ("stateVersion", stateVersion, nullptr);
        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrl::DooblrLProcessor();
}
