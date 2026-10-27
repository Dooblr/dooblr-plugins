#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Effects.h"
#include "MeterTap.h"
#include "VocalSettings.h"

namespace dooblrvocals::dsp
{
    using ChainOrder = std::array<int, numChainStages>;

    ChainOrder chainOrderFromIndex (int index) noexcept;
    int chainOrderToIndex (const ChainOrder& order) noexcept;
    const char* chainStageName (int stage) noexcept;

    /**
        Gate, EQ, compressor and limiter in a user-chosen order.
        prepare() allocates; process() does not.
    */
    class ProcessingChain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        void process (juce::AudioBuffer<float>& buffer, int numSamples, const VocalSettings& settings,
                      const ChainOrder& order, MeterBlock* meter, MeterTap* spectrumTap) noexcept;

        float getLastGainReductionDb() const noexcept { return limiter.getLastGainReductionDb(); }

    private:
        Gate gate;
        ThreeBandEq eq;
        Compressor compressor;
        OutputStage limiter;
    };
}
