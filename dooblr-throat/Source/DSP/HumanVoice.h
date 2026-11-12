#pragma once
// Real-time human voice: lungs → glottis → articulated airway.
// Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Articulation.h"
#include "Decimator.h"
#include "Glottis.h"
#include "HumanAirway.h"
#include "Phonetics/Score.h"
#include "Klatt.h"
#include "ProvenVoice.h"

namespace dooblrthroat::dsp
{
    struct VoiceSource
    {
        double f0 = 110.0, glide = 0.06, pressure = 8.0, breathiness = 0.15, tension = 0.5;
        double fry = 0.0, roughness = 0.1, vibrato_depth = 0.0, vibrato_rate = 6.0, drift = 0.0;
        bool voiced = true;
        double brightness = 0.25;
        double effort = 0.5;          // 0.5 matches the 0.2.0 pressure-derived default
        double formantTuning = 0.0;   // 0 = off (speech); 1 = F1/F2 track harmonics at high f0
    };

    inline constexpr VoiceSource DEFAULT_SOURCE {};

    inline size_t oversample_for (double fs_out)
    {
        if (! is_finite (fs_out) || fs_out < 1.0)
            fs_out = 48000.0;
        return size_t (js_max (1.0, floor (60000.0 / fs_out) + 1.0));
    }

    struct HumanVoice
    {
        double fs_out = 48000.0, fs = 48000.0;
        size_t m = 2;
        HumanAirway airway;
        ParametricGlottis glottis;
        ArticulatorBank artic;
        VoiceSource source = DEFAULT_SOURCE;
        Decimator dec;
        double gate = 0.0, gate_target = 0.0, p_lung = 0.0, p_lung_now = 0.0, p_lung_step = 0.0, f0 = 110.0;
        double output_gain = 5.5e-7, dc_x = 0.0, dc_y = 0.0, dc_coef = 0.0;
        double lim_env = 0.0, lim_release = 0.0, max_penetration = 0.1;
        int64_t ctrl_every = 0, ctrl_counter = 0, lim_hold = 0, lim_hold_n = 0;
        double base_abduction = 0.0, base_closure = 0.0;
        double effort_body = 0.0;
        int clarityMode = 0; // 0 default, 1 A, 2 B, 3 C, 4 K, 5 E espeak, 6 P piper, 7 M mbrola
        ArticPatch schwaRef {};
        LfGlottis lf;
        // Hybrid consonant layer (mode B): allocation-free biquad + burst envelope.
        Rng consRng { 991 };
        double consX1 = 0, consX2 = 0, consY1 = 0, consY2 = 0;
        double consB0 = 0, consB1 = 0, consB2 = 0, consA1 = 0, consA2 = 0;
        double consFc = 0, burstEnv = 0, prevLip = 1, prevMinA = 1, consHpX = 0, consHpY = 0;

        // Phrase / gesture score. The gesture pointer is into a PublishedPhrase
        // owned off the audio thread; the voice never allocates or frees it.
        const phonetics::Gesture* scoreGestures = nullptr;
        size_t scoreCount = 0;
        double scoreT = 0, scoreRate = 1, scoreF0Scale = 1, scoreF0Override = 0;
        float scoreNasal = -1.0f;
        bool scoreActive = false;
        const KlattFrame* klattFrames = nullptr;
        size_t klattCount = 0;
        double klattHop = 0.005, klattT0 = 0;
        KlattFrame klattPad {};
        bool klattPadOn = false;
        KlattSynth klatt;
        ProvenEngine* proven = nullptr;

        HumanVoice (double fs_out, HumanBody body, size_t oversample);
        static HumanVoice with_defaults (double fs_out) { return HumanVoice (fs_out, BODY_MALE, oversample_for (fs_out)); }

        void set_articulation (const ArticPatch& values, bool tip_free);
        void snap_articulation (const ArticPatch& values, bool tip_free);
        void set_source (const VoiceSource& s) { source = s; apply_source(); }
        void set_gate (double level) { gate_target = clamp (level, 0.0, 1.5); }
        void clearScore() noexcept
        {
            scoreGestures = nullptr;
            scoreCount = 0;
            scoreT = 0;
            scoreRate = 1;
            scoreF0Scale = 1;
            scoreF0Override = 0;
            scoreNasal = -1.0f;
            scoreActive = false;
            klattFrames = nullptr;
            klattCount = 0;
            klattHop = 0.005;
            klattT0 = 0;
            for (auto& sp : artic.speed) sp = 1.0;
        }
        void reset();
        void render (float* out, size_t n);

    private:
        void apply_source();
        void control();
        bool enforce_contacts();
        void applyScore (double dt);
        double hybridConsonant (double dt) noexcept;
        void setConsBand (double fc, double bw) noexcept;
        void renderKlatt (float* out, size_t n);
        void syncKlattFrame();
    };
}
