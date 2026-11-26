#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace dooblrchroma::dsp
{
    /** Concert-pitch C4 used as the resonator reference when Pitch is 0.00. */
    inline constexpr float kC4Hz = 261.63f;
    inline constexpr int kMaxModes = 70;
    inline constexpr int kMaxManualPartials = 4;
    inline constexpr int kDensityCounts[4] = { 4, 16, 30, 70 };

    enum class ResType
    {
        string = 0,
        bar,
        marimba,
        plate,
        drumhead,
        membrane,
        openTube,
        closedTube,
        manual
    };

    inline constexpr int kNumResTypes = 9;

    inline const char* resTypeName (ResType t) noexcept
    {
        switch (t)
        {
            case ResType::string:     return "String";
            case ResType::bar:        return "Bar";
            case ResType::marimba:    return "Marimba";
            case ResType::plate:      return "Plate";
            case ResType::drumhead:   return "Drumhead";
            case ResType::membrane:   return "Membrane";
            case ResType::openTube:   return "Open Tube";
            case ResType::closedTube: return "Closed Tube";
            case ResType::manual:     return "Manual";
        }
        return "String";
    }

    inline bool isTube (ResType t) noexcept
    {
        return t == ResType::openTube || t == ResType::closedTube;
    }

    /** How many modes to compute for this type and density index (0..3). */
    int modeCount (ResType type, int densityIndex, float fundamentalHz, float sampleRate) noexcept;

    /** Fill `ratios[0..n)` with f_k / f_0 for the type. `manualRanks` is four 1-based ranks. */
    int fillRatios (ResType type, int n, float* ratios, const float* manualRanks) noexcept;

    /** Relative mode amplitude from a hit (or coupling) position in [0, 1]:
        0 = border / edge, 1 = centre. */
    float hitAmplitude (ResType type, int modeIndex, float ratio, float position) noexcept;
}
