#include "RangeLearner.h"

#include <cmath>

#include "Parameters.h"

namespace dooblrvocals
{
    namespace
    {
        float midiForHz (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
        float hzForMidi (float midi) noexcept { return 440.0f * std::exp2 ((midi - 69.0f) / 12.0f); }
    }

    void RangeLearner::clear() noexcept
    {
        histogram.fill (0.0);
        totalSeconds = 0.0;
        lastHz = 0.0f;
    }

    void RangeLearner::add (const dsp::PitchFrame* frames, int numFrames) noexcept
    {
        for (int i = 0; i < numFrames; ++i)
        {
            const auto& f = frames[i];
            if (! f.voiced || f.confidence < minConfidence || ! (f.detectedHz > 20.0f) || ! std::isfinite (f.detectedHz))
                continue;

            const auto bin = (int) std::floor ((midiForHz (f.detectedHz) - (float) lowestNote) * (float) binsPerSemitone);
            if (bin < 0 || bin >= numBins)
                continue;

            const auto seconds = f.seconds > 0.0f ? (double) f.seconds : 64.0 / 48000.0;
            histogram[(size_t) bin] += seconds;
            totalSeconds += seconds;
            lastHz = f.detectedHz;
        }
    }

    LearnedRange RangeLearner::compute() const
    {
        LearnedRange r;
        r.voicedSeconds = (float) totalSeconds;

        if (totalSeconds < (double) minVoicedSeconds)
        {
            r.message = "Not enough voice detected (" + juce::String (totalSeconds, 1) + " s of "
                      + juce::String (minVoicedSeconds, 1) + " s) - range unchanged";
            return r;
        }

        const auto percentileMidi = [this] (double p)
        {
            const auto targetWeight = p * totalSeconds;
            auto cumulative = 0.0;
            for (int b = 0; b < numBins; ++b)
            {
                const auto before = cumulative;
                cumulative += histogram[(size_t) b];
                if (cumulative >= targetWeight && histogram[(size_t) b] > 0.0)
                {
                    const auto within = (targetWeight - before) / histogram[(size_t) b];
                    return (float) lowestNote + ((float) b + (float) std::clamp (within, 0.0, 1.0)) / (float) binsPerSemitone;
                }
            }
            return (float) highestNote;
        };

        const auto lowMidi = percentileMidi (lowPercentile);
        const auto highMidi = std::max (lowMidi + 1.0f, percentileMidi (highPercentile));

        r.valid = true;
        r.lowHz = hzForMidi (lowMidi);
        r.highHz = hzForMidi (highMidi);
        r.searchLowHz = std::max (dsp::liveMinHz, hzForMidi (lowMidi - marginSemitones));
        r.searchHighHz = std::min (1400.0f, hzForMidi (highMidi + marginSemitones));
        r.type = typeFor (r.lowHz, r.highHz);
        r.message = "Learned " + rangeText (r.lowHz, r.highHz) + " (" + juce::String (totalSeconds, 1) + " s voiced)";
        return r;
    }

    dsp::InputType RangeLearner::typeFor (float lowHz, float highHz) noexcept
    {
        auto best = dsp::InputType::tenor;
        auto bestScore = -1.0e9f;
        const auto lo = std::log2 (lowHz), hi = std::log2 (highHz);

        for (int t = 0; t < (int) dsp::InputType::numTypes; ++t)
        {
            const auto range = dsp::rangeFor ((dsp::InputType) t);
            const auto tLo = std::log2 (range.minHz), tHi = std::log2 (range.maxHz);
            const auto overlap = std::min (hi, tHi) - std::max (lo, tLo);
            const auto uncovered = std::max (0.0f, tLo - lo) + std::max (0.0f, hi - tHi);
            // Prefer covering the sung range; among those, the tightest type.
            const auto score = overlap - 2.0f * uncovered - 0.1f * (tHi - tLo);
            if (score > bestScore)
            {
                bestScore = score;
                best = (dsp::InputType) t;
            }
        }

        return best;
    }

    juce::String RangeLearner::rangeText (float lowHz, float highHz)
    {
        return params::noteName (juce::roundToInt (midiForHz (lowHz))) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
             + params::noteName (juce::roundToInt (midiForHz (highHz)));
    }
}
