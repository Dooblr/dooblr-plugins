/*
    DooblrVocalsLoadCheck: loads a built or installed dooblr-vocals.vst3 the way a host does.

    1. dlopen()s the bundle's binary and looks up GetPluginFactory. This goes through
       the same system checks a DAW's plug-in scanner hits (Gatekeeper/quarantine,
       library validation, missing symbols), and prints dlerror() verbatim on failure.
    2. Uses JUCE's VST3 hosting to scan the bundle, instantiate the plug-in, prepare it,
       run a sine through it, checking the output is non-silent and finite, that latency
       is reported, and that the factory programs are exposed.

    Usage: DooblrVocalsLoadCheck /path/to/dooblr-vocals.vst3 [expected-version]
    Exit code 0 only when every step succeeds.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <dlfcn.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace
{
    int fail (const juce::String& message)
    {
        std::fprintf (stderr, "LOAD CHECK FAILED: %s\n", message.toRawUTF8());
        return 1;
    }

    juce::File findBinary (const juce::File& bundle)
    {
        const auto contents = bundle.getChildFile ("Contents");
        const juce::StringArray dirs { "MacOS", "x86_64-linux", "arm64-linux", "i386-linux", "aarch64-linux" };

        for (const auto& dir : dirs)
        {
            const auto folder = contents.getChildFile (dir);
            const auto named = folder.getChildFile (bundle.getFileNameWithoutExtension());

            if (named.existsAsFile())
                return named;

            const auto so = folder.getChildFile (bundle.getFileNameWithoutExtension() + ".so");
            if (so.existsAsFile())
                return so;

            const auto files = folder.findChildFiles (juce::File::findFiles, false);
            if (! files.isEmpty())
                return files.getFirst();
        }

        return {};
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

    const auto binary = findBinary (bundle);

    if (! binary.existsAsFile())
        return fail ("no binary in " + bundle.getFullPathName() + "/Contents/{MacOS,*-linux}");

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

    if (description.isInstrument)
        return fail ("the plug-in is reported as an instrument; dooblr-vocals is an audio effect");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    juce::String error;
    auto instance = format.createInstanceFromDescription (description, sampleRate, blockSize, error);

    if (instance == nullptr)
        return fail ("instantiation failed: " + error);

    std::printf ("  instance buses: %d in / %d out (main output %d channels)\n", instance->getTotalNumInputChannels(),
                 instance->getTotalNumOutputChannels(), instance->getMainBusNumOutputChannels());

    if (instance->getMainBusNumOutputChannels() != 2)
        return fail ("the plug-in's main output is not stereo");

    instance->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);

    std::printf ("  programs: %d (", instance->getNumPrograms());
    for (int p = 0; p < instance->getNumPrograms(); ++p)
        std::printf ("%s%s", p > 0 ? ", " : "", instance->getProgramName (p).toRawUTF8());
    std::printf (")\n");

    if (instance->getNumPrograms() < 6)
        return fail ("expected Init plus the factory vocal presets");

    juce::AudioBuffer<float> buffer (2, blockSize);
    bool finite = true;
    float peak = 0.0f;
    constexpr int numBlocks = 200;
    double phase = 0.0;
    const auto increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;

    for (int block = 0; block < numBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto s = (float) (0.4 * std::sin (phase));
            phase += increment;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        juce::MidiBuffer midi;
        instance->processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = buffer.getSample (ch, i);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
    }

    const auto latency = instance->getLatencySamples();

    // Key Follow's optional sidechain: the host can enable it (stereo) and audio still flows.
    bool sidechainOk = true;
    juce::String sidechainInfo = "no aux input bus";
    if (instance->getBusCount (true) > 1)
    {
        instance->releaseResources();
        auto layout = instance->getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        const auto accepted = instance->setBusesLayout (layout);
        instance->prepareToPlay (sampleRate, blockSize);
        const auto channels = instance->getTotalNumInputChannels();
        juce::AudioBuffer<float> sc (juce::jmax (channels, instance->getTotalNumOutputChannels()), blockSize);
        float scPeak = 0.0f;
        for (int block = 0; block < 50; ++block)
        {
            for (int ch = 0; ch < sc.getNumChannels(); ++ch)
                for (int i = 0; i < blockSize; ++i)
                    sc.setSample (ch, i, (float) (0.3 * std::sin (phase + increment * i)));
            phase += increment * blockSize;
            juce::MidiBuffer midi;
            instance->processBlock (sc, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto v = sc.getSample (ch, i);
                    sidechainOk = sidechainOk && std::isfinite (v);
                    scPeak = std::max (scPeak, std::abs (v));
                }
        }
        sidechainOk = sidechainOk && accepted && channels == 4 && scPeak > 0.05f;
        sidechainInfo = juce::String ("\"") + instance->getBus (true, 1)->getName() + "\" bus, stereo "
                        + (accepted ? "accepted" : "REJECTED") + ", " + juce::String (channels) + " input channels, peak "
                        + juce::String (scPeak, 3);
    }
    std::printf ("  sidechain: %s\n", sidechainInfo.toRawUTF8());

    instance->releaseResources();
    std::printf ("  instantiated, %d parameters, latency %d samples, sine through %d blocks: peak %.3f, %s\n",
                 instance->getParameters().size(), latency, numBlocks, (double) peak,
                 finite ? "output finite" : "NON-FINITE OUTPUT");
    instance.reset();

    if (! finite)
        return fail ("processed audio contains NaN/Inf");

    if (peak < 0.05f)
        return fail ("a 220 Hz sine produced (almost) silent output");

    if (! sidechainOk)
        return fail ("the sidechain layout did not process cleanly");

    if (latency < 256)
        return fail ("latency is not reported (got " + juce::String (latency) + " samples)");

    std::printf ("  load check passed\n");
    return 0;
}
