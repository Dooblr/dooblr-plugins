#include "Targets.h"

#include <algorithm>
#include <cmath>

namespace dooblrmeter::dsp
{
    namespace targets
    {
        DeliveryTarget get (int index, float customLufs, float customCeiling) noexcept
        {
            using P = DeliveryTarget::Policy;
            switch (juce::jlimit (0, count - 1, index))
            {
                case 0: return { "Streaming -14", -14.0f, -1.0f, P::upPeakLimited };
                case 1: return { "Streaming -16", -16.0f, -1.0f, P::upPeakLimited };
                case 2: return { "Video -14", -14.0f, -1.0f, P::downOnly };
                case 3: return { "Streaming -15", -15.0f, -1.0f, P::downOnly };
                case 4: return { "Spoken -16", -16.0f, -1.0f, P::both };
                case 5: return { "Broadcast EU -23", -23.0f, -1.0f, P::both };
                case 6: return { "Broadcast US -24", -24.0f, -2.0f, P::both };
                case 7: return { "Club -8", -8.0f, -0.3f, P::none };
                default: break;
            }
            return { "Custom", customLufs, customCeiling, P::both };
        }

        juce::StringArray names()
        {
            juce::StringArray result;
            for (int i = 0; i < count; ++i)
                result.add (juce::String (juce::CharPointer_UTF8 (get (i).label)).replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))));
            return result;
        }

        float predictedGainDb (const DeliveryTarget& t, float integratedLufs, float truePeakDb) noexcept
        {
            using P = DeliveryTarget::Policy;
            const auto raw = t.lufs - integratedLufs;
            switch (t.policy)
            {
                case P::none:     return 0.0f;
                case P::both:     return raw;
                case P::downOnly: return std::min (0.0f, raw);
                case P::upPeakLimited:
                    if (raw <= 0.0f)
                        return raw;
                    return std::max (0.0f, std::min (raw, t.ceiling - truePeakDb));
            }
            return raw;
        }

        juce::String policyText (DeliveryTarget::Policy p)
        {
            using P = DeliveryTarget::Policy;
            switch (p)
            {
                case P::downOnly:      return "turns down only";
                case P::upPeakLimited: return "up to the peak ceiling";
                case P::both:          return "delivery spec";
                case P::none:          return "no normalisation";
            }
            return {};
        }
    }

    namespace curves
    {
        namespace
        {
            struct Shape { double lowHz, lowRoll, kneeHz, slope, lowBoostDb, lowBoostHz, presenceDb; };

            Shape shape (int curve) noexcept
            {
                switch (curve)
                {
                    case 1:  return { 40.0, 9.0, 120.0, -3.0, 0.0, 0.0, 0.0 };   // Bright
                    case 2:  return { 40.0, 9.0, 120.0, -4.2, 0.0, 0.0, 0.0 };   // Warm
                    case 3:  return { 32.0, 9.0, 120.0, -3.8, 4.0, 110.0, 0.0 }; // Bass-heavy
                    case 4:  return { 70.0, 9.0, 200.0, -3.3, 0.0, 0.0, 0.0 };   // Acoustic
                    case 5:  return { 110.0, 12.0, 250.0, -3.0, 0.0, 0.0, 2.0 }; // Spoken
                    default: return { 40.0, 9.0, 120.0, -3.6, 0.0, 0.0, 0.0 };   // Balanced
                }
            }
        }

        juce::StringArray names() { return { "Balanced", "Bright", "Warm", "Bass-heavy", "Acoustic", "Spoken" }; }

        float levelDb (int curve, double hz) noexcept
        {
            const auto s = shape (curve);
            hz = std::max (10.0, hz);
            double db = 0.0;

            if (hz < s.lowHz)
                db -= s.lowRoll * std::log2 (s.lowHz / hz);
            if (hz > s.kneeHz)
                db += s.slope * std::log2 (hz / s.kneeHz);
            if (hz > 10000.0)
                db -= 2.0 * std::log2 (hz / 10000.0); // natural air roll-off
            if (s.lowBoostDb > 0.0 && hz < 2.0 * s.lowBoostHz)
            {
                // Smooth shelf: full boost below lowBoostHz, fading out over one octave.
                const auto t = juce::jlimit (0.0, 1.0, std::log2 (2.0 * s.lowBoostHz / hz));
                db += s.lowBoostDb * t;
            }
            if (s.presenceDb > 0.0)
            {
                const auto o = std::log2 (hz / 3000.0);
                db += s.presenceDb * std::exp (-o * o * 2.0);
            }
            return (float) db;
        }

        bands::Levels levels (int curve) noexcept
        {
            bands::Levels l {};
            for (int b = 0; b < bands::count; ++b)
                l[(size_t) b] = levelDb (curve, bands::centre (b));
            return l;
        }
    }
}
