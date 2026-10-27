#include "KickEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    //==============================================================================
    const LayerCommon& KickSettings::common (int layer) const noexcept
    {
        switch (layer)
        {
            case clickLayer: return click.common;
            case bodyLayer:  return body.common;
            case subLayer:   return sub.common;
            case tailLayer:  return tail.common;
            default:         return noise.common;
        }
    }

    bool KickSettings::isLayerAudible (int layer) const noexcept
    {
        const auto& c = common (layer);

        if (! c.enabled || c.levelDb <= -59.9f)
            return false;

        auto anySolo = false;

        for (int i = 0; i < numLayers; ++i)
            anySolo = anySolo || (common (i).solo && common (i).enabled);

        return ! anySolo || c.solo;
    }

    //==============================================================================
    void KickEngine::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (16, maxBlockSize);
        for (auto* b : { &direct, &send, &dry, &fadeDirect, &fadeSend })
            b->setSize (2, maxBlock);

        for (auto& voice : voices)
            voice.prepare (sampleRate);

        for (auto& chain : chains)
            chain.prepare (sampleRate, maxBlock);

        outputGain.prepare (sampleRate);
        fadeLength = std::max (32, (int) std::lround (0.015 * sampleRate));
        reset();
    }

    void KickEngine::reset()
    {
        for (auto& voice : voices)
            voice.prepare (sampleRate);

        numHeld = 0;
        ringing = false;
        snapSettings = true;
        for (auto& chain : chains)
            chain.reset();

        activeChain = 0;
        activeOrder = -1;
        fadeRemaining = 0;
        previousMix = -1.0f;
        outputGain.reset();
        gainReductionDb = 0.0f;
    }

    void KickEngine::setSettings (const KickSettings& newSettings) noexcept
    {
        target = newSettings;
        for (auto* e : { &target.body.pitch, &target.body.amp, &target.click.pitch, &target.click.amp, &target.sub.pitch,
                         &target.sub.amp, &target.tail.pitch, &target.tail.amp, &target.noise.pitch, &target.noise.amp,
                         &target.master.envelope })
            e->sanitise();

        // Nothing is sounding, so there is nothing to smooth: jump straight to the new values.
        const auto sounding = std::any_of (voices.begin(), voices.end(), [] (const KickVoice& v) { return v.isActive(); });

        if (snapSettings || ! sounding)
        {
            settings = target;
            snapSettings = false;
        }
    }

    void KickEngine::advanceSmoothing (int numSamples) noexcept
    {
        const auto previous = settings;
        settings = target; // switches, choices, point counts and samples change immediately

        const auto a = 1.0f - std::exp (-(float) numSamples / (smoothingMs * 0.001f * (float) sampleRate));

        const auto lin = [a] (float& value, float from, float tolerance = 1.0e-5f)
        {
            const auto next = from + (value - from) * a;
            if (std::abs (value - next) > tolerance)
                value = next;
        };

        // Frequencies glide in the log domain.
        const auto log = [a] (float& value, float from)
        {
            if (from <= 0.0f || value <= 0.0f)
                return;

            const auto next = from * std::pow (value / from, a);
            if (std::abs (value - next) > 1.0e-4f * value)
                value = next;
        };

        const auto common = [&] (LayerCommon& c, const LayerCommon& p)
        {
            lin (c.levelDb, p.levelDb, 1.0e-3f);
            lin (c.pan, p.pan);
            lin (c.send, p.send);
            log (c.cutoffHz, p.cutoffHz);
            lin (c.resonance, p.resonance);
        };

        // v0.3.6: the layer envelopes' points glide too (time, value and curve), so automating a
        // point while a hit sounds does not step. A changed point count jumps. Settings that
        // do not change are left bit for bit as they are.
        const auto envelope = [&] (MultiPointEnvelope& e, const MultiPointEnvelope& p, float valueTolerance)
        {
            if (e.numNodes != p.numNodes)
                return;

            for (size_t n = 0; n < (size_t) e.numNodes; ++n)
            {
                lin (e.nodes[n].timeMs, p.nodes[n].timeMs, 1.0e-3f);
                lin (e.nodes[n].value, p.nodes[n].value, valueTolerance);
                lin (e.nodes[n].curve, p.nodes[n].curve);
            }

            e.sanitise();
        };

        envelope (settings.body.pitch, previous.body.pitch, 1.0e-4f); // log2 Hz
        envelope (settings.body.amp, previous.body.amp, 1.0e-5f);
        envelope (settings.click.pitch, previous.click.pitch, 1.0e-3f); // semitones
        envelope (settings.click.amp, previous.click.amp, 1.0e-5f);
        envelope (settings.sub.pitch, previous.sub.pitch, 1.0e-3f);
        envelope (settings.sub.amp, previous.sub.amp, 1.0e-5f);
        envelope (settings.tail.pitch, previous.tail.pitch, 1.0e-3f);
        envelope (settings.tail.amp, previous.tail.amp, 1.0e-5f);
        envelope (settings.noise.pitch, previous.noise.pitch, 1.0e-3f);
        envelope (settings.noise.amp, previous.noise.amp, 1.0e-5f);

        common (settings.click.common, previous.click.common);
        common (settings.body.common, previous.body.common);
        common (settings.sub.common, previous.sub.common);
        common (settings.tail.common, previous.tail.common);
        common (settings.noise.common, previous.noise.common);

        lin (settings.click.pitchSt, previous.click.pitchSt, 1.0e-4f);
        lin (settings.click.tone, previous.click.tone);
        lin (settings.body.tuneSt, previous.body.tuneSt, 1.0e-4f);
        lin (settings.body.harmonics, previous.body.harmonics);
        lin (settings.body.drive, previous.body.drive);
        lin (settings.body.sampleEnvAmount, previous.body.sampleEnvAmount);
        lin (settings.click.sampleEnvAmount, previous.click.sampleEnvAmount);
        lin (settings.tail.sampleEnvAmount, previous.tail.sampleEnvAmount);
        lin (settings.noise.sampleEnvAmount, previous.noise.sampleEnvAmount);
        lin (settings.sub.tuneSt, previous.sub.tuneSt, 1.0e-4f);
        lin (settings.sub.fineCents, previous.sub.fineCents, 1.0e-3f);
        lin (settings.sub.envAmount, previous.sub.envAmount);
        lin (settings.tail.tuneSt, previous.tail.tuneSt, 1.0e-4f);
        lin (settings.tail.spreadCents, previous.tail.spreadCents, 1.0e-3f);
        lin (settings.tail.drive, previous.tail.drive);
        lin (settings.noise.tuneSt, previous.noise.tuneSt, 1.0e-4f);
        lin (settings.noise.width, previous.noise.width);

        lin (settings.drive.driveDb, previous.drive.driveDb, 1.0e-3f);
        lin (settings.drive.tone, previous.drive.tone);
        lin (settings.drive.outputDb, previous.drive.outputDb, 1.0e-3f);
        lin (settings.drive.mix, previous.drive.mix);

        log (settings.eq.lowFreq, previous.eq.lowFreq);
        lin (settings.eq.lowGainDb, previous.eq.lowGainDb, 1.0e-3f);
        log (settings.eq.midFreq, previous.eq.midFreq);
        lin (settings.eq.midGainDb, previous.eq.midGainDb, 1.0e-3f);
        log (settings.eq.midQ, previous.eq.midQ);
        log (settings.eq.highFreq, previous.eq.highFreq);
        lin (settings.eq.highGainDb, previous.eq.highGainDb, 1.0e-3f);

        lin (settings.shaper.attack, previous.shaper.attack);
        lin (settings.shaper.sustain, previous.shaper.sustain);
        lin (settings.compressor.thresholdDb, previous.compressor.thresholdDb, 1.0e-3f);
        lin (settings.compressor.makeupDb, previous.compressor.makeupDb, 1.0e-3f);
        lin (settings.output.ceilingDb, previous.output.ceilingDb, 1.0e-3f);
        lin (settings.output.outputDb, previous.output.outputDb, 1.0e-3f);
        lin (settings.master.dryWet, previous.master.dryWet);
    }

    void KickEngine::removeHeld (int note) noexcept
    {
        int out = 0;

        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] != note)
                heldNotes[(size_t) out++] = heldNotes[(size_t) i];

        numHeld = out;
    }

    void KickEngine::noteOn (int note, float velocity) noexcept
    {
        ringing = true;
        ++hitCount;
        removeHeld (note);

        if (numHeld == (int) heldNotes.size())
        {
            std::copy (heldNotes.begin() + 1, heldNotes.end(), heldNotes.begin());
            --numHeld;
        }

        heldNotes[(size_t) numHeld++] = note;

        if (voices[(size_t) currentVoice].isActive())
        {
            voices[(size_t) currentVoice].choke();
            currentVoice ^= 1;
        }

        // A new hit starts exactly on the current settings; smoothing only applies to changes
        // made while a hit is sounding (the choked previous hit fades out in a few ms).
        settings = target;
        voices[(size_t) currentVoice].start (settings, note, velocity);
    }

    void KickEngine::noteOff (int note) noexcept
    {
        removeHeld (note);
        auto& current = voices[(size_t) currentVoice];

        if (! current.isActive() || current.getNote() != note)
            return;

        if (settings.play.gate)
            current.release (settings.play.releaseMs);
    }

    void KickEngine::allNotesOff() noexcept
    {
        numHeld = 0;

        for (auto& voice : voices)
            if (voice.isActive())
                voice.choke();
    }

    bool KickEngine::isPlaying() const noexcept
    {
        return ringing || voices[0].isActive() || voices[1].isActive();
    }

    void KickEngine::handle (const juce::MidiMessage& message) noexcept
    {
        if (message.isNoteOn())
            noteOn (message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            noteOff (message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();
    }

    void KickEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
    {
        const auto numSamples = buffer.getNumSamples();
        int position = 0;

        for (const auto metadata : midi)
        {
            const auto eventPosition = std::clamp (metadata.samplePosition, 0, numSamples);

            if (eventPosition > position)
            {
                render (buffer, position, eventPosition - position);
                position = eventPosition;
            }

            handle (metadata.getMessage());
        }

        if (position < numSamples)
            render (buffer, position, numSamples - position);
    }

    void KickEngine::render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        auto reduction = 0.0f;

        for (int offset = 0; offset < numSamples;)
        {
            const auto n = std::min (smoothingBlock, numSamples - offset);
            advanceSmoothing (n);
            direct.clear (0, n);
            send.clear (0, n);

            for (auto& voice : voices)
                voice.render (settings, direct.getWritePointer (0), direct.getWritePointer (1),
                              send.getWritePointer (0), send.getWritePointer (1), n);

            // Dry / wet: the dry signal is the summed layers, before the chain.
            const auto mixTo = std::clamp (settings.master.dryWet, 0.0f, 1.0f);
            const auto mixFrom = previousMix < 0.0f ? mixTo : previousMix;
            previousMix = mixTo;
            const auto mixing = mixFrom < 1.0f || mixTo < 1.0f;
            MeterBlock meter;

            if (mixing || meterTap != nullptr)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    dry.copyFrom (ch, 0, direct, ch, 0, n);
                    dry.addFrom (ch, 0, send, ch, 0, n);
                }

                meter.dry = std::max (dry.getMagnitude (0, 0, n), dry.getMagnitude (1, 0, n));
            }

            // Chain order: switch at once when nothing is sounding, else crossfade to a fresh chain.
            const auto wanted = std::clamp (settings.master.chainOrder, 0, numChainOrders - 1);

            if (activeOrder < 0 || (wanted != activeOrder && fadeRemaining == 0 && ! isPlaying()))
            {
                activeOrder = wanted;
            }
            else if (wanted != activeOrder && fadeRemaining == 0)
            {
                fadeOrder = wanted;
                fadeRemaining = fadeLength;
                chains[(size_t) (activeChain ^ 1)].reset();
            }

            auto* meterBlock = meterTap != nullptr ? &meter : nullptr;

            if (fadeRemaining > 0)
            {
                auto& incoming = chains[(size_t) (activeChain ^ 1)];

                for (int ch = 0; ch < 2; ++ch)
                {
                    fadeDirect.copyFrom (ch, 0, direct, ch, 0, n);
                    fadeSend.copyFrom (ch, 0, send, ch, 0, n);
                }

                chains[(size_t) activeChain].process (direct, send, n, settings, chainOrderFromIndex (activeOrder), nullptr, nullptr);
                incoming.process (fadeDirect, fadeSend, n, settings, chainOrderFromIndex (fadeOrder), meterBlock, meterTap);

                const auto done = fadeLength - fadeRemaining;

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* out = direct.getWritePointer (ch);
                    const auto* in = fadeDirect.getReadPointer (ch);

                    for (int i = 0; i < n; ++i)
                    {
                        const auto x = std::min (1.0f, (float) (done + i + 1) / (float) fadeLength);
                        out[i] += (in[i] - out[i]) * x;
                    }
                }

                fadeRemaining = std::max (0, fadeRemaining - n);

                if (fadeRemaining == 0)
                {
                    activeChain ^= 1;
                    activeOrder = fadeOrder;
                }
            }
            else
            {
                chains[(size_t) activeChain].process (direct, send, n, settings, chainOrderFromIndex (activeOrder), meterBlock, meterTap);
            }

            reduction = std::min (reduction, chains[(size_t) (fadeRemaining > 0 ? activeChain ^ 1 : activeChain)].getLastGainReductionDb());

            if (meterTap != nullptr)
                meter.wet = std::max (direct.getMagnitude (0, 0, n), direct.getMagnitude (1, 0, n));

            if (mixing)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* out = direct.getWritePointer (ch);
                    const auto* d = dry.getReadPointer (ch);

                    for (int i = 0; i < n; ++i)
                    {
                        const auto m = mixFrom + (mixTo - mixFrom) * (float) (i + 1) / (float) n;
                        out[i] = d[i] * (1.0f - m) + out[i] * m; // equal gain: dry and wet are correlated
                    }
                }
            }

            outputGain.process (direct, n, settings.output.outputDb);

            if (meterTap != nullptr)
            {
                meter.output = std::max (direct.getMagnitude (0, 0, n), direct.getMagnitude (1, 0, n));
                meterTap->addBlock (meter, n);
            }

            // Keep reporting "playing" until the effects tail has decayed below -100 dB.
            if (voices[0].isActive() || voices[1].isActive())
                ringing = true;
            else if (ringing)
                ringing = fadeRemaining > 0 || std::max (direct.getMagnitude (0, 0, n), direct.getMagnitude (1, 0, n)) >= 1.0e-5f;

            const auto start = startSample + offset;

            if (buffer.getNumChannels() >= 2)
            {
                buffer.copyFrom (0, start, direct, 0, 0, n);
                buffer.copyFrom (1, start, direct, 1, 0, n);

                for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                    buffer.clear (ch, start, n);
            }
            else if (buffer.getNumChannels() == 1)
            {
                buffer.copyFrom (0, start, direct.getReadPointer (0), n, 0.5f);
                buffer.addFrom (0, start, direct, 1, 0, n, 0.5f);
            }

            offset += n;
        }

        gainReductionDb = reduction;
    }

    double KickEngine::getRenderLengthSeconds (const KickSettings& s)
    {
        auto ms = (double) KickVoice::getOneShotLengthMs (s);

        if (s.play.gate)
            ms += s.play.releaseMs;

        return std::clamp ((ms + 120.0) * 0.001, 0.05, 30.0);
    }

    juce::AudioBuffer<float> KickEngine::renderOneShot (const KickSettings& s, double sampleRate, int note,
                                                        float velocity, double lengthSeconds)
    {
        if (lengthSeconds <= 0.0)
            lengthSeconds = getRenderLengthSeconds (s);

        const auto total = (int) std::ceil (lengthSeconds * sampleRate);
        juce::AudioBuffer<float> result (2, total);
        result.clear();

        KickEngine engine;
        engine.prepare (sampleRate, 512);
        engine.setSettings (s);
        engine.noteOn (note, velocity);

        // A gated kick is held for its one-shot length, then released.
        const auto releaseAt = s.play.gate ? (int) (KickVoice::getOneShotLengthMs (s) * 0.001 * sampleRate) : total;
        int position = 0;

        while (position < total)
        {
            auto n = std::min (512, total - position);

            if (position < releaseAt && position + n > releaseAt)
                n = releaseAt - position;

            engine.render (result, position, n);
            position += n;

            if (position == releaseAt)
                engine.noteOff (note);
        }

        return result;
    }
}
