#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "Patch.h"

namespace dooblr0coast::dsp
{
    struct Settings
    {
        float pitch = 0.0f;
        float fine = 0.0f;
        float portamento = 0.0f;
        float linFm = 0.0f;
        float overtone = 0.12f;
        float overtoneCv = 0.0f;
        float multiply = 0.18f;
        float multiplyCv = 0.0f;
        bool  slopeCycle = false;
        float slopeRise = 0.22f;
        float slopeFall = 0.28f;
        float slopeResponse = 0.5f;
        float contourOnset = 0.004f;
        float contourSustain = 0.72f;
        float contourDecay = 0.28f;
        float contourResponse = 0.62f;
        float balance = 0.32f;
        float balanceCv = 1.0f;
        float dynamics = 0.88f;
        float level = 0.72f;
        bool  arpOn = false;
        int   arpType = 0;
        bool  legato = true;
        int   midiBCv = 1;
        int   midiBGate = 2;
        int   clockDiv = 3;
        float mathAmt = 0.0f;
        float midiBLfoRate = 2.0f;
        int   bendRange = 2;
        float outputDb = 0.0f;
    };

    struct HostTime
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool hasPpq = false;
        bool playing = false;
    };

    struct Activity
    {
        float midiA = 0.0f, midiB = 0.0f, tempo = 0.0f;
        float slope = 0.0f, multiply = 0.0f, contour = 0.0f, dynamics = 0.0f;
        float eoc = 0.0f, eon = 0.0f;
    };

    /**
        Monophonic no-coast voice: triangle-core oscillator, overtone / multiply folding,
        cycling slope, contour envelope, LPG-style dynamics, MIDI arp, and a virtual patch bay.
    */
    class SynthEngine
    {
    public:
        static constexpr int scopeSize = 512;
        static constexpr int oversample = 2;

        void prepare (double sampleRate, int samplesPerBlock);
        void reset();
        void setSettings (const Settings& s) noexcept { settings = s; }

        void process (float* left, float* right, int numSamples, juce::MidiBuffer& midi, const HostTime& time);

        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();

        bool connect (OutJack from, InJack to);
        bool disconnectInput (InJack to);
        void clearCables();
        int numCables() const noexcept;
        Cable cableAt (int index) const noexcept;
        bool inputIsPatched (InJack to) const noexcept;
        int8_t sourceOf (InJack to) const noexcept;

        juce::ValueTree cablesToTree() const;
        void cablesFromTree (const juce::ValueTree& tree);

        int currentNote() const noexcept { return heldNote; }
        float lastPitchHz() const noexcept { return lastHz; }
        float lastSlope() const noexcept { return slopeLevel; }
        float lastContour() const noexcept { return contourLevel; }
        float lastEoc() const noexcept { return eocGate; }
        float lastEon() const noexcept { return eonGate; }
        float lastRandom() const noexcept { return randomLevel; }
        float lastClock() const noexcept { return clockGate; }
        float lastDynamicsCv() const noexcept { return lastDynCv; }
        float lastOutputSample() const noexcept { return lastOut; }
        float lastMidiBCv() const noexcept { return midiBCvLevel; }
        int arpLength() const noexcept { return arpCount; }
        int arpIndex() const noexcept { return arpStep; }

        Activity takeActivity() noexcept;
        void copyScope (float* dest, int count) const noexcept;

    private:
        struct Smooth
        {
            float current = 0.0f, coeff = 1.0f;
            bool primed = false;
            void setTime (double sr, float seconds) noexcept
            {
                coeff = seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (float) (sr * (double) seconds));
            }
            void unprime() noexcept { primed = false; }
            float tick (float target) noexcept
            {
                // First tick after prepare snaps so panel times match immediately;
                // later moves stay zipper-free.
                if (! primed)
                {
                    current = target;
                    primed = true;
                    return current;
                }
                current += coeff * (target - current);
                if (std::abs (current) < 1.0e-20f)
                    current = 0.0f;
                return current;
            }
        };

        struct OnePole
        {
            float z = 0.0f;
            float processLow (float x, float coeff) noexcept
            {
                z += coeff * (x - z);
                if (std::abs (z) < 1.0e-20f)
                    z = 0.0f;
                return z;
            }
            float processHigh (float x, float coeff) noexcept
            {
                const auto low = processLow (x, coeff);
                return x - low;
            }
        };

        struct AdaaFold
        {
            float x1 = 0.0f, F1 = 0.0f;
            float process (float x, float drive) noexcept;
        };

        static constexpr int maxHeld = 20;

        void handleMidi (const juce::MidiMessage& message);
        void handleMidiRaw (const juce::uint8* data, int numBytes);
        void retriggerContour() noexcept;
        void unprimeSmoothers() noexcept;
        void tickClock (float dt, const HostTime& time, bool tempoPatched, float tempoIn, bool& clockEdge);
        void tickArp (bool clockEdge);
        void tickMidiB (float dt);
        void tickSlope (float dt, bool trig, float timeCv);
        void tickContour (float dt, bool gate, float decayCv);
        void tickSmoothers() noexcept;
        void tickVoice (float dtOs, float linFmIn, float overtoneCvIn, float multiplyCvIn, float slopeAudio,
                        float& coreTri, float& coreSqr, float& folded);
        float readInput (InJack jack, float normal) const noexcept;
        void publishControlOutputs() noexcept;
        void publishAudioOutputs (float tri, float sqr, float audio) noexcept;
        float shapeRate (float level, float response) const noexcept;
        static float flush (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }
        static float polyblep (float t, float dt) noexcept;
        float nextRandom() noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        float invSr = 1.0f / 48000.0f;
        float osInvSr = 0.5f / 48000.0f;

        Smooth smPitch, smFine, smPort, smLinFm, smOvertone, smOvertoneCv, smMultiply, smMultiplyCv;
        Smooth smRise, smFall, smSlopeResp, smOnset, smSustain, smDecay, smContourResp;
        Smooth smBalance, smBalanceCv, smDynamics, smLevel, smMath, smOutput, smLfoRate;
        float curPitch = 0, curFine = 0, curPort = 0, curLinFm = 0, curOvertone = 0, curOvertoneCv = 0;
        float curMultiply = 0, curMultiplyCv = 0, curBalance = 0, curBalanceCv = 0, curDynamics = 0;
        float curLevel = 0, curMath = 0, curOutput = 0;

        float phase = 0.0f;
        float triIntegrator = 0.0f;
        float glideHz = 110.0f;
        float lastHz = 110.0f;
        OnePole fmDc, osLpf, lpgLp, vactrol, levelDc;
        AdaaFold folder;

        float slopeLevel = 0.0f, eocGate = 1.0f;
        int slopeStage = 0; // 0 idle, 1 rise, 2 fall
        bool slopeTrigPrev = false;
        bool cycleWasOn = false;

        float contourLevel = 0.0f, eonGate = 0.0f;
        int contourStage = 0; // 0 idle, 1 onset, 2 decay, 3 sustain, 4 release
        bool contourGatePrev = false;

        float clockPhase = 0.0f, clockGate = 0.0f, randomLevel = 0.5f;
        float tempoInPrev = 0.0f, tappedPeriod = 0.5f, tapTimer = 0.0f;
        bool clockHigh = false;

        float midiBCvLevel = 0.0f, midiBGateLevel = 0.0f, midiBLfoPhase = 0.0f;
        float midiACvLevel = 0.0f, midiAGateLevel = 0.0f;
        float modWheel = 0.0f, pitchBend = 0.0f, lastVelocity = 0.8f;
        float lastDynCv = 0.0f, lastOut = 0.0f;
        float mathOut = 0.0f;
        float outs[outCount] {};

        int heldNote = -1;
        int heldCount = 0;
        std::array<int, maxHeld> heldNotes {};
        std::array<float, maxHeld> heldVels {};

        int arpCount = 0, arpStep = 0;
        std::array<int, maxHeld> arpNotes {};
        std::array<float, maxHeld> arpVels {};
        int arpPlaying = -1;
        bool arpWasOn = false;

        uint32_t rng = 0xA341316Cu;
        std::array<std::atomic<int8_t>, inCount> inputSource {};

        std::atomic<float> actMidiA { 0 }, actMidiB { 0 }, actTempo { 0 };
        std::atomic<float> actSlope { 0 }, actMultiply { 0 }, actContour { 0 }, actDynamics { 0 };
        std::atomic<float> actEoc { 0 }, actEon { 0 };

        std::array<std::atomic<float>, scopeSize> scope {};
        std::atomic<int> scopeWrite { 0 };
    };
}
