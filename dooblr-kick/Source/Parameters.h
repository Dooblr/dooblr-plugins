#pragma once

#include <algorithm>
#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/KickSettings.h"

namespace dooblrkick::params
{
    inline constexpr int numLayers = dsp::numLayers;
    inline constexpr int maxNodes = dsp::maxEnvelopeNodes;

    inline constexpr std::array<const char*, numLayers> layerPrefixes { "click", "body", "sub", "tail", "noise" };
    inline constexpr std::array<const char*, numLayers> layerNames    { "Click", "Body", "Sub", "Tail", "Noise" };

    /** Layers with a Synth / Sample source switch (every layer except Sub). */
    inline constexpr bool layerHasSample (int layer) noexcept { return layer != dsp::subLayer; }

    /** Factory samples (CC0, embedded; see Resources/Samples/LICENSES.md), in choice order.
        The last choice, "User file", plays the WAV / AIFF loaded for that layer. */
    inline constexpr int numFactorySamples = 21;
    inline constexpr int userSampleChoice = numFactorySamples;
    inline constexpr std::array<const char*, numFactorySamples> factorySampleNames {
        "Kit Kick Close", "Kit Kick Mid", "Concert BD Muted", "Concert BD Open", "Concert BD Boom",
        "Cajon Bass", "Frame Drum Mute", "Slapstick", "Woodblock", "Hand Clap", "Shaker Hit",
        // Added in 0.1.1 (indices 11 .. 20; the first 11 keep their indices).
        "Synth Kick Punch", "Synth Kick Sub", "Synth Tom Low", "Synth Click", "Synth Clap",
        "Low Tom Mallet", "Timpani Low", "Brake Drum Hit", "Cabasa Hit", "Darbuka Doum" };

    /** The "User file" choice index used by state versions 1 and 2 (before the 0.1.1 samples). */
    inline constexpr int legacyUserSampleChoice = 11;
    juce::StringArray sampleChoices();

    /** e.g. layerId (1, "level") == "body_level". */
    juce::String layerId (int layer, const char* suffix);

    enum class Envelope { pitch, amp, master };

    /** Envelope node parameters: penv0_freq, penv3_time, aenv2_level, aenv1_curve, menv1_level ...
        (penv / aenv: body pitch / amp; menv: the master envelope on the Envelope page, 0..1). */
    juce::String envelopeCountId (Envelope envelope);
    juce::String nodeTimeId (Envelope envelope, int node);   // nodes 1 .. 7 (node 0 is fixed at 0 ms)
    juce::String nodeValueId (Envelope envelope, int node);  // nodes 0 .. 7 (pitch: Hz, amp: 0..1)
    juce::String nodeCurveId (Envelope envelope, int node);  // nodes 0 .. 6 (segment to the next node)

    inline constexpr float minPitchHz = 20.0f, maxPitchHz = 16000.0f, maxNodeTimeMs = 2000.0f;
    /** v0.3.3: master envelope (Envelope tab) points reach 6 s; the Body pitch / amp ones stay at 2 s. */
    inline constexpr float maxMasterNodeTimeMs = 6000.0f;
    constexpr float maxNodeTimeFor (Envelope e) noexcept { return e == Envelope::master ? maxMasterNodeTimeMs : maxNodeTimeMs; }

    /** v0.3.3 master envelope macros: "Master Env Time Scale" (menv_tscale, 25 % .. 400 %) stretches
        every point's time (capped at 6000 ms) and "Master Env Depth" (menv_depth, 0 .. 100 %) blends
        the envelope towards unity: gain = 1 - depth * (1 - level). At 100 % / 100 % (the defaults)
        a point is returned unchanged, bit for bit. */
    inline constexpr float minMasterTimeScale = 0.25f, maxMasterTimeScale = 4.0f;

    inline dsp::EnvelopeNode applyMasterMacros (dsp::EnvelopeNode node, float timeScale, float depth) noexcept
    {
        if (timeScale != 1.0f)
            node.timeMs = std::min (node.timeMs * timeScale, maxMasterNodeTimeMs);

        if (depth < 1.0f)
            node.value = 1.0f - std::max (0.0f, depth) * (1.0f - node.value);

        return node;
    }

    /** Chain order choices ("Drive > EQ > Dynamics > Limiter" ...), index = dsp chain order index. */
    juce::StringArray chainOrderChoices();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** "F1", "C#3": note names with middle C (60) = C3. */
    juce::String noteName (int midiNote);

    /** v0.3.5: the layer Level taper, like a console fader. Piecewise linear in dB through
        (travel, dB): (0, -inf = -60), (0.10, -48), (0.25, -24), (0.78, 0), (1, +12), so the top
        75 % of travel covers -24 .. +12 dB and 0 dB sits at 78 %. The parameter's
        NormalisableRange uses it, so the host's automation has the same feel. */
    constexpr float minLevelDb = -60.0f, maxLevelDb = 12.0f;
    float levelDbToTravel (float db) noexcept;
    float travelToLevelDb (float travel) noexcept;
    juce::NormalisableRange<float> levelRange();

    /** Formats a frequency as "48.0 Hz" / "1.25 kHz". */
    juce::String formatHz (float hz);

    /** Sample-layer tune text: the nearest semitone plus signed cents, e.g. "-12 st -25 ct". */
    juce::String formatTune (float semitones);

    /** Parses formatTune() text (or a plain number of semitones); clamps to the tune range. */
    float parseTune (const juce::String& text);

    /** Reads every parameter into a dsp::KickSettings (allocation free; any thread). */
    class SettingsReader
    {
    public:
        explicit SettingsReader (juce::AudioProcessorValueTreeState& state);
        dsp::KickSettings read() const noexcept;

    private:
        using Value = std::atomic<float>*;

        struct Common { Value enabled, solo, level, pan, start, send, filterType, cutoff, resonance, source, sample; };

        Value get (const juce::String& id) const;
        Common getCommon (int layer) const;

        juce::AudioProcessorValueTreeState& state;
        std::array<Common, numLayers> common;

        Value clickType, clickPitch, clickLength, clickTone, clickKeyTrack;
        Value bodyWave, bodyHarmonics, bodyTune, bodyPhase, bodyDrive, bodyKeyTrack, bodySampleEnv;
        Value pitchCount, ampCount;
        std::array<Value, maxNodes> pitchTime {}, pitchFreq {}, pitchCurve {}, ampTime {}, ampLevel {}, ampCurve {};
        Value subKeyTrack, noiseKeyTrack;
        Value subTune, subFine, subPhase, subEnvAmount, subAttack, subHold, subDecay, subCurve;
        Value tailWave, tailTune, tailSpread, tailPhase, tailAttack, tailHold, tailDecay, tailCurve, tailDrive, tailKeyTrack;
        Value noiseColour, noiseWidth, noiseAttack, noiseHold, noiseDecay, noiseCurve, noiseTune;
        Value keyNote, velocitySensitivity, gate, release;
        Value distOn, distType, distDrive, distTone, distBits, distRate, distOut, distMix;
        Value eqOn, eqLowFreq, eqLowGain, eqMidFreq, eqMidGain, eqMidQ, eqHighFreq, eqHighGain;
        Value shapeOn, shapeAttack, shapeSustain;
        Value compOn, compThreshold, compRatio, compAttack, compRelease, compMakeup;
        Value limMode, limCeiling, limRelease, outputGain;
        Value masterCount, masterOn, dryWet, chainOrder, masterTimeScale, masterDepth;
        std::array<Value, maxNodes> masterTime {}, masterLevel {}, masterCurve {};
    };
}
