#pragma once

#include "Clave.h"
#include "Pattern.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace dooblrrhythm
{
    struct GrooveSettings
    {
        float swing = 0.0f;     // 0..100, delays even 16ths (or the off-pulse in 6/8)
        float pushPull = 0.0f;  // -100..100, early / late the whole grid except clave
        float humanize = 0.0f;  // 0..100, per-hit timing + velocity noise
        Style style = Style::sonSalsa;
        TimeSignature timeSignature = TimeSignature::fourFour;
        uint32_t seed = 1;
    };

    /** Apply swing, push/pull and humanize in place. Clave/key strokes (structural == 2)
        never receive swing or push/pull; they can get a tiny humanize. */
    void applyGroove (Pattern& pattern, const GrooveSettings& groove);

    Role roleFor (Style style, Instrument instrument) noexcept;
    bool inStyleEnsemble (Style style, Instrument instrument) noexcept;

    /** One-shot generator. Deterministic in `request.seed`. Locked instruments are copied
        from `request.previous` (same absolute steps, truncated or wrapped to the new length). */
    Pattern generate (const GenerateRequest& request);

    /** MIDI file of the pattern (one track, ticks per bar = 96). */
    juce::MidiFile patternToMidi (const Pattern& pattern, double bpm);
    Pattern patternFromMidi (const juce::MidiFile& file, const Pattern& proto);

    juce::MidiFile patternToMidi (const Pattern& pattern, double bpm, int instrumentOnly);
}
