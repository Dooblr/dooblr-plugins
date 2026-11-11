#include "ModelCatalog.h"

namespace dooblrgenerate::gen::ModelCatalog
{
    namespace
    {
        const std::vector<ModelInfo>& models()
        {
            static const std::vector<ModelInfo> list {
                { stableAudioOpenSmall, "Stable Audio Open Small", "stabilityai/stable-audio-open-small",
                  "Stability AI Community License (free under $1M annual revenue; register for commercial use)",
                  "https://huggingface.co/stabilityai/stable-audio-open-small/blob/main/LICENSE",
                  true, false, true, 11.0, { "model_config.json", "model.safetensors" } },
                { audioLdm, "AudioLDM S v2 (non-commercial)", "cvssp/audioldm-s-full-v2",
                  "CC BY-NC-SA 4.0 (non-commercial use only)",
                  "https://huggingface.co/cvssp/audioldm-s-full-v2",
                  false, false, false, 30.0,
                  { "model_index.json", "unet/diffusion_pytorch_model.safetensors", "vae/diffusion_pytorch_model.safetensors",
                    "vocoder/model.safetensors", "text_encoder/model.safetensors" } },
                { testBackend, "Test backend (not AI)", "", "dooblr test signal", "", false, true, true, 30.0, {} },
            };
            return list;
        }
    }

    bool testBackendEnabled()
    {
        return juce::SystemStats::getEnvironmentVariable ("DOOBLR_GENERATE_TEST_BACKEND", {}) == "1";
    }

    std::vector<ModelInfo> all (bool includeTest)
    {
        std::vector<ModelInfo> out;
        for (const auto& m : models())
            if (includeTest || ! m.isTest)
                out.push_back (m);
        return out;
    }

    const ModelInfo* find (const juce::String& id)
    {
        for (const auto& m : models())
            if (m.id == id)
                return &m;
        return nullptr;
    }

    juce::StringArray missingFiles (const ModelInfo& model, const juce::File& folder)
    {
        juce::StringArray missing;
        for (const auto& f : model.requiredFiles)
            if (! folder.getChildFile (f).existsAsFile())
                missing.add (f);
        return missing;
    }
}
