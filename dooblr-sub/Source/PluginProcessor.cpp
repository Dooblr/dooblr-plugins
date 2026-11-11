#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrsub
{
    using namespace params;

    struct DooblrSubProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            subAmount = get (id::subAmount);
            loAmount = get (id::loAmount);
            frequency = get (id::frequency);
            octaveMode = get (id::octaveMode);
            trackingSpeed = get (id::trackingSpeed);
            drive = get (id::drive);
            mix = get (id::mix);
            output = get (id::output);
            shift = get (id::shift);
            transient = get (id::transient);
            bodyMix = get (id::bodyMix);
            shiftMix = get (id::shiftMix);
            monoBelow = get (id::monoBelow);
            monoFreq = get (id::monoFreq);
            infraHpf = get (id::infraHpf);
            infraFreq = get (id::infraFreq);
            subLimit = get (id::subLimit);
            target = get (id::target);
            bypass = get (id::bypass);
        }

        std::atomic<float> *subAmount, *loAmount, *frequency, *octaveMode, *trackingSpeed, *drive, *mix, *output,
            *shift, *transient, *bodyMix, *shiftMix, *monoBelow, *monoFreq, *infraHpf, *infraFreq, *subLimit,
            *target, *bypass;
    };

    DooblrSubProcessor::DooblrSubProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrSub", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        engine.prepare (44100.0, dsp::SubEngine::maxChunk);
        setLatencySamples (engine.getLatencySamples());
    }

    DooblrSubProcessor::~DooblrSubProcessor() = default;

    bool DooblrSubProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }

    dsp::EngineSettings DooblrSubProcessor::readSettings() const
    {
        dsp::EngineSettings s;
        s.sub.subAmount = raw->subAmount->load() * 0.01f;
        s.sub.loAmount = raw->loAmount->load() * 0.01f;
        s.sub.frequencyHz = raw->frequency->load();
        s.sub.octaveMode = juce::jlimit (0, 2, (int) std::lround (raw->octaveMode->load()));
        s.sub.trackingSpeed = raw->trackingSpeed->load() * 0.01f;
        s.sub.drive = raw->drive->load() * 0.01f;
        s.tone.cents = raw->shift->load();
        s.tone.transientPreserve = raw->transient->load() * 0.01f;
        s.tone.bodyMix = raw->bodyMix->load() * 0.01f;
        s.tone.mix = raw->shiftMix->load() * 0.01f;
        s.monoBelow = raw->monoBelow->load() > 0.5f;
        s.monoHz = raw->monoFreq->load();
        s.infraHpf = raw->infraHpf->load() > 0.5f;
        s.infraHz = raw->infraFreq->load();
        s.subLimit = raw->subLimit->load() > 0.5f;
        s.target = (dsp::Target) juce::jlimit (0, 2, (int) std::lround (raw->target->load()));
        s.mix = raw->mix->load() * 0.01f;
        s.outputDb = raw->output->load();
        s.bypass = raw->bypass->load() > 0.5f;
        return s;
    }

    void DooblrSubProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.prepare (sampleRate, samplesPerBlock);
        engine.setSettings (readSettings());
        setLatencySamples (engine.getLatencySamples());
    }

    void DooblrSubProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        float* l = main.getWritePointer (0);
        float* r = numChannels > 1 ? main.getWritePointer (1) : nullptr;
        engine.process (l, r, numSamples);
    }

    juce::AudioProcessorParameter* DooblrSubProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrSubProcessor::createEditor()
    {
        return new DooblrSubEditor (*this);
    }

    int DooblrSubProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrSubProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrSubProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrSubProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrSubProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrSubProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrSubProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrSubProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrSubProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new dooblrsub::DooblrSubProcessor();
}
