#include "SynthEngine.h"

#include <cmath>

namespace dooblrprophet::dsp
{
    namespace
    {
        constexpr float stealFadeSeconds = 0.003f;
        constexpr float killFadeSeconds = 0.006f;
        constexpr float maxUnisonDetuneCents = 30.0f;
        constexpr float envToCutoffOctaves = 7.0f;
        constexpr float lfoToPitchSemitones = 12.0f;
        constexpr float lfoToShape = 0.45f;
        constexpr float lfoToCutoffOctaves = 3.0f;
        constexpr float polyModToPitchSemitones = 36.0f;
        constexpr float polyModToShape = 0.5f;
        constexpr float polyModToCutoffOctaves = 4.0f;
        constexpr float aftertouchToCutoffOctaves = 3.0f;
        constexpr float osc2FixedNote = 48.0f;      // key track off: C3
        constexpr float lowFreqSemitones = -72.0f;  // low-frequency mode: six octaves down
        constexpr float voiceHeadroom = 0.25f;

        float clampShape (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

        /** Oscillator frequency (analog-reference.md, Blueprint / Oscillator): 1V/oct exponential
            law, a small PWM->pitch leak, then HF flattening f_eff = 1 / (1/f_ideal + t_r). */
        float oscillatorHz (float note, float pulseWidth) noexcept
        {
            const auto ideal = semitonesToHz (note) * (1.0f + model::pwmPitchLeak * (pulseWidth - 0.5f));
            return 1.0f / (1.0f / ideal + model::rampResetSeconds);
        }

        WaveWeights weightsFor (const OscSettings& osc, float shape) noexcept
        {
            return osc.switched ? WaveWeights::fromSwitches (osc.saw, osc.tri, osc.pulse) : WaveWeights::fromShape (shape);
        }
    }

    //==============================================================================
    void Voice::prepare (double newSampleRate, uint32_t seed)
    {
        sampleRate = newSampleRate;
        random.seed (seed * 2654435761u + 12345u);
        slopPitch1 = random.bipolar();
        slopPitch2 = random.bipolar();
        slopCutoff = random.bipolar();
        slopEnv = random.bipolar();
        slopScale = random.bipolar();
        warm1 = random.bipolar();
        warm2 = random.bipolar();
        warmCutoff = random.bipolar();
        skew1 = random.bipolar();
        skew2 = random.bipolar();
        for (auto& m : stageSlop)
            m = random.bipolar();
        oversample = 0;
        lastSkewVintage = -1.0f;
        HalfbandDecimator::coefficients(); // build the table off the audio thread
        lastEnvVintage = -1.0f;
        reset();
    }

    void Voice::reset()
    {
        active = gate = pendingStart = killing = sustained = false;
        fade = 1.0f;
        fadeStep = 0.0f;
        filterEnv.reset();
        ampEnv.reset();
        osc1.reset();
        osc2.reset();
        sub.reset();
        lowPass.reset();
        highPass.reset();
        decimator4.reset();
        decimator2.reset();
        feedPrimed = false;
        feedState = 0.0f;
    }

    void Voice::start (int note, float newVelocity, float detuneCents, float newGain, uint64_t newOrder)
    {
        startOrder = newOrder;
        sustained = false;
        killing = false;

        if (active && ampEnv.getLevel() > 1.0e-3f && targetNote != note)
        {
            // Steal: fade the old note out, then begin the new one.
            pendingStart = true;
            pendingNote = note;
            pendingVelocity = newVelocity;
            pendingDetune = detuneCents;
            pendingGain = newGain;
            fadeStep = -1.0f / std::max (1.0f, stealFadeSeconds * (float) sampleRate);
            gate = true;
            return;
        }

        pendingNote = note;
        pendingVelocity = newVelocity;
        pendingDetune = detuneCents;
        pendingGain = newGain;
        begin();
    }

    void Voice::begin()
    {
        const bool wasIdle = ! active;
        pendingStart = false;
        targetNote = pendingNote;
        target = (float) pendingNote;
        velocity = pendingVelocity;
        detune = pendingDetune;
        gain = pendingGain;
        fade = 1.0f;
        fadeStep = 0.0f;

        if (wasIdle)
        {
            // Free-running analog oscillators: a new note starts at an arbitrary phase.
            osc1.reset (0.5f * (random.bipolar() + 1.0f));
            osc2.reset (0.5f * (random.bipolar() + 1.0f));
            sub.reset();
            lowPass.reset();
            highPass.reset();
            decimator4.reset();
            decimator2.reset();
            feedPrimed = false;
            feedState = 0.0f;
            filterEnv.reset();
            ampEnv.reset();
        }

        active = true;
        gate = true;
        filterEnv.noteOn();
        ampEnv.noteOn();
    }

