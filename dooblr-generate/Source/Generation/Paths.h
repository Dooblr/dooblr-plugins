#pragma once

#include <juce_core/juce_core.h>

namespace dooblrgenerate::gen
{
    /**
        Where dooblr-generate keeps its files:
            ~/Library/Application Support/dooblr/generate/
                models/<model-id>/     downloaded weights (never shipped with dooblr)
                hf-cache/              Hugging Face cache for the text encoders models fetch
                history/               generated WAVs + history.json
                source/                copies of dropped samples used as Variation init audio
                runtime/               the worker script (written by the plug-in)
                settings.json          chosen Python, located model folders
                hf_token               a Hugging Face token the user pasted (0600)
        DOOBLR_GENERATE_HOME overrides the root (the tests use a temporary folder).
    */
    struct Paths
    {
        juce::File root;

        static Paths standard();
        static Paths at (const juce::File& root) { return { root }; }

        juce::File models() const      { return root.getChildFile ("models"); }
        juce::File hfCache() const     { return root.getChildFile ("hf-cache"); }
        juce::File history() const     { return root.getChildFile ("history"); }
        juce::File historyIndex() const { return history().getChildFile ("history.json"); }
        juce::File source() const      { return root.getChildFile ("source"); }
        juce::File runtime() const     { return root.getChildFile ("runtime"); }
        juce::File worker() const      { return runtime().getChildFile ("dooblr_generate_worker.py"); }
        juce::File settings() const    { return root.getChildFile ("settings.json"); }
        juce::File tokenFile() const   { return root.getChildFile ("hf_token"); }
    };
}
