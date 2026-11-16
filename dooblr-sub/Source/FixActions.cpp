#include "FixActions.h"

#include "Parameters.h"
#include "PluginProcessor.h"

namespace dooblrsub
{
    namespace id = params::id;

    namespace
    {
        float getReal (const juce::AudioProcessorValueTreeState& state, const char* pid, float fallback)
        {
            if (auto* p = state.getParameter (pid))
                return p->convertFrom0to1 (p->getValue());
            return fallback;
        }

        bool getBool (const juce::AudioProcessorValueTreeState& state, const char* pid, bool fallback)
        {
            return getReal (state, pid, fallback ? 1.0f : 0.0f) > 0.5f;
        }

        int getChoice (const juce::AudioProcessorValueTreeState& state, const char* pid, int fallback)
        {
            return juce::jlimit (0, 8, (int) std::lround (getReal (state, pid, (float) fallback)));
        }

        bool differs (float a, float b, float eps = 1.0e-4f) noexcept
        {
            return std::abs (a - b) > eps;
        }

        void setIf (FixActions::Current& c, float FixActions::Current::* member, float value, int& n)
        {
            if (differs (c.*member, value))
            {
                c.*member = value;
                ++n;
            }
        }
    }

    FixActions::FixActions (DooblrSubProcessor& processor) : proc (processor) {}

    FixActions::Current FixActions::read (const juce::AudioProcessorValueTreeState& state)
    {
        Current c;
        c.subAmount = getReal (state, id::subAmount, c.subAmount);
        c.loAmount = getReal (state, id::loAmount, c.loAmount);
        c.frequency = getReal (state, id::frequency, c.frequency);
        c.octaveMode = getChoice (state, id::octaveMode, c.octaveMode);
        c.trackingSpeed = getReal (state, id::trackingSpeed, c.trackingSpeed);
        c.drive = getReal (state, id::drive, c.drive);
        c.mix = getReal (state, id::mix, c.mix);
        c.output = getReal (state, id::output, c.output);
        c.monoBelow = getBool (state, id::monoBelow, c.monoBelow);
        c.monoFreq = getReal (state, id::monoFreq, c.monoFreq);
        c.infraHpf = getBool (state, id::infraHpf, c.infraHpf);
        c.infraFreq = getReal (state, id::infraFreq, c.infraFreq);
        c.infraSlope = getChoice (state, id::infraSlope, c.infraSlope);
        c.subLimit = getBool (state, id::subLimit, c.subLimit);
        c.target = getChoice (state, id::target, c.target);
        c.eqShelfGain = getReal (state, id::eqShelfGain, c.eqShelfGain);
        c.eqShelfFreq = getReal (state, id::eqShelfFreq, c.eqShelfFreq);
        c.eqPeakGain = getReal (state, id::eqPeakGain, c.eqPeakGain);
        c.eqPeakFreq = getReal (state, id::eqPeakFreq, c.eqPeakFreq);
        c.eqPeakQ = getReal (state, id::eqPeakQ, c.eqPeakQ);
        c.eqPresenceGain = getReal (state, id::eqPresenceGain, c.eqPresenceGain);
        c.eqPresenceFreq = getReal (state, id::eqPresenceFreq, c.eqPresenceFreq);
        c.eqPresenceQ = getReal (state, id::eqPresenceQ, c.eqPresenceQ);
        c.harmAmount = getReal (state, id::harmAmount, c.harmAmount);
        c.phaseInvert = getBool (state, id::phaseInvert, c.phaseInvert);
        c.phaseDelay = getReal (state, id::phaseDelay, c.phaseDelay);
        return c;
    }

