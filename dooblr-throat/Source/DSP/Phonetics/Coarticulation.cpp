#include "Coarticulation.h"
#include "../Math.h"
#include <cmath>

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        void clampToVowelTriangleImpl (double& a1, double& a2) noexcept
        {
            a1 = js_max (0.0, js_min (1.0, a1));
            a2 = js_max (0.0, js_min (1.0, a2));
            if (a1 + a2 > 1.0)
            {
                const auto excess = a1 + a2 - 1.0;
                a1 = js_max (0.0, a1 - 0.5 * excess);
                a2 = js_max (0.0, a2 - 0.5 * excess);
            }
        }

        bool solveTwoCol (const double* col0, const double* col1, const double* b,
                          int m, double& a1, double& a2) noexcept
        {
            double ata00 = 0.0, ata01 = 0.0, ata11 = 0.0, atb0 = 0.0, atb1 = 0.0;
            for (int i = 0; i < m; ++i)
            {
                ata00 += col0[i] * col0[i];
                ata01 += col0[i] * col1[i];
                ata11 += col1[i] * col1[i];
                atb0 += col0[i] * b[i];
                atb1 += col1[i] * b[i];
            }
            const auto det = ata00 * ata11 - ata01 * ata01;
            if (! std::isfinite (det) || std::abs (det) < 1.0e-12)
                return false;
            a1 = ( ata11 * atb0 - ata01 * atb1) / det;
            a2 = (-ata01 * atb0 + ata00 * atb1) / det;
            return std::isfinite (a1) && std::isfinite (a2);
        }

        ArticPatch cornerVowel (std::string_view ipa, const BodyMix& body)
        {
            if (auto v = vowelArtic (ipa, body))
                return *v;
            return {};
        }

        double getOr (const ArticPatch& p, ArticKey k, double fallback)
        {
            if (auto x = p.get (k))
                return *x;
            return fallback;
        }
    }

    void clampToVowelTriangle (double& a1, double& a2) noexcept
    {
        clampToVowelTriangleImpl (a1, a2);
    }

    CornerWeights vowelCornerWeights (const ArticPatch& vowel, const BodyMix& body)
    {
        const auto xa = cornerVowel ("a", body);
        const auto xi = cornerVowel ("i", body);
        const auto xu = cornerVowel ("u", body);
        CornerWeights w;

        // Lingual subspace: tongue-body position (Birkholz 2013, the /a,i,u/ tongue plane).
        {
            const ArticKey keys[] { ArticKey::TbX, ArticKey::TbY };
            double col0[2], col1[2], b[2];
            for (int i = 0; i < 2; ++i)
            {
                const auto k = keys[i];
                const auto a = getOr (xa, k, 0.0);
                col0[i] = getOr (xi, k, a) - a;
                col1[i] = getOr (xu, k, a) - a;
                b[i] = getOr (vowel, k, a) - a;
            }
            if (! solveTwoCol (col0, col1, b, 2, w.a1, w.a2))
                w.a1 = w.a2 = 0.0;
            clampToVowelTriangleImpl (w.a1, w.a2);
        }

        // Labial subspace, solved independently (Birkholz 2013 eqs. 6–7).
        {
            const ArticKey keys[] { ArticKey::LipAp, ArticKey::LipProt, ArticKey::LipWidth };
            double col0[3], col1[3], b[3];
            for (int i = 0; i < 3; ++i)
            {
                const auto k = keys[i];
                const auto a = getOr (xa, k, 0.0);
                col0[i] = getOr (xi, k, a) - a;
                col1[i] = getOr (xu, k, a) - a;
                b[i] = getOr (vowel, k, a) - a;
            }
            if (! solveTwoCol (col0, col1, b, 3, w.b1, w.b2))
                w.b1 = w.b2 = 0.0;
            clampToVowelTriangleImpl (w.b1, w.b2);
        }
        return w;
    }

    ArticPatch consonantInVowelContext (const PhonemeDef& def, const ArticPatch& vowel)
    {
        ArticPatch out = vowel;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            const auto k = ARTIC_ALL[i];
            // Velum is an independent motor command (Birkholz 2007). Source-coupled
            // keys stay off this oral blend.
            if (k == ArticKey::Velum || k == ArticKey::Epilarynx || k == ArticKey::Ventricular)
            {
                out.values[i].reset();
                continue;
            }
            const auto xc = def.targets.get (k);
            const auto xv = vowel.get (k);
            const auto dominant = def.isDominant (k);
            const double w = dominant ? 1.0 : (xc ? 0.35 : 0.0);
            if (! xc && ! xv)
            {
                out.values[i].reset();
                continue;
            }
            if (! xc)
                continue; // keep the vowel value already copied
            if (! xv)
            {
                out.set (k, *xc);
                continue;
            }
            // Eq. (5) of Birkholz 2007: xc|v = xv + w_c (xc - xv).
            out.set (k, *xv + w * (*xc - *xv));
        }
        return out;
    }

    namespace
    {
        bool isLabialKey (ArticKey k) noexcept
        {
            return k == ArticKey::LipAp || k == ArticKey::LipProt
                || k == ArticKey::LipWidth || k == ArticKey::Labiodental;
        }
    }

    ArticPatch contextConsonantTarget (const PhonemeDef& def,
                                       const ArticPatch* vowel,
                                       const BodyMix& body)
    {
        if (vowel == nullptr || vowel->is_empty())
            return def.targets;

        const auto xa = cornerVowel ("a", body);
        const auto xi = cornerVowel ("i", body);
        const auto xu = cornerVowel ("u", body);
        if (xa.is_empty() || xi.is_empty() || xu.is_empty())
            return def.targets;

        const auto ca = consonantInVowelContext (def, xa);
        const auto ci = consonantInVowelContext (def, xi);
        const auto cu = consonantInVowelContext (def, xu);
        const auto w = vowelCornerWeights (*vowel, body);

        ArticPatch out;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            const auto k = ARTIC_ALL[i];
            const auto da = ca.get (k), di = ci.get (k), du = cu.get (k);
            if (! da && ! di && ! du)
                continue;
            const auto a = da.value_or (di.value_or (du.value_or (0.0)));
            const auto iv = di.value_or (a);
            const auto uv = du.value_or (a);
            const auto c1 = isLabialKey (k) ? w.b1 : w.a1;
            const auto c2 = isLabialKey (k) ? w.b2 : w.a2;
            // Eq. (5) / (7) of Birkholz 2013.
            out.set (k, a + c1 * (iv - a) + c2 * (uv - a));
        }
        return out;
    }
}
