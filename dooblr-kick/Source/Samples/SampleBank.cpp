#include "SampleBank.h"

#include <BinaryData.h>

namespace dooblrkick::samples
{
    namespace
    {
        // BinaryData resource names, in the same order as params::factorySampleNames.
        constexpr std::array<const char*, (size_t) params::numFactorySamples> resourceNames {
            "kit_kick_close_wav", "kit_kick_mid_wav", "concert_bd_muted_wav", "concert_bd_open_wav",
            "concert_bd_boom_wav", "cajon_bass_wav", "frame_drum_mute_wav", "slapstick_wav",
            "woodblock_wav", "hand_clap_wav", "shaker_hit_wav",
            "synth_kick_punch_wav", "synth_kick_sub_wav", "synth_tom_low_wav", "synth_click_wav", "synth_clap_wav",
            "low_tom_mallet_wav", "timpani_low_wav", "brake_drum_hit_wav", "cabasa_hit_wav", "darbuka_doum_wav" };

        juce::AudioFormatManager& formats()
        {
            static juce::AudioFormatManager manager;
            static const bool registered = [] { manager.registerFormat (new juce::WavAudioFormat(), true);
                                                manager.registerFormat (new juce::AiffAudioFormat(), false);
                                                return true; }();
            juce::ignoreUnused (registered);
            return manager;
        }

        std::unique_ptr<dsp::SampleData> readAll (juce::AudioFormatReader& reader, juce::String& error)
        {
            if (reader.sampleRate < 8000.0 || reader.numChannels < 1)
            {
                error = "unsupported sample rate or channel count";
                return {};
            }

            const auto maxLength = (juce::int64) (maxUserSampleSeconds * reader.sampleRate);
            const auto length = (int) juce::jmin (reader.lengthInSamples, maxLength);

            if (length <= 0)
            {
                error = "the file has no audio";
                return {};
            }

            juce::AudioBuffer<float> buffer ((int) reader.numChannels, length);
            reader.read (&buffer, 0, length, 0, true, true);

            auto data = std::make_unique<dsp::SampleData>();
            data->sampleRate = reader.sampleRate;
            data->samples.assign ((size_t) length, 0.0f);
            const auto scale = 1.0f / (float) buffer.getNumChannels();

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* in = buffer.getReadPointer (ch);

                for (int n = 0; n < length; ++n)
                    data->samples[(size_t) n] += in[n] * scale;
            }

            return data;
        }
    }

    FactoryBank::FactoryBank()
    {
        dsp::SampleReader::prepareTables();

        for (size_t i = 0; i < resourceNames.size(); ++i)
        {
            int size = 0;
            const auto* bytes = BinaryData::getNamedResource (resourceNames[i], size);
            jassert (bytes != nullptr);

            if (bytes == nullptr)
                continue;

            juce::String error;

            if (auto decoded = decode (std::make_unique<juce::MemoryInputStream> (bytes, (size_t) size, false), error))
                samples[i] = std::move (*decoded);
        }
    }

    const dsp::SampleData* FactoryBank::get (int index) const noexcept
    {
        if (index < 0 || index >= (int) samples.size() || samples[(size_t) index].samples.empty())
            return nullptr;

        return &samples[(size_t) index];
    }

    std::unique_ptr<dsp::SampleData> decode (std::unique_ptr<juce::InputStream> stream, juce::String& error)
    {
        if (stream == nullptr)
        {
            error = "cannot open the file";
            return {};
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (std::move (stream)));

        if (reader == nullptr)
        {
            error = "not a WAV or AIFF file";
            return {};
        }

        return readAll (*reader, error);
    }

    std::unique_ptr<dsp::SampleData> loadAudioFile (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "file not found";
            return {};
        }

        return decode (file.createInputStream(), error);
    }
}
