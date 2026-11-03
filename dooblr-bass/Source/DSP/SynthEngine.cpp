#include "SynthEngine.h"

#include <cmath>

namespace dooblrbass::dsp
{
    namespace
    {
        constexpr float killFadeSeconds = 0.006f;
        constexpr float envToCutoffOctaves = 7.0f;
        constexpr float lfoToPitchSemitones = 12.0f;
        constexpr float lfoToCutoffOctaves = 4.0f;
        constexpr float osc3FixedNote = 36.0f;     // key track off: C2
        constexpr float lowFreqSemitones = -72.0f; // lo-freq: six octaves down
        constexpr float voiceHeadroom = 0.42f;
        constexpr float smoothSeconds = 0.012f;

        int footage (int octaveIndex) noexcept
        {
            static constexpr int st[] { -24, -12, 0, 12 };
            return st[std::clamp (octaveIndex, 0, 3)];
        }

        WaveWeights weightsFor (Wave wave) noexcept
        {
            return wave == Wave::square ? WaveWeights::forSquare() : WaveWeights::forSaw();
        }

        WaveWeights weightsFor (Osc3Wave wave) noexcept
        {
            switch (wave)
            {
                case Osc3Wave::triangle: return WaveWeights::forTriangle();
                case Osc3Wave::square:   return WaveWeights::forSquare();
                case Osc3Wave::saw:      return WaveWeights::forSaw();
            }
            return WaveWeights::forSaw();
        }

        /** Expo converter + analog high-frequency flattening: f_eff = 1 / (1/f + t_r). */
        float oscillatorHz (float note) noexcept
        {
            const auto ideal = model::expoHz (note);
            return 1.0f / (1.0f / std::max (ideal, 0.01f) + model::rampResetSeconds);
        }

        float smoothCoef (double sampleRate) noexcept
        {
            return (float) (1.0 - std::exp (-1.0 / (smoothSeconds * sampleRate)));
        }