    void Voice::release (uint64_t newOrder)
    {
        if (! active)
            return;

        releaseOrder = newOrder;
        gate = false;
        sustained = false;

        if (pendingStart)
        {
            // Released during the steal fade: start it, already in release.
            begin();
            gate = false;
        }

        filterEnv.noteOff();
        ampEnv.noteOff();
    }

    void Voice::kill()
    {
        if (! active)
            return;

        pendingStart = false;
        killing = true;
        gate = false;
        fadeStep = -1.0f / std::max (1.0f, killFadeSeconds * (float) sampleRate);
    }

    void Voice::updateEnvelopes (const Settings& s)
    {
        const auto same = [] (const EnvSettings& a, const EnvSettings& b)
        {
            return juce::exactlyEqual (a.attack, b.attack) && juce::exactlyEqual (a.decay, b.decay) && juce::exactlyEqual (a.sustain, b.sustain) && juce::exactlyEqual (a.release, b.release);
        };

        if (juce::exactlyEqual (lastEnvVintage, s.vintage) && same (lastFilterEnv, s.filterEnv) && same (lastAmpEnv, s.ampEnv))
            return;

        lastEnvVintage = s.vintage;
        lastFilterEnv = s.filterEnv;
        lastAmpEnv = s.ampEnv;
        const auto scale = 1.0f + 0.12f * s.vintage * slopEnv; // vintage: +-12 % per-voice envelope times
        filterEnv.setTimes (sampleRate, s.filterEnv.attack * scale, s.filterEnv.decay * scale, s.filterEnv.sustain, s.filterEnv.release * scale);
        ampEnv.setTimes (sampleRate, s.ampEnv.attack * scale, s.ampEnv.decay * scale, s.ampEnv.sustain, s.ampEnv.release * scale);
    }

