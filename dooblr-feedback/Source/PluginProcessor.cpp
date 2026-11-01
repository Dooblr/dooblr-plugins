#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrfeedback
{
    using namespace params;

    struct DooblrFeedbackProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid)
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            detect = get (id::detect); ringOut = get (id::ringOut); mode = get (id::mode); sensitivity = get (id::sensitivity);
            sidechain = get (id::sidechain); rangeLow = get (id::rangeLow); rangeHigh = get (id::rangeHigh);
            maxCut = get (id::maxCut); notchQ = get (id::notchQ); liveNotches = get (id::liveNotches); release = get (id::release);
            output = get (id::output); bypass = get (id::bypass);
        }

        std::atomic<float>* detect, * ringOut, * mode, * sensitivity, * sidechain, * rangeLow, * rangeHigh, * maxCut, * notchQ,
            * liveNotches, * release, * output, * bypass;
    };

    DooblrFeedbackProcessor::DooblrFeedbackProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                    .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
          state (*this, nullptr, "DooblrFeedback", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        engine.prepare (44100.0, maxChunk);
        setLatencySamples (0);
    }

    DooblrFeedbackProcessor::~DooblrFeedbackProcessor() = default;

    bool DooblrFeedbackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        if (in != out || (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()))
            return false;
        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
                return false;
        }
        return true;
    }

    bool DooblrFeedbackProcessor::isSidechainConnected() const
    {
        const auto* bus = getBus (true, 1);
        return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
    }

    dsp::FeedbackEngine::Settings DooblrFeedbackProcessor::readSettings() const
    {
        dsp::FeedbackEngine::Settings s;
        s.detect = raw->detect->load() > 0.5f;
        s.ringOut = raw->ringOut->load() > 0.5f;
        s.aggressive = raw->mode->load() > 0.5f;
        s.hold = raw->liveNotches->load() > 0.5f;
        s.sensitivity = raw->sensitivity->load() / 100.0f;
        s.maxCutDb = raw->maxCut->load();
        s.baseQ = raw->notchQ->load();
        s.releaseSec = raw->release->load();
        s.lowHz = raw->rangeLow->load();
        s.highHz = raw->rangeHigh->load();
        return s;
    }

    void DooblrFeedbackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.prepare (sampleRate, std::min (samplesPerBlock, maxChunk));
        engine.setSettings (readSettings());
        dryBuffer.setSize (2, maxChunk, false, true, false);
        detectorBuffer.setSize (1, maxChunk, false, true, false);
        outGain.reset (sampleRate, 0.02);
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));
        bypassMix.reset (sampleRate, 0.02);
        bypassMix.setCurrentAndTargetValue (raw->bypass->load() > 0.5f ? 1.0f : 0.0f);
        setLatencySamples (0);
    }

    void DooblrFeedbackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        const auto bypassed = raw->bypass->load() > 0.5f;
        bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);
        outGain.setTargetValue (juce::Decibels::decibelsToGain (raw->output->load()));

        const float* sc[2] {};
        int numSc = 0;
        if (raw->sidechain->load() > 0.5f && isSidechainConnected())
        {
            auto side = getBusBuffer (buffer, true, 1);
            numSc = std::min (2, side.getNumChannels());
            for (int ch = 0; ch < numSc; ++ch) sc[ch] = side.getReadPointer (ch);
        }

        float peaks[2] { 0.0f, 0.0f };
        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const auto n = std::min (maxChunk, numSamples - start);
            float* io[2] {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                io[ch] = main.getWritePointer (ch, start);
                dryBuffer.copyFrom (ch, 0, io[ch], n);
            }

            const float* detector = nullptr;
            if (numSc > 0)
            {
                auto* d = detectorBuffer.getWritePointer (0);
                juce::FloatVectorOperations::copy (d, sc[0] + start, n);
                if (numSc > 1)
                {
                    juce::FloatVectorOperations::add (d, sc[1] + start, n);
                    juce::FloatVectorOperations::multiply (d, 0.5f, n);
                }
                detector = d;
            }

            engine.process (io, numChannels, n, detector, ! bypassed);

            if (! bypassMix.isSmoothing() && ! outGain.isSmoothing() && bypassMix.getTargetValue() == 0.0f && outGain.getTargetValue() == 1.0f)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    peaks[ch] = std::max (peaks[ch], juce::FloatVectorOperations::findMaximum (io[ch], n));
                for (int ch = 0; ch < numChannels; ++ch)
                    peaks[ch] = std::max (peaks[ch], -juce::FloatVectorOperations::findMinimum (io[ch], n));
                continue; // the notched signal as it is (bit-exact when no notch is active)
            }

            for (int i = 0; i < n; ++i)
            {
                const auto b = bypassMix.getNextValue(), g = outGain.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto y = (1.0f - b) * io[ch][i] * g + b * dryBuffer.getSample (ch, i);
                    io[ch][i] = y;
                    peaks[ch] = std::max (peaks[ch], std::abs (y));
                }
            }
        }

        peakTap.push (peaks[0], numChannels > 1 ? peaks[1] : peaks[0]);
    }

    juce::AudioProcessorParameter* DooblrFeedbackProcessor::getBypassParameter() const
    {
        return state.getParameter (id::bypass);
    }

    juce::AudioProcessorEditor* DooblrFeedbackProcessor::createEditor()
    {
        return new DooblrFeedbackEditor (*this);
    }

    //==============================================================================
    void DooblrFeedbackProcessor::setNotes (const juce::String& text)
    {
        if (text == notes) return;
        notes = text;
        roomChanged.sendChangeMessage();
    }

    bool DooblrFeedbackProcessor::clearNotch (int notchId)
    {
        const auto ok = engine.clearNotch (notchId);
        roomChanged.sendChangeMessage();
        return ok;
    }

    void DooblrFeedbackProcessor::clearAllNotches()
    {
        engine.clearAll();
        roomChanged.sendChangeMessage();
    }

    bool DooblrFeedbackProcessor::setNotchFixed (int notchId, bool fixed)
    {
        const auto ok = engine.setFixed (notchId, fixed);
        roomChanged.sendChangeMessage();
        return ok;
    }

    //==============================================================================
    int DooblrFeedbackProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrFeedbackProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        undoHistory.clear();
    }

    const juce::String DooblrFeedbackProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    void DooblrFeedbackProcessor::selectCompareSlot (int slot)
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

    void DooblrFeedbackProcessor::copyToOtherCompareSlot()
    {
        compareStore[(size_t) (1 - compareSlot)] = state.copyState();
    }

    juce::String DooblrFeedbackProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrFeedbackProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrFeedbackProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, notes, engine.getNotches(), file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrFeedbackProcessor::loadUserPreset (const juce::File& file)
    {
        juce::String loadedNotes;
        dsp::NotchArray loadedNotches {};
        const auto error = presets::load (state, file, loadedNotes, loadedNotches);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        engine.setNotches (loadedNotches);
        notes = loadedNotes;
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        roomChanged.sendChangeMessage();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    juce::String DooblrFeedbackProcessor::renameUserPreset (const juce::File& file, const juce::String& newName)
    {
        juce::String error;
        const auto renamed = presets::rename (file, newName, error);
        if (renamed == juce::File())
            return "Could not rename preset: " + error;
        if (getPresetName() == file.getFileNameWithoutExtension())
            setPresetName (renamed.getFileNameWithoutExtension());
        else
            presetChanged.sendChangeMessage();
        return "Renamed to " + renamed.getFileNameWithoutExtension();
    }

    void DooblrFeedbackProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        copy.setProperty ("analyzerSpeed", ui::analyzerSpeedName (ui::analyzerSpeedFromIndex (getAnalyzerSpeed())), nullptr);
        copy.setProperty ("notes", notes, nullptr);
        copy.removeChild (copy.getChildWithName ("NOTCHES"), nullptr);
        copy.appendChild (presets::notchesToTree (engine.getNotches()), nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrFeedbackProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("stateVersion", stateVersion, nullptr);

        auto speed = (int) ui::defaultAnalyzerSpeed;
        const auto savedSpeed = tree.getProperty ("analyzerSpeed").toString();
        for (int i = 0; i < ui::numAnalyzerSpeeds; ++i)
            if (savedSpeed == ui::analyzerSpeedName (ui::analyzerSpeedFromIndex (i)))
                speed = i;
        setAnalyzerSpeed (speed);
        tree.removeProperty ("analyzerSpeed", nullptr);

        notes = tree.getProperty ("notes").toString();
        tree.removeProperty ("notes", nullptr);
        const auto notchTree = tree.getChildWithName ("NOTCHES");
        engine.setNotches (presets::notchesFromTree (notchTree));
        tree.removeChild (notchTree, nullptr);

        state.replaceState (tree);
        undoHistory.clear();
        presetChanged.sendChangeMessage();
        roomChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrfeedback::DooblrFeedbackProcessor();
}
