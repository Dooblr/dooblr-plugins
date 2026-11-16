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

        float divisionBeats (int index) noexcept
        {
            static constexpr float beats[] { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
            return beats[juce::jlimit (0, 5, index)];
        }

        constexpr float stealFadeSeconds = 0.004f;
    }

    void SynthEngine::Voice::prepare (double sr, int index)
    {
        osc1.prepare (sr);
        osc2.prepare (sr);
        osc3.prepare (sr);
        ladder.prepare (sr);
        filt.prepare (sr);
        amp.prepare (sr);
        const auto u = (std::uint32_t) index;
        osc3AudioGain = 1.0f;
        onsetGain = 1.0f;
        reset();
        noise.seed (0xC0FFEE01u + u * 0x9E3779B9u);
        drift1.seed (0x11111111u + u * 0x00010001u);
        drift2.seed (0x22222222u + u * 0x00020002u);
        drift3.seed (0x33333333u + u * 0x00030003u);
        thermal.seed (0xDEADBEEFu + u * 0x01010101u);
        lfo.rng.seed (0x51F00Du + u * 17u);
    }

    void SynthEngine::Voice::reset()
    {
        osc1.reset();
        osc2.reset();
        osc3.reset();
        ladder.reset();
        filt.reset();
        amp.reset();
        note = 60;
        velocity = 0.8f;
        gate = false;
        hasPlayed = false;
        sustained = false;
        startOrder = 0;
        feedbackSample = 0.0f;
        lastCutoffHz = 1200.0f;
        osc3AudioGain = 1.0f;
        onsetGain = 1.0f;
        onsetSamplesLeft = 0;
        onsetSamplesTotal = 1;
        glide.jump (60.0f);
        lfo = {};
    }

    bool SynthEngine::Voice::isIdle() const noexcept
    {
        return ! gate && amp.stage == Contour::Stage::idle;
    }

    bool SynthEngine::Voice::needsRender() const noexcept
    {
        return gate
            || amp.stage != Contour::Stage::idle
            || filt.stage != Contour::Stage::idle
            || onsetSamplesLeft > 0;
    }

    void SynthEngine::Voice::start (int newNote, float newVelocity, bool stolen,
                                    const Settings& s, float sr)
    {
        note = juce::jlimit (0, 127, newNote);
        velocity = juce::jlimit (0.0f, 1.0f, newVelocity);
        gate = true;
        sustained = false;
        filt.gate (true, true);
        amp.gate (true, true);

        if (! hasPlayed || s.glide <= 1.0e-4f)
            glide.jump ((float) note);
        hasPlayed = true;

        if (stolen)
        {
            onsetSamplesTotal = juce::jmax (1, (int) std::round (stealFadeSeconds * sr));
            onsetSamplesLeft = onsetSamplesTotal;
            onsetGain = 0.0f;
        }
        else
        {
            onsetSamplesLeft = 0;
            onsetGain = 1.0f;
        }
    }

    void SynthEngine::Voice::release()
    {
        gate = false;
        sustained = false;
        filt.gate (false, true);
        amp.gate (false, true);
    }

    float SynthEngine::Voice::render (const Settings& s, float sr,
                                      float globalLfo, float lfoHzTarget,
                                      float midiBend, float midiMod)
    {
        if (onsetSamplesLeft > 0)
        {
            onsetGain = 1.0f - (float) onsetSamplesLeft / (float) juce::jmax (1, onsetSamplesTotal);
            --onsetSamplesLeft;
            if (onsetSamplesLeft == 0)
                onsetGain = 1.0f;
        }
        else
        {
            onsetGain = 1.0f;
        }

        const auto noteCv = glide.process ((float) note, s.glide, sr);
        const auto bendSt = (float) s.bendRange * (midiBend + s.pitchBend);
        const auto wheel = juce::jmax (s.modWheel, midiMod);

        const auto d1 = drift1.process();
        const auto d2 = drift2.process();
        const auto d3 = drift3.process();

        const auto oscHz = [&] (int rangeIndex, float detuneSt, bool keyTrack, float n, float modSt, float driftSt)
        {
            const auto baseNote = keyTrack ? n : 48.0f;
            const auto midi = baseNote
                              + s.tune
                              + (float) rangeSemitones (rangeIndex)
                              + detuneSt
                              + modSt
                              + driftSt;
            return midiToHz (midi);
        };

        float osc3Out = 0.0f;
        if (s.osc3Lfo)
        {
            if (s.lfoRetrigger)
                osc3Out = lfo.process (lfoHzTarget, sr, s.osc3Wave);
            else
                osc3Out = globalLfo;
        }
        else
        {
            const auto osc3Hz = oscHz (s.osc3Range, s.osc3Freq + bendSt,
                                       s.osc3Control, noteCv, 0.0f, d3);
            osc3Out = osc3.process (osc3Hz, sr, s.osc3Wave);
        }

        const auto audioGainTarget = s.osc3Lfo ? 0.0f : 1.0f;
        if (std::abs (audioGainTarget - osc3AudioGain) < 1.0e-5f)
            osc3AudioGain = audioGainTarget;
        else
            osc3AudioGain += (audioGainTarget - osc3AudioGain)
                             * (1.0f - std::exp (-1.0f / (0.005f * sr)));

        const auto noiseOut = s.noiseType == 0 ? noise.white() : noise.pink();
        const auto modSource = (1.0f - s.modMix) * osc3Out + s.modMix * noiseOut;
        const auto modAmount = wheel * modSource;
        const auto pitchModSt = s.oscMod ? 7.0f * modAmount : 0.0f;
        const auto filterModOct = s.filterMod ? 3.0f * modAmount : 0.0f;

        const auto osc1Hz = oscHz (s.osc1Range, bendSt, true, noteCv, pitchModSt, d1);
        const auto osc2Hz = oscHz (s.osc2Range, s.osc2Freq + bendSt, true, noteCv, pitchModSt, d2);
        const auto o1 = osc1.process (osc1Hz, sr, s.osc1Wave);
        const auto o2 = osc2.process (osc2Hz, sr, s.osc2Wave);

        float mix = 0.0f;
        float mixDrive = 0.0f;
        const auto add = [&] (bool on, float level, float signal)
        {
            if (! on)
                return;
            mix += level * signal;
            mixDrive += level;
        };
        add (s.mixOsc1On, s.mixOsc1, o1);
        add (s.mixOsc2On, s.mixOsc2, o2);
        add (s.mixOsc3On, s.mixOsc3 * osc3AudioGain, osc3Out);
        add (s.mixNoiseOn, s.mixNoise, noiseOut);
        if (s.mixExtOn)
        {
            mix += s.mixExt * feedbackSample;
            mixDrive += s.mixExt;
        }

        const auto drive = 0.85f + 0.9f * mixDrive;
        mix = tanhApprox (mix * drive);

        const auto vel = s.velocityOn ? velocity : 1.0f;
        const auto fEnv = filt.process (s.filterEnv.attack, s.filterEnv.decay,
                                        s.filterEnv.sustain, s.filterEnv.decaySwitch);
        const auto aEnv = amp.process (s.ampEnv.attack, s.ampEnv.decay,
                                       s.ampEnv.sustain, s.ampEnv.decaySwitch);

        const auto kybd = (s.kybd1 ? 1.0f / 3.0f : 0.0f) + (s.kybd2 ? 2.0f / 3.0f : 0.0f);
        auto cutoffHz = s.cutoff;
        cutoffHz *= std::exp2 (kybd * (noteCv - 60.0f) / 12.0f);
        cutoffHz *= std::exp2 (s.contourAmt * contourOctaves * fEnv * vel);
        cutoffHz *= std::exp2 (filterModOct);
        lastCutoffHz = cutoffHz;

        ladder.set (cutoffHz, s.emphasis);
        const auto filtered = ladder.process (mix + thermal.bipolar() * 1.5e-5f);
        feedbackSample = filtered;

        auto sample = filtered * aEnv * vel * s.volume * onsetGain;
        return flushDenormal (juce::jlimit (-4.0f, 4.0f, sample));
    }

    void SynthEngine::setSettings (const Settings& s) noexcept
    {
        if (s.polyphony != settings.polyphony)
        {
            if (s.polyphony)
            {
                gate = false;
                filt.gate (false, true);
                amp.gate (false, true);
            }
            else
            {
                for (auto& v : voices)
                    v.release();
            }
        }
        settings = s;
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
        outputSmoothed = 1.0f;
        osc3AudioGain = settings.osc3Lfo ? 0.0f : 1.0f;
        lfo.hzSmoothed = lfoTargetHz();
        for (int i = 0; i < maxVoices; ++i)
            voices[(size_t) i].prepare (sampleRate, i);
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
        filterEnvTap.store (0.0f, std::memory_order_relaxed);
        filterEnvStageTap.store (0, std::memory_order_relaxed);
        lfo = {};
        lfo.hzSmoothed = lfoTargetHz();
        lfoTap.store (0.0f, std::memory_order_relaxed);
        lastLfoHz = lfo.hzSmoothed;
        lastLfo = 0.0f;
        osc3AudioGain = settings.osc3Lfo ? 0.0f : 1.0f;
        voiceAge = 0;
        for (int i = 0; i < maxVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v.reset();
            v.osc3AudioGain = settings.osc3Lfo ? 0.0f : 1.0f;
            v.lfo.hzSmoothed = lfoTargetHz();
            const auto u = (std::uint32_t) i;
            v.noise.seed (0xC0FFEE01u + u * 0x9E3779B9u);
            v.drift1.seed (0x11111111u + u * 0x00010001u);
            v.drift2.seed (0x22222222u + u * 0x00020002u);
            v.drift3.seed (0x33333333u + u * 0x00030003u);
            v.thermal.seed (0xDEADBEEFu + u * 0x01010101u);
            v.lfo.rng.seed (0x51F00Du + u * 17u);
        }
    }

    void SynthEngine::allNotesOff()
    {
        heldCount = 0;
        gate = false;
        filt.gate (false, true);
        amp.gate (false, true);
        for (auto& v : voices)
            v.release();
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        if (settings.polyphony)
        {
            polyNoteOn (note, velocity);
            return;
        }

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
        const bool retrigger = ! settings.legato || ! hadGate;
        if (settings.osc3Lfo && settings.lfoRetrigger)
            lfo.retrigger();

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

        filt.gate (true, retrigger);
        amp.gate (true, retrigger);

        // First note after reset jumps; later notes (including after silence) glide from the last CV.
        if (! hasPlayed || settings.glide <= 1.0e-4f)
            glide.jump ((float) currentNote);
        hasPlayed = true;
    }

    void SynthEngine::noteOff (int note)
    {
        if (settings.polyphony)
        {
            polyNoteOff (note);
            return;
        }

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
            if (settings.polyphony)
                polySustain (d2 >= 64);
            else if (d2 >= 64)
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

    float SynthEngine::lfoTargetHz() const noexcept
    {
        if (! settings.lfoSync)
            return juce::jlimit (0.05f, 30.0f, settings.lfoRate);

        const auto bpm = (hostTime.bpm > 20.0 && hostTime.bpm < 400.0) ? hostTime.bpm : 120.0;
        const auto beats = divisionBeats (settings.lfoDivision);
        return juce::jlimit (0.05f, 30.0f, (float) (bpm / 60.0 / (double) beats));
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
        float osc3Out = 0.0f;
        if (settings.osc3Lfo)
        {
            osc3Out = lfo.process (lfoTargetHz(), (float) sampleRate, settings.osc3Wave);
            lastLfoHz = lfo.hzSmoothed;
            lastLfo = osc3Out;
        }
        else
        {
            const auto osc3Hz = oscillatorHz (settings.osc3Range, settings.osc3Freq + bendSt,
                                              settings.osc3Control, note, 0.0f, d3);
            osc3Out = osc3.process (osc3Hz, (float) sampleRate, settings.osc3Wave);
        }

        const auto audioGainTarget = settings.osc3Lfo ? 0.0f : 1.0f;
        if (std::abs (audioGainTarget - osc3AudioGain) < 1.0e-5f)
            osc3AudioGain = audioGainTarget;
        else
            osc3AudioGain += (audioGainTarget - osc3AudioGain)
                             * (1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate)));

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
        add (settings.mixOsc3On, settings.mixOsc3 * osc3AudioGain, osc3Out);
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
        cutoffHz *= std::exp2 (settings.contourAmt * contourOctaves * fEnv * vel);
        cutoffHz *= std::exp2 (filterModOct);
        lastCutoffHz = cutoffHz;

        ladder.set (cutoffHz, settings.emphasis);
        // Thermal noise so a fully resonant ladder can start from rest (paper: large-signal
        // behaviour; analog transistors are never silent).
        const auto filtered = ladder.process (mix + thermal.bipolar() * 1.5e-5f);
        feedbackSample = filtered;

        auto sample = filtered * aEnv * vel * settings.volume;

        const auto targetGain = dbToGain (settings.outputDb);
        outputSmoothed += (targetGain - outputSmoothed) * 0.0015f;
        sample *= outputSmoothed;
        sample = flushDenormal (juce::jlimit (-4.0f, 4.0f, sample));
        lastSample = sample;
        return sample;
    }

    int SynthEngine::allocateVoice (int note) noexcept
    {
        for (int i = 0; i < maxVoices; ++i)
            if (voices[(size_t) i].gate && voices[(size_t) i].note == note)
                return i;

        for (int i = 0; i < maxVoices; ++i)
            if (voices[(size_t) i].note == note && ! voices[(size_t) i].isIdle())
                return i;

        for (int i = 0; i < maxVoices; ++i)
            if (voices[(size_t) i].isIdle())
                return i;

        auto olderQuieter = [this] (int a, int b)
        {
            const auto& va = voices[(size_t) a];
            const auto& vb = voices[(size_t) b];
            if (va.startOrder != vb.startOrder)
                return va.startOrder < vb.startOrder;
            return va.amp.value < vb.amp.value;
        };

        int best = -1;
        for (int i = 0; i < maxVoices; ++i)
            if (! voices[(size_t) i].gate && (best < 0 || olderQuieter (i, best)))
                best = i;
        if (best >= 0)
            return best;

        best = 0;
        for (int i = 1; i < maxVoices; ++i)
            if (olderQuieter (i, best))
                best = i;
        return best;
    }

    void SynthEngine::polyNoteOn (int note, float velocity)
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

        const int index = allocateVoice (note);
        auto& v = voices[(size_t) index];
        const bool stolen = ! v.isIdle() && v.note != note;

        if (settings.osc3Lfo && settings.lfoRetrigger)
        {
            lfo.retrigger();
            v.lfo.retrigger();
        }

        v.start (note, velocity, stolen, settings, (float) sampleRate);
        v.startOrder = ++voiceAge;
        currentNote = note;
        currentVelocity = velocity;
    }

    void SynthEngine::polyNoteOff (int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note)
            {
                held[(size_t) i] = held[(size_t) (heldCount - 1)];
                --heldCount;
                break;
            }

        if (sustainPedal)
        {
            for (auto& v : voices)
                if (v.gate && v.note == note)
                    v.sustained = true;
            return;
        }

        for (auto& v : voices)
            if (v.note == note && v.gate)
                v.release();
    }

    void SynthEngine::polySustain (bool down)
    {
        if (down)
        {
            sustainPedal = true;
            return;
        }

        sustainPedal = false;
        for (auto& v : voices)
        {
            if (! v.gate)
                continue;
            bool stillHeld = false;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].note == v.note)
                {
                    stillHeld = true;
                    break;
                }
            if (! stillHeld)
                v.release();
        }
    }

    int SynthEngine::newestSoundingVoice() const noexcept
    {
        int best = -1;
        int bestOrder = -1;
        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (! v.needsRender())
                continue;
            if (v.startOrder >= bestOrder)
            {
                bestOrder = v.startOrder;
                best = i;
            }
        }
        return best;
    }

    void SynthEngine::updatePolyTaps() noexcept
    {
        const auto i = newestSoundingVoice();
        if (i >= 0)
        {
            const auto& v = voices[(size_t) i];
            filterEnvTap.store (v.filt.value, std::memory_order_relaxed);
            filterEnvStageTap.store ((int) v.filt.stage, std::memory_order_relaxed);
            lastCutoffHz = v.lastCutoffHz;
        }
        else
        {
            filterEnvTap.store (0.0f, std::memory_order_relaxed);
            filterEnvStageTap.store (0, std::memory_order_relaxed);
        }
        lfoTap.store (settings.osc3Lfo ? lastLfo : 0.0f, std::memory_order_relaxed);
    }

    float SynthEngine::renderPolySample()
    {
        float globalLfo = 0.0f;
        if (settings.osc3Lfo)
        {
            globalLfo = lfo.process (lfoTargetHz(), (float) sampleRate, settings.osc3Wave);
            lastLfoHz = lfo.hzSmoothed;
            lastLfo = globalLfo;
        }

        const auto midiBend = midiPitchBend.load (std::memory_order_relaxed);
        const auto midiMod = midiModWheel.load (std::memory_order_relaxed);
        const auto lfoHz = lfoTargetHz();

        float mix = 0.0f;
        for (auto& v : voices)
        {
            if (! v.needsRender())
                continue;
            mix += v.render (settings, (float) sampleRate, globalLfo, lfoHz, midiBend, midiMod);
        }

        const auto targetGain = dbToGain (settings.outputDb);
        outputSmoothed += (targetGain - outputSmoothed) * 0.0015f;
        mix *= outputSmoothed;
        mix = flushDenormal (juce::jlimit (-4.0f, 4.0f, mix));
        lastSample = mix;
        return mix;
    }

    int SynthEngine::getCurrentNote() const noexcept
    {
        if (! settings.polyphony)
            return currentNote;
        const auto i = newestSoundingVoice();
        return i >= 0 ? voices[(size_t) i].note : currentNote;
    }

    bool SynthEngine::isGated() const noexcept
    {
        if (! settings.polyphony)
            return gate;
        for (const auto& v : voices)
            if (v.gate)
                return true;
        return false;
    }

    float SynthEngine::getAmpEnv() const noexcept
    {
        if (! settings.polyphony)
            return amp.value;
        float m = 0.0f;
        for (const auto& v : voices)
            m = juce::jmax (m, v.amp.value);
        return m;
    }

    float SynthEngine::getGlidePitch() const noexcept
    {
        if (! settings.polyphony)
            return glide.current;
        const auto i = newestSoundingVoice();
        return i >= 0 ? voices[(size_t) i].glide.current : glide.current;
    }

    int SynthEngine::getSoundingVoiceCount() const noexcept
    {
        if (! settings.polyphony)
            return (gate || amp.stage != Contour::Stage::idle) ? 1 : 0;
        int n = 0;
        for (const auto& v : voices)
            if (! v.isIdle())
                ++n;
        return n;
    }

    int SynthEngine::getVoiceNote (int index) const noexcept
    {
        if (! settings.polyphony)
            return index == 0 && (gate || amp.stage != Contour::Stage::idle) ? currentNote : -1;
        if (! juce::isPositiveAndBelow (index, maxVoices))
            return -1;
        const auto& v = voices[(size_t) index];
        return v.isIdle() ? -1 : v.note;
    }

    bool SynthEngine::isVoiceGated (int index) const noexcept
    {
        if (! settings.polyphony)
            return index == 0 && gate;
        if (! juce::isPositiveAndBelow (index, maxVoices))
            return false;
        return voices[(size_t) index].gate;
    }

    void SynthEngine::process (float* left, float* right, int numSamples, juce::MidiBuffer& midi, const HostTime& time)
    {
        hostTime = time;

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

            const auto s = settings.polyphony ? renderPolySample() : renderSample();
            left[i] = s;
            right[i] = s;

            if ((++scopeDecim & 1) == 0)
                scope.push (s);
        }

        if (settings.polyphony)
            updatePolyTaps();
        else
        {
            filterEnvTap.store (filt.value, std::memory_order_relaxed);
            filterEnvStageTap.store ((int) filt.stage, std::memory_order_relaxed);
            lfoTap.store (settings.osc3Lfo ? lastLfo : 0.0f, std::memory_order_relaxed);
        }

        while (eventIndex < nEvents)
        {
            handleMidiBytes (events[eventIndex].data, events[eventIndex].size);
            ++eventIndex;
        }
    }
}
