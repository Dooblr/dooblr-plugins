#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace dooblrmeter
{
    class DooblrMeterProcessor::AnalysisThread final : public juce::Thread
    {
    public:
        explicit AnalysisThread (DooblrMeterProcessor& p) : juce::Thread ("dooblr-meter analysis"), owner (p) {}

        void run() override
        {
            while (! threadShouldExit())
            {
                owner.pumpAnalysis (false);
                wait (33);
            }
        }

    private:
        DooblrMeterProcessor& owner;
    };

    DooblrMeterProcessor::DooblrMeterProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "dooblr-meter", params::createLayout())
    {
        listenParam = state.getRawParameterValue (params::id::listen);
        monoParam = state.getRawParameterValue (params::id::monoAudition);
        matchParam = state.getRawParameterValue (params::id::listenMatch);
        state.state.setProperty ("stateVersion", params::stateVersion, nullptr);
        thread = std::make_unique<AnalysisThread> (*this);
    }

    DooblrMeterProcessor::~DooblrMeterProcessor()
    {
        thread->stopThread (2000);
    }

    bool DooblrMeterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }

    void DooblrMeterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        thread->stopThread (2000);
        analyzer.setConsumerRunning (false);
        analyzer.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
        audition.prepare (sampleRate, samplesPerBlock);
        wet.setSize (2, std::max (1, samplesPerBlock), false, true, true);
        mixStep = 1.0f / (float) std::max (1.0, 0.02 * sampleRate); // 20 ms crossfades
        mix = 0.0f;
        runningProfile = -1;
        runningMono = false;
        activeProfile.store (-1);
        setLatencySamples (0);
        if (threadEnabled)
        {
            analyzer.setConsumerRunning (true);
            thread->startThread (juce::Thread::Priority::low);
        }
    }

    void DooblrMeterProcessor::releaseResources()
    {
        thread->stopThread (2000);
        analyzer.setConsumerRunning (false);
    }

    dsp::Analyzer::Settings DooblrMeterProcessor::readSettings() const
    {
        const auto v = [this] (const char* pid) { return state.getRawParameterValue (pid)->load(); };
        dsp::Analyzer::Settings s;
        s.target = juce::roundToInt (v (params::id::target));
        s.curve = juce::roundToInt (v (params::id::curve));
        s.songMode = juce::roundToInt (v (params::id::mode)) == 1;
        s.customLufs = v (params::id::customLufs);
        s.customCeiling = v (params::id::customTp);
        s.historySpan = juce::roundToInt (v (params::id::historySpan));
        s.averaging = juce::roundToInt (v (params::id::averaging));
        s.sensitivity = juce::roundToInt (v (params::id::sensitivity));
        s.freeze = v (params::id::freeze) > 0.5f;
        return s;
    }

    void DooblrMeterProcessor::pumpAnalysis (bool forceFull)
    {
        analyzer.setSettings (readSettings());
        analyzer.pump (forceFull);
    }

    void DooblrMeterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        const auto numChannels = std::min (buffer.getNumChannels(), getTotalNumInputChannels());
        const auto n = buffer.getNumSamples();
        if (numChannels <= 0 || n <= 0)
            return;

        juce::int64 songSample = -1;
        if (auto* head = getPlayHead())
        {
            if (const auto pos = head->getPosition())
            {
                if (pos->getIsPlaying())
                    if (const auto t = pos->getTimeInSamples(); t.hasValue() && *t >= 0)
                        songSample = *t;
                if (const auto bpm = pos->getBpm())
                {
                    double beats = 4.0;
                    if (const auto sig = pos->getTimeSignature(); sig.hasValue() && sig->denominator > 0)
                        beats = sig->numerator * 4.0 / sig->denominator;
                    analyzer.setTempo (*bpm, beats);
                }
            }
        }

        // Analysis sees the mix exactly as it arrives (before any audition).
        const float* left = buffer.getReadPointer (0);
        const float* right = numChannels > 1 ? buffer.getReadPointer (1) : nullptr;
        analyzer.processAudio (left, right, n, songSample, isNonRealtime());

        // Auditions. Off (the default, and always after loading) = the buffer is not touched.
        const int wantProfile = juce::roundToInt (listenParam->load()) - 1;
        const bool wantMono = monoParam->load() > 0.5f;
        const bool levelMatch = matchParam->load() > 0.5f;

        if (mix <= 0.0f && wantProfile < 0 && ! wantMono && runningProfile < 0 && ! runningMono)
        {
            activeProfile.store (-1);
            return; // bit-exact pass-through
        }

        if (wet.getNumSamples() < n)
            wet.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            wet.copyFrom (ch, 0, buffer, std::min (ch, numChannels - 1), 0, n);

        const bool changing = wantProfile != runningProfile || wantMono != runningMono;
        if (changing && mix <= 0.0f)
        {
            runningProfile = wantProfile;
            runningMono = wantMono;
            audition.setProfile (runningProfile);
        }
        const bool engaged = runningProfile >= 0 || runningMono;
        const float targetMix = (changing || ! engaged) ? 0.0f : 1.0f;

        if (runningProfile >= 0)
            audition.process (wet.getWritePointer (0), wet.getWritePointer (1), n, levelMatch);
        if (runningMono)
        {
            auto* l = wet.getWritePointer (0);
            auto* r = wet.getWritePointer (1);
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = 0.5f * (l[i] + r[i]);
        }

        for (int i = 0; i < n; ++i)
        {
            mix = mix < targetMix ? std::min (targetMix, mix + mixStep) : std::max (targetMix, mix - mixStep);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                const auto w = wet.getSample (std::min (ch, 1), i);
                out[i] = out[i] + mix * (w - out[i]);
            }
        }

        if (mix <= 0.0f && ! engaged)
            audition.setProfile (-1);
        activeProfile.store (mix > 0.0f ? runningProfile : -1);
        matchGainDb.store (audition.getMatchGainDb());
    }

    juce::AudioProcessorEditor* DooblrMeterProcessor::createEditor()
    {
        return new DooblrMeterEditor (*this);
    }

    void DooblrMeterProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        tree.setProperty ("stateVersion", params::stateVersion, nullptr);
        tree.removeChild (tree.getChildWithName ("analysis"), nullptr);
        juce::ValueTree analysis ("analysis");
        const auto data = analyzer.saveAnalysis();
        analysis.setProperty ("data", data.toBase64Encoding(), nullptr);
        tree.appendChild (analysis, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrMeterProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        auto tree = juce::ValueTree::fromXml (*xml);
        const auto analysis = tree.getChildWithName ("analysis");
        tree.removeChild (analysis, nullptr);
        state.replaceState (tree);

        // The auditions always load Off, whatever was saved.
        for (const auto* pid : { params::id::listen, params::id::monoAudition })
            if (auto* p = state.getParameter (pid))
                p->setValueNotifyingHost (p->getDefaultValue());

        if (analysis.isValid())
        {
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (analysis["data"].toString()))
                analyzer.loadAnalysis (block);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrmeter::DooblrMeterProcessor();
}
