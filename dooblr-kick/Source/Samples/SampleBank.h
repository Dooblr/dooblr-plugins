#pragma once

#include <array>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "../DSP/SamplePlayer.h"
#include "../Parameters.h"

namespace dooblrkick::samples
{
    /** The embedded factory samples, decoded once per process (share with juce::SharedResourcePointer). */
    class FactoryBank
    {
    public:
        FactoryBank();

        /** nullptr for an out-of-range index. */
        const dsp::SampleData* get (int index) const noexcept;

    private:
        std::array<dsp::SampleData, (size_t) params::numFactorySamples> samples;
    };

    /** Decodes a WAV / AIFF (any channel count, mixed to mono; at most 10 s). Message thread. */
    std::unique_ptr<dsp::SampleData> loadAudioFile (const juce::File& file, juce::String& error);

    /** Decodes an in-memory WAV / AIFF the same way. */
    std::unique_ptr<dsp::SampleData> decode (std::unique_ptr<juce::InputStream> stream, juce::String& error);

    inline constexpr double maxUserSampleSeconds = 10.0;
}
