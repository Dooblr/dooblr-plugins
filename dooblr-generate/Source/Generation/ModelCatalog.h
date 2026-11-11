#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace dooblrgenerate::gen
{
    /** A model dooblr-generate can run (through the worker). Weights are never bundled. */
    struct ModelInfo
    {
        juce::String id;            // folder name and worker --model
        juce::String displayName;
        juce::String repo;          // Hugging Face repo ("" for the test backend)
        juce::String licence;       // short licence line shown in the plug-in
        juce::String licenceUrl;
        bool gated = false;         // needs a Hugging Face account + accepted licence + token
        bool isTest = false;        // NOT AI: the synthetic test backend
        bool supportsSourceVariation = false;  // audio-to-audio Variation from a dropped sample
        double maxSeconds = 10.0;
        juce::StringArray requiredFiles;
    };

    namespace ModelCatalog
    {
        inline constexpr const char* stableAudioOpenSmall = "stable-audio-open-small";
        inline constexpr const char* audioLdm = "audioldm-s-full-v2";
        inline constexpr const char* testBackend = "test-backend";

        /** The models offered in the plug-in. The test backend is listed only when
            includeTest is true (tests, or DOOBLR_GENERATE_TEST_BACKEND=1). */
        std::vector<ModelInfo> all (bool includeTest);
        const ModelInfo* find (const juce::String& id);

        /** Whether the test backend is offered (DOOBLR_GENERATE_TEST_BACKEND=1). */
        bool testBackendEnabled();
        /** Files a model folder lacks (empty: complete). */
        juce::StringArray missingFiles (const ModelInfo&, const juce::File& folder);
    }
}
