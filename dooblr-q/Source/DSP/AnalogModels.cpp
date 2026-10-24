#include "AnalogModels.h"

#include <algorithm>
#include <cmath>

namespace dooblrq::dsp
{
    namespace
    {
        constexpr double butterworthQ = 0.70710678118654752;

        double clampQ (double q) noexcept { return std::clamp (q, 0.05, 40.0); }

        /** |gain| relative to 15 dB, limited (used by the gain-dependent Q laws). */
        double normalisedGain (double gainDb, double reference) noexcept
        {
            return std::min (std::abs (gainDb) / reference, 1.5);
        }

        struct Builder
        {
            BandPrototype& p;

            void add (AnalogPrototype section) noexcept
            {
                if (p.numSections < maxSections)
                    p.sections[(size_t) p.numSections++] = section;
            }

            /** A makeEqPrototype section at r times the band frequency. */
            void eq (FilterType type, double q, double gainDb, double r = 1.0) noexcept
            {
                auto s = makeEqPrototype (type, clampQ (q), gainDb);
                s.scale *= r;
                add (s);
            }

            /** (b2 s^2 + b1 s + b0) / (s^2 + a1 s + a0), s normalised to r times the band frequency. */
            void polynomial (double b2, double b1, double b0, double a1, double a0, double r = 1.0) noexcept
            {
                b1 *= r; b0 *= r * r; a1 *= r; a0 *= r * r;
                const auto w0 = std::sqrt (a0);
                const auto k = a1 / w0;
                add ({ w0, k, b2, b1 / w0 - b2 * k, b0 / a0 - b2 });
            }

            // First-order sections, written as second-order ones with a cancelled (s + 1) pair.
            void lowShelf1 (double gainDb, double r = 1.0) noexcept
            {
                const auto g = std::pow (10.0, gainDb / 20.0);
                const auto wz = std::sqrt (g), wp = 1.0 / wz; // corner (geometric mean) at r
                polynomial (1.0, wz + 1.0, wz, wp + 1.0, wp, r);
            }

            void highShelf1 (double gainDb, double r = 1.0) noexcept
            {
                const auto g = std::pow (10.0, gainDb / 20.0);
                const auto wz = 1.0 / std::sqrt (g), wp = 1.0 / wz;
                polynomial (g, g * (wz + 1.0), g * wz, wp + 1.0, wp, r);
            }

            void highPass1 (double r = 1.0) noexcept { polynomial (1.0, 1.0, 0.0, 2.0, 1.0, r); }
            void lowPass1  (double r = 1.0) noexcept { polynomial (0.0, 1.0, 1.0, 2.0, 1.0, r); }

            /** 12 dB/oct (order 2) or 18 dB/oct (order 3, Butterworth) cut; q scales the resonance. */
            void cut (bool low, int order, double q, double baseQ) noexcept
            {
                const auto resonance = q / butterworthQ;
                eq (low ? FilterType::lowCut : FilterType::highCut, (order == 3 ? 1.0 : baseQ) * resonance, 0.0);
                if (order == 3)
                {
                    if (low) highPass1();
                    else     lowPass1();
                }
            }

            void identityIfEmpty() noexcept
            {
                if (p.numSections == 0)
                    add ({ 1.0, 1.0, 1.0, 0.0, 0.0 });
            }
        };

        double bellQ (Model model, double q, double gainDb) noexcept
        {
            switch (model)
            {
                case Model::neve:     return q * 0.55 * (1.0 + 0.35 * normalisedGain (gainDb, 15.0));
                case Model::sslE:     return q;                                                       // constant Q
                case Model::sslG:     return q * (0.4 + 0.6 * normalisedGain (gainDb, 12.0));          // variable Q
                case Model::api:      return q * (0.3 + 0.7 * normalisedGain (gainDb, 12.0));          // proportional Q
                case Model::pultec:   return q * 0.5;
                case Model::harrison: return q * (0.45 + 0.55 * normalisedGain (gainDb, 10.0));
                case Model::trident:  return q * 0.7;
                case Model::helios:   return q * (gainDb >= 0.0 ? 0.8 : 1.4);
                case Model::emi:      return q * 0.6;
                case Model::digital:  break;
            }
            return q;
        }
    }

