#include "Randomiser.h"

#include <cmath>
#include <utility>
#include <vector>

#include "Parameters.h"

namespace dooblrprophet::randomiser
{
    namespace id = params::id;

    const juce::StringArray& excludedParameters()
    {
        static const juce::StringArray names { id::output, id::voices, id::voiceMode, id::unisonVoices, id::unisonDetune,
                                               id::bendRange, id::atFilter, id::atLfo, id::osc1Coarse, id::osc1Fine,
                                               id::lfoSync, id::lfoDivision };
        return names;
    }

    namespace
    {
        struct Patch
        {
            juce::Random& r;
            std::vector<std::pair<const char*, float>> values;

            float uniform (float lo, float hi) { return lo + r.nextFloat() * (hi - lo); }
            float logUniform (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); }
            bool chance (float p) { return r.nextFloat() < p; }
            void set (const char* pid, float value) { values.emplace_back (pid, value); }
            void setBool (const char* pid, bool on) { set (pid, on ? 1.0f : 0.0f); }

            void waves (const char* saw, const char* tri, const char* pulse)
            {
                // At least one wave, so Switched mode is never silent.
                auto s = chance (0.6f), t = chance (0.35f), p = chance (0.45f);
                if (! (s || t || p))
                    s = true;
                setBool (saw, s);
                setBool (tri, t);
                setBool (pulse, p);
            }
        };
    }

    int apply (juce::AudioProcessorValueTreeState& state, juce::Random& random)
    {
        Patch p { random, {} };

        // Oscillators
        p.set (id::osc1Mode, p.chance (0.5f) ? 1.0f : 0.0f);
        p.set (id::osc1Shape, p.uniform (0.0f, 1.0f));
        p.set (id::osc1PW, p.uniform (0.1f, 0.9f));
        p.waves (id::osc1Saw, id::osc1Tri, id::osc1Pulse);
        p.setBool (id::osc1Sync, p.chance (0.2f));

        static const int intervals[] { -12, -12, -7, -5, 0, 0, 0, 3, 4, 5, 7, 7, 12, 12, 19, 24 };
        const auto lowFreq = p.chance (0.1f);
        p.set (id::osc2Mode, p.chance (0.5f) ? 1.0f : 0.0f);
        p.set (id::osc2Coarse, (float) intervals[random.nextInt ((int) std::size (intervals))]);
        p.set (id::osc2Fine, p.uniform (-9.0f, 9.0f));
        p.set (id::osc2Shape, p.uniform (0.0f, 1.0f));
        p.set (id::osc2PW, p.uniform (0.1f, 0.9f));
        p.waves (id::osc2Saw, id::osc2Tri, id::osc2Pulse);
        p.setBool (id::osc2LowFreq, lowFreq);
        p.setBool (id::osc2KeyTrack, ! lowFreq || p.chance (0.3f));

        // Mixer: Osc A always audible.
        p.set (id::mixOsc1, p.uniform (0.45f, 1.0f));
        p.set (id::mixOsc2, lowFreq ? p.uniform (0.0f, 0.25f) : p.uniform (0.0f, 1.0f));
        p.set (id::mixSub, p.chance (0.4f) ? p.uniform (0.1f, 0.6f) : 0.0f);
        p.set (id::mixNoise, p.chance (0.2f) ? p.uniform (0.02f, 0.3f) : 0.0f);

        // Filter: a dark cutoff always gets positive envelope; screaming resonance is rare.
        const auto cutoff = p.logUniform (250.0f, 12000.0f);
        auto envAmount = p.chance (0.8f) ? p.uniform (0.0f, 0.8f) : p.uniform (-0.3f, 0.0f);
        if (cutoff < 900.0f)
            envAmount = p.uniform (0.35f, 0.85f);
        p.set (id::lpfCutoff, cutoff);
        p.set (id::lpfEnvAmt, envAmount);
        p.set (id::lpfReso, p.chance (0.1f) ? p.uniform (0.6f, 0.82f) : p.uniform (0.0f, 0.6f));
        p.set (id::lpfDrive, p.uniform (0.0f, 0.6f));
        p.set (id::lpfKeyTrack, p.uniform (0.0f, 1.0f));
        p.set (id::lpfModel, p.chance (0.5f) ? 1.0f : 0.0f);
        p.set (id::hpfCutoff, p.chance (0.7f) ? 20.0f : p.logUniform (20.0f, 300.0f));
        p.set (id::hpfReso, p.uniform (0.0f, 0.3f));
        p.set (id::hpfEnvAmt, p.chance (0.8f) ? 0.0f : p.uniform (-0.3f, 0.3f));

        // Envelopes: the amp envelope always opens within 1.2 s and sustains at least 40 %.
        p.set (id::fenvAttack, p.logUniform (0.001f, 1.5f));
        p.set (id::fenvDecay, p.logUniform (0.05f, 3.0f));
        p.set (id::fenvSustain, p.uniform (0.0f, 1.0f));
        p.set (id::fenvRelease, p.logUniform (0.05f, 2.5f));
        p.set (id::fenvVelAmt, p.uniform (0.0f, 0.6f));
        p.set (id::aenvAttack, p.chance (0.7f) ? p.logUniform (0.001f, 0.05f) : p.logUniform (0.05f, 1.2f));
        p.set (id::aenvDecay, p.logUniform (0.1f, 3.0f));
        p.set (id::aenvSustain, p.uniform (0.4f, 1.0f));
        p.set (id::aenvRelease, p.logUniform (0.05f, 2.0f));
        p.set (id::aenvVelAmt, p.uniform (0.0f, 0.5f));
        p.setBool (id::fenvSync, p.chance (0.2f)); // now and then the filter follows the amp ADSR

        // Poly-Mod: about a third of patches, gently.
        const auto polyMod = p.chance (0.35f);
        p.set (id::pmodFiltEnv, polyMod ? p.uniform (-0.4f, 0.5f) : 0.0f);
        p.set (id::pmodOsc2, polyMod ? p.uniform (0.0f, 0.45f) : 0.0f);
        {
            auto freq = polyMod && p.chance (0.6f), shape = polyMod && p.chance (0.4f), filter = polyMod && p.chance (0.5f);
            if (polyMod && ! (freq || shape || filter))
                filter = true;
            p.setBool (id::pmodDestFreq1, freq);
            p.setBool (id::pmodDestShape1, shape);
            p.setBool (id::pmodDestFilter, filter);
        }

        // LFO / wheel-mod (free-running rate only; sync and division are left alone).
        p.set (id::lfoWave, (float) random.nextInt (5));
        p.set (id::lfoRate, p.logUniform (0.2f, 12.0f));
        p.set (id::lfoInitial, p.chance (0.3f) ? p.uniform (0.02f, 0.3f) : 0.0f);
        p.set (id::lfoWheel, p.uniform (0.1f, 0.5f));
        p.set (id::lfoNoiseMix, p.chance (0.2f) ? p.uniform (0.0f, 0.5f) : 0.0f);
        p.setBool (id::lfoDestFreq1, p.chance (0.6f));
        p.setBool (id::lfoDestFreq2, p.chance (0.6f));
        p.setBool (id::lfoDestShape1, p.chance (0.25f));
        p.setBool (id::lfoDestShape2, p.chance (0.25f));
        p.setBool (id::lfoDestLpf, p.chance (0.3f));
        p.setBool (id::lfoDestHpf, p.chance (0.1f));
        p.setBool (id::lfoDestAmp, p.chance (0.1f));

        // Voice character
        p.set (id::glide, p.chance (0.15f) ? p.logUniform (0.02f, 0.3f) : 0.0f);
        p.set (id::vintage, p.uniform (0.0f, 0.5f));

        // One gesture around every change: the undo history records a single step.
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;
        for (const auto& [pid, value] : p.values)
        {
            jassert (! excludedParameters().contains (pid));
            if (auto* param = state.getParameter (pid))
            {
                const auto normalised = param->convertTo0to1 (param->convertFrom0to1 (param->convertTo0to1 (value)));
                if (std::abs (normalised - param->getValue()) > 1.0e-6f)
                    changes.emplace_back (param, normalised);
            }
        }

        for (auto& change : changes)
            change.first->beginChangeGesture();
        for (auto& change : changes)
            change.first->setValueNotifyingHost (change.second);
        for (auto& change : changes)
            change.first->endChangeGesture();

        return (int) changes.size();
    }
}
