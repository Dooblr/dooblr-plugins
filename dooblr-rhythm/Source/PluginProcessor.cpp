#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace dooblrrhythm
{
    using namespace params;

    static int wrapPositive (int v, int n)
    {
        if (n <= 0) return 0;
        v %= n;
        return v < 0 ? v + n : v;
    }

    static uint32_t mix32 (uint32_t a, uint32_t b, uint32_t c)
    {
        a ^= b + 0x9e3779b9u + (a << 6) + (a >> 2);
        a ^= c + 0x9e3779b9u + (a << 6) + (a >> 2);
        return a;
    }

    struct DooblrRhythmProcessor::RawParameters
    {
        explicit RawParameters (juce::AudioProcessorValueTreeState& s)
        {
            const auto get = [&s] (const juce::String& pid) -> std::atomic<float>*
            {
                auto* v = s.getRawParameterValue (pid);
                jassert (v != nullptr);
                return v;
            };
            style = get (id::style);
            patternLength = get (id::patternLength);
            timeSignature = get (id::timeSignature);
            claveDirection = get (id::claveDirection);
            complexity = get (id::complexity);
            syncopation = get (id::syncopation);
            humanize = get (id::humanize);
            swing = get (id::swing);
            pushPull = get (id::pushPull);
            section = get (id::section);
            output = get (id::output);

            for (int i = 0; i < numInstruments; ++i)
            {
                density[i]     = get (instrumentParam (i, "density"));
                instComplex[i] = get (instrumentParam (i, "complexity"));
                instSync[i]    = get (instrumentParam (i, "syncopation"));
                velocity[i]    = get (instrumentParam (i, "velocity"));
                instHuman[i]   = get (instrumentParam (i, "humanize"));
                timing[i]      = get (instrumentParam (i, "timing"));
                volume[i]      = get (instrumentParam (i, "volume"));
                pan[i]         = get (instrumentParam (i, "pan"));
                mute[i]        = get (instrumentParam (i, "mute"));
                solo[i]        = get (instrumentParam (i, "solo"));
                lock[i]        = get (instrumentParam (i, "lock"));
                pitch[i]       = get (instrumentParam (i, "pitch"));
                decay[i]       = get (instrumentParam (i, "decay"));
            }
        }

        std::atomic<float> *style, *patternLength, *timeSignature, *claveDirection, *complexity,
            *syncopation, *humanize, *swing, *pushPull, *section, *output;
        std::atomic<float> *density[numInstruments], *instComplex[numInstruments], *instSync[numInstruments],
            *velocity[numInstruments], *instHuman[numInstruments], *timing[numInstruments],
            *volume[numInstruments], *pan[numInstruments], *mute[numInstruments], *solo[numInstruments],
            *lock[numInstruments], *pitch[numInstruments], *decay[numInstruments];
    };

    DooblrRhythmProcessor::DooblrRhythmProcessor()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "DooblrRhythm", createLayout()),
          undoHistory (*this),
          raw (std::make_unique<RawParameters> (state))
    {
        state.state.setProperty ("stateVersion", stateVersion, nullptr);
        state.state.setProperty ("presetName", presets::factory().front().name, nullptr);
        state.state.setProperty ("seed", 1, nullptr);
        kit.prepare (44100.0, 512);
        setPattern (generateArrangement (1));
    }

    DooblrRhythmProcessor::~DooblrRhythmProcessor() = default;

    bool DooblrRhythmProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }

    GenerateRequest DooblrRhythmProcessor::readRequest() const
    {
        GenerateRequest r;
        r.style = (Style) juce::jlimit (0, (int) Style::count - 1, (int) std::lround (raw->style->load()));
        r.timeSignature = (TimeSignature) juce::jlimit (0, (int) TimeSignature::count - 1,
                                                        (int) std::lround (raw->timeSignature->load()));
        r.clave = (ClaveDirection) juce::jlimit (0, 1, (int) std::lround (raw->claveDirection->load()));
        r.section = (Section) juce::jlimit (0, (int) Section::count - 1, (int) std::lround (raw->section->load()));
        r.bars = juce::jlimit (minBars, maxBars, (int) std::lround (raw->patternLength->load()));
        r.bpm = (float) hostBpmAtomic.load();
        r.seed = seedAtomic.load();
        r.complexity = raw->complexity->load();
        r.syncopation = raw->syncopation->load();
        r.humanize = raw->humanize->load();
        r.swing = raw->swing->load();
        r.pushPull = raw->pushPull->load();
        for (int i = 0; i < numInstruments; ++i)
        {
            auto& c = r.instruments[(size_t) i];
            c.density = raw->density[i]->load();
            c.complexity = raw->instComplex[i]->load();
            c.syncopation = raw->instSync[i]->load();
            c.velocity = raw->velocity[i]->load();
            c.humanize = raw->instHuman[i]->load();
            c.timingOffset = raw->timing[i]->load();
            c.volumeDb = raw->volume[i]->load();
            c.pan = raw->pan[i]->load();
            c.mute = raw->mute[i]->load() > 0.5f;
            c.solo = raw->solo[i]->load() > 0.5f;
            c.locked = raw->lock[i]->load() > 0.5f;
            c.pitch = raw->pitch[i]->load();
            c.decay = raw->decay[i]->load();
        }
        return r;
    }

    dsp::KitSettings DooblrRhythmProcessor::readKitSettings() const
    {
        dsp::KitSettings s;
        s.outputGain = std::pow (10.0f, raw->output->load() * 0.05f);
        s.anySolo = false;
        for (int i = 0; i < numInstruments; ++i)
        {
            auto& c = s.instruments[(size_t) i];
            c.volumeDb = raw->volume[i]->load();
            c.pan = raw->pan[i]->load();
            c.mute = raw->mute[i]->load() > 0.5f;
            c.solo = raw->solo[i]->load() > 0.5f;
            c.pitch = raw->pitch[i]->load();
            c.decay = raw->decay[i]->load();
            c.velocity = raw->velocity[i]->load();
            s.anySolo = s.anySolo || c.solo;
        }
        return s;
    }

    uint32_t DooblrRhythmProcessor::currentSeed() const { return seedAtomic.load(); }

    juce::File DooblrRhythmProcessor::tempFolder()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-rhythm");
        d.createDirectory();
        return d;
    }

    void DooblrRhythmProcessor::storePatternInState (const Pattern& p)
    {
        auto existing = state.state.getChildWithName ("Pattern");
        if (existing.isValid())
            state.state.removeChild (existing, nullptr);
        state.state.appendChild (p.toTree(), nullptr);
        state.state.setProperty ("seed", (int) p.seed, nullptr);
    }

    void DooblrRhythmProcessor::restorePatternFromState()
    {
        seedAtomic.store ((uint32_t) juce::jmax (1, (int) state.state.getProperty ("seed", 1)));
        auto tree = state.state.getChildWithName ("Pattern");
        if (tree.isValid())
            patternSlot.publish (std::make_shared<Pattern> (Pattern::fromTree (tree)));
        else
            setPattern (generateArrangement (seedAtomic.load()));
    }

    Pattern DooblrRhythmProcessor::generateArrangement (uint32_t seed)
    {
        seedAtomic.store (seed ? seed : 1u);
        auto req = readRequest();
        req.seed = seedAtomic.load();
        auto prev = patternSlot.current();
        req.previous = prev.get();
        auto p = generate (req);
        setPattern (std::move (p));
        return currentPattern();
    }

    Pattern DooblrRhythmProcessor::generateArrangement()
    {
        return generateArrangement (seedAtomic.load());
    }

    Pattern DooblrRhythmProcessor::variation()
    {
        return generateArrangement (seedAtomic.load() + 1u);
    }

    Pattern DooblrRhythmProcessor::regenerate()
    {
        auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt (juce::Range<int> (1, 2147483647));
        return generateArrangement (seed);
    }

    void DooblrRhythmProcessor::setPattern (Pattern p)
    {
        seedAtomic.store (p.seed ? p.seed : 1u);
        storePatternInState (p);
        patternSlot.publish (std::make_shared<Pattern> (std::move (p)));
        lastFiredStep = -1;
        patternChanged.sendChangeMessage();
    }

    Pattern DooblrRhythmProcessor::currentPattern() const
    {
        if (auto c = patternSlot.current())
            return *c;
        return {};
    }

    void DooblrRhythmProcessor::toggleHit (int instrument, int step, float velocity)
    {
        auto p = currentPattern();
        const int total = juce::jmax (1, p.totalSteps());
        step = wrapPositive (step, total);
        instrument = juce::jlimit (0, numInstruments - 1, instrument); // NOLINT

        int found = -1;
        for (int i = 0; i < (int) p.hits.size(); ++i)
            if ((int) p.hits[(size_t) i].instrument == instrument && (int) p.hits[(size_t) i].step == step)
                found = i;

        if (found >= 0)
            p.hits.erase (p.hits.begin() + found);
        else
        {
            Hit h;
            h.instrument = (uint8_t) instrument;
            h.step = (uint16_t) step;
            h.articulation = (uint8_t) Articulation::open;
            h.velocity = juce::jlimit (0.05f, 1.0f, velocity);
            h.structural = 1;
            p.add (h);
        }
        setPattern (std::move (p));
    }

    void DooblrRhythmProcessor::setHitVelocity (int instrument, int step, float velocity)
    {
        auto p = currentPattern();
        bool any = false;
        for (auto& h : p.hits)
            if ((int) h.instrument == instrument && (int) h.step == step)
            {
                h.velocity = juce::jlimit (0.05f, 1.0f, velocity);
                any = true;
            }
        if (any)
            setPattern (std::move (p));
        else
            toggleHit (instrument, step, velocity);
    }

    void DooblrRhythmProcessor::clearHit (int instrument, int step)
    {
        auto p = currentPattern();
        const auto before = p.hits.size();
        p.hits.erase (std::remove_if (p.hits.begin(), p.hits.end(), [&] (const Hit& h)
                                      { return (int) h.instrument == instrument && (int) h.step == step; }),
                      p.hits.end());
        if (p.hits.size() != before)
            setPattern (std::move (p));
    }

    juce::File DooblrRhythmProcessor::writeMidiFile (int instrumentOnly) const
    {
        const auto p = currentPattern();
        const auto bpm = hostBpmAtomic.load();
        auto file = patternToMidi (p, bpm, instrumentOnly);
        auto dest = tempFolder().getChildFile (instrumentOnly < 0
            ? "dooblr-rhythm.mid"
            : "dooblr-rhythm-" + juce::String (instrumentIds[instrumentOnly]) + ".mid");
        dest.deleteFile();
        juce::FileOutputStream out (dest);
        if (out.openedOk())
            file.writeTo (out);
        return dest;
    }

    juce::File DooblrRhythmProcessor::writeWavFile() const
    {
        auto p = currentPattern();
        const double bpm = juce::jmax (20.0, hostBpmAtomic.load());
        const double sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        const double quartersPerBar = (double) beatsPerBar (p.timeSignature) * (4.0 / (double) beatUnit (p.timeSignature));
        const double seconds = (double) p.bars * quartersPerBar * 60.0 / bpm;
        const int n = juce::jmax (1, (int) std::lround (seconds * sr));

        dsp::SynthKit local;
        local.prepare (sr, 512);
        local.setSettings (readKitSettings());

        const double spq = (double) p.stepsPerBar / juce::jmax (1e-6, quartersPerBar);
        juce::AudioBuffer<float> buffer (2, n);
        buffer.clear();
        int last = -1;
        const int block = 512;
        juce::AudioBuffer<float> chunk (2, block);

        for (int pos = 0; pos < n; pos += block)
        {
            const int m = juce::jmin (block, n - pos);
            const double stepPos = ((double) pos / sr) * (bpm / 60.0) * spq;
            const int step = ((int) stepPos % juce::jmax (1, p.totalSteps()) + p.totalSteps()) % juce::jmax (1, p.totalSteps());
            if (step != last)
            {
                last = step;
                juce::MidiBuffer ignore;
                for (const auto& h : p.hits)
                    if ((int) h.step == step)
                    {
                        const uint32_t rr = mix32 (p.seed, (uint32_t) h.instrument, (uint32_t) h.step);
                        local.noteOn ((int) h.instrument, (Articulation) h.articulation, h.velocity, rr);
                    }
                juce::ignoreUnused (ignore);
            }
            local.render (chunk.getWritePointer (0), chunk.getWritePointer (1), m);
            buffer.copyFrom (0, pos, chunk, 0, 0, m);
            buffer.copyFrom (1, pos, chunk, 1, 0, m);
        }

        auto dest = tempFolder().getChildFile ("dooblr-rhythm.wav");
        dest.deleteFile();
        juce::WavAudioFormat fmt;
        if (std::unique_ptr<juce::OutputStream> os { dest.createOutputStream() })
            if (auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (sr)
                                                     .withNumChannels (2)
                                                     .withBitsPerSample (16)))
                w->writeFromAudioSampleBuffer (buffer, 0, n);
        return dest;
    }

    juce::String DooblrRhythmProcessor::loadReplacement (int instrument, int articBucket, const juce::File& file)
    {
        instrument = juce::jlimit (0, numInstruments - 1, instrument);
        articBucket = juce::jlimit (0, 3, articBucket);
        juce::String error;
        auto sample = dsp::Sample::load (file, error);
        if (sample == nullptr)
            return error;
        sample->instrument = instrument;
        sample->articulation = articBucket;
        kit.replacements[instrument][articBucket].publish (sample);

        auto samples = state.state.getOrCreateChildWithName ("Samples", nullptr);
        const auto key = instrumentParam (instrument, "s") + juce::String (articBucket);
        auto child = samples.getChildWithProperty ("id", key);
        if (! child.isValid())
        {
            child = juce::ValueTree ("Sample");
            samples.appendChild (child, nullptr);
        }
        child.setProperty ("id", key, nullptr);
        child.setProperty ("path", file.getFullPathName(), nullptr);
        child.setProperty ("instrument", instrument, nullptr);
        child.setProperty ("artic", articBucket, nullptr);
        return {};
    }

    void DooblrRhythmProcessor::clearReplacement (int instrument, int articBucket)
    {
        instrument = juce::jlimit (0, numInstruments - 1, instrument);
        articBucket = juce::jlimit (0, 3, articBucket);
        kit.replacements[instrument][articBucket].publish (nullptr);
        auto samples = state.state.getChildWithName ("Samples");
        const auto key = instrumentParam (instrument, "s") + juce::String (articBucket);
        auto child = samples.getChildWithProperty ("id", key);
        if (child.isValid())
            samples.removeChild (child, nullptr);
    }

    void DooblrRhythmProcessor::setPreviewPlaying (bool shouldPlay)
    {
        previewPlay.store (shouldPlay);
        if (! shouldPlay)
            lastFiredStep = -1;
    }

    void DooblrRhythmProcessor::prepareToPlay (double sr, int samplesPerBlock)
    {
        sampleRate = sr;
        kit.prepare (sr, samplesPerBlock);
        lastFiredStep = -1;
        phaseSteps = 0.0;
    }

    double DooblrRhythmProcessor::stepsPerQuarter (const Pattern& pattern) const noexcept
    {
        const double quarters = (double) beatsPerBar (pattern.timeSignature)
            * (4.0 / (double) beatUnit (pattern.timeSignature));
        return (double) pattern.stepsPerBar / juce::jmax (1e-9, quarters);
    }

    void DooblrRhythmProcessor::fireStep (const Pattern& pattern, int step, juce::MidiBuffer& midi, int sampleOffset)
    {
        const int total = juce::jmax (1, pattern.totalSteps());
        step = ((step % total) + total) % total;
        for (const auto& h : pattern.hits)
        {
            if ((int) h.step != step)
                continue;
            const uint32_t rr = mix32 (pattern.seed, (uint32_t) h.instrument, (uint32_t) h.step);
            kit.noteOn ((int) h.instrument, (Articulation) h.articulation, h.velocity, rr);
            const int note = midiNoteFor ((Instrument) h.instrument, (Articulation) h.articulation);
            const juce::uint8 vel = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (h.velocity * 127.0f));
            midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), sampleOffset);
            midi.addEvent (juce::MidiMessage::noteOff (1, note), juce::jmin (sampleOffset + 8, sampleOffset + 1));
        }
        playheadStepAtomic.store (step, std::memory_order_relaxed);
    }

    void DooblrRhythmProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.clear (ch, 0, numSamples);

        kit.setSettings (readKitSettings());
        const Pattern* pattern = patternSlot.acquire();
        if (pattern == nullptr || numSamples <= 0)
        {
            float* l = buffer.getWritePointer (0);
            float* r = numChannels > 1 ? buffer.getWritePointer (1) : l;
            kit.render (l, r, numSamples);
            return;
        }

        double bpm = 120.0;
        double ppq = 0.0;
        bool playing = false;
        if (auto* head = getPlayHead())
        {
            if (auto pos = head->getPosition())
            {
                bpm = pos->getBpm().orFallback (120.0);
                playing = pos->getIsPlaying();
                ppq = pos->getPpqPosition().orFallback (0.0);
            }
        }
        hostBpmAtomic.store (bpm, std::memory_order_relaxed);
        hostPlayingAtomic.store (playing, std::memory_order_relaxed);

        for (const auto metadata : midi)
        {
            if (metadata.numBytes < 3)
                continue;
            const auto status = metadata.data[0];
            if ((status & 0xF0) != 0x90 || metadata.data[2] == 0)
                continue;
            const int note = metadata.data[1];
            const float vel = (float) metadata.data[2] * (1.0f / 127.0f);
            if (note == midiTransportNote && ! playing)
                previewPlay.store (! previewPlay.load());
            else
            {
                const auto inst = instrumentFromMidiNote (note);
                const auto artic = articulationFromMidiNote (note);
                kit.noteOn ((int) inst, artic, vel,
                            mix32 (seedAtomic.load(), (uint32_t) note, (uint32_t) metadata.samplePosition));
            }
        }
        midi.clear();

        const int total = juce::jmax (1, pattern->totalSteps());
        const double spq = stepsPerQuarter (*pattern);
        const bool run = playing || previewPlay.load();

        if (playing)
        {
            const double stepPos = ppq * spq;
            const double stepsInBlock = (double) numSamples * (bpm / 60.0) * spq / sampleRate;
            const int startStep = (int) std::floor (stepPos);
            const int endStep = (int) std::floor (stepPos + stepsInBlock);
            for (int s = startStep; s <= endStep; ++s)
            {
                const int wrapped = ((s % total) + total) % total;
                if (wrapped == lastFiredStep && s != startStep)
                    continue;
                if (wrapped != lastFiredStep || ! lastHostPlaying)
                {
                    const double frac = (double) s - stepPos;
                    const int offset = juce::jlimit (0, numSamples - 1,
                        (int) std::lround (frac / juce::jmax (1e-12, stepsInBlock) * (double) numSamples));
                    fireStep (*pattern, wrapped, midi, offset);
                    lastFiredStep = wrapped;
                }
            }
        }
        else if (run)
        {
            const double stepsInBlock = (double) numSamples * (bpm / 60.0) * spq / sampleRate;
            double pos = phaseSteps;
            const double end = pos + stepsInBlock;
            int s = (int) std::floor (pos);
            const int endS = (int) std::floor (end);
            for (; s <= endS; ++s)
            {
                const int wrapped = ((s % total) + total) % total;
                if (wrapped != lastFiredStep)
                {
                    const double frac = (double) s - pos;
                    const int offset = juce::jlimit (0, numSamples - 1,
                        (int) std::lround (frac / juce::jmax (1e-12, stepsInBlock) * (double) numSamples));
                    fireStep (*pattern, wrapped, midi, offset);
                    lastFiredStep = wrapped;
                }
            }
            phaseSteps = std::fmod (end, (double) total);
            if (phaseSteps < 0.0) phaseSteps += (double) total;
        }
        else
        {
            lastFiredStep = -1;
        }
        lastHostPlaying = playing;

        float* l = buffer.getWritePointer (0);
        float* r = numChannels > 1 ? buffer.getWritePointer (1) : l;
        kit.render (l, r, numSamples);
    }

    juce::AudioProcessorEditor* DooblrRhythmProcessor::createEditor()
    {
        return new DooblrRhythmEditor (*this);
    }

    int DooblrRhythmProcessor::getNumPrograms() { return (int) presets::factory().size(); }

    void DooblrRhythmProcessor::setCurrentProgram (int index)
    {
        const auto& all = presets::factory();
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return;
        currentProgram = index;
        presets::apply (state, all[(size_t) index]);
        setPresetName (all[(size_t) index].name);
        generateArrangement (seedAtomic.load());
        undoHistory.clear();
    }

    const juce::String DooblrRhythmProcessor::getProgramName (int index)
    {
        const auto& all = presets::factory();
        return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
    }

    juce::String DooblrRhythmProcessor::getPresetName() const
    {
        return state.state.getProperty ("presetName").toString();
    }

    void DooblrRhythmProcessor::setPresetName (const juce::String& name)
    {
        state.state.setProperty ("presetName", name, nullptr);
        presetChanged.sendChangeMessage();
    }

    juce::String DooblrRhythmProcessor::saveUserPreset (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (! presets::save (state, file, name))
            return "Could not save preset: " + file.getFullPathName();
        setPresetName (name);
        return "Saved " + name;
    }

    juce::String DooblrRhythmProcessor::loadUserPreset (const juce::File& file)
    {
        const auto error = presets::load (state, file);
        if (error.isNotEmpty())
            return "Could not load preset: " + error;
        restorePatternFromState();
        setPresetName (file.getFileNameWithoutExtension());
        undoHistory.clear();
        patternChanged.sendChangeMessage();
        return "Loaded " + file.getFileNameWithoutExtension();
    }

    void DooblrRhythmProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto copy = state.copyState();
        copy.setProperty ("stateVersion", stateVersion, nullptr);
        copy.setProperty ("seed", (int) seedAtomic.load(), nullptr);
        if (const auto xml = copy.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DooblrRhythmProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        auto tree = juce::ValueTree::fromXml (*xml);
        migrateParameterTree (tree, (int) tree.getProperty ("stateVersion", 1));
        tree.setProperty ("stateVersion", stateVersion, nullptr);
        state.replaceState (tree);
        restorePatternFromState();

        auto samples = state.state.getChildWithName ("Samples");
        for (int i = 0; i < samples.getNumChildren(); ++i)
        {
            const auto c = samples.getChild (i);
            const auto path = c.getProperty ("path").toString();
            if (path.isEmpty())
                continue;
            loadReplacement ((int) c.getProperty ("instrument", 0),
                             (int) c.getProperty ("artic", 0),
                             juce::File (path));
        }

        undoHistory.clear();
        presetChanged.sendChangeMessage();
        patternChanged.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dooblrrhythm::DooblrRhythmProcessor();
}
