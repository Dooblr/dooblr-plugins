#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "DynamicEQBand.h"
#include "SvfFilter.h"

/**
    Band Solo (0.9.1): audition only the frequency region a band works on.

    The stage runs after the EQ (and after the Natural / Linear phase FIR), so it is IIR,
    adds no latency in any phase mode and works the same for every model. With no band
    soloed it is an exact pass-through. While any band is soloed the output is the sum,
    over the soloed bands, of the EQ'd signal through that band's region filter:

      Bell, Notch     band-pass at the band's frequency, -3 dB at the band's Width edges
                      (two sections whose cascade has the band's Q), so the notched region
                      of a notch is what you hear.
      Low Shelf       low-pass at the shelf midpoint (the side the shelf moves).
      High Shelf      high-pass at the shelf midpoint.
      Tilt            the side the tilt raises: high-pass at the pivot for a positive tilt,
                      low-pass for a negative one.
      Low / High Cut  (0.9.4) what the cut removes: the band's input (the plug-in input,
                      aligned with the EQ output in every phase mode) through a low-pass
                      (Low Cut) or high-pass (High Cut) at the cutoff, the live cutoff while
                      dynamics move it. (0.9.1 to 0.9.3 played a band-pass of the EQ'd
                      output around the cutoff, i.e. what the cut keeps near its edge.)

    Every region filter is unity gain at its centre (band-pass) or in its passband, so the
    soloed region plays at the level the EQ gives it: comparable to the normal output.
    Region filters are 4th order (24 dB/oct skirts).

    Switching is click-free: each band's contribution and the dry/solo mix ramp linearly
    over fadeSeconds, and region frequency / Q glide (one-pole, glideSeconds) when a soloed
    band is moved.
*/
namespace dooblrq::dsp
{
    enum class SoloShape { bandPass, lowPass, highPass };

    struct SoloRegion
    {
        SoloShape shape = SoloShape::bandPass;
        double frequency = 1000.0;
        double q = 0.707;
        bool fromInput = false; // true: filter the input (cut bands), not the EQ output
    };

    /** The region a band's solo plays; dynamicGainDb moves a dynamic cut's cutoff. */
    SoloRegion soloRegionFor (const BandSettings& settings, double dynamicGainDb) noexcept;

    struct SoloCoefficients
    {
        std::array<SvfCoefficients, 2> sections {};
    };

    SoloCoefficients makeSoloCoefficients (const SoloRegion& region, double sampleRate) noexcept;

    /** Linear magnitude of a region filter (for the display and the tests). */
    double soloMagnitudeAt (const SoloRegion& region, double frequency, double sampleRate) noexcept;

    /** Same, from coefficients already made for the region (no per-call tan()). */
    double soloMagnitudeAt (const SoloCoefficients& coefficients, double frequency, double sampleRate) noexcept;

    /** (0.9.5) What the solo stage is playing right now, as published by the audio thread
        after every process() call: the dry/solo mix, and per band the fade weight and the
        region it is actually filtering with (the glided frequency / Q, not the target). */
    struct SoloView
    {
        struct Band
        {
            float weight = 0.0f;   // 0..1, this band's 20 ms fade
            SoloRegion region;     // the current (smoothed) region
            bool settled = true;   // the glide has reached the target region
        };

        float mix = 0.0f;          // 0..1, the dry/solo crossfade
        double sampleRate = 48000.0;
        std::array<Band, 6> bands {};

        /** True while a fade or a glide is still moving (the display keeps repainting). */
        bool isMoving (bool anyTarget) const noexcept;
    };

    class SoloStage
    {
    public:
        static constexpr int maxBands = 6;
        static constexpr int maxChannels = 2;
        static constexpr int updateInterval = 16;
        static constexpr double fadeSeconds = 0.02;
        static constexpr double glideSeconds = 0.01;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        /** Called once per block for every band (audio thread). */
        void setBand (int bandIndex, bool soloed, const SoloRegion& region) noexcept;

        /** In place on the EQ output. input (may be null: the EQ output is used) is the
            plug-in input aligned with it, for regions with fromInput. */
        void process (float* const* channels, const float* const* input, int numChannels, int numSamples) noexcept;

        /** True while any band is soloed or a fade is still running. */
        bool isActive() const noexcept;

        /** Current dry/solo mix (0 = normal output, 1 = solo only). Audio thread. */
        float getMix() const noexcept { return mix; }

        /** (0.9.5) The state last published by process() / prepare() / reset(); safe from
            any thread (lock-free sequence lock over relaxed atomics, never blocks the
            audio thread). */
        SoloView getView() const noexcept;

    private:
        struct Band
        {
            bool target = false, running = false;
            float weight = 0.0f;
            SoloRegion region;
            double logFrequency = 0.0, q = 0.707;
            SoloShape shape = SoloShape::bandPass;
            bool fromInput = false;
            SoloCoefficients coefficients;
            std::array<std::array<SvfState, 2>, maxChannels> state {};
        };

        void updateCoefficients (Band& band, bool snap) noexcept;
        void publish() noexcept;

        struct PublishedBand
        {
            std::atomic<float> weight { 0.0f };
            std::atomic<double> frequency { 1000.0 }, q { 0.707 };
            std::atomic<int> shape { 0 };
            std::atomic<bool> fromInput { false }, settled { true };
        };

        std::array<PublishedBand, maxBands> published;
        std::atomic<float> publishedMix { 0.0f };
        std::atomic<double> publishedSampleRate { 48000.0 };
        std::atomic<std::uint32_t> sequence { 0 };

        std::array<Band, maxBands> bands;
        float mix = 0.0f, step = 1.0f / 960.0f;
        double sampleRate = 48000.0, glide = 0.3;
    };
}
