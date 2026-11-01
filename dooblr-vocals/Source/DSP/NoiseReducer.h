#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "VocalSettings.h"

namespace dooblrvocals::dsp
{
    /**
        Spectral (STFT) noise reducer; the first stage of the engine, in front of pitch detection.

        Streaming STFT: sqrt-Hann analysis and synthesis windows, 75 % overlap, overlap-add;
        latency = the FFT size exactly. Per bin a gain g = max(floor, 1 - T / P) is applied, where
        P is the bin's power, T = noise floor x a sensitivity offset (+12 dB at 0 %, -6 dB at 100 %)
        and floor = -Amount dB. Against "musical noise" the gains are smoothed across three
        neighbouring bins and over time (opening in ~5 ms, closing in ~60 ms).

        The noise floor is a power spectral density (|X|^2 / sum w^2), which does not depend on the
        FFT size, stored on a fixed 513-point grid from 0 Hz to Nyquist so one learned profile works
        for every quality mode. Learned: the profile averaged while Learn is on. Auto: a minimum
        tracker (minimum statistics: the minimum of the 50 ms-smoothed power over the last ~1.5 s,
        times a bias that grows with the hop), seeded from the learned profile if any.

        Tracking mode uses a 256-point FFT (64-sample hop); Mix 1024 / 256, HQ 2048 / 512 (sizes
        at 44.1 / 48 kHz, doubled per doubling of the rate). Off: the buffer is not touched and no
        latency is added. Switching it on or off fades the output out and in over 5 ms around the
        latency change. prepare() allocates; process() does not.
    */
    class NoiseReducer
    {
    public:
        static constexpr int profileSize = 513;
        static constexpr int maxChannels = 2;
        static constexpr float maxAmountDb = 40.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** FFT size (= added latency) for a process mode; 0 when the reducer is off. */
        static int fftSizeFor (ProcessMode mode, double sampleRate) noexcept;
        int latencySamples (const NoiseReducerSettings& s, ProcessMode mode) const noexcept
        {
            return s.enabled ? fftSizeFor (mode, sampleRate) : 0;
        }

        void process (juce::AudioBuffer<float>& buffer, int numSamples, const NoiseReducerSettings& s, ProcessMode mode) noexcept;

        /** Learned profile (PSD per grid point); valid only when hasProfile(). Safe from any thread. */
        bool hasProfile() const noexcept { return profileValid.load (std::memory_order_acquire); }
        void copyProfile (std::vector<float>& destination) const;
        /** Restores a saved profile (e.g. from the session); applied by the audio thread. */
        void setProfile (const std::vector<float>& source);
        void clearProfile() noexcept { profileValid.store (false, std::memory_order_release); }
        /** Seconds of noise measured by the current / last Learn. */
        float getLearnedSeconds() const noexcept { return learnedSeconds.load (std::memory_order_relaxed); }
        /** Average attenuation applied over the last frame (dB, <= 0), for the panel readout. */
        float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    private:
        void configure (int fftOrder) noexcept;
        void processFrame (const NoiseReducerSettings& s) noexcept;
        float floorAt (int bin) const noexcept;

        double sampleRate = 48000.0;
        int maxOrder = 11;
        int order = 0, size = 0, hop = 0, bins = 0, pos = 0;
        std::array<std::unique_ptr<juce::dsp::FFT>, 4> ffts; // orders base .. base + 3 (only three used)
        int baseOrder = 8;

        std::vector<float> window, fftData, power, gain, smoothGain, noise, frameScratch;
        std::array<std::vector<float>, maxChannels> inFrame, outAccum, inFifo, outFifo;
        std::array<std::vector<float>, maxChannels> spectrum;
        float windowPower = 1.0f;

        // Learned profile on the fixed grid (written by the audio thread when Learn ends).
        std::vector<float> learnSum;
        int learnFrames = 0;
        bool wasLearning = false;
        std::array<std::atomic<float>, profileSize> profile {};
        std::atomic<bool> profileValid { false };
        std::array<std::atomic<float>, profileSize> pendingProfile {};
        std::atomic<bool> pendingValid { false };
        std::atomic<float> learnedSeconds { 0.0f }, reductionDb { 0.0f };
        static constexpr int autoSubWindows = 8;
        std::vector<float> smoothPower, subMin, minRing;
        int subCount = 0, ringPos = 0;
        bool noiseSeeded = false;
        int lastMode = 1;

        // On / off and frame-size changes: fade out, switch, fade in.
        bool running = false, primed = false;
        float fade = 1.0f;
        float fadeStep = 0.01f;
        enum class Phase { steady, fadingOut, fadingIn } phase = Phase::steady;
    };
}
