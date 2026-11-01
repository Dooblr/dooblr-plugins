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
    /** The order with the limiter moved to the end (it runs after the time FX, so it is always last). */
    ChainOrder withLimiterLast (const ChainOrder& order) noexcept;

    /**
        Gate, EQ and compressor in a user-chosen order. The limiter is not part of the chain:
        the engine runs it last, after the delay / reverb and the output trim, so nothing can
        push the final output past its ceiling. A limiter entry in the order is skipped here.
        prepare() allocates; process() does not.
    */
    class ProcessingChain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        void process (juce::AudioBuffer<float>& buffer, int numSamples, const VocalSettings& settings,
                      const ChainOrder& order, MeterBlock* meter, MeterTap* spectrumTap) noexcept;

        const ThreeBandEq& getEq() const noexcept { return eq; }

    private:
        Gate gate;
        ThreeBandEq eq;
        Compressor compressor;
    };
}
