#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <juce_core/juce_core.h>

namespace dooblrrhythm
{
    inline constexpr int numInstruments = 21;
    inline constexpr int maxBars = 16;
    inline constexpr int minBars = 1;
    inline constexpr int maxStepsPerBar = 16;
    inline constexpr int maxSteps = maxBars * maxStepsPerBar;
    inline constexpr int maxVoices = 64;
    inline constexpr int maxHits = 4096;

    enum class Instrument : uint8_t
    {
        clave = 0,
        cowbell,
        gankogui,
        agogo,
        quinto,
        conga,
        tumba,
        bongos,
        timbales,
        guiro,
        shakers,
        maracas,
        shekere,
        cabasa,
        ganza,
        djembe,
        talkingDrum,
        kenkeni,
        sangban,
        dundunba,
        udu
    };

    enum class Articulation : uint8_t
    {
        open = 0,
        slap,
        mute,
        heel,
        toe,
        tip,
        cascara,
        bell,
        mouth,
        scratch,
        shake,
        bass,
        tone,
        count
    };

    enum class Role : uint8_t
    {
        timekeeper = 0,
        claveKey,
        bassAnchor,
        interlockMid,
        leadSolo
    };

    enum class Register : uint8_t { bass = 0, mid, high };

    enum class Style : uint8_t
    {
        sonSalsa = 0,
        rumba,
        cumbia,
        sambaBatucada,
        afroCuban68,
        westAfrican,
        afrobeat,
        afroHouse,
        amapianoPerc,
        baileFunk,
        worldFusion,
        count
    };

    enum class TimeSignature : uint8_t
    {
        fourFour = 0,
        twoFour,
        threeFour,
        sixEight,
        twelveEight,
        count
    };

    enum class ClaveDirection : uint8_t { threeTwo = 0, twoThree };

    enum class Section : uint8_t
    {
        intro = 0,
        main,
        fill,
        brk,
        outro,
        count
    };

    inline constexpr const char* instrumentNames[numInstruments] {
        "Clave", "Cowbell", "Gankogui", "Agogo",
        "Quinto", "Conga", "Tumba", "Bongos", "Timbales",
        "Guiro", "Shakers", "Maracas", "Shekere", "Cabasa", "Ganza",
        "Djembe", "Talking", "Kenkeni", "Sangban", "Dundunba", "Udu"
    };

    inline constexpr const char* instrumentIds[numInstruments] {
        "clave", "cowbell", "gankogui", "agogo",
        "quinto", "conga", "tumba", "bongos", "timbales",
        "guiro", "shakers", "maracas", "shekere", "cabasa", "ganza",
        "djembe", "talking", "kenkeni", "sangban", "dundunba", "udu"
    };

    inline constexpr const char* styleNames[] {
        "Son / Salsa", "Rumba", "Cumbia", "Samba / Batucada", "Afro-Cuban 6/8",
        "West African", "Afrobeat", "Afro-house", "Amapiano perc", "Baile funk", "World / Fusion"
    };

    inline constexpr const char* timeSignatureNames[] { "4/4", "2/4", "3/4", "6/8", "12/8" };
    inline constexpr const char* claveNames[] { "3-2", "2-3" };
    inline constexpr const char* sectionNames[] { "Intro", "Main", "Fill", "Break", "Outro" };

    inline constexpr const char* articulationNames[(int) Articulation::count] {
        "Open", "Slap", "Mute", "Heel", "Toe", "Tip", "Cascara", "Bell",
        "Mouth", "Scratch", "Shake", "Bass", "Tone"
    };

    inline constexpr char articulationGlyph[(int) Articulation::count] {
        'O', 'S', 'M', 'H', 'T', 'i', 'C', 'B', 'o', 'x', 'k', 'D', 'n'
    };

    struct Hit
    {
        uint16_t step = 0;          // absolute step from the start of the pattern
        uint8_t instrument = 0;
        uint8_t articulation = 0;
        float velocity = 0.8f;      // 0..1
        float timing = 0.0f;        // -1..1, fraction of a step (negative = early)
        uint8_t structural = 1;     // 0 = ornament / ghost, 1 = groove, 2 = clave/key (never dropped)
    };

    inline int stepsPerBar (TimeSignature ts) noexcept
    {
        switch (ts)
        {
            case TimeSignature::fourFour:    return 16;
            case TimeSignature::twoFour:     return 8;
            case TimeSignature::threeFour:   return 12;
            case TimeSignature::sixEight:    return 12;
            case TimeSignature::twelveEight: return 12;
            case TimeSignature::count:       return 16;
        }
        return 16;
    }

