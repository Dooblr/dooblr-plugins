#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Effects.h"
#include "KickSettings.h"
#include "MeterTap.h"

namespace dooblrkick::dsp
{
    using ChainOrder = std::array<int, numChainStages>;

    /** The 24 orders of the four stages, in lexicographic order (index 0 = the default
        Drive, EQ, Dynamics, Limiter). Out-of-range indices give the default. */
    ChainOrder chainOrderFromIndex (int index) noexcept;
    int chainOrderToIndex (const ChainOrder& order) noexcept; // -1 if not a permutation
    const char* chainStageName (int stage) noexcept;          // "Drive", "EQ", "Dynamics", "Limiter"

    /**
        The processing chain in a user-chosen order: Drive, EQ, Dynamics (transient shaper ->
        compressor) and the clipper / limiter.

        The layers arrive on two buses: direct and Drive send (each layer's Send splits it).
        Until the Drive stage runs the buses stay separate, so the per-layer sends keep working
        wherever Drive sits:
          - EQ before Drive: a second, identical EQ filters the send bus (the EQ is linear);
          - Dynamics / Limiter before Drive: the detector and the gain are computed from the sum
            of both buses, and the same per-sample gain is applied to each bus.
        Drive then distorts the send bus and folds it into the direct bus. With the default order
        the arithmetic is exactly the v0.2.0 chain. Output gain is not part of the chain.

        prepare() allocates; process() does not.
    */
    class ProcessingChain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes numSamples (<= maxBlockSize) of both buses; the result is in direct. */
        void process (juce::AudioBuffer<float>& direct, juce::AudioBuffer<float>& send, int numSamples,
                      const KickSettings& settings, const ChainOrder& order,
                      MeterBlock* meter, MeterTap* spectrumTap) noexcept;

        float getLastGainReductionDb() const noexcept { return limiter.getLastGainReductionDb(); }

    private:
        template <typename Process>
        void gainStage (juce::AudioBuffer<float>& direct, juce::AudioBuffer<float>& send, int n, bool split,
                        Process&& process) noexcept;
        float peakOf (const juce::AudioBuffer<float>& direct, const juce::AudioBuffer<float>& send, int n, bool split) noexcept;

        Distortion distortion;
        ThreeBandEq eq, sendEq;
        TransientShaper shaper;
        Compressor compressor;
        OutputStage limiter; // run with 0 dB output gain: the output gain is applied after dry / wet
        juce::AudioBuffer<float> sum, sumIn;
        std::array<float, 2> lastRatio { 1.0f, 1.0f };
    };

    /** The final output gain, with the per-sample 5 ms ramp OutputStage used in v0.2.0. */
    class OutputGain
    {
    public:
        void prepare (double newSampleRate) { sampleRate = newSampleRate; reset(); }
        void reset() noexcept { gain = -1.0f; }
        void process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb) noexcept;

    private:
        double sampleRate = 48000.0;
        float gain = -1.0f; // < 0: jump to the target
    };
}
