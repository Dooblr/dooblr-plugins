#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

#include "AnalogModels.h"
#include "SvfFilter.h"

namespace dooblrq::dsp
{
    /** Plain values for one band, read from the parameters once per block. */
    struct BandSettings
    {
        bool enabled = false;
        FilterType type = FilterType::bell;
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        bool dynamicsEnabled = false;
        float rangeDb = 0.0f;
        float thresholdDb = -24.0f;
        float attackMs = 10.0f;
        float releaseMs = 120.0f;
        Slope slope = Slope::db12;
        // 0.8.0: analogue model (frequency / gain arrive already snapped when Stepped is on).
        Model model = Model::digital;
        float attenDb = 0.0f;  // Pultec low / high atten (ignored by other models)
        float driveDb = 0.0f;  // colour stage drive (analogue models only)
    };

    /**
        One EQ band with an optional dynamic gain section.

        Signal path per channel: the band's input feeds a side-chain detector filter
        (tuned to the band's frequency region) and a peak envelope follower with
        attack/release ballistics. The detected level drives the band's gain:

            effective gain = static gain + dynamic gain (clamped to +/- maxTotalGainDb)
            dynamic gain   = sign(range) * min(|range|, slope * over-threshold)

        Low Cut / High Cut (0.9.0) have no gain, so the same signed dynamic amount moves the
        cutoff instead (dynamicCutFrequency): cutOctavesPerDb octaves per dB, clamped to
        20 Hz .. 20 kHz. Positive range: a louder band pushes the cutoff into the passband
        (Low Cut rises, High Cut falls), cutting more. Negative range: a louder band opens
        the filter (Low Cut falls, High Cut rises). The detector is the band's usual one
        (a band-pass at the static cutoff, width from Q), so the cutoff's own movement never
        feeds back into the detection. The applied shift is slewed (cutShiftSmoothingSeconds)
        so fast attacks do not step the cutoff audibly.

        where over-threshold uses a soft knee. The EQ filter itself is a TPT SVF whose
        coefficients are recomputed for every sub-block passed to process(). With a slope
        above 12 dB/oct the filter is a cascade of SVF sections (see makeBandPrototype).
        A change of type, slope or model (which changes the filter topology) is crossfaded: the
        old cascade keeps running on its own state and fades out over
        topologyFadeSeconds while the new one fades in, so switching never clicks.

        Real-time safe: no allocation, locks or system calls after prepare().
    */
    class DynamicEQBand
    {
    public:
        static constexpr int maxChannels = 2;

        /** dB of gain change per dB above threshold (a 2:1 ratio-style law). */
        static constexpr float dynamicSlope = 0.5f;
        static constexpr float kneeWidthDb = 6.0f;
        static constexpr float maxTotalGainDb = 30.0f;
        static constexpr double topologyFadeSeconds = 0.01;

        /** Dynamic cuts: octaves of cutoff movement per dB of dynamic amount (1 dB = 1/6 oct,
            so the full 24 dB range moves a cut 4 octaves). */
        static constexpr double cutOctavesPerDb = 1.0 / 6.0;
        static constexpr double cutShiftSmoothingSeconds = 0.0015;

        /** Cutoff of a Low Cut / High Cut moved by a dynamic amount (dB, signed like the
            range): Low Cut f * 2^(+dB/6), High Cut f * 2^(-dB/6), clamped to 20 Hz .. 20 kHz.
            Other types return the frequency unchanged. */
        static double dynamicCutFrequency (FilterType type, double frequency, double dynamicDb) noexcept;

        /** Maps a detector level to the dynamic gain offset in dB. */
        static float computeDynamicGainDb (float levelDb, float thresholdDb, float rangeDb) noexcept;

        void prepare (double newSampleRate) noexcept;
        void reset() noexcept;

        /** Sets new targets; values are smoothed. If snap is true, jumps straight to them. */
        void setSettings (const BandSettings& newSettings, bool snap = false) noexcept;

        /** Processes [startSample, startSample + numSamples) in place. Coefficients are
            computed once per call, so callers pass short sub-blocks (e.g. 16 samples). */
        void process (float* const* channels, int numChannels, int startSample, int numSamples,
                      bool stereoLinked, bool applyColour = true) noexcept;

        /** Runs only the model's colour stage (Natural / Linear Phase: after the FIR). */
        void processColour (float* const* channels, int numChannels, int startSample, int numSamples) noexcept
        {
            colour.process (channels, numChannels, startSample, numSamples);
        }

        bool hasColour() const noexcept { return ! colour.isSilent(); }
        float getColourLatency() const noexcept { return colour.getResidualLatency(); }

        /** Current dynamic gain offset (dB), safe to read from any thread. */
        float getDynamicGainDb() const noexcept { return dynamicGainForDisplay.load (std::memory_order_relaxed); }

        /** True while a type/slope change is being crossfaded. */
        bool isCrossfading() const noexcept { return crossfadeRemaining > 0; }

        /** True while the band is producing output (enabled or fading out). */
        bool isActive() const noexcept { return active; }

    private:
        void updateBallistics() noexcept;
        void resetFilterState() noexcept;

        double sampleRate = 44100.0;
        BandSettings settings;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequency { 1000.0f }, q { 1.0f };
        juce::SmoothedValue<float> gainDb, rangeDb, thresholdDb, wet, attenDb;
        ColourStage colour;

        float attackCoefficient = 0.0f, releaseCoefficient = 0.0f;

        using CascadeState = std::array<SvfState, maxSections>;
        std::array<CascadeState, maxChannels> filterState {}, fadingState {};
        std::array<SvfState, maxChannels> detectorState {};

        // Topology currently running, and the one fading out during a crossfade.
        FilterType appliedType = FilterType::bell, fadingType = FilterType::bell;
        Slope appliedSlope = Slope::db12, fadingSlope = Slope::db12;
        Model appliedModel = Model::digital, fadingModel = Model::digital;
        int crossfadeRemaining = 0, crossfadeLength = 1;
        std::array<float, maxChannels> envelope {}, dynamicGainDb {};
        std::array<float, maxChannels> cutShiftDb {}; // slewed dynamic amount applied to a cut's cutoff

        bool active = false;
        std::atomic<float> dynamicGainForDisplay { 0.0f };
    };
}
