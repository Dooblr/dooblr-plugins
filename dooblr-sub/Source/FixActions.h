#pragma once

#include "DSP/SystemCheck.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace dooblrsub
{
    class DooblrSubProcessor;

    /**
        One-click Fix: maps a failing / warning system-check reason onto a conservative
        set of parameter changes. All changes in one begin/endChangeGesture group so the
        host (and the in-plugin undo history) treat them as a single undo step.
    */
    class FixActions
    {
    public:
        explicit FixActions (DooblrSubProcessor& processor);

        bool canFix (dsp::CheckId id) const;
        bool canFixAll() const;
        bool canUndo() const noexcept { return hasSnapshot; }

        /** Applies a conservative recipe for one check. Returns true if any param moved. */
        bool apply (dsp::CheckId id);
        bool applyAll();
        bool undoLast();

        struct Current
        {
            float subAmount = 50.0f, loAmount = 25.0f, frequency = 60.0f, trackingSpeed = 55.0f, drive = 20.0f;
            float mix = 100.0f, output = 0.0f;
            int octaveMode = 0;
            bool monoBelow = true;
            float monoFreq = 100.0f;
            bool infraHpf = true;
            float infraFreq = 25.0f;
            int infraSlope = 0;
            bool subLimit = true;
            int target = 0;
            float eqShelfGain = 0.0f, eqShelfFreq = 32.0f;
            float eqPeakGain = 0.0f, eqPeakFreq = 90.0f, eqPeakQ = 1.0f;
            float eqPresenceGain = 0.0f, eqPresenceFreq = 160.0f, eqPresenceQ = 1.0f;
            float harmAmount = 0.0f;
            bool phaseInvert = false;
            float phaseDelay = 0.0f;
        };

        struct Proposal
        {
            Current values;
            int numChanges = 0;
        };

        static Current read (const juce::AudioProcessorValueTreeState& state);
        static Proposal propose (dsp::CheckId id, const dsp::Analysis& analysis, const Current& current);
        static Proposal proposeAll (const dsp::ReasonList& reasons, const dsp::Analysis& analysis, const Current& current);

    private:
        void captureSnapshot();
        bool commit (const Current& next);

        DooblrSubProcessor& proc;
        std::vector<float> snapshot;
        bool hasSnapshot = false;
    };
}
