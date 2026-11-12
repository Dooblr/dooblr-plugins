#include "Clave.h"

namespace dooblrrhythm
{
    namespace
    {
        ClaveGrid make (int cycleBars, int spb, std::initializer_list<int> hits)
        {
            ClaveGrid g;
            g.cycleBars = cycleBars;
            g.stepsPerBar = spb;
            g.hitCount = 0;
            for (int h : hits)
                if (g.hitCount < 8)
                    g.hits[g.hitCount++] = h;
            return g;
        }
    }

    ClaveGrid sonClave (ClaveDirection direction, bool rumba) noexcept
    {
        // Two-bar 16th grid. 3-side first for 3-2.
        const int third = rumba ? 13 : 12;
        if (direction == ClaveDirection::threeTwo)
            return make (2, 16, { 0, 6, third, 16 + 4, 16 + 8 });
        // 2-3: the 2-side first, then the 3-side.
        return make (2, 16, { 4, 8, 16 + 0, 16 + 6, 16 + third });
    }

    ClaveGrid sixEightBell() noexcept
    {
        // 12-step 6/8 / 12/8 bell: x . x x . x . x . x . .
        return make (1, 12, { 0, 2, 3, 5, 7, 9 });
    }

    ClaveGrid sixEightClave() noexcept
    {
        // 5-stroke 6/8 clave: x . x . . x . x . . x .
        return make (1, 12, { 0, 2, 5, 7, 10 });
    }

    ClaveGrid partidoAlto() noexcept
    {
        return make (2, 16, { 2, 4, 5, 7, 16 + 0, 16 + 2, 16 + 5, 16 + 7 });
    }

    ClaveGrid gankogui() noexcept
    {
        // Kenkeni/gankogui 12/8: long bell with the double on pulses 2-3.
        return sixEightBell();
    }

    bool isKeyStroke (const ClaveGrid& grid, int step) noexcept
    {
        return keyStrokeIndex (grid, step) >= 0;
    }

    int keyStrokeIndex (const ClaveGrid& grid, int step) noexcept
    {
        const int cycle = juce::jmax (1, grid.cycleBars * grid.stepsPerBar);
        const int local = ((step % cycle) + cycle) % cycle;
        for (int i = 0; i < grid.hitCount; ++i)
            if (grid.hits[i] == local)
                return i;
        return -1;
    }

    int cycleBar (const ClaveGrid& grid, int step) noexcept
    {
        const int cycle = juce::jmax (1, grid.cycleBars * grid.stepsPerBar);
        const int local = ((step % cycle) + cycle) % cycle;
        return grid.stepsPerBar > 0 ? local / grid.stepsPerBar : 0;
    }

    ClaveGrid keyPatternFor (Style style, ClaveDirection direction, TimeSignature ts) noexcept
    {
        if (isCompound (ts) || usesSixEightBell (style))
            return style == Style::westAfrican ? gankogui() : sixEightClave();

        if (style == Style::sambaBatucada || style == Style::baileFunk)
            return partidoAlto();

        if (style == Style::rumba)
            return sonClave (direction, true);

        if (usesClave (style) || style == Style::afroHouse || style == Style::amapianoPerc)
            return sonClave (direction, false);

        return sonClave (direction, false);
    }

    bool isClaveConsistentOpen (Style style, ClaveDirection direction, TimeSignature ts, int step) noexcept
    {
        const auto grid = keyPatternFor (style, direction, ts);
        if (isKeyStroke (grid, step))
            return true;

        const int spb = stepsPerBar (ts);
        const int inBar = ((step % spb) + spb) % spb;

        if (isCompound (ts))
        {
            // 6/8 open tones sit on the dotted-quarter (0, 6) and the pickup 5 / 11.
            return inBar == 0 || inBar == 6 || inBar == 5 || inBar == 11;
        }

        // 4/4 tumbao opens: 4 and 4& (steps 12, 14). Bombo: & of 2 on the 3-side (step 6 of that bar).
        if (inBar == 12 || inBar == 14)
            return true;

        if (inBar == 6)
        {
            if (! usesClave (style) && style != Style::afroHouse)
                return true;
            const int bar = step / juce::jmax (1, spb);
            const bool threeSide = direction == ClaveDirection::threeTwo ? (bar % 2 == 0) : (bar % 2 == 1);
            return threeSide || usesRumbaClave (style);
        }
        return false;
    }
}
