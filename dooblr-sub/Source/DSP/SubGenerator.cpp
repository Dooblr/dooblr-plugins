#include "SubGenerator.h"

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    void SubGenerator::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        tracker.prepare (sampleRate);
        reset();
    }

    void SubGenerator::reset() noexcept
    {
        tracker.reset();
        phaseA = phaseB = freqA = freqB = 0.0f;
        mixA = 1.0f;
        crossfade = 0.0f;
        active = 0;
        envelope = loLp = loBp = fundLp = 0.0f;
        corrSub = corrFund = corrProd = 0.0f;
        corrN = 0;
    }

    float SubGenerator::tickOsc (float& phase, float hz) const noexcept
    {
        if (! (hz > 1.0f))
            return 0.0f;
        phase += hz / (float) sampleRate;
        phase -= std::floor (phase);
        const auto s = std::sin (6.28318530718f * phase);
        // Light odd-harmonic shaper so the sub still speaks on small speakers when Drive is up.
        const auto amount = juce::jlimit (0.0f, 1.0f, settings.drive);
        const auto shaped = s + amount * 0.35f * (s * s * s);
        return shaped;
    }

    float SubGenerator::saturate (float x, float amount) const noexcept
    {
        if (amount < 1.0e-4f)
            return x;
        const auto drive = 1.0f + amount * 8.0f;
        return std::tanh (x * drive) / std::tanh (drive);
    }

    float SubGenerator::process (float x, float& generatedSub) noexcept
    {
        const auto tracked = tracker.process (x, settings.frequencyHz, settings.trackingSpeed);
        const auto voiced = tracker.isVoiced();

        // Envelope follows the input (fast attack, medium release). Tracking speed
        // also shortens the release so the control is never silent on a steady tone
        // that is amplitude-modulated.
        const auto absx = std::abs (x);
        const auto atk = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
        const auto relSec = juce::jmap (juce::jlimit (0.0f, 1.0f, settings.trackingSpeed), 0.18f, 0.04f);
        const auto rel = 1.0f - std::exp (-1.0f / (relSec * (float) sampleRate));
        if (absx > envelope)
            envelope += atk * (absx - envelope);
        else
            envelope += rel * (absx - envelope);

        const auto targetHz = voiced ? tracked : 0.0f;
        const auto current = active == 0 ? freqA : freqB;
        const auto jumped = targetHz > 1.0f && current > 1.0f
                            && std::abs (targetHz - current) / current > 0.06f;

        if (jumped && crossfade <= 0.0f)
        {
            if (active == 0)
            {
                freqB = targetHz;
                phaseB = phaseA; // phase-continuous hand-off
                active = 1;
            }
            else
            {
                freqA = targetHz;
                phaseA = phaseB;
                active = 0;
            }
            crossfade = 1.0f;
        }
        else if (targetHz > 1.0f)
        {
            if (active == 0) freqA += 0.12f * (targetHz - freqA);
            else             freqB += 0.12f * (targetHz - freqB);
        }

        if (crossfade > 0.0f)
        {
            const auto fadeSamp = 0.008f * (float) sampleRate;
            crossfade = juce::jmax (0.0f, crossfade - 1.0f / juce::jmax (8.0f, fadeSamp));
        }

        const auto env = juce::jmin (1.0f, envelope * 2.5f);
        auto osc = 0.0f;
        if (settings.octaveMode != 1) // -1 oct or both
        {
            const auto a = tickOsc (phaseA, freqA * 0.5f);
            const auto b = tickOsc (phaseB, freqB * 0.5f);
            const auto w = active == 0 ? (1.0f - crossfade) : crossfade;
            osc += (1.0f - w) * b + w * a;
        }
        if (settings.octaveMode != 0) // unison or both
        {
            // Re-use the same phases an octave up by doubling inside the sin argument:
            // a cheap second partial from the same accumulator would alias the phase,
            // so tick a unison at the tracked freq with the same voices.
            const auto a = std::sin (6.28318530718f * phaseA * 2.0f);
            const auto b = std::sin (6.28318530718f * phaseB * 2.0f);
            const auto w = active == 0 ? (1.0f - crossfade) : crossfade;
            osc += 0.55f * ((1.0f - w) * b + w * a);
        }

        osc *= env;
        osc = saturate (osc, settings.drive);
        generatedSub = osc * settings.subAmount;

        // Lo band: SVF-ish band-pass at Frequency with a little drive for harmonics.
        const auto g = std::tan (3.14159265f * juce::jlimit (20.0f, 180.0f, settings.frequencyHz) / (float) sampleRate);
        const auto k = 1.2f;
        const auto inv = 1.0f / (1.0f + g * (g + k));
        const auto v1 = (x - loLp - k * loBp) * inv;
        const auto v2 = loBp + g * v1;
        const auto v0 = g * v2 + loLp;
        loBp = v2 + g * v1;
        loLp = v0 + g * v2;
        const auto lo = saturate (v2 * 1.4f, settings.drive * 0.6f) * settings.loAmount * env;

        // Correlation taps: generated sub vs a 1-pole low of the input (the original fundamental).
        const auto fundCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * settings.frequencyHz / (float) sampleRate);
        fundLp += fundCoeff * (x - fundLp);
        corrSub += generatedSub * generatedSub;
        corrFund += fundLp * fundLp;
        corrProd += generatedSub * fundLp;
        ++corrN;

        return generatedSub + lo;
    }

    void SubGenerator::takeCorrelationTaps (float& subAcc, float& fundAcc, float& productAcc, int& n) noexcept
    {
        subAcc = corrSub;
        fundAcc = corrFund;
        productAcc = corrProd;
        n = corrN;
        corrSub = corrFund = corrProd = 0.0f;
        corrN = 0;
    }
}
