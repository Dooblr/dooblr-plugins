#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrsoothe
{
    using namespace params;

    struct DooblrSootheProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            mode = get (id::mode); depth = get (id::depth); sharpness = get (id::sharpness); selectivity = get (id::selectivity);
            attack = get (id::attack); release = get (id::release); stereoMode = get (id::stereoMode); link = get (id::link);
            focus = get (id::focus); maxCut = get (id::maxCut); wetTrim = get (id::wetTrim); mix = get (id::mix);
            output = get (id::output); delta = get (id::delta); bypass = get (id::bypass); sidechain = get (id::sidechain);
            quality = get (id::quality); listenBand = get (id::listenBand);
            for (int b = 0; b < numBands; ++b)
            {
                auto& band = bands[(size_t) b];
                band.active = get (bandActive (b)); band.shape = get (bandShape (b)); band.freq = get (bandFreq (b));
                band.gain = get (bandGain (b)); band.q = get (bandQ (b));
            }
        }

        std::atomic<float>* mode, * depth, * sharpness, * selectivity, * attack, * release, * stereoMode, * link, * focus,
            * maxCut, * wetTrim, * mix, * output, * delta, * bypass, * sidechain, * quality, * listenBand;
        struct BandRaw { std::atomic<float>* active, * shape, * freq, * gain, * q; };
        std::array<BandRaw, numBands> bands {};
    };

    DooblrSootheProcessor::DooblrSootheProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                    .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
          state (*this, nullptr, "DooblrSoothe", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        engine.prepare (44100.0, (int) raw->quality->load());
        setLatencySamples (engine.getLatencySamples());
    }

    DooblrSootheProcessor::~DooblrSootheProcessor() = default;

    bool DooblrSootheProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        if (in != out || (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()))
            return false;
        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
                return false;
        }
        return true;
    }

    void DooblrSootheProcessor::prepareToPlay (double sampleRate, int)
    {
        preparedQuality = (int) raw->quality->load();
        engine.prepare (sampleRate, preparedQuality);
        setLatencySamples (engine.getLatencySamples());

        for (auto* b : { &procIn, &detIn, &wetOut, &dryOut })
            b->setSize (2, maxChunk, false, true, false);

        for (auto* s : { &dryGain, &wetGain, &outGain })
            s->reset (sampleRate, 0.02);
        const auto [dryTarget, wetTarget] = mixTargets();
        dryGain.setCurrentAndTargetValue (dryTarget);
        wetGain.setCurrentAndTargetValue (wetTarget);
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));
    }

    std::pair<float, float> DooblrSootheProcessor::mixTargets() const noexcept
    {
        // Output = dry gain x (input delayed by the latency) + wet gain x (processed):
        // normal = (1 - mix) dry + mix trim wet; Delta = dry - normal (what is removed);
        // Bypass = dry; Band Listen = the band's share of what is removed (the engine's wet).
        const auto mix = juce::jlimit (0.0f, 1.0f, raw->mix->load() * 0.01f);
        const auto trim = juce::Decibels::decibelsToGain (raw->wetTrim->load());
        if (raw->bypass->load() > 0.5f)             return { 1.0f, 0.0f };
        if ((int) raw->listenBand->load() > 0)      return { 0.0f, 1.0f };
        if (raw->delta->load() > 0.5f)              return { mix, -mix * trim };
        return { 1.0f - mix, mix * trim };
    }

    dsp::Bands DooblrSootheProcessor::readBands() const
    {
        dsp::Bands bands;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& r = raw->bands[(size_t) b];
            auto& band = bands[(size_t) b];
            band.active = r.active->load() > 0.5f;
            band.shape = (Shape) juce::jlimit (0, numShapes - 1, (int) r.shape->load());
            band.freq = r.freq->load();
            band.gain = r.gain->load();
            band.q = r.q->load();
        }
        return bands;
    }

    dsp::ResonanceEngine::Settings DooblrSootheProcessor::readSettings() const
    {
        dsp::ResonanceEngine::Settings s;
        s.depth = raw->depth->load() * 0.01f;
        s.sharpness = raw->sharpness->load();
        s.selectivity = raw->selectivity->load();
        s.attackMs = raw->attack->load();
        s.releaseMs = raw->release->load();
        s.hard = raw->mode->load() > 0.5f;
        s.link = raw->link->load() * 0.01f;
        s.focus = raw->focus->load() * 0.01f;
        s.maxCutDb = raw->maxCut->load();
        s.listenBand = (int) raw->listenBand->load() - 1;
        s.bands = readBands();
        return s;
    }

    bool DooblrSootheProcessor::isSidechainConnected() const
    {
        auto* bus = getBus (true, 1);
        return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
    }

    void DooblrSootheProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();
        auto main = getBusBuffer (buffer, true, 0);
        const auto numChannels = juce::jlimit (0, 2, std::min (main.getNumChannels(), getMainBusNumOutputChannels()));

        if (numChannels == 0 || numSamples == 0)
            return;

        const auto quality = juce::jlimit (0, 2, (int) raw->quality->load());
        if (quality != preparedQuality)
        {
            preparedQuality = quality;
            engine.setQuality (quality);
            setLatencySamples (engine.getLatencySamples());
        }

        engine.setSettings (readSettings());

        const bool midSide = numChannels == 2 && raw->stereoMode->load() > 0.5f;
        const auto scBus = getBus (true, 1);
        const float* scRead[2] {};
        int numDetector = 0;
        if (raw->sidechain->load() > 0.5f && scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0)
        {
            const auto available = std::min (2, scBus->getNumberOfChannels());
            for (int ch = 0; ch < available; ++ch)
            {
                const auto index = getChannelIndexInProcessBlockBuffer (true, 1, ch);
                if (index >= 0 && index < buffer.getNumChannels())
                    scRead[numDetector++] = buffer.getReadPointer (index);
            }
            if (numChannels == 1)
                numDetector = std::min (numDetector, 1);
        }

        const auto [dryTarget, wetTarget] = mixTargets();
        dryGain.setTargetValue (dryTarget);
        wetGain.setTargetValue (wetTarget);
        outGain.setTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));

        float peaks[2] { 0.0f, 0.0f };

        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const auto n = std::min (maxChunk, numSamples - start);
            const float* inputs[2] {};
            float* procs[2] { procIn.getWritePointer (0), procIn.getWritePointer (1) };

            for (int ch = 0; ch < numChannels; ++ch)
                inputs[ch] = main.getReadPointer (ch, start);

            if (midSide)
                for (int i = 0; i < n; ++i)
                {
                    const auto l = inputs[0][i], r = inputs[1][i];
                    procs[0][i] = 0.5f * (l + r);
                    procs[1][i] = 0.5f * (l - r);
                }
            else
                for (int ch = 0; ch < numChannels; ++ch)
                    juce::FloatVectorOperations::copy (procs[ch], inputs[ch], n);

            const float* detPtrs[2] { detIn.getReadPointer (0), detIn.getReadPointer (1) };
            if (numDetector > 0)
            {
                float* d[2] { detIn.getWritePointer (0), detIn.getWritePointer (1) };
                if (midSide && numDetector == 2)
                    for (int i = 0; i < n; ++i)
                    {
                        const auto l = scRead[0][start + i], r = scRead[1][start + i];
                        d[0][i] = 0.5f * (l + r);
                        d[1][i] = 0.5f * (l - r);
                    }
                else
                    for (int ch = 0; ch < numDetector; ++ch)
                        juce::FloatVectorOperations::copy (d[ch], scRead[ch] + start, n);
            }

            float* wet[2] { wetOut.getWritePointer (0), wetOut.getWritePointer (1) };
            float* dry[2] { dryOut.getWritePointer (0), dryOut.getWritePointer (1) };
            const float* procRead[2] { procs[0], procs[1] };
            engine.process (procRead, numDetector > 0 ? detPtrs : nullptr, numDetector, wet, dry, numChannels, n);

            if (midSide)
                for (auto* pair : { wet, dry })
                    for (int i = 0; i < n; ++i)
                    {
                        const auto m = pair[0][i], s = pair[1][i];
                        pair[0][i] = m + s;
                        pair[1][i] = m - s;
                    }

            for (int i = 0; i < n; ++i)
            {
                const auto dg = dryGain.getNextValue(), wg = wetGain.getNextValue(), og = outGain.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto y = (dg * dry[ch][i] + wg * wet[ch][i]) * og;
                    main.setSample (ch, start + i, y);
                    peaks[ch] = std::max (peaks[ch], std::abs (y));
                }
            }
        }

        peakTap.push (peaks[0], numChannels > 1 ? peaks[1] : peaks[0]);
    }

    double DooblrSootheProcessor::getTailLengthSeconds() const
    {
        return engine.getLatencySamples() / std::max (1.0, engine.getSampleRate());
    }

    juce::AudioProcessorParameter* DooblrSootheProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrSootheProcessor::createEditor()
    {
        return new DooblrSootheEditor (*this);
    }

    //==============================================================================
    int DooblrSootheProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrSootheProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrSootheProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrSootheProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrSootheProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrSootheProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrSootheProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrSootheProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrSootheProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        // stateVersion 1 is the first format; future versions migrate here before replacing.
        tree.setProperty ("stateVersion", stateVersion, nullptr);
        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrsoothe::DooblrSootheProcessor();
}
