#include "Parameters.h"

namespace dooblrchroma::params
{
    namespace
    {
        void addBool (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                      const char* identifier, const juce::String& name, bool defaultOn)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (pid (identifier), name, defaultOn));
        }

        juce::String formatHz (float v, int)
        {
            if (v >= 1000.0f)
                return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
            return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        }

        juce::String formatMs (float v, int)
        {
            if (v < 1.0f)
                return juce::String (v * 1000.0f, 0) + " ms";
            return juce::String (v, v < 10.0f ? 2 : 1) + " s";
        }

        juce::String formatDb (float v, int) { return juce::String (v, 1) + " dB"; }
        juce::String formatPct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String formatCents (float v, int)
        {
            return (v >= 0.0f ? "+" : "") + juce::String (v, 0) + " ct";
        }

        juce::String formatQ (float v, int)
        {
            return juce::String (v, 2);
        }

        juce::String formatHzRate (float v, int)
        {
            return juce::String (v, v < 1.0f ? 2 : 1) + " Hz";
        }

        juce::String formatPartial (float v, int)
        {
            return juce::String (v, 2);
        }

        juce::String formatMaterial (float v, int)
        {
            return juce::String (v, 2);
        }

        juce::String formatSignedSt (float v, int)
        {
            if (v > -0.05f && v < 0.05f)
                return "0 st";
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
        }

        juce::ParameterID pidStr (const juce::String& identifier)
        {
            return { identifier, 1 };
        }

        void addResonator (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                           const juce::String& prefix, const juce::String& label, bool defaultOn,
                           int defaultType)
        {
            const auto idOf = [&prefix] (const char* suffix) { return prefix + suffix; };

            layout.add (std::make_unique<juce::AudioParameterBool> (pidStr (idOf ("_on")), label + " On", defaultOn));
            layout.add (std::make_unique<juce::AudioParameterChoice> (pidStr (idOf ("_type")),
                                                                      label + " Type", resTypeNames(), defaultType));
            layout.add (std::make_unique<juce::AudioParameterChoice> (pidStr (idOf ("_density")),
                                                                      label + " Density", densityNames(), 1)); // 16
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_pitch")),
                                                                     label + " Pitch",
                                                                     juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                                     Attributes().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                                                                     { return (v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_cents")),
                                                                     label + " Cents",
                                                                     juce::NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
                                                                     Attributes().withStringFromValueFunction (formatCents)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_key")),
                                                                     label + " Key",
                                                                     juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
                                                                     Attributes().withStringFromValueFunction ([] (float v, int)
                                                                     { return juce::String (v, 2) + ":1"; })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_pLevel")),
                                                                     label + " P.Level",
                                                                     juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                                     Attributes().withLabel ("st").withStringFromValueFunction (formatSignedSt)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_pRate")),
                                                                     label + " P.Rate",
                                                                     rangeWithCentre (0.001f, 2.0f, 0.08f), 0.05f,
                                                                     Attributes().withStringFromValueFunction (formatMs)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_pVel")),
                                                                     label + " P.Vel",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_decay")),
                                                                     label + " Decay",
                                                                     rangeWithCentre (0.05f, 12.0f, 1.5f), 1.4f,
                                                                     Attributes().withStringFromValueFunction (formatMs)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_decayKey")),
                                                                     label + " Decay Key",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_decayVel")),
                                                                     label + " Decay Vel",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_release")),
                                                                     label + " Release",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f,
                                                                     Attributes().withStringFromValueFunction (formatPct)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_material")),
                                                                     label + " Material",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -0.35f,
                                                                     Attributes().withStringFromValueFunction (formatMaterial)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_tone")),
                                                                     label + " Tone",
                                                                     juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), -3.0f,
                                                                     Attributes().withLabel ("dB/oct").withStringFromValueFunction ([] (float v, int)
                                                                     { return juce::String (v, 1) + " dB/oct"; })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_lowCut")),
                                                                     label + " Low Cut",
                                                                     juce::NormalisableRange<float> (0.0f, 16.0f, 1.0f), 0.0f,
                                                                     Attributes().withStringFromValueFunction ([] (float v, int)
                                                                     { return juce::roundToInt (v) == 0 ? juce::String ("Off")
                                                                                                       : juce::String ("H") + juce::String (juce::roundToInt (v)); })));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_radius")),
                                                                     label + " Radius",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f,
                                                                     Attributes().withStringFromValueFunction (formatPct)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_hit")),
                                                                     label + " Hit",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.22f,
                                                                     Attributes().withStringFromValueFunction (formatPct)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_hitVel")),
                                                                     label + " Hit Vel",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_hitKey")),
                                                                     label + " Hit Key",
                                                                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf ("_hitRnd")),
                                                                     label + " Hit Rnd",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                     Attributes().withStringFromValueFunction (formatPct)));
            for (int i = 1; i <= 4; ++i)
            {
                const auto suffix = "_part" + juce::String (i);
                layout.add (std::make_unique<juce::AudioParameterFloat> (pidStr (idOf (suffix.toRawUTF8())),
                                                                         label + " Partial " + juce::String (i),
                                                                         juce::NormalisableRange<float> (1.0f, 32.0f, 0.01f), (float) i,
                                                                         Attributes().withStringFromValueFunction (formatPartial)));
            }
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::malletStiffness), "Stiffness",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::malletNoise), "Mallet Noise",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.12f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::malletColor), "Color",
                                                                 rangeWithCentre (80.0f, 8000.0f, 1200.0f), 1800.0f,
                                                                 Attributes().withStringFromValueFunction (formatHz)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::mixMallet), "Mallet",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.85f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::mixNoise), "Noise Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::mixDirect), "Direct",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::noiseFilter), "Noise Filter", noiseFilterNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::noiseFreq), "Noise Freq",
                                                                 rangeWithCentre (40.0f, 12000.0f, 1500.0f), 1800.0f,
                                                                 Attributes().withStringFromValueFunction (formatHz)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::noiseQ), "Noise Q",
                                                                 rangeWithCentre (0.3f, 12.0f, 1.0f), 0.8f,
                                                                 Attributes().withStringFromValueFunction (formatQ)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::envMode), "Env Mode", envModeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::envAttack), "Attack",
                                                                 rangeWithCentre (0.001f, 4.0f, 0.05f), 0.005f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::envHold), "Hold",
                                                                 rangeWithCentre (0.0f, 4.0f, 0.1f), 0.08f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::envDecay), "Env Decay",
                                                                 rangeWithCentre (0.01f, 8.0f, 0.3f), 0.25f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::envSustain), "Sustain",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::envRelease), "Env Release",
                                                                 rangeWithCentre (0.01f, 8.0f, 0.2f), 0.15f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoRate), "LFO Rate",
                                                                 rangeWithCentre (0.05f, 30.0f, 3.0f), 4.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHzRate)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::lfoDepth), "LFO Depth",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::unisonCount), "Unison", unisonNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::unisonDetune), "Detune",
                                                                 juce::NormalisableRange<float> (0.0f, 50.0f, 0.1f), 8.0f,
                                                                 Attributes().withStringFromValueFunction (formatCents)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::vibratoRate), "Vibrato Rate",
                                                                 rangeWithCentre (0.1f, 12.0f, 5.0f), 5.0f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHzRate)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::vibratoDepth), "Vibrato Depth",
                                                                 juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatCents)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::glide), "Glide",
                                                                 rangeWithCentre (0.0f, 1.5f, 0.08f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::macro1), "Macro 1",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::macro2), "Macro 2",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::macro3), "Macro 3",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::macro4), "Macro 4",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::layerMode), "Layers", layerModeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::splitNote), "Split Note",
                                                                 juce::NormalisableRange<float> (0.0f, 127.0f, 1.0f), 60.0f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 4); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::layer1Level), "Layer 1 Level",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::layer2Level), "Layer 2 Level",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::layer2Transpose), "Layer 2 Transpose",
                                                                 juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                                 Attributes().withLabel ("st")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::output), "Output",
                                                                 juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatDb)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::eqLow), "EQ Low",
                                                                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::eqMid), "EQ Mid",
                                                                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::eqHigh), "EQ High",
                                                                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::eqMidFreq), "EQ Mid Freq",
                                                                 rangeWithCentre (200.0f, 5000.0f, 1000.0f), 1000.0f,
                                                                 Attributes().withStringFromValueFunction (formatHz)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::compThreshold), "Comp Thresh",
                                                                 juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::compRatio), "Comp Ratio",
                                                                 rangeWithCentre (1.0f, 20.0f, 4.0f), 4.0f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 { return juce::String (v, 1) + ":1"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::compAttack), "Comp Attack",
                                                                 rangeWithCentre (0.001f, 0.1f, 0.01f), 0.01f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::compRelease), "Comp Release",
                                                                 rangeWithCentre (0.02f, 1.0f, 0.15f), 0.12f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::compMix), "Comp Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::distDrive), "Drive",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::distMix), "Dist Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::chorusRate), "Chorus Rate",
                                                                 rangeWithCentre (0.05f, 8.0f, 0.8f), 0.8f,
                                                                 Attributes().withLabel ("Hz").withStringFromValueFunction (formatHzRate)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::chorusDepth), "Chorus Depth",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::chorusMix), "Chorus Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::delayTime), "Delay Time",
                                                                 rangeWithCentre (0.01f, 1.5f, 0.3f), 0.35f,
                                                                 Attributes().withStringFromValueFunction (formatMs)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::delayFeedback), "Delay Feedback",
                                                                 juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f), 0.35f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::delayMix), "Delay Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::reverbSize), "Reverb Size",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::reverbDamp), "Reverb Damp",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::reverbMix), "Reverb Mix",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                                 Attributes().withStringFromValueFunction (formatPct)));

        addBool (layout, id::l1_couple, "L1 Coupling", false);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::l1_balance), "L1 Balance",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 {
                                                                     if (std::abs (v - 0.5f) < 0.01f) return juce::String ("A=B");
                                                                     return v < 0.5f ? juce::String ("A ") + juce::String ((0.5f - v) * 200.0f, 0) + " %"
                                                                                     : juce::String ("B ") + juce::String ((v - 0.5f) * 200.0f, 0) + " %";
                                                                 })));
        addBool (layout, id::l2_couple, "L2 Coupling", false);
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::l2_balance), "L2 Balance",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int)
                                                                 {
                                                                     if (std::abs (v - 0.5f) < 0.01f) return juce::String ("A=B");
                                                                     return v < 0.5f ? juce::String ("A ") + juce::String ((0.5f - v) * 200.0f, 0) + " %"
                                                                                     : juce::String ("B ") + juce::String ((v - 0.5f) * 200.0f, 0) + " %";
                                                                 })));

        // L1 A on (string), L1 B off (plate). L2 A on (bar) but layer2Level defaults to 0. L2 B off.
        addResonator (layout, "l1_a", "L1 A", true,  0);
        addResonator (layout, "l1_b", "L1 B", false, 3);
        addResonator (layout, "l2_a", "L2 A", true,  1);
        addResonator (layout, "l2_b", "L2 B", false, 4);

        return layout;
    }
}
