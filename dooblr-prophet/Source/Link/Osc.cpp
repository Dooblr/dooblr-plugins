#include "Osc.h"

#include <cmath>
#include <cstring>

namespace dooblrprophet::link
{
    namespace
    {
        void appendPadded (juce::MemoryOutputStream& out, const juce::String& text)
        {
            const auto utf8 = text.toRawUTF8();
            const auto length = std::strlen (utf8);
            out.write (utf8, length);
            // At least one terminating zero, then padding to a multiple of 4.
            const auto padded = (length / 4 + 1) * 4;
            for (auto i = length; i < padded; ++i)
                out.writeByte (0);
        }

        void appendBigEndian (juce::MemoryOutputStream& out, juce::uint32 v)
        {
            const char bytes[4] { (char) (v >> 24), (char) (v >> 16), (char) (v >> 8), (char) v };
            out.write (bytes, 4);
        }

        struct Reader
        {
            const juce::uint8* data;
            size_t size, pos = 0;

            bool readString (juce::String& result)
            {
                size_t end = pos;
                while (end < size && data[end] != 0)
                    ++end;
                if (end >= size)
                    return false;
                result = juce::String::fromUTF8 ((const char*) data + pos, (int) (end - pos));
                pos = (end / 4 + 1) * 4;
                return pos <= size;
            }

            bool readWord (juce::uint32& v)
            {
                if (pos + 4 > size)
                    return false;
                v = ((juce::uint32) data[pos] << 24) | ((juce::uint32) data[pos + 1] << 16) | ((juce::uint32) data[pos + 2] << 8) | (juce::uint32) data[pos + 3];
                pos += 4;
                return true;
            }
        };
    }

    juce::MemoryBlock encode (const OscMessage& message)
    {
        juce::MemoryOutputStream out;
        appendPadded (out, message.address);

        juce::String tags (",");
        for (const auto& a : message.args)
            tags << (a.isString() ? "s" : (a.isDouble() ? "f" : "i"));
        appendPadded (out, tags);

        for (const auto& a : message.args)
        {
            if (a.isString())
            {
                appendPadded (out, a.toString());
            }
            else if (a.isDouble())
            {
                const auto f = (float) (double) a;
                juce::uint32 bits;
                std::memcpy (&bits, &f, 4);
                appendBigEndian (out, bits);
            }
            else
            {
                appendBigEndian (out, (juce::uint32) (int) a);
            }
        }

        return out.getMemoryBlock();
    }

    std::optional<OscMessage> decode (const void* data, size_t size)
    {
        if (data == nullptr || size < 4 || size % 4 != 0)
            return std::nullopt;

        Reader r { static_cast<const juce::uint8*> (data), size };
        OscMessage m;
        if (! r.readString (m.address) || ! m.address.startsWithChar ('/'))
            return std::nullopt;

        if (r.pos == size)
            return m; // no type tag string: no arguments (old-style OSC)

        juce::String tags;
        if (! r.readString (tags) || ! tags.startsWithChar (','))
            return std::nullopt;

        for (int i = 1; i < tags.length(); ++i)
        {
            const auto t = tags[i];
            juce::uint32 word = 0;

            if (t == 'i')
            {
                if (! r.readWord (word)) return std::nullopt;
                m.args.add ((int) word);
            }
            else if (t == 'f')
            {
                if (! r.readWord (word)) return std::nullopt;
                float f;
                std::memcpy (&f, &word, 4);
                if (! std::isfinite (f)) return std::nullopt;
                m.args.add ((double) f);
            }
            else if (t == 's')
            {
                juce::String s;
                if (! r.readString (s)) return std::nullopt;
                m.args.add (s);
            }
            else
            {
                return std::nullopt;
            }
        }

        return m;
    }
}
