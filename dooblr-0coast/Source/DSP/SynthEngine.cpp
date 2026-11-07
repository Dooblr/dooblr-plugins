#include "SynthEngine.h"

#include <algorithm>

namespace dooblr0coast::dsp
{
    namespace
    {
        constexpr float twoPi = 6.28318530717958647692f;
        constexpr float voltScale = 8.0f; // 1.0 internal = 8 V

        float midiToHz (float note)
        {
            return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
        }

        float dbToGain (float db)
        {
            return std::pow (10.0f, db / 20.0f);
        }

        float clamp01 (float x) noexcept { return juce::jlimit (0.0f, 1.0f, x); }
    }

    float SynthEngine::AdaaFold::process (float x, float drive) noexcept
    {
        const auto a = juce::jmax (1.0e-4f, drive);
        const auto F = -std::cos (a * x) / a;
        const auto dx = x - x1;
        float y;
        if (std::abs (dx) < 1.0e-6f)
            y = std::sin (0.5f * a * (x + x1));
        else
            y = (F - F1) / dx;
        x1 = x;
        F1 = F;
        return juce::jlimit (-1.5f, 1.5f, y);
    }

    float SynthEngine::polyblep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            const auto x = t / dt;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            const auto x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    float SynthEngine::nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) (rng >> 8) * (1.0f / 16777215.0f);
    }

    void SynthEngine::prepare (double sr, int)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        invSr = 1.0f / (float) sampleRate;
        osInvSr = invSr / (float) oversample;

        const auto audio = (float) sampleRate;
        smPitch.setTime (audio, 0.010f);
        smFine.setTime (audio, 0.010f);
        smPort.setTime (audio, 0.010f);
        smLinFm.setTime (audio, 0.012f);
        smOvertone.setTime (audio, 0.012f);
        smOvertoneCv.setTime (audio, 0.012f);
        smMultiply.setTime (audio, 0.012f);
        smMultiplyCv.setTime (audio, 0.012f);
        smRise.setTime (audio, 0.015f);
        smFall.setTime (audio, 0.015f);
        smSlopeResp.setTime (audio, 0.015f);
        smOnset.setTime (audio, 0.015f);
        smSustain.setTime (audio, 0.012f);
        smDecay.setTime (audio, 0.015f);
        smContourResp.setTime (audio, 0.015f);
        smBalance.setTime (audio, 0.012f);
        smBalanceCv.setTime (audio, 0.012f);
        smDynamics.setTime (audio, 0.012f);
        smLevel.setTime (audio, 0.012f);
        smMath.setTime (audio, 0.012f);
        smOutput.setTime (audio, 0.010f);
        smLfoRate.setTime (audio, 0.020f);

        reset();
    }

    void SynthEngine::reset()
    {
        phase = 0.0f;
        triIntegrator = 0.0f;
        glideHz = 110.0f;
        lastHz = 110.0f;
        fmDc = {};
        osLpf = {};
        lpgLp = {};
        vactrol = {};
        levelDc = {};
        folder = {};
        slopeLevel = 0.0f;
        eocGate = 1.0f;
        slopeStage = 0;
        slopeTrigPrev = false;
        cycleWasOn = false;
        contourLevel = 0.0f;
        eonGate = 0.0f;
        contourStage = 0;
        contourGatePrev = false;
        clockPhase = 0.0f;
        clockGate = 0.0f;
        randomLevel = 0.5f;
        tempoInPrev = 0.0f;
        tappedPeriod = 0.5f;
        tapTimer = 0.0f;
        clockHigh = false;
        midiBCvLevel = 0.0f;
        midiBGateLevel = 0.0f;
        midiBLfoPhase = 0.0f;
        midiACvLevel = 0.0f;
        midiAGateLevel = 0.0f;
        modWheel = 0.0f;
        pitchBend = 0.0f;
        lastVelocity = 0.8f;
        lastDynCv = 0.0f;
        lastOut = 0.0f;
        mathOut = 0.0f;
        unprimeSmoothers();
        std::fill (outs, outs + outCount, 0.0f);
        heldNote = -1;
        heldCount = 0;
        arpCount = 0;
        arpStep = 0;
        arpPlaying = -1;
        arpWasOn = false;
        for (auto& s : inputSource)
            s.store (-1, std::memory_order_relaxed);
    }

    bool SynthEngine::connect (OutJack from, InJack to)
    {
        if ((int) from < 0 || (int) from >= outCount || (int) to < 0 || (int) to >= inCount)
            return false;
        inputSource[(size_t) to].store ((int8_t) from, std::memory_order_release);
        return true;
    }

    bool SynthEngine::disconnectInput (InJack to)
    {
        if ((int) to < 0 || (int) to >= inCount)
            return false;
        const auto prev = inputSource[(size_t) to].exchange (-1, std::memory_order_acq_rel);
        return prev >= 0;
    }

    void SynthEngine::clearCables()
    {
        for (auto& s : inputSource)
            s.store (-1, std::memory_order_relaxed);
    }

    int SynthEngine::numCables() const noexcept
    {
        int n = 0;
        for (auto& s : inputSource)
            if (s.load (std::memory_order_relaxed) >= 0)
                ++n;
        return n;
    }

    Cable SynthEngine::cableAt (int index) const noexcept
    {
        int n = 0;
        for (int i = 0; i < inCount; ++i)
        {
            const auto src = inputSource[(size_t) i].load (std::memory_order_relaxed);
            if (src < 0)
                continue;
            if (n == index)
                return { (OutJack) src, (InJack) i };
            ++n;
        }
        return {};
    }

    bool SynthEngine::inputIsPatched (InJack to) const noexcept
    {
        return sourceOf (to) >= 0;
    }

    int8_t SynthEngine::sourceOf (InJack to) const noexcept
    {
        if ((int) to < 0 || (int) to >= inCount)
            return -1;
        return inputSource[(size_t) to].load (std::memory_order_relaxed);
    }

    juce::ValueTree SynthEngine::cablesToTree() const
    {
        juce::ValueTree tree ("CABLES");
        for (int i = 0; i < inCount; ++i)
        {
            const auto src = inputSource[(size_t) i].load (std::memory_order_relaxed);
            if (src < 0)
                continue;
            juce::ValueTree child ("CABLE");
            child.setProperty ("from", src, nullptr);
            child.setProperty ("to", i, nullptr);
            tree.appendChild (child, nullptr);
        }
        return tree;
    }

    void SynthEngine::cablesFromTree (const juce::ValueTree& tree)
    {
        clearCables();
        auto root = tree;
        if (root.hasType ("CABLES") == false)
            root = tree.getChildWithName ("CABLES");
        if (! root.isValid())
            return;
        for (int i = 0; i < root.getNumChildren(); ++i)
        {
            const auto child = root.getChild (i);
            connect ((OutJack) (int) child.getProperty ("from", -1),
                     (InJack) (int) child.getProperty ("to", -1));
        }
    }

    Activity SynthEngine::takeActivity() noexcept
    {
        Activity a;
        a.midiA = actMidiA.exchange (0.0f);
        a.midiB = actMidiB.exchange (0.0f);
        a.tempo = actTempo.exchange (0.0f);
        a.slope = actSlope.load (std::memory_order_relaxed);
        a.multiply = actMultiply.load (std::memory_order_relaxed);
        a.contour = actContour.load (std::memory_order_relaxed);
        a.dynamics = actDynamics.load (std::memory_order_relaxed);
        a.eoc = actEoc.load (std::memory_order_relaxed);
        a.eon = actEon.load (std::memory_order_relaxed);
        return a;
    }

    void SynthEngine::copyScope (float* dest, int count) const noexcept
    {
        const auto n = juce::jlimit (1, scopeSize, count);
        const auto w = scopeWrite.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = scope[(size_t) ((w + i) % scopeSize)].load (std::memory_order_relaxed);
    }

    void SynthEngine::allNotesOff()
    {
        heldCount = 0;
        heldNote = -1;
        midiAGateLevel = 0.0f;
        if (settings.arpOn && settings.arpType == 0)
        {
            arpCount = 0;
            arpStep = 0;
            arpPlaying = -1;
        }
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        note = juce::jlimit (0, 127, note);
        velocity = clamp01 (velocity);
        lastVelocity = velocity;
        actMidiA.store (1.0f, std::memory_order_relaxed);

        if (settings.arpOn)
        {
            if (settings.arpType == 1)
            {
                for (int i = 0; i < arpCount; ++i)
                    if (arpNotes[(size_t) i] == note)
                    {
                        for (int j = i; j < arpCount - 1; ++j)
                        {
                            arpNotes[(size_t) j] = arpNotes[(size_t) j + 1];
                            arpVels[(size_t) j] = arpVels[(size_t) j + 1];
                        }
                        --arpCount;
                        if (arpCount > 0)
                            arpStep %= arpCount;
                        else
                        {
                            arpStep = 0;
                            arpPlaying = -1;
                            heldNote = -1;
                        }
                        return;
                    }
                if (arpCount >= maxHeld)
                {
                    for (int j = 0; j < arpCount - 1; ++j)
                    {
                        arpNotes[(size_t) j] = arpNotes[(size_t) j + 1];
                        arpVels[(size_t) j] = arpVels[(size_t) j + 1];
                    }
                    --arpCount;
                }
                arpNotes[(size_t) arpCount] = note;
                arpVels[(size_t) arpCount] = velocity;
                ++arpCount;
            }
            else
            {
                if (heldCount >= maxHeld)
                    return;
                heldNotes[(size_t) heldCount] = note;
                heldVels[(size_t) heldCount] = velocity;
                ++heldCount;
                arpNotes[(size_t) arpCount] = note;
                arpVels[(size_t) arpCount] = velocity;
                ++arpCount;
            }
            return;
        }

        const bool alreadyHeld = heldCount > 0;
        if (heldCount < maxHeld)
        {
            heldNotes[(size_t) heldCount] = note;
            heldVels[(size_t) heldCount] = velocity;
            ++heldCount;
        }
        heldNote = note;
        midiACvLevel = clamp01 ((float) note / (12.0f * voltScale));
        midiAGateLevel = 1.0f;
        if (! (settings.legato && alreadyHeld))
            retriggerContour();
    }

    void SynthEngine::noteOff (int note)
    {
        note = juce::jlimit (0, 127, note);

        if (settings.arpOn && settings.arpType == 0)
        {
            int idx = -1;
            for (int i = 0; i < heldCount; ++i)
                if (heldNotes[(size_t) i] == note)
                    idx = i;
            if (idx >= 0)
            {
                for (int j = idx; j < heldCount - 1; ++j)
                {
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    heldVels[(size_t) j] = heldVels[(size_t) j + 1];
                }
                --heldCount;
            }
            idx = -1;
            for (int i = 0; i < arpCount; ++i)
                if (arpNotes[(size_t) i] == note)
                    idx = i;
            if (idx >= 0)
            {
                for (int j = idx; j < arpCount - 1; ++j)
                {
                    arpNotes[(size_t) j] = arpNotes[(size_t) j + 1];
                    arpVels[(size_t) j] = arpVels[(size_t) j + 1];
                }
                --arpCount;
                if (arpCount > 0)
                    arpStep %= arpCount;
                else
                {
                    arpStep = 0;
                    arpPlaying = -1;
                    heldNote = -1;
                    midiAGateLevel = 0.0f;
                }
            }
            return;
        }

        if (settings.arpOn && settings.arpType == 1)
            return;

        int idx = -1;
        for (int i = 0; i < heldCount; ++i)
            if (heldNotes[(size_t) i] == note)
                idx = i;
        if (idx < 0)
            return;
        for (int j = idx; j < heldCount - 1; ++j)
        {
            heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
            heldVels[(size_t) j] = heldVels[(size_t) j + 1];
        }
        --heldCount;
        if (heldCount == 0)
        {
            heldNote = -1;
            midiAGateLevel = 0.0f;
        }
        else
        {
            heldNote = heldNotes[(size_t) heldCount - 1];
            lastVelocity = heldVels[(size_t) heldCount - 1];
            midiACvLevel = clamp01 ((float) heldNote / (12.0f * voltScale));
            midiAGateLevel = 1.0f;
            if (! settings.legato)
                retriggerContour();
        }
    }

    void SynthEngine::handleMidi (const juce::MidiMessage& message)
    {
        handleMidiRaw (message.getRawData(), message.getRawDataSize());
    }

    void SynthEngine::handleMidiRaw (const juce::uint8* data, int numBytes)
    {
        if (data == nullptr || numBytes <= 0)
            return;

        const auto status = (int) data[0];
        const auto type = status & 0xf0;
        const auto d1 = numBytes > 1 ? (int) data[1] : 0;
        const auto d2 = numBytes > 2 ? (int) data[2] : 0;

        if (type == 0x90)
        {
            if (d2 > 0)
                noteOn (d1, (float) d2 * (1.0f / 127.0f));
            else
                noteOff (d1);
        }
        else if (type == 0x80)
            noteOff (d1);
        else if (type == 0xe0)
            pitchBend = ((float) (d1 | (d2 << 7)) - 8192.0f) / 8192.0f;
        else if (type == 0xb0)
        {
            if (d1 == 1)
                modWheel = (float) d2 * (1.0f / 127.0f);
            else if (d1 == 120 || d1 == 123)
                allNotesOff();
        }
    }

    void SynthEngine::retriggerContour() noexcept
    {
        contourStage = 1;
        contourLevel = 0.0f;
        contourGatePrev = true;
        midiAGateLevel = 1.0f;
    }

    void SynthEngine::unprimeSmoothers() noexcept
    {
        Smooth* all[] {
            &smPitch, &smFine, &smPort, &smLinFm, &smOvertone, &smOvertoneCv,
            &smMultiply, &smMultiplyCv, &smRise, &smFall, &smSlopeResp, &smOnset,
            &smSustain, &smDecay, &smContourResp, &smBalance, &smBalanceCv,
            &smDynamics, &smLevel, &smMath, &smOutput, &smLfoRate
        };
        for (auto* s : all)
            s->unprime();
    }

    float SynthEngine::readInput (InJack jack, float normal) const noexcept
    {
        const auto src = sourceOf (jack);
        if (src < 0)
            return normal;
        return outs[src];
    }

    float SynthEngine::shapeRate (float level, float response) const noexcept
    {
        // Vari-Response: log (CCW) stretches the cycle, linear (noon) matches the
        // Rise/Fall/Onset/Decay times, expo (CW) compresses them.
        const auto v = clamp01 (level);
        const auto logShape = 1.35f - 0.70f * v;
        const auto expoShape = 0.50f + 0.95f * v;
        float shape = 1.0f;
        if (response < 0.5f)
            shape = logShape + (1.0f - logShape) * (response * 2.0f);
        else
            shape = 1.0f + (expoShape - 1.0f) * ((response - 0.5f) * 2.0f);
        const auto speed = 0.50f + response;
        return juce::jmax (0.12f, shape * speed);
    }

    void SynthEngine::tickClock (float dt, const HostTime& time, bool tempoPatched, float tempoIn, bool& clockEdge)
    {
        clockEdge = false;
        if (tempoPatched)
        {
            const bool high = tempoIn > 0.3f;
            if (high && tempoInPrev <= 0.3f)
            {
                if (tapTimer > 1.0e-3f && tapTimer < 4.0f)
                    tappedPeriod = tapTimer;
                tapTimer = 0.0f;
                clockEdge = true;
                randomLevel = nextRandom();
            }
            tempoInPrev = tempoIn;
            tapTimer += dt;
            clockGate = high ? 1.0f : 0.0f;
            if (clockEdge)
                actTempo.store (1.0f, std::memory_order_relaxed);
            return;
        }

        const auto bpm = time.bpm > 1.0 ? time.bpm : 120.0;
        static constexpr double quarters[] { 4.0, 2.0, 1.0, 0.5, 0.25 };
        const auto div = quarters[juce::jlimit (0, 4, settings.clockDiv)];
        const auto hz = (float) (bpm / 60.0 / div);
        clockPhase += hz * dt;
        while (clockPhase >= 1.0f)
        {
            clockPhase -= 1.0f;
            clockEdge = true;
            randomLevel = nextRandom();
            actTempo.store (1.0f, std::memory_order_relaxed);
        }
        clockGate = clockPhase < 0.5f ? 1.0f : 0.0f;
    }

    void SynthEngine::tickArp (bool clockEdge)
    {
        if (! settings.arpOn)
        {
            if (arpWasOn)
            {
                arpPlaying = -1;
                if (heldCount == 0)
                    midiAGateLevel = 0.0f;
            }
            arpWasOn = false;
            return;
        }
        arpWasOn = true;

        if (settings.arpType == 0)
        {
            arpCount = heldCount;
            for (int i = 0; i < heldCount; ++i)
            {
                arpNotes[(size_t) i] = heldNotes[(size_t) i];
                arpVels[(size_t) i] = heldVels[(size_t) i];
            }
        }

        if (arpCount <= 0)
        {
            midiAGateLevel = 0.0f;
            heldNote = -1;
            arpPlaying = -1;
            return;
        }

        const bool startNow = arpPlaying < 0;
        if (clockEdge || startNow)
        {
            // Do not wrap after incrementing: a second held note can arrive
            // after the first step, and wrapping while length==1 would replay note 0.
            if (startNow)
                arpStep = 0;
            arpStep %= arpCount;

            arpPlaying = arpNotes[(size_t) arpStep];
            lastVelocity = arpVels[(size_t) arpStep];
            heldNote = arpPlaying;
            midiACvLevel = clamp01 ((float) heldNote / (12.0f * voltScale));
            midiAGateLevel = 1.0f;
            retriggerContour();
            ++arpStep;
        }
        else
        {
            midiAGateLevel = 1.0f;
            heldNote = arpPlaying;
        }
    }

    void SynthEngine::tickMidiB (float dt)
    {
        const auto rate = smLfoRate.tick (settings.midiBLfoRate);
        midiBLfoPhase += rate * dt;
        midiBLfoPhase -= std::floor (midiBLfoPhase);
        const auto tri = 1.0f - 4.0f * std::abs (midiBLfoPhase - 0.5f); // -1..1
        const auto sqr = midiBLfoPhase < 0.5f ? 1.0f : 0.0f;
        const auto noteCv = heldNote >= 0 ? clamp01 ((float) heldNote / (12.0f * voltScale)) : 0.0f;

        switch (juce::jlimit (0, 3, settings.midiBCv))
        {
            case 0: midiBCvLevel = noteCv; break;
            case 1: midiBCvLevel = lastVelocity; break;
            case 2: midiBCvLevel = modWheel; break;
            default: midiBCvLevel = tri; break;
        }

        switch (juce::jlimit (0, 3, settings.midiBGate))
        {
            case 0: midiBGateLevel = midiAGateLevel; break;
            case 1: midiBGateLevel = lastVelocity > 0.5f ? 1.0f : 0.0f; break;
            case 2: midiBGateLevel = modWheel > 0.5f ? 1.0f : 0.0f; break;
            default: midiBGateLevel = sqr; break;
        }

        if (std::abs (midiBCvLevel) > 0.02f || midiBGateLevel > 0.5f)
            actMidiB.store (1.0f, std::memory_order_relaxed);
    }

    void SynthEngine::tickSlope (float dt, bool trig, float timeCv)
    {
        const auto rise = juce::jmax (0.0008f, smRise.tick (settings.slopeRise));
        const auto fall = juce::jmax (0.0008f, smFall.tick (settings.slopeFall));
        const auto resp = clamp01 (smSlopeResp.tick (settings.slopeResponse));
        const auto timeScale = sourceOf (InJack::SlopeTime) >= 0
                                   ? std::pow (8.0f, -clamp01 (timeCv))
                                   : 1.0f;

        const bool cycle = settings.slopeCycle;
        if (cycle && ! cycleWasOn && slopeStage == 0)
            slopeStage = 1;
        if (! cycle && cycleWasOn && slopeStage == 1 && slopeLevel <= 0.0f)
            slopeStage = 0;
        cycleWasOn = cycle;

        if (trig && ! slopeTrigPrev)
            slopeStage = 1;
        slopeTrigPrev = trig;

        if (slopeStage == 1)
        {
            slopeLevel += dt / (rise * timeScale) * shapeRate (slopeLevel, resp);
            if (slopeLevel >= 1.0f)
            {
                slopeLevel = 1.0f;
                slopeStage = 2;
            }
            eocGate = 1.0f;
        }
        else if (slopeStage == 2)
        {
            slopeLevel -= dt / (fall * timeScale) * shapeRate (slopeLevel, resp);
            if (slopeLevel <= 0.0f)
            {
                slopeLevel = 0.0f;
                slopeStage = cycle ? 1 : 0;
            }
            eocGate = 0.0f;
        }
        else
        {
            eocGate = 1.0f;
        }

        slopeLevel = clamp01 (slopeLevel);
        actSlope.store (slopeLevel, std::memory_order_relaxed);
        actEoc.store (eocGate, std::memory_order_relaxed);
    }

    void SynthEngine::tickContour (float dt, bool gate, float decayCv)
    {
        const auto onset = juce::jmax (0.0006f, smOnset.tick (settings.contourOnset));
        const auto sustain = clamp01 (smSustain.tick (settings.contourSustain));
        const auto decay = juce::jmax (0.0006f, smDecay.tick (settings.contourDecay));
        const auto resp = clamp01 (smContourResp.tick (settings.contourResponse));
        const auto decayScale = sourceOf (InJack::ContourDecay) >= 0
                                    ? std::pow (8.0f, -clamp01 (decayCv))
                                    : 1.0f;

        const bool oneShot = sustain < 0.01f;
        const bool rising = gate && ! contourGatePrev;
        contourGatePrev = gate;

        if (rising)
            contourStage = 1;

        if (contourStage == 1)
        {
            contourLevel += dt / onset * shapeRate (contourLevel, resp);
            if (contourLevel >= 1.0f)
            {
                contourLevel = 1.0f;
                if (oneShot)
                    contourStage = 4;
                else if (gate)
                    contourStage = 2;
                else
                    contourStage = 4;
            }
            eonGate = 0.0f;
        }
        else if (contourStage == 2)
        {
            if (! gate)
                contourStage = 4;
            else if (contourLevel > sustain)
            {
                contourLevel -= dt / (decay * decayScale) * shapeRate (contourLevel, resp);
                if (contourLevel <= sustain)
                {
                    contourLevel = sustain;
                    contourStage = 3;
                }
            }
            else
                contourStage = 3;
            eonGate = 1.0f;
        }
        else if (contourStage == 3)
        {
            contourLevel = sustain;
            if (! gate)
                contourStage = 4;
            eonGate = 1.0f;
        }
        else if (contourStage == 4)
        {
            contourLevel -= dt / (decay * decayScale) * shapeRate (contourLevel, resp);
            if (contourLevel <= 0.0f)
            {
                contourLevel = 0.0f;
                contourStage = 0;
                eonGate = 0.0f;
            }
            else
                eonGate = 1.0f;
        }
        else
        {
            contourLevel = 0.0f;
            eonGate = 0.0f;
        }

        contourLevel = clamp01 (contourLevel);
        actContour.store (contourLevel, std::memory_order_relaxed);
        actEon.store (eonGate, std::memory_order_relaxed);
    }

    void SynthEngine::tickSmoothers() noexcept
    {
        curPitch = smPitch.tick (settings.pitch);
        curFine = smFine.tick (settings.fine);
        curPort = smPort.tick (settings.portamento);
        curLinFm = clamp01 (smLinFm.tick (settings.linFm));
        curOvertone = clamp01 (smOvertone.tick (settings.overtone));
        curOvertoneCv = clamp01 (smOvertoneCv.tick (settings.overtoneCv));
        curMultiply = clamp01 (smMultiply.tick (settings.multiply));
        curMultiplyCv = juce::jlimit (-1.0f, 1.0f, smMultiplyCv.tick (settings.multiplyCv));
        curBalance = clamp01 (smBalance.tick (settings.balance));
        curBalanceCv = clamp01 (smBalanceCv.tick (settings.balanceCv));
        curDynamics = clamp01 (smDynamics.tick (settings.dynamics));
        curLevel = clamp01 (smLevel.tick (settings.level));
        curMath = juce::jlimit (-1.0f, 1.0f, smMath.tick (settings.mathAmt));
        curOutput = smOutput.tick (settings.outputDb);
    }

    void SynthEngine::tickVoice (float dtOs, float linFmIn, float overtoneCvIn, float multiplyCvIn, float slopeAudio,
                                 float& coreTri, float& coreSqr, float& folded)
    {
        const auto pitchOct = curPitch;
        const auto fineCt = curFine;
        const auto porta = curPort;
        const auto linAmt = curLinFm;
        const auto ov = curOvertone;
        const auto ovCvAmt = curOvertoneCv;
        const auto mul = curMultiply;
        const auto mulCv = curMultiplyCv;

        const auto note = heldNote >= 0 ? (float) heldNote : 48.0f;
        const auto bend = pitchBend * (float) settings.bendRange;
        const auto octIn = readInput (InJack::Osc1VOct, 0.0f) * voltScale;
        const auto targetNote = note + pitchOct * 12.0f + fineCt * 0.01f + bend + octIn * 12.0f;
        const auto targetHz = juce::jlimit (8.0f, 8000.0f, midiToHz (targetNote));

        if (porta <= 1.0e-4f || heldNote < 0)
            glideHz = targetHz;
        else
        {
            const auto coeff = 1.0f - std::exp (-dtOs / juce::jmax (1.0e-4f, porta));
            glideHz += (targetHz - glideHz) * coeff;
        }
        lastHz = glideHz;

        const auto fmAc = fmDc.processHigh (linFmIn, 1.0f - std::exp (-twoPi * 6.0f * dtOs));
        auto inc = glideHz * dtOs;
        inc += linAmt * fmAc * 0.22f;
        if (linAmt > 0.8f)
            inc *= 1.0f + (linAmt - 0.8f) * 1.4f * fmAc;
        inc = juce::jlimit (0.0f, 0.49f, inc);

        phase += inc;
        phase -= std::floor (phase);

        auto naiveSqr = phase < 0.5f ? 1.0f : -1.0f;
        naiveSqr += polyblep (phase, inc);
        naiveSqr -= polyblep (std::fmod (phase + 0.5f, 1.0f), inc);
        coreSqr = juce::jlimit (-1.2f, 1.2f, naiveSqr);

        triIntegrator += coreSqr * inc * 4.0f;
        triIntegrator *= 1.0f - 2.0f * inc;
        coreTri = juce::jlimit (-1.2f, 1.2f, triIntegrator);

        const auto ovMod = clamp01 (ov + ovCvAmt * overtoneCvIn);
        const auto t3 = (4.0f * coreTri * coreTri - 3.0f) * coreTri;
        const auto t2 = 2.0f * coreTri * coreTri - 1.0f;
        const auto odd = 0.72f * t3 + 0.28f * coreSqr;
        float harmonic;
        if (ovMod < 0.5f)
            harmonic = odd + (t2 - odd) * (ovMod * 2.0f);
        else
            harmonic = t2 + (coreTri * slopeAudio - t2) * ((ovMod - 0.5f) * 2.0f);

        const auto mulAmt = clamp01 (mul + mulCv * multiplyCvIn);
        actMultiply.store (mulAmt, std::memory_order_relaxed);
        // Pre-gain into the folder so Slope→Multiply CV is audible as cyclic AM,
        // not only a same-amplitude spectral change (ADAA sin(drive·x) is unity-gain).
        const auto pre = harmonic * (0.45f + mulAmt * 2.2f);
        const auto drive = 0.80f + mulAmt * 4.2f;
        folded = folder.process (pre, drive);
        folded *= 0.18f + 0.95f * mulAmt;
    }

    void SynthEngine::publishControlOutputs() noexcept
    {
        outs[(int) OutJack::MidiACv] = midiACvLevel;
        outs[(int) OutJack::MidiAGate] = midiAGateLevel;
        outs[(int) OutJack::MidiBCv] = midiBCvLevel;
        outs[(int) OutJack::MidiBGate] = midiBGateLevel;
        outs[(int) OutJack::Clock] = clockGate;
        outs[(int) OutJack::Random] = randomLevel;
        outs[(int) OutJack::Math1] = mathOut;
        outs[(int) OutJack::Math2] = mathOut;
        outs[(int) OutJack::SlopeEoc] = eocGate;
        outs[(int) OutJack::SlopeCv] = slopeLevel;
        outs[(int) OutJack::ContourEon] = eonGate;
        outs[(int) OutJack::ContourCv] = contourLevel;
    }

    void SynthEngine::publishAudioOutputs (float tri, float sqr, float audio) noexcept
    {
        outs[(int) OutJack::OscTri] = tri;
        outs[(int) OutJack::OscSqr] = sqr;
        outs[(int) OutJack::Dynamics] = audio;
    }

    void SynthEngine::process (float* left, float* right, int numSamples, juce::MidiBuffer& midi, const HostTime& time)
    {
        struct Event { int sample; const juce::uint8* data; int numBytes; };
        Event events[64];
        int nEvents = 0;
        for (const auto metadata : midi)
        {
            if (nEvents >= 64)
                break;
            events[nEvents++] = { metadata.samplePosition, metadata.data, metadata.numBytes };
        }
        int eventIndex = 0;

        const float osCoeff = 1.0f - std::exp (-twoPi * 0.42f * (float) sampleRate * osInvSr);

        for (int i = 0; i < numSamples; ++i)
        {
            while (eventIndex < nEvents && events[eventIndex].sample <= i)
            {
                handleMidiRaw (events[eventIndex].data, events[eventIndex].numBytes);
                ++eventIndex;
            }

            const auto tempoIn = readInput (InJack::Tempo, 0.0f);
            bool clockEdge = false;
            tickClock (invSr, time, sourceOf (InJack::Tempo) >= 0, tempoIn, clockEdge);
            tickArp (clockEdge);
            tickMidiB (invSr);

            tickSmoothers();

            const auto mathCh1 = readInput (InJack::MathCh1, 0.0f);
            const auto mathCh2 = readInput (InJack::MathCh2, curMath);
            const auto mathCh2Scaled = sourceOf (InJack::MathCh2) >= 0 ? mathCh2 * curMath : mathCh2;
            mathOut = juce::jlimit (-2.0f, 2.0f, mathCh1 + mathCh2Scaled);

            // Slope/contour run at audio rate; the voice at 2x.
            const auto slopeTrig = readInput (InJack::SlopeTrig, 0.0f) > 0.3f;
            const auto slopeTime = readInput (InJack::SlopeTime, 0.0f);
            tickSlope (invSr, slopeTrig, slopeTime);

            const auto contourGateNormal = midiAGateLevel > 0.5f;
            const auto contourGate = readInput (InJack::ContourGate, contourGateNormal ? 1.0f : 0.0f) > 0.3f;
            const auto decayCv = readInput (InJack::ContourDecay, 0.0f);
            tickContour (invSr, contourGate, decayCv);

            publishControlOutputs();

            float accTri = 0.0f, accSqr = 0.0f, accFold = 0.0f;
            for (int os = 0; os < oversample; ++os)
            {
                const auto linFmIn = readInput (InJack::OscLinFm, 0.0f);
                const auto ovCvIn = readInput (InJack::OvertoneCv, 0.0f);
                const auto mulCvIn = readInput (InJack::MultiplyCv, slopeLevel);
                float tri = 0.0f, sqr = 0.0f, fold = 0.0f;
                tickVoice (osInvSr, linFmIn, ovCvIn, mulCvIn, slopeLevel * 2.0f - 1.0f, tri, sqr, fold);
                accTri += tri;
                accSqr += sqr;
                accFold += fold;
            }
            const auto coreTri = accTri * (1.0f / (float) oversample);
            const auto coreSqr = accSqr * (1.0f / (float) oversample);
            const auto folded = osLpf.processLow (accFold * (1.0f / (float) oversample), osCoeff);

            const auto balanceKnob = curBalance;
            const auto balanceAmt = curBalanceCv;
            const auto fund = readInput (InJack::BalanceExt, coreTri);
            float balancePos;
            if (sourceOf (InJack::BalanceCv) >= 0)
                balancePos = clamp01 (readInput (InJack::BalanceCv, 0.0f) * balanceAmt);
            else
                balancePos = balanceKnob;
            const auto mixed = fund + (folded - fund) * balancePos;

            const auto dynAmt = curDynamics;
            const auto dynNormal = contourLevel;
            const auto dynCv = clamp01 (readInput (InJack::DynamicsCv, dynNormal) * dynAmt);
            lastDynCv = dynCv;
            // Transistor LPG: faster than a vactrol, still lagged. Attack ~4 ms, release ~45 ms.
            const auto vactCoeff = dynCv > vactrol.z
                                       ? 1.0f - std::exp (-1.0f / ((float) sampleRate * 0.004f))
                                       : 1.0f - std::exp (-1.0f / ((float) sampleRate * 0.045f));
            const auto cv = vactrol.processLow (dynCv, vactCoeff);
            const auto cutoff = 40.0f * std::pow (18000.0f / 40.0f, std::pow (cv, 1.35f));
            const auto lpCoeff = 1.0f - std::exp (-twoPi * cutoff * invSr);
            const auto filtered = lpgLp.processLow (mixed, lpCoeff);
            const auto vca = std::pow (cv, 1.45f);
            auto voice = filtered * vca;
            actDynamics.store (cv, std::memory_order_relaxed);

            auto levelAmt = curLevel;
            auto gain = levelAmt / 0.80f;
            voice *= gain;
            if (levelAmt > 0.80f)
            {
                const auto extra = (levelAmt - 0.80f) / 0.20f;
                voice = std::tanh (voice * (1.0f + extra * 2.8f)) * (1.0f + extra * 0.15f);
            }

            const auto outGain = dbToGain (curOutput);
            voice *= outGain * 0.22f;
            voice = juce::jlimit (-1.5f, 1.5f, flush (voice));
            lastOut = voice;

            publishAudioOutputs (coreTri, coreSqr, voice);

            left[i] = voice;
            right[i] = voice;

            const auto w = scopeWrite.load (std::memory_order_relaxed);
            scope[(size_t) w].store (voice, std::memory_order_relaxed);
            scopeWrite.store ((w + 1) % scopeSize, std::memory_order_release);
        }
    }
}