        float lagCoef (double sampleRate, float seconds) noexcept
        {
            return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-5, (double) seconds) * sampleRate)));
        }
    }

    //==============================================================================
    void Voice::prepare (double newSampleRate, uint32_t seed)
    {
        sampleRate = newSampleRate;
        noise.seed (seed * 2654435761u + 12345u);
        HalfbandDecimator::coefficients();
        lowPass.prepare();
        envTimesPrimed = false;
        reset();
    }

    void Voice::reset()
    {
        active = gate = killing = sustained = false;
        fade = 1.0f;
        fadeStep = 0.0f;
        lastFilt = 0.0f;
        filterEnv.reset();
        ampEnv.reset();
        osc1.reset (0.0f);
        osc2.reset (0.17f);
        osc3.reset (0.41f);
        sub.reset();
        lowPass.reset();
        decimator.reset();
        vca.reset();
        cutoffCv.reset();
        pitchExpo.reset();
        envTimesPrimed = false;
    }

    void Voice::start (int note, float newVelocity, bool retrigger)
    {
        targetNote = note;
        target = (float) note;
        velocity = std::clamp (newVelocity, 0.0f, 1.0f);
        ampVel = 0.35f + 0.65f * velocity;
        sustained = false;
        killing = false;
        begin (retrigger);
    }

    void Voice::setNote (int note) noexcept
    {
        targetNote = note;
        target = (float) note;
    }

    void Voice::begin (bool retrigger)
    {
        const bool wasIdle = ! active;
        active = true;
        gate = true;
        fade = 1.0f;
        fadeStep = 0.0f;

        if (wasIdle)
            pitch = target;

        if (wasIdle || retrigger)
        {
            filterEnv.noteOn();
            ampEnv.noteOn();
        }
    }

    void Voice::release()
    {
        gate = false;
        sustained = false;
        filterEnv.noteOff();
        ampEnv.noteOff();
    }

    void Voice::kill()
    {
        if (! active)
            return;

        gate = false;
        sustained = false;
        killing = true;
        fadeStep = -1.0f / std::max (1.0f, killFadeSeconds * (float) sampleRate);
    }

    void Voice::render (float* out, int numSamples, const SubBlock& block)
    {
        if (! active)
            return;

        const auto& s = *block.settings;
        const auto oversampledRate = sampleRate * (double) model::oversample;
        const auto incScale = 1.0f / (float) oversampledRate;

        auto filterTimes = s.filterEnv;
        auto ampTimes = s.ampEnv;
        if (s.sharedDecay)
        {
            filterTimes.release = filterTimes.decay;
            ampTimes.release = ampTimes.decay;
        }

        const auto envChanged = [] (const EnvSettings& a, const EnvSettings& b)
        {
            return ! juce::exactlyEqual (a.attack, b.attack) || ! juce::exactlyEqual (a.decay, b.decay)
                || ! juce::exactlyEqual (a.sustain, b.sustain) || ! juce::exactlyEqual (a.release, b.release);
        };

        if (! envTimesPrimed || envChanged (filterTimes, lastFilterEnv) || envChanged (ampTimes, lastAmpEnv))
        {
            filterEnv.setTimes (sampleRate, filterTimes.attack, filterTimes.decay, filterTimes.sustain, filterTimes.release);
            ampEnv.setTimes (sampleRate, ampTimes.attack, ampTimes.decay, ampTimes.sustain, ampTimes.release);
            lastFilterEnv = filterTimes;
            lastAmpEnv = ampTimes;
            envTimesPrimed = true;
        }

        const auto glideSeconds = std::max (0.0f, s.glide);
        const auto glideCoef = glideSeconds <= 1.0e-4f ? 1.0f
                                                       : (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, (double) glideSeconds) * sampleRate)));
        const auto pitchExpoCoef = lagCoef (sampleRate, model::pitchExpoSeconds);
        const auto cutoffExpoCoef = lagCoef (sampleRate, model::cutoffExpoSeconds);
        const auto vcaCoef = lagCoef (sampleRate, model::vcaExpoSeconds);

        const auto w1 = weightsFor (s.osc1Wave);
        const auto w2 = weightsFor (s.osc2Wave);
        const auto w3 = weightsFor (s.osc3Wave);
        const auto twoOctaves = s.subOctave != 0;
        const auto filtVelAmt = s.filterEnv.velocity * velocity + (1.0f - s.filterEnv.velocity);

        for (int i = 0; i < numSamples; ++i)
        {
            pitch += (target - pitch) * glideCoef;
            const auto fenv = filterEnv.next() * filtVelAmt;
            const auto aenv = ampEnv.next();
            const auto lfo = block.lfo[i];

            // Pitch CV after the glide capacitor, then the expo-converter lag.
            const auto note = pitchExpo.process (pitch + block.bend + lfo * block.lfoPitchAmt * lfoToPitchSemitones,
                                                 pitchExpoCoef);
            const auto n1 = note + (float) footage (s.osc1Octave);
            const auto n2 = note + (float) footage (s.osc2Octave) + s.osc2Tune;
            auto n3 = (s.osc3KeyTrack ? note : osc3FixedNote) + (float) footage (s.osc3Octave) + s.osc3Tune;
            if (s.osc3LoFreq)
                n3 += lowFreqSemitones;

            const auto inc1 = oscillatorHz (n1) * incScale;
            const auto inc2 = oscillatorHz (n2) * incScale;
            const auto inc3 = oscillatorHz (n3) * incScale;

            const auto cutoffTarget = 69.0f + block.lpfOctaves * 12.0f
                                      + s.lpfKeyTrack * (pitch - 60.0f)
                                      + s.lpfEnvAmt * fenv * envToCutoffOctaves * 12.0f
                                      + lfo * block.lfoFiltAmt * lfoToCutoffOctaves * 12.0f;
            const auto cutoffNote = cutoffCv.process (cutoffTarget, cutoffExpoCoef);
            const auto fc = model::expoHz (cutoffNote);
            const auto w = (float) (pi * std::clamp (fc, 8.0f, 0.45f * (float) oversampledRate) / oversampledRate);
            lowPass.setCoefficients (w, model::ladderK (block.lpfReso));

            float oversampled[2];
            for (int u = 0; u < model::oversample; ++u)
            {
                float wrap1 = -1.0f, wrap2 = -1.0f, wrap3 = -1.0f;
                const auto o1 = osc1.process (inc1, block.pw1, w1, wrap1);
                const auto o2 = osc2.process (inc2, block.pw2, w2, wrap2);
                const auto o3 = osc3.process (inc3, 0.5f, w3, wrap3);
                juce::ignoreUnused (wrap2, wrap3);
                const auto subOut = sub.process (wrap1, twoOctaves);
                const auto nz = noise.bipolar();

                // Virtual-earth mixer: channels + filter-output overload into one saturating summer.
                const auto sum = o1 * block.mix[0] + o2 * block.mix[1] + o3 * block.mix[2]
                               + subOut * block.mix[3] + nz * block.mix[4]
                               + lastFilt * block.overload;
                const auto mix = model::virtualEarth (sum, block.drive) + model::thermalNoise * noise.bipolar();
                lastFilt = lowPass.process (mix);
                oversampled[u] = lastFilt;
            }

            auto y = decimator.process (oversampled[0], oversampled[1]);
            y = vca.process (y, aenv * ampVel * fade * block.outputGain * voiceHeadroom, vcaCoef);

            if (fadeStep != 0.0f)
            {
                fade += fadeStep;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    active = gate = killing = false;
                    filterEnv.reset();
                    ampEnv.reset();
                    lowPass.reset();
                    vca.reset();
                    lastFilt = 0.0f;
                    out[i] += y;
                    return;
                }
            }

            out[i] += y;

            if (! killing && ! ampEnv.isActive() && ! filterEnv.isActive())
            {
                active = false;
                lastFilt = 0.0f;
            }
        }
    }

    //==============================================================================
    void SynthEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;
        voice.prepare (sampleRate, 0xC0FFEEu);
        lfoRandom.seed (0xA5A5A5A5u);
        HalfbandDecimator::coefficients();
        smoothersPrimed = false;
        reset();
    }

    void SynthEngine::reset()
    {
        voice.reset();
        stackSize = 0;
        currentNote = -1;
        sustainPedal = false;
        heldBySustain.fill (false);
        bendTarget = bend = 0.0f;
        lfoPhase = 0.0;
        lfoHeld = lfoSmoothed = 0.0f;
        smoothersPrimed = false;
        tap.note = -1;
        tap.level = 0.0f;
        tap.lfo = 0.0f;
    }

    void SynthEngine::setSettings (const Settings& newSettings) { settings = newSettings; }

    void SynthEngine::updateLfoRate (const HostTime& time)
    {
        double hz = (double) settings.lfoRate;
        if (settings.lfoSync && time.bpm > 1.0)
            hz = (time.bpm / 60.0) / std::max (1.0e-6, settings.lfoSyncQuarters);
        lfoIncrement = hz / sampleRate;
    }

    float SynthEngine::nextLfo() noexcept
    {
        lfoPhase += lfoIncrement;
        bool wrapped = false;
        if (lfoPhase >= 1.0)
        {
            lfoPhase -= std::floor (lfoPhase);
            wrapped = true;
        }

        float raw = 0.0f;
        const auto p = (float) lfoPhase;
        switch (settings.lfoWave)
        {
            case LfoWave::triangle:    raw = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
            case LfoWave::saw:         raw = 2.0f * p - 1.0f; break;
            case LfoWave::reverseSaw:  raw = 1.0f - 2.0f * p; break;
            case LfoWave::square:      raw = p < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::sampleHold:
                if (wrapped || lfoIncrement <= 0.0)
                    lfoHeld = lfoRandom.bipolar();
                raw = lfoHeld;
                break;
        }

        // Soften square / S&H edges so the destinations don't click.
        const auto coef = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
        lfoSmoothed += (raw - lfoSmoothed) * coef;
        return lfoSmoothed;
    }

    int SynthEngine::chooseNote() const noexcept
    {
        if (stackSize <= 0)
            return -1;

        if (settings.notePriority == NotePriority::low)
        {
            int best = stack[0].note;
            for (int i = 1; i < stackSize; ++i)
                if (stack[(size_t) i].note < best)
                    best = stack[(size_t) i].note;
            return best;
        }

        int best = stack[0].note;
        uint64_t newest = stack[0].order;
        for (int i = 1; i < stackSize; ++i)
            if (stack[(size_t) i].order >= newest)
            {
                newest = stack[(size_t) i].order;
                best = stack[(size_t) i].note;
            }
        return best;
    }

    void SynthEngine::removeFromStack (int note) noexcept
    {
        int w = 0;
        for (int i = 0; i < stackSize; ++i)
            if (stack[(size_t) i].note != note)
                stack[(size_t) w++] = stack[(size_t) i];
        stackSize = w;
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        note = juce::jlimit (0, 127, note);
        removeFromStack (note);
        if (stackSize < (int) stack.size())
            stack[(size_t) stackSize++] = { note, ++order };
        heldBySustain[(size_t) note] = false;

        const bool hadGate = voice.isGateOn();
        const bool retrigger = ! settings.legato || ! hadGate;
        currentNote = chooseNote();

        if (hadGate && settings.legato)
            voice.setNote (currentNote);
        else
            voice.start (currentNote, velocity, retrigger);
    }

    void SynthEngine::noteOff (int note)
    {
        note = juce::jlimit (0, 127, note);

        if (sustainPedal)
        {
            heldBySustain[(size_t) note] = true;
            return;
        }

        removeFromStack (note);
        const auto next = chooseNote();
        currentNote = next;

        if (next < 0)
        {
            if (voice.isGateOn())
                voice.release();
            return;
        }

        if (next != voice.getNote())
        {
            if (settings.legato && voice.isGateOn())
                voice.setNote (next);
            else
                voice.start (next, 0.8f, true);
        }
    }

    void SynthEngine::allNotesOff()
    {
        stackSize = 0;
        currentNote = -1;
        heldBySustain.fill (false);
        if (voice.isGateOn())
            voice.release();
    }

    void SynthEngine::allSoundOff()
    {
        stackSize = 0;
        currentNote = -1;
        heldBySustain.fill (false);
        sustainPedal = false;
        voice.kill();
    }

    void SynthEngine::handleMidi (const juce::MidiMessage& message)
    {
        if (message.isNoteOn())
            noteOn (message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            noteOff (message.getNoteNumber());
        else if (message.isAllNotesOff())
            allNotesOff();
        else if (message.isAllSoundOff())
            allSoundOff();
        else if (message.isPitchWheel())
            bendTarget = ((float) message.getPitchWheelValue() - 8192.0f) * (1.0f / 8192.0f) * (float) settings.bendRange;
        else if (message.isController())
        {
            if (message.getControllerNumber() == 1)
                modWheel = (float) message.getControllerValue() * (1.0f / 127.0f);
            else if (message.getControllerNumber() == 64)
            {
                const bool down = message.getControllerValue() >= 64;
                if (sustainPedal && ! down)
                {
                    sustainPedal = false;
                    for (int n = 0; n < 128; ++n)
                        if (heldBySustain[(size_t) n])
                        {
                            heldBySustain[(size_t) n] = false;
                            noteOff (n);
                        }
                }
                else
                {
                    sustainPedal = down;
                }
            }
        }
    }

    void SynthEngine::renderSubBlock (float* left, float* right, int numSamples)
    {
        const auto coef = smoothCoef (sampleRate);
        const auto targetMix = std::array<float, 5> { settings.mixOsc1, settings.mixOsc2, settings.mixOsc3,
                                                      settings.mixSub, settings.mixNoise };
        const auto targetDrive = settings.mixDrive;
        const auto targetOverload = settings.mixOverload;
        const auto targetOctaves = std::log2 (std::max (settings.lpfCutoff, 8.0f) / 440.0f);
        const auto targetReso = settings.lpfReso;
        const auto targetGain = juce::Decibels::decibelsToGain (settings.outputDb);
        const auto targetPw1 = settings.osc1PW, targetPw2 = settings.osc2PW;

        if (! smoothersPrimed)
        {
            for (int i = 0; i < 5; ++i)
                block.mix[i] = targetMix[(size_t) i];
            block.drive = targetDrive;
            block.overload = targetOverload;
            block.lpfOctaves = targetOctaves;
            block.lpfReso = targetReso;
            outputGain = targetGain;
            block.pw1 = targetPw1;
            block.pw2 = targetPw2;
            bend = bendTarget;
            smoothersPrimed = true;
        }

        auto mix = std::array<float, 5> { block.mix[0], block.mix[1], block.mix[2], block.mix[3], block.mix[4] };
        auto drive = block.drive, overload = block.overload, octaves = block.lpfOctaves, reso = block.lpfReso;
        auto gain = outputGain, pw1 = block.pw1, pw2 = block.pw2, b = bend;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int m = 0; m < 5; ++m)
                mix[(size_t) m] += (targetMix[(size_t) m] - mix[(size_t) m]) * coef;
            drive += (targetDrive - drive) * coef;
            overload += (targetOverload - overload) * coef;
            octaves += (targetOctaves - octaves) * coef;
            reso += (targetReso - reso) * coef;
            gain += (targetGain - gain) * coef;
            pw1 += (targetPw1 - pw1) * coef;
            pw2 += (targetPw2 - pw2) * coef;
            b += (bendTarget - b) * coef;

            block.lfo[i] = nextLfo();
        }

        for (int m = 0; m < 5; ++m)
            block.mix[m] = mix[(size_t) m];
        block.drive = drive;
        block.overload = overload;
        block.lpfOctaves = octaves;
        block.lpfReso = reso;
        outputGain = gain;
        block.pw1 = pw1;
        block.pw2 = pw2;
        bend = b;
        block.outputGain = outputGain;
        block.bend = bend;
        block.lfoPitchAmt = settings.lfoToPitch + settings.lfoWheel * modWheel;
        block.lfoFiltAmt = settings.lfoToFilter;
        block.settings = &settings;
        block.sampleRate = sampleRate;

        voice.render (left, numSamples, block);

        for (int i = 0; i < numSamples; ++i)
            right[i] = left[i];
    }

    void SynthEngine::process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const HostTime& time)
    {
        std::fill (left, left + numSamples, 0.0f);
        if (right != left)
            std::fill (right, right + numSamples, 0.0f);

        updateLfoRate (time);

        int start = 0;
        for (const auto metadata : midi)
        {
            const auto pos = juce::jlimit (0, numSamples, metadata.samplePosition);
            while (start < pos)
            {
                const auto n = std::min (subBlockSize, pos - start);
                renderSubBlock (left + start, right + start, n);
                start += n;
            }
            handleMidi (metadata.getMessage());
        }

        while (start < numSamples)
        {
            const auto n = std::min (subBlockSize, numSamples - start);
            renderSubBlock (left + start, right + start, n);
            start += n;
        }

        tap.note.store (voice.isGateOn() ? voice.getNote() : -1, std::memory_order_relaxed);
        tap.level.store (voice.getLevel(), std::memory_order_relaxed);
        tap.lfo.store (block.lfo[0], std::memory_order_relaxed);
        tap.cutoffHz.store (440.0f * std::exp2 (block.lpfOctaves), std::memory_order_relaxed);
    }
}
