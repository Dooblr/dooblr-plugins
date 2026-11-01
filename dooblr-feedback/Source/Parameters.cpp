#include "Parameters.h"

#include "DSP/FeedbackEngine.h"

#include <cmath>

namespace dooblrfeedback::params
{
    juce::String formatValue (double value, int decimals)
    {
        decimals = juce::jlimit (0, 6, decimals);
        const auto scale = std::pow (10.0, (double) decimals);
        auto rounded = std::round (value * scale) / scale;
        if (! std::isfinite (rounded)) rounded = 0.0;
        if (std::abs (rounded) < 0.5 / scale) rounded = 0.0; // never "-0"
        if (decimals == 0) return juce::String (juce::roundToInt (rounded));
        return juce::String (rounded, decimals);
    }

    juce::String formatFrequency (double hz)
    {
        if (! std::isfinite (hz)) hz = 0.0;
        if (hz >= 10000.0) return formatValue (hz / 1000.0, 1) + " kHz";
        if (hz >= 1000.0)  return formatValue (hz / 1000.0, 2) + " kHz";
        return formatValue (hz, 0) + " Hz";
    }

    juce::String noteName (double hz, int* cents)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (! (hz > 0.0) || ! std::isfinite (hz))
        {
            if (cents != nullptr) *cents = 0;
            return "-";
        }
        const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto nearest = (int) std::lround (midi);
        if (cents != nullptr) *cents = (int) std::lround ((midi - nearest) * 100.0);
        const auto pitchClass = ((nearest % 12) + 12) % 12;
        const auto octave = (int) std::floor (nearest / 12.0) - 1;
        return juce::String (names[pitchClass]) + juce::String (octave);
    }

    juce::String noteWithCents (double hz)
    {
        int cents = 0;
        const auto name = noteName (hz, &cents);
        if (std::abs (cents) <= 2) return name;
        return name + (cents > 0 ? " +" : " ") + juce::String (cents) + "c";
    }

    bool isInfiniteCut (float v) { return v >= 0.5f * (maxCutFinite + maxCutInfinite); }
    bool isMuteDepth (float v)   { return v >= 0.5f * (safetyDepthFinite + safetyDepthMute); }

    float maxCutToEngineDb (float v)
    {
        return isInfiniteCut (v) ? dsp::FeedbackEngine::fullCutDb : juce::jlimit (0.0f, maxCutFinite, v);
    }

    float safetyDepthToEngineDb (float v)
    {
        return isMuteDepth (v) ? dsp::FeedbackEngine::fullDuckDb : juce::jlimit (0.0f, safetyDepthFinite, v);
    }

    juce::String formatCut (double cutDb, bool negative, int decimals)
    {
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92"); // U+2212
        if (cutDb >= (double) dsp::FeedbackEngine::fullCutDb - 0.5 || ! std::isfinite (cutDb))
            return (negative ? minus : juce::String()) + juce::String::fromUTF8 ("\xe2\x88\x9e") + " dB"; // infinity
        const auto text = formatValue (negative ? -cutDb : cutDb, decimals);
        return text.replace ("-", minus) + " dB";
    }

    void migrateParameterTree (juce::ValueTree& tree, int fromVersion)
    {
        if (fromVersion >= 2) return;
        // 0.1.0 defaulted Range high to 16 kHz, which hid howls at 16 - 20 kHz (0.1.1 fix): a
        // session still at exactly that value is moved to the new 20 kHz default.
        for (auto child : tree)
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == id::rangeHigh
                && std::abs ((float) (double) child.getProperty ("value", 0.0) - legacyRangeHigh) < 0.5f)
                child.setProperty ("value", (double) defaultRangeHigh, nullptr);
    }

    juce::StringArray safetyTriggerNames() { return { "Feedback", "Always" }; }

    juce::StringArray modeNames()      { return { "Safe", "Aggressive" }; }
    juce::StringArray liveNotchNames() { return { "Float", "Hold" }; }

    namespace
    {
        using Attributes = juce::AudioParameterFloatAttributes;

        std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* pid, const juce::String& name, juce::NormalisableRange<float> range,
                                                               float defaultValue, const juce::String& unit, int decimals)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, name, range, defaultValue,
                Attributes().withLabel (unit)
                            .withStringFromValueFunction ([unit, decimals] (float v, int)
                            {
                                return formatValue (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
                            })
                            .withValueFromStringFunction ([] (const juce::String& text) { return text.retainCharacters ("-0123456789.").getFloatValue(); }));
        }

        /** lo .. finite in the first `fraction` of the travel, then a detent at `top` (infinite / mute). */
        juce::NormalisableRange<float> detentRange (float lo, float finite, float top, float fraction)
        {
            return juce::NormalisableRange<float> (lo, top,
                [lo, finite, top, fraction] (float, float, float n)
                {
                    return n > fraction + 0.5f * (1.0f - fraction) ? top : lo + (finite - lo) * juce::jmin (1.0f, n / fraction);
                },
                [lo, finite, top, fraction] (float, float, float v)
                {
                    return v >= 0.5f * (finite + top) ? 1.0f : juce::jlimit (0.0f, fraction, fraction * (v - lo) / (finite - lo));
                },
                [lo, finite, top] (float, float, float v)
                {
                    if (v >= 0.5f * (finite + top)) return top;
                    return juce::jlimit (lo, finite, std::round (v * 10.0f) / 10.0f);
                });
        }

        bool saysInfinite (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            return t.contains ("inf") || t.contains (juce::String::fromUTF8 ("\xe2\x88\x9e")) || t.contains ("full") || t.contains ("mute");
        }

        std::unique_ptr<juce::AudioParameterFloat> freqParam (const char* pid, const juce::String& name, float lo, float hi, float defaultValue)
        {
            juce::NormalisableRange<float> range (lo, hi);
            range.setSkewForCentre (std::sqrt (lo * hi));
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, name, range, defaultValue,
                Attributes().withLabel ("Hz")
                            .withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                auto v = text.retainCharacters ("0123456789.").getFloatValue();
                                if (text.containsIgnoreCase ("k")) v *= 1000.0f;
                                return v;
                            }));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::detect, 1 }, "Auto", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::ringOut, 1 }, "Ring-out", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::mode, 1 }, "Mode", modeNames(), 0));
        layout.add (floatParam (id::sensitivity, "Sensitivity", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::sidechain, 1 }, "Sidechain", false));
        layout.add (freqParam (id::rangeLow, "Range low", 40.0f, 1000.0f, 80.0f));
        layout.add (freqParam (id::rangeHigh, "Range high", 2000.0f, 20000.0f, 20000.0f));

        // Max cut: 3 - 30 dB over 96 % of the travel; the last detent is a full-depth notch (infinite).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::maxCut, 1 }, "Max cut", detentRange (3.0f, maxCutFinite, maxCutInfinite, 0.96f), 18.0f,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return isInfiniteCut (v) ? formatCut (dsp::FeedbackEngine::fullCutDb, false) : formatValue (v, 1) + " dB"; })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            if (saysInfinite (text)) return maxCutInfinite;
                            return juce::jlimit (3.0f, maxCutFinite, std::abs (text.retainCharacters ("-0123456789.").getFloatValue()));
                        })));
        {
            juce::NormalisableRange<float> q (4.0f, 64.0f, 0.1f);
            q.setSkewForCentre (16.0f);
            layout.add (floatParam (id::notchQ, "Notch Q", q, 20.0f, {}, 1));
        }
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::liveNotches, 1 }, "Live notches", liveNotchNames(), 0));
        {
            juce::NormalisableRange<float> r (2.0f, 120.0f, 0.1f);
            r.setSkewForCentre (20.0f);
            layout.add (floatParam (id::release, "Release", r, 20.0f, "s", 1));
        }

        layout.add (floatParam (id::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB", 1));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, 1 }, "Bypass", false));

        // Safety duck (0.1.1)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::safety, 1 }, "Safety", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::safetyTrigger, 1 }, "Safety trigger", safetyTriggerNames(), 0));
        layout.add (floatParam (id::safetyThreshold, "Safety threshold", { -24.0f, 0.0f, 0.1f }, -6.0f, "dBFS", 1));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::safetyDepth, 1 }, "Safety depth", detentRange (3.0f, safetyDepthFinite, safetyDepthMute, 0.96f), 12.0f,
            Attributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return isMuteDepth (v) ? juce::String ("Mute") : formatCut (v, true); })
                        .withValueFromStringFunction ([] (const juce::String& text)
                        {
                            if (saysInfinite (text)) return safetyDepthMute;
                            return juce::jlimit (3.0f, safetyDepthFinite, std::abs (text.retainCharacters ("-0123456789.").getFloatValue()));
                        })));
        {
            juce::NormalisableRange<float> r (0.2f, 5.0f, 0.01f);
            r.setSkewForCentre (1.0f);
            layout.add (floatParam (id::safetyRelease, "Safety release", r, 1.5f, "s", 2));
        }
        return layout;
    }
}
