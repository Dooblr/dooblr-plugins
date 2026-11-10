#include "Decimator.h"
#include "Math.h"

namespace dooblrthroat::dsp
{
    static double bessel_i0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            const auto q = x / (2.0 * double (k));
            term *= q * q;
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    static std::vector<double> design_lowpass (size_t taps, double cutoff, double beta)
    {
        std::vector<double> h (taps);
        const auto mid = (double (taps) - 1.0) / 2.0;
        const auto i0b = bessel_i0 (beta);
        double sum = 0.0;
        for (size_t i = 0; i < taps; ++i)
        {
            const auto x = double (i) - mid;
            const auto sinc = (x == 0.0) ? 2.0 * cutoff : sin (2.0 * PI * cutoff * x) / (PI * x);
            const auto r = (2.0 * double (i)) / (double (taps) - 1.0) - 1.0;
            const auto w = bessel_i0 (beta * sqrt (js_max (0.0, 1.0 - r * r))) / i0b;
            h[i] = sinc * w;
            sum += h[i];
        }
        for (auto& hi : h) hi /= sum;
        return h;
    }

    Decimator::Decimator (size_t factorIn, size_t taps_per_phase) : factor (factorIn)
    {
        const auto taps = factor == 1 ? size_t (1) : factor * taps_per_phase + 1;
        h = factor == 1 ? std::vector<double> { 1.0 } : design_lowpass (taps, 0.46 / double (factor), 8.5);
        buf.assign (h.size() * 2, 0.0);
    }

    bool Decimator::push (double x) noexcept
    {
        const auto n = h.size();
        buf[pos] = x;
        buf[pos + n] = x;
        pos = (pos + 1 == n) ? 0 : pos + 1;
        ++count;
        if (count < factor) return false;
        count = 0;
        const auto start = pos;
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i)
            acc += h[n - 1 - i] * buf[start + i];
        value = acc;
        return true;
    }
}
