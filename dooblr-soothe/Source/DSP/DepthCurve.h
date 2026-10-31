#pragma once

#include <array>

#include "../Parameters.h"

namespace dooblrsoothe::dsp
{
    /**
        The depth curve: how strongly each frequency is processed, as a multiplier on the
        reduction the detector asks for (1 = as detected, 2 = twice as much, 0 = untouched).

        The curve reads like an EQ drawn over the spectrum: where it dips, the plug-in cuts more.
        Bell, shelf and band-pass bands add their Gain (dB) to the curve over their region; the
        curve's dB value becomes the multiplier 10^(-dB / 20), so -6 dB roughly doubles the
        reduction there and +6 dB halves it. Low and high cuts are masks that fade the
        processing out below / above their frequency (Q sets the steepness); the curve rises
        there (less reduction). When any band pass is in the curve, processing is limited to the
        union of the band-pass regions.

        Plain functions of the band settings: the audio thread evaluates them per FFT bin when
        a band changes, and the editor draws exactly the same curve.
    */
    struct Band
    {
        bool active = false;
        params::Shape shape = params::Shape::bell;
        float freq = 1000.0f, gain = 0.0f, q = 1.0f;

        bool operator== (const Band& o) const noexcept
        {
            return active == o.active && shape == o.shape && freq == o.freq && gain == o.gain && q == o.q;
        }
        bool operator!= (const Band& o) const noexcept { return ! (*this == o); }
    };

    using Bands = std::array<Band, params::numBands>;

    /** The multiplier (>= 0) at a frequency. */
    float curveAmount (const Bands& bands, float frequency) noexcept;

    /** The curve in dB as drawn (-20 log10 of the amount: below 0 = more reduction), capped at
        ceilingDb where a cut switches the processing off. */
    float curveDb (const Bands& bands, float frequency, float ceilingDb = 60.0f) noexcept;

    /** One band's own contribution in dB (bells / shelves / band pass), or its mask in dB (cuts):
        the band's coloured outline in the editor. */
    float bandDb (const Band& band, float frequency, float ceilingDb = 60.0f) noexcept;

    /** 0..1: where a band acts (its region). Band Listen plays only this part of what is removed. */
    float bandRegion (const Band& band, float frequency) noexcept;
}
