#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrdistressor
{
    using namespace params;

    struct DooblrDistressorProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            input = get (id::input);
            attack = get (id::attack);
            release = get (id::release);
            output = get (id::output);
            ratio = get (id::ratio);
            audioHp = get (id::audioHp);
            distMode = get (id::distMode);
            detHp = get (id::detHp);
            detBand = get (id::detBand);
            stereoLink = get (id::stereoLink);
            headroom = get (id::headroom);
            mix = get (id::mix);
            bypass = get (id::bypass);
        }

        std::atomic<float> *input, *attack, *release, *output, *ratio, *audioHp, *distMode,
            *detHp, *detBand, *stereoLink, *headroom, *mix, *bypass;
    };

    DooblrDistressorProcessor::DooblrDistressorProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrDistressor", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        engine.prepare (44100.0, maxChunk);
        setLatencySamples (0);
    }

    DooblrDistressorProcessor::~DooblrDistressorProcessor() = default;

    bool DooblrDistressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }

    dsp::Settings DooblrDistressorProcessor::readSettings() const
    {
        dsp::Settings s;
        s.inputKnob = raw->input->load();
        s.attackKnob = raw->attack->load();
        s.releaseKnob = raw->release->load();
        s.outputKnob = raw->output->load();
        s.ratio = juce::jlimit (0, params::numRatios - 1, (int) std::lround (raw->ratio->load()));
        s.audioHp = raw->audioHp->load() > 0.5f;
        s.dist = (dsp::DistMode) juce::jlimit (0, 2, (int) std::lround (raw->distMode->load()));
        s.detHp = raw->detHp->load() > 0.5f;
        s.detBand = raw->detBand->load() > 0.5f;
        s.stereoLink = raw->stereoLink->load() > 0.5f;
        s.headroomDb = (float) headroomDbForIndex ((int) std::lround (raw->headroom->load()));
        s.mix = raw->mix->load() * 0.01f;
        s.bypass = raw->bypass->load() > 0.5f;
        return s;
    }

    void DooblrDistressorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.prepare (sampleRate, std::min (samplesPerBlock, maxChunk));
        engine.setSettings (readSettings());
        setLatencySamples (0);
    }

    void DooblrDistressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    }

    juce::AudioProcessorParameter* DooblrDistressorProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrDistressorProcessor::createEditor()
    {
        return new DooblrDistressorEditor (*this);
    }

    int DooblrDistressorProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrDistressorProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrDistressorProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrDistressorProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrDistressorProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrDistressorProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrDistressorProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrDistressorProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrDistressorProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new dooblrdistressor::DooblrDistressorProcessor();
}
