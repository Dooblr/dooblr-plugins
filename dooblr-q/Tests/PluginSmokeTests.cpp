// Smoke tests for the dooblr-q processor, DSP and editor.
// Exit code is 0 only if every check passes; registered with CTest.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Analysis/SpectrumDisplay.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>
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
        check (processor->getParameters().size() == params::numBands * 10 + 3,
               "exposes 10 parameters per band plus output gain, stereo link and phase mode");
        check (processor->getParameters()[params::numBands * 10]->getName (64) == "Output Gain"
                   && processor->getParameters().getLast()->getName (64) == "Phase Mode",
               "phase mode (added in 0.4.0) comes after all older parameters (old indices unchanged)");

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

    //==============================================================================
    juce::MouseEvent makeMouseEvent (juce::Component& component, juce::Point<float> position,
                                     juce::Point<float> downPosition, int clicks, bool dragged)
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position,
                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, clicks, dragged };
    }

    /** Simulates a mouse drag through the component's own mouse handlers. */
    void simulateDrag (juce::Component& component, juce::Point<float> from, juce::Point<float> to)
    {
        component.mouseDown (makeMouseEvent (component, from, from, 1, false));

        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            component.mouseDrag (makeMouseEvent (component, from + (to - from) * ((float) i / (float) steps), from, 1, true));

        component.mouseUp (makeMouseEvent (component, to, from, 1, true));
    }

    void simulateDoubleClick (juce::Component& component, juce::Point<float> position)
    {
        component.mouseDoubleClick (makeMouseEvent (component, position, position, 2, false));
    }

    /** Mean y of the pixels in a column that match the curve colour, or -1 if none. */
    float findCurveY (const juce::Image& image, int x, juce::Colour curveColour)
    {
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        float sumY = 0.0f, sumWeight = 0.0f;

        for (int y = 0; y < image.getHeight(); ++y)
        {
            const auto p = pixels.getPixelColour (x, y);
            const auto distance = std::abs (p.getRed() - curveColour.getRed())
                                + std::abs (p.getGreen() - curveColour.getGreen())
                                + std::abs (p.getBlue() - curveColour.getBlue());

            if (distance < 90)
            {
                const auto weight = (float) (90 - distance);
                sumY += weight * ((float) y + 0.5f);
                sumWeight += weight;
            }
        }

        return sumWeight > 0.0f ? sumY / sumWeight : -1.0f;
    }

    void saveSnapshot (const juce::Image& image, const juce::String& suffix)
    {
        const auto base = juce::SystemStats::getEnvironmentVariable ("DOOBLRQ_EDITOR_SNAPSHOT", {});

        if (base.isEmpty())
            return;

        const juce::File baseFile (base);
        auto file = baseFile.getSiblingFile (baseFile.getFileNameWithoutExtension() + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        const auto written = stream.openedOk() && png.writeImageToStream (image, stream);
        check (written, "snapshot written");

        if (written)
            note ("snapshot: " + file.getFullPathName().toStdString());
    }

    void testCurveFollowsHandles()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        constexpr double sampleRate = 48000.0;

        DooblrQAudioProcessor processor;
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        editor->setSize (1300, 760);
        auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve"));
        check (curve != nullptr, "response curve component exists");

        if (curve == nullptr)
            return;

        constexpr int bell = 4;   // band 5: Bell, 4 kHz by default
        constexpr int shelf = 1;  // band 2: Low Shelf, 120 Hz by default
        const auto bandSettings = [&] (int b) { return processor.getBandSettings (b); };

        check (! bandSettings (bell).enabled && ! bandSettings (shelf).enabled, "bands start disabled");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-drag-before");

        // Drag the disabled bell up to +9 dB at 5 kHz.
        const auto bellTarget = juce::Point<float> (curve->frequencyToX (5000.0), curve->decibelsToY (9.0));
        simulateDrag (*curve, curve->handlePosition (bell), bellTarget);

        check (bandSettings (bell).enabled, "dragging a disabled bell handle enables the band");
        check (std::abs (bandSettings (bell).gainDb - 9.0f) < 0.1f, "bell gain follows the drag (+9 dB)");
        check (std::abs (bandSettings (bell).frequency - 5000.0f) < 25.0f, "bell frequency follows the drag (5 kHz)");
        check (curve->handlePosition (bell).getDistanceFrom (bellTarget) < 0.5f, "bell handle ends at the drop point");

        // Drag the disabled low shelf down so its handle (the shelf midpoint) sits at -6 dB at 150 Hz.
        const auto shelfTarget = juce::Point<float> (curve->frequencyToX (150.0), curve->decibelsToY (-6.0));
        simulateDrag (*curve, curve->handlePosition (shelf), shelfTarget);

        check (bandSettings (shelf).enabled, "dragging a disabled shelf handle enables the band");
        check (std::abs (bandSettings (shelf).gainDb + 12.0f) < 0.15f, "shelf gain is twice the handle level (-12 dB)");
        check (curve->handlePosition (shelf).getDistanceFrom (shelfTarget) < 0.5f, "shelf handle ends at the drop point");

        // The drawn (combined) curve passes through each handle.
        const auto bellCurveDb  = curve->responseDecibelsAt (bandSettings (bell).frequency, false);
        const auto shelfCurveDb = curve->responseDecibelsAt (bandSettings (shelf).frequency, false);
        note ("curve at bell handle: " + format (bellCurveDb) + " dB (handle +9.00 dB), curve at shelf handle: " + format (shelfCurveDb) + " dB (handle -6.00 dB)");
        check (std::abs (bellCurveDb - 9.0) < 0.1, "curve level at the bell handle equals the handle level");
        check (std::abs (shelfCurveDb + 6.0) < 0.1, "curve level at the shelf handle equals the handle level");

        // Pixels: render and locate the curve just either side of each handle (the handle
        // itself covers the line). For the symmetric bell and the shelf midpoint, the mean
        // of the two sides is the line's height at the handle.
        const auto image = curve->createComponentSnapshot (curve->getLocalBounds());
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-drag-after");

        for (const auto band : { bell, shelf })
        {
            const auto handle = curve->handlePosition (band);
            const auto x = juce::roundToInt (handle.x);
            const auto left  = findCurveY (image, x - 12, dooblrq::ui::Palette::accent);
            const auto right = findCurveY (image, x + 12, dooblrq::ui::Palette::accent);
            const auto lineY = 0.5f * (left + right);
            const auto expectedY = [curve] (int column)
            {
                return curve->decibelsToY (curve->responseDecibelsAt (curve->xToFrequency ((float) column + 0.5f), false));
            };
            const auto expectedLeft = expectedY (x - 12), expectedRight = expectedY (x + 12);

            note ("band " + std::to_string (band + 1) + ": handle y " + format (handle.y, 1) + " px, drawn line y "
                  + format (lineY, 1) + " px (left " + format (left, 1) + " vs computed " + format (expectedLeft, 1)
                  + ", right " + format (right, 1) + " vs computed " + format (expectedRight, 1) + ")");
            check (left >= 0.0f && right >= 0.0f, "curve pixels found beside band " + std::to_string (band + 1));
            check (std::abs (left - expectedLeft) <= 1.5f && std::abs (right - expectedRight) <= 1.5f,
                   "drawn pixels match the computed curve beside band " + std::to_string (band + 1) + " (within 1.5 px)");
            check (std::abs (lineY - handle.y) <= 2.0f,
                   "drawn curve passes through band " + std::to_string (band + 1) + "'s handle (within 2 px)");
        }

        // Cut and notch handles sit on the line at their corner/centre convention.
        dsp::BandSettings cut;
        cut.type = dsp::FilterType::lowCut;
        cut.frequency = 100.0f;
        cut.q = 0.707f;
        check (std::abs (Curve::handleDecibels (cut, sampleRate) + 3.01) < 0.05, "low-cut handle sits at its -3 dB corner");

        // The dynamic curve follows too: loud in-band audio pulls the live curve down.
        setBand (processor, bell, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, bell, params::band::range, -12.0f);
        setBand (processor, bell, params::band::threshold, -40.0f);
        measureSineGainDb (processor, 5000.0, 0.5f, sampleRate, 0.3);
        const auto dynamicGain = processor.getBandDynamicGainDb (bell);
        const auto liveDb = curve->responseDecibelsAt (bandSettings (bell).frequency, true);
        note ("dynamic: live gain " + format (dynamicGain) + " dB, live curve at bell " + format (liveDb) + " dB");
        check (dynamicGain < -6.0f, "dynamic gain responds to the loud tone");
        check (std::abs (liveDb - (9.0 + (double) dynamicGain)) < 0.15, "live curve = static gain + dynamic gain at the handle");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-drag-dynamic");

        // Double-click toggles a band.
        simulateDoubleClick (*curve, curve->handlePosition (bell));
        check (! bandSettings (bell).enabled, "double-click turns the band off");
        check (std::abs (curve->responseDecibelsAt (5000.0, false)) < 0.2, "a disabled band no longer bends the curve");
        simulateDoubleClick (*curve, curve->handlePosition (bell));
        check (bandSettings (bell).enabled, "double-click turns it back on");

        // Changes from a non-message thread (host automation) flag a redraw.
        curve->createComponentSnapshot (curve->getLocalBounds());
        check (! curve->isRepaintPending(), "no redraw pending after painting");
        std::thread automation ([&] { setBand (processor, bell, params::band::gain, 3.0f); });
        automation.join();
        check (curve->isRepaintPending(), "automation from another thread schedules a redraw");

        processor.editorBeingDeleted (editor.get());
        editor.reset();
    }

    //==============================================================================
    // Spectrum analyzer
    using Analyzer = dooblrq::analysis::SpectrumAnalyzer;
    using AnalyzerFifo = dooblrq::analysis::AnalyzerFifo;

    std::vector<float> makeSine (double frequency, float amplitude, double sampleRate, int numSamples)
    {
        std::vector<float> samples ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
            samples[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);

        return samples;
    }

    int peakBin (const std::vector<float>& binDb)
    {
        return (int) std::distance (binDb.begin(), std::max_element (binDb.begin() + 1, binDb.end()));
    }

    float meanDb (const std::vector<float>& binDb, double sampleRate, double low, double high)
    {
        double sum = 0.0;
        int count = 0;

        for (int bin = 1; bin < (int) binDb.size(); ++bin)
        {
            const auto f = bin * sampleRate / Analyzer::fftSize;

            if (f >= low && f <= high)
            {
                sum += (double) binDb[(size_t) bin];
                ++count;
            }
        }

        return count > 0 ? (float) (sum / count) : Analyzer::floorDb;
    }

    bool allFiniteValues (const std::vector<float>& values)
    {
        return std::all_of (values.begin(), values.end(), [] (float v) { return std::isfinite (v); });
    }

    void testSpectrumAnalyzerCore()
    {
        constexpr double sampleRate = 48000.0;
        const auto binWidth = sampleRate / Analyzer::fftSize;

        // Calibration: a sine of peak amplitude A reads 20 log10 A at its bin.
        {
            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            const auto onBin = 85.0 * binWidth; // 996.09 Hz, exactly on bin 85
            const auto sine = makeSine (onBin, 0.5f, sampleRate, 48000);
            const auto frames = analyzer.pushSamples (sine.data(), (int) sine.size());
            const auto& latest = analyzer.getLatestFrameDb();
            note ("on-bin sine " + format (onBin) + " Hz, A = 0.5 (-6.02 dBFS): bin " + std::to_string (peakBin (latest))
                  + " reads " + format (latest[85], 3) + " dBFS; " + std::to_string (frames) + " frames from 1 s");
            check (frames == (48000 - Analyzer::fftSize) / Analyzer::hopSize + 1, "75% overlap: one frame per 1024 samples after the first 4096");
            check (peakBin (latest) == 85, "on-bin sine peaks in bin 85");
            check (std::abs (latest[85] + 6.0206f) < 0.05f, "on-bin sine reads -6.02 dBFS (within 0.05 dB)");

            Analyzer kHz;
            kHz.setSampleRate (sampleRate);
            const auto tone = makeSine (1000.0, 1.0f, sampleRate, 48000);
            kHz.pushSamples (tone.data(), (int) tone.size());
            const auto& db = kHz.getLatestFrameDb();
            const auto bin = peakBin (db);
            note ("1 kHz full-scale sine: peak bin " + std::to_string (bin) + " (" + format (bin * binWidth) + " Hz), "
                  + format (db[(size_t) bin], 3) + " dBFS; display level at 1 kHz "
                  + format (Analyzer::levelAt (db, 1000.0, sampleRate, 1.0 / 12.0), 3) + " dBFS");
            check (bin == 85, "1 kHz lands in bin 85 (nearest to 1000 / 11.72 = 85.3)");
            check (db[(size_t) bin] <= 0.05f && db[(size_t) bin] > -1.5f, "1 kHz full-scale reads 0 dBFS minus Hann scalloping (< 1.42 dB)");
            check (Analyzer::levelAt (db, 1000.0, sampleRate, 1.0 / 12.0) > -1.5f, "display level at 1 kHz shows the peak");
            check (meanDb (db, sampleRate, 3000.0, 20000.0) < -90.0f, "sidelobes far from the tone stay below -90 dBFS");
        }

        // Ballistics: instant attack, 36 dB/s release.
        {
            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            const auto tone = makeSine (85.0 * binWidth, 0.5f, sampleRate, 48000);
            analyzer.pushSamples (tone.data(), (int) tone.size());
            const std::vector<float> silence (24000, 0.0f);
            analyzer.pushSamples (silence.data(), (int) silence.size());
            const auto live = analyzer.getLiveDb()[85];
            note ("live level 0.5 s after the tone stops: " + format (live, 1) + " dBFS (latest frame "
                  + format (analyzer.getLatestFrameDb()[85], 1) + ")");
            check (live < -15.0f && live > -35.0f, "live display decays at the release rate after the tone stops");
        }

        // White noise is flat; silence reads the floor with no NaN/Inf.
        {
            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            std::vector<float> noise (48000 * 10);
            juce::Random random (42);
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            analyzer.freeze(); // the long-term average only runs while frozen
            analyzer.pushSamples (noise.data(), (int) noise.size());
            const auto& average = analyzer.getAverageDb();
            const auto low = meanDb (average, sampleRate, 100.0, 1000.0);
            const auto mid = meanDb (average, sampleRate, 1000.0, 5000.0);
            const auto high = meanDb (average, sampleRate, 5000.0, 20000.0);
            note ("white noise average: 100-1k " + format (low) + ", 1k-5k " + format (mid) + ", 5k-20k " + format (high) + " dBFS/bin");
            check (std::abs (low - high) < 1.0f && std::abs (mid - high) < 1.0f, "white noise reads flat (within 1 dB)");
            check (allFiniteValues (average) && allFiniteValues (analyzer.getLiveDb()), "noise levels are finite");

            Analyzer quiet;
            quiet.setSampleRate (sampleRate);
            const std::vector<float> silence (48000, 0.0f);
            quiet.pushSamples (silence.data(), (int) silence.size());
            const auto atFloor = [] (const std::vector<float>& v)
            {
                return std::all_of (v.begin(), v.end(), [] (float x) { return juce::exactlyEqual (x, Analyzer::floorDb); });
            };
            check (atFloor (quiet.getLatestFrameDb()) && atFloor (quiet.getLiveDb()) && atFloor (quiet.getAverageDb())
                       && atFloor (quiet.getPeakDb()),
                   "silence reads exactly the floor (-120 dBFS) everywhere, no NaN/Inf");
        }

        // FIFO: never blocks, bounded, drops (and counts) the overflow; variable blocks lose nothing.
        {
            AnalyzerFifo fifo;
            std::vector<float> left (8192, 0.25f), right (8192, 0.75f);
            const float* channels[] { left.data(), right.data() };
            juce::Random random (7);
            juce::int64 pushed = 0;
            const auto start = juce::Time::getMillisecondCounterHiRes();

            while (pushed < 200000)
            {
                const auto n = 1 + random.nextInt (8192);
                fifo.push (channels, 2, n);
                pushed += n;
            }

            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            note ("pushed " + std::to_string (pushed) + " samples with no consumer in " + format (elapsed, 2) + " ms: "
                  + std::to_string (fifo.getNumReady()) + " buffered, " + std::to_string (fifo.getDroppedSamples()) + " dropped");
            check (fifo.getNumReady() == AnalyzerFifo::capacity - 1 || fifo.getNumReady() == AnalyzerFifo::capacity,
                   "a full FIFO stays bounded at its capacity");
            check (fifo.getDroppedSamples() == pushed - fifo.getNumReady(), "overflow is dropped and counted exactly");

            std::vector<float> out (64);
            check (fifo.pop (out.data(), 64) == 64 && juce::exactlyEqual (out[0], 0.5f), "FIFO stores the mono average of the channels");

            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            AnalyzerFifo streaming;
            juce::int64 total = 0;
            int frames = 0;
            const auto sine = makeSine (1000.0, 0.5f, sampleRate, 8192);

            for (int block = 0; block < 400; ++block)
            {
                const auto n = random.nextInt (4097); // 0..4096, including empty blocks
                const float* mono[] { sine.data() };
                streaming.push (mono, 1, n);
                total += n;

                if (block % 3 == 0)
                    frames += analyzer.pull (streaming);
            }

            frames += analyzer.pull (streaming);
            const auto expected = total >= Analyzer::fftSize ? (int) ((total - Analyzer::fftSize) / Analyzer::hopSize + 1) : 0;
            note ("variable blocks: " + std::to_string (total) + " samples, " + std::to_string (frames) + " frames, "
                  + std::to_string (streaming.getDroppedSamples()) + " dropped");
            check (streaming.getDroppedSamples() == 0 && frames == expected, "variable block sizes with a regular consumer lose nothing");
        }

        // Averaging is tied to Freeze: idle while off, fresh on engage, stops and clears on disengage.
        {
            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            const auto floorOnly = [] (const std::vector<float>& v)
            {
                return std::all_of (v.begin(), v.end(), [] (float x) { return juce::exactlyEqual (x, Analyzer::floorDb); });
            };
            const auto lowTone = makeSine (300.0, 0.5f, sampleRate, 96000);
            const auto highTone = makeSine (5000.0, 0.5f, sampleRate, 96000);
            const auto other = makeSine (9000.0, 0.9f, sampleRate, 48000);

            // Freeze off: 2 s of audio, nothing accumulates (live view and peak hold still run).
            analyzer.pushSamples (lowTone.data(), (int) lowTone.size());
            note ("Freeze off, 2 s of 300 Hz: " + std::to_string (analyzer.getAccumulatedFrames()) + " averaged frames, "
                  + std::to_string (analyzer.getPeakFrames()) + " live/peak frames");
            check (! analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && juce::exactlyEqual (analyzer.getAccumulatedSeconds(), 0.0)
                       && floorOnly (analyzer.getAverageDb()),
                   "while Freeze is off the average is idle and empty");
            check (analyzer.getPeakFrames() > 80 && Analyzer::levelAt (analyzer.getLiveDb(), 300.0, sampleRate, 1.0 / 12.0) > -14.0f
                       && Analyzer::levelAt (analyzer.getPeakDb(), 300.0, sampleRate, 1.0 / 12.0) > -14.0f,
                   "the live spectrum and peak hold still run while Freeze is off");

            // Freeze on: starts from zero, then accumulates.
            analyzer.freeze();
            check (analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && floorOnly (analyzer.getAverageDb()),
                   "engaging Freeze starts the average at zero (nothing from before)");

            analyzer.pushSamples (highTone.data(), (int) highTone.size());
            analyzer.pushSamples (lowTone.data(), (int) lowTone.size());
            const auto average = analyzer.getAverageDb();
            const auto lowLevel  = Analyzer::levelAt (average, 300.0, sampleRate, 1.0 / 12.0);
            const auto highLevel = Analyzer::levelAt (average, 5000.0, sampleRate, 1.0 / 12.0);
            const auto between   = Analyzer::levelAt (average, 1500.0, sampleRate, 1.0 / 12.0);
            const auto liveHigh  = Analyzer::levelAt (analyzer.getLatestFrameDb(), 5000.0, sampleRate, 1.0 / 12.0);
            note ("frozen, two tones in sequence (2 s each): average at 300 Hz " + format (lowLevel) + ", 5 kHz " + format (highLevel)
                  + ", 1.5 kHz " + format (between) + " dBFS; latest frame at 5 kHz " + format (liveHigh) + " dBFS; "
                  + format (analyzer.getAccumulatedSeconds()) + " s accumulated");
            check (lowLevel > -14.0f && highLevel > -14.0f, "the average contains both tones played while frozen");
            check (between < -60.0f, "the average stays low between the tones");
            check (liveHigh < -60.0f, "the latest frame only shows the current tone");
            check (std::abs (analyzer.getAccumulatedSeconds() - (double) analyzer.getAccumulatedFrames() * Analyzer::hopSize / sampleRate) < 1.0e-9
                       && analyzer.getAccumulatedSeconds() > 3.9 && analyzer.getAccumulatedSeconds() < 4.1,
                   "accumulated time counts only the frames since Freeze was engaged (hop 1024)");

            const auto framesBefore = analyzer.getAccumulatedFrames();
            const auto nineBefore = Analyzer::levelAt (analyzer.getAverageDb(), 9000.0, sampleRate, 1.0 / 12.0);
            analyzer.pushSamples (other.data(), (int) other.size());
            const auto nineAfter = Analyzer::levelAt (analyzer.getAverageDb(), 9000.0, sampleRate, 1.0 / 12.0);
            note ("while frozen, a 1 s 9 kHz tone moves the overlay at 9 kHz from " + format (nineBefore) + " to "
                  + format (nineAfter) + " dBFS; " + std::to_string (analyzer.getAccumulatedFrames() - framesBefore) + " frames added");
            check (analyzer.getAccumulatedFrames() > framesBefore && nineAfter > nineBefore + 40.0f,
                   "the frozen overlay keeps accumulating live audio");
            check (Analyzer::levelAt (analyzer.getLiveDb(), 9000.0, sampleRate, 1.0 / 12.0) > -3.0f,
                   "the live spectrum keeps moving while frozen");

            const auto framesFrozen = analyzer.getAccumulatedFrames();
            analyzer.freeze();
            check (analyzer.getAccumulatedFrames() == framesFrozen, "freeze() while already frozen does not restart the average");

            // Freeze off: stops and clears; stays idle with more audio.
            analyzer.unfreeze();
            check (! analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && floorOnly (analyzer.getAverageDb()),
                   "disengaging Freeze stops and clears the average");
            analyzer.pushSamples (lowTone.data(), (int) lowTone.size());
            check (analyzer.getAccumulatedFrames() == 0 && floorOnly (analyzer.getAverageDb()),
                   "no averaging after Freeze is turned off");

            // Freeze on again: a fresh average without the earlier material.
            analyzer.freeze();
            analyzer.pushSamples (other.data(), (int) other.size());
            const auto freshLow = Analyzer::levelAt (analyzer.getAverageDb(), 300.0, sampleRate, 1.0 / 12.0);
            const auto freshNine = Analyzer::levelAt (analyzer.getAverageDb(), 9000.0, sampleRate, 1.0 / 12.0);
            note ("re-frozen with 1 s of 9 kHz: average at 9 kHz " + format (freshNine) + ", 300 Hz " + format (freshLow) + " dBFS");
            check (freshNine > -3.0f && freshLow < -100.0f && analyzer.getAccumulatedSeconds() < 1.01,
                   "re-engaging Freeze starts a fresh average");

            // Clear while frozen restarts the average and the peak hold; Freeze stays on.
            analyzer.clear();
            check (analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && analyzer.getPeakFrames() == 0
                       && floorOnly (analyzer.getAverageDb()) && floorOnly (analyzer.getPeakDb()),
                   "clear while frozen restarts the average and peak hold and keeps Freeze on");
            analyzer.pushSamples (other.data(), (int) other.size());
            check (analyzer.getAccumulatedFrames() > 0, "after clear the average rebuilds while frozen");

            // Clear while not frozen still has a job: it resets the peak hold.
            analyzer.unfreeze();
            analyzer.pushSamples (lowTone.data(), (int) lowTone.size());
            analyzer.clear();
            check (analyzer.getPeakFrames() == 0 && floorOnly (analyzer.getPeakDb()) && analyzer.getAccumulatedFrames() == 0,
                   "clear while not frozen resets the peak hold");

            analyzer.freeze();
            analyzer.pushSamples (other.data(), (int) other.size());
            analyzer.setSampleRate (44100.0);
            check (analyzer.getAccumulatedFrames() == 0 && floorOnly (analyzer.getLiveDb()), "a sample-rate change resets the analysis");
        }
    }

    void configureBusyEq (DooblrQAudioProcessor& processor)
    {
        configureDynamicBand (processor, -9.0f);
        setBand (processor, 0, params::band::enabled, 1.0f);
        setBand (processor, 5, params::band::enabled, 1.0f);
        setBand (processor, 5, params::band::gain, 4.0f);
        setParameter (processor, params::outputGain, -2.0f);
    }

    void testAnalyzerIsTransparent()
    {
        constexpr double sampleRate = 48000.0;
        const std::vector<int> blocks { 512, 17, 1024, 3, 4096, 160, 0, 777 };

        juce::AudioBuffer<float> input (2, 48000 * 3);
        fillWithNoise (input, 99, 0.6f);

        const auto render = [&] (bool analyzerOn, DooblrQAudioProcessor::AnalyzerSource source, juce::int64& fed)
        {
            DooblrQAudioProcessor processor;
            configureBusyEq (processor);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 4096);
            processor.setAnalyzerSource (source);
            processor.setAnalyzerRunning (analyzerOn);

            juce::AudioBuffer<float> buffer (input);
            processInBlocks (processor, buffer, blocks);
            fed = processor.getAnalyzerFifo().getNumReady() + processor.getAnalyzerFifo().getDroppedSamples();
            return buffer;
        };

        juce::int64 fedOff = 0, fedPost = 0, fedPre = 0;
        const auto off  = render (false, DooblrQAudioProcessor::AnalyzerSource::postEq, fedOff);
        const auto post = render (true, DooblrQAudioProcessor::AnalyzerSource::postEq, fedPost);
        const auto pre  = render (true, DooblrQAudioProcessor::AnalyzerSource::preEq, fedPre);

        note ("samples fed to the analyzer FIFO: off " + std::to_string (fedOff) + ", post " + std::to_string (fedPost)
              + ", pre " + std::to_string (fedPre));
        check (fedOff == 0, "nothing is pushed while no editor shows the analyzer");
        check (fedPost == input.getNumSamples() && fedPre == input.getNumSamples(), "every processed sample reaches the FIFO while running");
        check (bitIdentical (off, post, 2), "processBlock output is bit-identical with the post-EQ analyzer on vs off");
        check (bitIdentical (off, pre, 2), "processBlock output is bit-identical with the pre-EQ analyzer on vs off");

        // Pre vs post: output gain shows up only in the post-EQ analysis.
        const auto analyse = [&] (DooblrQAudioProcessor::AnalyzerSource source)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::outputGain, -12.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            processor.setAnalyzerSource (source);
            processor.setAnalyzerRunning (true);
            Analyzer analyzer;
            analyzer.setSampleRate (sampleRate);
            analyzer.freeze(); // the long-term average only runs while frozen
            juce::AudioBuffer<float> buffer (input);

            for (int start = 0; start < buffer.getNumSamples(); start += 8192)
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, std::min (8192, buffer.getNumSamples() - start));
                processInBlocks (processor, view, { 512 });
                analyzer.pull (processor.getAnalyzerFifo());
            }

            return meanDb (analyzer.getAverageDb(), sampleRate, 200.0, 10000.0);
        };

        const auto preLevel = analyse (DooblrQAudioProcessor::AnalyzerSource::preEq);
        const auto postLevel = analyse (DooblrQAudioProcessor::AnalyzerSource::postEq);
        note ("noise through -12 dB output gain: pre " + format (preLevel) + ", post " + format (postLevel) + " dBFS/bin");
        check (std::abs ((preLevel - postLevel) - 12.0f) < 0.2f, "post-EQ analysis includes the output gain, pre-EQ does not");
    }

    //==============================================================================
    // Tilt band
    void testTilt()
    {
        constexpr double sampleRate = 48000.0;
        const auto tiltDb = [] (double frequency, double gain, double q)
        {
            const auto c = dsp::makeEqCoefficients (dsp::FilterType::tilt, 1000.0, q, gain, sampleRate);
            return juce::Decibels::gainToDecibels (dsp::magnitudeAt (c, frequency, sampleRate), -200.0);
        };

        check (params::filterTypeNames().size() == dsp::numFilterTypes && params::filterTypeNames()[6] == "Tilt"
                   && (int) dsp::FilterType::tilt == 6,
               "Tilt is appended as type index 6");

        note ("display math, +6 dB tilt at 1 kHz, Q 0.707: 20 Hz " + format (tiltDb (20.0, 6.0, 0.707), 3) + ", 1 kHz "
              + format (tiltDb (1000.0, 6.0, 0.707), 3) + ", 20 kHz " + format (tiltDb (20000.0, 6.0, 0.707), 3) + " dB");
        check (std::abs (tiltDb (20.0, 6.0, 0.707) + 3.0) < 0.05, "+6 dB tilt: -3 dB at 20 Hz (display math)");
        check (std::abs (tiltDb (1000.0, 6.0, 0.707)) < 0.001, "+6 dB tilt: 0 dB at the pivot (display math)");
        check (std::abs (tiltDb (20000.0, 6.0, 0.707) - 3.0) < 0.05, "+6 dB tilt: +3 dB at 20 kHz (display math)");

        auto mirrored = true;
        for (const auto f : { 30.0, 200.0, 700.0, 1000.0, 1500.0, 5000.0, 16000.0 })
            mirrored = mirrored && std::abs (tiltDb (f, 6.0, 0.707) + tiltDb (f, -6.0, 0.707)) < 0.001;
        check (mirrored, "negative tilt mirrors positive tilt exactly (display math)");

        note ("Q shapes the transition: +6 dB tilt at 2 kHz is " + format (tiltDb (2000.0, 6.0, 0.3), 2) + " dB at Q 0.3, "
              + format (tiltDb (2000.0, 6.0, 0.707), 2) + " dB at Q 0.707, " + format (tiltDb (2000.0, 6.0, 2.0), 2) + " dB at Q 2");
        check (tiltDb (2000.0, 6.0, 0.3) < tiltDb (2000.0, 6.0, 0.707) - 0.3, "lower Q gives a gentler transition");

        // Audio matches the display math.
        for (const auto gain : { 6.0f, -6.0f })
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::type, (float) dsp::FilterType::tilt);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            setBand (processor, 2, params::band::gain, gain);
            setBand (processor, 2, params::band::q, 0.707f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            std::string line = "audio, " + format (gain, 0) + " dB tilt at 1 kHz:";
            auto matches = true, shape = true;

            for (const auto f : { 30.0, 1000.0, 15000.0 })
            {
                processor.reset();
                const auto measured = measureSineGainDb (processor, f, 0.25f, sampleRate, 1.0);
                const auto expected = tiltDb (f, gain, 0.707);
                const auto ideal = f < 500.0 ? -gain / 2.0 : (f > 2000.0 ? gain / 2.0 : 0.0);
                line += " " + format (f, 0) + " Hz " + format (measured, 3) + " dB (display " + format (expected, 3) + ")";
                matches = matches && std::abs (measured - expected) < 0.05;
                shape = shape && std::abs (measured - ideal) < 0.2;
            }

            note (line);
            check (matches, "measured tilt matches the display math within 0.05 dB (" + format (gain, 0) + " dB)");
            check (shape, "measured tilt is -gain/2 low, 0 at the pivot, +gain/2 high (" + format (gain, 0) + " dB)");
        }

        // Dynamic tilt: broadband detector, so loud material changes the tilt.
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::type, (float) dsp::FilterType::tilt);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            setBand (processor, 2, params::band::gain, 0.0f);
            setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, 2, params::band::range, -8.0f);
            setBand (processor, 2, params::band::threshold, -30.0f);
            setBand (processor, 2, params::band::attack, 2.0f);
            setBand (processor, 2, params::band::release, 50.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            const auto quiet = measureSineGainDb (processor, 8000.0, 0.003f, sampleRate, 0.5);
            const auto quietGain = processor.getBandDynamicGainDb (2);
            const auto loudHigh = measureSineGainDb (processor, 8000.0, 0.5f, sampleRate, 0.5);
            const auto loudGain = processor.getBandDynamicGainDb (2);
            const auto loudLow = measureSineGainDb (processor, 60.0, 0.5f, sampleRate, 0.5);
            note ("dynamic tilt (range -8 dB, threshold -30 dB): quiet 8 kHz " + format (quiet) + " dB (dyn " + format (quietGain)
                  + "), loud 8 kHz " + format (loudHigh) + " dB (dyn " + format (loudGain) + "), loud 60 Hz " + format (loudLow) + " dB");
            check (std::abs (quiet) < 0.1, "quiet material leaves a dynamic tilt flat");
            check (loudGain < -7.0f && loudHigh < -3.5 && loudLow > 3.5, "loud material tilts the band darker (highs down, lows up)");
            check (std::abs (loudHigh - (double) loudGain / 2.0) < 0.3, "dynamic tilt moves the high end by half the dynamic gain");
        }

        // Display: the curve passes through the handle at the 0 dB pivot; vertical drag sets the amount.
        {
            using Curve = dooblrq::ui::ResponseCurveComponent;
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::type, (float) dsp::FilterType::tilt);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1300, 760);
            auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve"));
            check (curve != nullptr, "response curve exists");

            if (curve != nullptr)
            {
                const auto start = curve->handlePosition (2);
                const auto threeDbUp = curve->decibelsToY (3.0) - curve->decibelsToY (0.0);
                simulateDrag (*curve, start, start + juce::Point<float> (0.0f, threeDbUp));
                const auto s = processor.getBandSettings (2);
                note ("tilt handle dragged up by 3 dB on the scale: enabled " + std::string (s.enabled ? "yes" : "no") + ", tilt "
                      + format (s.gainDb) + " dB, pivot " + format (s.frequency, 1) + " Hz");
                check (s.enabled && std::abs (s.gainDb - 6.0f) < 0.1f, "dragging a tilt handle up 3 dB sets +6 dB of tilt");
                check (std::abs (s.frequency - 1000.0f) < 15.0f, "a vertical drag keeps the pivot");
                check (std::abs (curve->handlePosition (2).y - curve->decibelsToY (0.0)) < 0.5f,
                       "the tilt handle sits on the 0 dB pivot of the curve");

                auto curveMatches = true;
                for (const auto f : { 40.0, 1000.0, 12000.0 })
                    curveMatches = curveMatches
                                   && std::abs (curve->responseDecibelsAt (f, false) - tiltDb (f, (double) s.gainDb, (double) s.q)) < 0.01;
                check (curveMatches, "the drawn tilt curve uses the same math as the audio");
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-tilt");
            }

            processor.editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    void testLegacyStateRestores()
    {
        // A session saved before Tilt existed (stateVersion 1, six types, indices 0-5).
        // The value tree stores choice parameters as their index, so indices must not move.
        const char* legacy = R"(<?xml version="1.0" encoding="UTF-8"?>
<DooblrQState stateVersion="1">
  <PARAM id="b1_enabled" value="1.0"/><PARAM id="b1_type" value="3.0"/><PARAM id="b1_freq" value="45.0"/>
  <PARAM id="b2_enabled" value="1.0"/><PARAM id="b2_type" value="1.0"/><PARAM id="b2_gain" value="2.5"/>
  <PARAM id="b3_enabled" value="1.0"/><PARAM id="b3_type" value="5.0"/><PARAM id="b3_freq" value="2345.0"/><PARAM id="b3_q" value="4.5"/>
  <PARAM id="b4_enabled" value="0.0"/><PARAM id="b4_type" value="4.0"/>
  <PARAM id="b5_enabled" value="1.0"/><PARAM id="b5_type" value="0.0"/><PARAM id="b5_gain" value="-7.5"/>
  <PARAM id="b6_enabled" value="1.0"/><PARAM id="b6_type" value="2.0"/><PARAM id="b6_gain" value="3.0"/>
  <PARAM id="output_gain" value="-3.0"/><PARAM id="stereo_link" value="0.0"/>
</DooblrQState>)";

        const auto xml = juce::parseXML (juce::String (legacy));
        check (xml != nullptr, "legacy state parses");

        if (xml == nullptr)
            return;

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*xml, block);
        DooblrQAudioProcessor processor;
        setParameter (processor, params::phaseMode, (float) params::PhaseMode::linearPhase);
        processor.setStateInformation (block.getData(), (int) block.getSize());
        check (juce::roundToInt (getParameter (processor, params::phaseMode)) == 0,
               "parameters missing from an old session load at their defaults (Zero Latency)");

        using T = dsp::FilterType;
        const T expected[] { T::lowCut, T::lowShelf, T::notch, T::highCut, T::bell, T::highShelf };
        auto typesMatch = true;
        std::string types;

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = processor.getBandSettings (b);
            typesMatch = typesMatch && s.type == expected[b];
            types += (b > 0 ? ", " : "") + params::filterTypeNames()[(int) s.type].toStdString();
        }

        note ("legacy types restored as: " + types);
        check (typesMatch, "every pre-Tilt band type restores to the same type");
        check (std::abs (processor.getBandSettings (2).frequency - 2345.0f) < 0.01f && std::abs (processor.getBandSettings (4).gainDb + 7.5f) < 0.01f
                   && ! processor.getBandSettings (3).enabled && std::abs (getParameter (processor, params::outputGain) + 3.0f) < 0.01f,
               "other legacy values restore");

        // And a Tilt band round-trips.
        setBand (processor, 3, params::band::type, (float) dsp::FilterType::tilt);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        DooblrQAudioProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getBandSettings (3).type == dsp::FilterType::tilt, "a Tilt band survives save/restore");

        // A v0.4.0 session: it had per-band stereo width (bN_width), which no longer exists.
        auto v040 = restored.getParameterState().copyState();
        v040.setProperty ("stateVersion", 1, nullptr);
        for (int b = 0; b < params::numBands; ++b)
        {
            juce::ValueTree width ("PARAM");
            width.setProperty ("id", params::bandId (b, "width"), nullptr);
            width.setProperty ("value", 40.0 + 20.0 * b, nullptr);
            v040.appendChild (width, nullptr);
        }
        v040.getChildWithProperty ("id", params::phaseMode).setProperty ("value", 2.0, nullptr);
        v040.getChildWithProperty ("id", params::bandId (1, params::band::q)).setProperty ("value", 2.5, nullptr);

        juce::MemoryBlock v040Block;
        juce::AudioProcessor::copyXmlToBinary (*v040.createXml(), v040Block);
        DooblrQAudioProcessor fromV040;
        fromV040.setStateInformation (v040Block.getData(), (int) v040Block.getSize());
        juce::MemoryBlock resaved;
        fromV040.getStateInformation (resaved);
        const auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        check (fromV040.getBandSettings (3).type == dsp::FilterType::tilt && std::abs (fromV040.getBandSettings (1).q - 2.5f) < 1.0e-4f
                   && juce::roundToInt (getParameter (fromV040, params::phaseMode)) == 2,
               "a v0.4.0 state with bN_width parameters loads (other values restored)");
        check (resavedXml != nullptr && ! resavedXml->toString().contains ("_width"), "the obsolete width parameters are dropped, not re-saved");
    }

    //==============================================================================
    // Editor: version label and analyzer controls
    std::vector<float> makeMusicLike (int numSamples, double sampleRate, juce::int64 seed, float brightness)
    {
        std::vector<float> out ((size_t) numSamples, 0.0f);
        juce::Random random (seed);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double bass[] { 55.0, 73.42, 49.0, 65.41 };
        const double chord[] { 220.0, 277.18, 329.63, 440.0 };

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = i / sampleRate;
            const auto beat = std::fmod (t, 0.5);
            const auto bar = (int) (t / 2.0) % 4;

            // Pink noise (Paul Kellet) as a bed.
            const auto white = random.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + white * 0.0555179; b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520; b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522; b5 = -0.7616 * b5 - white * 0.0168980;
            const auto pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11;
            b6 = white * 0.115926;

            auto v = 0.05 * pink;
            const auto kick = std::exp (-beat * 18.0);
            v += 0.35 * kick * std::sin (twoPi * (45.0 + 60.0 * std::exp (-beat * 40.0)) * beat);

            for (int h = 1; h <= 4; ++h)
                v += 0.12 / h * std::sin (twoPi * bass[bar] * h * t);

            for (const auto pitch : chord)
                for (int h = 1; h <= 8; ++h)
                    v += 0.03 / (h * h) * std::pow (brightness, h - 1) * std::sin (twoPi * pitch * h * t);

            const auto hat = std::fmod (t + 0.25, 0.25);
            v += 0.06 * brightness * std::exp (-hat * 60.0) * white;
            out[(size_t) i] = (float) v;
        }

        return out;
    }

    /** Plays a mono signal through the processor (as stereo) in host-sized blocks while the
        editor's timer would pull the analyzer; the timer is driven by hand here. */
    void playThroughEditor (DooblrQAudioProcessor& processor, dooblrq::ui::ResponseCurveComponent& curve,
                            const std::vector<float>& signal)
    {
        constexpr int chunk = 2048;

        for (int start = 0; start < (int) signal.size(); start += chunk)
        {
            const auto n = std::min (chunk, (int) signal.size() - start);
            juce::AudioBuffer<float> buffer (2, n);
            buffer.copyFrom (0, 0, signal.data() + start, n);
            buffer.copyFrom (1, 0, signal.data() + start, n);
            processInBlocks (processor, buffer, { 512 });
            curve.updateAnalyzer();
            curve.updateDisplay ((double) n / 48000.0); // as the 60 Hz timer would, in audio time
        }
    }

    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        if (parent.getComponentID() == id)
            return &parent;

        for (auto* child : parent.getChildren())
            if (auto* found = findById (*child, id))
                return found;

        return nullptr;
    }

    void testEditorAnalyzerAndVersion()
    {
        constexpr double sampleRate = 48000.0;
        DooblrQAudioProcessor processor;
        setBand (processor, 0, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::gain, -4.0f);
        setBand (processor, 4, params::band::enabled, 1.0f);
        setBand (processor, 4, params::band::gain, 3.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* curve = dynamic_cast<dooblrq::ui::ResponseCurveComponent*> (findById (*editor, "responseCurve"));
        auto* version = dynamic_cast<juce::Label*> (findById (*editor, "version"));
        auto* freeze = dynamic_cast<juce::Button*> (findById (*editor, "freeze"));
        auto* clear = dynamic_cast<juce::Button*> (findById (*editor, "clear"));
        auto* spectrum = dynamic_cast<juce::Button*> (findById (*editor, "spectrum"));
        auto* peak = dynamic_cast<juce::Button*> (findById (*editor, "analyzerPeak"));
        auto* readout = dynamic_cast<juce::Label*> (findById (*editor, "analyzerReadout"));
        check (curve && version && freeze && clear && spectrum && peak && readout, "analyzer toolbar controls exist");

        if (! (curve && version && freeze && clear && spectrum && peak && readout))
            return;

        // The label comes from the build's version, which comes from project(VERSION) in CMake.
        note ("version label: " + version->getText().toStdString() + ", JucePlugin_VersionString " JucePlugin_VersionString
              ", CMake PROJECT_VERSION " DOOBLRQ_PROJECT_VERSION);
        check (version->getText() == juce::String ("v") + JucePlugin_VersionString, "version label shows JucePlugin_VersionString");
        check (juce::String (JucePlugin_VersionString) == DOOBLRQ_PROJECT_VERSION, "plugin version equals the CMake project version");

        check (processor.isAnalyzerRunning(), "opening the editor starts the analyzer feed");

        const auto clickToggle = [] (juce::Button& button)
        {
            button.setToggleState (! button.getToggleState(), juce::dontSendNotification);
            button.onClick();
        };

        auto& analyzer = processor.getSpectrumAnalyzer();
        analyzer.clear();

        // Material A: darker mix; capture it.
        playThroughEditor (processor, *curve, makeMusicLike (48000 * 6, sampleRate, 1, 0.35f));
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        dooblrEditor->updateAnalyzerControls(); // what the editor timer does
        note ("after 6 s of material: readout \"" + readout->getText().toStdString() + "\", "
              + std::to_string (analyzer.getAccumulatedFrames()) + " frames, "
              + std::to_string (processor.getAnalyzerFifo().getDroppedSamples()) + " samples dropped");
        check (analyzer.getPeakFrames() > 250, "the editor pulls audio into the analyzer");
        check (analyzer.getAccumulatedFrames() == 0 && readout->getText() == "Live",
               "with Freeze off nothing is averaged and the readout says \"Live\"");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-spectrum-live");

        check (readout->getFont().getTypefaceName() == juce::Font::getDefaultMonospacedFontName(),
               "the readout uses fixed-width digits");

        const auto allFinite = [] (const std::vector<float>& v)
        {
            return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
        };
        const auto maxDifference = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            auto d = 0.0f;
            for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
                d = std::max (d, std::abs (a[i] - b[i]));
            return d;
        };

        clickToggle (*freeze);
        curve->updateDisplay (1.0 / 60.0);
        dooblrEditor->updateAnalyzerControls();
        const auto frozenText = readout->getText();
        note ("after Freeze: readout \"" + frozenText.toStdString() + "\"");
        check (analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && frozenText == "Frozen: 0.0 s",
               "Freeze starts a fresh average: readout \"Frozen: 0.0 s\"");
        const auto overlayAtFreeze = curve->getOverlayDisplay().getLevels();

        // Material B: brighter mix; the overlay keeps updating (long-term average), the readout counts on.
        clickToggle (*peak);
        const auto secondsAtFreeze = analyzer.getAccumulatedSeconds();
        std::vector<double> readoutValues;
        auto overlayStepMax = 0.0f;
        auto previousOverlay = overlayAtFreeze;
        const auto materialB = makeMusicLike (48000 * 3, sampleRate, 2, 0.9f);

        for (int part = 0; part < 6; ++part)
        {
            const auto begin = materialB.begin() + part * 24000;
            playThroughEditor (processor, *curve, std::vector<float> (begin, begin + 24000));
            dooblrEditor->updateAnalyzerControls();
            readoutValues.push_back (readout->getText().fromFirstOccurrenceOf ("Frozen: ", false, false).getDoubleValue());
            overlayStepMax = std::max (overlayStepMax, maxDifference (previousOverlay, curve->getOverlayDisplay().getLevels()));
            previousOverlay = curve->getOverlayDisplay().getLevels();
        }

        const auto overlayChange = maxDifference (overlayAtFreeze, curve->getOverlayDisplay().getLevels());
        note ("3 s of brighter material while frozen: overlay moved up to " + format (overlayChange) + " dB (largest change per 0.5 s "
              + format (overlayStepMax) + " dB); readout " + format (readoutValues.front(), 1) + " -> " + format (readoutValues.back(), 1)
              + " s; average now " + format (analyzer.getAccumulatedSeconds(), 1) + " s");
        check (analyzer.isFrozen() && overlayChange > 1.0f, "the frozen overlay keeps updating while frozen");
        check (std::abs (readoutValues.back() - analyzer.getAccumulatedSeconds()) < 0.06 && readoutValues.back() > 2.8
                   && readoutValues.back() < 3.1,
               "the readout counts from the moment Freeze was engaged");
        const auto& overlayLevels = curve->getOverlayDisplay().getLevels();
        check (*std::max_element (overlayLevels.begin(), overlayLevels.end()) > -80.0f,
               "the overlay shows the audio played since Freeze was engaged");
        check (analyzer.getAccumulatedSeconds() > secondsAtFreeze + 2.9, "the average keeps accumulating while frozen");
        check (std::is_sorted (readoutValues.begin(), readoutValues.end()) && readoutValues.back() > readoutValues.front() + 2.0,
               "the frozen readout counts up");
        check (allFinite (curve->getLiveDisplay().getLevels()) && allFinite (curve->getOverlayDisplay().getLevels())
                   && allFinite (analyzer.getAverageDb()),
               "display levels are finite (no NaN/Inf)");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-spectrum-frozen");
        dooblrEditor->selectBand (2); // shows its bandwidth handles over the spectrum and frozen overlay
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-overview");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.5.1");

        clickToggle (*freeze);
        curve->updateDisplay (1.0 / 60.0);
        dooblrEditor->updateAnalyzerControls();
        check (! analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && readout->getText() == "Live",
               "Freeze off stops and clears the average; readout back to \"Live\"");
        playThroughEditor (processor, *curve, makeMusicLike (48000, sampleRate, 3, 0.5f));
        check (analyzer.getAccumulatedFrames() == 0, "nothing accumulates after Freeze is turned off");
        clickToggle (*freeze);
        dooblrEditor->updateAnalyzerControls();
        check (analyzer.isFrozen() && analyzer.getAccumulatedFrames() == 0 && readout->getText() == "Frozen: 0.0 s",
               "Freeze on again starts a fresh average at 0 s");
        playThroughEditor (processor, *curve, makeMusicLike (24000, sampleRate, 5, 0.5f));
        clear->onClick();
        curve->updateDisplay (1.0 / 60.0);
        dooblrEditor->updateAnalyzerControls();
        check (analyzer.isFrozen() && freeze->getToggleState() && analyzer.getAccumulatedFrames() == 0
                   && readout->getText() == "Frozen: 0.0 s",
               "Clear restarts the average and the readout; Freeze stays on");
        playThroughEditor (processor, *curve, makeMusicLike (12000, sampleRate, 4, 0.5f));
        check (analyzer.getAccumulatedFrames() > 0 && allFinite (curve->getOverlayDisplay().getLevels()),
               "after Clear the overlay restarts from the new audio");
        clickToggle (*freeze);
        check (! analyzer.isFrozen() && ! freeze->getToggleState(), "Freeze off hides the overlay");

        clickToggle (*spectrum);
        check (! processor.isAnalyzerRunning() && ! freeze->isEnabled(), "turning the spectrum off stops the feed and disables its controls");
        clickToggle (*spectrum);
        check (processor.isAnalyzerRunning(), "turning it back on restarts the feed");

        processor.editorBeingDeleted (editor.get());
        editor.reset();
        check (! processor.isAnalyzerRunning(), "closing the editor stops the analyzer feed");
    }

    //==============================================================================
    // Phase modes
    using PhaseMode = params::PhaseMode;

    const char* phaseName (PhaseMode mode)
    {
        switch (mode)
        {
            case PhaseMode::naturalPhase: return "Natural";
            case PhaseMode::linearPhase:  return "Linear";
            case PhaseMode::zeroLatency:  break;
        }

        return "Zero";
    }

    constexpr PhaseMode allPhaseModes[] { PhaseMode::zeroLatency, PhaseMode::naturalPhase, PhaseMode::linearPhase };

    void testPhaseModeLatency()
    {
        struct Expected { double rate; int natural, linear; };
        const Expected expected[] { { 44100.0, 1024, 4352 }, { 48000.0, 1024, 4352 }, { 96000.0, 1792, 8448 } };

        for (const auto& e : expected)
        {
            std::string line = format (e.rate / 1000.0, 1) + " kHz latency:";

            for (const auto mode : allPhaseModes)
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                prepare (processor, juce::AudioChannelSet::stereo(), e.rate, 512);
                const auto want = mode == PhaseMode::zeroLatency ? 0 : (mode == PhaseMode::naturalPhase ? e.natural : e.linear);
                line += std::string (" ") + phaseName (mode) + " " + std::to_string (processor.getLatencySamples()) + " smp ("
                        + format (1000.0 * processor.getLatencySamples() / e.rate, 1) + " ms)";
                check (processor.getLatencySamples() == want && DooblrQAudioProcessor::latencyFor (mode, e.rate) == want,
                       std::string ("reported latency, ") + phaseName (mode) + " at " + format (e.rate, 0) + " Hz");
            }

            note (line);
        }

        // Changing the mode while running updates the reported latency at the next block.
        DooblrQAudioProcessor processor;
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        setParameter (processor, params::phaseMode, (float) PhaseMode::linearPhase);
        processInBlocks (processor, buffer, { 512 });
        check (processor.getLatencySamples() == 4352 && processor.getActivePhaseMode() == PhaseMode::linearPhase,
               "switching to Linear Phase while running reports 4352 samples");
        setParameter (processor, params::phaseMode, (float) PhaseMode::zeroLatency);
        processInBlocks (processor, buffer, { 512 });
        check (processor.getLatencySamples() == 0, "switching back to Zero Latency reports 0");
    }

    void testPhaseModeResponse()
    {
        constexpr double sampleRate = 48000.0;

        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            setBand (processor, 3, params::band::enabled, 1.0f);
            setBand (processor, 3, params::band::type, (float) dsp::FilterType::bell);
            setBand (processor, 3, params::band::frequency, 1000.0f);
            setBand (processor, 3, params::band::gain, 12.0f);
            setBand (processor, 3, params::band::q, 1.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            const auto centre = measureSineGainDb (processor, 1000.0, 0.25f, sampleRate, 1.0);
            processor.reset();
            const auto low = measureSineGainDb (processor, 60.0, 0.25f, sampleRate, 1.0);
            processor.reset();
            const auto high = measureSineGainDb (processor, 16000.0, 0.25f, sampleRate, 1.0);
            note (std::string (phaseName (mode)) + ": +12 dB bell at 1 kHz measures " + format (centre, 3) + " dB at 1 kHz, "
                  + format (low, 3) + " dB at 60 Hz, " + format (high, 3) + " dB at 16 kHz");
            check (std::abs (centre - 12.0) < 0.1, std::string (phaseName (mode)) + ": +12 dB bell measures +12 dB at its centre");
            check (std::abs (low) < 0.3 && std::abs (high) < 0.3, std::string (phaseName (mode)) + ": far from the bell stays flat");
        }

        // Near Nyquist: the zero-latency (bilinear) bell is cramped, the FIR modes follow the analogue shape.
        constexpr double rate = 44100.0;
        const auto proto = dsp::makeEqPrototype (dsp::FilterType::bell, 0.7, 9.0);
        const auto analogDb = juce::Decibels::gainToDecibels (std::abs (dsp::analogResponseAt (proto, 15000.0, 20000.0)));
        const auto digitalDb = juce::Decibels::gainToDecibels (
            dsp::magnitudeAt (dsp::makeEqCoefficients (dsp::FilterType::bell, 15000.0, 0.7, 9.0, rate), 20000.0, rate));
        std::string line = "+9 dB bell at 15 kHz (Q 0.7, 44.1 kHz), level at 20 kHz: analogue target " + format (analogDb)
                           + " dB, bilinear " + format (digitalDb) + " dB; measured";

        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            setBand (processor, 3, params::band::enabled, 1.0f);
            setBand (processor, 3, params::band::frequency, 15000.0f);
            setBand (processor, 3, params::band::gain, 9.0f);
            setBand (processor, 3, params::band::q, 0.7f);
            prepare (processor, juce::AudioChannelSet::stereo(), rate, 512);
            const auto measured = measureSineGainDb (processor, 20000.0, 0.25f, rate, 1.0);
            line += std::string (" ") + phaseName (mode) + " " + format (measured);
            const auto expected = mode == PhaseMode::zeroLatency ? digitalDb : analogDb;
            check (std::abs (measured - expected) < 0.25,
                   std::string (phaseName (mode)) + ": 20 kHz level matches its design (" + (mode == PhaseMode::zeroLatency ? "bilinear" : "analogue") + ")");
        }

        note (line);
        check (analogDb - digitalDb > 1.0, "the analogue target is not cramped near Nyquist");

        // The analogue prototypes equal the digital filters well below Nyquist (all types).
        auto prototypesMatch = true;
        for (int t = 0; t < dsp::numFilterTypes; ++t)
        {
            const auto type = (dsp::FilterType) t;
            const auto c = dsp::makeEqCoefficients (type, 1000.0, 0.9, 7.0, 192000.0);
            const auto p = dsp::makeEqPrototype (type, 0.9, 7.0);

            for (const auto f : { 100.0, 700.0, 1000.0, 1400.0, 3000.0 })
            {
                if (type == dsp::FilterType::notch && f > 900.0 && f < 1100.0)
                    continue; // both are ~0 at the notch centre

                const auto d = std::abs (dsp::responseAt (c, f, 192000.0));
                const auto a = std::abs (dsp::analogResponseAt (p, 1000.0, f));
                prototypesMatch = prototypesMatch && std::abs (juce::Decibels::gainToDecibels (d / a, -200.0)) < 0.02;
            }
        }
        check (prototypesMatch, "analogue prototypes match the digital filters far below Nyquist (all 7 types, < 0.02 dB)");
    }

    void testLinearPhaseImpulse()
    {
        constexpr double sampleRate = 48000.0;
        DooblrQAudioProcessor processor;
        setParameter (processor, params::phaseMode, (float) PhaseMode::linearPhase);
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::gain, 8.0f);   // bell 400 Hz
        setBand (processor, 5, params::band::enabled, 1.0f);
        setBand (processor, 5, params::band::gain, -6.0f);  // high shelf 10 kHz
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        const auto latency = processor.getLatencySamples();
        juce::AudioBuffer<float> buffer (2, latency * 3);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
        processInBlocks (processor, buffer, { 512 });

        const auto* out = buffer.getReadPointer (0);
        auto peak = 0;
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            if (std::abs (out[i]) > std::abs (out[peak]))
                peak = i;

        const auto half = processor.getFirEngine().getKernelLength() / 2;
        float asymmetry = 0.0f, energyOutside = 0.0f;
        for (int k = 1; k < half; ++k)
            asymmetry = std::max (asymmetry, std::abs (out[latency + k] - out[latency - k]));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (i < latency - half || i > latency + half)
                energyOutside = std::max (energyOutside, std::abs (out[i]));

        note ("linear-phase impulse: peak at sample " + std::to_string (peak) + " (latency " + std::to_string (latency)
              + "), peak " + format (out[peak], 4) + ", max |h[L+k] - h[L-k]| " + juce::String (asymmetry, 9).toStdString()
              + ", max outside the kernel " + juce::String (energyOutside, 9).toStdString());
        check (peak == latency, "the impulse response peaks exactly at the reported latency");
        check (asymmetry < 1.0e-5f, "the impulse response is symmetric about the latency (linear phase)");
        check (energyOutside < 1.0e-6f, "nothing outside the kernel span");

        const auto& kernel = processor.getFirEngine().getKernel();
        auto kernelSymmetric = true;
        for (int k = 1; k < half; ++k)
            kernelSymmetric = kernelSymmetric && std::abs (kernel[(size_t) (half + k)] - kernel[(size_t) (half - k)]) < 1.0e-6f;
        check (kernelSymmetric, "the designed kernel is symmetric");
    }

    void testLongModesNeutralAndBypass()
    {
        constexpr double sampleRate = 48000.0;

        for (const auto mode : { PhaseMode::naturalPhase, PhaseMode::linearPhase })
        {
            for (const auto allEnabled : { false, true })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);

                if (allEnabled)
                    for (int b = 0; b < params::numBands; ++b)
                    {
                        setBand (processor, b, params::band::enabled, 1.0f);
                        setBand (processor, b, params::band::type, (float) dsp::FilterType::bell);
                    }

                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 1024);
                const auto latency = processor.getLatencySamples();
                juce::AudioBuffer<float> input (2, 48000);
                fillWithNoise (input, 5, 0.5f);
                juce::AudioBuffer<float> output (input);
                processInBlocks (processor, output, { 1024, 7, 333, 512 });

                float error = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = latency; i < input.getNumSamples(); ++i)
                        error = std::max (error, std::abs (output.getSample (ch, i) - input.getSample (ch, i - latency)));

                note (std::string (phaseName (mode)) + (allEnabled ? ", all bands on at 0 dB" : ", defaults")
                      + ": max |out[n] - in[n - latency]| = " + juce::String (error, 9).toStdString());
                check (error < 2.0e-6f, std::string (phaseName (mode)) + ": neutral settings are identity after delay compensation");
            }

            // Bypass: input delayed by exactly the latency, and continuous across bypass toggles.
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto latency = processor.getLatencySamples();
            juce::AudioBuffer<float> input (2, 512 * 40);
            fillWithNoise (input, 9, 0.5f);
            juce::AudioBuffer<float> output (input);
            juce::MidiBuffer midi;
            float bypassError = 0.0f, activeError = 0.0f;

            for (int block = 0; block < 40; ++block)
            {
                juce::AudioBuffer<float> view (output.getArrayOfWritePointers(), 2, block * 512, 512);
                const auto bypassed = (block / 5) % 2 == 1;

                if (bypassed)
                    processor.processBlockBypassed (view, midi);
                else
                    processor.processBlock (view, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto n = block * 512 + i;
                        if (n < latency)
                            continue;
                        const auto difference = std::abs (output.getSample (ch, n) - input.getSample (ch, n - latency));
                        (bypassed ? bypassError : activeError) = std::max (bypassed ? bypassError : activeError, difference);
                    }
            }

            note (std::string (phaseName (mode)) + " bypass toggling every 5 blocks: bypassed max error " + juce::String (bypassError, 9).toStdString()
                  + ", active max error " + juce::String (activeError, 9).toStdString());
            check (bypassError == 0.0f, std::string (phaseName (mode)) + ": bypass outputs the input delayed by exactly the latency");
            check (activeError < 2.0e-6f, std::string (phaseName (mode)) + ": bypass/active toggles stay aligned and click-free");
        }
    }

    void testModeSwitchingAndDynamics()
    {
        constexpr double sampleRate = 48000.0;

        {
            DooblrQAudioProcessor processor;
            configureBusyEq (processor);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (3);
            auto finite = true;
            float peak = 0.0f;

            for (int block = 0; block < 400; ++block)
            {
                if (block % 7 == 0)
                    setParameter (processor, params::phaseMode, (float) random.nextInt (3));

                const auto length = 1 + random.nextInt (512);
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, 0, length);
                fillWithNoise (view, block, 0.8f);
                processor.processBlock (view, midi);
                finite = finite && allFinite (view);
                peak = std::max (peak, view.getMagnitude (0, length));
            }

            note ("random phase-mode switching every 7 blocks (400 blocks): peak output " + format (peak, 2));
            check (finite && peak < 8.0f, "switching phase modes during playback stays finite and bounded");
        }

        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            configureDynamicBand (processor, -12.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto quiet = measureSineGainDb (processor, 1000.0, 0.00316f, sampleRate, 1.0);
            const auto loud = measureSineGainDb (processor, 1000.0, 0.5f, sampleRate, 1.0);
            note (std::string (phaseName (mode)) + " dynamics (range -12, threshold -30): quiet " + format (quiet) + " dB, loud "
                  + format (loud) + " dB (dynamic gain " + format (processor.getBandDynamicGainDb (3)) + " dB)");
            check (std::abs (quiet) < 0.2 && loud < -11.0 && loud > -12.5,
                   std::string (phaseName (mode)) + ": dynamics work (quiet unchanged, loud cut by ~the range)");
        }
    }

    void testCpuUsage()
    {
        constexpr double sampleRate = 48000.0;
        constexpr int seconds = 10;
        std::string line = "CPU, 10 s stereo @ 48 kHz, 512-sample blocks, 6 bands on (one dynamic):";

        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            configureBusyEq (processor);
            for (int b = 0; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 1.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (11);
            const auto start = juce::Time::getMillisecondCounterHiRes();

            for (int block = 0; block < seconds * 48000 / 512; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, (random.nextFloat() - 0.5f) * ((block / 40) % 2 == 0 ? 1.0f : 0.05f));
                processor.processBlock (buffer, midi);
            }

            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            line += std::string (" ") + phaseName (mode) + " " + format (100.0 * elapsed / (seconds * 1000.0), 2) + " % of one core";
            check (elapsed < seconds * 1000.0, std::string (phaseName (mode)) + ": runs faster than real time");
        }

        note (line);
    }

    //==============================================================================
    // Width = bandwidth in octaves (the Q parameter shown in octaves)
    void testBandwidth()
    {
        constexpr double sampleRate = 48000.0;
        const auto oct1 = params::qToOctaves (1.414), oct2 = params::qToOctaves (0.667);
        note ("Q 1.414 -> " + format (oct1, 4) + " oct, Q 0.667 -> " + format (oct2, 4) + " oct, 1 oct -> Q "
              + format (params::octavesToQ (1.0), 4) + ", 2 oct -> Q " + format (params::octavesToQ (2.0), 4));
        check (std::abs (oct1 - 1.0) < 0.001 && std::abs (oct2 - 2.0) < 0.002, "Q 1.414 = 1 octave and Q 0.667 = 2 octaves");

        auto roundTrip = true;
        for (const auto q : { 0.1, 0.3, 0.707, 1.0, 2.5, 9.0, 18.0 })
            roundTrip = roundTrip && std::abs (params::octavesToQ (params::qToOctaves (q)) - q) < 1.0e-9 * q;
        check (roundTrip, "octaves -> Q -> octaves round-trips across the Q range");

        // A bell is at half its dB gain at centre / 2^(BW/2) and centre * 2^(BW/2): for a -6 dB
        // bell those are its -3 dB points, so the measured -3 dB bandwidth equals the Width.
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        {
            double previousAt1500 = 0.0;

            for (const auto octaves : { 1.0, 2.0, 3.0 })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                setBand (processor, 3, params::band::enabled, 1.0f);
                setBand (processor, 3, params::band::type, (float) dsp::FilterType::bell);
                setBand (processor, 3, params::band::frequency, 1000.0f);
                setBand (processor, 3, params::band::gain, -6.0f);
                setBand (processor, 3, params::band::q, (float) params::octavesToQ (octaves));
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

                const auto edge = std::pow (2.0, octaves / 2.0);
                const auto lower = measureSineGainDb (processor, 1000.0 / edge, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto upper = measureSineGainDb (processor, 1000.0 * edge, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto at1500 = measureSineGainDb (processor, 1500.0, 0.25f, sampleRate, 1.0);

                note (std::string (phaseName (mode)) + ", -6 dB bell, Width " + format (octaves, 1) + " oct (Q "
                      + format (params::octavesToQ (octaves), 3) + "): " + format (1000.0 / edge, 1) + " Hz " + format (lower, 3) + " dB, "
                      + format (1000.0 * edge, 1) + " Hz " + format (upper, 3) + " dB; at 1.5 kHz " + format (at1500, 2) + " dB");
                check (std::abs (lower + 3.0) < 0.1 && std::abs (upper + 3.0) < 0.1,
                       std::string (phaseName (mode)) + ": the -3 dB points sit at the Width edges (" + format (octaves, 0) + " oct)");
                check (at1500 < previousAt1500 - 0.3, std::string (phaseName (mode)) + ": a wider Width cuts more at 1.5 kHz");
                previousAt1500 = at1500;
            }
        }
    }

    //==============================================================================
    // Editor: bandwidth handles, phase mode control
    void testWidthHandles()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        DooblrQAudioProcessor processor;
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::frequency, 1000.0f);
        setBand (processor, 2, params::band::gain, 6.0f);
        setBand (processor, 2, params::band::q, 1.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
        check (dooblrEditor != nullptr && curve != nullptr, "editor and curve exist");

        if (dooblrEditor == nullptr || curve == nullptr)
            return;

        auto* widthKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::q));
        check (findById (*editor, params::phaseMode) != nullptr && widthKnob != nullptr, "phase mode selector and Width knob exist");

        dooblrEditor->selectBand (2);
        check (curve->widthHandlesVisible(), "the selected, enabled band shows bandwidth handles");

        const auto [lowerBefore, upperBefore] = curve->widthHandlePositions (2);
        const auto centreX = curve->frequencyToX (1000.0);
        const auto qBefore = processor.getBandSettings (2).q;
        note ("before: Q " + format (qBefore) + " (" + format (Curve::qToOctaves (qBefore)) + " oct), lower x " + format (lowerBefore.x, 1)
              + ", centre x " + format (centreX, 1) + ", upper x " + format (upperBefore.x, 1));
        check (std::abs ((centreX - lowerBefore.x) - (upperBefore.x - centreX)) < 0.5f, "handles start symmetric about the centre");
        check (std::abs (lowerBefore.y - curve->decibelsToY (curve->responseDecibelsAt (curve->xToFrequency (lowerBefore.x), false))) < 0.5f,
               "bandwidth handles sit on the curve");

        constexpr float dragPixels = 40.0f;
        const auto target = upperBefore + juce::Point<float> (dragPixels, 0.0f);
        curve->mouseDown (makeMouseEvent (*curve, upperBefore, upperBefore, 1, false));
        for (int i = 1; i <= 8; ++i)
            curve->mouseDrag (makeMouseEvent (*curve, upperBefore + (target - upperBefore) * ((float) i / 8.0f), upperBefore, 1, true));
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-width-handles");
        curve->mouseUp (makeMouseEvent (*curve, target, upperBefore, 1, true));

        const auto s = processor.getBandSettings (2);
        const auto [lowerAfter, upperAfter] = curve->widthHandlePositions (2);
        const auto upperMoved = upperAfter.x - upperBefore.x;
        const auto lowerMoved = lowerBefore.x - lowerAfter.x;
        note ("dragged the upper handle " + format (dragPixels, 0) + " px outward: Q " + format (s.q) + " (" + format (Curve::qToOctaves (s.q))
              + " oct), centre " + format (s.frequency, 1) + " Hz, upper moved " + format (upperMoved, 2) + " px, lower moved "
              + format (lowerMoved, 2) + " px outward");
        check (s.q < qBefore && Curve::qToOctaves (s.q) > Curve::qToOctaves (qBefore), "dragging outward widens the band (lower Q)");
        check (std::abs (s.frequency - 1000.0f) < 0.01f, "the centre frequency is unchanged");
        check (std::abs (upperMoved - dragPixels) < 1.0f, "the dragged handle follows the mouse");
        check (std::abs (upperMoved - lowerMoved) < 1.0f, "the opposite handle mirrors it (same pixel distance in log-frequency)");

        // Inward narrows again; the scroll wheel also changes Q.
        const auto [lowerNow, upperNow] = curve->widthHandlePositions (2);
        juce::ignoreUnused (upperNow);
        simulateDrag (*curve, lowerNow, lowerNow + juce::Point<float> (30.0f, 0.0f));
        check (processor.getBandSettings (2).q > s.q, "dragging a handle inward narrows the band");

        const auto qBeforeWheel = processor.getBandSettings (2).q;
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.25f;
        curve->mouseWheelMove (makeMouseEvent (*curve, curve->handlePosition (2), curve->handlePosition (2), 0, false), wheel);
        check (processor.getBandSettings (2).q > qBeforeWheel * 1.3f, "scrolling over a handle changes Q");

        // The Width knob shows octaves and writes Q.
        if (widthKnob != nullptr)
        {
            dooblrEditor->selectBand (2);
            note ("Width knob reads \"" + widthKnob->getTextFromValue (widthKnob->getValue()).toStdString() + "\" for Q "
                  + format (processor.getBandSettings (2).q, 3));
            check (std::abs (widthKnob->getValue() - params::qToOctaves (processor.getBandSettings (2).q)) < 1.0e-4,
                   "the Width knob shows the band's Q as octaves");
            widthKnob->setValue (1.0, juce::sendNotificationSync);
            check (std::abs (processor.getBandSettings (2).q - 1.4142f) < 0.002f, "setting Width to 1.00 oct sets Q 1.414");
            check (widthKnob->getTextFromValue (1.0) == "1.00 oct (Q 1.41)", "the Width readout is like '1.00 oct (Q 1.41)'");
            widthKnob->setValue (2.0, juce::sendNotificationSync);
            check (std::abs (processor.getBandSettings (2).q - 0.6667f) < 0.002f, "setting Width to 2.00 oct sets Q 0.667");
        }

        // FIR modes draw the analogue response.
        setParameter (processor, params::phaseMode, (float) PhaseMode::linearPhase);
        const auto proto = dsp::makeEqPrototype (dsp::FilterType::bell, (double) processor.getBandSettings (2).q, 6.0);
        check (curve->showsAnalogResponse()
                   && std::abs (curve->responseDecibelsAt (18000.0, false)
                                - juce::Decibels::gainToDecibels (std::abs (dsp::analogResponseAt (proto, 1000.0, 18000.0)))) < 1.0e-6,
               "in Linear Phase the curve shows the analogue response the FIR is built from");

        processor.editorBeingDeleted (editor.get());
        editor.reset();

    }

    //==============================================================================
    // Smoothed spectrum display (frequency + temporal smoothing)
    void testSpectrumDisplay()
    {
        using Display = dooblrq::analysis::SpectrumDisplay;
        constexpr double sampleRate = 48000.0;

        Display display;
        display.prepare (sampleRate, Analyzer::numBins);

        // Flat bins read flat (the smoothing preserves level).
        std::vector<float> flat ((size_t) Analyzer::numBins, -40.0f), result;
        display.computeTarget (flat, result);
        const auto [flatMin, flatMax] = std::minmax_element (result.begin(), result.end());
        check (*flatMin > -40.01f && *flatMax < -39.99f, "flat input gives a flat display");

        // Spectral jaggedness of white noise (should be flat): standard deviation across
        // 1-20 kHz of (a) per-bin levels, (b) the old display (max within 1/12 octave), (c) the
        // new 1/12-octave smoothed display.
        Analyzer analyzer;
        analyzer.setSampleRate (sampleRate);
        juce::Random random (7);
        std::vector<float> noise (96000);
        for (auto& x : noise)
            x = random.nextFloat() * 0.5f - 0.25f;
        analyzer.pushSamples (noise.data(), 48000);
        const auto frame = analyzer.getLatestFrameDb();
        display.computeTarget (frame, result);
        Display unsmoothed (Display::defaultNumPoints, 20.0, 20000.0, 1.0e-4);
        unsmoothed.prepare (sampleRate, Analyzer::numBins);
        std::vector<float> perBin;
        unsmoothed.computeTarget (frame, perBin);

        const auto spread = [&] (auto levelAtPoint, double fromHz, double toHz)
        {
            double sum = 0.0, sumSquares = 0.0;
            auto count = 0;
            for (int i = 0; i < display.getNumPoints(); ++i)
            {
                if (display.getFrequency (i) < fromHz || display.getFrequency (i) > toHz)
                    continue;
                const auto v = (double) levelAtPoint (i);
                sum += v;
                sumSquares += v * v;
                ++count;
            }
            const auto mean = sum / std::max (1, count);
            return std::sqrt (std::max (0.0, sumSquares / std::max (1, count) - mean * mean));
        };
        const auto rawAt = [&] (int i) { return Analyzer::levelAt (frame, display.getFrequency (i), sampleRate, 1.0 / 12.0); };
        const auto perBinAt = [&] (int i) { return perBin[(size_t) i]; };
        const auto smoothAt = [&] (int i) { return result[(size_t) i]; };
        const auto spreadPerBin = spread (perBinAt, 1000.0, 20000.0), spreadOld = spread (rawAt, 1000.0, 20000.0),
                   spreadSmooth = spread (smoothAt, 1000.0, 20000.0);
        note ("white noise, one frame, spread across 1-20 kHz: per-bin " + format (spreadPerBin) + " dB, old display "
              + format (spreadOld) + " dB, smoothed " + format (spreadSmooth) + " dB");
        check (spreadSmooth < 0.5 * spreadPerBin && spreadSmooth < spreadOld, "1/12-octave smoothing flattens the noise floor");

        // Frame-to-frame flicker at 5 kHz with the live time constants (hop = 21 ms).
        {
            Display live;
            live.prepare (sampleRate, Analyzer::numBins);
            int point = 0;
            while (display.getFrequency (point) < 5000.0)
                ++point;
            double rawFlicker = 0.0, smoothFlicker = 0.0;
            float previousRaw = 0.0f, previousSmooth = 0.0f;
            int frames = 0;
            for (int start = 48000; start + Analyzer::hopSize <= (int) noise.size(); start += Analyzer::hopSize)
            {
                analyzer.pushSamples (noise.data() + start, Analyzer::hopSize);
                live.advance (analyzer.getLiveDb(), (double) Analyzer::hopSize / sampleRate, 0.03, 0.16);
                const auto raw = Analyzer::levelAt (analyzer.getLiveDb(), live.getFrequency (point), sampleRate, 1.0 / 12.0);
                const auto smooth = live.getLevels()[(size_t) point];
                if (frames++ > 10)
                {
                    rawFlicker += std::abs (raw - previousRaw);
                    smoothFlicker += std::abs (smooth - previousSmooth);
                }
                previousRaw = raw;
                previousSmooth = smooth;
            }
            rawFlicker /= std::max (1, frames - 11);
            smoothFlicker /= std::max (1, frames - 11);
            note ("live flicker at 5 kHz (mean change per frame): old display " + format (rawFlicker) + " dB, smoothed " + format (smoothFlicker) + " dB");
            check (smoothFlicker < 0.5 * rawFlicker, "temporal smoothing at least halves the frame-to-frame flicker");
        }

        // Low-frequency curve has no corners: second differences stay small.
        auto maxCorner = 0.0;
        for (int i = 2; i < display.getNumPoints(); ++i)
            if (display.getFrequency (i) < 100.0)
                maxCorner = std::max (maxCorner, std::abs ((double) result[(size_t) i] - 2.0 * result[(size_t) i - 1] + result[(size_t) i - 2]));
        auto rawCorner = 0.0;
        for (int i = 2; i < display.getNumPoints(); ++i)
            if (display.getFrequency (i) < 100.0)
                rawCorner = std::max (rawCorner, std::abs ((double) rawAt (i) - 2.0 * rawAt (i - 1) + rawAt (i - 2)));
        note ("largest second difference below 100 Hz: raw " + format (rawCorner, 3) + " dB, smoothed " + format (maxCorner, 3) + " dB");
        check (maxCorner < rawCorner, "the low end is a smoother curve than straight segments between bins");

        // A sine still shows a clear peak at its frequency.
        Analyzer toneAnalyzer;
        toneAnalyzer.setSampleRate (sampleRate);
        std::vector<float> tone (16384);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / sampleRate);
        toneAnalyzer.pushSamples (tone.data(), (int) tone.size());
        display.computeTarget (toneAnalyzer.getLatestFrameDb(), result);
        const auto peakIndex = (int) std::distance (result.begin(), std::max_element (result.begin(), result.end()));
        note ("0.5 sine at 1 kHz: smoothed display peak " + format (result[(size_t) peakIndex]) + " dBFS at "
              + format (display.getFrequency (peakIndex), 0) + " Hz (per-bin level -6.02 dBFS)");
        check (std::abs (std::log2 (display.getFrequency (peakIndex) / 1000.0)) < 1.0 / 24.0 && result[(size_t) peakIndex] > -20.0f,
               "a tone still shows as a peak at its frequency");

        // Temporal smoothing: one-pole in dB, independent of the frame rate.
        std::vector<float> quiet ((size_t) Analyzer::numBins, -100.0f), loud ((size_t) Analyzer::numBins, -20.0f);
        Display a, b;
        a.prepare (sampleRate, Analyzer::numBins);
        b.prepare (sampleRate, Analyzer::numBins);
        a.snapTo (quiet);
        b.snapTo (quiet);
        a.advance (loud, 0.03, 0.03, 0.16);
        for (int i = 0; i < 10; ++i)
            b.advance (loud, 0.003, 0.03, 0.16);
        const auto expected = -100.0 + 80.0 * (1.0 - std::exp (-1.0));
        note ("rise after one time constant: " + format (a.getLevels()[200]) + " dB (expected " + format (expected) + "), at 10x the frame rate "
              + format (b.getLevels()[200]) + " dB");
        check (std::abs (a.getLevels()[200] - expected) < 0.01 && std::abs (a.getLevels()[200] - b.getLevels()[200]) < 0.01,
               "the display rises with the set time constant at any frame rate");

        a.snapTo (loud);
        a.advance (quiet, 0.03, 0.03, 0.16);
        const auto fell = -20.0f - a.getLevels()[200];
        note ("fall in 30 ms: " + format (fell) + " dB (rise was " + format (80.0 * (1.0 - std::exp (-1.0))) + " dB)");
        check (fell > 1.0f && fell < 80.0f * (1.0f - std::exp (-1.0f)), "decay is slower than the attack");
    }

    //==============================================================================
    // Undo / redo
    std::vector<float> allParameterValues (DooblrQAudioProcessor& processor)
    {
        std::vector<float> values;
        for (auto* parameter : processor.getParameters())
            values.push_back (parameter->getValue());
        return values;
    }

    bool sameValues (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-5f)
                return false;
        return true;
    }

    /** What a UI control does: begin gesture, set, end gesture. */
    void uiGesture (DooblrQAudioProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.getParameterState().getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }

    void testUndo()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        DooblrQAudioProcessor processor;
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::gain, 3.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
        auto* undoButton = dynamic_cast<juce::Button*> (findById (*editor, "undo"));
        auto* redoButton = dynamic_cast<juce::Button*> (findById (*editor, "redo"));
        auto* frequencyKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::frequency));
        auto* typeBox = dynamic_cast<juce::ComboBox*> (findById (*editor, params::band::type));
        auto* bandSwitch = dynamic_cast<juce::Button*> (findById (*editor, "bandSwitch4"));
        check (dooblrEditor && curve && undoButton && redoButton && frequencyKnob && typeBox && bandSwitch,
               "editor, Undo/Redo buttons and controls exist");

        if (! (dooblrEditor && curve && undoButton && redoButton && frequencyKnob && typeBox && bandSwitch))
            return;

        auto& history = processor.getUndoHistory();
        history.clear();
        dooblrEditor->updateUndoButtons();
        check (history.getMaxTransactions() == 50, "history limit is 50 actions");
        check (! undoButton->isEnabled() && ! redoButton->isEnabled(), "Undo/Redo are disabled with no history");
        check (editor->getWantsKeyboardFocus(), "the editor accepts keyboard focus");

        // Host automation (no gestures), on the message thread and on another thread: not recorded.
        for (int i = 0; i < 100; ++i)
            setBand (processor, 0, params::band::gain, (float) (i % 10) - 5.0f);
        std::thread automation ([&processor]
        {
            for (int i = 0; i < 200; ++i)
                if (auto* p = processor.getParameterState().getParameter (params::bandId (1, params::band::gain)))
                    p->setValueNotifyingHost ((float) (i % 7) / 7.0f);
        });
        automation.join();
        check (! history.canUndo() && history.getNumUndoableSteps() == 0, "300 host automation changes create no undo entries");

        std::vector<std::vector<float>> states { allParameterValues (processor) };
        const auto record = [&] (const std::string& name, int expectedSteps)
        {
            states.push_back (allParameterValues (processor));
            check (history.getNumUndoableSteps() == expectedSteps && ! sameValues (states[states.size() - 2], states.back()),
                   name + " is one undo step");
        };

        // 1. Knob drag (band 1 frequency) through the slider's own mouse handlers.
        dooblrEditor->selectBand (0);
        const auto knobCentre = frequencyKnob->getLocalBounds().getCentre().toFloat();
        simulateDrag (*frequencyKnob, knobCentre, knobCentre + juce::Point<float> (0.0f, -60.0f));
        note ("knob drag: band 1 frequency " + format (getParameter (processor, params::bandId (0, params::band::frequency)), 1) + " Hz");
        record ("a frequency-knob drag", 1);

        // 2. Handle drag of band 3 (frequency and gain together).
        const auto handle = curve->handlePosition (2);
        simulateDrag (*curve, handle, handle + juce::Point<float> (50.0f, -30.0f));
        record ("a handle drag (frequency + gain)", 2);
        const auto afterHandle = states.back();

        // 3. Combo change (band 1 type) through the attachment.
        typeBox->setSelectedItemIndex ((int) dsp::FilterType::highShelf, juce::sendNotificationSync);
        record ("a type change", 3);

        // 4. Band-tab switch toggles band 4.
        bandSwitch->setToggleState (! bandSwitch->getToggleState(), juce::sendNotificationSync);
        record ("a band-tab switch", 4);

        // 5. Double-click on band 5's handle toggles it on.
        simulateDoubleClick (*curve, curve->handlePosition (4));
        record ("a double-click enable", 5);

        // 6. Bandwidth-handle drag of the selected band 3.
        dooblrEditor->selectBand (2);
        const auto upper = curve->widthHandlePositions (2).second;
        simulateDrag (*curve, upper, upper + juce::Point<float> (30.0f, 0.0f));
        record ("a bandwidth-handle drag", 6);

        dooblrEditor->updateUndoButtons();
        check (undoButton->isEnabled() && ! redoButton->isEnabled(), "Undo enabled, Redo disabled after edits");

        // Undo step by step restores each earlier state exactly.
        auto stepwise = true;
        for (int k = 1; k <= 6; ++k)
        {
            history.undo();
            stepwise = stepwise && sameValues (allParameterValues (processor), states[(size_t) (6 - k)]);
        }
        check (stepwise && ! history.canUndo(), "undo walks back through all 6 gestures, one at a time");
        check (sameValues (allParameterValues (processor), states.front()), "after 6 undos everything is as before the edits");

        // Redo.
        history.redo();
        history.redo();
        check (sameValues (allParameterValues (processor), afterHandle), "redo reapplies the knob and handle drags (frequency and gain together)");
        dooblrEditor->updateUndoButtons();
        check (undoButton->isEnabled() && redoButton->isEnabled(), "Undo and Redo both enabled mid-history");

        // A new edit after undo drops the redo branch.
        uiGesture (processor, params::outputGain, -3.0f);
        check (! history.canRedo() && history.getNumUndoableSteps() == 3, "a new edit after undo clears redo");

        // Keyboard: Cmd/Ctrl-Z, Cmd/Ctrl-Shift-Z, Cmd/Ctrl-Y.
        const auto command = juce::ModifierKeys::commandModifier;
        const auto beforeKey = allParameterValues (processor);
        check (dooblrEditor->keyPressed (juce::KeyPress ('z', command, 0)), "Cmd/Ctrl-Z is claimed by the editor");
        check (std::abs (getParameter (processor, params::outputGain)) < 1.0e-4f, "Cmd/Ctrl-Z undoes the last action");
        check (dooblrEditor->keyPressed (juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0))
                   && sameValues (allParameterValues (processor), beforeKey),
               "Cmd/Ctrl-Shift-Z redoes it");
        history.undo();
        check (dooblrEditor->keyPressed (juce::KeyPress ('y', command, 0)) && sameValues (allParameterValues (processor), beforeKey),
               "Cmd/Ctrl-Y redoes it");
        check (! dooblrEditor->keyPressed (juce::KeyPress ('k', command, 0)) && ! dooblrEditor->keyPressed (juce::KeyPress ('z')),
               "other keys are left to the host");

        // Undo / Redo buttons.
        dooblrEditor->updateUndoButtons();
        undoButton->onClick();
        check (std::abs (getParameter (processor, params::outputGain)) < 1.0e-4f, "the Undo button undoes");
        redoButton->onClick();
        check (std::abs (getParameter (processor, params::outputGain) + 3.0f) < 1.0e-3f, "the Redo button redoes");

        // Wheel ticks in quick succession merge into one step.
        const auto stepsBeforeWheel = history.getNumUndoableSteps();
        const auto qBeforeWheel = getParameter (processor, params::bandId (2, params::band::q));
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.05f;
        for (int i = 0; i < 5; ++i)
            curve->mouseWheelMove (makeMouseEvent (*curve, curve->handlePosition (2), curve->handlePosition (2), 0, false), wheel);
        check (history.getNumUndoableSteps() == stepsBeforeWheel + 1, "5 quick scroll-wheel ticks are one undo step");
        history.undo();
        check (std::abs (getParameter (processor, params::bandId (2, params::band::q)) - qBeforeWheel) < 1.0e-4f,
               "undoing the scroll restores Q");

        // Limit: 60 gestures keep only the last 50.
        history.clear();
        std::vector<std::vector<float>> limitStates { allParameterValues (processor) };
        for (int i = 0; i < 60; ++i)
        {
            // Alternate parameters so no two consecutive gestures merge.
            if (i % 2 == 0)
                uiGesture (processor, params::outputGain, -10.0f + 0.25f * (float) i);
            else
                uiGesture (processor, params::bandId (0, params::band::gain), -10.0f + 0.25f * (float) i);
            limitStates.push_back (allParameterValues (processor));
        }
        const auto stored = history.getNumUndoableSteps();
        auto undone = 0;
        while (history.undo())
            ++undone;
        note ("60 gestures: " + std::to_string (stored) + " stored, " + std::to_string (undone) + " undone");
        check (stored == 50 && undone == 50, "60 actions keep exactly the last 50");
        check (sameValues (allParameterValues (processor), limitStates[10]), "undoing all 50 lands on the state after action 10");

        // A loaded state starts a new history.
        uiGesture (processor, params::outputGain, 2.0f);
        juce::MemoryBlock block;
        processor.getStateInformation (block);
        processor.setStateInformation (block.getData(), (int) block.getSize());
        check (! history.canUndo() && ! history.canRedo(), "loading a state clears the history");

        processor.editorBeingDeleted (editor.get());
        editor.reset();
    }

    //==============================================================================
    // On/off switches in the band tabs
    int countColourPixels (const juce::Image& image, juce::Colour colour)
    {
        auto count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto p = image.getPixelAt (x, y);
                const auto distance = std::abs (p.getRed() - colour.getRed()) + std::abs (p.getGreen() - colour.getGreen())
                                      + std::abs (p.getBlue() - colour.getBlue());
                count += distance < 40 && p.getAlpha() > 200 ? 1 : 0;
            }
        return count;
    }

    void clickComponent (juce::Component& c)
    {
        const auto at = c.getLocalBounds().getCentre().toFloat();
        c.mouseDown (makeMouseEvent (c, at, at, 1, false));
        c.mouseUp (makeMouseEvent (c, at, at, 1, false));
    }

    void testBandTabSwitches()
    {
        DooblrQAudioProcessor processor;
        setBand (processor, 0, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::enabled, 1.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        check (dooblrEditor != nullptr, "editor exists");

        if (dooblrEditor == nullptr)
            return;

        processor.getUndoHistory().clear();
        dooblrEditor->selectBand (0);

        auto layoutOk = true, statesMatch = true, hitTestOk = true;

        for (int b = 0; b < params::numBands; ++b)
        {
            auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "band" + juce::String (b + 1)));
            auto* sw = dynamic_cast<juce::Button*> (findById (*editor, "bandSwitch" + juce::String (b + 1)));

            if (tab == nullptr || sw == nullptr)
            {
                layoutOk = false;
                continue;
            }

            layoutOk = layoutOk && sw->getParentComponent() == tab && sw->isVisible()
                       && sw->getX() >= 0 && sw->getRight() < tab->getWidth() / 4
                       && sw->getY() >= 0 && sw->getBottom() <= tab->getHeight();
            statesMatch = statesMatch && sw->getToggleState() == processor.getBandSettings (b).enabled;
            hitTestOk = hitTestOk && tab->getComponentAt (sw->getBounds().getCentre()) == sw
                        && tab->getComponentAt (tab->getLocalBounds().getCentre()) == tab;
        }

        check (layoutOk, "each band tab has its switch on the left side, inside the tab");
        check (statesMatch, "switch states match the bands' enabled parameters");
        check (hitTestOk, "clicks on the switch go to the switch, clicks elsewhere to the tab");

        auto* tab4 = dynamic_cast<juce::Button*> (findById (*editor, "band4"));
        auto* switch4 = dynamic_cast<juce::Button*> (findById (*editor, "bandSwitch4"));
        auto* switch6 = dynamic_cast<juce::Button*> (findById (*editor, "bandSwitch6"));

        if (tab4 == nullptr || switch4 == nullptr || switch6 == nullptr)
            return;

        // Click the switch (through its own mouse handlers): toggles band 4, tab selection unchanged.
        clickComponent (*switch4);
        check (processor.getBandSettings (3).enabled && switch4->getToggleState(), "clicking the switch turns band 4 on");
        check (dooblrEditor->getSelectedBand() == 0 && ! tab4->getToggleState(), "clicking the switch does not select the tab");
        const auto onImage = switch4->createComponentSnapshot (switch4->getLocalBounds());

        clickComponent (*switch4);
        check (! processor.getBandSettings (3).enabled && ! switch4->getToggleState(), "clicking it again turns band 4 off");
        const auto offImage = switch4->createComponentSnapshot (switch4->getLocalBounds());

        // Clicking the rest of the tab selects it.
        clickComponent (*tab4);
        check (dooblrEditor->getSelectedBand() == 3 && tab4->getToggleState(), "clicking the tab body selects band 4");
        check (! processor.getBandSettings (3).enabled, "selecting the tab does not toggle the band");

        // Parameter changes from elsewhere (host, curve double-click) update the switch.
        setBand (processor, 5, params::band::enabled, 1.0f);
        check (switch6->getToggleState(), "the switch follows the parameter");

        // Undoable.
        check (processor.getUndoHistory().getNumUndoableSteps() == 2, "each switch click is one undo step");
        processor.getUndoHistory().undo();
        check (processor.getBandSettings (3).enabled && switch4->getToggleState(), "undo reverts the last switch click");

        // On uses the band colour, off is dim.
        const auto onPixels = countColourPixels (onImage, dooblrq::ui::bandColour (3));
        const auto offPixels = countColourPixels (offImage, dooblrq::ui::bandColour (3));
        note ("band-colour pixels in switch 4: on " + std::to_string (onPixels) + ", off " + std::to_string (offPixels));
        check (onPixels > 100 && offPixels == 0, "on is drawn in the band colour, off is dim");

        processor.editorBeingDeleted (editor.get());
        editor.reset();
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
        { "curve follows dragged band handles",      testCurveFollowsHandles },
        { "spectrum analyzer core",                 testSpectrumAnalyzerCore },
        { "analyzer does not change the audio",     testAnalyzerIsTransparent },
        { "tilt band type",                         testTilt },
        { "pre-Tilt saved state restores",          testLegacyStateRestores },
        { "editor analyzer, freeze and version",    testEditorAnalyzerAndVersion },
        { "phase modes: reported latency",          testPhaseModeLatency },
        { "phase modes: frequency response",        testPhaseModeResponse },
        { "linear phase impulse response",          testLinearPhaseImpulse },
        { "long modes: neutral identity, bypass",   testLongModesNeutralAndBypass },
        { "mode switching and dynamics per mode",   testModeSwitchingAndDynamics },
        { "Width (bandwidth in octaves)",           testBandwidth },
        { "editor bandwidth handles",               testWidthHandles },
        { "smoothed spectrum display",              testSpectrumDisplay },
        { "undo / redo (50 steps, keys, buttons)",  testUndo },
        { "band tab on/off switches",               testBandTabSwitches },
        { "CPU per phase mode",                     testCpuUsage },
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
