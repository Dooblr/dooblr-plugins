#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrreverb
{
    using namespace params;

    struct DooblrReverbProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            space = get (id::space); decayRate = get (id::decayRate); brightness = get (id::brightness);
            character = get (id::character); distance = get (id::distance); width = get (id::width); mix = get (id::mix);
            predelay = get (id::predelay); predelaySync = get (id::predelaySync); predelayDiv = get (id::predelayDiv);
            input = get (id::input); output = get (id::output); bypass = get (id::bypass);
            for (int b = 0; b < numDecayBands; ++b)
                decay[(size_t) b] = { get (decayActive (b)), get (decayShape (b)), get (decayFreq (b)), get (decayRateId (b)), get (decayQ (b)) };
            for (int b = 0; b < numEqBands; ++b)
                eq[(size_t) b] = { get (eqActive (b)), get (eqShape (b)), get (eqFreq (b)), get (eqGain (b)), get (eqQ (b)), get (eqSlope (b)) };
        }

        std::atomic<float>* space, * decayRate, * brightness, * character, * distance, * width, * mix, * predelay,
            * predelaySync, * predelayDiv, * input, * output, * bypass;
        struct DecayRaw { std::atomic<float>* active, * shape, * freq, * rate, * q; };
        struct EqRaw { std::atomic<float>* active, * shape, * freq, * gain, * q, * slope; };
        std::array<DecayRaw, numDecayBands> decay {};
        std::array<EqRaw, numEqBands> eq {};
    };

    DooblrReverbProcessor::DooblrReverbProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrReverb", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        setLatencySamples (0);
    }

    DooblrReverbProcessor::~DooblrReverbProcessor() = default;

    bool DooblrReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;
        return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
    }

    void DooblrReverbProcessor::prepareToPlay (double sampleRate, int)
    {
        engine.prepare (sampleRate, maxChunk);
        engine.setSettings (readSettings());
        for (auto* b : { &dryIn, &original, &wet })
            b->setSize (2, maxChunk, false, true, false);

        for (auto* s : { &inGain, &outGain, &dryGain, &wetGain })
            s->reset (sampleRate, 0.02);
        bypassMix.reset (sampleRate, 0.01);
        const auto mix = juce::jlimit (0.0f, 1.0f, raw->mix->load() * 0.01f);
        inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw->input->load()));
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));
        dryGain.setCurrentAndTargetValue (1.0f - mix);
        wetGain.setCurrentAndTargetValue (mix);
        bypassMix.setCurrentAndTargetValue (raw->bypass->load() > 0.5f ? 1.0f : 0.0f);
        setLatencySamples (0);
    }

    dsp::DecayBands DooblrReverbProcessor::readDecayBands() const
    {
        dsp::DecayBands bands;
        for (int b = 0; b < numDecayBands; ++b)
        {
            const auto& r = raw->decay[(size_t) b];
            auto& band = bands[(size_t) b];
            band.active = r.active->load() > 0.5f;
            band.shape = (DecayShape) juce::jlimit (0, numDecayShapes - 1, (int) r.shape->load());
            band.freq = r.freq->load();
            band.rate = r.rate->load();
            band.q = r.q->load();
        }
        return bands;
    }

    dsp::EqBands DooblrReverbProcessor::readEqBands() const
    {
        dsp::EqBands bands;
        for (int b = 0; b < numEqBands; ++b)
        {
            const auto& r = raw->eq[(size_t) b];
            auto& band = bands[(size_t) b];
            band.active = r.active->load() > 0.5f;
            band.shape = (EqShape) juce::jlimit (0, numEqShapes - 1, (int) r.shape->load());
            band.freq = r.freq->load();
            band.gain = r.gain->load();
            band.q = r.q->load();
            band.slope = juce::jlimit (0, 2, (int) r.slope->load());
        }
        return bands;
    }

    double DooblrReverbProcessor::currentPredelayMs() const
    {
        if (raw->predelaySync->load() > 0.5f)
        {
            const auto beats = divisionBeats ((int) raw->predelayDiv->load());
            return juce::jlimit (0.0, (double) maxPredelayMs, beats * 60000.0 / juce::jlimit (20.0, 999.0, getHostBpm()));
        }
        return raw->predelay->load();
    }

    dsp::ReverbEngine::Settings DooblrReverbProcessor::readSettings() const
    {
        dsp::ReverbEngine::Settings s;
        s.space = raw->space->load();
        s.decayRate = raw->decayRate->load();
        s.brightness = raw->brightness->load();
        s.character = raw->character->load();
        s.distance = raw->distance->load();
        s.width = raw->width->load();
        s.predelayMs = (float) currentPredelayMs();
        s.decay = readDecayBands();
        s.eq = readEqBands();
        return s;
    }

    void DooblrReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();
        const auto numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
        for (auto ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);
        const auto numChannels = juce::jlimit (0, 2, std::min (buffer.getNumChannels(), numOut));
        if (numChannels == 0 || numSamples == 0)
            return;
        const auto monoIn = numIn < 2 || buffer.getNumChannels() < 2;

        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
                if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                    hostBpm.store (*bpm, std::memory_order_relaxed);

        engine.setSettings (readSettings());

        const auto mix = juce::jlimit (0.0f, 1.0f, raw->mix->load() * 0.01f);
        inGain.setTargetValue (juce::Decibels::decibelsToGain (raw->input->load()));
        outGain.setTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));
        dryGain.setTargetValue (1.0f - mix);
        wetGain.setTargetValue (mix);
        bypassMix.setTargetValue (raw->bypass->load() > 0.5f ? 1.0f : 0.0f);
        const auto analyzer = getAnalyzerMode();

        float peaks[2] { 0.0f, 0.0f };

        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const auto n = std::min (maxChunk, numSamples - start);
            const auto* srcL = buffer.getReadPointer (0, start);
            const auto* srcR = monoIn ? srcL : buffer.getReadPointer (1, start);

            auto* origL = original.getWritePointer (0);
            auto* origR = original.getWritePointer (1);
            auto* dl = dryIn.getWritePointer (0);
            auto* dr = dryIn.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                origL[i] = srcL[i];
                origR[i] = srcR[i];
                const auto g = inGain.getNextValue();
                dl[i] = srcL[i] * g;
                dr[i] = srcR[i] * g;
            }

            auto* wl = wet.getWritePointer (0);
            auto* wr = wet.getWritePointer (1);
            engine.process (dl, dr, wl, wr, n);

            if (analyzer > 0)
            {
                for (int i = 0; i < n; ++i)
                    monoScratch[(size_t) i] = 0.5f * (wl[i] + wr[i]);
                postTap.push (monoScratch.data(), n);
                if (analyzer > 1)
                {
                    for (int i = 0; i < n; ++i)
                        monoScratch[(size_t) i] = 0.5f * (dl[i] + dr[i]);
                    preTap.push (monoScratch.data(), n);
                }
            }

            for (int i = 0; i < n; ++i)
            {
                const auto dg = dryGain.getNextValue(), wg = wetGain.getNextValue(), og = outGain.getNextValue();
                const auto bp = bypassMix.getNextValue();
                auto l = (dg * dl[i] + wg * wl[i]) * og;
                auto r = (dg * dr[i] + wg * wr[i]) * og;
                l += bp * (origL[i] - l);
                r += bp * (origR[i] - r);
                if (numChannels == 1)
                {
                    const auto m = monoIn ? 0.5f * (l + r) : l;
                    buffer.setSample (0, start + i, m);
                    peaks[0] = std::max (peaks[0], std::abs (m));
                }
                else
                {
                    buffer.setSample (0, start + i, l);
                    buffer.setSample (1, start + i, r);
                    peaks[0] = std::max (peaks[0], std::abs (l));
                    peaks[1] = std::max (peaks[1], std::abs (r));
                }
            }
        }

        peakTap.push (peaks[0], numChannels > 1 ? peaks[1] : peaks[0]);
    }

    double DooblrReverbProcessor::getTailLengthSeconds() const
    {
        return juce::jlimit (0.5, 60.0, engine.getLongestDecaySeconds() * 1.2 + maxPredelayMs * 0.001);
    }

    juce::AudioProcessorParameter* DooblrReverbProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrReverbProcessor::createEditor()
    {
        return new DooblrReverbEditor (*this);
    }

    //==============================================================================
    int DooblrReverbProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrReverbProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrReverbProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    void DooblrReverbProcessor::selectCompareSlot (int slot)
    {
        slot = juce::jlimit (0, 1, slot);
        if (slot == compareSlot)
            return;

        compareStore[(size_t) compareSlot] = state.copyState();
        if (! compareStore[(size_t) slot].isValid())
            compareStore[(size_t) slot] = compareStore[(size_t) compareSlot].createCopy();

        compareSlot = slot;
        state.replaceState (compareStore[(size_t) slot].createCopy());
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }

    void DooblrReverbProcessor::copyToOtherCompareSlot()
    {
        compareStore[(size_t) (1 - compareSlot)] = state.copyState();
    }

    juce::String DooblrReverbProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrReverbProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrReverbProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrReverbProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        // A display preference, not a parameter: saved with the session only.
        copy.setProperty ("analyzer", analyzerModeNames()[getAnalyzerMode()], nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("stateVersion", stateVersion, nullptr);

        // Analyzer mode by name, so the order can change; older sessions get the default.
        const auto savedMode = analyzerModeNames().indexOf (tree.getProperty ("analyzer").toString());
        setAnalyzerMode (savedMode >= 0 ? savedMode : 1);
        tree.removeProperty ("analyzer", nullptr);

        state.replaceState (tree);

        // replaceState only notifies parameters whose stored value changed. A switch set by a host
        // to an in-between value (e.g. 0.4, which reads as off and is stored as 0) would keep it,
        // so push every stored value back explicitly.
        for (auto* param : getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const auto child = state.state.getChildWithProperty ("id", p->paramID);
                if (! child.isValid() || ! child.hasProperty ("value"))
                    continue;
                const auto normalised = p->convertTo0to1 ((float) (double) child.getProperty ("value"));
                if (std::abs (p->getValue() - normalised) > 1.0e-7f)
                    p->setValueNotifyingHost (normalised);
            }

        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrreverb::DooblrReverbProcessor();
}
