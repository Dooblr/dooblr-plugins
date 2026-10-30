#include "ControlRules.h"

#include <cmath>

#include "Parameters.h"
#include "UI/Layout.h"

namespace dooblrprophet::rules
{
    juce::StringArray dimmedControls (const ValueOf& valueOf)
    {
        namespace id = params::id;
        juce::StringArray dimmed;
        const auto dimIf = [&dimmed] (const char* control, bool ignored) { if (ignored) dimmed.add (control); };

        // Variable mode uses the Shape knob; Switched mode uses the wave switches. Pulse Width acts in both.
        const bool osc1Switched = valueOf (id::osc1Mode) > 0.5f;
        const bool osc2Switched = valueOf (id::osc2Mode) > 0.5f;
        dimIf (id::osc1Shape, osc1Switched);
        dimIf (id::osc2Shape, osc2Switched);
        for (auto* pid : { id::osc1Saw, id::osc1Tri, id::osc1Pulse })
            dimIf (pid, ! osc1Switched);
        for (auto* pid : { id::osc2Saw, id::osc2Tri, id::osc2Pulse })
            dimIf (pid, ! osc2Switched);

        // Poly-Mod destinations only matter when a Poly-Mod source is turned up.
        const bool polyModOn = std::abs (valueOf (id::pmodFiltEnv)) > 1.0e-4f || valueOf (id::pmodOsc2) > 1.0e-4f;
        for (auto* pid : { id::pmodDestFreq1, id::pmodDestShape1, id::pmodDestFilter })
            dimIf (pid, ! polyModOn);

        // Filter env synced to the amp envelope: its own ADSR is kept but not used.
        const bool envSynced = valueOf (id::fenvSync) > 0.5f;
        for (auto* pid : { id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease })
            dimIf (pid, envSynced);

        const bool lfoSynced = valueOf (id::lfoSync) > 0.5f;
        dimIf (id::lfoRate, lfoSynced);
        dimIf (id::lfoDivision, ! lfoSynced);

        // Voices sets the poly voice count; Unison and Detune only act in Unison mode; Mono uses one voice.
        const auto mode = juce::roundToInt (valueOf (id::voiceMode));
        dimIf (id::voices, mode != 0);
        dimIf (id::unisonVoices, mode != 1);
        dimIf (id::unisonDetune, mode != 1);
        dimIf (layout::voicePresetsId, mode != 0);

        // FX: a module's controls are dimmed while it is off; Time or Division by the Sync switch.
        const bool echoOn = valueOf (id::echoOn) > 0.5f, reverbOn = valueOf (id::reverbOn) > 0.5f;
        const bool echoSync = valueOf (id::echoSync) > 0.5f;
        for (auto* pid : { id::echoMix, id::echoFeedback, id::echoSync, id::echoPingPong })
            dimIf (pid, ! echoOn);
        dimIf (id::echoTime, ! (echoOn && ! echoSync));
        dimIf (id::echoDivision, ! (echoOn && echoSync));
        for (auto* pid : { id::reverbMix, id::reverbSize, id::reverbTone })
            dimIf (pid, ! reverbOn);
        dimIf (layout::echoPresetPickerId, ! echoOn);
        dimIf (layout::reverbPresetPickerId, ! reverbOn);

        return dimmed;
    }

    juce::String pageStatus (int page, const dsp::Settings& s, int activeVoices)
    {
        switch (page)
        {
            case 0:
            {
                const bool fm = s.pmodFreq1 && (std::abs (s.pmodFiltEnv) > 1.0e-4f || s.pmodOsc2 > 1.0e-4f);
                return fm ? "Poly-Mod FM active: 4x oversampled" : "2x oversampled";
            }
            case 1: return juce::String ("High-pass, then ") + (s.lpfModel == 1 ? "2040-style ladder" : "3320-style ladder");
            case 4: return juce::String (activeVoices) + " sounding";
            default: return {};
        }
    }
}
