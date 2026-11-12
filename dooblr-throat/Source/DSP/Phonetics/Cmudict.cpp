#include "Phrase.h"
#include <juce_core/juce_core.h>
#include <BinaryData.h>
#include <mutex>

namespace dooblrthroat
{
    bool loadBundledCmudict (dsp::phonetics::EnglishG2P& g2p)
    {
        static std::once_flag once;
        static std::string text;
        static bool ok = false;
        std::call_once (once, [] {
            const char* data = nullptr;
            int size = 0;
            // juce_add_binary_data names the file cmudict_txt_gz
            if (BinaryData::cmudict_txt_gzSize > 0)
            {
                data = BinaryData::cmudict_txt_gz;
                size = BinaryData::cmudict_txt_gzSize;
            }
            if (data == nullptr || size <= 0)
                return;
            juce::MemoryInputStream raw (data, size_t (size), false);
            juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
            juce::MemoryOutputStream out;
            out.writeFromInputStream (gz, -1);
            text = out.toString().toStdString();
            ok = ! text.empty();
        });
        if (! ok) return false;
        if (! g2p.isLoaded())
            g2p.loadDict (text);
        return g2p.isLoaded();
    }
}
