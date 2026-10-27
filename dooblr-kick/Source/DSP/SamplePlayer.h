#pragma once

#include <vector>

namespace dooblrkick::dsp
{
    /** A mono sample in memory at its own sample rate (decoded once, off the audio thread). */
    struct SampleData
    {
        std::vector<float> samples;
        double sampleRate = 48000.0;

        float getLengthMs() const noexcept
        {
            return samples.empty() ? 0.0f : (float) (1000.0 * (double) samples.size() / sampleRate);
        }
    };

    /**
        Variable-rate, band-limited sample reader: windowed-sinc interpolation (16 zero
        crossings, Blackman-Harris window) whose cut-off follows the playback ratio, so
        sample-rate conversion and re-pitching up to +2 octaves stay alias-free.
        Allocation free; the kernel table is a static array built by prepareTables().
    */
    class SampleReader
    {
    public:
        /** Builds the shared kernel table. Call once off the audio thread (prepare). */
        static void prepareTables() noexcept;

        void reset() noexcept { position = 0.0; }

        /** Next output sample. ratio = source samples consumed per output sample. */
        float read (const SampleData* data, double ratio) noexcept;

        double getPosition() const noexcept { return position; }

    private:
        double position = 0.0;
    };
}
