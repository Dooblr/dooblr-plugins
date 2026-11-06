/*
    DooblrGenerateLoadCheck: loads a built or installed dooblr-generate.vst3 the way a host does.

    1. dlopen()s the bundle's binary and looks up GetPluginFactory. This goes through
       the same system checks a DAW's plug-in scanner hits (Gatekeeper/quarantine,
       library validation, missing symbols), and prints dlerror() verbatim on failure.
    2. Uses JUCE's VST3 hosting to scan the bundle, instantiate the plug-in, prepare it
       and process a few blocks of audio, checking the output is finite.

    Usage: DooblrGenerateLoadCheck /path/to/dooblr-generate.vst3 [expected-version]
    Exit code 0 only when every step succeeds.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace
{
    int fail (const juce::String& message)
    {
        std::fprintf (stderr, "LOAD CHECK FAILED: %s\n", message.toRawUTF8());
        return 1;
    }

    juce::File findBinary (const juce::File& bundle)
    {
        const auto macDir = bundle.getChildFile ("Contents/MacOS");
        const auto named = macDir.getChildFile (bundle.getFileNameWithoutExtension());

        if (named.existsAsFile())
            return named;

        const auto files = macDir.findChildFiles (juce::File::findFiles, false);
        return files.isEmpty() ? juce::File() : files.getFirst();
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s /path/to/plugin.vst3 [expected-version]\n", argv[0]);
        return 2;
    }

    const juce::File bundle (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::String expectedVersion (argc > 2 ? argv[2] : "");

    if (! bundle.isDirectory())
        return fail ("bundle not found: " + bundle.getFullPathName());

    // --- 1. Raw dynamic-loader check -------------------------------------------------
    const auto binary = findBinary (bundle);

    if (! binary.existsAsFile())
        return fail ("no binary in " + bundle.getFullPathName() + "/Contents/MacOS");

    dlerror();
    void* handle = dlopen (binary.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);

    if (handle == nullptr)
    {
        const char* error = dlerror();
        return fail (juce::String ("dlopen: ") + (error != nullptr ? error : "unknown error"));
    }

    if (dlsym (handle, "GetPluginFactory") == nullptr)
        return fail ("dlopen succeeded but GetPluginFactory is not exported");

    std::printf ("  dlopen OK, GetPluginFactory exported: %s\n", binary.getFullPathName().toRawUTF8());

    // --- 2. Real VST3 hosting through JUCE ---------------------------------------------
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, bundle.getFullPathName());

    if (types.isEmpty())
        return fail ("JUCE's VST3 scanner found no plug-in classes in the bundle");

    const auto& description = *types.getFirst();
    std::printf ("  scanned: \"%s\" by %s, version %s, %d in / %d out\n",
                 description.name.toRawUTF8(), description.manufacturerName.toRawUTF8(),
                 description.version.toRawUTF8(), description.numInputChannels,
                 description.numOutputChannels);

    if (expectedVersion.isNotEmpty() && description.version != expectedVersion)
        return fail ("version is " + description.version + ", expected " + expectedVersion);

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    juce::String error;
    auto instance = format.createInstanceFromDescription (description, sampleRate, blockSize, error);

    if (instance == nullptr)
        return fail ("instantiation failed: " + error);

    instance->prepareToPlay (sampleRate, blockSize);

    // Sized for every bus the host gives us (main in / out plus the optional sidechain).
    const auto numChannels = std::max (2, std::max (instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buffer (numChannels, blockSize);
    juce::MidiBuffer midi;
    bool finite = true;

    for (int block = 0; block < 32; ++block)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, 0.25f * std::sin (0.05f * static_cast<float> (block * blockSize + i)));

        instance->processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
    }

    const auto latency = instance->getLatencySamples();
    instance->releaseResources();
    std::printf ("  instantiated, %d parameters, latency %d samples, processed 32 blocks: %s\n",
                 instance->getParameters().size(), latency,
                 finite ? "output finite" : "NON-FINITE OUTPUT");
    instance.reset();

    if (! finite)
        return fail ("processed audio contains NaN/Inf");

    if (latency != 0)
        return fail ("expected no latency (a sampler), got " + juce::String (latency));

    std::printf ("  load check passed\n");
    return 0;
}
