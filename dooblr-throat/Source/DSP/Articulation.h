#pragma once
// Articulator state, body mix and target-approximation tracking, ported from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge). The audio-thread tracker is a third-order
// cascaded first-order system after Birkholz (2007), Interspeech; time constants
// follow the lips / tip / body / velum relatives cited there from Mermelstein (1973).

#include "Geom.h"
#include "Math.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace dooblrthroat::dsp
{
    inline constexpr size_t N_ARTIC = 17;

    enum class ArticKey : uint8_t
    {
        Jaw, TbX, TbY, TtX, TtY, TtCurl, Root, LipAp, LipProt, LipWidth,
        Labiodental, Velum, Larynx, Epilarynx, Ventricular, Lateral, Dorsum
    };

    inline constexpr ArticKey ARTIC_ALL[N_ARTIC] = {
        ArticKey::Jaw, ArticKey::TbX, ArticKey::TbY, ArticKey::TtX, ArticKey::TtY,
        ArticKey::TtCurl, ArticKey::Root, ArticKey::LipAp, ArticKey::LipProt, ArticKey::LipWidth,
        ArticKey::Labiodental, ArticKey::Velum, ArticKey::Larynx, ArticKey::Epilarynx,
        ArticKey::Ventricular, ArticKey::Lateral, ArticKey::Dorsum
    };

    inline constexpr const char* ARTIC_NAMES[N_ARTIC] = {
        "jaw", "tbX", "tbY", "ttX", "ttY", "ttCurl", "root", "lipAp", "lipProt", "lipWidth",
        "labiodental", "velum", "larynx", "epilarynx", "ventricular", "lateral", "dorsum"
    };

    inline size_t artic_index (ArticKey k) noexcept { return size_t (k); }

    struct HumanArtic
    {
        double jaw = 0.6, tb_x = 2.9, tb_y = 6.85, tt_x = 6.45, tt_y = 7.3, tt_curl = 0.0;
        double root = 0.0, lip_ap = 0.7, lip_prot = 0.2, lip_width = 0.5, labiodental = 0.0;
        double velum = -0.15, larynx = 0.0, epilarynx = 0.0, ventricular = 0.0, lateral = 0.0, dorsum = 0.0;

        double get (ArticKey k) const noexcept;
        double& get_mut (ArticKey k) noexcept;
        void set (ArticKey k, double x) noexcept { get_mut (k) = x; }
        std::array<double, N_ARTIC> to_array() const;
        static HumanArtic from_array (const double* a, size_t n);
    };

    inline constexpr HumanArtic NEUTRAL {};

    struct ArticPatch
    {
        std::array<std::optional<double>, N_ARTIC> values {};
        std::optional<double> get (ArticKey k) const { return values[artic_index (k)]; }
        ArticPatch& set (ArticKey k, double x) { values[artic_index (k)] = x; return *this; }
        ArticPatch with (ArticKey k, double x) const { auto p = *this; p.set (k, x); return p; }
        HumanArtic apply_to (const HumanArtic& base) const;
        void merge (const ArticPatch& other);
        bool is_empty() const;
        static ArticPatch blend (const std::pair<double, ArticPatch>* parts, size_t n);
    };

    std::optional<ArticKey> artic_from_name (std::string_view s);
    Vec2 rest_tip (double jaw, double tb_x, double tb_y);

    struct HumanBody
    {
        const char* name = "Adult male";
        double scale = 1.0;
        double pharynx = 1.0;
        double tongue_radius = 2.25;
        double lip_width = 4.6;
    };

    inline constexpr HumanBody BODY_MALE   { "Adult male",   1.0,  1.0,  2.25, 4.6 };
    inline constexpr HumanBody BODY_FEMALE { "Adult female", 0.9,  0.88, 2.2,  4.4 };
    inline constexpr HumanBody BODY_CHILD  { "Child",        0.74, 0.8,  2.15, 4.0 };

    enum class BodyName { Male, Female, Child };
    inline HumanBody body_of (BodyName n)
    {
        switch (n) { case BodyName::Female: return BODY_FEMALE; case BodyName::Child: return BODY_CHILD; default: return BODY_MALE; }
    }

    struct BodyMix
    {
        double female = 0.0, child = 0.0;
        static BodyMix make (double female, double child);
        static const BodyMix MALE, FEMALE, CHILD;
        void weights (double w[3]) const; // male, female, child
        std::optional<BodyName> preset() const;
        BodyName nearest() const;
        HumanBody body() const;
        template <typename F> double blend (F f) const
        {
            if (auto p = preset()) return f (*p);
            double w[3]; weights (w);
            return w[0] * f (BodyName::Male) + w[1] * f (BodyName::Female) + w[2] * f (BodyName::Child);
        }
    };

    struct ArticSpec { double min, max, hz; };

    inline constexpr ArticSpec ARTIC_SPECS[N_ARTIC] = {
        { -0.1, 2.6, 10.0 }, { 1.0, 4.8, 12.0 }, { 4.8, 9.2, 12.0 }, { 3.5, 8.0, 13.0 },
        { 5.0, 10.8, 13.0 }, { 0.0, 1.0, 9.0 }, { -1.0, 1.0, 9.0 }, { -0.4, 2.4, 11.0 },
        { -0.2, 1.5, 11.0 }, { 0.0, 1.0, 11.0 }, { 0.0, 1.0, 10.0 }, { -0.25, 1.4, 12.0 },
        { -1.6, 1.6, 8.0 }, { 0.0, 1.0, 6.0 }, { 0.0, 1.0, 6.0 }, { 0.0, 1.0, 10.0 }, { 0.0, 1.0, 9.0 }
    };

    // Birkholz 2007, citing Mermelstein 1973: tongue body and jaw ~75 ms, tongue
    // tip ~50 ms, lips ~30 ms (first-order). The third-order system reaches 90%
    // in about 5.32 tau, vs 2.30 tau for first-order, so these values are those
    // constants scaled by 2.30/5.32 to keep the same rise time. Velum is an
    // independent command; 60 ms first-order equivalent.
    inline constexpr double kTauFirstToThird = 2.302585 / 5.322;
    inline constexpr double ARTIC_TAU[N_ARTIC] = {
        0.075 * kTauFirstToThird, // Jaw
        0.075 * kTauFirstToThird, // TbX
        0.075 * kTauFirstToThird, // TbY
        0.050 * kTauFirstToThird, // TtX
        0.050 * kTauFirstToThird, // TtY
        0.050 * kTauFirstToThird, // TtCurl
        0.075 * kTauFirstToThird, // Root
        0.030 * kTauFirstToThird, // LipAp
        0.030 * kTauFirstToThird, // LipProt
        0.030 * kTauFirstToThird, // LipWidth
        0.030 * kTauFirstToThird, // Labiodental
        0.060 * kTauFirstToThird, // Velum
        0.075 * kTauFirstToThird, // Larynx
        0.060 * kTauFirstToThird, // Epilarynx
        0.060 * kTauFirstToThird, // Ventricular
        0.050 * kTauFirstToThird, // Lateral
        0.075 * kTauFirstToThird  // Dorsum
    };

    struct ArticulatorBank
    {
        HumanArtic x = NEUTRAL, target = NEUTRAL, effective = NEUTRAL;
        double v[N_ARTIC] {}, speed[N_ARTIC] {}, omega[N_ARTIC] {};
        double z1[N_ARTIC] {}, z2[N_ARTIC] {}; // first two stages of the 3rd-order cascade
        double effortScale = 1.0;              // 1 at default Effort; smaller tau when > 1
        bool tip_free = true;

        ArticulatorBank();
        void set_target (const ArticPatch& values);
        void snap (const ArticPatch& values);
        void step (double dt);
    };
}
