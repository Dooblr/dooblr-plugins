#pragma once

#include <array>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include "SvfFilter.h"

/**
    Per-band analogue models (0.8.0).

    These are original behavioural models built from publicly documented characteristics
    of classic console and outboard equalisers (published frequency positions, Q
    behaviour, filter slopes and the general nature of their transformer / amplifier
    colour). They are not circuit simulations and do not claim to match any hardware
    unit. Product names are trademarks of their respective owners and are used only to
    describe the style being modelled.

    Each model has two parts:
      - a linear part: the band's filter shape (Q law, shelf resonance, cut slopes,
        the Pultec boost/atten network), expressed as a cascade of analogue sections so it
        runs in the same SVF cascade (Zero Latency) and is sampled by the FIR designer
        (Natural / Linear Phase) exactly like the Digital model;
      - a nonlinear colour stage (ColourStage): a level-dependent soft saturation with a
        little even-order content and optional low-frequency emphasis, oversampled 4x.
*/
namespace dooblrq::dsp
{
    enum class Model
    {
        digital = 0,
        neve,
        sslE,
        sslG,
        api,
        pultec,
        harrison,
        trident,
        helios,
        emi   // appended last: sessions store the choice index, so the order is fixed
    };

    inline constexpr int numModels = 10;

    constexpr bool isAnalog (Model m) noexcept { return m != Model::digital; }

    /** The per-band Slope applies to Digital only; analogue models fix their own slopes. */
    constexpr bool usesSlope (Model m) noexcept { return m == Model::digital; }

    /** The Pultec Atten control exists for its low and high shelves only. */
    constexpr bool usesAtten (Model m, FilterType t) noexcept
    {
        return m == Model::pultec && (t == FilterType::lowShelf || t == FilterType::highShelf);
    }

    /** Linear shape of a band under a model. Digital = makeBandPrototype (type, slope, ...). */
    BandPrototype makeModelPrototype (Model model, FilterType type, Slope slope, double q,
                                      double gainDb, double attenDb) noexcept;

    /** Digital sections for a band under a model. Digital calls the 0.7.1 code path unchanged. */
    void makeModelCoefficients (Model model, FilterType type, Slope slope, double frequency, double q,
                                double gainDb, double attenDb, double sampleRate, BandCoefficients& destination) noexcept;

    /** Effective bell Q divided by the band's Q (for the editor's Width handles). */
    double modelBellQFactor (Model model, FilterType type, double gainDb) noexcept;

    //==============================================================================
    /** Switched positions of a model (frequency in Hz, gain in dB), ascending. */
    struct StepTable
    {
        const float* values = nullptr;
        int size = 0;

        bool empty() const noexcept { return size <= 0; }
    };

    StepTable frequencySteps (Model model, FilterType type) noexcept;
    StepTable gainSteps (Model model, FilterType type) noexcept;

    /** True if Stepped does anything for this model and type. */
    inline bool hasSteps (Model model, FilterType type) noexcept
    {
        return ! frequencySteps (model, type).empty() || ! gainSteps (model, type).empty();
    }

    /** Nearest position (log distance for frequency, linear for gain); input unchanged if no table. */
    float snapFrequency (Model model, FilterType type, float frequency) noexcept;
    float snapGain (Model model, FilterType type, float gainDb) noexcept;

    const char* modelName (Model model) noexcept;

    //==============================================================================
    /** Colour (saturation) character of a model. 0 dBFS full scale = 1.0. */
    struct ColourCharacter
    {
        float headroom = 4.0f;    // soft-clip level: lower = earlier, stronger saturation
        float even = 0.0f;        // amount of even-order (asymmetric) content
        float lowEmphasis = 0.0f; // extra drive below ~120 Hz (transformer core behaviour)
        float amount = 0.0f;      // 0 = no colour (Digital)
    };

    ColourCharacter colourFor (Model model) noexcept;

    /**
        Residual saturation stage: y = x + amount * (shape (x) - x), where only the residual
        (shape (x) - x) is computed at 4x the sample rate (polyphase IIR half-band
        oversampling) and then band-limited back. The clean signal path is untouched, so the
        stage adds no latency to the audio and is exactly transparent when amount is 0; the
        residual's tiny oversampling-filter delay only affects the distortion products.

        shape (v) = H tanh (v / H) + e H tanh^2 (v / H) with v = drive * (x + beta * LF (x)),
        divided by drive again (so Drive changes the amount of colour, not the level). The
        even term's DC is removed by a 5 Hz DC blocker. Characters are smoothed over 20 ms,
        so changing model or Drive never clicks.
    */
    class ColourStage
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int maxBlock = 512;

        void prepare (double sampleRate);
        void reset() noexcept;

        void setTarget (const ColourCharacter& character, float driveDb, bool snap) noexcept;

        /** Adds the residual to [start, start + num). Does nothing (bit-exact) when silent. */
        void process (float* const* channels, int numChannels, int start, int num) noexcept;

        bool isSilent() const noexcept;

        /** Delay of the residual path in samples (not reported: the clean path is not delayed). */
        float getResidualLatency() const noexcept;

    private:
        void processPiece (float* const* channels, int numChannels, int start, int num) noexcept;

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        juce::AudioBuffer<float> scratch;
        double rate = 48000.0;
        juce::SmoothedValue<float> headroom { 4.0f }, even, lowEmphasis, amount, drive { 1.0f };
        std::array<float, maxChannels> lowState {}, dcIn {}, dcOut {};
        float lowCoefficient = 0.0f, dcCoefficient = 0.999f;
    };
}
