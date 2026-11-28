#include "Profiles.h"

#include <cmath>

#include "KWeighting.h"

namespace dooblrmeter::dsp
{
    namespace profiles
    {
        namespace
        {
            using T = Biquad::Type;
            using S = Profile::Stereo;

            const std::array<Profile, count>& all()
            {
                static const std::array<Profile, count> list {{
                    { "Club PA", "Club",
                      { { T::highPass, 28.0, 0.707, 0.0 }, { T::highPass, 28.0, 0.707, 0.0 }, { T::lowPass, 16000.0, 0.707, 0.0 },
                        { T::peak, 3000.0, 1.0, 1.0 } },
                      S::partial, 100.0, 1.0, false, 100.0, 3, false },
                    { "Bluetooth speaker", "BT speaker",
                      { { T::highPass, 80.0, 0.707, 0.0 }, { T::highPass, 80.0, 0.707, 0.0 }, { T::peak, 150.0, 1.2, 4.0 },
                        { T::peak, 3000.0, 1.0, 2.0 }, { T::lowPass, 14000.0, 0.707, 0.0 } },
                      S::mono, 0.0, 0.0, true, 75.0, 4, true },
                    { "Phone speaker", "Phone",
                      { { T::highPass, 400.0, 0.707, 0.0 }, { T::highPass, 400.0, 0.707, 0.0 }, { T::highPass, 400.0, 0.707, 0.0 },
                        { T::peak, 3000.0, 1.5, 5.0 }, { T::lowPass, 12000.0, 0.707, 0.0 }, { T::lowPass, 12000.0, 0.707, 0.0 } },
                      S::mono, 0.0, 0.0, true, 70.0, 4, true },
                    { "Laptop", "Laptop",
                      { { T::highPass, 180.0, 0.707, 0.0 }, { T::highPass, 180.0, 0.707, 0.0 }, { T::peak, 1500.0, 1.0, 2.0 },
                        { T::lowPass, 15000.0, 0.707, 0.0 } },
                      S::partial, 300.0, 0.5, true, 65.0, 0, false },
                    { "Earbuds", "Earbuds",
                      { { T::highPass, 20.0, 0.707, 0.0 }, { T::lowShelf, 60.0, 0.707, -6.0 }, { T::peak, 3000.0, 1.5, 4.0 },
                        { T::peak, 8000.0, 2.0, 3.0 } },
                      S::full, 0.0, 1.0, false, 75.0, 1, true },
                    { "Over-ear headphones", "Headphones",
                      { { T::highPass, 15.0, 0.707, 0.0 }, { T::lowShelf, 100.0, 0.707, 2.0 }, { T::peak, 3000.0, 1.0, 2.0 } },
                      S::full, 0.0, 1.0, false, 80.0, 0, false },
                    { "Car", "Car",
                      { { T::highPass, 25.0, 0.707, 0.0 }, { T::lowShelf, 60.0, 0.707, 8.0 }, { T::peak, 300.0, 1.0, -2.0 },
                        { T::lowPass, 15000.0, 0.707, 0.0 } },
                      S::full, 0.0, 0.7, false, 80.0, 2, true },
                    { "TV", "TV",
                      { { T::highPass, 110.0, 0.707, 0.0 }, { T::highPass, 110.0, 0.707, 0.0 }, { T::peak, 2000.0, 0.8, 3.0 },
                        { T::lowPass, 10000.0, 0.707, 0.0 } },
                      S::partial, 200.0, 0.6, true, 65.0, 4, false },
                }};
                return list;
            }
        }

        const Profile& get (int index) noexcept { return all()[(size_t) juce::jlimit (0, count - 1, index)]; }

        juce::StringArray listenNames()
        {
            juce::StringArray names { "Off" };
            for (int i = 0; i < count; ++i)
                names.add (get (i).shortName);
            return names;
        }

        double responseDb (const Profile& p, double hz) noexcept
        {
            constexpr double sr = 96000.0; // analysis rate for the model curves (no warping near 20 kHz)
            double db = 0.0;
            for (const auto& f : p.filters)
                db += Biquad::design (f.type, sr, f.hz, f.q, f.gainDb).magnitudeDb (hz, sr);
            return db;
        }

        double noiseDb (const Profile& p, double hz) noexcept
        {
            const auto o = std::log2 (std::max (20.0, hz) / 1000.0);
            switch (p.noise)
            {
                case 1: return 50.0 - 4.0 * o;                          // street: broadband, low-tilted
                case 2: return std::max (20.0, 52.0 - 9.0 * (o + 3.0)); // road: strong below 200 Hz
                case 3: return 68.0 - 3.0 * std::abs (o + 1.0);         // crowd: centred near 500 Hz
                case 4: return 28.0 - 3.0 * o;                          // living room
                default: return 20.0 - 3.0 * o;                         // quiet room
            }
        }
    }