    FixActions::Proposal FixActions::propose (dsp::CheckId check, const dsp::Analysis& a, const Current& current)
    {
        Proposal p;
        p.values = current;
        auto& v = p.values;
        const auto th = dsp::thresholdsFor ((dsp::Target) juce::jlimit (0, 2, current.target));
        int n = 0;

        switch (check)
        {
            case dsp::CheckId::infra:
            {
                v.infraHpf = true;
                if (! current.infraHpf) ++n;
                // 12 dB/oct HPF at the top of the range plus a low shelf at 20 Hz: a 6 dB/oct
                // cut at 28 Hz cannot bring an 18+50 Hz mix under the Club infra ratio.
                auto hz = juce::jmax (current.infraFreq, 36.0f + th.infraHzBias);
                const auto ratio = a.infraRms / juce::jmax (1.0e-12f, a.subRms);
                if (ratio > th.infraRatioWarn || a.bandRms[0] > 0.45f * juce::jmax (1.0e-12f, a.subRms))
                    hz = params::infraMaxHz;
                hz = juce::jlimit (params::infraMinHz, params::infraMaxHz, hz);
                setIf (v, &Current::infraFreq, hz, n);
                if (current.infraSlope < 2)
                {
                    v.infraSlope = 2; // 24 dB/oct: 6 dB cannot separate 18 Hz rumble from 50 Hz
                    ++n;
                }
                setIf (v, &Current::eqShelfGain, juce::jmin (current.eqShelfGain, -12.0f), n);
                setIf (v, &Current::eqShelfFreq, juce::jlimit (params::eqShelfMinHz, params::eqShelfMaxHz, 20.0f), n);
                break;
            }
            case dsp::CheckId::stereoLows:
            {
                v.monoBelow = true;
                if (! current.monoBelow) ++n;
                // Always take Mono to the top of the range: a 1-pole 100 Hz sum still leaves
                // enough 50 Hz side energy to fail evaluate().
                setIf (v, &Current::monoFreq, params::monoMaxHz, n);
                if (current.mix < 99.0f)
                    setIf (v, &Current::mix, 100.0f, n);
                break;
            }
            case dsp::CheckId::clipping:
            {
                v.subLimit = true;
                if (! current.subLimit) ++n;
                const auto peak = juce::jmax (a.headroomPeak, a.subPeak);
                auto out = current.output;
                if (peak >= th.clipPeak)
                    out = juce::jmax (-24.0f, out - 2.5f);
                else if (peak >= th.clipPeakWarn)
                    out = juce::jmax (-24.0f, out - 1.2f);
                setIf (v, &Current::output, out, n);
                if (peak >= 0.99f)
                    setIf (v, &Current::subAmount, juce::jmax (10.0f, current.subAmount * 0.85f), n);
                break;
            }
            case dsp::CheckId::cancellation:
            {
                v.phaseInvert = ! current.phaseInvert;
                ++n;
                if (std::abs (a.cancellation) > 0.7f && current.phaseDelay < 0.2f)
                {
                    const auto hz = a.pitchHz > 1.0f ? a.pitchHz : juce::jmax (30.0f, current.frequency);
                    const auto halfMs = 500.0f / hz;
                    setIf (v, &Current::phaseDelay, juce::jlimit (0.0f, params::phaseDelayMaxMs, 0.35f * halfMs), n);
                }
                break;
            }
            case dsp::CheckId::dcOffset:
            {
                v.infraHpf = true;
                if (! current.infraHpf) ++n;
                setIf (v, &Current::infraFreq, juce::jmax (current.infraFreq, 22.0f), n);
                break;
            }
            case dsp::CheckId::unstablePitch:
            {
                // Higher Tracking is more smoothing (8–180 ms).
                setIf (v, &Current::trackingSpeed, juce::jlimit (55.0f, 90.0f, juce::jmax (current.trackingSpeed, 78.0f)), n);
                break;
            }
            case dsp::CheckId::sustained:
            {
                setIf (v, &Current::subAmount, juce::jmax (12.0f, current.subAmount * 0.72f), n);
                setIf (v, &Current::output, juce::jmax (-24.0f, current.output - 3.0f), n);
                if (v.eqShelfGain > -2.0f)
                    setIf (v, &Current::eqShelfGain, -4.0f, n);
                v.subLimit = true;
                if (! current.subLimit) ++n;
                break;
            }
            case dsp::CheckId::subWeak:
            {
                setIf (v, &Current::subAmount, juce::jmin (70.0f, juce::jmax (current.subAmount + 16.0f, 48.0f)), n);
                setIf (v, &Current::loAmount, juce::jmin (40.0f, current.loAmount + 12.0f), n);
                if (current.mix < 80.0f)
                    setIf (v, &Current::mix, 90.0f, n);
                v.subLimit = true;
                if (! current.subLimit) ++n;
                if (current.output > -1.0f)
                    setIf (v, &Current::output, juce::jmax (-24.0f, current.output - 1.5f), n);
                break;
            }
            case dsp::CheckId::subStrong:
            {
                setIf (v, &Current::subAmount, juce::jmax (15.0f, current.subAmount * 0.68f), n);
                setIf (v, &Current::eqShelfGain, juce::jmin (current.eqShelfGain, -6.0f), n);
                setIf (v, &Current::eqPeakGain, juce::jmin (current.eqPeakGain, -3.0f), n);
                setIf (v, &Current::eqPeakFreq, juce::jlimit (params::eqPeakMinHz, params::eqPeakMaxHz,
                                                             current.frequency > 1.0f ? current.frequency : 60.0f), n);
                break;
            }
            case dsp::CheckId::mud:
            {
                auto hz = a.mudHz;
                if (hz < 80.0f || hz > 160.0f)
                    hz = 110.0f;
                hz = juce::jlimit (params::eqPeakMinHz, params::eqPeakMaxHz, hz);
                setIf (v, &Current::eqPeakFreq, hz, n);
                setIf (v, &Current::eqPeakQ, 2.4f, n);
                setIf (v, &Current::eqPeakGain, juce::jlimit (params::eqGainMinDb, 0.0f,
                                                             juce::jmin (current.eqPeakGain, -15.0f)), n);
                break;
            }
            case dsp::CheckId::fundamentalLow:
            {
                if (current.octaveMode != 1)
                {
                    v.octaveMode = 1; // Unison: drop the unusable octave-down
                    ++n;
                }
                setIf (v, &Current::harmAmount, juce::jmax (current.harmAmount, 28.0f), n);
                setIf (v, &Current::drive, juce::jmin (50.0f, juce::jmax (current.drive, 32.0f)), n);
                break;
            }
            case dsp::CheckId::harmonicThin:
            {
                setIf (v, &Current::harmAmount, juce::jmax (current.harmAmount, 80.0f), n);
                setIf (v, &Current::drive, juce::jmin (80.0f, juce::jmax (current.drive, 55.0f)), n);
                setIf (v, &Current::eqPresenceFreq, juce::jlimit (params::eqPresenceMinHz, params::eqPresenceMaxHz, 110.0f), n);
                setIf (v, &Current::eqPresenceQ, 0.80f, n);
                if (v.eqPresenceGain < 6.0f)
                    setIf (v, &Current::eqPresenceGain, 8.0f, n);
                break;
            }
            case dsp::CheckId::none:
            case dsp::CheckId::numChecks:
                break;
        }

        p.numChanges = n;
        return p;
    }

