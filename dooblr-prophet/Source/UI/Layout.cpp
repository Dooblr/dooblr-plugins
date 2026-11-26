#include "Layout.h"

#include "../Parameters.h"

namespace dooblrprophet::layout
{
    namespace
    {
        namespace id = params::id;
        using Kind = ui::ControlPanel::Kind;

        Control knob (const char* pid, const char* caption) { return { pid, caption, Kind::knob }; }
        Control toggle (const char* pid, const char* caption, int stack = -1)
        {
            Control c { pid, caption, Kind::toggle };
            c.stack = stack;
            return c;
        }
        Control combo (const char* pid, const char* caption, int width = 0)
        {
            Control c { pid, caption, Kind::combo };
            c.width = width;
            return c;
        }

        std::vector<Page> build()
        {
            std::vector<Page> all;

            // Sound: oscillators on top, envelope / filter / envelope in the middle, mixer with
            // basic Poly-Mod and LFO on the bottom so the oscillator row stays one knob high.
            all.push_back ({ "Sound",
                             { { "OSC 1", id::osc1Mode, { "Variable", "Switched" } },
                               { "OSC 2", id::osc2Mode, { "Variable", "Switched" } } },
                             { { "OSCILLATOR 1", { knob (id::osc1Coarse, "Coarse"), knob (id::osc1Fine, "Fine"), knob (id::osc1Shape, "Shape"),
                                                   knob (id::osc1PW, "Pulse Width"), toggle (id::osc1Saw, "Saw", 0), toggle (id::osc1Tri, "Tri", 0),
                                                   toggle (id::osc1Pulse, "Pulse", 0), toggle (id::osc1Sync, "Sync", 0) } },
                               { "OSCILLATOR 2", { knob (id::osc2Coarse, "Coarse"), knob (id::osc2Fine, "Fine"), knob (id::osc2Shape, "Shape"),
                                                   knob (id::osc2PW, "Pulse Width"), toggle (id::osc2Saw, "Saw", 0), toggle (id::osc2Tri, "Tri", 0),
                                                   toggle (id::osc2Pulse, "Pulse", 0), toggle (id::osc2LowFreq, "Lo Freq", 0),
                                                   toggle (id::osc2KeyTrack, "Key", 0) } },
                               { "MIXER", { knob (id::mixOsc1, "Osc 1"), knob (id::mixOsc2, "Osc 2"), knob (id::mixSub, "Sub"), knob (id::mixNoise, "Noise") } },
                               { "FILTER ENVELOPE", { knob (id::fenvAttack, "Attack"), knob (id::fenvDecay, "Decay"), knob (id::fenvSustain, "Sustain"),
                                                      knob (id::fenvRelease, "Release"), knob (id::fenvVelAmt, "Velocity"), toggle (id::fenvSync, "Sync to Amp") } },
                               { "HIGH-PASS", { knob (id::hpfCutoff, "Cutoff"), knob (id::hpfReso, "Resonance"), knob (id::hpfEnvAmt, "Env Amount") } },
                               { "LOW-PASS", { combo (id::lpfModel, "Model", 100), knob (id::lpfCutoff, "Cutoff"), knob (id::lpfReso, "Resonance"),
                                               knob (id::lpfDrive, "Drive"), knob (id::lpfEnvAmt, "Env Amount"), knob (id::lpfKeyTrack, "Key Track") } },
                               { "AMP ENVELOPE", { knob (id::aenvAttack, "Attack"), knob (id::aenvDecay, "Decay"), knob (id::aenvSustain, "Sustain"),
                                                   knob (id::aenvRelease, "Release"), knob (id::aenvVelAmt, "Velocity") } },
                               { "POLY-MOD", { knob (id::pmodFiltEnv, "Filter Env"), knob (id::pmodOsc2, "Osc 2"), toggle (id::pmodDestFreq1, "Osc 1 Freq") } },
                               { "LFO", { combo (id::lfoWave, "Wave", 100), knob (id::lfoRate, "Rate"), knob (id::lfoInitial, "Initial"),
                                          knob (id::lfoWheel, "Wheel"), toggle (id::lfoDestFreq1, "Osc 1 Freq", 1), toggle (id::lfoDestFreq2, "Osc 2 Freq", 1),
                                          toggle (id::lfoDestLpf, "Low-Pass", 1) } } },
                             { { 0, 1 }, { 3, 4, 5, 6 }, { 2, 7, 8 } } });

            all.push_back ({ "Modulation",
                             { { "LFO", id::lfoSync, { "Free", "Tempo Sync" } } },
                             { { "POLY-MOD DESTINATIONS", { toggle (id::pmodDestShape1, "Osc 1 Shape"), toggle (id::pmodDestFilter, "Filter") } },
                               { "LFO", { combo (id::lfoDivision, "Division"), knob (id::lfoNoiseMix, "Noise Mix") } },
                               { "LFO DESTINATIONS", { toggle (id::lfoDestShape1, "Osc 1 Shape"), toggle (id::lfoDestShape2, "Osc 2 Shape"),
                                                       toggle (id::lfoDestHpf, "High-Pass"), toggle (id::lfoDestAmp, "Amp") } } },
                             { { 0 }, { 1 }, { 2 } } });

            Page voicesPage { "Voices",
                              { { "MODE", id::voiceMode, { "Poly", "Unison", "Mono" } } },
                              { { "VOICES", { knob (id::voices, "Voices"), knob (id::unisonVoices, "Unison"), knob (id::unisonDetune, "Detune"),
                                              knob (id::glide, "Glide"), knob (id::vintage, "Vintage") } },
                                { "PERFORMANCE", { knob (id::bendRange, "Bend Range"), knob (id::atFilter, "AT > Filter"), knob (id::atLfo, "AT > LFO") } } },
                              { { 0 }, { 1 } } };
            voicesPage.panels[0].voicePresets = true;
            all.push_back (std::move (voicesPage));

            Page fxPage { "FX",
                          {},
                          { { "ECHO", { toggle (id::echoOn, "On"), knob (id::echoMix, "Mix"), knob (id::echoTime, "Time"), toggle (id::echoSync, "Sync"),
                                        combo (id::echoDivision, "Division"), knob (id::echoFeedback, "Feedback"), toggle (id::echoPingPong, "Ping-Pong") } },
                            { "REVERB", { toggle (id::reverbOn, "On"), knob (id::reverbMix, "Mix"), knob (id::reverbSize, "Size"), knob (id::reverbTone, "Tone") } } },
                          { { 0 }, { 1 } } };
            fxPage.panels[0].fxPicker = 0;
            fxPage.panels[1].fxPicker = 1;
            all.push_back (std::move (fxPage));

            return all;
        }

