#pragma once

#include "Types.h"

namespace dooblrrhythm
{
    /**
        Canonical clave, bell and partido-alto grids.

        Son 3-2 in 16th notes over two 4/4 bars: 0, 6, 12 | 4, 8
        (beat 1, & of 2, beat 4  |  beat 2, beat 3 of the next bar).
        Rumba 3-2 delays the last stroke of the 3-side onto the & of 4: 0, 6, 13 | 4, 8.
        2-3 is the same cycle rotated by one bar.

        6/8 / 12/8 bell (gankogui / rumba 6/8 clave) in 12 eighths: 0, 2, 3, 5, 7, 9
        (the standard "long bell" with the double stroke on the second pulse group).
        A sparser 5-stroke 6/8 clave used as a key pattern: 0, 2, 5, 7, 10.

        Partido alto (samba) in 16ths over two bars: 2, 4, 5, 7 | 8, 10, 13, 15
        of a 16-step cycle — the typical 2-bar partido-alto skeleton.
    */
    struct ClaveGrid
    {
        int cycleBars = 2;
        int stepsPerBar = 16;
        int hits[8] {};
        int hitCount = 0;
    };

    ClaveGrid sonClave (ClaveDirection direction, bool rumba) noexcept;
    ClaveGrid sixEightBell() noexcept;
    ClaveGrid sixEightClave() noexcept;
    ClaveGrid partidoAlto() noexcept;
    ClaveGrid gankogui() noexcept;

    /** True when `step` (absolute) is a clave/key stroke for this grid. */
    bool isKeyStroke (const ClaveGrid& grid, int step) noexcept;

    /** 0-based index of the clave stroke in its cycle, or -1. */
    int keyStrokeIndex (const ClaveGrid& grid, int step) noexcept;

    /** Which clave cycle bar (0 or 1 for a 2-bar clave) an absolute step falls in. */
    int cycleBar (const ClaveGrid& grid, int step) noexcept;

    /** The style's primary key pattern (clave, 6/8 bell, partido alto, or a fusion blend). */
    ClaveGrid keyPatternFor (Style style, ClaveDirection direction, TimeSignature ts) noexcept;

    /** Steps that are "on the clave" in the sense of tumbao placement: the 3-side & of 2
        (bombo) and the 4 / 4& of each bar, plus the clave strokes themselves. */
    bool isClaveConsistentOpen (Style style, ClaveDirection direction, TimeSignature ts, int step) noexcept;
}