    FixActions::Proposal FixActions::proposeAll (const dsp::ReasonList& reasons, const dsp::Analysis& analysis,
                                                 const Current& current)
    {
        // Apply recipes in a fixed order so level/headroom runs last.
        static constexpr dsp::CheckId order[] {
            dsp::CheckId::dcOffset,
            dsp::CheckId::infra,
            dsp::CheckId::stereoLows,
            dsp::CheckId::cancellation,
            dsp::CheckId::fundamentalLow,
            dsp::CheckId::mud,
            dsp::CheckId::harmonicThin,
            dsp::CheckId::unstablePitch,
            dsp::CheckId::subWeak,
            dsp::CheckId::subStrong,
            dsp::CheckId::sustained,
            dsp::CheckId::clipping,
        };

        Current walking = current;
        int total = 0;
        for (auto id : order)
        {
            if (! reasons.contains (id))
                continue;
            auto step = propose (id, analysis, walking);
            walking = step.values;
            total += step.numChanges;
        }
        Proposal p;
        p.values = walking;
        p.numChanges = total;
        return p;
    }

    bool FixActions::canFix (dsp::CheckId id) const
    {
        dsp::ReasonList reasons;
        proc.getEngine().display();
        const auto snap = proc.getEngine().display();
        (void) dsp::evaluate (snap.analysis, proc.readSettings().target, reasons);
        return reasons.contains (id);
    }

