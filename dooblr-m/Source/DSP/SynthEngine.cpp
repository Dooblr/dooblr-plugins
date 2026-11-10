#include "SynthEngine.h"

#include <cstring>

namespace dooblrm::dsp
{
    namespace
    {
        int rangeSemitones (int index) noexcept
        {
            static constexpr int st[] { -24, -12, 0, 12, 24, -72 };
            return st[juce::jlimit (0, 5, index)];
        }

        float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
    }

    void SynthEngine::prepare (double sr, int)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        osc1.prepare (sampleRate);
        osc2.prepare (sampleRate);
        osc3.prepare (sampleRate);
        ladder.prepare (sampleRate);
        filt.prepare (sampleRate);
        amp.prepare (sampleRate);
        noise.seed (0xC0FFEE01u);
        drift1.seed (0x11111111u);
        drift2.seed (0x22222222u);
        drift3.seed (0x33333333u);
        thermal.seed (0xDEADBEEFu);
        a440Phase = 0.0f;
        outputSmoothed = 1.0f;
        reset();
    }

    void SynthEngine::reset()
    {
        osc1.reset();
        osc2.reset();
        osc3.reset();
        ladder.reset();
        filt.reset();
        amp.reset();
        heldCount = 0;
        noteOrder = 0;
        currentNote = 60;
        currentVelocity = 0.8f;
        gate = false;
        hasPlayed = false;
        sustainPedal = false;
        feedbackSample = 0.0f;
        lastSample = 0.0f;
        glide.jump (60.0f);
        midiPitchBend.store (0.0f, std::memory_order_relaxed);
        midiModWheel.store (0.0f, std::memory_order_relaxed);
    }

    void SynthEngine::allNotesOff()
    {
        heldCount = 0;
        gate = false;
        filt.gate (false, true);
        amp.gate (false, true);
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        note = juce::jlimit (0, 127, note);
        velocity = juce::jlimit (0.0f, 1.0f, velocity);

        bool alreadyHeld = false;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note)
            {
                held[(size_t) i].order = ++noteOrder;
                alreadyHeld = true;
                break;
            }

        if (! alreadyHeld && heldCount < (int) held.size())
            held[(size_t) heldCount++] = { note, ++noteOrder };

        const auto hadGate = gate;

        int chosen = note;
        if (settings.notePriority == 0)
        {
            chosen = 127;
            for (int i = 0; i < heldCount; ++i)
                chosen = juce::jmin (chosen, held[(size_t) i].note);
        }
        else
        {
            int bestOrder = -1;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].order >= bestOrder)
                {
                    bestOrder = held[(size_t) i].order;
                    chosen = held[(size_t) i].note;
                }
        }

        currentNote = chosen;
        currentVelocity = velocity;
        gate = true;

        const bool retrigger = ! settings.legato || ! hadGate;
        filt.gate (true, retrigger);
        amp.gate (true, retrigger);

        // First note after reset jumps; later notes (including after silence) glide from the last CV.
        if (! hasPlayed || settings.glide <= 1.0e-4f)
            glide.jump ((float) currentNote);
        hasPlayed = true;
    }

    void SynthEngine::noteOff (int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note)
            {
                held[(size_t) i] = held[(size_t) (heldCount - 1)];
                --heldCount;
                break;
            }

        if (sustainPedal)
            return;

        if (heldCount == 0)
        {
            gate = false;
            filt.gate (false, true);
            amp.gate (false, true);
            return;
        }

        int chosen = held[0].note;
        if (settings.notePriority == 0)
        {
            chosen = 127;
            for (int i = 0; i < heldCount; ++i)
                chosen = juce::jmin (chosen, held[(size_t) i].note);
        }
        else
        {
            int bestOrder = -1;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].order >= bestOrder)
                {
                    bestOrder = held[(size_t) i].order;
                    chosen = held[(size_t) i].note;
                }
        }

        currentNote = chosen;
        // Legato: stay in sustain / decay; no retrigger when a held note takes over.
        filt.gate (true, false);
        amp.gate (true, false);
    }

    void SynthEngine::handleMidiBytes (const juce::uint8* data, int size)
    {
        if (data == nullptr || size <= 0)
            return;

        const auto status = data[0] & 0xf0;
        const auto d1 = size > 1 ? data[1] : 0;
        const auto d2 = size > 2 ? data[2] : 0;

        if (status == 0x90 && d2 > 0)
            noteOn (d1, (float) d2 * (1.0f / 127.0f));
        else if (status == 0x80 || (status == 0x90 && d2 == 0))
            noteOff (d1);
        else if (status == 0xb0 && (d1 == 123 || d1 == 120))
            allNotesOff();
        else if (status == 0xe0)
        {
            const auto w = d1 | (d2 << 7); // 0..16383, 8192 = centre
            midiPitchBend.store (((float) w - 8192.0f) / 8192.0f, std::memory_order_relaxed);
        }
        else if (status == 0xb0 && d1 == 1)
            midiModWheel.store ((float) d2 * (1.0f / 127.0f), std::memory_order_relaxed);
        else if (status == 0xb0 && d1 == 64)
        {
            if (d2 >= 64)
                sustainPedal = true;
            else
            {
                sustainPedal = false;
                if (heldCount == 0 && gate)
                {
                    gate = false;
                    filt.gate (false, true);
                    amp.gate (false, true);
                }
            }
        }
    }

    float SynthEngine::oscillatorHz (int rangeIndex, float detuneSt, bool keyTrack,
                                     float note, float modSt, float driftSt) const noexcept
    {
        const auto baseNote = keyTrack ? note : 48.0f; // C3 when Osc 3 Control is off
        const auto midi = baseNote
                          + settings.tune
                          + (float) rangeSemitones (rangeIndex)
                          + detuneSt
                          + modSt
                          + driftSt;
        return midiToHz (midi);
    }

    float SynthEngine::renderSample()
    {
        const auto note = glide.process ((float) currentNote, settings.glide, (float) sampleRate);
        const auto midiBend = midiPitchBend.load (std::memory_order_relaxed);
        const auto bendSt = (float) settings.bendRange * (midiBend + settings.pitchBend);
        const auto midiMod = midiModWheel.load (std::memory_order_relaxed);
        const auto wheel = juce::jmax (settings.modWheel, midiMod);

        const auto d1 = drift1.process();
        const auto d2 = drift2.process();
        const auto d3 = drift3.process();

        // Osc 3 first: it is a modulation source (and optionally an audio oscillator).
        const auto osc3Hz = oscillatorHz (settings.osc3Range, settings.osc3Freq + bendSt,
                                          settings.osc3Control, note, 0.0f, d3);
        const auto osc3Out = osc3.process (osc3Hz, (float) sampleRate, settings.osc3Wave);

        const auto noiseOut = settings.noiseType == 0 ? noise.white() : noise.pink();
        const auto modSource = (1.0f - settings.modMix) * osc3Out + settings.modMix * noiseOut;
        const auto modAmount = wheel * modSource;
        const auto pitchModSt = settings.oscMod ? 7.0f * modAmount : 0.0f;
        const auto filterModOct = settings.filterMod ? 3.0f * modAmount : 0.0f;

        const auto osc1Hz = oscillatorHz (settings.osc1Range, bendSt, true, note, pitchModSt, d1);
        const auto osc2Hz = oscillatorHz (settings.osc2Range, settings.osc2Freq + bendSt, true, note, pitchModSt, d2);
        const auto o1 = osc1.process (osc1Hz, (float) sampleRate, settings.osc1Wave);
        const auto o2 = osc2.process (osc2Hz, (float) sampleRate, settings.osc2Wave);

        float mix = 0.0f;
        float mixDrive = 0.0f;
        const auto add = [&] (bool on, float level, float signal)
        {
            if (! on)
                return;
            mix += level * signal;
            mixDrive += level;
        };
        add (settings.mixOsc1On, settings.mixOsc1, o1);
        add (settings.mixOsc2On, settings.mixOsc2, o2);
        add (settings.mixOsc3On, settings.mixOsc3, osc3Out);
        add (settings.mixNoiseOn, settings.mixNoise, noiseOut);
        if (settings.mixExtOn)
        {
            mix += settings.mixExt * feedbackSample;
            mixDrive += settings.mixExt;
        }

        // Mixer overload: tanh at the virtual-earth sum, hotter as more channels are up
        // (the classic "turn everything up / patch Ext to the output" trick).
        const auto drive = 0.85f + 0.9f * mixDrive;
        mix = tanhApprox (mix * drive);

        const auto vel = settings.velocityOn ? currentVelocity : 1.0f;
        const auto fEnv = filt.process (settings.filterEnv.attack, settings.filterEnv.decay,
                                        settings.filterEnv.sustain, settings.filterEnv.decaySwitch);
        const auto aEnv = amp.process (settings.ampEnv.attack, settings.ampEnv.decay,
                                       settings.ampEnv.sustain, settings.ampEnv.decaySwitch);

        const auto kybd = (settings.kybd1 ? 1.0f / 3.0f : 0.0f) + (settings.kybd2 ? 2.0f / 3.0f : 0.0f);
        auto cutoffHz = settings.cutoff;
        cutoffHz *= std::exp2 (kybd * (note - 60.0f) / 12.0f);
        cutoffHz *= std::exp2 (settings.contourAmt * 5.0f * fEnv * vel);
        cutoffHz *= std::exp2 (filterModOct);
        lastCutoffHz = cutoffHz;

        ladder.set (cutoffHz, settings.emphasis);
        // Thermal noise so a fully resonant ladder can start from rest (paper: large-signal
        // behaviour; analog transistors are never silent).
        const auto filtered = ladder.process (mix + thermal.bipolar() * 1.5e-5f);
        feedbackSample = filtered;

        auto sample = filtered * aEnv * vel * settings.volume;

        if (settings.a440)
        {
            a440Phase += 440.0f / (float) sampleRate;
            if (a440Phase >= 1.0f)
                a440Phase -= 1.0f;
            sample += 0.18f * std::sin (twoPi * a440Phase);
        }

        const auto targetGain = dbToGain (settings.outputDb);
        outputSmoothed += (targetGain - outputSmoothed) * 0.0015f;
        sample *= outputSmoothed;
        sample = flushDenormal (juce::jlimit (-4.0f, 4.0f, sample));
        lastSample = sample;
        return sample;
    }

    void SynthEngine::process (float* left, float* right, int numSamples, juce::MidiBuffer& midi, const HostTime&)
    {
        struct Event { int sample; juce::uint8 data[4]; juce::uint8 size; };
        Event events[64];
        int nEvents = 0;
        for (const auto metadata : midi)
        {
            if (nEvents >= 64)
                break;
            Event e { metadata.samplePosition, {}, (juce::uint8) juce::jmin (4, metadata.numBytes) };
            if (metadata.data != nullptr && e.size > 0)
                std::memcpy (e.data, metadata.data, e.size);
            events[nEvents++] = e;
        }

        int eventIndex = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (eventIndex < nEvents && events[eventIndex].sample <= i)
            {
                handleMidiBytes (events[eventIndex].data, events[eventIndex].size);
                ++eventIndex;
            }

            const auto s = renderSample();
            left[i] = s;
            right[i] = s;

            if ((++scopeDecim & 1) == 0)
                scope.push (s);
        }

        while (eventIndex < nEvents)
        {
            handleMidiBytes (events[eventIndex].data, events[eventIndex].size);
            ++eventIndex;
        }
    }
}
