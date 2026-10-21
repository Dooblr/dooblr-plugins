// Smoke tests for the dooblr-q processor, DSP and editor.
// Exit code is 0 only if every check passes; registered with CTest.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace params = dooblrq::params;
namespace dsp = dooblrq::dsp;

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

    void note (const std::string& text)
    {
        std::cout << "    " << text << "\n";
    }

    std::string format (double value, int decimals = 2)
    {
        return juce::String (value, decimals).toStdString();
    }

    using Layout = juce::AudioProcessor::BusesLayout;

    Layout makeLayout (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output)
    {
        Layout layout;
        layout.inputBuses.add (input);
        layout.outputBuses.add (output);
        return layout;
    }

    void setParameter (DooblrQAudioProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.getParameterState().getParameter (id);
        check (parameter != nullptr, "parameter " + id.toStdString() + " exists");

        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void setBand (DooblrQAudioProcessor& processor, int band, const char* suffix, float value)
    {
        setParameter (processor, params::bandId (band, suffix), value);
    }

    float getParameter (DooblrQAudioProcessor& processor, const juce::String& id)
    {
        return processor.getParameterState().getRawParameterValue (id)->load();
    }

    void prepare (DooblrQAudioProcessor& processor, const juce::AudioChannelSet& channels,
                  double sampleRate, int maxBlockSize)
    {
        const auto applied = processor.setBusesLayout (makeLayout (channels, channels));
        check (applied, "layout " + channels.getDescription().toStdString() + " is accepted");
        processor.setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
        processor.prepareToPlay (sampleRate, maxBlockSize);
    }

    void fillWithNoise (juce::AudioBuffer<float>& buffer, juce::int64 seed, float amplitude = 1.0f)
    {
        juce::Random random (seed);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, amplitude * (random.nextFloat() * 2.0f - 1.0f));
    }

    bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int numChannels)
    {
        if (a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < numChannels; ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch),
                             sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float result = 0.0f;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                result = std::max (result, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

        return result;
    }

    bool allFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (buffer.getSample (ch, i)))
                    return false;

        return true;
    }

    /** Processes a buffer in place using a sequence of block sizes (cycled). */
    void processInBlocks (DooblrQAudioProcessor& processor, juce::AudioBuffer<float>& buffer,
                          const std::vector<int>& blockSizes, bool bypassed = false)
    {
        juce::MidiBuffer midi;
        size_t blockIndex = 0;

        for (int start = 0; start < buffer.getNumSamples();)
        {
            const auto length = std::min (blockSizes[blockIndex++ % blockSizes.size()], buffer.getNumSamples() - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, length);

            if (bypassed)
                processor.processBlockBypassed (view, midi);
            else
                processor.processBlock (view, midi);

            start += length;
        }
    }

    /** Runs a steady sine through the processor and returns the gain in dB measured
        over the final quarter of the signal (after filters and envelopes settle). */
    double measureSineGainDb (DooblrQAudioProcessor& processor, double frequency, float amplitude,
                              double sampleRate, double seconds = 1.0)
    {
        const auto numSamples = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto value = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }

        processInBlocks (processor, buffer, { 512 });

        const auto start = numSamples * 3 / 4;
        const auto length = numSamples - start;
        const auto outputRms = (double) buffer.getRMSLevel (0, start, length);
        const auto inputRms = (double) amplitude / std::sqrt (2.0);
        return juce::Decibels::gainToDecibels (outputRms / inputRms, -200.0);
    }

    //==============================================================================
    void testInstantiationAndMetadata()
    {
        auto processor = std::make_unique<DooblrQAudioProcessor>();

        check (processor->getName() == "dooblr-q", "name is dooblr-q");
        check (! processor->acceptsMidi(), "does not accept MIDI");
        check (! processor->producesMidi(), "does not produce MIDI");
        check (! processor->isMidiEffect(), "is not a MIDI effect");
        check (processor->getLatencySamples() == 0, "latency is zero");
        check (processor->getTailLengthSeconds() >= 0.0, "tail length is non-negative");
        check (processor->getTotalNumInputChannels() == 2, "default input is stereo");
        check (processor->getTotalNumOutputChannels() == 2, "default output is stereo");
        check (processor->hasEditor(), "has an editor");
        check (processor->getParameters().size() == params::numBands * 10 + 2,
               "exposes 10 parameters per band plus output gain and stereo link");

        std::unique_ptr<juce::AudioProcessor> fromFactory (createPluginFilter());
        check (fromFactory != nullptr && fromFactory->getName() == "dooblr-q",
               "createPluginFilter() returns a dooblr-q instance");
    }

    void testBusLayouts()
    {
        DooblrQAudioProcessor processor;
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

    void testNeutralIdentity()
    {
        for (const auto& channels : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            const auto label = channels.getDescription().toStdString();

            // Defaults: every band disabled.
            {
                DooblrQAudioProcessor processor;
                prepare (processor, channels, 48000.0, 512);

                juce::AudioBuffer<float> buffer (channels.size(), 8192);
                fillWithNoise (buffer, 1);
                juce::AudioBuffer<float> reference;
                reference.makeCopyOf (buffer);

                processInBlocks (processor, buffer, { 512, 1, 37, 256 });
                note (label + ": defaults, max |out - in| = " + format (maxAbsDifference (buffer, reference), 9));
                check (bitIdentical (buffer, reference, channels.size()), label + ": default settings are bit-exact");
            }

            // Every band enabled as a bell/shelf at 0 dB with dynamics on but 0 dB range.
            {
                DooblrQAudioProcessor processor;

                for (int b = 0; b < params::numBands; ++b)
                {
                    setBand (processor, b, params::band::enabled, 1.0f);
                    setBand (processor, b, params::band::type, (float) (b % 3)); // bell, low shelf, high shelf
                    setBand (processor, b, params::band::gain, 0.0f);
                    setBand (processor, b, params::band::dynamicsEnabled, 1.0f);
                    setBand (processor, b, params::band::range, 0.0f);
                }

                prepare (processor, channels, 48000.0, 512);

                juce::AudioBuffer<float> buffer (channels.size(), 8192);
                fillWithNoise (buffer, 2);
                juce::AudioBuffer<float> reference;
                reference.makeCopyOf (buffer);

                processInBlocks (processor, buffer, { 512, 3, 100 });
                const auto difference = maxAbsDifference (buffer, reference);
                note (label + ": all bands enabled at 0 dB, max |out - in| = " + format (difference, 9));
                check (difference <= 1.0e-6f, label + ": enabled neutral bands are (near-)identity");
            }
        }
    }

    void testStaticBell()
    {
        constexpr double sampleRate = 48000.0;
        DooblrQAudioProcessor processor;
        setBand (processor, 3, params::band::enabled, 1.0f);
        setBand (processor, 3, params::band::type, (float) dsp::FilterType::bell);
        setBand (processor, 3, params::band::frequency, 1000.0f);
        setBand (processor, 3, params::band::gain, 12.0f);
        setBand (processor, 3, params::band::q, 1.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        const auto atCentre = measureSineGainDb (processor, 1000.0, 0.25f, sampleRate);
        processor.reset();
        const auto farAway = measureSineGainDb (processor, 60.0, 0.25f, sampleRate);
        processor.reset();
        const auto farAbove = measureSineGainDb (processor, 16000.0, 0.25f, sampleRate);

        note ("+12 dB bell @ 1 kHz: 1 kHz " + format (atCentre) + " dB, 60 Hz " + format (farAway)
              + " dB, 16 kHz " + format (farAbove) + " dB");
        check (std::abs (atCentre - 12.0) < 0.3, "1 kHz sine is raised by ~12 dB");
        check (std::abs (farAway) < 0.5, "60 Hz sine is (mostly) unaffected");
        check (std::abs (farAbove) < 0.5, "16 kHz sine is (mostly) unaffected");

        // Analytic response used by the display agrees with the measured DSP.
        const auto c = dsp::makeEqCoefficients (dsp::FilterType::bell, 1000.0, 1.0, 12.0, sampleRate);
        const auto analytic = juce::Decibels::gainToDecibels (dsp::magnitudeAt (c, 1000.0, sampleRate));
        check (std::abs (analytic - atCentre) < 0.3, "display maths matches measured gain");

        // A 12 dB/oct low cut at 200 Hz attenuates 50 Hz by roughly 24 dB.
        DooblrQAudioProcessor cut;
        setBand (cut, 0, params::band::enabled, 1.0f);
        setBand (cut, 0, params::band::type, (float) dsp::FilterType::lowCut);
        setBand (cut, 0, params::band::frequency, 200.0f);
        setBand (cut, 0, params::band::q, 0.707f);
        prepare (cut, juce::AudioChannelSet::stereo(), sampleRate, 512);
        const auto cut50 = measureSineGainDb (cut, 50.0, 0.25f, sampleRate);
        cut.reset();
        const auto cut5k = measureSineGainDb (cut, 5000.0, 0.25f, sampleRate);
        note ("low cut @ 200 Hz: 50 Hz " + format (cut50) + " dB, 5 kHz " + format (cut5k) + " dB");
        check (cut50 < -20.0, "low cut attenuates two octaves below by > 20 dB");
        check (std::abs (cut5k) < 0.2, "low cut leaves 5 kHz alone");
    }

    void configureDynamicBand (DooblrQAudioProcessor& processor, float rangeDb)
    {
        setBand (processor, 3, params::band::enabled, 1.0f);
        setBand (processor, 3, params::band::type, (float) dsp::FilterType::bell);
        setBand (processor, 3, params::band::frequency, 1000.0f);
        setBand (processor, 3, params::band::gain, 0.0f);
        setBand (processor, 3, params::band::q, 1.0f);
        setBand (processor, 3, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 3, params::band::range, rangeDb);
        setBand (processor, 3, params::band::threshold, -30.0f);
        setBand (processor, 3, params::band::attack, 2.0f);
        setBand (processor, 3, params::band::release, 50.0f);
    }

    void testDynamics()
    {
        constexpr double sampleRate = 48000.0;
        const auto loud  = juce::Decibels::decibelsToGain (-6.0f);
        const auto quiet = juce::Decibels::decibelsToGain (-50.0f);

        for (const auto rangeDb : { -12.0f, 12.0f })
        {
            const auto label = std::string (rangeDb < 0.0f ? "range -12 dB" : "range +12 dB");

            DooblrQAudioProcessor quietProcessor, loudProcessor;
            configureDynamicBand (quietProcessor, rangeDb);
            configureDynamicBand (loudProcessor, rangeDb);
            prepare (quietProcessor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            prepare (loudProcessor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            const auto quietGain = measureSineGainDb (quietProcessor, 1000.0, quiet, sampleRate);
            const auto loudGain  = measureSineGainDb (loudProcessor, 1000.0, loud, sampleRate);
            const auto meter     = loudProcessor.getBandDynamicGainDb (3);

            note (label + ", threshold -30 dB: quiet (-50 dBFS) " + format (quietGain) + " dB, loud (-6 dBFS) "
                  + format (loudGain) + " dB, live dynamic gain " + format (meter) + " dB");

            check (std::abs (quietGain) < 0.5, label + ": tone below threshold is unchanged");

            if (rangeDb < 0.0f)
            {
                check (loudGain < quietGain - 6.0, label + ": loud in-band tone is cut more than the quiet one");
                check (loudGain > -12.5, label + ": cut is limited to the range");
                check (meter < -6.0f, label + ": live dynamic gain reports the cut");
            }
            else
            {
                check (loudGain > quietGain + 6.0, label + ": loud in-band tone is boosted more than the quiet one");
                check (loudGain < 12.5, label + ": boost is limited to the range");
                check (meter > 6.0f, label + ": live dynamic gain reports the boost");
            }
        }

        // Dynamic-gain law itself.
        using Band = dsp::DynamicEQBand;
        check (std::abs (Band::computeDynamicGainDb (-60.0f, -30.0f, -12.0f)) < 1.0e-6f, "law: below threshold gives 0 dB");
        check (std::abs (Band::computeDynamicGainDb (-20.0f, -30.0f, -12.0f) + 5.0f) < 1.0e-4f, "law: 10 dB over gives -5 dB (2:1)");
        check (std::abs (Band::computeDynamicGainDb (0.0f, -30.0f, -12.0f) + 12.0f) < 1.0e-4f, "law: clamped to range");
        check (std::abs (Band::computeDynamicGainDb (0.0f, -30.0f, 0.0f)) < 1.0e-6f, "law: zero range is static");
    }

    void testExtremeSettingsAndAutomation()
    {
        for (const auto sampleRate : { 44100.0, 192000.0 })
        {
            DooblrQAudioProcessor processor;
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 256);

            juce::Random random (42);
            auto& parameters = processor.getParameters();
            juce::MidiBuffer midi;
            auto finite = true;
            float peak = 0.0f;

            // Random values for every parameter on every block, including on/off and type flips.
            for (int block = 0; block < 600; ++block)
            {
                for (auto* parameter : parameters)
                    parameter->setValueNotifyingHost (random.nextFloat());

                const auto blockSize = 1 + random.nextInt (256);
                juce::AudioBuffer<float> buffer (2, blockSize);
                fillWithNoise (buffer, block, 1.0f);

                if (block % 50 == 0)
                    buffer.setSample (0, 0, 8.0f);

                processor.processBlock (buffer, midi);
                finite = finite && allFinite (buffer);
                peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
            }

            note ("random automation @ " + format (sampleRate, 0) + " Hz: peak output " + format (peak, 1));
            check (finite, "output stays finite under per-block random automation @ " + format (sampleRate, 0));

            // Extreme static settings: maximum boost, narrowest Q, band edges.
            DooblrQAudioProcessor extreme;
            for (int b = 0; b < params::numBands; ++b)
            {
                setBand (extreme, b, params::band::enabled, 1.0f);
                setBand (extreme, b, params::band::type, (float) dsp::FilterType::bell);
                setBand (extreme, b, params::band::frequency, b % 2 == 0 ? params::maxFrequency : params::minFrequency);
                setBand (extreme, b, params::band::gain, params::maxGainDb);
                setBand (extreme, b, params::band::q, b % 3 == 0 ? params::maxQ : params::minQ);
                setBand (extreme, b, params::band::dynamicsEnabled, 1.0f);
                setBand (extreme, b, params::band::range, params::maxRangeDb);
                setBand (extreme, b, params::band::threshold, params::minThresholdDb);
                setBand (extreme, b, params::band::attack, params::minAttackMs);
            }
            prepare (extreme, juce::AudioChannelSet::stereo(), sampleRate, 512);

            juce::AudioBuffer<float> buffer (2, (int) sampleRate);
            fillWithNoise (buffer, 7, 1.0f);
            processInBlocks (extreme, buffer, { 512, 13 });
            check (allFinite (buffer), "output stays finite at extreme settings @ " + format (sampleRate, 0));
        }
    }

    void testBlockSizes()
    {
        constexpr double sampleRate = 48000.0;
        const auto configure = [] (DooblrQAudioProcessor& p)
        {
            setBand (p, 0, params::band::enabled, 1.0f);
            setBand (p, 1, params::band::enabled, 1.0f);
            setBand (p, 1, params::band::gain, 4.0f);
            setBand (p, 3, params::band::enabled, 1.0f);
            setBand (p, 3, params::band::gain, -6.0f);
            setBand (p, 3, params::band::q, 3.0f);
        };

        DooblrQAudioProcessor single, varied;
        configure (single);
        configure (varied);
        prepare (single, juce::AudioChannelSet::stereo(), sampleRate, 4096);
        prepare (varied, juce::AudioChannelSet::stereo(), sampleRate, 4096);

        juce::AudioBuffer<float> a (2, 4096 * 4);
        fillWithNoise (a, 11, 0.5f);
        juce::AudioBuffer<float> b;
        b.makeCopyOf (a);

        processInBlocks (single, a, { 4096 });
        processInBlocks (varied, b, { 1, 7, 64, 333, 1024, 16, 15, 17, 4096 });
        const auto difference = maxAbsDifference (a, b);
        note ("static EQ, one 4096 block vs mixed block sizes: max difference " + format (difference, 9));
        check (difference <= 1.0e-6f, "static EQ output is independent of block size");

        // Zero-length blocks, normal and bypassed, then normal processing continues.
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> empty (2, 0);
        single.processBlock (empty, midi);
        single.processBlockBypassed (empty, midi);
        check (empty.getNumSamples() == 0, "zero-length blocks are handled");

        juce::AudioBuffer<float> after (2, 512);
        fillWithNoise (after, 12, 0.5f);
        single.processBlock (after, midi);
        check (allFinite (after), "processing continues normally after a zero-length block");

        // Dynamic band with varying block sizes stays finite.
        DooblrQAudioProcessor dynamic;
        configureDynamicBand (dynamic, -12.0f);
        prepare (dynamic, juce::AudioChannelSet::stereo(), sampleRate, 1024);
        juce::AudioBuffer<float> dynamicBuffer (2, 48000);
        fillWithNoise (dynamicBuffer, 13, 0.8f);
        processInBlocks (dynamic, dynamicBuffer, { 1, 5, 1024, 77, 16 });
        check (allFinite (dynamicBuffer), "dynamic band with varying block sizes stays finite");
    }

    void testBypass()
    {
        for (const auto& channels : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            DooblrQAudioProcessor processor;
            configureDynamicBand (processor, -12.0f);
            setBand (processor, 0, params::band::enabled, 1.0f);
            setParameter (processor, params::outputGain, 6.0f);
            prepare (processor, channels, 48000.0, 512);

            juce::AudioBuffer<float> buffer (channels.size(), 4096);
            fillWithNoise (buffer, 21);
            juce::AudioBuffer<float> reference;
            reference.makeCopyOf (buffer);

            processInBlocks (processor, buffer, { 512, 17, 1 }, true);
            check (bitIdentical (buffer, reference, channels.size()),
                   channels.getDescription().toStdString() + ": bypass is bit-exact with active EQ settings");
        }
    }

    void testExtraBufferChannels()
    {
        // Only in == out layouts are accepted, so there are never output-only channels and
        // the clearing branch cannot be reached with a host-valid configuration. Check that
        // invariant, and that buffer channels beyond the bus are neither read nor written.
        DooblrQAudioProcessor processor;
        setBand (processor, 3, params::band::enabled, 1.0f);
        setBand (processor, 3, params::band::gain, 6.0f);
        prepare (processor, juce::AudioChannelSet::mono(), 48000.0, 256);
        check (processor.getTotalNumOutputChannels() <= processor.getTotalNumInputChannels(),
               "no output-only channels exist for the mono layout");

        juce::AudioBuffer<float> buffer (2, 256);
        fillWithNoise (buffer, 99);
        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        check (std::memcmp (buffer.getReadPointer (1), reference.getReadPointer (1), sizeof (float) * 256) == 0,
               "mono processor leaves the extra buffer channel untouched");
        check (std::memcmp (buffer.getReadPointer (0), reference.getReadPointer (0), sizeof (float) * 256) != 0,
               "mono processor does process channel 0");
    }

    void testStateRoundTrip()
    {
        DooblrQAudioProcessor source;
        setBand (source, 2, params::band::enabled, 1.0f);
        setBand (source, 2, params::band::type, (float) dsp::FilterType::notch);
        setBand (source, 2, params::band::frequency, 2345.0f);
        setBand (source, 2, params::band::q, 4.5f);
        setBand (source, 4, params::band::gain, -7.5f);
        setBand (source, 4, params::band::dynamicsEnabled, 1.0f);
        setBand (source, 4, params::band::range, 9.0f);
        setBand (source, 4, params::band::threshold, -18.0f);
        setBand (source, 4, params::band::attack, 3.0f);
        setBand (source, 4, params::band::release, 400.0f);
        setParameter (source, params::outputGain, -3.0f);
        setParameter (source, params::stereoLink, 0.0f);

        juce::MemoryBlock state;
        source.getStateInformation (state);
        check (state.getSize() > 0, "state serialises to a non-empty block");

        const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->getIntAttribute ("stateVersion") == params::stateVersion,
               "state carries the version tag");

        DooblrQAudioProcessor destination;
        destination.setStateInformation (state.getData(), (int) state.getSize());

        auto allMatch = true;
        for (auto* parameter : source.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            const auto restored = destination.getParameterState().getParameter (withId->paramID);
            if (restored == nullptr || std::abs (restored->getValue() - parameter->getValue()) > 1.0e-6f)
            {
                allMatch = false;
                note ("mismatch: " + withId->paramID.toStdString());
            }
        }
        check (allMatch, "every parameter value is restored");
        check (std::abs (getParameter (destination, params::bandId (2, params::band::frequency)) - 2345.0f) < 0.01f,
               "band 3 frequency restored (2345 Hz)");
        check (std::abs (getParameter (destination, params::bandId (4, params::band::release)) - 400.0f) < 0.01f,
               "band 5 release restored (400 ms)");

        // Malformed or foreign data must be ignored without crashing or changing anything.
        DooblrQAudioProcessor robust;
        robust.setStateInformation (nullptr, 0);
        const char garbage[] = "definitely not plugin state";
        robust.setStateInformation (garbage, (int) sizeof (garbage));
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomeOtherPlugin"), foreign);
        robust.setStateInformation (foreign.getData(), (int) foreign.getSize());
        check (std::abs (getParameter (robust, params::bandId (2, params::band::frequency)) - 400.0f) < 0.01f,
               "invalid state data is ignored");
    }

    void testEditor()
    {
        DooblrQAudioProcessor processor;
        configureDynamicBand (processor, -12.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        for (int iteration = 0; iteration < 2; ++iteration)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
            check (dooblrEditor != nullptr, "editor is created");

            if (dooblrEditor == nullptr)
                return;

            check (editor->getWidth() == DooblrQAudioProcessorEditor::defaultWidth
                       && editor->getHeight() == DooblrQAudioProcessorEditor::defaultHeight,
                   "editor opens at its default size");
            check (editor->isResizable(), "editor is resizable");

            bool foundTitle = false;
            for (auto* child : editor->getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    foundTitle |= label->getText() == "dooblr-q";
            check (foundTitle, "title label reads dooblr-q");

            // Controls are bound to the selected band's parameters.
            dooblrEditor->selectBand (2);
            auto* frequency = dynamic_cast<juce::Slider*> (editor->findChildWithID (params::band::frequency));
            auto* threshold = dynamic_cast<juce::Slider*> (editor->findChildWithID (params::band::threshold));
            auto* type = dynamic_cast<juce::ComboBox*> (editor->findChildWithID (params::band::type));
            check (frequency != nullptr && threshold != nullptr && type != nullptr, "band controls exist");

            if (frequency != nullptr && threshold != nullptr && type != nullptr)
            {
                frequency->setValue (2000.0, juce::sendNotificationSync);
                check (std::abs (getParameter (processor, params::bandId (2, params::band::frequency)) - 2000.0f) < 1.0f,
                       "frequency knob drives band 3 frequency");

                threshold->setValue (-40.0, juce::sendNotificationSync);
                check (std::abs (getParameter (processor, params::bandId (2, params::band::threshold)) + 40.0f) < 0.05f,
                       "threshold knob drives band 3 threshold");

                type->setSelectedItemIndex ((int) dsp::FilterType::highShelf, juce::sendNotificationSync);
                check (juce::roundToInt (getParameter (processor, params::bandId (2, params::band::type)))
                           == (int) dsp::FilterType::highShelf,
                       "type box drives band 3 type");

                dooblrEditor->selectBand (3);
                check (std::abs (frequency->getValue() - 1000.0) < 1.0, "selecting band 4 rebinds the knobs");
                check (std::abs (getParameter (processor, params::bandId (2, params::band::frequency)) - 2000.0f) < 1.0f,
                       "rebinding leaves band 3 unchanged");
            }

            // Run audio so the display has live dynamic gain to draw, then render off-screen.
            measureSineGainDb (processor, 1000.0, 0.5f, 48000.0, 0.25);
            note ("band 4 live dynamic gain while painting: " + format (processor.getBandDynamicGainDb (3)) + " dB");
            editor->setSize (1300, 760);
            check (editor->getWidth() == 1300 && editor->getHeight() == 760, "editor resizes");
            const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
            check (snapshot.isValid() && snapshot.getWidth() > 0, "editor paints to an image");

            // Optional visual check: DOOBLRQ_EDITOR_SNAPSHOT=/path/to/file.png
            if (const auto path = juce::SystemStats::getEnvironmentVariable ("DOOBLRQ_EDITOR_SNAPSHOT", {}); path.isNotEmpty() && iteration == 0)
            {
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat png;
                check (stream.openedOk() && png.writeImageToStream (snapshot, stream), "editor snapshot written");
            }

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
        { "instantiation and metadata",              testInstantiationAndMetadata },
        { "bus layouts (mono/stereo only)",          testBusLayouts },
        { "neutral settings are identity",           testNeutralIdentity },
        { "static bell and cut response",            testStaticBell },
        { "dynamic bands respond to level",          testDynamics },
        { "finite under automation/extremes",        testExtremeSettingsAndAutomation },
        { "block sizes and zero-length blocks",      testBlockSizes },
        { "bypass is bit-exact",                     testBypass },
        { "extra buffer channels / output-only",     testExtraBufferChannels },
        { "state save/restore",                      testStateRoundTrip },
        { "editor controls, paint, open/close",      testEditor },
    };

    int failedTests = 0;

    for (const auto& test : tests)
    {
        const auto failuresBefore = failedChecks;
        const auto passesBefore   = passedChecks;
        std::cout << "[RUN ] " << test.name << "\n";
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