    double hearingThresholdDb (double hz) noexcept
    {
        const auto f = std::max (20.0, hz) / 1000.0;
        return 3.64 * std::pow (f, -0.8) - 6.5 * std::exp (-0.6 * (f - 3.3) * (f - 3.3)) + 1.0e-3 * f * f * f * f;
    }

    // --------------------------------------------------------------------------------------------
    void ProfileAudition::prepare (double sr, int)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        eCoef = 1.0 - std::exp (-1.0 / (3.0 * sampleRate));
        gainCoef = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
        limAttack = 1.0 - std::exp (-1.0 / (0.005 * sampleRate));
        limRelease = 1.0 - std::exp (-1.0 / (0.150 * sampleRate));
        const auto p = profile;
        profile = -2;
        setProfile (p);
    }

    void ProfileAudition::reset() noexcept
    {
        for (auto& ch : chain)
            for (auto& b : ch)
                b.reset();
        for (auto* arr : { &sideHp, &limiterSplit })
            for (auto& b : *arr)
                b.reset();
        for (auto& b : kDry) b.reset();
        for (auto& b : kWet) b.reset();
        limEnv = 0.0;
        eDry = eWet = 1.0e-9;
        matchGain = 1.0;
    }

    void ProfileAudition::setProfile (int index) noexcept
    {
        if (index == profile)
            return;
        profile = index;
        if (index < 0)
        {
            model = nullptr;
            numFilters = 0;
            return;
        }
        model = &profiles::get (index);
        numFilters = std::min (maxFilters, (int) model->filters.size());
        for (auto& ch : chain)
            for (int i = 0; i < numFilters; ++i)
            {
                const auto& f = model->filters[(size_t) i];
                ch[(size_t) i] = Biquad::design (f.type, sampleRate, f.hz, f.q, f.gainDb);
            }
        sideHp[0] = Biquad::design (Biquad::Type::highPass, sampleRate, std::max (20.0, model->monoBelowHz), 0.707);
        sideHp[1] = sideHp[0];
        limiterSplit[0] = Biquad::design (Biquad::Type::lowPass, sampleRate, 200.0, 0.707);
        limiterSplit[1] = limiterSplit[0];
        limThreshold = std::pow (10.0, -18.0 / 20.0); // bass level where small drivers start limiting

        KWeighting k;
        k.prepare (sampleRate);
        kDry[0] = kDry[2] = k.shelf;
        kDry[1] = kDry[3] = k.rlb;
        kWet = kDry;
        reset();
    }

    void ProfileAudition::process (float* left, float* right, int n, bool levelMatch) noexcept
    {
        if (model == nullptr)
            return;

        const auto& m = *model;
        for (int i = 0; i < n; ++i)
        {
            const double dl = left[i], dr = right[i];
            double l = dl, r = dr;

            for (int f = 0; f < numFilters; ++f)
            {
                l = chain[0][(size_t) f].process (l);
                r = chain[1][(size_t) f].process (r);
            }

            double mid = 0.5 * (l + r), side = 0.5 * (l - r);
            switch (m.stereo)
            {
                case Profile::Stereo::mono:    side = 0.0; break;
                case Profile::Stereo::partial: side = m.sideGain * sideHp[1].process (sideHp[0].process (side)); break;
                case Profile::Stereo::full:    side *= m.sideGain; break;
            }

            if (m.limiter)
            {
                // Bass limiter on the mid low band: a small driver runs out of excursion.
                const auto low = limiterSplit[1].process (limiterSplit[0].process (mid));
                const auto level = std::abs (low);
                limEnv += (level > limEnv ? limAttack : limRelease) * (level - limEnv);
                if (limEnv > limThreshold)
                {
                    const auto gr = std::pow (limThreshold / limEnv, 0.75); // 4:1
                    mid += (gr - 1.0) * low;
                }
            }

            l = mid + side;
            r = mid - side;

            // Loudness match (K-weighted energy, 3 s).
            const auto kd = kDry[1].process (kDry[0].process (dl)), kd2 = kDry[3].process (kDry[2].process (dr));
            const auto kw = kWet[1].process (kWet[0].process (l)), kw2 = kWet[3].process (kWet[2].process (r));
            eDry += eCoef * (kd * kd + kd2 * kd2 - eDry);
            eWet += eCoef * (kw * kw + kw2 * kw2 - eWet);
            const auto target = levelMatch ? juce::jlimit (0.125, 8.0, std::sqrt ((eDry + 1.0e-12) / (eWet + 1.0e-12))) : 1.0;
            matchGain += gainCoef * (target - matchGain);

            left[i] = (float) (l * matchGain);
            right[i] = (float) (r * matchGain);
        }
    }
}
