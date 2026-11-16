#pragma once

#include "Types.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace dooblrrhythm
{
    struct Pattern
    {
        Style style = Style::sonSalsa;
        TimeSignature timeSignature = TimeSignature::fourFour;
        ClaveDirection clave = ClaveDirection::threeTwo;
        Section section = Section::main;
        int bars = 4;
        int stepsPerBar = 16;
        uint32_t seed = 1;
        std::vector<Hit> hits;
        std::array<bool, numInstruments> used {};

        int totalSteps() const noexcept { return bars * stepsPerBar; }

        void clear()
        {
            hits.clear();
            used.fill (false);
        }

        void add (Hit h)
        {
            if (h.instrument >= numInstruments)
                return;
            hits.push_back (h);
            used[(size_t) h.instrument] = true;
        }

        template <typename Fn>
        void forInstrument (int instrument, Fn&& fn) const
        {
            for (const auto& h : hits)
                if ((int) h.instrument == instrument)
                    fn (h);
        }

        int countInstrument (int instrument) const
        {
            int n = 0;
            for (const auto& h : hits)
                n += (int) h.instrument == instrument;
            return n;
        }

        bool hasHit (int instrument, int step, int artic = -1) const
        {
            for (const auto& h : hits)
                if ((int) h.instrument == instrument && (int) h.step == step
                    && (artic < 0 || (int) h.articulation == artic))
                    return true;
            return false;
        }

        juce::ValueTree toTree() const;
        static Pattern fromTree (const juce::ValueTree& tree);
        bool operator== (const Pattern& other) const;
    };

    /** Message-thread publish / audio-thread acquire, no locks on the audio thread. */
    class PatternSlot
    {
    public:
        PatternSlot();
        ~PatternSlot();

        void publish (std::shared_ptr<const Pattern> pattern);
        std::shared_ptr<const Pattern> current() const;
        const Pattern* acquire() noexcept;
        int numAlive() const;
        void collect();

    private:
        mutable juce::CriticalSection writeLock;
        std::vector<std::shared_ptr<const Pattern>> alive;
        std::shared_ptr<const Pattern> published;
        std::atomic<const Pattern*> publishedRaw { nullptr };
        std::atomic<const Pattern*> inUse { nullptr };
    };
}
