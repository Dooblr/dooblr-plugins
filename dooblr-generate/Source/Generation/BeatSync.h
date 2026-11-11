#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace dooblrgenerate::gen
{
    /** Discrete Sync lengths: beats or bars. Order is shortest-to-longest at a fixed time signature. */
    enum class LengthUnit : int { beat1 = 0, beat2, bar1, bar2, bar4, bar8, nUnits };

    inline int beatsFor (LengthUnit unit, int beatsPerBar)
    {
        const auto bpb = juce::jmax (1, beatsPerBar);
        switch (unit)
        {
            case LengthUnit::beat1: return 1;
            case LengthUnit::beat2: return 2;
            case LengthUnit::bar1:  return bpb;
            case LengthUnit::bar2:  return 2 * bpb;
            case LengthUnit::bar4:  return 4 * bpb;
            case LengthUnit::bar8:  return 8 * bpb;
            case LengthUnit::nUnits: break;
        }
        return bpb;
    }

    inline const char* unitName (LengthUnit unit)
    {
        switch (unit)
        {
            case LengthUnit::beat1: return "1 beat";
            case LengthUnit::beat2: return "2 beats";
            case LengthUnit::bar1:  return "1 bar";
            case LengthUnit::bar2:  return "2 bars";
            case LengthUnit::bar4:  return "4 bars";
            case LengthUnit::bar8:  return "8 bars";
            case LengthUnit::nUnits: break;
        }
        return "1 bar";
    }

    inline LengthUnit lengthUnitFromInt (int v)
    {
        return (LengthUnit) juce::jlimit (0, (int) LengthUnit::nUnits - 1, v);
    }

    inline double secondsForBeats (int beats, double bpm)
    {
        return (double) juce::jmax (1, beats) * 60.0 / juce::jmax (1.0, bpm);
    }

    struct BeatPlan
    {
        LengthUnit requested = LengthUnit::bar1;
        LengthUnit fitted = LengthUnit::bar1;
        double bpm = 120.0;
        int beatsPerBar = 4;
        int beats = 4;
        double uncappedSeconds = 2.0;
        double seconds = 2.0;
        bool capped = false;
        bool usingHostTempo = false;
    };

    /** seconds = beats*60/BPM, capped at maxSeconds. If the requested unit overshoots, pick the
        largest unit that fits; if even 1 beat is longer than the cap, use the cap. */
    inline BeatPlan makeBeatPlan (LengthUnit requested, double bpm, int beatsPerBar, double maxSeconds)
    {
        BeatPlan p;
        p.requested = lengthUnitFromInt ((int) requested);
        p.bpm = juce::jlimit (1.0, 400.0, bpm);
        p.beatsPerBar = juce::jmax (1, beatsPerBar);
        const auto cap = juce::jmax (0.25, maxSeconds);

        p.fitted = p.requested;
        p.beats = beatsFor (p.requested, p.beatsPerBar);
        p.uncappedSeconds = secondsForBeats (p.beats, p.bpm);
        p.seconds = juce::jmin (p.uncappedSeconds, cap);
        p.capped = p.uncappedSeconds > cap + 1.0e-9;

        if (p.capped)
        {
            p.fitted = LengthUnit::beat1;
            for (int i = 0; i < (int) LengthUnit::nUnits; ++i)
            {
                const auto u = (LengthUnit) i;
                if (secondsForBeats (beatsFor (u, p.beatsPerBar), p.bpm) <= cap + 1.0e-9)
                    p.fitted = u;
            }
            p.beats = beatsFor (p.fitted, p.beatsPerBar);
            p.seconds = juce::jmin (secondsForBeats (p.beats, p.bpm), cap);
        }

        return p;
    }

    inline int targetSamples (double seconds, double sampleRate)
    {
        return juce::jmax (1, (int) std::llround (juce::jmax (0.0, seconds) * juce::jmax (1.0, sampleRate)));
    }

    inline juce::String tempoPromptSuffix (double bpm)
    {
        return juce::String (juce::roundToInt (bpm)) + " BPM";
    }

    inline juce::String appendTempoToPrompt (const juce::String& prompt, double bpm)
    {
        const auto suffix = tempoPromptSuffix (bpm);
        const auto trimmed = prompt.trim();
        if (trimmed.isEmpty())
            return suffix;
        if (trimmed.containsIgnoreCase (suffix))
            return trimmed;
        return trimmed + ", " + suffix;
    }

    inline juce::String filenameBpmTag (double bpm)
    {
        return juce::String (juce::roundToInt (bpm)) + "bpm";
    }

    /** Trim or pad to nTarget samples and apply short linear fades so a loop on the grid
        does not click. Does not time-stretch. */
    inline void trimPadWithFades (juce::AudioBuffer<float>& audio, int nTarget, double sampleRate)
    {
        nTarget = juce::jmax (1, nTarget);
        const auto nIn = audio.getNumSamples();
        const auto nCh = juce::jmax (1, audio.getNumChannels());

        if (nIn != nTarget)
        {
            juce::AudioBuffer<float> out (nCh, nTarget);
            out.clear();
            const auto nCopy = juce::jmin (nIn, nTarget);
            if (nCopy > 0)
                for (int ch = 0; ch < nCh; ++ch)
                    out.copyFrom (ch, 0, audio, ch, 0, nCopy);
            audio = std::move (out);
        }

        const auto fade = juce::jlimit (1, juce::jmax (1, nTarget / 4),
                                        (int) std::llround (0.008 * juce::jmax (1.0, sampleRate)));
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* d = audio.getWritePointer (ch);
            for (int i = 0; i < fade; ++i)
            {
                const auto g = (float) (i + 1) / (float) fade;
                d[i] *= g;
                d[nTarget - 1 - i] *= g;
            }
        }
    }
}
