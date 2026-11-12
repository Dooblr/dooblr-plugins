#pragma once

#include "../Engine/Types.h"
#include "OutputPeakTap.h"
#include "SampleSlot.h"

#include <array>

namespace dooblrrhythm::dsp
{
    struct Voice
    {
        bool active = false;
        int instrument = 0;
        int age = 0;
        float gainL = 0.0f, gainR = 0.0f;
        float env = 0.0f, envDecay = 0.0f;
        float noise = 0.0f, noiseDecay = 0.0f, noiseState = 0.0f, noiseHp = 0.0f, noiseLp = 0.0f;
        uint32_t noiseSeed = 1;
        float modeFreq[6] {}, modePhase[6] {}, modeAmp[6] {}, modeDecay[6] {};
        int numModes = 0;
        const Sample* sample = nullptr;
        float samplePos = 0.0f, sampleInc = 1.0f, sampleGain = 0.0f;
    };

    struct KitSettings
    {
        float outputGain = 1.0f;
        std::array<InstrumentControls, numInstruments> instruments {};
        bool anySolo = false;
    };

    /**
        Built-in kit is modal + filtered-noise synthesis. Round-robin and velocity layers
        retune modes, move the strike (mode mix) and reseeds the noise. Optional per
        instrument/articulation WAV replacements are read through SampleSlot::acquire.
    */
    class SynthKit
    {
    public:
        void prepare (double sampleRate, int samplesPerBlock);
        void reset() noexcept;
        void setSettings (const KitSettings& s) noexcept { settings = s; }

        void noteOn (int instrument, Articulation artic, float velocity, uint32_t rrSeed) noexcept;
        void render (float* left, float* right, int numSamples) noexcept;

        OutputPeakTap peak;
        SampleSlot replacements[numInstruments][4];

        int activeVoices() const noexcept;

    private:
        void setupVoice (Voice& v, int instrument, Articulation artic, float velocity, uint32_t rrSeed) noexcept;
        void renderVoice (Voice& v, float* left, float* right, int numSamples) noexcept;

        double sampleRate = 44100.0;
        KitSettings settings;
        std::array<Voice, maxVoices> voices;
        int steal = 0;
    };
}