    double modelBellQFactor (Model model, FilterType type, double gainDb) noexcept
    {
        if (model == Model::digital || type != FilterType::bell)
            return 1.0;
        return clampQ (bellQ (model, 1.0, gainDb));
    }

    BandPrototype makeModelPrototype (Model model, FilterType type, Slope slope, double q,
                                      double gainDb, double attenDb) noexcept
    {
        if (model == Model::digital || type == FilterType::notch || type == FilterType::tilt)
            return makeBandPrototype (type, model == Model::digital ? slope : Slope::db12, q, gainDb);

        BandPrototype prototype;
        prototype.numSections = 0;
        Builder b { prototype };
        const auto shelfQ = q / butterworthQ; // 1 at the shelf default (Q 0.707)
        const auto ng = normalisedGain (gainDb, 15.0);
        attenDb = std::max (0.0, attenDb);

        if (type == FilterType::bell)
        {
            if (model == Model::pultec && gainDb < 0.0)
                b.eq (FilterType::bell, bellQ (model, q, gainDb) * 1.6, gainDb); // passive cut is narrower
            else
                b.eq (FilterType::bell, bellQ (model, q, gainDb), gainDb);
            return prototype;
        }

        if (type == FilterType::lowCut || type == FilterType::highCut)
        {
            const auto low = type == FilterType::lowCut;
            switch (model)
            {
                case Model::neve:    b.cut (low, low ? 3 : 2, q, 0.6); break;
                case Model::sslE:
                case Model::sslG:
                case Model::trident: b.cut (low, low ? 3 : 2, q, butterworthQ); break;
                case Model::api:
                case Model::harrison: b.cut (low, 2, q, butterworthQ); break;
                case Model::pultec:
                case Model::helios:
                case Model::emi:     b.cut (low, 2, q, 0.6); break;
                case Model::digital: break;
            }
            return prototype;
        }

        const auto low = type == FilterType::lowShelf;
        const auto shelf = low ? FilterType::lowShelf : FilterType::highShelf;

        switch (model)
        {
            case Model::neve:
                // Inductor shelves: a resonant bump / dip next to the shelf.
                b.eq (shelf, low ? std::clamp (1.0 * shelfQ, 0.4, 3.0) : 0.85 * shelfQ, gainDb);
                break;
            case Model::sslE: b.eq (shelf, (low ? 0.6 : 0.5) * shelfQ, gainDb); break;  // gentler shelves
            case Model::sslG: b.eq (shelf, 1.2 * shelfQ, gainDb); break;               // overshoot / undershoot
            case Model::api:  b.eq (shelf, (0.5 + 0.4 * ng) * shelfQ, gainDb); break;  // steepens with gain
            case Model::harrison: b.eq (shelf, (low ? 0.65 : 0.55) * shelfQ, gainDb); break;
            case Model::trident:
                if (low) b.eq (shelf, 0.8 * shelfQ, gainDb);
                else     b.highShelf1 (gainDb);
                break;
            case Model::helios:
                if (! low)               b.eq (shelf, 0.7 * shelfQ, gainDb);
                else if (gainDb >= 0.0)  b.eq (shelf, 1.6 * shelfQ, gainDb);    // peaky bass boost
                else                     b.lowShelf1 (gainDb);                   // gentle bass cut
                break;
            case Model::emi:
                if (low)                 b.lowShelf1 (gainDb);
                else if (gainDb > 0.0)   b.eq (FilterType::bell, 0.5 * shelfQ, gainDb, 0.5); // treble boost peaks an octave below
                else                     b.highShelf1 (gainDb);
                break;
            case Model::pultec:
            {
                // Passive LC network: the boost and the attenuation are separate sections at
                // slightly different frequencies, so using both gives the shelf-with-dip shape.
                const auto boost = std::max (0.0, gainDb);
                const auto cut = std::max (0.0, -gainDb) + attenDb;
                if (low)
                {
                    // Boost alone is a clean resonant shelf (no undershoot); the first-order
                    // atten shelf at the same frequency is what carves the dip above it.
                    if (boost > 0.0) b.eq (FilterType::lowShelf, 0.8 * shelfQ, boost, 1.0);
                    if (cut > 0.0)   b.lowShelf1 (-0.7 * cut, 1.0);
                }
                else
                {
                    if (boost > 0.0) b.eq (FilterType::bell, 0.7 * shelfQ, boost);   // high boost is a broad peak
                    if (cut > 0.0)   b.highShelf1 (-cut);                            // high atten is a shelf
                }
                break;
            }
            case Model::digital: break;
        }

        b.identityIfEmpty();
        return prototype;
    }

