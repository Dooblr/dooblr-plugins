#include "Sample.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace dooblrgenerate::engine
{
    std::shared_ptr<Sample> Sample::load (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFullPathName();
            return nullptr;
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Not a readable audio file: " + file.getFullPathName();
            return nullptr;
        }

        if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        {
            error = "The audio file is empty: " + file.getFullPathName();
            return nullptr;
        }

        const auto maxSamples = (juce::int64) (reader->sampleRate * 60.0);
        const auto length = (int) juce::jmin (reader->lengthInSamples, maxSamples);
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);

        auto sample = std::make_shared<Sample>();
        sample->audio.setSize (channels, length);
        reader->read (&sample->audio, 0, length, 0, true, channels > 1);
        sample->sampleRate = reader->sampleRate;
        sample->file = file;
        return sample;
    }

    SampleSlot::~SampleSlot()
    {
        publishedRaw.store (nullptr);
        inUse.store (nullptr);
    }

    void SampleSlot::publish (std::shared_ptr<const Sample> sample)
    {
        const juce::ScopedLock sl (writeLock);

        if (sample != nullptr && std::find (alive.begin(), alive.end(), sample) == alive.end())
            alive.push_back (sample);

        published = sample;
        publishedRaw.store (sample.get(), std::memory_order_seq_cst);

        const auto* used = inUse.load (std::memory_order_seq_cst);
        alive.erase (std::remove_if (alive.begin(), alive.end(), [&] (const std::shared_ptr<const Sample>& s)
                     {
                         return s != published && s.get() != used;
                     }),
                     alive.end());
    }

    void SampleSlot::collect()
    {
        publish (current());
    }

    std::shared_ptr<const Sample> SampleSlot::current() const
    {
        const juce::ScopedLock sl (writeLock);
        return published;
    }

    const Sample* SampleSlot::acquire() noexcept
    {
        for (;;)
        {
            const auto* p = publishedRaw.load (std::memory_order_seq_cst);
            inUse.store (p, std::memory_order_seq_cst);

            if (publishedRaw.load (std::memory_order_seq_cst) == p)
                return p;
        }
    }

    int SampleSlot::numAlive() const
    {
        const juce::ScopedLock sl (writeLock);
        return (int) alive.size();
    }
}