    void Voice::render (float* out, int numSamples, const SubBlock& b)
    {
        if (! active)
            return;

        const auto& s = *b.settings;
        updateEnvelopes (s);

        const auto sr = (float) sampleRate;
        const auto nyquistGuard = 0.45f * sr;
        const auto glideCoef = s.glide > 0.0005f ? 1.0f - std::exp (-1.0f / (s.glide * 0.33f * sr)) : 1.0f;

        // Oversampling (analog-reference.md, Blueprint): the filter always runs at 2x; Poly Mod
        // FM through the exponential input runs the whole voice at 4x.
        const bool pmFreq = s.pmodFreq1, pmShape = s.pmodShape1, pmFilter = s.pmodFilter;
        const bool polyModActive = s.pmodFiltEnv != 0.0f || s.pmodOsc2 != 0.0f;
        const int os = (pmFreq && polyModActive) ? model::oversamplePolyModFm : model::oversampleDefault;
        if (os != oversample || ! juce::exactlyEqual (lastSkewVintage, s.vintage))
        {
            if (os > oversample)
                decimator4.reset(); // its history is stale when 4x resumes
            oversample = os;
            lastSkewVintage = s.vintage;
            const auto rate = sampleRate * os;
            const auto skewDepth = model::triangleSkewFixed + model::triangleSkewVintage * s.vintage;
            osc1.configure (rate, 0.5f + skewDepth * skew1);
            osc2.configure (rate, 0.5f + skewDepth * skew2);
            float mismatch[4];
            for (int i = 0; i < 4; ++i)
                mismatch[i] = (model::stageMismatchFixed + model::stageMismatchVintage * s.vintage) * stageSlop[i];
            lowPass.setStageMismatch (mismatch);
        }
        lowPass.setModel (model::ladderModel (s.lpfModel));

        const auto osRate = sr * (float) os;
        const auto inverseOs = 1.0f / osRate;
        const auto piOverOs = pi * inverseOs;

        // Vintage drift: a new random target a few times a second, followed slowly.
        driftCountdown -= numSamples;
        if (driftCountdown <= 0)
        {
            driftCountdown = (int) (0.25f * sr);
            driftTarget1 = random.bipolar();
            driftTarget2 = random.bipolar();
        }
        const auto driftCoef = 1.0f - std::exp (-(float) numSamples / (0.6f * sr));
        drift1 += (driftTarget1 - drift1) * driftCoef;
        drift2 += (driftTarget2 - drift2) * driftCoef;

        // Per-voice variance (analog-reference.md, Oscillator): offset, drift, warm-up, scale.
        const auto v = s.vintage;
        const auto cents1 = (detune + v * (model::offsetCentsMax * slopPitch1 + model::driftCentsMax * drift1
                                           + model::warmupCentsMax * warm1 * b.warmup)) * 0.01f;
        const auto cents2 = (detune + v * (model::offsetCentsMax * slopPitch2 + model::driftCentsMax * drift2
                                           + model::warmupCentsMax * warm2 * b.warmup)) * 0.01f;
        const auto scale = 1.0f + v * model::scaleErrorMax * slopScale;
        const auto cutoffSlop = v * (model::cutoffOffsetOctaves * slopCutoff + model::cutoffWarmupOctaves * warmCutoff * b.warmup);

        const auto tune1 = (float) s.osc1.coarse + s.osc1.fine * 0.01f + cents1;
        const auto tune2 = (float) s.osc2.coarse + s.osc2.fine * 0.01f + cents2 + (s.osc2LowFreq ? lowFreqSemitones : 0.0f);

        const auto filterVel = 1.0f - s.filterEnv.velocity + s.filterEnv.velocity * velocity;
        const auto ampVel = 1.0f - s.ampEnv.velocity + s.ampEnv.velocity * velocity;
        const auto keyOctaves = s.lpfKeyTrack * ((float) targetNote - 60.0f) / 12.0f;
        const auto lpfStatic = b.lpfOctaves + keyOctaves + cutoffSlop + s.atFilter * b.aftertouch * aftertouchToCutoffOctaves;
        const auto hpfStatic = b.hpfOctaves + 0.5f * cutoffSlop;
        const auto k = model::ladderK (model::ladderModel (s.lpfModel), b.lpfReso);
        const auto drive = b.lpfDrive;
        const auto inverseDrive = 1.0f / drive;
        const auto feedCoef = std::exp (-2.0f * pi * model::feedthroughHighPassHz / sr);

        const bool lfoF1 = s.lfoFreq1, lfoF2 = s.lfoFreq2, lfoS1 = s.lfoShape1, lfoS2 = s.lfoShape2;
        const bool lfoLp = s.lfoLpf, lfoHp = s.lfoHpf, lfoAmp = s.lfoAmp;

        // Shape mod targets the shape knob in Variable mode and the pulse width in Switched mode.
        const auto staticWeights1 = weightsFor (s.osc1, b.shape1);
        const auto staticWeights2 = weightsFor (s.osc2, b.shape2);
        const bool shapeMod1 = lfoS1 || (pmShape && polyModActive);
        const bool polyModFilter = pmFilter && polyModActive;

        for (int i = 0; i < numSamples; ++i)
        {
            pitch += (target - pitch) * glideCoef;

            const auto fe = filterEnv.next() * filterVel;
            const auto ae = ampEnv.next();
            const auto lfo = b.lfo[i];
            const auto played = 60.0f + (pitch - 60.0f) * scale + b.bend;

            // Oscillator 2 (also the poly-mod source and the sync master): pitch per base sample.
            const auto note2 = (s.osc2KeyTrack ? played : osc2FixedNote + b.bend) + tune2 + (lfoF2 ? lfo * lfoToPitchSemitones : 0.0f);
            const auto shapeOffset2 = lfoS2 ? lfo * lfoToShape : 0.0f;
            const auto w2 = (lfoS2 && ! s.osc2.switched) ? WaveWeights::fromShape (clampShape (b.shape2 + shapeOffset2)) : staticWeights2;
            const auto pw2 = b.pw2 + (s.osc2.switched ? shapeOffset2 : 0.0f);
            const auto inc2 = oscillatorHz (note2, pw2) * inverseOs;

            const auto note1Base = played + tune1 + (lfoF1 ? lfo * lfoToPitchSemitones : 0.0f);
            const auto shapeBase1 = lfoS1 ? lfo * lfoToShape : 0.0f;

            // Filter cutoffs (exponential CV), per base sample unless Poly Mod moves the cutoff.
            const auto hpOct = hpfStatic + s.hpfEnvAmt * fe * envToCutoffOctaves + (lfoHp ? lfo * lfoToCutoffOctaves : 0.0f);
            const auto hpHz = std::clamp (std::exp2 (hpOct), 8.0f, nyquistGuard);
            highPass.setCoefficients (std::tan (hpHz * piOverOs), b.hpfReso);

            const auto lpOct = lpfStatic + s.lpfEnvAmt * fe * envToCutoffOctaves + (lfoLp ? lfo * lfoToCutoffOctaves : 0.0f);
            if (! polyModFilter)
                lowPass.setCoefficients (std::clamp (std::exp2 (lpOct), 8.0f, nyquistGuard) * piOverOs, k);

            float rendered[model::oversamplePolyModFm];
            float polyModLast = 0.0f;
            for (int j = 0; j < os; ++j)
            {
                float wrap2 = -1.0f;
                const auto o2 = osc2.process (inc2, pw2, w2, -1.0f, wrap2);
                const auto polyMod = s.pmodFiltEnv * fe + s.pmodOsc2 * o2;
                polyModLast = polyMod;

                // Oscillator 1: Poly Mod FM through the exponential input, exp2 per (oversampled) sample.
                const auto note1 = note1Base + (pmFreq ? polyMod * polyModToPitchSemitones : 0.0f);
                const auto shapeOffset1 = shapeBase1 + (pmShape ? polyMod * polyModToShape : 0.0f);
                const auto w1 = (shapeMod1 && ! s.osc1.switched) ? WaveWeights::fromShape (clampShape (b.shape1 + shapeOffset1)) : staticWeights1;
                const auto pw1 = b.pw1 + (s.osc1.switched ? shapeOffset1 : 0.0f);
                float wrap1 = -1.0f;
                const auto o1 = osc1.process (oscillatorHz (note1, pw1) * inverseOs, pw1, w1, s.sync ? wrap2 : -1.0f, wrap1);
                if (wrap1 >= 0.0f)
                    ++osc1Cycles;
                const auto subOut = sub.process (wrap1);
                const auto noiseOut = random.bipolar();

                const auto mixed = o1 * b.mix[0] + o2 * b.mix[1] + subOut * (0.7f * b.mix[2]) + noiseOut * (0.6f * b.mix[3] + model::thermalNoise);

                if (polyModFilter)
                {
                    const auto hz = std::clamp (std::exp2 (lpOct + polyMod * polyModToCutoffOctaves), 8.0f, nyquistGuard);
                    lowPass.setCoefficients (hz * piOverOs, k);
                }

                // 2-pole high-pass into the 4-pole ladder (order unconfirmed, see the docs).
                rendered[j] = lowPass.process (highPass.process (mixed) * drive) * inverseDrive;
            }

            float filtered;
            if (os == 4)
            {
                const auto first = decimator4.process (rendered[0], rendered[1]);
                const auto second = decimator4.process (rendered[2], rendered[3]);
                filtered = decimator2.process (first, second);
            }
            else
            {
                filtered = decimator2.process (rendered[0], rendered[1]);
            }

            // Cutoff / resonance CV feedthrough: a small high-passed term proportional to the CV.
            const auto cv = model::cutoffFeedthrough * (lpOct + (polyModFilter ? polyModLast * polyModToCutoffOctaves : 0.0f))
                          + model::resonanceFeedthrough * k;
            if (! feedPrimed) { lastCv = cv; feedPrimed = true; }
            feedState = feedCoef * (feedState + cv - lastCv);
            lastCv = cv;
            filtered += feedState;

            auto amp = ae * ampVel * gain * fade;
            if (lfoAmp)
                amp *= 1.0f - 0.5f * (b.lfoAmount - lfo);

            out[i] += filtered * amp;

            if (fadeStep != 0.0f)
            {
                fade += fadeStep;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    fadeStep = 0.0f;
                    if (pendingStart)
                    {
                        begin();
                    }
                    else if (killing)
                    {
                        reset();
                        return;
                    }
                }
            }

            if (! ampEnv.isActive() && ! pendingStart)
            {
                reset();
                return;
            }
        }
    }

