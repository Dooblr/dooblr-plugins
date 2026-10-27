#include "KickVoice.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586476925;

        float levelToGain (float db) noexcept { return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f); }

        float panLeft (float pan) noexcept  { return std::min (1.0f, 1.0f - pan); }
        float panRight (float pan) noexcept { return std::min (1.0f, 1.0f + pan); }

        float waveValue (int wave, double phase) noexcept
        {
            const auto s = (float) std::sin (twoPi * phase);

            switch (wave)
            {
                case triangleWave:
                {
                    const auto shifted = phase + 0.75;
                    const auto frac = (float) (shifted - std::floor (shifted));
                    return 4.0f * std::abs (frac - 0.5f) - 1.0f;
                }
                case saturatedWave: return std::tanh (3.0f * s) / std::tanh (3.0f);
                case squareWave:    return std::tanh (10.0f * s) / std::tanh (10.0f);
                default:            return s;
            }
        }

        float applyDrive (float x, float drive) noexcept
        {
            if (drive <= 1.0e-4f)
                return x;

            const auto k = 1.0f + 9.0f * drive;
            return std::tanh (k * x) / std::tanh (k);
        }

        void advance (double& phase, double increment) noexcept
        {
            phase += increment;
            phase -= std::floor (phase);
        }

        // Fixed seeds: every hit with the same settings renders identically (like a sample),
        // so the preview, the WAV export and playback match.
        constexpr std::uint32_t seedLeft = 0x12345678u, seedRight = 0x9abcdef1u, seedClick = 0x2468ace1u;
    }

    float midiNoteToHz (float midiNote) noexcept
    {
        return 440.0f * std::exp2 ((midiNote - 69.0f) / 12.0f);
    }

    void KickVoice::prepare (double newSampleRate)
    {
        SampleReader::prepareTables();
        sampleRate = newSampleRate;
        active = false;
    }

    void KickVoice::start (const KickSettings& s, int newNote, float velocity)
    {
        active = true;
        held = true;
        note = newNote;
        samplesElapsed = 0;

        const auto sensitivity = std::clamp (s.play.velocitySensitivity, 0.0f, 1.0f);
        velocityGain = (1.0f - sensitivity) + sensitivity * std::clamp (velocity, 0.0f, 1.0f);
        sampleKeyRatio = std::exp2 ((double) (newNote - s.play.keyNote) / 12.0);

        releaseGain = 1.0f;
        releaseStep = 0.0f;

        for (int layer = 0; layer < numLayers; ++layer)
        {
            auto& state = layers[(size_t) layer];
            state.gain = s.isLayerAudible (layer) ? levelToGain (s.common (layer).levelDb) : 0.0f;
            state.pan = s.common (layer).pan;

            for (auto& filter : state.filters)
                filter.reset();
        }

        bodyPhase = s.body.phaseDeg / 360.0;
        subPhase = s.sub.phaseDeg / 360.0;
        tailPhase.fill (s.tail.phaseDeg / 360.0);
        clickPhase = 0.0;

        noiseStateL = seedLeft;
        noiseStateR = seedRight;
        clickNoiseState = seedClick;

        for (auto& p : pink)
            p.fill (0.0f);

        brown.fill (0.0f);
        clickBand.reset();
        clickTone = 0.0f;

        for (auto& reader : readers)
            reader.reset();
    }

    double KickVoice::sampleStep (const SampleSource& source, double pitchRatio) const noexcept
    {
        return source.data == nullptr ? 0.0 : source.data->sampleRate / sampleRate * pitchRatio;
    }

    double KickVoice::sampleRatio (bool keyTrack, float tuneSt) const noexcept
    {
        const auto tune = std::clamp ((double) tuneSt, (double) minSampleTuneSt, (double) maxSampleTuneSt);
        return std::exp2 (tune / 12.0) * (keyTrack ? sampleKeyRatio : 1.0);
    }

    void KickVoice::release (float releaseMs) noexcept
    {
        held = false;
        const auto step = 1.0f / std::max (1.0f, releaseMs * 0.001f * (float) sampleRate);
        releaseStep = std::max (releaseStep, step);
    }

    void KickVoice::choke() noexcept
    {
        held = false;
        releaseStep = std::max (releaseStep, 1.0f / std::max (1.0f, 0.004f * (float) sampleRate));
    }

    float KickVoice::getOneShotLengthMs (const KickSettings& s) noexcept
    {
        auto length = 0.0f;

        const auto consider = [&] (int layer, float layerLengthMs)
        {
            if (s.isLayerAudible (layer))
                length = std::max (length, s.common (layer).startMs + layerLengthMs);
        };

        consider (clickLayer, s.click.lengthMs);
        consider (bodyLayer, s.body.amp.getEndTimeMs() + 10.0f);
        consider (subLayer, s.sub.amp.getLengthMs());
        consider (tailLayer, s.tail.amp.getLengthMs());
        consider (noiseLayer, s.noise.amp.getLengthMs());

        // A master envelope that ends at silence ends the hit there.
        if (s.master.envelopeActive() && s.master.envelope.getEndValue() <= 1.0e-4f)
            length = std::min (length, s.master.envelope.getEndTimeMs());

        return length;
    }

    float KickVoice::nextNoise (std::uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }

    //==============================================================================
    float KickVoice::renderClick (const ClickSettings& c, float tMs) noexcept
    {
        const auto lengthMs = std::max (0.5f, c.lengthMs);

        if (tMs < 0.0f || tMs >= lengthMs)
            return 0.0f;

        const auto x = tMs / lengthMs;
        const auto taper = 1.0f - x;
        const auto ratio = std::exp2 (c.pitchSt / 12.0f); // synth click: never follows the MIDI note
        const auto nyquistGuard = 0.45 * sampleRate;
        auto v = 0.0f;

        if (c.sample.enabled)
        {
            // Sample click: plays the sample (tuned by Pitch, optionally key-tracked) and
            // fades it out over the last 40 % of the click Length.
            const auto fade = x < 0.6f ? 1.0f : 0.5f + 0.5f * std::cos ((float) twoPi * 0.5f * (x - 0.6f) / 0.4f);
            v = readers[(size_t) clickLayer].read (c.sample.data, sampleStep (c.sample, sampleRatio (c.keyTrack, c.pitchSt))) * fade;
        }
        else
        {
        const auto oscillate = [&] (double frequency)
        {
            const auto value = (float) std::sin (twoPi * clickPhase);
            advance (clickPhase, std::min (frequency, nyquistGuard) / sampleRate);
            return value;
        };

        switch (c.type)
        {
            case ClickSettings::noiseBurst:
            {
                clickBand.setParameters (SvfFilter::Mode::bandPass, 4000.0f * ratio, 0.35f, sampleRate);
                v = 2.5f * clickBand.process (nextNoise (clickNoiseState)) * std::exp (-5.0f * x) * taper;
                break;
            }
            case ClickSettings::snap:
            {
                constexpr float pulseMs = 0.3f;
                const auto pulse = tMs < pulseMs ? 0.5f * (1.0f - std::cos ((float) twoPi * tMs / pulseMs)) : 0.0f;
                v = pulse + 0.6f * oscillate (1800.0 * ratio) * std::exp (-7.0f * x) * taper;
                break;
            }
            case ClickSettings::tick:
                v = oscillate (3200.0 * ratio) * std::min (1.0f, tMs / 0.1f) * std::exp (-8.0f * x) * taper;
                break;
            case ClickSettings::knock:
                v = oscillate (160.0 * ratio) * std::min (1.0f, tMs / 0.5f) * std::exp (-3.0f * x) * taper;
                break;
            default: // chirp: fast exponential sweep 12 kHz -> 300 Hz
                v = oscillate (300.0 * ratio * std::pow (40.0, 1.0 - x)) * std::exp (-4.0f * x) * taper;
                break;
        }
        }

        if (std::abs (c.tone) > 0.01f)
        {
            const auto fc = c.tone < 0.0f ? 20000.0f * std::pow (0.025f, -c.tone) : 20.0f * std::pow (400.0f, c.tone);
            const auto a = 1.0f - std::exp (-2.0f * (float) twoPi * 0.5f * std::min (fc, 0.45f * (float) sampleRate) / (float) sampleRate);
            clickTone += a * (v - clickTone);
            v = c.tone < 0.0f ? clickTone : v - clickTone;
        }

        return v;
    }

    float KickVoice::renderBody (const BodySettings& b, float tMs, bool sustain) noexcept
    {
        if (tMs < 0.0f)
            return 0.0f;

        const auto endMs = b.amp.getEndTimeMs();
        float amplitude;

        if (tMs <= endMs)
            amplitude = b.amp.evaluate (tMs);
        else if (sustain)
            amplitude = b.amp.getEndValue();
        else
            amplitude = b.amp.getEndValue() * std::max (0.0f, 1.0f - (tMs - endMs) / 10.0f);

        if (b.sample.enabled)
        {
            auto ratio = sampleRatio (b.keyTrack, b.tuneSt);

            if (b.sampleEnvAmount > 1.0e-4f)
                ratio *= std::exp2 ((double) b.sampleEnvAmount * (double) (b.pitch.evaluate (tMs) - b.pitch.getEndValue()));

            return applyDrive (readers[(size_t) bodyLayer].read (b.sample.data, sampleStep (b.sample, ratio)), b.drive) * amplitude;
        }

        const auto log2Hz = b.pitch.evaluate (tMs) + b.tuneSt / 12.0f; // independent of the MIDI note
        const auto frequency = std::clamp ((double) std::exp2 (log2Hz), 1.0, 0.45 * sampleRate);

        auto v = waveValue (b.wave, bodyPhase);

        if (b.harmonics > 1.0e-4f)
        {
            auto partials = 0.0f;

            for (int k = 2; k <= 8; ++k)
                if (frequency * k < 0.45 * sampleRate)
                    partials += (float) std::sin (twoPi * k * bodyPhase) / (float) k;

            v = (v + b.harmonics * partials) / (1.0f + 0.6f * b.harmonics);
        }

        v = applyDrive (v, b.drive);
        advance (bodyPhase, frequency / sampleRate);
        return v * amplitude;
    }

    float KickVoice::renderSub (const KickSettings& s, float tMs) noexcept
    {
        const auto& sub = s.sub;

        if (tMs < 0.0f)
            return 0.0f;

        const auto amplitude = sub.amp.evaluate (tMs);
        const auto baseNote = (float) s.play.keyNote + sub.tuneSt + sub.fineCents / 100.0f; // independent of the MIDI note
        auto frequency = (double) midiNoteToHz (baseNote);

        if (sub.envAmount > 1.0e-4f)
            frequency *= std::exp2 (sub.envAmount * (s.body.pitch.evaluate (tMs) - s.body.pitch.getEndValue()));

        frequency = std::clamp (frequency, 1.0, 0.45 * sampleRate);
        const auto v = (float) std::sin (twoPi * subPhase);
        advance (subPhase, frequency / sampleRate);
        return v * amplitude;
    }

    void KickVoice::renderTail (const TailSettings& t, float tMs, float keyNote, float& left, float& right) noexcept
    {
        left = right = 0.0f;

        if (tMs < 0.0f)
            return;

        const auto amplitude = t.amp.evaluate (tMs);

        if (t.sample.enabled)
        {
            const auto step = sampleStep (t.sample, sampleRatio (t.keyTrack, t.tuneSt));
            left = right = applyDrive (readers[(size_t) tailLayer].read (t.sample.data, step), t.drive) * amplitude;
            return;
        }

        const auto frequency = (double) midiNoteToHz (keyNote + t.tuneSt); // independent of the MIDI note
        const auto spread = std::exp2 ((double) t.spreadCents / 2400.0);
        const auto wave = std::clamp (t.wave, 0, (int) saturatedWave);

        left  = applyDrive (waveValue (wave, tailPhase[0]), t.drive) * amplitude;
        right = applyDrive (waveValue (wave, tailPhase[1]), t.drive) * amplitude;
        advance (tailPhase[0], std::min (frequency * spread, 0.45 * sampleRate) / sampleRate);
        advance (tailPhase[1], std::min (frequency / spread, 0.45 * sampleRate) / sampleRate);
    }

    void KickVoice::renderNoise (const NoiseSettings& n, float tMs, float& left, float& right) noexcept
    {
        left = right = 0.0f;

        if (tMs < 0.0f)
            return;

        const auto amplitude = n.amp.evaluate (tMs);

        if (n.sample.enabled)
        {
            left = right = readers[(size_t) noiseLayer].read (n.sample.data, sampleStep (n.sample, sampleRatio (false, n.tuneSt))) * amplitude;
            return;
        }

        float raw[2] = { nextNoise (noiseStateL), nextNoise (noiseStateR) };

        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto w = raw[ch];

            if (n.colour == NoiseSettings::pink)
            {
                auto& b = pink[ch];
                b[0] = 0.99886f * b[0] + w * 0.0555179f;
                b[1] = 0.99332f * b[1] + w * 0.0750759f;
                b[2] = 0.96900f * b[2] + w * 0.1538520f;
                b[3] = 0.86650f * b[3] + w * 0.3104856f;
                b[4] = 0.55000f * b[4] + w * 0.5329522f;
                b[5] = -0.7616f * b[5] - w * 0.0168980f;
                const auto value = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
                b[6] = w * 0.115926f;
                raw[ch] = value * 0.2f;
            }
            else if (n.colour == NoiseSettings::brown)
            {
                brown[ch] = (brown[ch] + 0.02f * w) / 1.02f;
                raw[ch] = brown[ch] * 3.5f;
            }
        }

        const auto mid = 0.5f * (raw[0] + raw[1]);
        const auto side = 0.5f * (raw[0] - raw[1]) * std::clamp (n.width, 0.0f, 1.0f);
        left = (mid + side) * amplitude;
        right = (mid - side) * amplitude;
    }

    //==============================================================================
    void KickVoice::render (const KickSettings& s, float* directL, float* directR,
                            float* sendL, float* sendR, int numSamples) noexcept
    {
        if (! active)
            return;

        const auto smoothing = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        std::array<float, numLayers> targets {};

        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto& c = s.common (layer);
            targets[(size_t) layer] = s.isLayerAudible (layer) ? levelToGain (c.levelDb) : 0.0f;

            if (c.filterType > 0)
            {
                const auto mode = c.filterType == 1 ? SvfFilter::Mode::lowPass
                                : c.filterType == 2 ? SvfFilter::Mode::highPass : SvfFilter::Mode::bandPass;

                for (auto& filter : layers[(size_t) layer].filters)
                    filter.setParameters (mode, c.cutoffHz, c.resonance, sampleRate);
            }
        }

        const auto lengthMs = getOneShotLengthMs (s);
        const auto masterActive = s.master.envelopeActive();
        const auto bodySustains = s.play.gate && held && s.isLayerAudible (bodyLayer) && s.body.amp.getEndValue() > 1.0e-4f
                               && ! (masterActive && s.master.envelope.getEndValue() <= 1.0e-4f);
        const auto msPerSample = 1000.0f / (float) sampleRate;
        const auto key = (float) s.play.keyNote;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto tMs = (float) samplesElapsed * msPerSample;
            // The master envelope runs on each hit's own clock, so a retrigger restarts it on the
            // new voice while the choked one fades out as before. Flat (the default): untouched.
            const auto voiceGain = masterActive ? velocityGain * releaseGain * std::clamp (s.master.envelope.evaluate (tMs), 0.0f, 1.0f)
                                                : velocityGain * releaseGain;

            for (int layer = 0; layer < numLayers; ++layer)
            {
                auto& state = layers[(size_t) layer];
                const auto target = targets[(size_t) layer];
                state.gain += (target - state.gain) * smoothing;

                if (target <= 0.0f && state.gain < 1.0e-5f)
                {
                    state.gain = 0.0f;
                    continue;
                }

                const auto& c = s.common (layer);
                const auto t = tMs - c.startMs;
                float left = 0.0f, right = 0.0f;
                bool stereo = false;

                switch (layer)
                {
                    case clickLayer: left = renderClick (s.click, t); break;
                    case bodyLayer:  left = renderBody (s.body, t, bodySustains); break;
                    case subLayer:   left = renderSub (s, t); break;
                    case tailLayer:  renderTail (s.tail, t, key, left, right); stereo = true; break;
                    default:         renderNoise (s.noise, t, left, right); stereo = true; break;
                }

                if (c.filterType > 0)
                {
                    left = state.filters[0].process (left);
                    right = stereo ? state.filters[1].process (right) : left;
                }
                else if (! stereo)
                {
                    right = left;
                }

                const auto gain = state.gain * voiceGain;
                state.pan += (c.pan - state.pan) * smoothing;
                left *= gain * panLeft (state.pan);
                right *= gain * panRight (state.pan);
                const auto send = std::clamp (c.send, 0.0f, 1.0f);

                directL[i] += left * (1.0f - send);
                directR[i] += right * (1.0f - send);
                sendL[i] += left * send;
                sendR[i] += right * send;
            }

            ++samplesElapsed;

            if (releaseStep > 0.0f)
            {
                releaseGain -= releaseStep;

                if (releaseGain <= 0.0f)
                {
                    active = false;
                    return;
                }
            }
        }

        // Done once every layer has finished, plus time for the filters to ring out.
        if (! bodySustains && (float) samplesElapsed * msPerSample > lengthMs + 30.0f)
            active = false;
    }
}
