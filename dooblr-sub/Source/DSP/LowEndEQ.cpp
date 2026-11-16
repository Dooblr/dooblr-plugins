#include "LowEndEQ.h"

namespace dooblrsub::dsp
{
    namespace
    {
        constexpr float twoPi = 6.28318530718f;
        constexpr float dbEps = 0.05f;

        bool silent (const LowEndEQSettings& s) noexcept
        {
            return std::abs (s.shelfDb) < dbEps
                && std::abs (s.peakDb) < dbEps
                && std::abs (s.presenceDb) < dbEps;
        }
    }

    void LowEndEQ::Biquad::setLowShelf (float hz, float gainDb, double sr) noexcept
    {
        if (std::abs (gainDb) < dbEps)
        {
            identity();
            return;
        }

        const auto a = std::pow (10.0f, gainDb / 40.0f);
        const auto w0 = twoPi * juce::jlimit (10.0f, 250.0f, hz) / (float) sr;
        const auto cosw = std::cos (w0);
        const auto sinw = std::sin (w0);
        const auto alpha = sinw * 0.5f * std::sqrt (2.0f);
        const auto ap1 = a + 1.0f;
        const auto am1 = a - 1.0f;
        const auto ap1c = ap1 * cosw;
        const auto am1c = am1 * cosw;
        const auto twoSa = 2.0f * std::sqrt (a) * alpha;

        const auto b0_ = a * (ap1 - am1c + twoSa);
        const auto b1_ = 2.0f * a * (am1 - ap1c);
        const auto b2_ = a * (ap1 - am1c - twoSa);
        const auto a0_ = ap1 + am1c + twoSa;
        const auto a1_ = -2.0f * (am1 + ap1c);
        const auto a2_ = ap1 + am1c - twoSa;
        const auto inv = 1.0f / juce::jmax (1.0e-12f, a0_);
        b0 = b0_ * inv;
        b1 = b1_ * inv;
        b2 = b2_ * inv;
        a1 = a1_ * inv;
        a2 = a2_ * inv;
    }

    void LowEndEQ::Biquad::setPeak (float hz, float q, float gainDb, double sr) noexcept
    {
        if (std::abs (gainDb) < dbEps)
        {
            identity();
            return;
        }

        const auto a = std::pow (10.0f, gainDb / 40.0f);
        const auto w0 = twoPi * juce::jlimit (20.0f, 400.0f, hz) / (float) sr;
        const auto cosw = std::cos (w0);
        const auto sinw = std::sin (w0);
        const auto qq = juce::jlimit (0.2f, 8.0f, q);
        const auto alpha = sinw / (2.0f * qq);

        const auto b0_ = 1.0f + alpha * a;
        const auto b1_ = -2.0f * cosw;
        const auto b2_ = 1.0f - alpha * a;
        const auto a0_ = 1.0f + alpha / a;
        const auto a1_ = -2.0f * cosw;
        const auto a2_ = 1.0f - alpha / a;
        const auto inv = 1.0f / juce::jmax (1.0e-12f, a0_);
        b0 = b0_ * inv;
        b1 = b1_ * inv;
        b2 = b2_ * inv;
        a1 = a1_ * inv;
        a2 = a2_ * inv;
    }

    void LowEndEQ::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        smoothA = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
        reset();
    }

    void LowEndEQ::reset() noexcept
    {
        smoothed = target;
        shelf.reset();
        peak.reset();
        presence.reset();
        coeffHop = 0;
        engaged = ! silent (smoothed);
        updateCoeffs();
    }

    bool LowEndEQ::isNeutral() const noexcept { return ! engaged && silent (target); }

    void LowEndEQ::tickSmoothers() noexcept
    {
        auto follow = [this] (float& z, float t) noexcept { z += smoothA * (t - z); };
        follow (smoothed.shelfDb, target.shelfDb);
        follow (smoothed.shelfHz, target.shelfHz);
        follow (smoothed.peakDb, target.peakDb);
        follow (smoothed.peakHz, target.peakHz);
        follow (smoothed.peakQ, target.peakQ);
        follow (smoothed.presenceDb, target.presenceDb);
        follow (smoothed.presenceHz, target.presenceHz);
        follow (smoothed.presenceQ, target.presenceQ);
    }

    void LowEndEQ::updateCoeffs() noexcept
    {
        shelf.setLowShelf (smoothed.shelfHz, smoothed.shelfDb, sampleRate);
        peak.setPeak (smoothed.peakHz, smoothed.peakQ, smoothed.peakDb, sampleRate);
        presence.setPeak (smoothed.presenceHz, smoothed.presenceQ, smoothed.presenceDb, sampleRate);
    }

    void LowEndEQ::process (float& left, float& right) noexcept
    {
        tickSmoothers();
        const auto want = ! silent (smoothed);
        if (! want && ! engaged)
            return;

        if (++coeffHop >= 8)
        {
            coeffHop = 0;
            updateCoeffs();
        }

        shelf.process (left, right);
        peak.process (left, right);
        presence.process (left, right);
        engaged = want;
    }
}
