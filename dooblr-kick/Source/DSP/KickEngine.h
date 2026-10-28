#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "KickVoice.h"
#include "MeterTap.h"
#include "ProcessingChain.h"

namespace dooblrkick::dsp
{
    /**
        Monophonic kick instrument: MIDI handling (retrigger with a short choke, gate), the layer
        voices (each hit shaped by the master envelope), then

            dry = summed layers
            wet = processing chain in the chosen order (Drive, EQ, Dynamics, Limiter by default)
            out = output gain * ((1 - mix) * dry + mix * wet)

        Changing the chain order crossfades (15 ms, linear) from the running chain to a second,
        freshly reset chain in the new order, so a reorder never steps the waveform.

        Continuous controls (levels, pans, filter cutoffs, tunes, drive, EQ, dynamics, output)
        are de-zippered: the engine glides its working settings towards the latest ones with a
        ~10 ms one-pole, updated every 32 samples. The first settings after prepare() / reset()
        are taken as they are, so offline renders match exactly.

        prepare() allocates; everything called from processBlock is allocation free.
    */
    class KickEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        void setSettings (const KickSettings& newSettings) noexcept;
        /** The settings currently in use (smoothed towards the latest setSettings()). */
        const KickSettings& getSettings() const noexcept { return settings; }

        void noteOn (int note, float velocity) noexcept;
        void noteOff (int note) noexcept;
        void allNotesOff() noexcept;

        /** Overwrites the buffer with the instrument output, handling the MIDI in sample order. */
        void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

        /** Overwrites [startSample, startSample + numSamples) with the output. */
        void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

        /** Meter feed for the editor (owned by the processor; null = no metering, e.g. offline
            renders). Set before processing starts. */
        void setMeterTap (MeterTap* newTap) noexcept { meterTap = newTap; }

        bool isPlaying() const noexcept;
        /** True while a chain reorder is crossfading. */
        bool isReordering() const noexcept { return fadeRemaining > 0; }
        int getActiveChainOrder() const noexcept { return activeOrder; }
        float getGainReductionDb() const noexcept { return gainReductionDb; }
        double getSampleRate() const noexcept { return sampleRate; }
        const KickVoice& getCurrentVoice() const noexcept { return voices[(size_t) currentVoice]; }
        /** Counts note-ons (wraps); the editor's playhead uses it to see retriggers. */
        std::uint32_t getHitCount() const noexcept { return hitCount; }

        /** Length (seconds) a rendered one-shot needs: the hit, a gate release, and ring-out. */
        static double getRenderLengthSeconds (const KickSettings& settings);

        /** Offline render of one hit into a new stereo buffer (message thread; allocates). */
        static juce::AudioBuffer<float> renderOneShot (const KickSettings& settings, double sampleRate,
                                                       int note, float velocity, double lengthSeconds = -1.0);

    private:
        void handle (const juce::MidiMessage& message) noexcept;
        void removeHeld (int note) noexcept;
        void advanceSmoothing (int numSamples) noexcept;

        static constexpr int smoothingBlock = 32;
        static constexpr float smoothingMs = 10.0f;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        KickSettings settings, target;
        bool snapSettings = true;

        std::array<KickVoice, 2> voices;
        int currentVoice = 0;
        std::uint32_t hitCount = 0;
        std::array<int, 16> heldNotes {};
        int numHeld = 0;

        juce::AudioBuffer<float> direct, send, dry, fadeDirect, fadeSend;
        std::array<ProcessingChain, 2> chains;
        int activeChain = 0;
        int activeOrder = -1;         // chain order index in use; -1: take the next one without a fade
        int fadeOrder = 0, fadeRemaining = 0, fadeLength = 720;
        float previousMix = -1.0f;    // dry / wet at the end of the last sub-block (-1: none yet)
        OutputGain outputGain;
        float gainReductionDb = 0.0f;
        MeterTap* meterTap = nullptr;
        bool ringing = false; // effects (EQ, oversampling, compressor) still decaying after the voices end
    };
}
