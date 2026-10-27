#pragma once

#include "Effects.h"
#include "Envelope.h"
#include "SamplePlayer.h"

namespace dooblrkick::dsp
{
    enum LayerIndex { clickLayer = 0, bodyLayer, subLayer, tailLayer, noiseLayer, numLayers };

    struct LayerCommon
    {
        bool enabled = true;
        bool solo = false;
        float levelDb = 0.0f;   // <= -60 dB is silent
        float pan = 0.0f;       // -1 .. +1 (balance)
        float startMs = 0.0f;   // start offset
        float send = 1.0f;      // 0 = straight to the EQ/dynamics, 1 = through the Drive stage
        int filterType = 0;     // 0 off, 1 low-pass, 2 high-pass, 3 band-pass
        float cutoffHz = 20000.0f;
        float resonance = 0.0f;
    };

    /** Synth / Sample switch of a layer. data is resolved by the processor (factory bank or
        the user's file) and stays valid while these settings are in use. */
    struct SampleSource
    {
        bool enabled = false; // false: synthesised, true: sample playback
        int index = 0;        // factory sample index, or the user-file slot
        const SampleData* data = nullptr;
    };

    struct ClickSettings
    {
        enum Type { chirp = 0, noiseBurst, snap, tick, knock, numTypes };

        LayerCommon common;
        int type = chirp;
        float pitchSt = 0.0f;
        float lengthMs = 12.0f;
        float tone = 0.0f; // -1 low-pass ... +1 high-pass
        bool keyTrack = false; // Sample source only: follow the MIDI note relative to the Key
        SampleSource sample;
    };

    enum Wave { sineWave = 0, triangleWave, saturatedWave, squareWave, numWaves };

    struct BodySettings
    {
        LayerCommon common;
        int wave = sineWave;
        float harmonics = 0.0f; // 0..1, adds partials 2..8
        float tuneSt = 0.0f;
        float phaseDeg = 0.0f;
        float drive = 0.0f;     // 0..1
        bool keyTrack = false;    // Sample source only: follow the MIDI note relative to the Key
        MultiPointEnvelope pitch; // values in log2(Hz); the synth body ignores the MIDI note
        MultiPointEnvelope amp;   // values 0..1
        SampleSource sample;
        float sampleEnvAmount = 0.0f; // 0..1: how much the pitch envelope bends a Body sample
    };

    struct SubSettings
    {
        LayerCommon common;
        float tuneSt = 0.0f, fineCents = 0.0f, phaseDeg = 0.0f;
        float envAmount = 0.0f; // 0..1: how much of the Body pitch sweep the sub follows
        AhdEnvelope amp { 2.0f, 50.0f, 400.0f, 0.4f };
    };

    struct TailSettings
    {
        LayerCommon common;
        int wave = sineWave; // sine / triangle / saturated
        float tuneSt = 0.0f, spreadCents = 0.0f, phaseDeg = 0.0f, drive = 0.0f;
        AhdEnvelope amp { 20.0f, 50.0f, 700.0f, 0.5f };
        bool keyTrack = false; // Sample source only: follow the MIDI note relative to the Key
        SampleSource sample;
    };

    struct NoiseSettings
    {
        enum Colour { white = 0, pink, brown, numColours };

        LayerCommon common;
        int colour = white;
        float width = 0.5f;
        float tuneSt = 0.0f; // Sample source only (the synth noise has no pitch)
        AhdEnvelope amp { 0.5f, 5.0f, 120.0f, 0.6f };
        SampleSource sample;
    };

    /** Sample tune range (semitones). -48 st plays a sample 16x slower. */
    inline constexpr float minSampleTuneSt = -48.0f, maxSampleTuneSt = 24.0f;

    struct PlaySettings
    {
        // The kick's own key: the base pitch of the Sub and the synth Tail, and the reference
        // note for sample key tracking. The MIDI note never changes the synthesised layers.
        // (Note names use middle C = C3, as Ableton does; 29 = F0, 43.7 Hz.)
        int keyNote = 29;
        float velocitySensitivity = 0.5f;
        bool gate = false;
        float releaseMs = 40.0f;
    };

    /** Processing chain stages, in their default (v0.2.0) order. */
    enum ChainStage { driveStage = 0, eqStage, dynamicsStage, limiterStage, numChainStages };
    inline constexpr int numChainOrders = 24; // 4! orders of the four stages

    /** Envelope page: a master amplitude envelope over the summed hit (before the processing
        chain, so it shapes the dry signal too), the chain's dry/wet and the chain order. */
    struct MasterSettings
    {
        bool envelopeOn = true;
        MultiPointEnvelope envelope = unityEnvelope(); // levels 0..1; the default is flat at 1
        float dryWet = 1.0f;  // 0 = dry (summed layers before the chain) .. 1 = processed
        int chainOrder = 0;   // index into the 24 stage orders, 0 = Drive, EQ, Dynamics, Limiter

        /** On and not flat at unity: only then does the envelope change the sound. */
        bool envelopeActive() const noexcept
        {
            if (! envelopeOn)
                return false;

            for (int i = 0; i < envelope.numNodes; ++i)
                if (envelope.nodes[(size_t) i].value < 1.0f)
                    return true;

            return false;
        }

        static MultiPointEnvelope unityEnvelope() noexcept
        {
            MultiPointEnvelope e;
            e.numNodes = 2;
            e.nodes[0] = { 0.0f, 1.0f, 0.0f };
            e.nodes[1] = { 1000.0f, 1.0f, 0.0f }; // matches the menv parameter defaults
            return e;
        }
    };

    struct KickSettings
    {
        ClickSettings click;
        BodySettings body;
        SubSettings sub;
        TailSettings tail;
        NoiseSettings noise;
        PlaySettings play;
        DistortionSettings drive;
        EqSettings eq;
        ShaperSettings shaper;
        CompressorSettings compressor;
        LimiterSettings output;
        MasterSettings master;

        const LayerCommon& common (int layer) const noexcept;
        bool isLayerAudible (int layer) const noexcept;
    };
}
