#include "SourceAudio.h"
#include "ModelCatalog.h"

#include <cmath>

namespace dooblrgenerate::gen
{
    void SourceAudio::registerFormats (juce::AudioFormatManager& formats)
    {
        formats.registerBasicFormats();
       #if JUCE_MAC || JUCE_IOS
        formats.registerFormat (new juce::CoreAudioFormat(), false);
       #endif
    }

    bool modelSupportsSourceVariation (const juce::String& modelId)
    {
        const auto* model = ModelCatalog::find (modelId);
        return model != nullptr && model->supportsSourceVariation;
    }

    namespace
    {
        void resampleChannel (const float* in, int nIn, float* out, int nOut)
        {
            if (nIn <= 0 || nOut <= 0)
                return;

            if (nIn == nOut)
            {
                juce::FloatVectorOperations::copy (out, in, nOut);
                return;
            }

            if (nIn == 1)
            {
                juce::FloatVectorOperations::fill (out, in[0], nOut);
                return;
            }

            const double scale = (double) (nIn - 1) / (double) (nOut - 1);

            for (int i = 0; i < nOut; ++i)
            {
                const double pos = (double) i * scale;
                const auto i0 = (int) pos;
                const auto i1 = juce::jmin (i0 + 1, nIn - 1);
                const auto frac = (float) (pos - (double) i0);
                out[i] = in[i0] + frac * (in[i1] - in[i0]);
            }
        }

        bool isGzip (const juce::MemoryBlock& data)
        {
            return data.getSize() >= 2
                   && (juce::uint8) data[0] == 0x1f
                   && (juce::uint8) data[1] == 0x8b;
        }

        void collectAbsolutePaths (const juce::XmlElement* e, juce::StringArray& paths)
        {
            if (e == nullptr)
                return;

            const auto value = e->getStringAttribute ("Value");
            if (value.isNotEmpty() && juce::File::isAbsolutePath (value))
                paths.addIfNotAlreadyThere (value);

            for (auto* child = e->getFirstChildElement(); child != nullptr; child = child->getNextElement())
                collectAbsolutePaths (child, paths);
        }

        bool isReadableAudio (const juce::File& file, juce::AudioFormatManager& formats)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            return reader != nullptr && reader->sampleRate > 0 && reader->lengthInSamples > 0;
        }

        juce::File resolveAbletonClip (const juce::File& file, juce::AudioFormatManager& formats, juce::String& error)
        {
            juce::MemoryBlock data;
            if (! file.loadFileAsData (data) || data.getSize() < 2)
            {
                error = "Ableton clip could not be read: " + file.getFileName();
                return {};
            }

            std::unique_ptr<juce::InputStream> stream;
            if (isGzip (data))
                stream.reset (new juce::GZIPDecompressorInputStream (new juce::MemoryInputStream (data, true), true));
            else
                stream.reset (new juce::MemoryInputStream (data, true));

            const auto text = stream->readEntireStreamAsString();
            const auto xml = juce::parseXML (text);
            if (xml == nullptr)
            {
                error = "Ableton clip could not be read: " + file.getFileName();
                return {};
            }

            juce::StringArray paths;
            collectAbsolutePaths (xml.get(), paths);

            juce::StringArray missing;
            for (const auto& p : paths)
            {
                const juce::File candidate (p);
                if (! candidate.existsAsFile())
                {
                    missing.add (candidate.getFileName());
                    continue;
                }
                if (isReadableAudio (candidate, formats))
                    return candidate;
            }

            if (paths.isEmpty())
                error = "Ableton clip has no audio file path: " + file.getFileName();
            else if (missing.size() == paths.size())
                error = "Ableton clip: audio file not found (" + missing[0] + ")";
            else
                error = "Ableton clip: no readable audio file: " + file.getFileName();

            return {};
        }
    }

    juce::File SourceAudio::resolveDroppedFile (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFileName();
            return {};
        }

        juce::AudioFormatManager formats;
        registerFormats (formats);

        if (isReadableAudio (file, formats))
            return file;

        if (file.hasFileExtension ("alc"))
            return resolveAbletonClip (file, formats, error);

        error = "Not a readable audio file: " + file.getFileName();
        return {};
    }

    bool SourceAudio::convertToModelInput (const juce::AudioBuffer<float>& in, double inRate,
                                           juce::AudioBuffer<float>& out, bool& trimmed)
    {
        trimmed = false;
        out.setSize (0, 0);

        if (in.getNumSamples() <= 0 || in.getNumChannels() <= 0 || inRate <= 0)
            return false;

        const auto maxIn = (int) std::llround (maxSeconds * inRate);
        const auto nTake = juce::jmin (in.getNumSamples(), juce::jmax (1, maxIn));
        trimmed = in.getNumSamples() > maxIn;

        juce::AudioBuffer<float> stereo (targetChannels, nTake);
        stereo.copyFrom (0, 0, in, 0, 0, nTake);
        if (in.getNumChannels() > 1)
            stereo.copyFrom (1, 0, in, 1, 0, nTake);
        else
            stereo.copyFrom (1, 0, in, 0, 0, nTake);

        if (std::abs (inRate - targetRate) < 0.5)
        {
            out = std::move (stereo);
            return true;
        }

        const auto nOut = juce::jmax (1, (int) std::llround ((double) nTake * targetRate / inRate));
        out.setSize (targetChannels, nOut);
        for (int ch = 0; ch < targetChannels; ++ch)
            resampleChannel (stereo.getReadPointer (ch), nTake, out.getWritePointer (ch), nOut);

        return true;
    }

    bool SourceAudio::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr)
            return false;

        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer == nullptr)
            return false;

        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    std::unique_ptr<SourceAudio> SourceAudio::loadAndStore (const juce::File& dropped, const juce::File& destWav,
                                                            juce::String& error)
    {
        const auto resolved = resolveDroppedFile (dropped, error);
        if (resolved == juce::File())
            return nullptr;

        juce::AudioFormatManager formats;
        registerFormats (formats);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (resolved));

        if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        {
            error = "Not a readable audio file: " + resolved.getFileName();
            return nullptr;
        }

        const auto maxIn = (juce::int64) std::ceil (maxSeconds * reader->sampleRate);
        const auto length = (int) juce::jmin (reader->lengthInSamples, juce::jmax ((juce::int64) 1, maxIn));
        const auto channels = (int) juce::jlimit (1u, 8u, reader->numChannels);
        juce::AudioBuffer<float> loaded (channels, length);
        loaded.clear();
        reader->read (&loaded, 0, length, 0, true, channels > 1);

        auto source = std::make_unique<SourceAudio>();
        source->originalName = dropped.getFileName();
        if (dropped.hasFileExtension ("alc"))
            source->originalName = resolved.getFileName();
        source->originalSeconds = reader->lengthInSamples / reader->sampleRate;

        if (! convertToModelInput (loaded, reader->sampleRate, source->audio, source->trimmed))
        {
            error = "Could not convert " + source->originalName;
            return nullptr;
        }

        source->sampleRate = targetRate;
        if (source->audio.getNumSamples() <= 0)
        {
            error = "The audio file is empty: " + source->originalName;
            return nullptr;
        }

        if (! writeWav (destWav, source->audio, source->sampleRate))
        {
            error = "Could not save a copy of " + source->originalName;
            return nullptr;
        }

        source->file = destWav;
        return source;
    }
}