    void makeModelCoefficients (Model model, FilterType type, Slope slope, double frequency, double q,
                                double gainDb, double attenDb, double sampleRate, BandCoefficients& destination) noexcept
    {
        if (model == Model::digital)
        {
            destination = makeBandCoefficients (type, slope, frequency, q, gainDb, sampleRate);
            return;
        }

        makeBandCoefficients (makeModelPrototype (model, type, slope, q, gainDb, attenDb), frequency, sampleRate, destination);
    }

    //==============================================================================
    namespace steps
    {
        // Published switch positions (Hz) of the units these models are styled on.
        constexpr float neveHpf[]   { 50, 80, 160, 300 };
        constexpr float neveLow[]   { 35, 60, 110, 220 };
        constexpr float neveMid[]   { 360, 700, 1600, 3200, 4800, 7200 };
        constexpr float neveHigh[]  { 12000 };
        constexpr float apiLow[]    { 30, 40, 50, 100, 200, 300, 400 };
        constexpr float apiMid[]    { 30, 40, 50, 100, 200, 300, 400, 600, 800, 1500, 2500, 3000, 5000,
                                      7000, 10000, 12500, 15000, 20000 };
        constexpr float apiHigh[]   { 2500, 5000, 7000, 10000, 12500, 15000, 20000 };
        constexpr float apiHpf[]    { 50 };
        constexpr float apiLpf[]    { 15000 };
        constexpr float apiGain[]   { -12, -9, -6, -4, -2, 0, 2, 4, 6, 9, 12 };
        constexpr float pultecLow[] { 20, 30, 60, 100 };
        constexpr float pultecMid[] { 3000, 4000, 5000, 8000, 10000, 12000, 16000 };
        constexpr float pultecHigh[] { 5000, 10000, 20000 };
        constexpr float tridentLow[] { 50, 80, 100, 150 };
        constexpr float tridentMid[] { 250, 500, 1000, 2000, 3000, 5000, 7000, 9000 };
        constexpr float tridentHigh[] { 8000, 10000, 12000, 15000 };
        constexpr float tridentHpf[] { 25, 50, 100 };
        constexpr float tridentLpf[] { 9000, 12000, 15000 };
        constexpr float heliosLow[] { 50, 60, 100, 200, 300 };
        constexpr float heliosMid[] { 700, 1000, 1400, 2000, 2800, 3500, 4500, 6000 };
        constexpr float heliosHigh[] { 10000 };
        constexpr float heliosHighGain[] { -16, -12, -8, -4, 0, 4, 8, 12 };
        constexpr float emiLow[]    { 50 };
        constexpr float emiHigh[]   { 10000 };

        template <size_t N>
        constexpr StepTable table (const float (&values)[N]) noexcept { return { values, (int) N }; }
    }

