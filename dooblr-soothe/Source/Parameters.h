#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrsoothe::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    inline constexpr int numBands = 8;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Detection
        inline constexpr const char* mode        = "mode";        // Soft | Hard
        inline constexpr const char* depth       = "depth";
        inline constexpr const char* sharpness   = "sharpness";
        inline constexpr const char* selectivity = "selectivity";
        inline constexpr const char* attack      = "attack";
        inline constexpr const char* release     = "release";

        // Stereo
        inline constexpr const char* stereoMode  = "stereoMode";  // L/R | M/S
        inline constexpr const char* link        = "link";
        inline constexpr const char* focus       = "focus";

        // Output
        inline constexpr const char* maxCut      = "maxCut";
        inline constexpr const char* wetTrim     = "wetTrim";
        inline constexpr const char* mix         = "mix";
        inline constexpr const char* output      = "output";

        // Monitoring / engine
        inline constexpr const char* delta       = "delta";
        inline constexpr const char* bypass      = "bypass";
        inline constexpr const char* sidechain   = "sidechain";
        inline constexpr const char* quality     = "quality";     // Standard | High | Ultra
        inline constexpr const char* listenBand  = "listenBand";  // Off, Band 1 .. Band 8
    }

    /** Depth-curve band parameter IDs: "b1Active", "b1Shape", "b1Freq", "b1Gain", "b1Q", ... */
    juce::String bandId (int band, const char* field);

    /** Value text with a fixed number of decimals: never exponent notation, never "-0". */
    juce::String formatValue (double value, int decimals);
    inline juce::String bandActive (int band) { return bandId (band, "Active"); }
    inline juce::String bandShape (int band)  { return bandId (band, "Shape"); }
    inline juce::String bandFreq (int band)   { return bandId (band, "Freq"); }
    inline juce::String bandGain (int band)   { return bandId (band, "Gain"); }
    inline juce::String bandQ (int band)      { return bandId (band, "Q"); }

    enum class Shape { bell = 0, lowShelf, highShelf, lowCut, highCut, bandPass };
    inline constexpr int numShapes = 6;
    juce::StringArray shapeNames();
    /** Shapes whose Gain matters (cuts and band pass only gate where reduction happens). */
    inline bool shapeUsesGain (Shape s) noexcept { return s == Shape::bell || s == Shape::lowShelf || s == Shape::highShelf || s == Shape::bandPass; }

    juce::StringArray qualityNames();

    /** The parameter layout, in the order hosts list it. */
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Default band layout (0-based index): returned so tests and the reset path agree. */
    struct BandDefault { bool active; Shape shape; float freq, gain, q; };
    BandDefault bandDefault (int band);

    inline constexpr float minFreq = 20.0f, maxFreq = 20000.0f, minGain = -18.0f, maxGain = 18.0f, minQ = 0.1f, maxQ = 10.0f;
}
