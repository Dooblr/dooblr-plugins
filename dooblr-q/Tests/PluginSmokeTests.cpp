// Smoke tests for the OctaEQ milestone-1 pass-through processor and editor.
// Exit code is 0 only if every check passes; registered with CTest.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    int failedChecks = 0;
    int passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition)
        {
            ++passedChecks;
        }
        else
        {
            ++failedChecks;
            std::cout << "    FAILED: " << description << "\n";
        }
    }

    using Layout = juce::AudioProcessor::BusesLayout;

    Layout makeLayout (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output)
    {
        Layout layout;
        layout.inputBuses.add (input);
        layout.outputBuses.add (output);
        return layout;
    }

    /** Deterministic test signal including edge-case values that must survive bit-exactly. */
    void fillWithTestSignal (juce::AudioBuffer<float>& buffer, juce::int64 seed)
    {
        juce::Random random (seed);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = random.nextFloat() * 2.0f - 1.0f;

            const float specials[] = { 0.0f, -0.0f, 1.0f, -1.0f, 4.0f,
                                       std::numeric_limits<float>::denorm_min(),
                                       -std::numeric_limits<float>::min() * 0.5f };

            for (int i = 0; i < (int) std::size (specials) && i < buffer.getNumSamples(); ++i)
                samples[i] = specials[i];
        }
    }

    bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int numChannels)
    {
        if (a.getNumSamples() != b.getNumSamples())
            return false;

        for (int channel = 0; channel < numChannels; ++channel)
            if (std::memcmp (a.getReadPointer (channel), b.getReadPointer (channel),
                             sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    std::unique_ptr<OctaEQAudioProcessor> makePreparedProcessor (const juce::AudioChannelSet& channels,
                                                                 double sampleRate, int maxBlockSize)
    {
        auto processor = std::make_unique<OctaEQAudioProcessor>();
        const auto applied = processor->setBusesLayout (makeLayout (channels, channels));
        check (applied, "layout " + channels.getDescription().toStdString() + " is accepted");
        processor->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
        processor->prepareToPlay (sampleRate, maxBlockSize);
        return processor;
    }

    /** Runs blocks of the given sizes through the processor and checks each is bit-exact. */
    void checkPassThrough (OctaEQAudioProcessor& processor, int numChannels,
                           const std::vector<int>& blockSizes, bool bypassed, const std::string& label)
    {
        juce::MidiBuffer midi;
        juce::int64 seed = 1234;

        for (const auto blockSize : blockSizes)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithTestSignal (buffer, seed++);

            juce::AudioBuffer<float> reference;
            reference.makeCopyOf (buffer);

            if (bypassed)
                processor.processBlockBypassed (buffer, midi);
            else
                processor.processBlock (buffer, midi);

            check (bitIdentical (buffer, reference, numChannels),
                   label + ": block of " + std::to_string (blockSize) + " samples is bit-exact");
            check (midi.isEmpty(), label + ": no MIDI produced");
        }
    }

    //==============================================================================
    void testInstantiationAndMetadata()
    {
        auto processor = std::make_unique<OctaEQAudioProcessor>();

        check (processor->getName() == "OctaEQ", "name is OctaEQ");
        check (! processor->acceptsMidi(), "does not accept MIDI");
        check (! processor->producesMidi(), "does not produce MIDI");
        check (! processor->isMidiEffect(), "is not a MIDI effect");
        check (processor->getTailLengthSeconds() == 0.0, "tail length is zero");
        check (processor->getLatencySamples() == 0, "latency is zero");
        check (processor->getTotalNumInputChannels() == 2, "default input is stereo");
        check (processor->getTotalNumOutputChannels() == 2, "default output is stereo");
        check (processor->hasEditor(), "has an editor");

        // Factory function used by the plugin wrappers.
        std::unique_ptr<juce::AudioProcessor> fromFactory (createPluginFilter());
        check (fromFactory != nullptr && fromFactory->getName() == "OctaEQ",
               "createPluginFilter() returns an OctaEQ instance");
    }

    void testBusLayouts()
    {
        OctaEQAudioProcessor processor;
        const auto mono   = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();

        check (processor.checkBusesLayoutSupported (makeLayout (mono, mono)), "mono -> mono supported");
        check (processor.checkBusesLayoutSupported (makeLayout (stereo, stereo)), "stereo -> stereo supported");
        check (! processor.checkBusesLayoutSupported (makeLayout (mono, stereo)), "mono -> stereo rejected");
        check (! processor.checkBusesLayoutSupported (makeLayout (stereo, mono)), "stereo -> mono rejected");
        check (! processor.checkBusesLayoutSupported (makeLayout (juce::AudioChannelSet::disabled(), stereo)),
               "disabled input -> stereo rejected");
        check (! processor.checkBusesLayoutSupported (makeLayout (juce::AudioChannelSet::create5point1(),
                                                                  juce::AudioChannelSet::create5point1())),
               "5.1 rejected");
    }

    void testPassThrough (const juce::AudioChannelSet& channels, const std::string& label)
    {
        const auto numChannels = channels.size();

        for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            auto processor = makePreparedProcessor (channels, sampleRate, 1024);
            check (processor->getTotalNumInputChannels() == numChannels, label + ": input channel count");
            check (processor->getTotalNumOutputChannels() == numChannels, label + ": output channel count");

            // Varying sizes within the prepared maximum, including odd and single-sample blocks.
            checkPassThrough (*processor, numChannels, { 512, 1, 7, 64, 333, 1024, 128, 2 }, false,
                              label + " @" + std::to_string ((int) sampleRate));
            processor->releaseResources();
        }
    }

    void testZeroLengthBlocks()
    {
        for (const auto& channels : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            auto processor = makePreparedProcessor (channels, 48000.0, 512);
            juce::MidiBuffer midi;

            juce::AudioBuffer<float> empty (channels.size(), 0);
            processor->processBlock (empty, midi);
            processor->processBlockBypassed (empty, midi);
            check (empty.getNumSamples() == 0, "zero-length block handled (" + channels.getDescription().toStdString() + ")");

            // Processing must keep working normally after an empty block.
            checkPassThrough (*processor, channels.size(), { 256 }, false, "after zero-length block");
        }
    }

    void testBypass()
    {
        auto processor = makePreparedProcessor (juce::AudioChannelSet::stereo(), 48000.0, 512);
        checkPassThrough (*processor, 2, { 512, 17, 1 }, true, "bypassed stereo");

        auto monoProcessor = makePreparedProcessor (juce::AudioChannelSet::mono(), 48000.0, 512);
        checkPassThrough (*monoProcessor, 1, { 512, 17, 1 }, true, "bypassed mono");
    }

    void testOutputOnlyChannels()
    {
        // With the only supported layouts (mono->mono, stereo->stereo) there are never
        // output-only channels, so the clearing branch in processBlock cannot be reached
        // through a host-valid configuration. Verify that invariant, and that channels in
        // the buffer beyond the processor's output count are neither read nor written.
        auto processor = makePreparedProcessor (juce::AudioChannelSet::mono(), 48000.0, 256);
        check (processor->getTotalNumOutputChannels() <= processor->getTotalNumInputChannels(),
               "no output-only channels exist for the mono layout");

        juce::AudioBuffer<float> buffer (2, 256);
        fillWithTestSignal (buffer, 99);
        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);

        juce::MidiBuffer midi;
        processor->processBlock (buffer, midi);
        check (bitIdentical (buffer, reference, 2), "mono processor leaves channel 0 and the extra channel untouched");
    }

    void testStateRoundTrip()
    {
        OctaEQAudioProcessor source;
        juce::MemoryBlock state;
        source.getStateInformation (state);
        check (state.getSize() > 0, "state serialises to a non-empty block");

        OctaEQAudioProcessor destination;
        destination.setStateInformation (state.getData(), (int) state.getSize());
        check (destination.getLastRestoredStateVersion() == OctaEQAudioProcessor::currentStateVersion,
               "state round-trip restores the state version");

        juce::MemoryBlock reserialised;
        destination.getStateInformation (reserialised);
        check (reserialised == state, "re-serialised state is identical");

        // Malformed or foreign data must be ignored without crashing.
        OctaEQAudioProcessor robust;
        robust.setStateInformation (nullptr, 0);
        const char garbage[] = "definitely not plugin state";
        robust.setStateInformation (garbage, (int) sizeof (garbage));
        juce::MemoryBlock foreign;
        robust.copyXmlToBinary (juce::XmlElement ("SomeOtherPlugin"), foreign);
        robust.setStateInformation (foreign.getData(), (int) foreign.getSize());
        check (robust.getLastRestoredStateVersion() == 0, "invalid state data is ignored");
    }

    void testEditor()
    {
        OctaEQAudioProcessor processor;

        // Create, exercise and destroy the editor twice to catch teardown problems.
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            check (editor != nullptr, "editor is created");

            if (editor == nullptr)
                return;

            check (processor.getActiveEditor() == editor.get(), "processor tracks the active editor");
            check (editor->getWidth() == OctaEQAudioProcessorEditor::defaultWidth
                       && editor->getHeight() == OctaEQAudioProcessorEditor::defaultHeight,
                   "editor default size is 900 x 520");
            check (editor->isResizable(), "editor is resizable");

            bool foundStatus = false;
            for (auto* child : editor->getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    foundStatus |= label->getText() == OctaEQAudioProcessorEditor::statusText;
            check (foundStatus, "status label reads \"Audio pass-through prototype\"");

            editor->setSize (1200, 700);
            check (editor->getWidth() == 1200 && editor->getHeight() == 700, "editor resizes");

            // Render off-screen to exercise paint() and layout without a window.
            const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
            check (snapshot.isValid() && snapshot.getWidth() > 0, "editor paints to an image");

            // Hosts (via the JUCE wrappers) notify the processor before deleting its editor.
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            check (processor.getActiveEditor() == nullptr, "editor is destroyed cleanly");
        }
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };
}