    inline int beatsPerBar (TimeSignature ts) noexcept
    {
        switch (ts)
        {
            case TimeSignature::fourFour:    return 4;
            case TimeSignature::twoFour:     return 2;
            case TimeSignature::threeFour:   return 3;
            case TimeSignature::sixEight:    return 6;
            case TimeSignature::twelveEight: return 12;
            case TimeSignature::count:       return 4;
        }
        return 4;
    }

    inline int beatUnit (TimeSignature ts) noexcept
    {
        switch (ts)
        {
            case TimeSignature::fourFour:
            case TimeSignature::twoFour:
            case TimeSignature::threeFour:   return 4;
            case TimeSignature::sixEight:
            case TimeSignature::twelveEight: return 8;
            case TimeSignature::count:       return 4;
        }
        return 4;
    }

    inline bool isCompound (TimeSignature ts) noexcept
    {
        return ts == TimeSignature::sixEight || ts == TimeSignature::twelveEight;
    }

    inline bool usesClave (Style s) noexcept
    {
        return s == Style::sonSalsa || s == Style::rumba || s == Style::cumbia
            || s == Style::afrobeat || s == Style::worldFusion;
    }

    inline bool usesRumbaClave (Style s) noexcept { return s == Style::rumba; }

    inline bool usesSixEightBell (Style s) noexcept
    {
        return s == Style::afroCuban68 || s == Style::westAfrican;
    }

    inline Register instrumentRegister (Instrument i) noexcept
    {
        switch (i)
        {
            case Instrument::tumba:
            case Instrument::dundunba:
            case Instrument::udu:
            case Instrument::talkingDrum:
                return Register::bass;
            case Instrument::clave:
            case Instrument::cowbell:
            case Instrument::gankogui:
            case Instrument::agogo:
            case Instrument::guiro:
            case Instrument::shakers:
            case Instrument::maracas:
            case Instrument::shekere:
            case Instrument::cabasa:
            case Instrument::ganza:
                return Register::high;
            case Instrument::quinto:
            case Instrument::conga:
            case Instrument::bongos:
            case Instrument::timbales:
            case Instrument::djembe:
            case Instrument::kenkeni:
            case Instrument::sangban:
                return Register::mid;
        }
        return Register::mid;
    }

    inline int midiNoteFor (Instrument i, Articulation a) noexcept
    {
        // Dedicated map starting at C1, with extra notes per articulation so a DAW clip
        // round-trips without collapsing slap/open/mute.
        const int base = 24 + (int) i * 4;
        const int off = juce::jlimit (0, 3, (int) a % 4);
        return juce::jlimit (0, 127, base + off);
    }

    inline Instrument instrumentFromMidiNote (int note) noexcept
    {
        if (note < 24 || note >= 24 + numInstruments * 4)
            return Instrument::clave;
        return (Instrument) juce::jlimit (0, numInstruments - 1, (note - 24) / 4);
    }

    inline Articulation articulationFromMidiNote (int note) noexcept
    {
        if (note < 24)
            return Articulation::open;
        return (Articulation) ((note - 24) % 4);
    }

    /** MIDI note that starts or stops the pattern when the host is stopped. */
    inline constexpr int midiTransportNote = 36; // C2, also a common kick — used only as a toggle when host is idle.

    struct InstrumentControls
    {
        float density = 70.0f;
        float complexity = 40.0f;
        float syncopation = 35.0f;
        float velocity = 80.0f;
        float humanize = 20.0f;
        float timingOffset = 0.0f; // -50..50 ms-ish as % of a step
        float volumeDb = 0.0f;
        float pan = 0.0f;          // -100..100
        bool mute = false;
        bool solo = false;
        bool locked = false;
        float pitch = 0.0f;        // -12..12 st
        float decay = 50.0f;       // 0..100
    };

    struct GenerateRequest
    {
        Style style = Style::sonSalsa;
        TimeSignature timeSignature = TimeSignature::fourFour;
        ClaveDirection clave = ClaveDirection::threeTwo;
        Section section = Section::main;
        int bars = 4;
        float bpm = 120.0f;
        uint32_t seed = 1;
        float complexity = 45.0f;
        float syncopation = 40.0f;
        float humanize = 25.0f;
        float swing = 0.0f;
        float pushPull = 0.0f;
        std::array<InstrumentControls, numInstruments> instruments {};
        const struct Pattern* previous = nullptr; // locked parts copied from here
    };

    inline const char* paramId (int instrument, const char* suffix)
    {
        // "tumba_density" etc. Not for hot paths; the Parameters module caches String IDs.
        static thread_local char buf[64];
        std::snprintf (buf, sizeof (buf), "%s_%s", instrumentIds[instrument], suffix);
        return buf;
    }
}
