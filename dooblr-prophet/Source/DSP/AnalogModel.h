#pragma once

#include <algorithm>
#include <cmath>

/**
    Model constants for the analog emulation. Every value here follows docs/analog-reference.md
    (section names in the comments). They are REPRESENTATIVE values taken from the clone
    datasheets (Alfa AS3340A / AS3320) and community descriptions quoted there, not fits to
    real hardware: the "Measurements to fit later" list in that document is still open.
*/
namespace dooblrprophet::dsp::model
{
    //==============================================================================
    // Oscillator (CEM3340 style), "Oscillator" + "Blueprint / Oscillator"

    /** Reset time of the ramp core. f_eff = 1 / (1/f_ideal + t_r); doc: t_r ~1-3 us,
        "2 us @ 5 kHz ~ 17 cents flat". */
    inline constexpr float rampResetSeconds = 2.0e-6f;

    /** Finite reset (~1 us) used for the hard-sync slave reset delay and the triangle glitch. */
    inline constexpr float syncResetSeconds = 1.0e-6f;

    /** "Tri ~ half saw amplitude (~10 V saw, 5 V tri pre-scaling)": applied to the switched
        (CEM3340-style) waves. The continuous-shape (P6-style discrete) VCO keeps full level. */
    inline constexpr float switchedTriangleLevel = 0.5f;

    /** "Triangle asymmetry 45-55 %": peak position = 0.5 +- (fixed + vintage-scaled) spread. */
    inline constexpr float triangleSkewFixed = 0.01f;
    inline constexpr float triangleSkewVintage = 0.04f;

    /** "Triangle glitch at reset": a spike of this height (in triangle units) lasting
        syncResetSeconds, injected as a band-limited impulse of equal area. */
    inline constexpr float triangleGlitchHeight = 0.6f;

    /** "Small PWM->pitch crosstalk": relative frequency change per unit of (width - 0.5).
        0.004 -> about +-3 cents at the width extremes. */
    inline constexpr float pwmPitchLeak = 0.004f;

    /** "Scale error <= ~3.5 cents": per-voice 1V/oct scale error (fraction of a semitone per
        semitone from C4), times Vintage. 0.001 -> 3.6 cents three octaves from C4. */
    inline constexpr float scaleErrorMax = 0.001f;

    /** Per-voice static offset and slow random drift ("per-voice variance"), cents, times Vintage. */
    inline constexpr float offsetCentsMax = 7.0f;
    inline constexpr float driftCentsMax = 5.0f;

    /** "Temp ~+3300 ppm/degC -> ... warm-up exponential": initial per-voice warm-up offset in
        cents (times Vintage) decaying with this time constant after the plugin starts. */
    inline constexpr float warmupCentsMax = 6.0f;
    inline constexpr float warmupSeconds = 120.0f;

    /** "4-8x oversampling when Poly Mod FM active"; the filter always runs at >= 2x. */
    inline constexpr int oversamplePolyModFm = 4;
    inline constexpr int oversampleDefault = 2;

    //==============================================================================
    // Filter, "Filter CEM3320", "SSM2040 / 2140", "Blueprint / Filter"

    struct LadderModel
    {
        float kMax;            // feedback at full resonance (self-oscillation from k = 4)
        float resoTaper;       // k = kMax * reso^taper ("k linear in resonance control with a knob taper")
        float inputLevel;      // soft clip at mixer / filter input ("saturation mainly at mixer level")
        float resonanceLevel;  // tanh-like resonance cell in the feedback (sets self-osc amplitude)
        float stageLevel;      // per-stage saturation; 0 = clean cells
        float outputLevel;     // output stage clip near the rails ...
        float outputBias;      // ... with a DC bias ("12 Vpp with 6.5 V DC offset") -> asymmetric
    };

    /** CEM3320 ("Rev3"): clean gain cells; saturation at input, resonance cell and output. */
    inline constexpr LadderModel cem3320 { 4.2f, 1.0f, 2.0f, 1.5f, 0.0f, 2.5f, 0.25f };

    /** SSM2040 / 2140 ("Rev2", also the P6-style filter): same architecture, "saturates earlier
        and softer" (community hypothesis, unverified), with mild per-stage saturation and a
        different resonance law. */
    inline constexpr LadderModel ssm2040 { 4.3f, 1.25f, 1.3f, 1.1f, 0.9f, 2.0f, 0.25f };

    inline const LadderModel& ladderModel (int index) noexcept { return index == 1 ? ssm2040 : cem3320; }

    /** Feedback k for a 0..1 resonance control. */
    inline float ladderK (const LadderModel& m, float resonance) noexcept
    {
        return m.kMax * std::pow (std::clamp (resonance, 0.0f, 1.0f), m.resoTaper);
    }

    /** "Per-voice randomize each stage's cutoff": relative mismatch = fixed + vintage-scaled. */
    inline constexpr float stageMismatchFixed = 0.004f;
    inline constexpr float stageMismatchVintage = 0.02f;

    /** Per-voice cutoff offset (octaves, times Vintage) and warm-up drift ("warm-up drift 0.5 %"). */
    inline constexpr float cutoffOffsetOctaves = 0.25f;
    inline constexpr float cutoffWarmupOctaves = 0.0072f; // log2 (1.005)

    /** "CV feedthrough: 60 mV cutoff, 0.2 V resonance (model as small HP-coupled term ~ CV)".
        Output added per octave of cutoff CV change / per unit of k change, high-passed. */
    inline constexpr float cutoffFeedthrough = 0.004f;
    inline constexpr float resonanceFeedthrough = 0.003f;
    inline constexpr float feedthroughHighPassHz = 30.0f;

    /** Newton iterations per sample on the resonance-cell nonlinearity ("1-2 Newton / fixed-point
        iterations per sample at >= 2x oversampling"). */
    inline constexpr int newtonIterations = 2;

    /** Tiny input noise floor (circuit noise): lets a self-oscillating filter start from silence. */
    inline constexpr float thermalNoise = 1.0e-5f;
}
