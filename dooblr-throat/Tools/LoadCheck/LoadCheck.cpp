/*
    DooblrThroatLoadCheck: loads a built or installed dooblr-throat.vst3 the way a host does.

    1. dlopen()s the bundle's binary and looks up GetPluginFactory. This goes through
       the same system checks a DAW's plug-in scanner hits (Gatekeeper/quarantine,
       library validation, missing symbols), and prints dlerror() verbatim on failure.
    2. Uses JUCE's VST3 hosting to scan the bundle, instantiate the plug-in, prepare it
       send MIDI note-ons and process audio, checking the output is non-silent and finite,
       and that the factory programs are exposed.

    Usage: DooblrThroatLoadCheck /path/to/dooblr-throat.vst3 [expected-version]
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

    if (! description.isInstrument)
        return fail ("the plug-in is not reported as an instrument");

    // The fast scan does not instantiate, so its channel counts can read 0; report the real buses.
    std::printf ("  instance buses: %d in / %d out (main output %d channels)\n", instance->getTotalNumInputChannels(),
                 instance->getTotalNumOutputChannels(), instance->getMainBusNumOutputChannels());

    if (instance->getMainBusNumOutputChannels() != 2)
        return fail ("the plug-in's main output is not stereo");

    instance->setPlayConfigDetails (0, 2, sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);

    std::printf ("  programs: %d (", instance->getNumPrograms());
    for (int p = 0; p < instance->getNumPrograms(); ++p)
        std::printf ("%s%s", p > 0 ? ", " : "", instance->getProgramName (p).toRawUTF8());
    std::printf (")\n");

    if (instance->getNumPrograms() < 10)
        return fail ("expected the Init + 9 factory programs");

    juce::AudioBuffer<float> buffer (2, blockSize);
    bool finite = true;
    float peak = 0.0f, silentTailPeak = 0.0f;
    constexpr int numBlocks = 200; // ~2.1 s

    for (int block = 0; block < numBlocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        // A note at the start, then a second (different pitch) at ~0.53 s; nothing after 0.8 s.
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 120), 10);
        if (block == 50)
            midi.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), 0);
        if (block == 6)
            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
        if (block == 56)
            midi.addEvent (juce::MidiMessage::noteOff (1, 55), 0);

        instance->processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = buffer.getSample (ch, i);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));

                if (block >= numBlocks - 10)
                    silentTailPeak = std::max (silentTailPeak, std::abs (v));
            }
    }

    instance->releaseResources();
    std::printf ("  instantiated, %d parameters, latency %d samples, 2 MIDI notes over %d blocks: peak %.3f, "
                 "last-100 ms peak %.6f, %s\n",
                 instance->getParameters().size(), instance->getLatencySamples(), numBlocks, (double) peak,
                 (double) silentTailPeak, finite ? "output finite" : "NON-FINITE OUTPUT");
    instance.reset();

    if (! finite)
        return fail ("processed audio contains NaN/Inf");

    if (peak < 0.05f)
        return fail ("MIDI note-ons produced (almost) silent output");

    std::printf ("  load check passed\n");
    return 0;
}
