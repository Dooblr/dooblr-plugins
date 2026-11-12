#include "Pattern.h"

#include <algorithm>
#include <cmath>

namespace dooblrrhythm
{
    juce::ValueTree Pattern::toTree() const
    {
        juce::ValueTree t ("Pattern");
        t.setProperty ("style", (int) style, nullptr);
        t.setProperty ("timeSignature", (int) timeSignature, nullptr);
        t.setProperty ("clave", (int) clave, nullptr);
        t.setProperty ("section", (int) section, nullptr);
        t.setProperty ("bars", bars, nullptr);
        t.setProperty ("stepsPerBar", stepsPerBar, nullptr);
        t.setProperty ("seed", (int) seed, nullptr);

        for (const auto& h : hits)
        {
            juce::ValueTree c ("Hit");
            c.setProperty ("i", (int) h.instrument, nullptr);
            c.setProperty ("s", (int) h.step, nullptr);
            c.setProperty ("a", (int) h.articulation, nullptr);
            c.setProperty ("v", (double) h.velocity, nullptr);
            c.setProperty ("t", (double) h.timing, nullptr);
            c.setProperty ("k", (int) h.structural, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    Pattern Pattern::fromTree (const juce::ValueTree& tree)
    {
        Pattern p;
        if (! tree.hasType ("Pattern"))
            return p;

        p.style = (Style) (int) tree.getProperty ("style", 0);
        p.timeSignature = (TimeSignature) (int) tree.getProperty ("timeSignature", 0);
        p.clave = (ClaveDirection) (int) tree.getProperty ("clave", 0);
        p.section = (Section) (int) tree.getProperty ("section", 1);
        p.bars = juce::jlimit (minBars, maxBars, (int) tree.getProperty ("bars", 4));
        p.stepsPerBar = juce::jlimit (6, maxStepsPerBar, (int) tree.getProperty ("stepsPerBar", 16));
        p.seed = (uint32_t) (int) tree.getProperty ("seed", 1);

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto c = tree.getChild (i);
            if (! c.hasType ("Hit"))
                continue;
            Hit h;
            h.instrument = (uint8_t) juce::jlimit (0, numInstruments - 1, (int) c.getProperty ("i", 0));
            h.step = (uint16_t) juce::jlimit (0, maxSteps - 1, (int) c.getProperty ("s", 0));
            h.articulation = (uint8_t) juce::jlimit (0, (int) Articulation::count - 1, (int) c.getProperty ("a", 0));
            h.velocity = juce::jlimit (0.0f, 1.0f, (float) (double) c.getProperty ("v", 0.8));
            h.timing = juce::jlimit (-1.0f, 1.0f, (float) (double) c.getProperty ("t", 0.0));
            h.structural = (uint8_t) juce::jlimit (0, 2, (int) c.getProperty ("k", 1));
            p.add (h);
        }
        return p;
    }

    bool Pattern::operator== (const Pattern& other) const
    {
        if (style != other.style || timeSignature != other.timeSignature || clave != other.clave
            || section != other.section || bars != other.bars || stepsPerBar != other.stepsPerBar
            || seed != other.seed || hits.size() != other.hits.size())
            return false;

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto& a = hits[i];
            const auto& b = other.hits[i];
            if (a.step != b.step || a.instrument != b.instrument || a.articulation != b.articulation
                || a.structural != b.structural
                || std::abs (a.velocity - b.velocity) > 1.0e-5f
                || std::abs (a.timing - b.timing) > 1.0e-5f)
                return false;
        }
        return true;
    }

    PatternSlot::PatternSlot()
    {
        auto empty = std::make_shared<Pattern>();
        publish (std::move (empty));
    }

    PatternSlot::~PatternSlot()
    {
        publishedRaw.store (nullptr);
        inUse.store (nullptr);
    }

    void PatternSlot::publish (std::shared_ptr<const Pattern> pattern)
    {
        const juce::ScopedLock sl (writeLock);

        if (pattern != nullptr && std::find (alive.begin(), alive.end(), pattern) == alive.end())
            alive.push_back (pattern);

        published = pattern;
        publishedRaw.store (pattern.get(), std::memory_order_seq_cst);

        const auto* used = inUse.load (std::memory_order_seq_cst);
        alive.erase (std::remove_if (alive.begin(), alive.end(), [&] (const std::shared_ptr<const Pattern>& s)
                     {
                         return s != published && s.get() != used;
                     }),
                     alive.end());
    }

    void PatternSlot::collect() { publish (current()); }

    std::shared_ptr<const Pattern> PatternSlot::current() const
    {
        const juce::ScopedLock sl (writeLock);
        return published;
    }

    const Pattern* PatternSlot::acquire() noexcept
    {
        for (;;)
        {
            const auto* p = publishedRaw.load (std::memory_order_seq_cst);
            inUse.store (p, std::memory_order_seq_cst);
            if (publishedRaw.load (std::memory_order_seq_cst) == p)
                return p;
        }
    }

    int PatternSlot::numAlive() const
    {
        const juce::ScopedLock sl (writeLock);
        return (int) alive.size();
    }
}
