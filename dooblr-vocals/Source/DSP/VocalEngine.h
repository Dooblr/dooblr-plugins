#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "MeterTap.h"
#include "NoiseReducer.h"
#include "PitchEngine.h"
#include "PitchTap.h"
#include "ProcessingChain.h"
#include "VocalSettings.h"

namespace dooblrvocals::dsp
{
    /**
        The optional spectral noise reducer comes first (off by default), so the pitch detector,
        the gate threshold, the compressor and the harmonizer grains all see the denoised vocal.
        Then pitch correction and the harmonizer (first of the main stages; the harmonies are summed with the
        corrected lead so Gate / EQ / Comp process the whole stack), then Gate / EQ / Comp in
        the chosen order, then the delay / reverb, the output trim and finally the limiter, so
        the limiter protects the true final output (wet included) and its meter shows it.

        Pitch stays first so the detector sees the incoming vocal, not a compressed or
        EQ'd version: gating, EQ and dynamics after correction is the usual vocal chain.
        Changing the chain order crossfades (15 ms, linear) onto a freshly reset copy of
        the chain so a reorder never steps the waveform.

        Continuous controls are de-zippered (~10 ms, every 32 samples). prepare() allocates;
        process() does not.
    */
    class VocalEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        void setSettings (const VocalSettings& newSettings) noexcept { target = newSettings; }
        const VocalSettings& getSettings() const noexcept { return settings; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return pitch.currentLatency(); }
        int latencyFor (const PitchSettings& s, bool nonRealtime) const noexcept { return pitch.latencySamples (s, nonRealtime); }
        /** Total latency: the noise reducer's STFT (0 when off) plus the pitch engine's look-ahead. */
        int latencyFor (const VocalSettings& s) const noexcept
        {
            return pitch.latencySamples (s.pitch, s.nonRealtime)
                 + noiseReducer.latencySamples (s.noiseReducer, processModeFor (s.pitch, s.nonRealtime));
        }

        NoiseReducer& getNoiseReducer() noexcept { return noiseReducer; }
        const NoiseReducer& getNoiseReducer() const noexcept { return noiseReducer; }

        ProcessMode getProcessMode() const noexcept { return pitch.currentMode(); }
        float getGainReductionDb() const noexcept { return lastGrDb; }
        /** Audio thread: the dynamic gain (dB) each EQ band is applying now (0 = static). */
        float getEqDynamicGainDb (int band) const noexcept { return chains[(size_t) activeChain].getEq().getDynamicGainDb (band); }
        /** De-esser cut now (dB, <= 0; 0 when off). */
        float getDeEssReductionDb() const noexcept { return deEsser.getCurrentReductionDb(); }
        float getReverbDuckDb() const noexcept { return fx.getReverbDuckDb(); }
        float getDelayDuckDb() const noexcept { return fx.getDelayDuckDb(); }
        PitchEstimate getLastPitch() const noexcept { return pitch.getLastEstimate(); }

        void setMeterTap (MeterTap* tap) noexcept { meterTap = tap; }
        void setPitchTap (PitchTap* tap) noexcept { pitchTap = tap; }
        void setLearnTap (PitchTap* tap) noexcept { pitch.setLearnTap (tap); }

    private:
        void smoothTowardsTarget (int numSamples) noexcept;
        static void lerpSettings (VocalSettings& current, const VocalSettings& from, const VocalSettings& to, float a) noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        VocalSettings settings, target;
        bool haveSettings = false;

        NoiseReducer noiseReducer;
        PitchEngine pitch;
        std::array<ProcessingChain, 2> chains;
        DeEsser deEsser;
        OutputGain outputGain;
        FxBlock fx;
        OutputStage limiter;

        int activeChain = 0, activeOrder = 0, fadeOrder = 0, fadeRemaining = 0, fadeLength = 1;
        juce::AudioBuffer<float> fadeBuffer;
        MeterTap* meterTap = nullptr;
        PitchTap* pitchTap = nullptr;
        float lastGrDb = 0.0f;
        int smoothCounter = 0;
    };
}