    bool FixActions::canFixAll() const
    {
        const auto snap = proc.getEngine().display();
        return snap.reasons.count > 0;
    }

    void FixActions::captureSnapshot()
    {
        snapshot.clear();
        for (auto* p : proc.getParameters())
            snapshot.push_back (p->getValue());
        hasSnapshot = true;
    }

    bool FixActions::commit (const Current& next)
    {
        struct Hit { juce::RangedAudioParameter* p; float value01; };
        std::vector<Hit> hits;
        auto want = [&] (const char* pid, float real)
        {
            auto* raw = proc.getState().getParameter (pid);
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (p == nullptr)
                return;
            const auto v01 = p->convertTo0to1 (real);
            if (std::abs (v01 - p->getValue()) > 1.0e-5f)
                hits.push_back ({ p, v01 });
        };

        want (id::subAmount, next.subAmount);
        want (id::loAmount, next.loAmount);
        want (id::frequency, next.frequency);
        want (id::octaveMode, (float) next.octaveMode);
        want (id::trackingSpeed, next.trackingSpeed);
        want (id::drive, next.drive);
        want (id::mix, next.mix);
        want (id::output, next.output);
        want (id::monoBelow, next.monoBelow ? 1.0f : 0.0f);
        want (id::monoFreq, next.monoFreq);
        want (id::infraHpf, next.infraHpf ? 1.0f : 0.0f);
        want (id::infraFreq, next.infraFreq);
        want (id::infraSlope, (float) next.infraSlope);
        want (id::subLimit, next.subLimit ? 1.0f : 0.0f);
        want (id::eqShelfGain, next.eqShelfGain);
        want (id::eqShelfFreq, next.eqShelfFreq);
        want (id::eqPeakGain, next.eqPeakGain);
        want (id::eqPeakFreq, next.eqPeakFreq);
        want (id::eqPeakQ, next.eqPeakQ);
        want (id::eqPresenceGain, next.eqPresenceGain);
        want (id::eqPresenceFreq, next.eqPresenceFreq);
        want (id::eqPresenceQ, next.eqPresenceQ);
        want (id::harmAmount, next.harmAmount);
        want (id::phaseInvert, next.phaseInvert ? 1.0f : 0.0f);
        want (id::phaseDelay, next.phaseDelay);

        if (hits.empty())
            return false;

        captureSnapshot();
        for (auto& h : hits) h.p->beginChangeGesture();
        for (auto& h : hits) h.p->setValueNotifyingHost (h.value01);
        for (auto& h : hits) h.p->endChangeGesture();
        return true;
    }

    bool FixActions::apply (dsp::CheckId id)
    {
        const auto snap = proc.getEngine().display();
        const auto now = read (proc.getState());
        const auto proposal = propose (id, snap.analysis, now);
        if (proposal.numChanges <= 0)
            return false;
        return commit (proposal.values);
    }

    bool FixActions::applyAll()
    {
        const auto snap = proc.getEngine().display();
        if (snap.reasons.count == 0)
            return false;
        const auto now = read (proc.getState());
        const auto proposal = proposeAll (snap.reasons, snap.analysis, now);
        if (proposal.numChanges <= 0)
            return false;
        return commit (proposal.values);
    }

    bool FixActions::undoLast()
    {
        if (! hasSnapshot || snapshot.size() != (size_t) proc.getParameters().size())
            return false;

        struct Hit { juce::AudioProcessorParameter* p; float value01; };
        std::vector<Hit> hits;
        const auto all = proc.getParameters();
        for (int i = 0; i < all.size(); ++i)
        {
            auto* p = all[i];
            if (p != nullptr && std::abs (p->getValue() - snapshot[(size_t) i]) > 1.0e-5f)
                hits.push_back ({ p, snapshot[(size_t) i] });
        }

        if (hits.empty())
        {
            hasSnapshot = false;
            return false;
        }

        for (auto& h : hits) h.p->beginChangeGesture();
        for (auto& h : hits) h.p->setValueNotifyingHost (h.value01);
        for (auto& h : hits) h.p->endChangeGesture();
        hasSnapshot = false;
        snapshot.clear();
        return true;
    }
}