    //==============================================================================
    SynthEngine::SynthEngine()
    {
        lfoRandom.seed (0x1234567u);
        noise.seed (0x7654321u);
    }

    void SynthEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;
        secondsSincePrepare = 0.0; // oscillators and filters "warm up" again (analog-reference.md)
        for (int i = 0; i < maxVoices; ++i)
            voices[(size_t) i].prepare (sampleRate, (uint32_t) i + 1);
        reset();
    }

    void SynthEngine::reset()
    {
        for (auto& voice : voices)
            voice.reset();
        stackSize = 0;
        sustainPedal = monoSustained = false;
        heldBySustain.fill (false);
        bendTarget = bend = 0.0f;
        aftertouch = 0.0f;
        polyPressure.fill (0.0f);
        lfoPhase = 0.0;
        lfoHeld = lfoSmoothed = 0.0f;
        smoothersPrimed = false;
    }

    void SynthEngine::setSettings (const Settings& newSettings)
    {
        settings = newSettings;
        settings.voices = std::clamp (settings.voices, 1, maxVoices);
        settings.unisonVoices = std::clamp (settings.unisonVoices, 1, maxVoices);

        if (settings.mode != lastMode)
        {
            // Changing the allocation scheme: release everything cleanly.
            for (auto& voice : voices)
                voice.kill();
            stackSize = 0;
            monoSustained = false;
            heldBySustain.fill (false);
            lastMode = settings.mode;
        }

        const auto inUse = voicesInUse();
        for (int i = inUse; i < maxVoices; ++i)
            voices[(size_t) i].kill();
    }

    int SynthEngine::voicesInUse() const noexcept
    {
        switch (settings.mode)
        {
            case VoiceMode::poly: return settings.voices;
            case VoiceMode::unison: return settings.unisonVoices;
            case VoiceMode::mono: return 1;
        }
        return settings.voices;
    }

    int SynthEngine::getNumActiveVoices() const noexcept
    {
        int count = 0;
        for (const auto& voice : voices)
            count += voice.isActive() ? 1 : 0;
        return count;
    }

    //==============================================================================
    void SynthEngine::noteOn (int note, float velocity)
    {
        note = std::clamp (note, 0, 127);
        heldBySustain[(size_t) note] = false;

        if (settings.mode != VoiceMode::poly)
        {
            startUnison (note, velocity);
            return;
        }

        const auto count = settings.voices;
        Voice* chosen = nullptr;

        // 1. The same note still sounding: retrigger it on its own voice.
        for (int i = 0; i < count && chosen == nullptr; ++i)
            if (voices[(size_t) i].isActive() && voices[(size_t) i].getNote() == note)
                chosen = &voices[(size_t) i];

        // 2. An idle voice: the one whose last note started longest ago (round robin).
        if (chosen == nullptr)
            for (int i = 0; i < count; ++i)
                if (auto& v = voices[(size_t) i]; ! v.isActive() && (chosen == nullptr || v.getStartOrder() < chosen->getStartOrder()))
                    chosen = &v;

        // 3. A releasing voice: the one released longest ago.
        if (chosen == nullptr)
            for (int i = 0; i < count; ++i)
                if (auto& v = voices[(size_t) i]; ! v.isGateOn() && (chosen == nullptr || v.getReleaseOrder() < chosen->getReleaseOrder()))
                    chosen = &v;

        // 4. Steal the oldest held note (sustain-pedal notes first).
        if (chosen == nullptr)
            for (int i = 0; i < count; ++i)
            {
                auto& v = voices[(size_t) i];
                if (chosen == nullptr || (v.sustained && ! chosen->sustained)
                    || (v.sustained == chosen->sustained && v.getStartOrder() < chosen->getStartOrder()))
                    chosen = &v;
            }

        if (chosen != nullptr)
            chosen->start (note, velocity, 0.0f, 1.0f, ++order);
    }

    void SynthEngine::startUnison (int note, float velocity)
    {
        removeFromStack (note);
        if (stackSize == (int) stack.size())
            removeFromStack (stack[0]);
        stack[(size_t) stackSize++] = note;

        const auto count = voicesInUse();
        bool legato = false;
        for (int i = 0; i < count; ++i)
            legato = legato || voices[(size_t) i].isGateOn();

        const auto gain = 1.0f / std::sqrt ((float) count);
        for (int i = 0; i < count; ++i)
        {
            auto& voice = voices[(size_t) i];
            const auto spread = count > 1 ? 2.0f * (float) i / (float) (count - 1) - 1.0f : 0.0f;
            if (legato && voice.isGateOn())
                voice.setNote (note);
            else
                voice.start (note, velocity, spread * settings.unisonDetune * maxUnisonDetuneCents, gain, ++order);
        }
        monoSustained = false;
    }

    void SynthEngine::removeFromStack (int note) noexcept
    {
        int w = 0;
        for (int r = 0; r < stackSize; ++r)
            if (stack[(size_t) r] != note)
                stack[(size_t) w++] = stack[(size_t) r];
        stackSize = w;
    }

    void SynthEngine::releaseVoice (Voice& voice)
    {
        voice.release (++order);
    }

    void SynthEngine::noteOff (int note)
    {
        note = std::clamp (note, 0, 127);

        if (settings.mode != VoiceMode::poly)
        {
            const bool wasTop = stackSize > 0 && stack[(size_t) stackSize - 1] == note;
            removeFromStack (note);
            const auto count = voicesInUse();

            if (stackSize > 0)
            {
                if (wasTop)
                    for (int i = 0; i < count; ++i)
                        if (voices[(size_t) i].isGateOn())
                            voices[(size_t) i].setNote (stack[(size_t) stackSize - 1]);
            }
            else if (sustainPedal)
            {
                monoSustained = true;
            }
            else
            {
                for (int i = 0; i < count; ++i)
                    if (voices[(size_t) i].isGateOn())
                        releaseVoice (voices[(size_t) i]);
            }
            return;
        }

        for (auto& voice : voices)
        {
            if (voice.isGateOn() && voice.getNote() == note && ! voice.sustained)
            {
                if (sustainPedal)
                    voice.sustained = true;
                else
                    releaseVoice (voice);
            }
        }
    }

    void SynthEngine::allNotesOff()
    {
        stackSize = 0;
        monoSustained = false;
        for (auto& voice : voices)
            if (voice.isGateOn())
                releaseVoice (voice);
    }

    void SynthEngine::allSoundOff()
    {
        stackSize = 0;
        monoSustained = false;
        for (auto& voice : voices)
            voice.kill();
    }

    void SynthEngine::handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        }
        else if (m.isNoteOff())
        {
            noteOff (m.getNoteNumber());
        }
        else if (m.isPitchWheel())
        {
            bendTarget = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        }
        else if (m.isChannelPressure())
        {
            aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        }
        else if (m.isAftertouch())
        {
            // Polyphonic pressure: the strongest key drives the (global) aftertouch.
            polyPressure[(size_t) m.getNoteNumber()] = (float) m.getAfterTouchValue() / 127.0f;
            float strongest = 0.0f;
            for (auto p : polyPressure)
                strongest = std::max (strongest, p);
            aftertouch = strongest;
        }
        else if (m.isController())
        {
            const auto cc = m.getControllerNumber();
            const auto value = m.getControllerValue();

            if (cc == 1)
            {
                modWheel = (float) value / 127.0f;
            }
            else if (cc == 64)
            {
                const bool down = value >= 64;
                if (sustainPedal && ! down)
                {
                    for (auto& voice : voices)
                        if (voice.sustained)
                            releaseVoice (voice);

                    if (monoSustained && stackSize == 0)
                        for (int i = 0; i < voicesInUse(); ++i)
                            if (voices[(size_t) i].isGateOn())
                                releaseVoice (voices[(size_t) i]);
                    monoSustained = false;
                }
                sustainPedal = down;
            }
            else if (cc == 120)
            {
                allSoundOff();
            }
            else if (cc == 123)
            {
                allNotesOff();
            }
        }
        else if (m.isAllNotesOff())
        {
            allNotesOff();
        }
        else if (m.isAllSoundOff())
        {
            allSoundOff();
        }
    }

    //==============================================================================
    void SynthEngine::updateLfoRate (const HostTime& time)
    {
        if (settings.lfoSync)
        {
            const auto bpm = time.bpm > 1.0 ? time.bpm : 120.0;
            const auto quarters = std::max (1.0e-3, settings.lfoSyncQuarters);
            lfoIncrement = bpm / 60.0 / quarters / sampleRate;

            if (time.playing && time.hasPpq)
            {
                const auto cycles = time.ppq / quarters;
                const auto newPhase = cycles - std::floor (cycles);
                if (settings.lfoWave == LfoWave::sampleHold && newPhase < lfoPhase - 0.5)
                    lfoHeld = lfoRandom.bipolar();
                lfoPhase = newPhase;
            }
        }
        else
        {
            lfoIncrement = (double) settings.lfoRate / sampleRate;
        }
    }

    float SynthEngine::nextLfo() noexcept
    {
        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0)
        {
            lfoPhase -= std::floor (lfoPhase);
            lfoHeld = lfoRandom.bipolar();
        }

        const auto p = (float) lfoPhase;
        float raw = 0.0f;
        switch (settings.lfoWave)
        {
            case LfoWave::triangle:   raw = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case LfoWave::saw:        raw = 2.0f * p - 1.0f; break;
            case LfoWave::reverseSaw: raw = 1.0f - 2.0f * p; break;
            case LfoWave::square:     raw = p < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::sampleHold: raw = lfoHeld; break;
        }

        // ~1 ms smoothing takes the click out of the square, saw and S&H steps.
        lfoSmoothed += (raw - lfoSmoothed) * (float) std::min (1.0, 1.0 / (0.001 * sampleRate));
        return lfoSmoothed;
    }

    void SynthEngine::process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const HostTime& time)
    {
        updateLfoRate (time);

        int position = 0;
        auto it = midi.cbegin();
        const auto end = midi.cend();

        while (position < numSamples)
        {
            while (it != end && (*it).samplePosition <= position)
            {
                handleMidi ((*it).getMessage());
                ++it;
            }

            auto next = numSamples;
            if (it != end)
                next = std::clamp ((*it).samplePosition, position + 1, numSamples);

            const auto n = std::min (subBlockSize, next - position);
            renderSubBlock (left + position, right + position, n);
            position += n;
        }

        for (; it != end; ++it)
            handleMidi ((*it).getMessage());

        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& voice = voices[(size_t) i];
            tap.notes[(size_t) i].store (voice.isActive() ? voice.getNote() : -1, std::memory_order_relaxed);
            tap.levels[(size_t) i].store (voice.getLevel(), std::memory_order_relaxed);
        }
        tap.lfo.store (block.lfoAmount > 0.0f ? lfoSmoothed : 0.0f, std::memory_order_relaxed);
    }

    void SynthEngine::renderSubBlock (float* left, float* right, int n)
    {
        const auto& s = settings;

        // Targets for the smoothed controls (~10 ms, stepped per sub-block).
        const float targets[] { std::log2 (std::max (1.0f, s.lpfCutoff)), s.lpfReso, 1.0f + 5.0f * s.lpfDrive * s.lpfDrive,
                                std::log2 (std::max (1.0f, s.hpfCutoff)), s.hpfReso,
                                s.mixOsc1, s.mixOsc2, s.mixSub, s.mixNoise,
                                s.osc1.shape, s.osc2.shape, s.osc1.pulseWidth, s.osc2.pulseWidth,
                                juce::Decibels::decibelsToGain (s.outputDb) };
        float* values[] { &block.lpfOctaves, &block.lpfReso, &block.lpfDrive, &block.hpfOctaves, &block.hpfReso,
                          &block.mix[0], &block.mix[1], &block.mix[2], &block.mix[3],
                          &block.shape1, &block.shape2, &block.pw1, &block.pw2, &outputGain };

        const auto coef = smoothersPrimed ? 1.0f - std::exp (-(float) n / (0.01f * (float) sampleRate)) : 1.0f;
        for (size_t i = 0; i < std::size (targets); ++i)
            *values[i] += (targets[i] - *values[i]) * coef;
        smoothersPrimed = true;

        bend += (bendTarget - bend) * (1.0f - std::exp (-(float) n / (0.005f * (float) sampleRate)));

        block.settings = &settings;
        block.sampleRate = sampleRate;
        block.bend = bend * (float) s.bendRange;
        block.aftertouch = aftertouch;
        block.warmup = (float) std::exp (-secondsSincePrepare / model::warmupSeconds);
        secondsSincePrepare += n / sampleRate;
        block.lfoAmount = std::clamp (s.lfoInitial + s.lfoWheel * modWheel + s.atLfo * aftertouch, 0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const auto lfo = nextLfo();
            const auto mixed = s.lfoNoiseMix > 0.0f ? lfo + (noise.bipolar() - lfo) * s.lfoNoiseMix : lfo;
            block.lfo[i] = mixed * block.lfoAmount;
        }

        std::fill (left, left + n, 0.0f);
        bool anyActive = false;
        for (auto& voice : voices)
        {
            if (voice.isActive())
            {
                voice.render (left, n, block);
                anyActive = true;
            }
        }

        const auto gain = voiceHeadroom * outputGain;
        for (int i = 0; i < n; ++i)
        {
            left[i] = anyActive ? left[i] * gain : 0.0f;
            right[i] = left[i];
        }
    }
}
