#pragma once

#include <initializer_list>

#include "Effects.h"
#include "Envelope.h"
#include "SamplePlayer.h"

namespace dooblrkick::dsp
{
    enum LayerIndex { clickLayer = 0, bodyLayer, subLayer, tailLayer, noiseLayer, numLayers };

    inline MultiPointEnvelope makeEnvelope (std::initializer_list<EnvelopeNode> nodes) noexcept
    {
        MultiPointEnvelope e;
        e.numNodes = 0;
        for (const auto& n : nodes)
            if (e.numNodes < maxEnvelopeNodes)
                e.nodes[(size_t) e.numNodes++] = n;
        e.sanitise();
        return e;
    }

    /** v0.3.6 defaults of the Click, Sub, Tail and Noise pitch (semitones, flat at 0) and amp
        (0..1) envelopes. The amp ones are exactly the old shapes: the Click's linear taper over
        its 12 ms length, and the Sub / Tail / Noise attack-hold-decay envelopes (an AHD decay used
        the same curve law as a multi-point segment). Also the parameter defaults. */
    inline MultiPointEnvelope defaultLayerEnvelope (int layer, bool amp) noexcept
    {
        switch (layer)
        {
            case 0:  return amp ? makeEnvelope ({ { 0.0f, 1.0f, 0.0f }, { 12.0f, 0.0f, 0.0f } })
                                : makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 12.0f, 0.0f, 0.0f } });
            case 2:  return amp ? makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 61.0f, 1.0f, 0.4f }, { 411.0f, 0.0f, 0.0f } })
                                : makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 400.0f, 0.0f, 0.0f } });
            case 3:  return amp ? makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 30.0f, 1.0f, 0.5f }, { 930.0f, 0.0f, 0.0f } })
                                : makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 900.0f, 0.0f, 0.0f } });
            case 4:  return amp ? makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 5.5f, 1.0f, 0.6f }, { 155.5f, 0.0f, 0.0f } })
                                : makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 150.0f, 0.0f, 0.0f } });
            default: return amp ? makeEnvelope ({ { 0.0f, 1.0f, 0.0f }, { 500.0f, 0.0f, 0.0f } })
                                : makeEnvelope ({ { 0.0f, 0.0f, 0.0f }, { 500.0f, 0.0f, 0.0f } });
        }
    }

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
        float tone = 0.0f; // -1 low-pass ... +1 high-pass
        bool keyTrack = false; // follow the MIDI note relative to the Key (synth and sample, 0.3.5)
        // v0.3.6: the amp envelope replaces Length (its last point is the click's length, the
        // timeline of the type's own decay and the chirp sweep); the pitch envelope is semitones.
        MultiPointEnvelope pitch = defaultLayerEnvelope (0, false);
        MultiPointEnvelope amp = defaultLayerEnvelope (0, true);
        SampleSource sample;
        float sampleEnvAmount = 1.0f; // 0..1: how much the pitch envelope bends a Click sample
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
        bool keyTrack = false;    // follow the MIDI note relative to the Key (synth and sample, 0.3.5)
        MultiPointEnvelope pitch; // values in log2(Hz); the synth body follows the note only with keyTrack
        MultiPointEnvelope amp;   // values 0..1
        SampleSource sample;
        float sampleEnvAmount = 0.0f; // 0..1: how much the pitch envelope bends a Body sample
    };

    struct SubSettings
    {
        LayerCommon common;
        float tuneSt = 0.0f, fineCents = 0.0f, phaseDeg = 0.0f;
        float envAmount = 0.0f; // 0..1: how much of the Body pitch sweep the sub follows
        bool keyTrack = false;  // 0.3.5: follow the MIDI note relative to the Key
        MultiPointEnvelope pitch = defaultLayerEnvelope (2, false); // semitones (v0.3.6)
        MultiPointEnvelope amp = defaultLayerEnvelope (2, true);
    };

    struct TailSettings
    {
        LayerCommon common;
        int wave = sineWave; // sine / triangle / saturated
        float tuneSt = 0.0f, spreadCents = 0.0f, phaseDeg = 0.0f, drive = 0.0f;
        MultiPointEnvelope pitch = defaultLayerEnvelope (3, false); // semitones (v0.3.6)
        MultiPointEnvelope amp = defaultLayerEnvelope (3, true);
        bool keyTrack = false; // follow the MIDI note relative to the Key (synth and sample, 0.3.5)
        SampleSource sample;
        float sampleEnvAmount = 1.0f; // 0..1: how much the pitch envelope bends a Tail sample
    };

    struct NoiseSettings
    {
        enum Colour { white = 0, pink, brown, numColours };

        LayerCommon common;
        int colour = white;
        float width = 0.5f;
        float tuneSt = 0.0f; // Sample source only (the synth noise has no pitch)
        bool keyTrack = false; // 0.3.5: Sample source only (no pitch to track on the synth noise)
        // v0.3.6: semitones. The synth noise has no pitch, so its pitch envelope moves a tone
        // filter instead: below 0 st a low-pass from 20 kHz down, above 0 st a high-pass from
        // 20 Hz up, one semitone of cutoff per semitone. A sample is bent in pitch.
        MultiPointEnvelope pitch = defaultLayerEnvelope (4, false);
        MultiPointEnvelope amp = defaultLayerEnvelope (4, true);
        SampleSource sample;
        float sampleEnvAmount = 1.0f; // 0..1: how much the pitch envelope bends a Noise sample
    };

    /** Sample tune range (semitones). -48 st plays a sample 16x slower. */
    inline constexpr float minSampleTuneSt = -48.0f, maxSampleTuneSt = 24.0f;

    struct PlaySettings
    {
        // The kick's own key: the base pitch of the Sub and the synth Tail, and the reference
        // note for key tracking. The MIDI note changes a layer only with its Key Track on (0.3.5).
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
