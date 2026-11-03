#include "Randomiser.h"

#include "Parameters.h"

namespace dooblrbass::randomiser
{
    namespace
    {
        float between (juce::Random& r, float lo, float hi) { return lo + r.nextFloat() * (hi - lo); }
    }

    int apply (juce::AudioProcessorValueTreeState& state, juce::Random& random)
    {
        namespace id = params::id;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;

        const auto add = [&] (const char* pid, float plain)
        {
            auto* p = state.getParameter (pid);
            if (p == nullptr)
                return;
            const auto n = p->convertTo0to1 (plain);
            if (std::abs (n - p->getValue()) > 1.0e-5f)
                changes.emplace_back (p, n);
        };

        add (id::osc1Wave, (float) random.nextInt (2));
        add (id::osc1Octave, (float) random.nextInt (4));
        add (id::osc1PW, between (random, 0.2f, 0.8f));
        add (id::osc2Wave, (float) random.nextInt (2));
        add (id::osc2Octave, (float) random.nextInt (4));
        add (id::osc2Tune, random.nextFloat() < 0.35f ? (random.nextBool() ? 7.0f : -7.0f) * between (random, 0.0f, 1.0f)
                                                      : between (random, -0.2f, 0.2f));
        add (id::osc2PW, between (random, 0.2f, 0.8f));
        add (id::osc3Wave, (float) random.nextInt (3));
        add (id::osc3Octave, (float) random.nextInt (4));
        add (id::osc3Tune, between (random, -0.15f, 0.15f));
        add (id::osc3LoFreq, random.nextFloat() < 0.15f ? 1.0f : 0.0f);
        add (id::osc3KeyTrack, random.nextFloat() < 0.8f ? 1.0f : 0.0f);
        add (id::subOctave, (float) random.nextInt (2));

        // Mixer: never all off.
        add (id::mixOsc1, between (random, 0.4f, 1.0f));
        add (id::mixOsc2, between (random, 0.0f, 0.8f));
        add (id::mixOsc3, random.nextFloat() < 0.35f ? between (random, 0.1f, 0.5f) : 0.0f);
        add (id::mixSub, between (random, 0.1f, 0.8f));
        add (id::mixNoise, random.nextFloat() < 0.2f ? between (random, 0.02f, 0.15f) : 0.0f);
        add (id::mixDrive, between (random, 0.1f, 0.7f));
        add (id::mixOverload, random.nextFloat() < 0.3f ? between (random, 0.05f, 0.35f) : 0.0f);

        const auto dark = random.nextFloat() < 0.55f;
        add (id::lpfCutoff, dark ? between (random, 80.0f, 500.0f) : between (random, 400.0f, 2500.0f));
        add (id::lpfReso, between (random, 0.1f, 0.7f));
        add (id::lpfEnvAmt, dark ? between (random, 0.3f, 0.9f) : between (random, 0.1f, 0.6f));
        add (id::lpfKeyTrack, between (random, 0.0f, 0.6f));

        add (id::fenvAttack, random.nextFloat() < 0.8f ? between (random, 0.001f, 0.02f) : between (random, 0.05f, 0.4f));
        add (id::fenvDecay, between (random, 0.08f, 0.8f));
        add (id::fenvSustain, between (random, 0.0f, 0.5f));
        add (id::fenvRelease, between (random, 0.08f, 0.5f));
        add (id::fenvVelAmt, between (random, 0.15f, 0.7f));
        add (id::aenvAttack, between (random, 0.001f, 0.03f));
        add (id::aenvDecay, between (random, 0.15f, 0.8f));
        add (id::aenvSustain, between (random, 0.55f, 1.0f));
        add (id::aenvRelease, between (random, 0.08f, 0.45f));
        add (id::sharedDecay, random.nextFloat() < 0.25f ? 1.0f : 0.0f);

        add (id::glide, random.nextFloat() < 0.4f ? between (random, 0.02f, 0.18f) : 0.0f);
        add (id::legato, random.nextFloat() < 0.35f ? 1.0f : 0.0f);
        add (id::lfoWave, (float) random.nextInt (5));
        add (id::lfoRate, between (random, 0.4f, 8.0f));
        add (id::lfoToPitch, random.nextFloat() < 0.25f ? between (random, 0.05f, 0.25f) : 0.0f);
        add (id::lfoToFilter, random.nextFloat() < 0.35f ? between (random, 0.15f, 0.6f) : 0.0f);
        add (id::lfoWheel, between (random, 0.15f, 0.5f));

        if (changes.empty())
            return 0;

        for (auto& c : changes) c.first->beginChangeGesture();
        for (auto& c : changes) c.first->setValueNotifyingHost (c.second);
        for (auto& c : changes) c.first->endChangeGesture();
        return (int) changes.size();
    }
}