        std::vector<Page> buildEditor()
        {
            std::vector<Page> all;

            // Main: musical essentials. Oscillator core + mixer, filter with mode toggle,
            // stacked filter / amp ADSR (full width, A/D/S/R aligned). Volume lives in the
            // header; Glide sits next to the filter.
            all.push_back ({ "Main",
                             { { "OSC 1", id::osc1Mode, { "Variable", "Switched" } },
                               { "OSC 2", id::osc2Mode, { "Variable", "Switched" } } },
                             { { "OSCILLATOR 1", { knob (id::osc1Coarse, "Coarse"), knob (id::osc1Fine, "Fine"), knob (id::osc1Shape, "Shape"),
                                                   toggle (id::osc1Saw, "Saw", 0), toggle (id::osc1Tri, "Tri", 0), toggle (id::osc1Pulse, "Pulse", 0) } },
                               { "OSCILLATOR 2", { knob (id::osc2Coarse, "Coarse"), knob (id::osc2Fine, "Fine"), knob (id::osc2Shape, "Shape"),
                                                   toggle (id::osc2Saw, "Saw", 0), toggle (id::osc2Tri, "Tri", 0), toggle (id::osc2Pulse, "Pulse", 0) } },
                               { "MIXER", { knob (id::mixOsc1, "Osc 1"), knob (id::mixOsc2, "Osc 2"), knob (id::mixSub, "Sub"), knob (id::mixNoise, "Noise") } },
                               { "LOW-PASS", { combo (id::filterMode, "Mode", 140), knob (id::lpfCutoff, "Cutoff"),
                                               knob (id::lpfReso, "Resonance"), knob (id::lpfEnvAmt, "Env Amount") } },
                               { "PLAY", { knob (id::glide, "Glide") } },
                               { "FILTER ENVELOPE", { knob (id::fenvAttack, "Attack"), knob (id::fenvDecay, "Decay"),
                                                      knob (id::fenvSustain, "Sustain"), knob (id::fenvRelease, "Release") } },
                               { "AMP ENVELOPE", { knob (id::aenvAttack, "Attack"), knob (id::aenvDecay, "Decay"),
                                                   knob (id::aenvSustain, "Sustain"), knob (id::aenvRelease, "Release") } } },
                             { { 0, 1, 2 }, { 3, 4 }, { 5 }, { 6 } } });

            Page advanced { "Advanced",
                            { { "LFO", id::lfoSync, { "Free", "Tempo Sync" } },
                              { "MODE", id::voiceMode, { "Poly", "Unison", "Mono" } } },
                            { { "OSCILLATOR 1", { knob (id::osc1PW, "Pulse Width"), toggle (id::osc1Sync, "Sync") } },
                              { "OSCILLATOR 2", { knob (id::osc2PW, "Pulse Width"), toggle (id::osc2LowFreq, "Lo Freq"),
                                                  toggle (id::osc2KeyTrack, "Key") } },
                              { "LOW-PASS", { combo (id::lpfModel, "Model", 80), knob (id::lpfDrive, "Drive"),
                                              knob (id::lpfKeyTrack, "Key Track") } },
                              { "HIGH-PASS", { knob (id::hpfCutoff, "Cutoff"), knob (id::hpfReso, "Resonance"),
                                               knob (id::hpfEnvAmt, "Env Amount") } },
                              { "ENVELOPES", { knob (id::fenvVelAmt, "Filter Vel"), toggle (id::fenvSync, "Sync to Amp"),
                                               knob (id::aenvVelAmt, "Amp Vel") } },
                              { "POLY-MOD", { knob (id::pmodFiltEnv, "Filter Env"), knob (id::pmodOsc2, "Osc 2"),
                                              toggle (id::pmodDestFreq1, "Osc 1 Freq"), toggle (id::pmodDestShape1, "Osc 1 Shape"),
                                              toggle (id::pmodDestFilter, "Filter") } },
                              { "LFO", { combo (id::lfoWave, "Wave", 100), knob (id::lfoRate, "Rate"), knob (id::lfoInitial, "Initial"),
                                         knob (id::lfoWheel, "Wheel"), combo (id::lfoDivision, "Division"), knob (id::lfoNoiseMix, "Noise Mix"),
                                         toggle (id::lfoDestFreq1, "Osc 1 Freq", 1), toggle (id::lfoDestFreq2, "Osc 2 Freq", 1),
                                         toggle (id::lfoDestShape1, "Osc 1 Shape", 1), toggle (id::lfoDestShape2, "Osc 2 Shape", 1),
                                         toggle (id::lfoDestLpf, "Low-Pass", 1), toggle (id::lfoDestHpf, "High-Pass", 1),
                                         toggle (id::lfoDestAmp, "Amp", 1) } },
                              { "VOICES", { knob (id::voices, "Voices"), knob (id::unisonVoices, "Unison"),
                                            knob (id::unisonDetune, "Detune") } },
                              { "PERFORMANCE", { knob (id::vintage, "Vintage"), knob (id::bendRange, "Bend Range"),
                                                 knob (id::atFilter, "AT > Filter"), knob (id::atLfo, "AT > LFO") } },
                              { "ECHO", { toggle (id::echoOn, "On", 0), knob (id::echoMix, "Mix"), knob (id::echoTime, "Time"),
                                          toggle (id::echoSync, "Sync", 0), combo (id::echoDivision, "Division", 70),
                                          knob (id::echoFeedback, "Feedback"), toggle (id::echoPingPong, "Ping-Pong", 0) } },
                              { "REVERB", { toggle (id::reverbOn, "On"), knob (id::reverbMix, "Mix"),
                                            knob (id::reverbSize, "Size"), knob (id::reverbTone, "Tone") } } },
                            { { 0, 1, 2, 3 }, { 4, 5, 7, 8 }, { 6 }, { 9, 10 } } };
            advanced.panels[7].voicePresets = true;
            advanced.panels[9].fxPicker = 0;
            advanced.panels[10].fxPicker = 1;
            all.push_back (std::move (advanced));

            return all;
        }
    }

    const std::vector<Page>& pages()
    {
        static const auto all = build();
        return all;
    }

    const std::vector<Page>& editorPages()
    {
        static const auto all = buildEditor();
        return all;
    }
}
