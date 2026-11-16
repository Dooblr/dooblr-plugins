#pragma once

namespace dooblrsub::dsp
{
    /**
        Low-end harmonic exciter: waveshapes a ~90 Hz low band and mixes the residual
        (high-passed so it does not add rumble). Amount 0 is a no-op (bit-identical).
    */
    class HarmonicExciter
    {
    public:
        void prepare (double sampleRateToUse);
        void reset() noexcept;
        void setAmount (float amount01) noexcept { targetAmount = amount01; }

        void process (float& left, float& right) noexcept;

    private:
        double sampleRate = 44100.0;
        float targetAmount = 0.0f, amountZ = 0.0f;
        float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f;
        float lpA = 0.01f, hpA = 0.01f, smoothA = 0.02f;
    };
}