    StepTable frequencySteps (Model model, FilterType type) noexcept
    {
        using T = FilterType;
        switch (model)
        {
            case Model::neve:
                if (type == T::lowCut)    return steps::table (steps::neveHpf);
                if (type == T::lowShelf)  return steps::table (steps::neveLow);
                if (type == T::bell)      return steps::table (steps::neveMid);
                if (type == T::highShelf) return steps::table (steps::neveHigh);
                break;
            case Model::api:
                if (type == T::lowShelf)  return steps::table (steps::apiLow);
                if (type == T::bell)      return steps::table (steps::apiMid);
                if (type == T::highShelf) return steps::table (steps::apiHigh);
                if (type == T::lowCut)    return steps::table (steps::apiHpf);
                if (type == T::highCut)   return steps::table (steps::apiLpf);
                break;
            case Model::pultec:
                if (type == T::lowShelf)  return steps::table (steps::pultecLow);
                if (type == T::bell)      return steps::table (steps::pultecMid);
                if (type == T::highShelf) return steps::table (steps::pultecHigh);
                break;
            case Model::trident:
                if (type == T::lowShelf)  return steps::table (steps::tridentLow);
                if (type == T::bell)      return steps::table (steps::tridentMid);
                if (type == T::highShelf) return steps::table (steps::tridentHigh);
                if (type == T::lowCut)    return steps::table (steps::tridentHpf);
                if (type == T::highCut)   return steps::table (steps::tridentLpf);
                break;
            case Model::helios:
                if (type == T::lowShelf)  return steps::table (steps::heliosLow);
                if (type == T::bell)      return steps::table (steps::heliosMid);
                if (type == T::highShelf) return steps::table (steps::heliosHigh);
                break;
            case Model::emi:
                if (type == T::lowShelf)  return steps::table (steps::emiLow);
                if (type == T::highShelf) return steps::table (steps::emiHigh);
                break;
            case Model::digital:
            case Model::sslE:
            case Model::sslG:
            case Model::harrison:
                break;
        }
        return {};
    }

    StepTable gainSteps (Model model, FilterType type) noexcept
    {
        if (model == Model::api && (type == FilterType::bell || type == FilterType::lowShelf || type == FilterType::highShelf))
            return steps::table (steps::apiGain);
        if (model == Model::helios && type == FilterType::highShelf)
            return steps::table (steps::heliosHighGain);
        return {};
    }

    float snapFrequency (Model model, FilterType type, float frequency) noexcept
    {
        const auto t = frequencySteps (model, type);
        if (t.empty() || ! (frequency > 0.0f))
            return frequency;

        auto best = t.values[0];
        for (int i = 1; i < t.size; ++i)
            if (std::abs (std::log (t.values[i] / frequency)) < std::abs (std::log (best / frequency)))
                best = t.values[i];
        return best;
    }

    float snapGain (Model model, FilterType type, float gainDb) noexcept
    {
        const auto t = gainSteps (model, type);
        if (t.empty())
            return gainDb;

        auto best = t.values[0];
        for (int i = 1; i < t.size; ++i)
            if (std::abs (t.values[i] - gainDb) < std::abs (best - gainDb))
                best = t.values[i];
        return best;
    }

    const char* modelName (Model model) noexcept
    {
        switch (model)
        {
            case Model::digital:  return "Digital";
            case Model::neve:     return "Neve 1073/1081";
            case Model::sslE:     return "SSL 4000 E";
            case Model::sslG:     return "SSL 4000 G";
            case Model::api:      return "API 550A/B";
            case Model::pultec:   return "Pultec EQP-1A";
            case Model::harrison: return "Harrison 32C";
            case Model::trident:  return "Trident A-Range";
            case Model::helios:   return "Helios";
            case Model::emi:      return "EMI TG";
        }
        return "Digital";
    }

    //==============================================================================
    ColourCharacter colourFor (Model model) noexcept
    {
        switch (model)
        {
            //                            headroom  even   LF    amount
            case Model::neve:     return { 3.5f,    0.06f, 0.6f, 1.0f };  // transformers, class A
            case Model::sslE:     return { 5.5f,    0.01f, 0.0f, 1.0f };  // clean VCA-era op-amps
            case Model::sslG:     return { 6.5f,    0.01f, 0.0f, 1.0f };
            case Model::api:      return { 4.0f,    0.03f, 0.3f, 1.0f };  // discrete op-amps, output transformer
            case Model::pultec:   return { 3.2f,    0.10f, 0.4f, 1.0f };  // tube make-up stage
            case Model::harrison: return { 5.0f,    0.02f, 0.1f, 1.0f };
            case Model::trident:  return { 3.8f,    0.05f, 0.5f, 1.0f };
            case Model::helios:   return { 3.0f,    0.08f, 0.6f, 1.0f };  // inductor saturation
            case Model::emi:      return { 3.4f,    0.08f, 0.3f, 1.0f };
            case Model::digital:  break;
        }
        return { 4.0f, 0.0f, 0.0f, 0.0f };
    }

