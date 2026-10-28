#pragma once

#include <array>
#include <cstdint>

#include "KickSettings.h"

namespace dooblrkick::dsp
{
    /** Frequency of a MIDI note (A4 = note 69 = 440 Hz). Fractional notes allowed. */
    float midiNoteToHz (float note) noexcept;

    /**
        One kick hit: Click, Body, Sub, Tail and Noise layers rendered sample by sample, each
        with its own start offset, envelope, filter, level, pan and Drive send. Allocation free.

        By default the MIDI note never changes a layer: every note plays the same kick, pitched
        by the Key, the Tune controls and the pitch envelope. A layer with its Key Track switch
        on (0.3.5: synth or sample; the synth Noise has no pitch) follows the note relative to
        the Key, on top of its tune and pitch envelope.
    */
    class KickVoice
    {
    public:
        void prepare (double sampleRate);

        /** Starts a hit. */
        void start (const KickSettings& settings, int note, float velocity);

        /** Gate mode note-off: fades out over the release time. */
        void release (float releaseMs) noexcept;

        /** Fast (4 ms) fade used when a new hit replaces this one. */
        void choke() noexcept;

        bool isActive() const noexcept { return active; }
        bool isReleasing() const noexcept { return releaseStep > 0.0f; }
        int getNote() const noexcept { return note; }
        double getElapsedSeconds() const noexcept { return (double) samplesElapsed / sampleRate; }
        std::int64_t getSamplesElapsed() const noexcept { return samplesElapsed; }

        /** Adds this voice into the direct and Drive-send stereo buses. */
        void render (const KickSettings& settings, float* directL, float* directR,
                     float* sendL, float* sendR, int numSamples) noexcept;

        /** Length of a one-shot hit with these settings (without release), in ms. */
        static float getOneShotLengthMs (const KickSettings& settings) noexcept;

    private:
        struct LayerState
        {
            float gain = 0.0f;
            float pan = 0.0f; // smoothed per sample, like gain
            std::array<SvfFilter, 2> filters;
        };

        double sampleRatio (bool keyTrack, float tuneSt) const noexcept;

        float nextNoise (std::uint32_t& state) noexcept;
        // pitchOn: the layer's pitch envelope is not flat at 0 st (v0.3.6; checked once per block,
        // so a flat envelope costs nothing). sustain: a gated note holds the amp envelope's end.
        float renderClick (const ClickSettings&, float tMs, bool pitchOn) noexcept;
        float renderBody (const BodySettings&, float tMs, bool sustain) noexcept;
        float renderSub (const KickSettings&, float tMs, bool pitchOn, bool sustain) noexcept;
        void renderTail (const TailSettings&, float tMs, float keyNote, bool pitchOn, bool sustain, float& left, float& right) noexcept;
        void renderNoise (const NoiseSettings&, float tMs, bool pitchOn, bool sustain, float& left, float& right) noexcept;

        double sampleRate = 48000.0;
        bool active = false;
        int note = 60;
        float velocityGain = 1.0f;
        std::int64_t samplesElapsed = 0;
        double sampleKeyRatio = 1.0; // 2^((note - Key) / 12): only for key-tracked layers
        float keyOffsetSt = 0.0f;    // note - Key in semitones (key-tracked synth layers, 0.3.5)
        float releaseGain = 1.0f, releaseStep = 0.0f;
        bool held = true;

        std::array<LayerState, numLayers> layers {};
        double bodyPhase = 0.0, subPhase = 0.0, clickPhase = 0.0;
        std::array<double, 2> tailPhase {};
        std::uint32_t noiseStateL = 1, noiseStateR = 2, clickNoiseState = 3;
        std::array<std::array<float, 7>, 2> pink {};
        std::array<float, 2> brown {};
        double sampleStep (const SampleSource&, double pitchRatio) const noexcept;

        std::array<SampleReader, numLayers> readers {};
        SvfFilter clickBand;
        float clickTone = 0.0f;

        // v0.3.6: the synth Noise's pitch envelope drives this tone filter pair (low-pass below
        // 0 st, high-pass above); coefficients are updated every noiseToneInterval samples.
        static constexpr int noiseToneInterval = 8;
        std::array<SvfFilter, 2> noiseLow {}, noiseHigh {};
        int noiseToneCountdown = 0;
    };
}
