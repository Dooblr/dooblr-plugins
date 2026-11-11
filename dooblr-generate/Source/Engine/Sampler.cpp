#include "Sampler.h"
#include "../Parameters.h"

#include <cmath>

namespace dooblrgenerate::engine
{
    void Sampler::prepare (double sampleRate, int)
    {
        hostRate = sampleRate > 0 ? sampleRate : 44100.0;
        outputGain.reset (hostRate, 0.02);
        reset();
    }

    void Sampler::reset()
    {
        for (auto& v : voices)
            v = Voice {};
    }

    int Sampler::numActiveVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.active ? 1 : 0;
        return n;
    }

    void Sampler::noteOn (int note, float velocity, const Sample* sample, const PlaybackSettings& s)
    {
        if (sample == nullptr || sample->audio.getNumSamples() == 0)
            return;

        Voice* target = nullptr;

        for (auto& v : voices)
            if (! v.active) { target = &v; break; }

        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.age < target->age)
                    target = &v;
        }

        auto& v = *target;
        const auto semis = (double) (note - rootNote) + s.tuneSemitones + s.fineCents / 100.0;
        v.active = true;
        v.sample = sample;
        v.increment = std::pow (2.0, semis / 12.0) * sample->sampleRate / hostRate;
        v.position = (double) sample->audio.getNumSamples() * juce::jlimit (0.0f, 95.0f, s.startPercent) / 100.0;
        const auto sens = juce::jlimit (0.0f, 1.0f, s.velocityPercent / 100.0f);
        v.gain = (1.0f - sens) + sens * juce::jlimit (0.0f, 1.0f, velocity);
        const auto attackSamples = s.attackMs * 0.001 * hostRate;
        v.env = attackSamples < 1.0 ? 1.0f : 0.0f;
        v.attackStep = attackSamples < 1.0 ? 1.0f : (float) (1.0 / attackSamples);
        v.decayMul = s.decayMs >= params::decayOffMs - 0.05f
                       ? 1.0f
                       : (float) std::pow (0.001, 1.0 / juce::jmax (1.0, s.decayMs * 0.001 * hostRate)); // -60 dB over Decay
        v.age = ++counter;
    }

    void Sampler::render (Voice& v, juce::AudioBuffer<float>& out, int start, int num)
    {
        const auto& audio = v.sample->audio;
        const auto length = audio.getNumSamples();
        const auto* left = audio.getReadPointer (0);
        const auto* right = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
        auto* outL = out.getWritePointer (0);
        auto* outR = out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0);
        const bool stereoOut = out.getNumChannels() > 1;

        for (int i = start; i < start + num; ++i)
        {
            const auto index = (int) v.position;

            const bool decayedOut = v.attackStep >= 1.0f && v.decayMul < 1.0f && v.env < 1.0e-5f;

            if (index >= length - 1 || decayedOut)
            {
                v.active = false;
                return;
            }

            const auto frac = (float) (v.position - index);
            const auto l = left[index] + frac * (left[index + 1] - left[index]);
            const auto r = right[index] + frac * (right[index + 1] - right[index]);

            // Attack ramps up linearly; once full the decay multiplies down (-60 dB over Decay).
            if (v.attackStep < 1.0f)
            {
                v.env += v.attackStep;
                if (v.env >= 1.0f) { v.env = 1.0f; v.attackStep = 1.0f; }
            }
            else
            {
                v.env *= v.decayMul;
            }

            const auto g = v.env * v.gain;
            outL[i] += l * g;
            if (stereoOut)
                outR[i] += r * g;

            v.position += v.increment;
        }
    }

    void Sampler::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Sample* sample,
                           const PlaybackSettings& settings, const Sample* extraKeep)
    {
        out.clear();
        outputGain.setTargetValue (settings.outputDb <= -47.99f ? 0.0f : juce::Decibels::decibelsToGain (settings.outputDb));

        // A sample that was replaced stops its voices (the old buffer may be freed after this block).
        for (auto& v : voices)
            if (v.active && v.sample != sample && v.sample != extraKeep)
                v.active = false;

        int position = 0;
        const auto numSamples = out.getNumSamples();

        for (const auto metadata : midi)
        {
            const auto when = juce::jlimit (0, numSamples, metadata.samplePosition);

            for (auto& v : voices)
                if (v.active)
                    render (v, out, position, when - position);

            position = when;
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
                noteOn (message.getNoteNumber(), message.getFloatVelocity(), sample, settings);
            else if (message.isAllNotesOff() || message.isAllSoundOff())
                reset();
        }

        for (auto& v : voices)
            if (v.active)
                render (v, out, position, numSamples - position);

        if (outputGain.isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto g = outputGain.getNextValue();
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.getWritePointer (ch)[i] *= g;
            }
        }
        else
        {
            out.applyGain (outputGain.getTargetValue());
        }
    }
}
