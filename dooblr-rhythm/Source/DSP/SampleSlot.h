#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace dooblrrhythm::dsp
{
    struct Sample
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::File file;
        int instrument = 0;
        int articulation = 0;

        static std::shared_ptr<Sample> load (const juce::File& file, juce::String& error);
    };

    class SampleSlot
    {
    public:
        ~SampleSlot();
        void publish (std::shared_ptr<const Sample> sample);
        std::shared_ptr<const Sample> current() const;
        const Sample* acquire() noexcept;
        int numAlive() const;
        void collect();

    private:
        mutable juce::CriticalSection writeLock;
        std::vector<std::shared_ptr<const Sample>> alive;
        std::shared_ptr<const Sample> published;
        std::atomic<const Sample*> publishedRaw { nullptr };
        std::atomic<const Sample*> inUse { nullptr };
    };
}
