// Standalone renderer used to compile-check the DSP without JUCE.
#include "DSP/ThroatEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    using namespace dooblrthroat::dsp;
    ThroatEngine e;
    e.prepare (48000.0, 512);
    std::vector<float> L (512), R (512);
    e.process (L.data(), R.data(), 512);
    double e0 = 0;
    for (auto x : L) e0 += x * x;
    if (e0 > 1e-12) { std::fprintf (stderr, "silent expected, energy %g\n", e0); return 1; }

    e.noteOn (60, 0.8f);
    double energy = 0, peak = 0;
    bool nan = false;
    for (int b = 0; b < 200; ++b)
    {
        e.process (L.data(), R.data(), 512);
        for (auto x : L)
        {
            if (! std::isfinite (x)) nan = true;
            energy += x * x;
            peak = std::max (peak, (double) std::abs (x));
        }
    }
    e.noteOff (60);
    for (int b = 0; b < 50; ++b) e.process (L.data(), R.data(), 512);
    std::printf ("energy %g peak %g nan %d\n", energy, peak, (int) nan);
    return (nan || energy < 1e-8 || peak > 1.0) ? 2 : 0;
}
