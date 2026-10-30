#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace dooblrprophet::link
{
    /**
        A minimal OSC 1.0 message codec for the remote link: int32 ('i'), float32 ('f') and string
        ('s') arguments, big-endian, 4-byte aligned. No bundles. juce::var arguments: int, double
        (sent as float32) or string.
    */
    struct OscMessage
    {
        juce::String address;
        juce::Array<juce::var> args;

        OscMessage() = default;
        explicit OscMessage (juce::String a) : address (std::move (a)) {}
        OscMessage& add (int v) { args.add (v); return *this; }
        OscMessage& add (float v) { args.add ((double) v); return *this; }
        OscMessage& add (double v) { args.add (v); return *this; }
        OscMessage& add (const juce::String& v) { args.add (v); return *this; }
        OscMessage& add (const char* v) { args.add (juce::String (v)); return *this; }

        int size() const noexcept { return args.size(); }
        bool isNumber (int i) const { return args[i].isInt() || args[i].isDouble() || args[i].isInt64(); }
        float floatArg (int i, float fallback = 0.0f) const { return isNumber (i) ? (float) (double) args[i] : fallback; }
        /** Numbers round to the nearest int (Max sends whole floats as 'f' sometimes). */
        int intArg (int i, int fallback = 0) const { return isNumber (i) ? juce::roundToInt ((double) args[i]) : fallback; }
        juce::String stringArg (int i) const { return args[i].isString() ? args[i].toString() : juce::String(); }
    };

    juce::MemoryBlock encode (const OscMessage& message);

    /** nullopt for anything malformed (truncated, bad alignment, unknown type tag, bundle). */
    std::optional<OscMessage> decode (const void* data, size_t size);
}