    void ColourStage::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) maxChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) maxBlock);
        scratch.setSize (maxChannels, maxBlock);

        for (auto* v : { &headroom, &even, &lowEmphasis, &amount, &drive })
            v->reset (rate, 0.02);

        lowCoefficient = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 120.0 / (4.0 * rate));
        dcCoefficient = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 5.0 / rate);
        reset();
    }

    void ColourStage::reset() noexcept
    {
        if (oversampling != nullptr)
            oversampling->reset();
        lowState.fill (0.0f);
        dcIn.fill (0.0f);
        dcOut.fill (0.0f);
        for (auto* v : { &headroom, &even, &lowEmphasis, &amount, &drive })
            v->setCurrentAndTargetValue (v->getTargetValue());
    }

    void ColourStage::setTarget (const ColourCharacter& c, float driveDb, bool snap) noexcept
    {
        const auto driveGain = juce::Decibels::decibelsToGain (driveDb);
        // While silent, the character can jump (it is not audible); only amount ramps.
        const auto jump = snap || (! amount.isSmoothing() && juce::exactlyEqual (amount.getCurrentValue(), 0.0f));

        if (c.amount > 0.0f || snap)
        {
            for (auto [v, target] : { std::pair { &headroom, c.headroom }, { &even, c.even }, { &lowEmphasis, c.lowEmphasis },
                                      { &drive, driveGain } })
            {
                if (jump) v->setCurrentAndTargetValue (target);
                else      v->setTargetValue (target);
            }
        }

        if (snap) amount.setCurrentAndTargetValue (c.amount);
        else      amount.setTargetValue (c.amount);
    }

    bool ColourStage::isSilent() const noexcept
    {
        return oversampling == nullptr
            || (! amount.isSmoothing() && juce::exactlyEqual (amount.getTargetValue(), 0.0f));
    }

    float ColourStage::getResidualLatency() const noexcept
    {
        return oversampling != nullptr ? oversampling->getLatencyInSamples() : 0.0f;
    }

    void ColourStage::process (float* const* channels, int numChannels, int start, int num) noexcept
    {
        if (isSilent() || num <= 0 || numChannels <= 0)
            return;

        numChannels = std::min (numChannels, maxChannels);
        constexpr int piece = 32; // characters are updated (smoothed) per piece

        for (int offset = 0; offset < num; offset += piece)
            processPiece (channels, numChannels, start + offset, std::min (piece, num - offset));

        if (isSilent())
            reset(); // faded out: start clean next time
    }

    void ColourStage::processPiece (float* const* channels, int numChannels, int start, int num) noexcept
    {
        const auto h = headroom.skip (num);
        const auto e = even.skip (num);
        const auto beta = lowEmphasis.skip (num);
        const auto a = amount.skip (num);
        const auto d = drive.skip (num);
        const auto invH = 1.0f / h;
        const auto scale = a / d;

        for (int ch = 0; ch < numChannels; ++ch)
            scratch.copyFrom (ch, 0, channels[ch] + start, num);
        for (int ch = numChannels; ch < maxChannels; ++ch)
            scratch.clear (ch, 0, num);

        juce::dsp::AudioBlock<float> block (scratch.getArrayOfWritePointers(), (size_t) maxChannels, 0, (size_t) num);
        auto up = oversampling->processSamplesUp (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = up.getChannelPointer ((size_t) ch);
            auto lp = lowState[(size_t) ch];

            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                lp = x[i] + lowCoefficient * (lp - x[i]);
                const auto v = d * (x[i] + beta * lp);
                const auto t = std::tanh (v * invH);
                x[i] = scale * ((h * t - v) + e * h * t * t);
            }

            lowState[(size_t) ch] = lp;
        }

        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* r = scratch.getReadPointer (ch);
            auto* out = channels[ch] + start;
            auto xIn = dcIn[(size_t) ch], yOut = dcOut[(size_t) ch];

            for (int i = 0; i < num; ++i)
            {
                yOut = r[i] - xIn + dcCoefficient * yOut;
                xIn = r[i];
                out[i] += yOut;
            }

            dcIn[(size_t) ch] = xIn;
            dcOut[(size_t) ch] = yOut;
        }
    }
}
