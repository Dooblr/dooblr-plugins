#pragma once

#include "../Engine/Sample.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace dooblrgenerate::gen
{
    /**
        A dropped source sample, copied into the plug-in's working folder as 44.1 kHz stereo
        (trimmed to the longest model input, 11 s). Used as init audio for Variation.
    */
    struct SourceAudio
    {
        juce::AudioBuffer<float> audio;   // stereo
        double sampleRate = targetRate;
        juce::File file;                  // the copy in the working folder
        juce::String originalName;        // name the user dropped (for display)
        double originalSeconds = 0.0;     // length before trim
        bool trimmed = false;

        double lengthSeconds() const { return sampleRate > 0 ? audio.getNumSamples() / sampleRate : 0.0; }

        static constexpr double targetRate = 44100.0;
        static constexpr int targetChannels = 2;
        static constexpr double maxSeconds = 11.0;

        /** WAV / AIFF / FLAC, plus CoreAudio formats (mp3, caf, ...) on macOS. */
        static void registerFormats (juce::AudioFormatManager&);

        /**
            If file is readable audio, returns it. If it is an Ableton .alc clip, returns the
            audio file the clip names, but only when that path exists and is readable audio.
            Otherwise nullptr and a short error.
        */
        static juce::File resolveDroppedFile (const juce::File& file, juce::String& error);

        /** Mono -> stereo (duplicate), extra channels dropped, resampled to 44.1 kHz, trimmed to maxSeconds. */
        static bool convertToModelInput (const juce::AudioBuffer<float>& in, double inRate,
                                         juce::AudioBuffer<float>& out, bool& trimmed);

        /** Reads, converts, and writes destWav (24-bit WAV). destWav's parent is created. */
        static std::unique_ptr<SourceAudio> loadAndStore (const juce::File& dropped, const juce::File& destWav,
                                                          juce::String& error);

        static bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate);
    };

    /** True for a model that can run audio-to-audio variation (Stable Audio Open Small, test backend). */
    bool modelSupportsSourceVariation (const juce::String& modelId);
}
