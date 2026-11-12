#pragma once
// Real-time human voice: lungs → glottis → articulated airway.
// Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Articulation.h"
#include "Decimator.h"
#include "Glottis.h"
#include "HumanAirway.h"
#include "Phonetics/Score.h"

namespace dooblrthroat::dsp
{
    struct VoiceSource
    {
        double f0 = 110.0, glide = 0.06, pressure = 8.0, breathiness = 0.15, tension = 0.5;
        double fry = 0.0, roughness = 0.1, vibrato_depth = 0.0, vibrato_rate = 6.0, drift = 5.0;
        bool voiced = true;
        double brightness = 0.25;
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

        // Phrase / gesture score. The gesture pointer is into a PublishedPhrase
        // owned off the audio thread; the voice never allocates or frees it.
        const phonetics::Gesture* scoreGestures = nullptr;
        size_t scoreCount = 0;
        double scoreT = 0, scoreRate = 1, scoreF0Scale = 1, scoreF0Override = 0;
        float scoreNasal = -1.0f;
        bool scoreActive = false;

        HumanVoice (double fs_out, HumanBody body, size_t oversample);
        static HumanVoice with_defaults (double fs_out) { return HumanVoice (fs_out, BODY_MALE, oversample_for (fs_out)); }

        void set_articulation (const ArticPatch& values, bool tip_free);
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
            for (auto& sp : artic.speed) sp = 1.0;
        }
        void reset();
        void render (float* out, size_t n);

    private:
        void apply_source();
        void control();
        bool enforce_contacts();
        void applyScore (double dt);
    };
}
