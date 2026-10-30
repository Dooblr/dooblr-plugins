#include "Layout.h"

#include "../Parameters.h"

namespace dooblrprophet::layout
{
    namespace
    {
        namespace id = params::id;
        using Kind = ui::ControlPanel::Kind;

        Control knob (const char* pid, const char* caption) { return { pid, caption, Kind::knob }; }
        Control toggle (const char* pid, const char* caption) { return { pid, caption, Kind::toggle }; }
        Control combo (const char* pid, const char* caption) { return { pid, caption, Kind::combo }; }

        std::vector<Page> build()
        {
            std::vector<Page> all;

            all.push_back ({ "Oscillators",
                             { { "OSC 1", id::osc1Mode, { "Variable", "Switched" } }, { "OSC 2", id::osc2Mode, { "Variable", "Switched" } } },
                             { { "OSCILLATOR 1", { knob (id::osc1Coarse, "Coarse"), knob (id::osc1Fine, "Fine"), knob (id::osc1Shape, "Shape"),
                                                   knob (id::osc1PW, "Pulse Width"), toggle (id::osc1Saw, "Saw"), toggle (id::osc1Tri, "Triangle"),
                                                   toggle (id::osc1Pulse, "Pulse"), toggle (id::osc1Sync, "Sync") } },
                               { "OSCILLATOR 2", { knob (id::osc2Coarse, "Coarse"), knob (id::osc2Fine, "Fine"), knob (id::osc2Shape, "Shape"),
                                                   knob (id::osc2PW, "Pulse Width"), toggle (id::osc2Saw, "Saw"), toggle (id::osc2Tri, "Triangle"),
                                                   toggle (id::osc2Pulse, "Pulse"), toggle (id::osc2LowFreq, "Low Freq"),
                                                   toggle (id::osc2KeyTrack, "Key Track") } },
                               { "MIXER", { knob (id::mixOsc1, "Osc 1"), knob (id::mixOsc2, "Osc 2"), knob (id::mixSub, "Sub"), knob (id::mixNoise, "Noise") } } },
                             { { 0 }, { 1 }, { 2 } } });

            all.push_back ({ "Filters",
                             { { "LOW-PASS", id::lpfModel, params::lpfModelNames() } },
                             { { "HIGH-PASS (2-POLE)", { knob (id::hpfCutoff, "Cutoff"), knob (id::hpfReso, "Resonance"), knob (id::hpfEnvAmt, "Env Amount") } },
                               { "LOW-PASS (4-POLE LADDER)", { knob (id::lpfCutoff, "Cutoff"), knob (id::lpfReso, "Resonance"), knob (id::lpfDrive, "Drive"),
                                                               knob (id::lpfEnvAmt, "Env Amount"), knob (id::lpfKeyTrack, "Key Track") } } },
                             { { 0, 1 } } });

            all.push_back ({ "Envelopes",
                             {},
                             { { "FILTER ENVELOPE", { knob (id::fenvAttack, "Attack"), knob (id::fenvDecay, "Decay"), knob (id::fenvSustain, "Sustain"),
                                                      knob (id::fenvRelease, "Release"), knob (id::fenvVelAmt, "Velocity"), toggle (id::fenvSync, "Sync to Amp") } },
                               { "AMP ENVELOPE", { knob (id::aenvAttack, "Attack"), knob (id::aenvDecay, "Decay"), knob (id::aenvSustain, "Sustain"),
                                                   knob (id::aenvRelease, "Release"), knob (id::aenvVelAmt, "Velocity") } } },
                             { { 0 }, { 1 } } });

            all.push_back ({ "Modulation",
                             { { "LFO", id::lfoSync, { "Free", "Tempo Sync" } } },
                             { { "POLY-MOD", { knob (id::pmodFiltEnv, "Filter Env"), knob (id::pmodOsc2, "Osc 2"), toggle (id::pmodDestFreq1, "Osc 1 Freq"),
                                               toggle (id::pmodDestShape1, "Osc 1 Shape"), toggle (id::pmodDestFilter, "Filter") } },
                               { "LFO", { combo (id::lfoWave, "Wave"), knob (id::lfoRate, "Rate"), combo (id::lfoDivision, "Division"),
                                          knob (id::lfoInitial, "Initial"), knob (id::lfoWheel, "Wheel"), knob (id::lfoNoiseMix, "Noise Mix") } },
                               { "LFO DESTINATIONS", { toggle (id::lfoDestFreq1, "Osc 1 Freq"), toggle (id::lfoDestFreq2, "Osc 2 Freq"),
                                                       toggle (id::lfoDestShape1, "Osc 1 Shape"), toggle (id::lfoDestShape2, "Osc 2 Shape"),
                                                       toggle (id::lfoDestLpf, "Low-Pass"), toggle (id::lfoDestHpf, "High-Pass"), toggle (id::lfoDestAmp, "Amp") } } },
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
    }

    const std::vector<Page>& pages()
    {
        static const auto all = build();
        return all;
    }
}
