#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrkick
{
    /**
        Undo/redo of the plug-in's own UI edits, built on juce::UndoManager.

        One transaction per user gesture: everything changed between the first
        beginChangeGesture() and the matching last endChangeGesture() (a knob drag, a handle
        drag that moves frequency and gain together, a bandwidth-handle drag, a combo or
        toggle change, a double-click enable). Rapid steps of the same continuous parameter
        (scroll-wheel ticks less than mergeWindowSeconds apart) merge into one entry.

        Only gestures create entries. Host automation and state loads change parameters
        without gestures, so they never reach the history. JUCE's
        AudioProcessorValueTreeState undo support is deliberately not used: it records every
        value change from a 10-50 Hz flush timer, including host automation, and splits
        gestures across timer ticks.

        Message thread only (gesture callbacks from other threads are ignored).
    */
    class ParameterUndoHistory final : private juce::AudioProcessorParameter::Listener
    {
    public:
        static constexpr int defaultMaxTransactions = 50;
        static constexpr double mergeWindowSeconds = 0.6;

        explicit ParameterUndoHistory (juce::AudioProcessor& processorToWatch,
                                       int maxTransactions = defaultMaxTransactions);
        ~ParameterUndoHistory() override;

        bool undo();
        bool redo();
        bool canUndo() const;
        bool canRedo() const;

        /** Number of undoable steps currently stored (at most the limit). */
        int getNumUndoableSteps() const;
        int getMaxTransactions() const noexcept { return maxTransactions; }

        /** Drops all history (e.g. after a preset or session load). */
        void clear();

        /** Broadcasts a change message whenever the history changes (for button states). */
        juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    private:
        struct Change
        {
            juce::AudioProcessorParameter* parameter = nullptr;
            float before = 0.0f, after = 0.0f;
        };

        class GestureAction;

        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
        void commitGesture();
        void apply (const std::vector<Change>& changes, bool useAfter);

        juce::AudioProcessor& processor;
        juce::UndoManager undoManager;
        const int maxTransactions;

        struct Pending
        {
            juce::AudioProcessorParameter* parameter;
            float before;
        };

        std::vector<Pending> pending;
        int gestureDepth = 0;
        bool applying = false;
        double gestureStartTime = 0.0;
        double lastCommitTime = -1.0e9;
        bool lastCommitMergeable = false;
        juce::AudioProcessorParameter* lastCommitParameter = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterUndoHistory)
    };
}
