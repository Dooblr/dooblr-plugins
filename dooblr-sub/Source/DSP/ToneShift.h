#pragma once

#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace dooblrsub::dsp
{
    struct ToneShiftSettings
    {
        float cents = 0.0f;            // -1200..+1200
        float transientPreserve = 0.8f; // 0..1
        float bodyMix = 1.0f;           // 0..1
        float mix = 0.0f;               // 0..1 shift mix
    };

    /**
        Duration-preserving tone shift of percussive signals.

        The input is split into a transient path and a body path with a fast/slow
        envelope follower (no FFT resampling). The transient is delayed by the STFT
        latency and left unshifted. The body is resynthesised two ways, mixed, and
        delayed to the same latency: (1) a single-mode resonator whose frequency is the
        body's zero-crossing period scaled by 2^(cents/1200); (2) a real-cepstrum
        spectral envelope plus residual, both warped along frequency and overlap-add
        resynthesised. Duration is unchanged because hop and envelope timing are fixed.

        References (algorithm, not code):
          - Makhoul, J. "Linear prediction: A tutorial review." Proc. IEEE 63(4), 1975.
          - Oppenheim, A. V. & Schafer, R. W. Discrete-Time Signal Processing (cepstrum).
          - McAulay, R. & Quatieri, T. "Speech analysis/synthesis based on a sinusoidal
            representation." IEEE TASSP 34(4), 1986.
          - Serra, X. "A system for sound analysis/transformation/synthesis based on a
            deterministic plus stochastic decomposition." PhD, Stanford CCRMA, 1989.
          - Laroche, J. & Dolson, M. "New phase-vocoder techniques for pitch-shifting
            and time-scaling." J. AES 47(11), 1999. Used here only as duration-locked
            frequency-axis warping of the body — not as time-domain resampling.
    */
    class ToneShift
    {
    public:
        static constexpr int fftOrder = 11;          // 2048
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hopSize = fftSize / 4;  // 512, 75 % overlap
        static constexpr int latencySamples = fftSize;

        void prepare (double sampleRateToUse);
        void reset();
        void setSettings (const ToneShiftSettings& s) noexcept { settings = s; }

        void process (const float* in, float* out, int numSamples);

        int getLatencySamples() const noexcept { return latencySamples; }

    private:
        void processFrame();
        void splitTransient (float x, float& transient, float& body) noexcept;
        void warpSpectrum (const float* magIn, const float* envIn, float* magOut, float ratio) const noexcept;
        void cepstralEnvelope (const float* mag, float* env) noexcept;

        double sampleRate = 44100.0;
        ToneShiftSettings settings;
        std::unique_ptr<juce::dsp::FFT> fft;

        std::vector<float> inputFifo, outputFifo, window;
        std::vector<float> fftWork, mag, env, magOut, cepWork, synthWork;
        int fifoIndex = 0, hopCount = 0;

        float fastEnv = 0.0f, slowEnv = 0.0f;
        std::vector<float> dryDelay, transDelay, modalDelay;
        int delayWrite = 0;

        // Single-mode resonator of the body (duration-locked; frequencies scaled by cents).
        float bodyEnv = 0.0f, bodyPrev = 0.0f, modalPhase = 0.0f, modalHz = 0.0f;
        int zcCount = 0;
    };
}