int main()
{
    // Provides the MessageManager and font/graphics subsystems needed by the editor.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "instantiation and metadata",         testInstantiationAndMetadata },
        { "bus layouts (mono/stereo only)",     testBusLayouts },
        { "mono pass-through (bit-exact)",      [] { testPassThrough (juce::AudioChannelSet::mono(), "mono"); } },
        { "stereo pass-through (bit-exact)",    [] { testPassThrough (juce::AudioChannelSet::stereo(), "stereo"); } },
        { "zero-length blocks",                 testZeroLengthBlocks },
        { "bypass pass-through (bit-exact)",    testBypass },
        { "output-only channels",               testOutputOnlyChannels },
        { "state save/restore",                 testStateRoundTrip },
        { "editor open/close",                  testEditor },
    };

    int failedTests = 0;

    for (const auto& test : tests)
    {
        const auto failuresBefore = failedChecks;
        const auto passesBefore   = passedChecks;
        test.run();
        const auto failed = failedChecks - failuresBefore;
        failedTests += failed > 0 ? 1 : 0;

        std::cout << (failed == 0 ? "[PASS] " : "[FAIL] ") << test.name
                  << " (" << (passedChecks - passesBefore) << " checks passed, "
                  << failed << " failed)\n";
    }

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << "/" << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " checks failed\n";

    return failedChecks == 0 ? 0 : 1;
}
