#include "Paths.h"

namespace dooblrgenerate::gen
{
    Paths Paths::standard()
    {
        const auto overrideRoot = juce::SystemStats::getEnvironmentVariable ("DOOBLR_GENERATE_HOME", {});

        if (overrideRoot.isNotEmpty())
            return at (juce::File (overrideRoot));

       #if JUCE_MAC
        return at (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Application Support/dooblr/generate"));
       #else
        return at (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("dooblr/generate"));
       #endif
    }
}
