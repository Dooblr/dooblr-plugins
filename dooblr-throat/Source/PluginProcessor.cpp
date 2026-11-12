#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace dooblrthroat
{
#define DOOBLRTHROAT_PARAMETERS(X) \
    X(vowel) X(height) X(back) X(nasal) X(female) X(child) \
    X(breath) X(tension) X(brightness) X(fry) X(roughness) X(pressure) X(voiced) \
    X(vibratoDepth) X(vibratoRate) X(glide) X(bendRange) \
    X(attack) X(decay) X(sustain) X(release) X(output) \
    X(mode) X(rate) X(effort) X(formantTune)

    struct DooblrThroatProcessor::RawParameters
    {
#define DOOBLRTHROAT_DECLARE(name) std::atomic<float>* name = nullptr;
        DOOBLRTHROAT_PARAMETERS (DOOBLRTHROAT_DECLARE)
#undef DOOBLRTHROAT_DECLARE

        explicit RawParameters (juce::AudioProcessorValueTreeState& apvts)
        {
#define DOOBLRTHROAT_FETCH(name) name = apvts.getRawParameterValue (params::id::name); jassert (name != nullptr);
            DOOBLRTHROAT_PARAMETERS (DOOBLRTHROAT_FETCH)
#undef DOOBLRTHROAT_FETCH
        }
    };

    DooblrThroatProcessor::~DooblrThroatProcessor()
    {
        cancelPendingUpdate();
    }

    DooblrThroatProcessor::DooblrThroatProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrThroat", params::createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        state.state.setProperty (params::phraseTextProperty, "", nullptr);
        loadBundledCmudict (g2p);
        dsp::phonetics::initInventory();
        publishPhraseFromState();
    }

    bool DooblrThroatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
            && layouts.inputBuses.isEmpty();
    }

    void DooblrThroatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.setSettings (readSettings());
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void DooblrThroatProcessor::releaseResources()
    {
        engine.reset();
    }

    dsp::ThroatSettings DooblrThroatProcessor::readSettings() const
    {
        const auto& r = *raw;
        const auto f = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
        const auto i = [&f] (std::atomic<float>* p) { return juce::roundToInt (f (p)); };
        const auto b = [&f] (std::atomic<float>* p) { return f (p) >= 0.5f; };

        dsp::ThroatSettings s;
        const auto vowelChoice = juce::jlimit (0, 16, i (r.vowel));
        s.vowelIndex = vowelChoice == 0 ? -1 : vowelChoice - 1;
        s.vowelHeight = f (r.height);
        s.vowelBack = f (r.back);
        s.nasal = f (r.nasal);
        s.body.female = f (r.female);
        s.body.child = f (r.child);
        s.breathiness = f (r.breath);
        s.tension = f (r.tension);
        s.brightness = f (r.brightness);
        s.fry = f (r.fry);
        s.roughness = f (r.roughness);
        s.pressure = f (r.pressure);
        s.voiced = b (r.voiced);
        s.vibratoDepth = f (r.vibratoDepth);
        s.vibratoRate = f (r.vibratoRate);
        s.glide = f (r.glide);
        s.attack = f (r.attack);
        s.decay = f (r.decay);
        s.sustain = f (r.sustain);
        s.release = f (r.release);
        s.outputDb = f (r.output);
        s.tipFree = true;
        s.speechMode = i (r.mode);
        s.speechRate = f (r.rate);
        s.effort = f (r.effort);
        s.formantTune = f (r.formantTune);
        return s;
    }

    void DooblrThroatProcessor::publishPhraseFromState()
    {
        const auto text = state.state.getProperty (params::phraseTextProperty).toString();
        const auto settings = readSettings();
        const bool bodyChanged = std::abs (publishedBody.female - settings.body.female) > 1.0e-6f
                              || std::abs (publishedBody.child - settings.body.child) > 1.0e-6f;
        if (text == publishedText && ! bodyChanged)
            return;
        auto plan = dsp::phonetics::buildPhrase (g2p, text.toStdString(), settings.body);
        engine.publishPhrase (std::move (plan));
        publishedText = text;
        publishedBody = settings.body;
    }

    juce::String DooblrThroatProcessor::getPhraseText() const
    {
        return state.state.getProperty (params::phraseTextProperty).toString();
    }

    void DooblrThroatProcessor::setPhraseText (const juce::String& text, bool force)
    {
        state.state.setProperty (params::phraseTextProperty, text, nullptr);
        if (force)
            publishedText.clear();
        publishPhraseFromState();
        presetChanged.sendChangeMessage();
    }

    void DooblrThroatProcessor::requestPhraseRestart()
    {
        engine.requestRestart();
    }

    void DooblrThroatProcessor::applyBodyOnMessageThread()
    {
        engine.rebuildVoiceOffAudioThread (readSettings());
        publishedText.clear();
        publishPhraseFromState();
    }

    void DooblrThroatProcessor::handleAsyncUpdate()
    {
        applyBodyOnMessageThread();
    }

    void DooblrThroatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        dsp::AudioCallbackScope audioScope (engine);

        auto settings = readSettings();
        bendRangeCached = juce::jlimit (0, 24, juce::roundToInt (raw->bendRange->load (std::memory_order_relaxed)));

        const bool bodyChanged = std::abs (settings.body.female - engine.appliedFemale()) > 1.0e-6f
                              || std::abs (settings.body.child - engine.appliedChild()) > 1.0e-6f;
        if (bodyChanged)
        {
            settings.body.female = engine.appliedFemale();
            settings.body.child = engine.appliedChild();
            engine.setSettings (settings);
            triggerAsyncUpdate();
        }
        else
        {
            engine.setSettings (settings);
        }

        const auto numSamples = buffer.getNumSamples();
        const auto channels = buffer.getNumChannels();
        if (channels == 0)
            return;

        auto* left = buffer.getWritePointer (0);
        auto* right = channels > 1 ? buffer.getWritePointer (1) : left;

        auto handle = [this] (const juce::MidiMessage& m)
        {
            if (m.isNoteOn())
                engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff())
                engine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                engine.allNotesOff();
            else if (m.isPitchWheel())
            {
                const auto w = (float) m.getPitchWheelValue();
                engine.pitchBend (((w - 8192.0f) / 8192.0f) * (float) bendRangeCached);
            }
        };

        int cursor = 0;
        for (const auto metadata : midi)
        {
            const int pos = juce::jlimit (0, numSamples, metadata.samplePosition);
            if (pos > cursor)
                engine.process (left + cursor, right + cursor, pos - cursor);
            handle (metadata.getMessage());
            cursor = pos;
        }
        if (cursor < numSamples)
            engine.process (left + cursor, right + cursor, numSamples - cursor);

        for (int ch = 2; ch < channels; ++ch)
            buffer.clear (ch, 0, numSamples);

        peakTap.push (buffer.getMagnitude (0, 0, numSamples),
                      buffer.getMagnitude (std::min (1, channels - 1), 0, numSamples));
    }

    juce::AudioProcessorEditor* DooblrThroatProcessor::createEditor()
    {
        return new DooblrThroatEditor (*this);
    }

    int DooblrThroatProcessor::getNumPrograms()
    {
        return (int) presets::factory().size();
    }

    void DooblrThroatProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;

        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        applyBodyOnMessageThread();
        undoHistory.clear();
    }

    const juce::String DooblrThroatProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrThroatProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrThroatProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrThroatProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrThroatProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        setPresetName (file.getFileNameWithoutExtension());
        applyBodyOnMessageThread();
        undoHistory.clear();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrThroatProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", params::stateVersion, nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrThroatProcessor::setStateInformation (const void* data, int sizeInBytes)
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
        applyBodyOnMessageThread();
        undoHistory.clear();
        presetChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrthroat::DooblrThroatProcessor();
}
