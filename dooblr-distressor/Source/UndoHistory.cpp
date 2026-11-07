#include "UndoHistory.h"

namespace dooblrdistressor
{
    //==============================================================================
    class ParameterUndoHistory::GestureAction final : public juce::UndoableAction
    {
    public:
        GestureAction (ParameterUndoHistory& ownerToUse, std::vector<Change> changesToStore, bool canMerge)
            : owner (ownerToUse), changes (std::move (changesToStore)), mergeable (canMerge)
        {
        }

        bool perform() override
        {
            // The first perform() is the gesture itself, which has already happened.
            if (! performedOnce)
            {
                performedOnce = true;
                return true;
            }

            owner.apply (changes, true);
            return true;
        }

        bool undo() override
        {
            owner.apply (changes, false);
            return true;
        }

        int getSizeInUnits() override { return 1; }

        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* nextAction) override
        {
            auto* next = dynamic_cast<GestureAction*> (nextAction);

            if (next == nullptr || ! mergeable || ! next->mergeable || next->changes.size() != changes.size())
                return nullptr;

            for (size_t i = 0; i < changes.size(); ++i)
                if (changes[i].parameter != next->changes[i].parameter)
                    return nullptr;

            auto merged = changes;

            for (size_t i = 0; i < merged.size(); ++i)
                merged[i].after = next->changes[i].after;

            auto* action = new GestureAction (owner, std::move (merged), true);
            action->performedOnce = true;
            return action;
        }

    private:
        ParameterUndoHistory& owner;
        std::vector<Change> changes;
        bool mergeable = false;
        bool performedOnce = false;
    };

    //==============================================================================
    ParameterUndoHistory::ParameterUndoHistory (juce::AudioProcessor& processorToWatch, int maxTransactionsToKeep)
        : processor (processorToWatch),
          // Every action counts as one unit and the unit budget is 1, so UndoManager trims
          // the oldest transactions until exactly maxTransactions remain.
          undoManager (1, juce::jmax (1, maxTransactionsToKeep)),
          maxTransactions (juce::jmax (1, maxTransactionsToKeep))
    {
        for (auto* parameter : processor.getParameters())
            parameter->addListener (this);
    }

    ParameterUndoHistory::~ParameterUndoHistory()
    {
        for (auto* parameter : processor.getParameters())
            parameter->removeListener (this);
    }

    void ParameterUndoHistory::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
    {
        if (applying || ! juce::MessageManager::existsAndIsCurrentThread())
            return;

        auto* parameter = processor.getParameters()[parameterIndex];

        if (parameter == nullptr)
            return;

        if (gestureIsStarting)
        {
            if (gestureDepth == 0)
            {
                pending.clear();
                gestureStartTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
            }

            ++gestureDepth;

            const auto known = std::any_of (pending.begin(), pending.end(),
                                            [parameter] (const Pending& p) { return p.parameter == parameter; });

            if (! known)
                pending.push_back ({ parameter, parameter->getValue() });

            return;
        }

        if (gestureDepth == 0)
            return; // an end without a start (e.g. the editor opened mid-gesture)

        if (--gestureDepth == 0)
            commitGesture();
    }

    void ParameterUndoHistory::commitGesture()
    {
        std::vector<Change> changes;

        for (const auto& p : pending)
        {
            const auto after = p.parameter->getValue();

            if (! juce::approximatelyEqual (after, p.before))
                changes.push_back ({ p.parameter, p.before, after });
        }

        pending.clear();

        if (changes.empty())
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        // Quick steps of one continuous parameter (wheel ticks) can merge; drags, toggles
        // and choices are always their own entries.
        const auto quick = now - gestureStartTime < 0.25;
        const auto continuous = changes.size() == 1 && ! changes.front().parameter->isDiscrete()
                                && ! changes.front().parameter->isBoolean();
        const auto mergeable = quick && continuous;
        const auto mergeWithLast = mergeable && lastCommitMergeable && canUndo() && ! canRedo()
                                   && changes.front().parameter == lastCommitParameter
                                   && now - lastCommitTime < mergeWindowSeconds;

        auto* firstParameter = changes.front().parameter;

        if (! mergeWithLast)
            undoManager.beginNewTransaction();

        undoManager.perform (new GestureAction (*this, std::move (changes), mergeable));
        lastCommitTime = now;
        lastCommitMergeable = mergeable;
        lastCommitParameter = mergeable ? firstParameter : nullptr;
    }

    void ParameterUndoHistory::apply (const std::vector<Change>& changes, bool useAfter)
    {
        const juce::ScopedValueSetter<bool> guard (applying, true);

        for (const auto& change : changes)
        {
            change.parameter->beginChangeGesture();
            change.parameter->setValueNotifyingHost (useAfter ? change.after : change.before);
            change.parameter->endChangeGesture();
        }
    }

    void ParameterUndoHistory::appendToLastTransaction (std::unique_ptr<juce::UndoableAction> action)
    {
        if (action == nullptr)
            return;

        if (! canUndo() || canRedo())
            undoManager.beginNewTransaction();

        undoManager.perform (action.release());
        lastCommitMergeable = false; // the next gesture is a step of its own
    }

    bool ParameterUndoHistory::undo()
    {
        lastCommitMergeable = false;
        return undoManager.undo();
    }

    bool ParameterUndoHistory::redo()
    {
        lastCommitMergeable = false;
        return undoManager.redo();
    }

    bool ParameterUndoHistory::canUndo() const { return undoManager.canUndo(); }
    bool ParameterUndoHistory::canRedo() const { return undoManager.canRedo(); }

    int ParameterUndoHistory::getNumUndoableSteps() const
    {
        // UndoManager has no direct count; walk the descriptions of the undoable transactions.
        return undoManager.getUndoDescriptions().size();
    }

    void ParameterUndoHistory::clear()
    {
        pending.clear();
        gestureDepth = 0;
        lastCommitMergeable = false;
        undoManager.clearUndoHistory();
    }
}
