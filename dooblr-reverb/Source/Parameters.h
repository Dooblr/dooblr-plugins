#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrreverb::params
{
    /** State-tree version written with every saved state (bump when a migration is needed).
        1: 0.1.0. */
    inline constexpr int stateVersion = 1;

    inline constexpr int numDecayBands = 6, numEqBands = 6;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Main knobs
        inline constexpr const char* space       = "space";
        inline constexpr const char* decayRate   = "decayRate";
        inline constexpr const char* brightness  = "brightness";
        inline constexpr const char* character   = "character";
        inline constexpr const char* distance    = "distance";
        inline constexpr const char* width       = "width";
        inline constexpr const char* mix         = "mix";

        // Bottom bar
        inline constexpr const char* predelay     = "predelay";
        inline constexpr const char* predelaySync = "predelaySync";
        inline constexpr const char* predelayDiv  = "predelayDiv";
        inline constexpr const char* input        = "input";
        inline constexpr const char* output       = "output";
        inline constexpr const char* bypass       = "bypass";
    }

    /** Decay Rate EQ band IDs: "d1Active", "d1Shape", "d1Freq", "d1Rate", "d1Q", ... */
    juce::String decayId (int band, const char* field);
    inline juce::String decayActive (int b) { return decayId (b, "Active"); }
    inline juce::String decayShape (int b)  { return decayId (b, "Shape"); }
    inline juce::String decayFreq (int b)   { return decayId (b, "Freq"); }
    inline juce::String decayRateId (int b) { return decayId (b, "Rate"); }
    inline juce::String decayQ (int b)      { return decayId (b, "Q"); }

    /** Post EQ band IDs: "e1Active", "e1Shape", "e1Freq", "e1Gain", "e1Q", "e1Slope", ... */
    juce::String eqId (int band, const char* field);
    inline juce::String eqActive (int b) { return eqId (b, "Active"); }
    inline juce::String eqShape (int b)  { return eqId (b, "Shape"); }
    inline juce::String eqFreq (int b)   { return eqId (b, "Freq"); }
    inline juce::String eqGain (int b)   { return eqId (b, "Gain"); }
    inline juce::String eqQ (int b)      { return eqId (b, "Q"); }
    inline juce::String eqSlope (int b)  { return eqId (b, "Slope"); }

    enum class DecayShape { bell = 0, lowShelf, highShelf, notch };
    inline constexpr int numDecayShapes = 4;
    juce::StringArray decayShapeNames();
    inline bool decayShapeUsesRate (DecayShape s) noexcept { return s != DecayShape::notch; }

    enum class EqShape { bell = 0, lowShelf, highShelf, lowCut, highCut };
    inline constexpr int numEqShapes = 5;
    juce::StringArray eqShapeNames();
    inline bool eqShapeUsesGain (EqShape s) noexcept { return s == EqShape::bell || s == EqShape::lowShelf || s == EqShape::highShelf; }
    inline bool eqShapeIsCut (EqShape s) noexcept { return s == EqShape::lowCut || s == EqShape::highCut; }
    juce::StringArray slopeNames(); // 12 / 24 / 48 dB/oct
    inline int slopeSections (int slopeIndex) noexcept { return slopeIndex <= 0 ? 1 : (slopeIndex == 1 ? 2 : 4); }

    /** Predelay sync divisions (beats of a quarter note = 1.0). */
    juce::StringArray divisionNames();
    double divisionBeats (int index) noexcept;

    /** The Space knob (0..100 %) as the room's nominal decay time, 0.2 s .. 10 s (log). */
    double spaceToSeconds (double spacePercent) noexcept;

    /** Value text with a fixed number of decimals: never exponent notation, never "-0". */
    juce::String formatValue (double value, int decimals);
    juce::String formatFrequency (double hz);
    juce::String formatSeconds (double seconds);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct DecayDefault { bool active; DecayShape shape; float freq, rate, q; };
    DecayDefault decayDefault (int band);
    struct EqDefault { bool active; EqShape shape; float freq, gain, q; int slope; };
    EqDefault eqDefault (int band);

    inline constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    inline constexpr float minRate = 12.5f, maxRate = 400.0f;   // decay band rate, % of the decay time
    inline constexpr float minGain = -24.0f, maxGain = 24.0f;   // post EQ gain, dB
    inline constexpr float minQ = 0.1f, maxQ = 10.0f;
    inline constexpr float maxPredelayMs = 500.0f;
}
