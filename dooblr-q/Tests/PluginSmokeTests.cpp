// Smoke tests for the dooblr-q processor, DSP and editor.
// Exit code is 0 only if every check passes; registered with CTest.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Analysis/SpectrumDisplay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 0.9.5: counts heap allocations (the solo highlight computation must make none).
namespace allocationCounter
{
    std::atomic<long long> count { 0 };
}

void* operator new (std::size_t size)
{
    allocationCounter::count.fetch_add (1, std::memory_order_relaxed);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

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
        check (processor->getParameters().size() == params::numBands * 16 + 3,
               "exposes 16 parameters per band plus output gain, stereo link and phase mode");
        check (processor->getParameters()[params::numBands * 10]->getName (64) == "Output Gain"
                   && processor->getParameters()[params::numBands * 10 + 2]->getName (64) == "Phase Mode",
               "phase mode (added in 0.4.0) comes after all older parameters (old indices unchanged)");
        {
            auto slopesLast = true;
            for (int b = 0; b < params::numBands; ++b)
            {
                auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (processor->getParameters()[params::numBands * 10 + 3 + b]);
                slopesLast = slopesLast && p != nullptr && p->paramID == params::bandId (b, params::band::slope);
            }
            check (slopesLast, "the six band slopes (added in 0.7.0) come after every older parameter");
        }
        {
            auto modelsLast = true;
            const char* suffixes[] { params::band::model, params::band::stepped, params::band::drive, params::band::atten };
            for (int b = 0; b < params::numBands; ++b)
                for (int k = 0; k < 4; ++k)
                {
                    auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (processor->getParameters()[params::numBands * 11 + 3 + b * 4 + k]);
                    modelsLast = modelsLast && p != nullptr && p->paramID == params::bandId (b, suffixes[k]);
                }
            check (modelsLast, "the 0.8.0 model parameters come after every older parameter");
        }
        {
            auto solosLast = true;
            for (int b = 0; b < params::numBands; ++b)
            {
                auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (processor->getParameters()[params::numBands * 15 + 3 + b]);
                solosLast = solosLast && p != nullptr && p->paramID == params::bandId (b, params::band::solo)
                            && p->getName (64) == "Band " + juce::String (b + 1) + " Solo" && p->getDefaultValue() == 0.0f
                            && p->isAutomatable();
            }
            check (solosLast, "the six 'Band N Solo' parameters (0.9.1) come last, default off, automatable");
        }

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
    // 0.6.0: band 1 / band 6 defaults and 2D handle dragging for every band type.
    void testDefaultCutsAnd2dDrag()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        using T = dsp::FilterType;
        constexpr double sampleRate = 48000.0;

        {
            DooblrQAudioProcessor processor;
            const auto b1 = processor.getBandSettings (0), b6 = processor.getBandSettings (5);
            check (b1.type == T::lowCut && std::abs (b1.frequency - 30.0f) < 0.01f && std::abs (b1.q - 0.707f) < 1.0e-4f,
                   "band 1 defaults to Low Cut (high-pass) at 30 Hz, Q 0.707");
            check (b6.type == T::highCut && std::abs (b6.frequency - 18000.0f) < 0.5f && std::abs (b6.q - 0.707f) < 1.0e-4f,
                   "band 6 defaults to High Cut (low-pass) at 18 kHz, Q 0.707");
            check (! b1.enabled && ! b6.enabled, "bands 1 and 6 still default off (a fresh insert is transparent)");

            // Snapshot with the two default cuts switched on.
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            setBand (processor, 0, params::band::enabled, 1.0f);
            setBand (processor, 5, params::band::enabled, 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1300, 760);
            auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve"));
            check (curve != nullptr, "response curve exists (defaults snapshot)");

            if (curve != nullptr)
            {
                check (std::abs (curve->yToDecibels (curve->handlePosition (0).y) + 3.01) < 0.05
                           && std::abs (curve->yToDecibels (curve->handlePosition (5).y) + 3.01) < 0.05,
                       "default cut handles sit at their -3 dB corners");
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.6.0");
            }

            processor.editorBeingDeleted (editor.get());
        }

        const std::pair<T, const char*> types[] {
            { T::bell, "Bell" }, { T::lowShelf, "Low Shelf" }, { T::highShelf, "High Shelf" }, { T::tilt, "Tilt" },
            { T::lowCut, "Low Cut" }, { T::highCut, "High Cut" }, { T::notch, "Notch" }
        };

        for (int bandIndex : { 0, 5 })
        {
            for (const auto& [type, name] : types)
            {
                const auto label = std::string (name) + " on band " + std::to_string (bandIndex + 1);
                const bool qType = type == T::lowCut || type == T::highCut || type == T::notch;

                DooblrQAudioProcessor processor;
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                setBand (processor, bandIndex, params::band::type, (float) type);
                setBand (processor, bandIndex, params::band::frequency, 1000.0f);
                setBand (processor, bandIndex, params::band::q, 1.0f);
                setBand (processor, bandIndex, params::band::gain, 0.0f);
                setBand (processor, bandIndex, params::band::enabled, 1.0f);

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
                editor->setSize (1300, 760);
                auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve"));

                if (curve == nullptr)
                {
                    check (false, "response curve exists for " + label);
                    continue;
                }

                auto& history = processor.getUndoHistory();
                history.clear();
                const auto before = processor.getBandSettings (bandIndex);

                // Up and to the right: 1 kHz -> 2 kHz, 60 px up.
                const auto start = curve->handlePosition (bandIndex);
                const auto up = juce::Point<float> (curve->frequencyToX (2000.0), start.y - 60.0f);
                const auto deltaDb = curve->yToDecibels (up.y) - curve->yToDecibels (start.y);
                simulateDrag (*curve, start, up);
                const auto s = processor.getBandSettings (bandIndex);

                check (std::abs (s.frequency - 2000.0f) < 10.0f, label + ": horizontal drag sets frequency (2 kHz)");

                if (qType)
                {
                    const auto expectedQ = std::pow (10.0, deltaDb / 20.0);
                    note (label + ": up " + format (deltaDb) + " dB -> Q " + format (s.q) + " (expected " + format (expectedQ) + ")");
                    check (std::abs (s.q - expectedQ) < 0.02 * expectedQ, label + ": vertical drag up raises Q (Q = 10^(dB/20))");
                    check (std::abs (s.gainDb - before.gainDb) < 1.0e-6f, label + ": vertical drag does not change gain");
                }
                else
                {
                    const auto expectedGain = type == T::bell ? deltaDb : 2.0 * deltaDb; // shelf midpoint / tilt pivot scale
                    note (label + ": up " + format (deltaDb) + " dB -> gain " + format (s.gainDb) + " dB (expected " + format (expectedGain) + ")");
                    check (std::abs (s.gainDb - expectedGain) < 0.1, label + ": vertical drag up raises gain");
                    check (std::abs (s.q - before.q) < 1.0e-6f, label + ": vertical drag does not change Q");
                }

                const auto handle = curve->handlePosition (bandIndex);

                if (type == T::tilt)
                    check (std::abs (handle.x - up.x) < 0.5f && std::abs (curve->yToDecibels (handle.y)) < 0.05,
                           label + ": handle follows horizontally and stays on its 0 dB pivot");
                else
                    check (handle.getDistanceFrom (up) < 0.75f, label + ": handle ends under the pointer");

                // On the curve: the drawn (combined) response at the handle equals the handle level
                // (the notch's centre is -inf, so its handle height is its Q instead).
                if (type != T::notch)
                    check (std::abs (curve->responseDecibelsAt (s.frequency, false) - curve->yToDecibels (handle.y)) < 0.1,
                           label + ": handle sits on the drawn curve");
                else
                    check (curve->responseDecibelsAt (s.frequency, false) < -40.0,
                           label + ": the curve follows (deep notch at the new centre)");

                // Then 120 px down from the handle: the vertical value falls below where it began.
                const auto down = juce::Point<float> (curve->frequencyToX (500.0), handle.y + 120.0f);
                simulateDrag (*curve, handle, down);
                const auto d = processor.getBandSettings (bandIndex);
                check (std::abs (d.frequency - 500.0f) < 3.0f, label + ": horizontal drag back down in frequency");
                check (qType ? (d.q < before.q && std::abs (d.gainDb - before.gainDb) < 1.0e-6f)
                             : (d.gainDb < before.gainDb && std::abs (d.q - before.q) < 1.0e-6f),
                       label + (qType ? ": dragging down lowers Q" : ": dragging down lowers gain"));

                // Each drag is one undo step.
                check (history.getNumUndoableSteps() == 2, label + ": two drags = two undo steps");
                history.undo();
                const auto u = processor.getBandSettings (bandIndex);
                check (std::abs (u.frequency - s.frequency) < 0.5f && std::abs (u.q - s.q) < 1.0e-3f && std::abs (u.gainDb - s.gainDb) < 1.0e-3f,
                       label + ": undo restores the first drag's result");
                history.undo();
                const auto u0 = processor.getBandSettings (bandIndex);
                check (std::abs (u0.frequency - before.frequency) < 0.5f && std::abs (u0.q - before.q) < 1.0e-3f
                           && std::abs (u0.gainDb - before.gainDb) < 1.0e-3f && u0.type == type,
                       label + ": second undo restores the original band");

                processor.editorBeingDeleted (editor.get());
            }
        }

        // A horizontal-only drag of a cut at a Q above the display range keeps its Q.
        {
            DooblrQAudioProcessor processor;
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            setBand (processor, 0, params::band::enabled, 1.0f);
            setBand (processor, 0, params::band::frequency, 200.0f);
            setBand (processor, 0, params::band::q, 18.0f);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1300, 760);

            if (auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve")))
            {
                const auto start = curve->handlePosition (0);
                simulateDrag (*curve, start, { curve->frequencyToX (400.0), start.y });
                const auto s = processor.getBandSettings (0);
                check (std::abs (s.q - 18.0f) < 1.0e-3f && std::abs (s.frequency - 400.0f) < 2.0f,
                       "horizontal-only drag of a Q-18 cut keeps Q 18");
            }

            processor.editorBeingDeleted (editor.get());
        }
    }

    //==============================================================================
    // 0.6.1: dynamic activity is coloured per band, only over that band's affected range.
    void testDynamicRegions()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        constexpr double sampleRate = 48000.0;

        DooblrQAudioProcessor processor;
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        constexpr int bell = 2, shelf = 1, quiet = 4;
        const auto setupDynamic = [&] (int b, dsp::FilterType type, float frequency, float q)
        {
            setBand (processor, b, params::band::type, (float) type);
            setBand (processor, b, params::band::frequency, frequency);
            setBand (processor, b, params::band::q, q);
            setBand (processor, b, params::band::gain, 0.0f);
            setBand (processor, b, params::band::enabled, 1.0f);
            setBand (processor, b, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, b, params::band::range, -12.0f);
            setBand (processor, b, params::band::threshold, -40.0f);
        };

        setupDynamic (bell, dsp::FilterType::bell, 1000.0f, 2.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        editor->setSize (1300, 760);
        auto* curve = dynamic_cast<Curve*> (editor->findChildWithID ("responseCurve"));
        check (curve != nullptr, "response curve exists");

        if (curve == nullptr)
            return;

        // Dynamics on but nothing pushing them: no coloured region.
        check (std::abs (processor.getBandDynamicGainDb (bell)) < 1.0e-3f, "idle bell has no dynamic gain");
        check (curve->dynamicRegions (bell).empty(), "idle dynamics draw no region");

        // A loud 1 kHz tone pulls the bell down.
        measureSineGainDb (processor, 1000.0, 0.5f, sampleRate, 0.3);
        const auto bellGain = processor.getBandDynamicGainDb (bell);
        const auto bellRegions = curve->dynamicRegions (bell);
        note ("bell dynamic gain " + format (bellGain) + " dB; regions: " + std::to_string (bellRegions.size())
              + (bellRegions.empty() ? std::string() : " (" + format (bellRegions.front().getStart(), 0) + " - "
                                                         + format (bellRegions.front().getEnd(), 0) + " Hz)"));
        check (bellGain < -3.0f, "loud in-band tone pushes the bell's dynamics");
        check (bellRegions.size() == 1, "the bell has exactly one region");

        if (bellRegions.size() == 1)
        {
            const auto r = bellRegions.front();
            check (r.contains (1000.0), "the bell's region contains its centre");
            check (r.getStart() > 100.0 && r.getEnd() < 10000.0, "the bell's region is confined around 1 kHz (not the whole spectrum)");
            check (r.getStart() > 20.5 && r.getEnd() < 19500.0, "the bell's region does not reach either edge");
        }

        check (std::abs (curve->bandDynamicDeltaDecibelsAt (bell, 1000.0) - bellGain) < 0.05,
               "the bell's region depth at its centre equals its dynamic gain");
        check (std::abs (curve->bandDynamicDeltaDecibelsAt (bell, 60.0)) < Curve::dynamicRegionThresholdDb
                   && std::abs (curve->bandDynamicDeltaDecibelsAt (bell, 15000.0)) < Curve::dynamicRegionThresholdDb,
               "no dynamic change far from the bell (60 Hz, 15 kHz)");
        check (std::abs (curve->responseDecibelsAt (1000.0, true) - curve->responseDecibelsAt (1000.0, false) - bellGain) < 0.05
                   && std::abs (curve->responseDecibelsAt (60.0, true) - curve->responseDecibelsAt (60.0, false)) < 0.1,
               "the combined live curve still moves only where the bell acts");

        // Add a dynamic low shelf and a static bell; drive both dynamic bands with noise.
        setupDynamic (shelf, dsp::FilterType::lowShelf, 200.0f, 0.707f);
        setBand (processor, quiet, params::band::enabled, 1.0f);
        setBand (processor, quiet, params::band::gain, 4.0f); // static only
        {
            juce::AudioBuffer<float> noise (2, (int) (0.3 * sampleRate));
            fillWithNoise (noise, 1234, 0.7f);
            processInBlocks (processor, noise, { 512 });
        }

        const auto shelfGain = processor.getBandDynamicGainDb (shelf);
        const auto shelfRegions = curve->dynamicRegions (shelf);
        const auto bellRegions2 = curve->dynamicRegions (bell);
        note ("shelf dynamic gain " + format (shelfGain) + " dB, bell " + format (processor.getBandDynamicGainDb (bell))
              + " dB; shelf regions " + std::to_string (shelfRegions.size())
              + (shelfRegions.empty() ? std::string() : " (" + format (shelfRegions.front().getStart(), 0) + " - "
                                                          + format (shelfRegions.front().getEnd(), 0) + " Hz)"));
        check (shelfGain < -1.0f, "noise pushes the low shelf's dynamics");
        check (shelfRegions.size() == 1 && shelfRegions.front().getStart() < 25.0 && shelfRegions.front().getEnd() < 3000.0,
               "the low shelf's region covers only its low side (from the bottom edge, ending well below the top)");
        check (std::abs (curve->bandDynamicDeltaDecibelsAt (shelf, 12000.0)) < Curve::dynamicRegionThresholdDb,
               "no shelf change on its unaffected (high) side");
        check (bellRegions2.size() == 1, "the bell keeps its own separate region alongside the shelf's");
        check (curve->dynamicRegions (quiet).empty(), "a band without dynamics has no region");

        // Pixels: the shelf's colour appears in its region and nowhere near 10 kHz.
        const auto image = curve->createComponentSnapshot (curve->getLocalBounds());
        const auto countBandPixels = [&] (int band, double frequency)
        {
            const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
            const auto colour = dooblrq::ui::bandColour (band);
            const auto x = juce::roundToInt (curve->frequencyToX (frequency));
            int count = 0;

            for (int y = 0; y < image.getHeight(); ++y)
            {
                const auto p = pixels.getPixelColour (x, y);
                count += std::abs (p.getRed() - colour.getRed()) + std::abs (p.getGreen() - colour.getGreen())
                         + std::abs (p.getBlue() - colour.getBlue()) < 40 ? 1 : 0;
            }

            return count;
        };
        note ("band-colour pixels: bell at 1 kHz " + std::to_string (countBandPixels (bell, 1000.0)) + ", at 10 kHz "
              + std::to_string (countBandPixels (bell, 10000.0)) + "; shelf at 40 Hz " + std::to_string (countBandPixels (shelf, 40.0))
              + ", at 10 kHz " + std::to_string (countBandPixels (shelf, 10000.0)));
        check (countBandPixels (bell, 1000.0) > 0 && countBandPixels (shelf, 40.0) > 0, "regions are drawn in each band's colour");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.6.1");

        // Compare with the same display with both dynamic bands switched to static: only the
        // columns inside a region may differ.
        setBand (processor, bell, params::band::dynamicsEnabled, 0.0f);
        setBand (processor, shelf, params::band::dynamicsEnabled, 0.0f);
        check (curve->dynamicRegions (bell).empty() && curve->dynamicRegions (shelf).empty(), "dynamics off: no regions");
        const auto staticImage = curve->createComponentSnapshot (curve->getLocalBounds());
        const auto changedPixels = [&] (double frequency)
        {
            const juce::Image::BitmapData a (image, juce::Image::BitmapData::readOnly), b (staticImage, juce::Image::BitmapData::readOnly);
            const auto x = juce::roundToInt (curve->frequencyToX (frequency));
            int count = 0;

            for (int y = 0; y < image.getHeight(); ++y)
                count += a.getPixelColour (x, y) != b.getPixelColour (x, y) ? 1 : 0;

            return count;
        };
        note ("pixels changed by dynamics: 40 Hz " + std::to_string (changedPixels (40.0)) + ", 1 kHz "
              + std::to_string (changedPixels (1000.0)) + ", 10 kHz " + std::to_string (changedPixels (10000.0))
              + ", 15 kHz " + std::to_string (changedPixels (15000.0)));
        check (changedPixels (40.0) > 5 && changedPixels (1000.0) > 5, "the regions colour the shelf's and bell's ranges");
        check (changedPixels (10000.0) == 0 && changedPixels (15000.0) == 0,
               "nothing is coloured outside the active bands' ranges (10 kHz, 15 kHz)");


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

    //==============================================================================
    // Per-band slope (0.7.0): 12 ... 60 dB/oct and Brickwall
    void testSlopes()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        using T = dsp::FilterType;
        using S = dsp::Slope;
        constexpr double sampleRate = 48000.0;
        const auto db = [] (double magnitude) { return juce::Decibels::gainToDecibels (magnitude, -400.0); };
        constexpr S allSlopes[] { S::db12, S::db24, S::db36, S::db48, S::db60, S::brickwall };

        // 1. Design: 12 dB/oct is exactly the pre-0.7.0 filter, for every type.
        auto identical = true;
        for (int t = 0; t < dsp::numFilterTypes; ++t)
        {
            const auto a = dsp::makeEqCoefficients ((T) t, 1234.0, 1.3, 5.0, sampleRate);
            const auto b = dsp::makeBandCoefficients ((T) t, S::db12, 1234.0, 1.3, 5.0, sampleRate);
            const auto& c = b.sections[0];
            identical = identical && b.numSections == 1 && juce::exactlyEqual (c.a1, a.a1) && juce::exactlyEqual (c.a2, a.a2)
                        && juce::exactlyEqual (c.a3, a.a3) && juce::exactlyEqual (c.m0, a.m0) && juce::exactlyEqual (c.m1, a.m1)
                        && juce::exactlyEqual (c.m2, a.m2);
        }
        check (identical, "12 dB/oct is bit-identical to the previous single-section filter (all types)");

        // 2. Cuts: -3 dB at the corner and 6.02 dB/oct per order, digital and analogue.
        for (const auto type : { T::lowCut, T::highCut })
        {
            std::string line = std::string (type == T::lowCut ? "Low Cut" : "High Cut") + " @ 1 kHz (digital 48 kHz):";

            for (const auto slope : allSlopes)
            {
                const auto order = 2 * dsp::sectionsForSlope (slope);
                const auto c = dsp::makeBandCoefficients (type, slope, 1000.0, 0.70710678, 0.0, sampleRate);
                const auto p = dsp::makeBandPrototype (type, slope, 0.70710678, 0.0);
                const auto past = [&] (double octaves) { return type == T::lowCut ? 1000.0 / std::pow (2.0, octaves) : 1000.0 * std::pow (2.0, octaves); };
                const auto corner = db (dsp::magnitudeAt (c, 1000.0, sampleRate));
                const auto one = db (dsp::magnitudeAt (c, past (1.0), sampleRate));
                const auto two = db (dsp::magnitudeAt (c, past (2.0), sampleRate));
                const auto analogOne = db (std::abs (dsp::analogResponseAt (p, 1000.0, past (1.0))));
                const auto analogTwo = db (std::abs (dsp::analogResponseAt (p, 1000.0, past (2.0))));
                const auto ideal = [order] (double octaves) { return -10.0 * std::log10 (1.0 + std::pow (2.0, 2.0 * order * octaves)); };
                const auto name = params::slopeNames()[(int) slope].toStdString();
                line += " " + name + ": corner " + format (corner, 2) + ", 1 oct " + format (one, 1) + ", 2 oct " + format (two, 1)
                        + " (per oct " + format (one - two, 1) + ");";

                check (std::abs (corner + 3.01) < 0.02, name + " " + (type == T::lowCut ? "low" : "high") + " cut: -3 dB at the corner");
                check (std::abs (analogOne - ideal (1.0)) < 0.01 && std::abs (analogTwo - ideal (2.0)) < 0.01,
                       name + ": analogue response is the order-" + std::to_string (order) + " Butterworth");

                if (slope != S::brickwall)
                {
                    // 1 -> 2 octaves past the corner drops by ~6.02 dB per order (bilinear adds a
                    // little more above the corner for a high cut).
                    const auto perOctave = one - two;
                    const auto expected = 6.0206 * order;
                    check (perOctave > expected - 0.3 && perOctave < expected + (type == T::highCut ? 3.0 : 0.3),
                           name + ": " + format (expected, 0) + " dB/oct measured " + format (perOctave, 2) + " between 1 and 2 octaves");
                    check (std::abs (one - ideal (1.0)) < (type == T::highCut ? 1.5 : 0.3), name + ": attenuation one octave past matches the design");
                }
                else
                {
                    check (one < -180.0, "Brickwall: below -180 dB one octave past the corner (design)");
                    const auto tenPercent = db (dsp::magnitudeAt (c, type == T::lowCut ? 900.0 : 1100.0, sampleRate));
                    check (tenPercent < -25.0, "Brickwall: below -25 dB just 10 % past the corner (" + format (tenPercent, 1) + " dB)");
                }
            }

            note (line);
        }

        // Resonance: Q scales the corner level to 20 log10 (Q) at every order.
        auto resonance = true;
        for (const auto slope : allSlopes)
            for (const auto q : { 0.3, 2.0, 8.0 })
                resonance = resonance && std::abs (db (dsp::magnitudeAt (dsp::makeBandCoefficients (T::lowCut, slope, 1000.0, q, 0.0, sampleRate), 1000.0, sampleRate))
                                                   - 20.0 * std::log10 (q)) < 0.05;
        check (resonance, "cut corner level = 20 log10 (Q) for every slope (Q adds resonance, 0.707 = -3 dB)");

        // 3. Shelves and tilt get steeper while their midpoint stays at half the gain.
        for (const auto type : { T::lowShelf, T::highShelf, T::tilt })
        {
            std::string line = params::filterTypeNames()[(int) type].toStdString() + " +18 dB @ 1 kHz, max transition slope:";
            double previousSteepness = 0.0;
            auto ok = true;

            for (const auto slope : allSlopes)
            {
                const auto c = dsp::makeBandCoefficients (type, slope, 1000.0, 0.70710678, 18.0, sampleRate);
                const auto mid = db (dsp::magnitudeAt (c, 1000.0, sampleRate));
                const auto expectedMid = type == T::lowShelf || type == T::highShelf ? 9.0 : 0.0;
                double steepness = 0.0;
                for (double oct = -3.0; oct < 3.0; oct += 0.01)
                    steepness = std::max (steepness, std::abs (db (dsp::magnitudeAt (c, 1000.0 * std::pow (2.0, oct + 0.01), sampleRate))
                                                               - db (dsp::magnitudeAt (c, 1000.0 * std::pow (2.0, oct), sampleRate))) / 0.01);
                const auto low = db (dsp::magnitudeAt (c, 40.0, sampleRate)), high = db (dsp::magnitudeAt (c, 15000.0, sampleRate));
                const auto expectedLow = type == T::lowShelf ? 18.0 : type == T::tilt ? -9.0 : 0.0;
                const auto expectedHigh = type == T::highShelf ? 18.0 : type == T::tilt ? 9.0 : 0.0;
                line += " " + params::slopeNames()[(int) slope].toStdString() + " " + format (steepness, 1) + " dB/oct;";
                ok = ok && std::abs (mid - expectedMid) < 0.02 && steepness > previousSteepness + 1.0
                        && std::abs (low - expectedLow) < (slope == S::db12 ? 0.6 : 0.05) && std::abs (high - expectedHigh) < (slope == S::db12 ? 0.6 : 0.1);
                previousSteepness = steepness;
            }

            note (line);
            check (ok, params::filterTypeNames()[(int) type].toStdString() + ": each slope is steeper, midpoint at half the gain, plateaus exact");
        }

        // 4. Measured through the plugin (Zero Latency): slope per order with sines.
        {
            std::string line = "Zero Latency, Low Cut 1 kHz, measured:";
            for (const auto slope : allSlopes)
            {
                DooblrQAudioProcessor processor;
                setBand (processor, 0, params::band::enabled, 1.0f);
                setBand (processor, 0, params::band::type, (float) T::lowCut);
                setBand (processor, 0, params::band::frequency, 1000.0f);
                setBand (processor, 0, params::band::q, 0.707f);
                setBand (processor, 0, params::band::slope, (float) slope);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                check (processor.getBandSettings (0).slope == slope, "slope parameter reaches the band settings");

                const auto corner = measureSineGainDb (processor, 1000.0, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto one = measureSineGainDb (processor, 500.0, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto two = measureSineGainDb (processor, 250.0, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto pass = measureSineGainDb (processor, 4000.0, 0.25f, sampleRate, 1.0);
                const auto name = params::slopeNames()[(int) slope].toStdString();
                line += " " + name + ": 1 kHz " + format (corner, 2) + ", 500 Hz " + format (one, 1) + ", 250 Hz " + format (two, 1) + ", 4 kHz " + format (pass, 2) + ";";

                check (std::abs (corner + 3.01) < 0.1, name + ": measured -3 dB at the corner");
                check (std::abs (pass) < 0.1, name + ": passband flat two octaves above");

                if (slope != S::brickwall)
                {
                    const auto order = 2 * dsp::sectionsForSlope (slope);
                    const auto ideal1 = -10.0 * std::log10 (1.0 + std::pow (2.0, 2.0 * order)), ideal2 = -10.0 * std::log10 (1.0 + std::pow (2.0, 4.0 * order));
                    check (std::abs (one - ideal1) < 0.2 && std::abs (two - ideal2) < 0.5,
                           name + ": measured " + format (one, 1) + " / " + format (two, 1) + " dB one / two octaves below (design "
                               + format (ideal1, 1) + " / " + format (ideal2, 1) + ")");
                }
                else
                {
                    check (one < -110.0, "Brickwall: measured below -110 dB one octave below (" + format (one, 1) + " dB)");
                    processor.reset();
                    const auto near = measureSineGainDb (processor, 900.0, 0.25f, sampleRate, 1.0);
                    line += " 900 Hz " + format (near, 1) + ";";
                    check (near < -25.0, "Brickwall: measured below -25 dB 10 % below the corner");
                }
            }
            note (line);
        }

        // 5. Natural / Linear phase: the kernels follow the slope too.
        for (const auto mode : { PhaseMode::naturalPhase, PhaseMode::linearPhase })
        {
            std::string line = std::string (phaseName (mode)) + ", Low Cut 1 kHz:";
            for (const auto slope : { S::db12, S::db48, S::brickwall })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                setBand (processor, 0, params::band::enabled, 1.0f);
                setBand (processor, 0, params::band::type, (float) T::lowCut);
                setBand (processor, 0, params::band::frequency, 1000.0f);
                setBand (processor, 0, params::band::q, 0.707f);
                setBand (processor, 0, params::band::slope, (float) slope);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                const auto corner = measureSineGainDb (processor, 1000.0, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto one = measureSineGainDb (processor, 500.0, 0.25f, sampleRate, 1.0);
                processor.reset();
                const auto pass = measureSineGainDb (processor, 4000.0, 0.25f, sampleRate, 1.0);
                const auto name = params::slopeNames()[(int) slope].toStdString();
                line += " " + name + ": 1 kHz " + format (corner, 2) + ", 500 Hz " + format (one, 1) + ", 4 kHz " + format (pass, 2) + ";";
                const auto order = 2 * dsp::sectionsForSlope (slope);
                const auto ideal1 = -10.0 * std::log10 (1.0 + std::pow (2.0, 2.0 * order));
                check (std::abs (corner + 3.01) < 0.15 && std::abs (pass) < 0.15,
                       std::string (phaseName (mode)) + " " + name + ": -3 dB corner and flat passband");
                check (slope == S::brickwall ? one < -70.0 : std::abs (one - ideal1) < 0.5,
                       std::string (phaseName (mode)) + " " + name + ": attenuation one octave below (" + format (one, 1) + " dB)");
            }
            note (line);
        }

        // 6. Bell and notch slopes: see testBellNotchSlopes (0.7.1).

        // 7. Click-safe slope and type changes while audio runs (sine well inside the passband).
        for (const auto change : { 0, 1 })
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 0, params::band::enabled, 1.0f);
            setBand (processor, 0, params::band::type, (float) T::lowCut);
            setBand (processor, 0, params::band::frequency, 100.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 64);
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            float previous = 0.0f, steadyStep = 0.0f, switchStep = 0.0f;
            int n = 0;
            for (int block = 0; block < 1500; ++block)
            {
                if (block == 750)
                {
                    if (change == 0) setBand (processor, 0, params::band::slope, (float) S::brickwall);
                    else             setBand (processor, 0, params::band::type, (float) T::lowShelf);
                }
                for (int i = 0; i < 64; ++i, ++n)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * n / sampleRate));
                processor.processBlock (buffer, midi);
                for (int i = 0; i < 64; ++i)
                {
                    const auto step = std::abs (buffer.getSample (0, i) - previous);
                    previous = buffer.getSample (0, i);
                    if (block > 300 && block < 750) steadyStep = std::max (steadyStep, step);
                    if (block >= 750 && block < 800) switchStep = std::max (switchStep, step);
                }
            }
            const auto label = change == 0 ? std::string ("slope 12 -> Brickwall") : std::string ("type Low Cut -> Low Shelf (48 dB/oct)");
            note (label + " while running: max sample step " + format (switchStep, 4) + " vs steady " + format (steadyStep, 4));
            check (switchStep < steadyStep * 1.15f, label + ": no click (sample steps stay within the steady-state sine's)");
        }

        // 8. Dynamics still work with a steep shelf.
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 4, params::band::enabled, 1.0f);
            setBand (processor, 4, params::band::type, (float) T::highShelf);
            setBand (processor, 4, params::band::frequency, 2000.0f);
            setBand (processor, 4, params::band::slope, (float) S::db48);
            setBand (processor, 4, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, 4, params::band::range, -12.0f);
            setBand (processor, 4, params::band::threshold, -40.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto loud = measureSineGainDb (processor, 8000.0, 0.5f, sampleRate, 1.0);
            const auto dyn = processor.getBandDynamicGainDb (4);
            note ("48 dB/oct dynamic high shelf (range -12 dB): loud 8 kHz sine measures " + format (loud, 2) + " dB, dynamic gain " + format (dyn, 2) + " dB");
            check (dyn < -6.0 && loud < -6.0 && std::abs (loud - dyn) < 0.5, "dynamics drive a steep shelf (measured gain follows the dynamic gain)");
        }

        // 9. Old sessions (no slope parameters) load at 12 dB/oct.
        {
            DooblrQAudioProcessor source;
            for (int b = 0; b < params::numBands; ++b)
                setBand (source, b, params::band::slope, (float) S::db60);
            auto tree = source.getParameterState().copyState();
            for (int b = 0; b < params::numBands; ++b)
                tree.removeChild (tree.getChildWithProperty ("id", params::bandId (b, params::band::slope)), nullptr);
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);

            DooblrQAudioProcessor target;
            for (int b = 0; b < params::numBands; ++b)
                setBand (target, b, params::band::slope, (float) S::brickwall);
            target.setStateInformation (block.getData(), (int) block.getSize());
            auto all12 = true;
            for (int b = 0; b < params::numBands; ++b)
                all12 = all12 && target.getBandSettings (b).slope == S::db12;
            check (all12, "a session saved before 0.7.0 loads every band at 12 dB/oct");

            setBand (target, 2, params::band::slope, (float) S::db36);
            juce::MemoryBlock saved;
            target.getStateInformation (saved);
            DooblrQAudioProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());
            check (restored.getBandSettings (2).slope == S::db36, "a slope survives save/restore");
        }

        // 10. Editor: slope selector, right-click menu, undo,
        //     handles on the steep curves.
        {
            DooblrQAudioProcessor processor;
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            setBand (processor, 0, params::band::enabled, 1.0f);
            setBand (processor, 0, params::band::frequency, 80.0f);
            setBand (processor, 0, params::band::slope, (float) S::db48);
            setBand (processor, 1, params::band::enabled, 1.0f);
            setBand (processor, 1, params::band::type, (float) T::lowShelf);
            setBand (processor, 1, params::band::frequency, 250.0f);
            setBand (processor, 1, params::band::gain, 5.0f);
            setBand (processor, 1, params::band::slope, (float) S::db24);
            setBand (processor, 3, params::band::enabled, 1.0f);
            setBand (processor, 3, params::band::gain, -4.0f);
            setBand (processor, 5, params::band::enabled, 1.0f);
            setBand (processor, 5, params::band::frequency, 9000.0f);
            setBand (processor, 5, params::band::q, 1.4f);
            setBand (processor, 5, params::band::slope, (float) S::brickwall);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1300, 760);
            auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
            auto* slopeBox = dynamic_cast<juce::ComboBox*> (findById (*editor, params::band::slope));
            auto* editorImpl = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
            check (curve != nullptr && slopeBox != nullptr && editorImpl != nullptr, "editor has a slope selector");

            if (curve != nullptr && slopeBox != nullptr && editorImpl != nullptr)
            {
                check (slopeBox->getNumItems() == dsp::numSlopes && slopeBox->getItemText (5) == "Brickwall", "slope selector lists 12-60 dB/oct and Brickwall");

                editorImpl->selectBand (5);
                check (slopeBox->getSelectedId() == (int) S::brickwall + 1 && slopeBox->isEnabled(), "band 6 (High Cut) shows Brickwall, enabled");


                const auto cutMenu = curve->createSlopeMenu (0);
                int items = 0, ticked = -1;
                for (juce::PopupMenu::MenuItemIterator it (cutMenu); it.next();)
                    if (it.getItem().itemID > 0 && it.getItem().itemID <= dsp::numSlopes && it.getItem().isEnabled)
                    {
                        ++items;
                        if (it.getItem().isTicked) ticked = it.getItem().itemID - 1;
                    }
                check (items == dsp::numSlopes && ticked == (int) S::db48, "a cut's right-click menu offers all six slopes, current one ticked");

                // Undo: a slope change from the menu and from the selector are one step each.
                auto& history = processor.getUndoHistory();
                history.clear();
                curve->setBandSlope (0, S::db60);
                editorImpl->selectBand (0);
                slopeBox->setSelectedId ((int) S::brickwall + 1, juce::sendNotificationSync);
                check (processor.getBandSettings (0).slope == S::brickwall && history.getNumUndoableSteps() == 2,
                       "menu and selector slope changes are one undo step each");
                history.undo();
                check (processor.getBandSettings (0).slope == S::db60, "undo restores the menu's slope (60 dB/oct)");
                history.undo();
                check (processor.getBandSettings (0).slope == S::db48, "second undo restores 48 dB/oct");
                history.redo();
                check (processor.getBandSettings (0).slope == S::db60, "redo re-applies the slope change");
                curve->setBandSlope (0, S::db48);

                // Handles sit on the steep curves and the drawn curve shows the true slope.
                auto onCurve = true;
                for (const auto b : { 0, 1, 5 })
                {
                    const auto s = processor.getBandSettings (b);
                    // Against the band's own response (the drawn sum also contains the neighbouring bands).
                    const auto own = db (dsp::magnitudeAt (dsp::makeBandCoefficients (s.type, s.slope, s.frequency, s.q, s.gainDb, sampleRate), s.frequency, sampleRate));
                    const auto handleDb = curve->yToDecibels (curve->handlePosition (b).y);
                    note ("band " + std::to_string (b + 1) + " (" + params::slopeNames()[(int) s.slope].toStdString() + ") handle "
                          + format (handleDb, 2) + " dB, its own curve " + format (own, 2) + " dB");
                    onCurve = onCurve && std::abs (own - handleDb) < 0.1;
                }
                check (onCurve, "cut and shelf handles sit on their band's curve at every slope");
                const auto oneOctaveBelow = curve->responseDecibelsAt (40.0, false);
                note ("drawn curve: 48 dB/oct low cut at 80 Hz reads " + format (oneOctaveBelow, 1) + " dB at 40 Hz; brickwall high cut at 9 kHz reads "
                      + format (curve->responseDecibelsAt (9900.0, false), 1) + " dB at 9.9 kHz");
                check (oneOctaveBelow < -40.0, "the drawn curve shows the 48 dB/oct slope (-48 dB, plus the +5 dB low shelf)");
                check (curve->responseDecibelsAt (9900.0, false) < -20.0, "the drawn curve shows the brickwall");

                editorImpl->selectBand (5);
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.7.0");
            }

            processor.editorBeingDeleted (editor.get());
        }
    }

    //==============================================================================
    // 0.7.1: Slope for bells and notches.
    void testBellNotchSlopes()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        using T = dsp::FilterType;
        using S = dsp::Slope;
        constexpr double sampleRate = 48000.0;
        const auto db = [] (double magnitude) { return juce::Decibels::gainToDecibels (magnitude, -400.0); };
        constexpr S allSlopes[] { S::db12, S::db24, S::db36, S::db48, S::db60, S::brickwall };
        const auto slopeName = [] (S s) { return params::slopeNames()[(int) s].toStdString(); };
        constexpr double centre = 1000.0, q = 1.414; // about 1 octave wide
        const auto halfBw = std::pow (2.0, 0.5 * params::qToOctaves (q));
        const auto lowerEdge = centre / halfBw, upperEdge = centre * halfBw;

        // Responses on a fine log grid (1/192 octave) from centre / 64 to centre * 16.
        struct Shape { double maxSlope = 0.0, transition = 0.0, flatWidth = 0.0, rejectWidth = 0.0; };
        const auto analyse = [&] (const std::function<double (double)>& dbAt, double gainDb, bool isNotch)
        {
            Shape shape;
            constexpr int steps = 192 * 10;
            std::vector<double> f ((size_t) steps + 1), d ((size_t) steps + 1);
            for (int i = 0; i <= steps; ++i)
            {
                f[(size_t) i] = centre / 64.0 * std::pow (2.0, i / 192.0);
                d[(size_t) i] = dbAt (f[(size_t) i]);
            }
            // Max skirt slope: upper skirt (centre .. centre * 16), avoiding the notch's zero itself.
            const auto firstAbove = [&] (double hz) { int i = 0; while (f[(size_t) i] < hz) ++i; return i; };
            const auto c = firstAbove (centre);
            for (int i = c; i < steps; ++i)
            {
                if (isNotch && d[(size_t) i] < -60.0) continue;
                shape.maxSlope = std::max (shape.maxSlope, std::abs (d[(size_t) i + 1] - d[(size_t) i]) * 192.0);
            }
            // Octave positions above the centre where the response crosses a fraction of the gain.
            const auto crossing = [&] (double level, bool below)
            {
                for (int i = c; i < steps; ++i)
                    if (below ? (d[(size_t) i] <= level && d[(size_t) i + 1] > level) : (std::abs (d[(size_t) i]) >= std::abs (level) && std::abs (d[(size_t) i + 1]) < std::abs (level)))
                        return std::log2 (f[(size_t) i] / centre) + (level - d[(size_t) i]) / (d[(size_t) i + 1] - d[(size_t) i]) / 192.0;
                return 0.0;
            };
            if (! isNotch)
            {
                const auto at90 = crossing (0.9 * gainDb, false), at10 = crossing (0.1 * gainDb, false);
                shape.transition = 0.8 * std::abs (gainDb) / std::max (at10 - at90, 1.0e-6); // dB/oct between 90 % and 10 % of the gain
                shape.flatWidth = 2.0 * at90;                                                 // octaves within 10 % of the gain
            }
            else
            {
                const auto at20 = crossing (-20.0, true), at3 = crossing (-3.0103, true);
                shape.transition = 16.99 / std::max (at3 - at20, 1.0e-6); // dB/oct between -20 and -3 dB
                shape.rejectWidth = 2.0 * at20;                           // octaves below -20 dB
            }
            return shape;
        };

        // 1. Bells (+12 and -12 dB) and notches: analogue design and digital at 48 kHz.
        for (const auto type : { T::bell, T::notch })
            for (const auto gain : type == T::bell ? std::vector<double> { 12.0, -12.0 } : std::vector<double> { 0.0 })
            {
                const auto isNotch = type == T::notch;
                std::string line = std::string (isNotch ? "Notch" : (gain > 0 ? "Bell +12 dB" : "Bell -12 dB")) + " @ 1 kHz, 1 oct:";
                double previousMax = 0.0, previousTransition = 0.0, previousWidth = 0.0;
                auto monotonic = true;

                for (const auto slope : allSlopes)
                {
                    const auto name = slopeName (slope);
                    const auto p = dsp::makeBandPrototype (type, slope, q, gain);
                    const auto c = dsp::makeBandCoefficients (type, slope, centre, q, gain, sampleRate);
                    check (p.numSections == dsp::sectionsForSlope (slope) && c.numSections == p.numSections, name + ": section count follows the slope");
                    const auto analogDb = [&] (double hz) { return db (std::abs (dsp::analogResponseAt (p, centre, hz))); };
                    const auto digitalDb = [&] (double hz) { return db (dsp::magnitudeAt (c, hz, sampleRate)); };
                    const auto expectedEdge = isNotch ? -3.0103 : 0.5 * gain;

                    const auto aCentre = analogDb (centre), dCentre = digitalDb (centre);
                    const auto aLow = analogDb (lowerEdge), aHigh = analogDb (upperEdge);
                    const auto dLow = digitalDb (lowerEdge), dHigh = digitalDb (upperEdge);
                    if (isNotch)
                        check (aCentre < -150.0 && dCentre < -100.0, name + " notch: a zero at the centre (analogue " + format (aCentre, 0) + ", digital " + format (dCentre, 0) + " dB)");
                    else
                        check (std::abs (aCentre - gain) < 1.0e-6 && std::abs (dCentre - gain) < 1.0e-3, name + ": centre exactly at the gain (digital " + format (dCentre, 4) + " dB)");
                    check (std::abs (aLow - expectedEdge) < 1.0e-6 && std::abs (aHigh - expectedEdge) < 1.0e-6,
                           name + ": analogue Width edges exactly at " + format (expectedEdge, 2) + " dB");
                    check (std::abs (dLow - expectedEdge) < 0.15 && std::abs (dHigh - expectedEdge) < 0.15,
                           name + ": digital Width edges at " + format (dLow, 2) + " / " + format (dHigh, 2) + " dB");

                    const auto shape = analyse (analogDb, gain, isNotch);
                    const auto dShape = analyse (digitalDb, gain, isNotch);
                    line += " " + name + ": edges " + format (dLow, 2) + "/" + format (dHigh, 2) + ", max " + format (shape.maxSlope, 1)
                            + " dB/oct, " + (isNotch ? "-3..-20 dB " : "90..10 % ") + format (shape.transition, 1) + " dB/oct, "
                            + (isNotch ? "< -20 dB over " + format (shape.rejectWidth, 3) : "within 10 % over " + format (shape.flatWidth, 3)) + " oct;";
                    check (std::abs (dShape.maxSlope - shape.maxSlope) < 0.1 * shape.maxSlope + 1.0, name + ": digital skirt matches the analogue design at 1 kHz");

                    const auto width = isNotch ? shape.rejectWidth : shape.flatWidth;
                    if (slope != S::db12)
                        monotonic = monotonic && (isNotch || shape.maxSlope > previousMax) && shape.transition > previousTransition && width > previousWidth;
                    previousMax = shape.maxSlope; previousTransition = shape.transition; previousWidth = width;

                    if (slope == S::brickwall)
                    {
                        check (shape.transition > (isNotch ? 200.0 : 100.0), name + ": very steep edges (" + format (shape.transition, 0) + " dB/oct)");
                        if (isNotch)
                            check (shape.rejectWidth > 0.85, "Brickwall notch: below -20 dB over most of the Width (" + format (shape.rejectWidth, 2) + " of 1 oct)");
                        else
                        {
                            // Near-rectangular: flat within 10 % over most of the Width, and nearly 0 dB a little outside.
                            check (shape.flatWidth > 0.8, "Brickwall bell: within 10 % of the gain over " + format (shape.flatWidth, 2) + " of 1 oct");
                            const auto outside = analogDb (upperEdge * std::pow (2.0, 0.1));
                            check (std::abs (outside) < 0.1 * std::abs (gain), "Brickwall bell: within 10 % of 0 dB 0.1 oct outside the edge (" + format (outside, 2) + " dB)");
                        }
                    }
                }
                check (monotonic, line.substr (0, line.find (':')) + (isNotch ? ": edges get steeper, and the rejection band wider, at every step up in slope"
                                                                              : ": skirts get steeper, and the flat top wider, at every step up in slope"));
                note (line);
            }

        // 1b. Digital Width edges stay on their marks at high frequencies too (warp-corrected design).
        for (const auto type : { T::bell, T::notch })
        {
            std::string line = std::string (type == T::bell ? "Bell +12 dB" : "Notch") + ", 1 oct, digital 48 kHz, centre / edges (dB):";
            auto ok = true;
            for (const auto frequency : { 1000.0, 5000.0, 10000.0, 15000.0 })
                for (const auto slope : { S::db24, S::db60, S::brickwall })
                {
                    const auto c = dsp::makeBandCoefficients (type, slope, frequency, q, 12.0, sampleRate);
                    const auto at = [&] (double hz) { return db (dsp::magnitudeAt (c, hz, sampleRate)); };
                    const auto mid = at (frequency), lo = at (frequency / halfBw), hi = at (frequency * halfBw);
                    line += " " + format (frequency / 1000.0, 0) + "k " + slopeName (slope) + " " + format (mid, 3) + " / " + format (lo, 2) + ", " + format (hi, 2) + ";";
                    if (type == T::bell)
                        ok = ok && std::abs (mid - 12.0) < (frequency < 12000.0 ? 0.05 : 0.3) && std::abs (lo - 6.0) < 0.05 && std::abs (hi - 6.0) < 0.05;
                    else
                        ok = ok && mid < -60.0 && std::abs (lo + 3.01) < 0.05 && hi > -3.1 && (frequency > 2000.0 || std::abs (hi + 3.01) < 0.2);
                }
            note (line);
            check (ok, std::string (type == T::bell ? "bell: digital half-gain edges on their marks up to 15 kHz, centre within 0.05 dB up to 10 kHz (0.3 at 15 kHz)"
                                                    : "notch: digital null at the frequency and -3 dB at the lower edge up to 15 kHz; the upper -3 dB point no further out than its mark (within 0.2 dB of it at 1 kHz)"));
        }

        // 2. 12 dB/oct is still the pre-0.7.1 bell / notch, through the plugin too.
        for (const auto type : { T::bell, T::notch })
        {
            juce::AudioBuffer<float> outputs[2];
            for (int i = 0; i < 2; ++i)
            {
                DooblrQAudioProcessor processor;
                setBand (processor, 2, params::band::enabled, 1.0f);
                setBand (processor, 2, params::band::type, (float) type);
                setBand (processor, 2, params::band::gain, 6.0f);
                if (i == 1) setBand (processor, 2, params::band::slope, (float) S::db12);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                outputs[i].setSize (2, 8192);
                fillWithNoise (outputs[i], 5, 0.3f);
                processInBlocks (processor, outputs[i], { 512 });
            }
            check (bitIdentical (outputs[0], outputs[1], 2), params::filterTypeNames()[(int) type].toStdString() + ": 12 dB/oct output identical to the default (pre-0.7.1) band");
        }

        // 3. Measured with sines in every phase mode: centre, edges, and the skirt half an octave out.
        for (const auto mode : allPhaseModes)
            for (const auto type : { T::bell, T::notch })
            {
                std::string line = std::string (phaseName (mode)) + ", " + (type == T::bell ? "Bell +12 dB" : "Notch") + " 1 kHz 1 oct, measured:";
                double previousOutside = 1.0e9;
                auto steeper = true;
                for (const auto slope : { S::db12, S::db36, S::db60, S::brickwall })
                {
                    DooblrQAudioProcessor processor;
                    setParameter (processor, params::phaseMode, (float) mode);
                    setBand (processor, 2, params::band::enabled, 1.0f);
                    setBand (processor, 2, params::band::type, (float) type);
                    setBand (processor, 2, params::band::frequency, (float) centre);
                    setBand (processor, 2, params::band::q, (float) q);
                    setBand (processor, 2, params::band::gain, 12.0f);
                    setBand (processor, 2, params::band::slope, (float) slope);
                    prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                    const auto measure = [&] (double hz) { processor.reset(); return measureSineGainDb (processor, hz, 0.25f, sampleRate, 1.0); };
                    const auto mCentre = measure (centre), mLow = measure (lowerEdge), mHigh = measure (upperEdge);
                    const auto outside = measure (upperEdge * std::pow (2.0, 0.5));
                    const auto name = slopeName (slope);
                    line += " " + name + ": centre " + format (mCentre, 2) + ", edges " + format (mLow, 2) + "/" + format (mHigh, 2) + ", +0.5 oct out " + format (outside, 2) + ";";
                    const auto edge = type == T::bell ? 6.0 : -3.01;
                    if (type == T::bell)
                        check (std::abs (mCentre - 12.0) < 0.15, std::string (phaseName (mode)) + " " + name + " bell: measured centre at the gain");
                    else
                        check (mCentre < -30.0, std::string (phaseName (mode)) + " " + name + " notch: measured deep rejection at the centre (" + format (mCentre, 1) + " dB)");
                    check (std::abs (mLow - edge) < 0.3 && std::abs (mHigh - edge) < 0.3, std::string (phaseName (mode)) + " " + name + ": measured Width edges at " + format (edge, 2) + " dB");
                    // Outside the band: the steeper the slope, the closer to 0 dB.
                    const auto distance = std::abs (outside);
                    steeper = steeper && distance < previousOutside;
                    previousOutside = distance;
                }
                check (steeper, line.substr (0, line.find (',')) + std::string (type == T::bell ? " bell" : " notch") + ": higher slope, closer to 0 dB outside the Width");
                note (line);
            }

        // 4. Dynamics on a Brickwall bell (every phase mode), and the cascade stays finite at extremes.
        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::type, (float) T::bell);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            setBand (processor, 2, params::band::q, 1.414f);
            setBand (processor, 2, params::band::slope, (float) S::brickwall);
            setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, 2, params::band::range, -12.0f);
            setBand (processor, 2, params::band::threshold, -40.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto loud = measureSineGainDb (processor, 1000.0, 0.5f, sampleRate, 1.0);
            const auto dyn = processor.getBandDynamicGainDb (2);
            processor.reset();
            const auto outside = measureSineGainDb (processor, 2000.0, 0.5f, sampleRate, 1.0); // > 0.5 oct outside the edge
            note (std::string (phaseName (mode)) + ", dynamic Brickwall bell (range -12 dB): loud 1 kHz sine " + format (loud, 2)
                  + " dB, dynamic gain " + format (dyn, 2) + " dB; loud 2 kHz sine " + format (outside, 2) + " dB");
            check (dyn < -6.0 && std::abs (loud - dyn) < 0.5, std::string (phaseName (mode)) + ": dynamics drive a Brickwall bell (measured gain follows the dynamic gain)");
            check (std::abs (outside) < 0.5, std::string (phaseName (mode)) + ": a loud sine outside the Brickwall bell's Width passes untouched");
        }
        {
            auto finite = true;
            float peak = 0.0f;
            for (const auto type : { T::bell, T::notch })
                for (const auto frequency : { 20.0f, 1000.0f, 20000.0f })
                    for (const auto qValue : { params::minQ, params::maxQ })
                        for (const auto gain : { params::minGainDb, params::maxGainDb })
                        {
                            DooblrQAudioProcessor processor;
                            setBand (processor, 2, params::band::enabled, 1.0f);
                            setBand (processor, 2, params::band::type, (float) type);
                            setBand (processor, 2, params::band::frequency, frequency);
                            setBand (processor, 2, params::band::q, qValue);
                            setBand (processor, 2, params::band::gain, gain);
                            setBand (processor, 2, params::band::slope, (float) S::brickwall);
                            setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
                            setBand (processor, 2, params::band::range, gain > 0 ? -24.0f : 24.0f);
                            setBand (processor, 2, params::band::threshold, -50.0f);
                            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 256);
                            juce::AudioBuffer<float> buffer (2, 24000);
                            fillWithNoise (buffer, 9, 0.3f);
                            processInBlocks (processor, buffer, { 256, 17, 512 });
                            finite = finite && allFinite (buffer);
                            peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
                        }
            note ("Brickwall bell / notch at 20 Hz, 1 kHz, 20 kHz x Q 0.1 / 18 x gain -24 / +24 dB, dynamic, noise 0.3: peak output " + format (peak, 3));
            check (finite && peak < 20.0f, "Brickwall bell / notch stays finite and bounded at every frequency, Q and gain extreme");
        }

        // 5. Click-safe slope change on a running bell (sine at the centre, where every slope has the same gain).
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::type, (float) T::bell);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            setBand (processor, 2, params::band::gain, 6.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 64);
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            float previous = 0.0f, steadyStep = 0.0f, switchStep = 0.0f;
            int n = 0;
            for (int block = 0; block < 1500; ++block)
            {
                if (block == 750) setBand (processor, 2, params::band::slope, (float) S::brickwall);
                for (int i = 0; i < 64; ++i, ++n)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / sampleRate));
                processor.processBlock (buffer, midi);
                for (int i = 0; i < 64; ++i)
                {
                    const auto step = std::abs (buffer.getSample (0, i) - previous);
                    previous = buffer.getSample (0, i);
                    if (block > 300 && block < 750) steadyStep = std::max (steadyStep, step);
                    if (block >= 750 && block < 800) switchStep = std::max (switchStep, step);
                }
            }
            note ("bell slope 12 -> Brickwall while running: max sample step " + format (switchStep, 4) + " vs steady " + format (steadyStep, 4));
            check (switchStep < steadyStep * 1.15f, "bell slope change: no click (sample steps stay within the steady-state sine's)");
        }

        // 6. Editor: the selector and the right-click menu work for bells and notches; undo; handles.
        {
            DooblrQAudioProcessor processor;
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1300, 760);
            auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
            auto* slopeBox = dynamic_cast<juce::ComboBox*> (findById (*editor, params::band::slope));
            auto* editorImpl = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
            check (curve != nullptr && slopeBox != nullptr && editorImpl != nullptr, "editor has a slope selector");

            if (curve != nullptr && slopeBox != nullptr && editorImpl != nullptr)
            {
                // A lone bell on band 3: handle on the centre gain, width handles at half the gain, every slope.
                for (int b = 0; b < params::numBands; ++b)
                    setBand (processor, b, params::band::enabled, b == 2 ? 1.0f : 0.0f);
                setBand (processor, 2, params::band::type, (float) T::bell);
                setBand (processor, 2, params::band::frequency, 1000.0f);
                setBand (processor, 2, params::band::q, 1.414f);
                setBand (processor, 2, params::band::gain, 10.0f);
                editorImpl->selectBand (2);
                check (slopeBox->isEnabled() && slopeBox->getTooltip().contains ("Bell"), "a Bell band's slope selector is enabled, with a Bell tooltip");
                std::string line = "lone +10 dB bell, handle / width handles (dB):";
                auto handlesOk = true;
                for (const auto slope : allSlopes)
                {
                    curve->setBandSlope (2, slope);
                    const auto handleDb = curve->yToDecibels (curve->handlePosition (2).y);
                    const auto [lower, upper] = curve->widthHandlePositions (2);
                    const auto lowDb = curve->yToDecibels (lower.y), highDb = curve->yToDecibels (upper.y);
                    line += " " + slopeName (slope) + " " + format (handleDb, 2) + " / " + format (lowDb, 2) + ", " + format (highDb, 2) + ";";
                    handlesOk = handlesOk && std::abs (handleDb - 10.0) < 0.1 && std::abs (lowDb - 5.0) < 0.2 && std::abs (highDb - 5.0) < 0.2;
                }
                note (line);
                check (handlesOk, "bell handle stays on the gain and width handles on half the gain at every slope");
                const auto drawnOutside = curve->responseDecibelsAt (1000.0 * std::pow (2.0, 0.7), false);
                check (drawnOutside < 1.0, "the drawn curve shows the Brickwall bell's steep skirt (" + format (drawnOutside, 2) + " dB 0.7 oct out)");

                setBand (processor, 2, params::band::type, (float) T::notch);
                editorImpl->selectBand (2);
                check (slopeBox->isEnabled() && slopeBox->getTooltip().contains ("Notch"), "a Notch band's slope selector is enabled, with a Notch tooltip");
                setBand (processor, 2, params::band::type, (float) T::bell);

                setBand (processor, 2, params::band::slope, (float) S::db48);
                const auto bellMenu = curve->createSlopeMenu (2);
                int items = 0, ticked = -1;
                for (juce::PopupMenu::MenuItemIterator it (bellMenu); it.next();)
                    if (it.getItem().itemID > 0 && it.getItem().itemID <= dsp::numSlopes && it.getItem().isEnabled)
                    {
                        ++items;
                        if (it.getItem().isTicked) ticked = it.getItem().itemID - 1;
                    }
                check (items == dsp::numSlopes && ticked == (int) S::db48, "a Bell's right-click menu offers all six slopes, current one ticked");

                auto& history = processor.getUndoHistory();
                history.clear();
                curve->setBandSlope (2, S::db60);
                editorImpl->selectBand (2);
                slopeBox->setSelectedId ((int) S::brickwall + 1, juce::sendNotificationSync);
                check (processor.getBandSettings (2).slope == S::brickwall && history.getNumUndoableSteps() == 2, "bell: menu and selector slope changes are one undo step each");
                history.undo();
                check (processor.getBandSettings (2).slope == S::db60, "bell: undo restores 60 dB/oct");
                history.undo();
                check (processor.getBandSettings (2).slope == S::db48, "bell: second undo restores 48 dB/oct");

                // Snapshot: bells and a notch at different slopes.
                struct B { T type; float frequency, gain, q; S slope; };
                const B bands[] { { T::bell, 60.0f, 6.0f, 1.414f, S::db12 },    { T::bell, 220.0f, -8.0f, 1.414f, S::db24 },
                                  { T::bell, 700.0f, 9.0f, 1.414f, S::db48 },   { T::notch, 2200.0f, 0.0f, 2.0f, S::brickwall },
                                  { T::bell, 5500.0f, -10.0f, 1.414f, S::brickwall }, { T::bell, 14000.0f, 6.0f, 1.414f, S::db36 } };
                for (int b = 0; b < params::numBands; ++b)
                {
                    setBand (processor, b, params::band::enabled, 1.0f);
                    setBand (processor, b, params::band::type, (float) bands[b].type);
                    setBand (processor, b, params::band::frequency, bands[b].frequency);
                    setBand (processor, b, params::band::gain, bands[b].gain);
                    setBand (processor, b, params::band::q, bands[b].q);
                    setBand (processor, b, params::band::slope, (float) bands[b].slope);
                }
                editorImpl->selectBand (4);
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.7.1");
            }

            processor.editorBeingDeleted (editor.get());
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

        // Worst case for 0.7.0: all six bands steep (cuts / shelves / tilt on Brickwall, i.e.
        // 16 sections each; band 4 a dynamic Brickwall shelf so its cascade is recomputed per block).
        std::string worst = "CPU, all 6 bands Brickwall (16 sections each), stereo @ 48 kHz, 512-sample blocks:";
        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            const dsp::FilterType types[] { dsp::FilterType::lowCut, dsp::FilterType::lowShelf, dsp::FilterType::tilt,
                                            dsp::FilterType::highShelf, dsp::FilterType::lowShelf, dsp::FilterType::highCut };
            const float freqs[] { 40.0f, 150.0f, 800.0f, 3000.0f, 6000.0f, 15000.0f };
            for (int b = 0; b < params::numBands; ++b)
            {
                setBand (processor, b, params::band::enabled, 1.0f);
                setBand (processor, b, params::band::type, (float) types[b]);
                setBand (processor, b, params::band::frequency, freqs[b]);
                setBand (processor, b, params::band::gain, b % 2 == 0 ? 3.0f : -3.0f);
                setBand (processor, b, params::band::slope, (float) dsp::Slope::brickwall);
            }
            setBand (processor, 3, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, 3, params::band::range, -9.0f);
            setBand (processor, 3, params::band::threshold, -30.0f);
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
            worst += std::string (" ") + phaseName (mode) + " " + format (100.0 * elapsed / (seconds * 1000.0), 2) + " % of one core";
            check (elapsed < seconds * 1000.0 && allFinite (buffer), std::string (phaseName (mode)) + ": all-Brickwall runs faster than real time, finite output");
        }
        note (worst);

        // Worst case for 0.7.1: all six bands Brickwall bells, all dynamic (every cascade
        // redesigned as the gain moves).
        std::string bells = "CPU, all 6 bands Brickwall bells (16 sections each), all dynamic, stereo @ 48 kHz, 512-sample blocks:";
        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            const float freqs[] { 60.0f, 200.0f, 700.0f, 2000.0f, 5000.0f, 12000.0f };
            for (int b = 0; b < params::numBands; ++b)
            {
                setBand (processor, b, params::band::enabled, 1.0f);
                setBand (processor, b, params::band::type, (float) dsp::FilterType::bell);
                setBand (processor, b, params::band::frequency, freqs[b]);
                setBand (processor, b, params::band::q, 1.4f);
                setBand (processor, b, params::band::gain, b % 2 == 0 ? 3.0f : -3.0f);
                setBand (processor, b, params::band::slope, (float) dsp::Slope::brickwall);
                setBand (processor, b, params::band::dynamicsEnabled, 1.0f);
                setBand (processor, b, params::band::range, -9.0f);
                setBand (processor, b, params::band::threshold, -30.0f);
            }
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
            bells += std::string (" ") + phaseName (mode) + " " + format (100.0 * elapsed / (seconds * 1000.0), 2) + " % of one core";
            check (elapsed < seconds * 1000.0 && allFinite (buffer), std::string (phaseName (mode)) + ": all-Brickwall dynamic bells run faster than real time, finite output");
        }
        note (bells);

        // 0.8.0: all six bands on one analogue model (each with its 4x oversampled colour stage),
        // mixed types, all dynamic, Drive +6 dB; Digital with the same settings for reference.
        std::string models = "CPU per model, 6 bands (cut, shelves, 2 bells, cut), all dynamic, Drive +6, stereo @ 48 kHz, 512-sample blocks:";
        for (const auto model : { dsp::Model::digital, dsp::Model::neve, dsp::Model::pultec, dsp::Model::helios })
        {
            models += std::string ("\n      ") + dsp::modelName (model) + ":";
            for (const auto mode : allPhaseModes)
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                const dsp::FilterType types[] { dsp::FilterType::lowCut, dsp::FilterType::lowShelf, dsp::FilterType::bell,
                                                dsp::FilterType::bell, dsp::FilterType::highShelf, dsp::FilterType::highCut };
                const float freqs[] { 40.0f, 100.0f, 700.0f, 3000.0f, 10000.0f, 16000.0f };
                for (int b = 0; b < params::numBands; ++b)
                {
                    setBand (processor, b, params::band::enabled, 1.0f);
                    setBand (processor, b, params::band::type, (float) types[b]);
                    setBand (processor, b, params::band::frequency, freqs[b]);
                    setBand (processor, b, params::band::gain, b % 2 == 0 ? 4.0f : -4.0f);
                    setBand (processor, b, params::band::model, (float) model);
                    setBand (processor, b, params::band::drive, 6.0f);
                    setBand (processor, b, params::band::atten, 4.0f);
                    setBand (processor, b, params::band::dynamicsEnabled, 1.0f);
                    setBand (processor, b, params::band::range, -6.0f);
                    setBand (processor, b, params::band::threshold, -30.0f);
                }
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                juce::Random random (13);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                for (int block = 0; block < seconds * 48000 / 512; ++block)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            buffer.setSample (ch, i, (random.nextFloat() - 0.5f) * ((block / 40) % 2 == 0 ? 1.0f : 0.05f));
                    processor.processBlock (buffer, midi);
                }
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
                models += std::string (" ") + phaseName (mode) + " " + format (100.0 * elapsed / (seconds * 1000.0), 2) + " %";
                check (elapsed < seconds * 1000.0 && allFinite (buffer),
                       std::string (dsp::modelName (model)) + ", " + phaseName (mode) + ": faster than real time, finite output");
            }
        }
        note (models + "   (of one core)");
    }


    //==============================================================================
    // 0.8.0: Digital (the default model) must sound exactly like 0.7.1. The golden hashes
    // and the saved state below were produced by the 0.7.1 build (commit 8d7bd9b).
    std::uint64_t hashBuffer (const juce::AudioBuffer<float>& buffer)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*> (buffer.getReadPointer (ch));
            for (size_t i = 0; i < sizeof (float) * (size_t) buffer.getNumSamples(); ++i)
                h = (h ^ bytes[i]) * 1099511628211ull;
        }
        return h;
    }

    void configureGoldenEq (DooblrQAudioProcessor& processor, int variant)
    {
        const dsp::FilterType typesA[] { dsp::FilterType::lowCut, dsp::FilterType::lowShelf, dsp::FilterType::bell,
                                         dsp::FilterType::bell, dsp::FilterType::highShelf, dsp::FilterType::highCut };
        const dsp::FilterType typesB[] { dsp::FilterType::lowShelf, dsp::FilterType::notch, dsp::FilterType::tilt,
                                         dsp::FilterType::bell, dsp::FilterType::bell, dsp::FilterType::highShelf };
        const float freqs[] { 45.0f, 160.0f, 700.0f, 2200.0f, 6000.0f, 14000.0f };
        for (int b = 0; b < params::numBands; ++b)
        {
            setBand (processor, b, params::band::enabled, 1.0f);
            setBand (processor, b, params::band::type, (float) (variant == 0 ? typesA[b] : typesB[b]));
            setBand (processor, b, params::band::frequency, freqs[b]);
            setBand (processor, b, params::band::gain, (b % 2 == 0 ? 4.5f : -6.0f) * (variant == 0 ? 1.0f : 1.5f));
            setBand (processor, b, params::band::q, 0.6f + 0.4f * (float) b);
            setBand (processor, b, params::band::slope, (float) ((b + variant * 3) % dsp::numSlopes));
        }
        setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 2, params::band::range, variant == 0 ? -9.0f : 6.0f);
        setBand (processor, 2, params::band::threshold, -24.0f);
        setBand (processor, 4, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 4, params::band::range, -6.0f);
        setBand (processor, 4, params::band::threshold, -30.0f);
        setParameter (processor, params::outputGain, -1.5f);
        setParameter (processor, params::stereoLink, variant == 0 ? 1.0f : 0.0f);
    }

    std::uint64_t goldenRun (DooblrQAudioProcessor& processor, int mode)
    {
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 96000);
        fillWithNoise (buffer, 2024 + mode);
        for (int i = 0; i < buffer.getNumSamples(); ++i)  // level changes so dynamics move
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * ((i / 12000) % 2 == 0 ? 0.8f : 0.03f) * (ch == 0 ? 1.0f : 0.7f));
        processInBlocks (processor, buffer, { 512, 37, 128, 1, 300 });
        return hashBuffer (buffer);
    }

    // Saved by 0.7.1 from configureGoldenEq (variant 0 and 1) with phase mode Zero Latency.
    const char* const goldenState071[] {
        "2877.VMjLgPyB...O+fWarAhckI2bo8la8HRLt.iHfTlai8FYo41Y8HRUTYTK3HxO9.BOD81ahwlbQMEcgQWYfLGcgQWYVUlbyk1at0iHwHhO7.UPRETSfjFY8HhXw7EY441WgQGcgM1Zh.hcgwVck0iHw.iKvHxK9vCTAIUPMARZj0iHhEyWjkmae8lah.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhXw7EY441WxElamUlHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHVLeQVdt8kbkwVYgMWYh.hcgwVck0iHwDSNtjSN4jSNxLyMvXCL0TiHu3COPEjTA0DHoQVOhHVLeQVdt8EcnIWYyg1arQlHfXWXrUWY8HRKxLiK4jSN4jCNvjiL1TSLybiHu3COPEjTA0DHoQVOhHVLeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHVLeYlbkEmHfXWXrUWY8HBMz3RN4jSN4XSL3TyLvHyMyHxK9vCTAIUPMARZj0iHhEyWmEVZtIBH1EFa0UVOhPiK0HxK9vCTAIUPMARZj0iHhEyWwIBH1EFa0UVOh.iK1.CLv.CLvfyLzPiM0.iL2HxK9vCTAIUPMARZj0iHhEyWyw1avUlHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHVLeQWdvUlHfXWXrUWY8HxLt.iHu3COPEjTA0DHoQVOhHlLeQVdt8UXzQWXislHfXWXrUWY8HRLv3BLh7hO7.UPRETSfjFY8HhXx7EY441Wu4lHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHlLeQVdt8kbg41YkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhIyWjkmaeIWYrUVXyUlHfXWXrUWY8HRLwjiK4jSN4jiLybCL1.SM0HxK9vCTAIUPMARZj0iHhIyWjkmaeQGZxU1bn8FajIBH1EFa0UVOhzhLy3RN4jSN4fCL4HiM0DyL2HxK9vCTAIUPMARZj0iHhIyWk4VXhwVYjIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhIyWlIWYwIBH1EFa0UVOhDSM43RN4jSN0PiLxLiMyHCNh7hO7.UPRETSfjFY8HhXx70Ygklah.hcgwVck0iHsXiKvHxK9vCTAIUPMARZj0iHhIyWwIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhIyWyw1avUlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHlLeQWdvUlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhH1LeQVdt8UXzQWXislHfXWXrUWY8HRLv3BLh7hO7.UPRETSfjFY8HhXy7EY441Wu4lHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhH1LeQVdt8kbg41YkIBH1EFa0UVOhzRNt.iHu3COPEjTA0DHoQVOhH1LeQVdt8kbkwVYgMWYh.hcgwVck0iHwDSNtjSN4jSNxLyMvXCL0TiHu3COPEjTA0DHoQVOhH1LeQVdt8EcnIWYyg1arQlHfXWXrUWY8HRKxLiK4jSN4jCNvjiL1TSLybiHu3COPEjTA0DHoQVOhH1LeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhH1LeYlbkEmHfXWXrUWY8HhM4jiK4jSN4LCN4XCM3PyL3HxK9vCTAIUPMARZj0iHhMyWmEVZtIBH1EFa0UVOhPiK0HxK9vCTAIUPMARZj0iHhMyWwIBH1EFa0UVOhDiKyjSN4jSN3TiM4PCN3TyLh7hO7.UPRETSfjFY8HhXy70br8FbkIBH1EFa0UVOhHiKvHxK9vCTAIUPMARZj0iHhMyWzkGbkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhQyWjkmaeEFczE1XqIBH1EFa0UVOhDCLt.iHu3COPEjTA0DHoQVOhHFMeQVdt80atIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhQyWjkmaeIWXtcVYh.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhXz7EY441WxUFakE1bkIBH1EFa0UVOhDSL43RN4jSN4HyL2.iMvTSMh7hO7.UPRETSfjFY8HhXz7EY441WzglbkMGZuwFYh.hcgwVck0iHsHyLtjSN4jSN3.SNxXSMwLyMh7hO7.UPRETSfjFY8HhXz7UYtElXrUFYh.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhXz7kYxUVbh.hcgwVck0iHxDSN43RN4jyM0TCN0jyL2TiHu3COPEjTA0DHoQVOhHFMecVXo4lHfXWXrUWY8HRK13BLh7hO7.UPRETSfjFY8HhXz7Ubh.hcgwVck0iHw3BNv.CLv.CL2DSMxTSM2PiHu3COPEjTA0DHoQVOhHFMeMGauAWYh.hcgwVck0iHy3BLh7hO7.UPRETSfjFY8HhXz7Ec4AWYh.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhX07EY441WgQGcgM1Zh.hcgwVck0iHw.iKvHxK9vCTAIUPMARZj0iHhUyWjkmae8lah.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhX07EY441WxElamUlHfXWXrUWY8HRK13BLh7hO7.UPRETSfjFY8HhX07EY441WxUFakE1bkIBH1EFa0UVOhDSL43RN4jSN4HyL2.iMvTSMh7hO7.UPRETSfjFY8HhX07EY441WzglbkMGZuwFYh.hcgwVck0iHsLCLt.iHu3COPEjTA0DHoQVOhHVMeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHVMeYlbkEmHfXWXrUWY8HhMv.CLt.iHu3COPEjTA0DHoQVOhHVMecVXo4lHfXWXrUWY8HBMtTiHu3COPEjTA0DHoQVOhHVMeEmHfXWXrUWY8HhLtHCLv.CLvTiLzTiLvfyMzHxK9vCTAIUPMARZj0iHhUyWyw1avUlHfXWXrUWY8HBMt.iHu3COPEjTA0DHoQVOhHVMeQWdvUlHfXWXrUWY8HhLt.iHu3COPEjTA0DHoQVOhHlMeQVdt8UXzQWXislHfXWXrUWY8HRLv3BLh7hO7.UPRETSfjFY8HhX17EY441Wu4lHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHlMeQVdt8kbg41YkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhYyWjkmaeIWYrUVXyUlHfXWXrUWY8HRLwjiK4jSN4jiLybCL1.SM0HxK9vCTAIUPMARZj0iHhYyWjkmaeQGZxU1bn8FajIBH1EFa0UVOhzhLy3RN4jSN4fCL4HiM0DyL2HxK9vCTAIUPMARZj0iHhYyWk4VXhwVYjIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhYyWlIWYwIBH1EFa0UVOhDyL4jSNtjSN3.CM1fyM0HxK9vCTAIUPMARZj0iHhYyWmEVZtIBH1EFa0UVOhzhMt.iHu3COPEjTA0DHoQVOhHlMeEmHfXWXrUWY8HhLtXCLv.CLvDCMy.SMwDCM2HxK9vCTAIUPMARZj0iHhYyWyw1avUlHfXWXrUWY8HRMt.iHu3COPEjTA0DHoQVOhHlMeQWdvUlHfXWXrUWY8HBMt.iHu3COPEjTA0DHoQVOh7VczAWcz80Ygklah.hcgwVck0iHsDiK0HxK9vCTAIUPMARZj0iHvgVXyU1Ws8FYkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHyQWYxU1aewVZtslHfXWXrUWY8HRLt.iHu3COuPzauIFaxE0TzEFck4C.",
        "2879.VMjLgXyB...O+fWarAhckI2bo8la8HRLt.iHfTlai8FYo41Y8HRUTYTK3HxO9.BOD81ahwlbQMEcgQWYfLGcgQWYVUlbyk1at0iHwHhO7.UPRETSfjFY8HhXw7EY441WgQGcgM1Zh.hcgwVck0iHw.iKvHxK9vCTAIUPMARZj0iHhEyWjkmae8lah.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhXw7EY441WxElamUlHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHVLeQVdt8kbkwVYgMWYh.hcgwVck0iHwDSNtjSN4jSNxLyMvXCL0TiHu3COPEjTA0DHoQVOhHVLeQVdt8EcnIWYyg1arQlHfXWXrUWY8HRKxLiK4jSN4jCNvjiL1TSLybiHu3COPEjTA0DHoQVOhHVLeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHVLeYlbkEmHfXWXrUWY8HBMz3RN4jSN4XSL3TyLvHyMyHxK9vCTAIUPMARZj0iHhEyWmEVZtIBH1EFa0UVOhXiK2TiHu3COPEjTA0DHoQVOhHVLeEmHfXWXrUWY8HBLtXCLv.CLv.CNyPCM1TCLxbiHu3COPEjTA0DHoQVOhHVLeMGauAWYh.hcgwVck0iHy3BLh7hO7.UPRETSfjFY8HhXw7Ec4AWYh.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhXx7EY441WgQGcgM1Zh.hcgwVck0iHw.iKvHxK9vCTAIUPMARZj0iHhIyWjkmae8lah.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhXx7EY441WxElamUlHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHlLeQVdt8kbkwVYgMWYh.hcgwVck0iHwDSNtjSN4jSNxLyMvXCL0TiHu3COPEjTA0DHoQVOhHlLeQVdt8EcnIWYyg1arQlHfXWXrUWY8HRKxLiK4jSN4jCNvjiL1TSLybiHu3COPEjTA0DHoQVOhHlLeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHlLeYlbkEmHfXWXrUWY8HRL0jiK4jSN4TCMxHyL1LiL3HxK9vCTAIUPMARZj0iHhIyWmEVZtIBH1EFa0UVOhzRNt.iHu3COPEjTA0DHoQVOhHlLeEmHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhHlLeMGauAWYh.hcgwVck0iHz3BLh7hO7.UPRETSfjFY8HhXx7Ec4AWYh.hcgwVck0iH03BLh7hO7.UPRETSfjFY8HhXy7EY441WgQGcgM1Zh.hcgwVck0iHw.iKvHxK9vCTAIUPMARZj0iHhMyWjkmae8lah.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhXy7EY441WxElamUlHfXWXrUWY8HhMt.iHu3COPEjTA0DHoQVOhH1LeQVdt8kbkwVYgMWYh.hcgwVck0iHwDSNtjSN4jSNxLyMvXCL0TiHu3COPEjTA0DHoQVOhH1LeQVdt8EcnIWYyg1arQlHfXWXrUWY8HRKxLiK4jSN4jCNvjiL1TSLybiHu3COPEjTA0DHoQVOhH1LeUlagIFakQlHfXWXrUWY8HRLt.iHu3COPEjTA0DHoQVOhH1LeYlbkEmHfXWXrUWY8HhM4jiK4jSN4LCN4XCM3PyL3HxK9vCTAIUPMARZj0iHhMyWmEVZtIBH1EFa0UVOhXiK2TiHu3COPEjTA0DHoQVOhH1LeEmHfXWXrUWY8HRLtLSN4jSN4fSM1jCM3fSMyHxK9vCTAIUPMARZj0iHhMyWyw1avUlHfXWXrUWY8HRMt.iHu3COPEjTA0DHoQVOhH1LeQWdvUlHfXWXrUWY8HhMt.iHu3COPEjTA0DHoQVOhHFMeQVdt8UXzQWXislHfXWXrUWY8HRLv3BLh7hO7.UPRETSfjFY8HhXz7EY441Wu4lHfXWXrUWY8HBLt.iHu3COPEjTA0DHoQVOhHFMeQVdt8kbg41YkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhQyWjkmaeIWYrUVXyUlHfXWXrUWY8HRLwjiK4jSN4jiLybCL1.SM0HxK9vCTAIUPMARZj0iHhQyWjkmaeQGZxU1bn8FajIBH1EFa0UVOhzhLy3RN4jSN4fCL4HiM0DyL2HxK9vCTAIUPMARZj0iHhQyWk4VXhwVYjIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhQyWlIWYwIBH1EFa0UVOhHSL4jiK4jSN2TSM3TSNybSMh7hO7.UPRETSfjFY8HhXz70Ygklah.hcgwVck0iHsjiKvHxK9vCTAIUPMARZj0iHhQyWwIBH1EFa0UVOhDiK3.CLv.CLvbSL0HSM0bCMh7hO7.UPRETSfjFY8HhXz70br8FbkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhQyWzkGbkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhUyWjkmaeEFczE1XqIBH1EFa0UVOhDCLt.iHu3COPEjTA0DHoQVOhHVMeQVdt80atIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhUyWjkmaeIWXtcVYh.hcgwVck0iHsXiKvHxK9vCTAIUPMARZj0iHhUyWjkmaeIWYrUVXyUlHfXWXrUWY8HRLwjiK4jSN4jiLybCL1.SM0HxK9vCTAIUPMARZj0iHhUyWjkmaeQGZxU1bn8FajIBH1EFa0UVOhzxLv3BLh7hO7.UPRETSfjFY8HhX07UYtElXrUFYh.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhX07kYxUVbh.hcgwVck0iH1.CLv3BLh7hO7.UPRETSfjFY8HhX070Ygklah.hcgwVck0iH13xM0HxK9vCTAIUPMARZj0iHhUyWwIBH1EFa0UVOhHiKx.CLv.CL0HCM0HCL3bCMh7hO7.UPRETSfjFY8HhX070br8FbkIBH1EFa0UVOhDiKvHxK9vCTAIUPMARZj0iHhUyWzkGbkIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhYyWjkmaeEFczE1XqIBH1EFa0UVOhDCLt.iHu3COPEjTA0DHoQVOhHlMeQVdt80atIBH1EFa0UVOh.iKvHxK9vCTAIUPMARZj0iHhYyWjkmaeIWXtcVYh.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HhX17EY441WxUFakE1bkIBH1EFa0UVOhDSL43RN4jSN4HyL2.iMvTSMh7hO7.UPRETSfjFY8HhX17EY441WzglbkMGZuwFYh.hcgwVck0iHsHyLtjSN4jSN3.SNxXSMwLyMh7hO7.UPRETSfjFY8HhX17UYtElXrUFYh.hcgwVck0iHw3BLh7hO7.UPRETSfjFY8HhX17kYxUVbh.hcgwVck0iHwLSN4jiK4jCNvPiM3bSMh7hO7.UPRETSfjFY8HhX170Ygklah.hcgwVck0iHsjiKvHxK9vCTAIUPMARZj0iHhYyWwIBH1EFa0UVOhHiK1.CLv.CLwPyLvTSLwPyMh7hO7.UPRETSfjFY8HhX170br8FbkIBH1EFa0UVOhHiKvHxK9vCTAIUPMARZj0iHhYyWzkGbkIBH1EFa0UVOhHiKvHxK9vCTAIUPMARZj0iHuUGcvUGcecVXo4lHfXWXrUWY8HRKw3RMh7hO7.UPRETSfjFY8HBbnE1bk8UauQVYh.hcgwVck0iHv3BLh7hO7.UPRETSfjFY8HxbzUlbk81WrklaqIBH1EFa0UVOh.iKvHxK9vyKD81ahwlbQMEcgQWY9.." };
    // The same 0.7.1 build loading goldenState071 (then setting the phase mode): a loaded session
    // differs from one configured by parameter calls in 0.7.1 too, so it has its own reference.
    constexpr std::uint64_t goldenLoadHash071[2][3] { { 8881870267597058686ull, 17602331995269512671ull, 736504212581514666ull }, { 18361192930680165835ull, 5193833403056371000ull, 13193493206338719672ull } };
    constexpr std::uint64_t goldenHash071[2][3] { { 12293026835121715796ull, 15259565978756077211ull, 15257821685803047736ull }, { 15353161111764110854ull, 15996728495184292435ull, 12518503497226783880ull } };

    void testDigitalMatches071()
    {
        for (int variant = 0; variant < 2; ++variant)
            for (int mode = 0; mode < 3; ++mode)
            {
                {
                    DooblrQAudioProcessor processor;
                    setParameter (processor, params::phaseMode, (float) mode);
                    configureGoldenEq (processor, variant);
                    check (goldenRun (processor, mode) == goldenHash071[variant][mode],
                           "Digital model output is bit-identical to 0.7.1 (variant " + std::to_string (variant)
                               + ", " + phaseName ((PhaseMode) mode) + ")");
                }
                {
                    // A session saved by 0.7.1 (no model parameters in it) loads as Digital.
                    DooblrQAudioProcessor processor;
                    for (int b = 0; b < params::numBands; ++b)
                        setBand (processor, b, params::band::model, (float) dsp::Model::neve); // must be reset
                    juce::MemoryBlock state;
                    state.fromBase64Encoding (goldenState071[variant]);
                    processor.setStateInformation (state.getData(), (int) state.getSize());
                    auto allDigital = true;
                    for (int b = 0; b < params::numBands; ++b)
                        allDigital = allDigital && juce::roundToInt (getParameter (processor, params::bandId (b, params::band::model))) == 0;
                    check (allDigital, "0.7.1 session loads with every band on Digital");
                    setParameter (processor, params::phaseMode, (float) mode);
                    check (goldenRun (processor, mode) == goldenLoadHash071[variant][mode],
                           "0.7.1 session sounds bit-identical (variant " + std::to_string (variant)
                               + ", " + phaseName ((PhaseMode) mode) + ")");
                }
            }
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
    //==============================================================================
    // 0.8.0 analogue models
    using dsp::Model;

    double protoDb (const dsp::BandPrototype& p, double bandHz, double hz)
    {
        return juce::Decibels::gainToDecibels (std::abs (dsp::analogResponseAt (p, bandHz, hz)), -200.0);
    }

    /** Width (octaves) of a bell between the points at half its centre dB gain. */
    double bellWidthOctaves (Model model, double q, double gainDb)
    {
        const auto p = dsp::makeModelPrototype (model, dsp::FilterType::bell, dsp::Slope::db12, q, gainDb, 0.0);
        const auto centre = protoDb (p, 1000.0, 1000.0);
        const auto half = centre * 0.5;
        const auto edge = [&] (double direction)
        {
            double lo = 0.0, hi = 6.0; // octaves away from the centre
            for (int i = 0; i < 60; ++i)
            {
                const auto mid = 0.5 * (lo + hi);
                const auto db = protoDb (p, 1000.0, 1000.0 * std::pow (2.0, direction * mid));
                ((gainDb > 0.0 ? db > half : db < half) ? lo : hi) = mid;
            }
            return 0.5 * (lo + hi);
        };
        return edge (-1.0) + edge (1.0);
    }

    void testModelShapes()
    {
        // Digital is the 0.7.1 function, untouched.
        auto digitalSame = true;
        for (int t = 0; t < dsp::numFilterTypes; ++t)
            for (int sl = 0; sl < dsp::numSlopes; ++sl)
            {
                const auto a = dsp::makeModelPrototype (Model::digital, (dsp::FilterType) t, (dsp::Slope) sl, 1.3, 7.0, 5.0);
                const auto b = dsp::makeBandPrototype ((dsp::FilterType) t, (dsp::Slope) sl, 1.3, 7.0);
                digitalSame = digitalSame && a.numSections == b.numSections;
                for (int i = 0; digitalSame && i < a.numSections; ++i)
                {
                    const auto& x = a.sections[(size_t) i];
                    const auto& y = b.sections[(size_t) i];
                    digitalSame = juce::exactlyEqual (x.scale, y.scale) && juce::exactlyEqual (x.k, y.k) && juce::exactlyEqual (x.m0, y.m0)
                                  && juce::exactlyEqual (x.m1, y.m1) && juce::exactlyEqual (x.m2, y.m2);
                }
            }
        check (digitalSame, "Digital model prototype is exactly the 0.7.1 band prototype (every type and slope)");

        // SSL: E keeps its bell Q constant with gain; G narrows as the gain rises.
        const auto eLow = bellWidthOctaves (Model::sslE, 1.0, 3.0), eHigh = bellWidthOctaves (Model::sslE, 1.0, 15.0);
        const auto gLow = bellWidthOctaves (Model::sslG, 1.0, 3.0), gHigh = bellWidthOctaves (Model::sslG, 1.0, 15.0);
        note ("SSL bell width (oct) at +3 / +15 dB: E " + format (eLow, 3) + " / " + format (eHigh, 3)
              + ", G " + format (gLow, 3) + " / " + format (gHigh, 3));
        check (std::abs (eHigh / eLow - 1.0) < 0.05, "SSL E: bell Q does not change with gain");
        check (gHigh / gLow < 0.8, "SSL G: bell narrows (Q rises) as gain rises");
        check (std::abs (gLow / eLow - 1.0) > 0.15, "SSL E and G bells differ at low gain");

        // API: proportional Q, narrows monotonically as gain rises (boost and cut).
        std::string apiLine = "API bell width (oct) at 2/4/8/12 dB:";
        auto narrowing = true;
        for (const auto sign : { 1.0, -1.0 })
        {
            auto previous = 1.0e9;
            for (const auto g : { 2.0, 4.0, 8.0, 12.0 })
            {
                const auto w = bellWidthOctaves (Model::api, 1.0, sign * g);
                apiLine += " " + format (w, 3);
                narrowing = narrowing && w < previous;
                previous = w;
            }
        }
        note (apiLine);
        check (narrowing, "API: bell narrows as gain rises (boost and cut)");
        check (bellWidthOctaves (Model::api, 1.0, 12.0) < 0.7 * bellWidthOctaves (Model::api, 1.0, 2.0),
               "API: at 12 dB the bell is at least 30 % narrower than at 2 dB");

        // Pultec: low boost + atten at the same frequency = shelf with a dip above it.
        {
            const auto both = dsp::makeModelPrototype (Model::pultec, dsp::FilterType::lowShelf, dsp::Slope::db12, 0.707, 10.0, 10.0);
            const auto boostOnly = dsp::makeModelPrototype (Model::pultec, dsp::FilterType::lowShelf, dsp::Slope::db12, 0.707, 10.0, 0.0);
            auto dipMin = 100.0, dipAt = 0.0, boostOnlyMin = 100.0;
            for (double hz = 100.0; hz <= 400.0; hz *= 1.02)
            {
                if (protoDb (both, 60.0, hz) < dipMin) { dipMin = protoDb (both, 60.0, hz); dipAt = hz; }
                boostOnlyMin = std::min (boostOnlyMin, protoDb (boostOnly, 60.0, hz));
            }
            const auto low = protoDb (both, 60.0, 30.0);
            note ("Pultec 60 Hz, boost 10 + atten 10: " + format (protoDb (both, 60.0, 20.0), 1) + " dB @ 20 Hz, "
                  + format (low, 1) + " dB @ 30 Hz, dip " + format (dipMin, 1) + " dB @ " + format (dipAt, 0) + " Hz");
            check (low > 3.0 && protoDb (both, 60.0, 40.0) > 3.0, "Pultec boost + atten: low end still boosted (> +3 dB at 30-40 Hz)");
            check (dipMin < -1.0, "Pultec boost + atten: dip below -1 dB between 100 and 400 Hz");
            check (boostOnlyMin > -0.3, "Pultec boost alone: no dip");
            check (std::abs (protoDb (both, 60.0, 5000.0)) < 0.3, "Pultec boost + atten: flat in the mids/highs");
        }

        // Every model and gain type is an exact identity at 0 dB (a neutral band does nothing).
        auto neutral = true;
        for (int m = 1; m < dsp::numModels; ++m)
            for (const auto t : { dsp::FilterType::bell, dsp::FilterType::lowShelf, dsp::FilterType::highShelf, dsp::FilterType::tilt })
            {
                const auto p = dsp::makeModelPrototype ((Model) m, t, dsp::Slope::db12, 0.9, 0.0, 0.0);
                for (double hz = 20.0; hz < 20000.0; hz *= 1.3)
                    neutral = neutral && std::abs (protoDb (p, 700.0, hz)) < 0.01;
            }
        check (neutral, "every model: 0 dB gain is flat (bell, shelves, tilt)");

        // Each model's boost peaks at the band's gain (bell) and cuts are -3 dB-ish at the corner.
        auto bellsHitGain = true, cutsSane = true;
        for (int m = 1; m < dsp::numModels; ++m)
        {
            const auto p = dsp::makeModelPrototype ((Model) m, dsp::FilterType::bell, dsp::Slope::db12, 1.0, 9.0, 0.0);
            bellsHitGain = bellsHitGain && std::abs (protoDb (p, 1000.0, 1000.0) - 9.0) < 0.05;
            const auto hp = dsp::makeModelPrototype ((Model) m, dsp::FilterType::lowCut, dsp::Slope::db12, 0.707, 0.0, 0.0);
            cutsSane = cutsSane && protoDb (hp, 100.0, 10.0) < -30.0 && std::abs (protoDb (hp, 100.0, 2000.0)) < 0.3;
        }
        check (bellsHitGain, "every model's bell reaches the set gain at its centre");
        check (cutsSane, "every model's low cut rejects a decade below and is flat well above");
    }

    void testSteppedPositions()
    {
        DooblrQAudioProcessor processor;
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::type, (float) dsp::FilterType::bell);
        setBand (processor, 2, params::band::frequency, 1000.0f);
        setBand (processor, 2, params::band::gain, 5.2f);
        setBand (processor, 2, params::band::model, (float) Model::neve);
        check (juce::approximatelyEqual (processor.getBandSettings (2).frequency, 1000.0f), "Stepped off: frequency is continuous");
        setBand (processor, 2, params::band::stepped, 1.0f);
        check (juce::approximatelyEqual (processor.getBandSettings (2).frequency, 700.0f), "Neve stepped: 1 kHz snaps to the 700 Hz position");
        setBand (processor, 2, params::band::frequency, 2900.0f);
        check (juce::approximatelyEqual (processor.getBandSettings (2).frequency, 3200.0f), "Neve stepped: 2.9 kHz snaps to 3.2 kHz");
        note ("Neve stepped, gain parameter 5.2 dB -> band gain " + format (processor.getBandSettings (2).gainDb, 4) + " dB");
        check (std::abs (processor.getBandSettings (2).gainDb - 5.2f) < 0.01f, "Neve stepped: gain stays continuous (no gain table)");

        setBand (processor, 2, params::band::model, (float) Model::api);
        check (juce::approximatelyEqual (processor.getBandSettings (2).gainDb, 6.0f), "API stepped: +5.2 dB snaps to +6 dB");
        setBand (processor, 2, params::band::gain, -10.0f);
        check (juce::approximatelyEqual (processor.getBandSettings (2).gainDb, -9.0f) || juce::approximatelyEqual (processor.getBandSettings (2).gainDb, -12.0f),
               "API stepped: -10 dB snaps to a switched position");

        setBand (processor, 2, params::band::model, (float) Model::sslE);
        setBand (processor, 2, params::band::frequency, 1234.0f);
        check (juce::approximatelyEqual (processor.getBandSettings (2).frequency, 1234.0f), "SSL (continuous pots): Stepped has no effect");
        check (! dsp::hasSteps (Model::sslE, dsp::FilterType::bell) && ! dsp::hasSteps (Model::digital, dsp::FilterType::bell),
               "SSL and Digital have no step tables (Stepped is disabled for them)");

        auto tablesSorted = true;
        for (int m = 0; m < dsp::numModels; ++m)
            for (int t = 0; t < dsp::numFilterTypes; ++t)
                for (const auto table : { dsp::frequencySteps ((Model) m, (dsp::FilterType) t), dsp::gainSteps ((Model) m, (dsp::FilterType) t) })
                    for (int i = 1; i < table.size; ++i)
                        tablesSorted = tablesSorted && table.values[i] > table.values[i - 1];
        check (tablesSorted, "every step table is strictly ascending");

        // The editor shows the snapped value and the handle sits at the snapped position.
        setBand (processor, 2, params::band::model, (float) Model::neve);
        setBand (processor, 2, params::band::frequency, 1000.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* frequencyKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::frequency));
        auto* steppedButton = dynamic_cast<juce::Button*> (findById (*editor, params::band::stepped));
        auto* curve = dynamic_cast<dooblrq::ui::ResponseCurveComponent*> (findById (*editor, "responseCurve"));
        check (dooblrEditor && frequencyKnob && steppedButton && curve, "editor has the Stepped toggle");
        if (! (dooblrEditor && frequencyKnob && steppedButton && curve))
            return;
        dooblrEditor->selectBand (2);
        check (frequencyKnob->getTextFromValue (1000.0) == "700 Hz", "Frequency knob shows the snapped position (700 Hz)");
        check (steppedButton->isEnabled() && steppedButton->getToggleState(), "Stepped toggle is on and enabled for Neve");
        const auto handleX = curve->handlePosition (2).x;
        check (std::abs (handleX - curve->frequencyToX (700.0)) < 0.5f, "the band's handle sits at the snapped 700 Hz");

        setBand (processor, 2, params::band::model, (float) Model::sslG);
        dooblrEditor->updateUndoButtons(); // any timer-equivalent refresh below
        dooblrEditor->selectBand (2);
        check (! steppedButton->isEnabled(), "Stepped is disabled for SSL G (continuous controls)");
    }

    /** Total harmonic distortion + noise of a steady sine (least-squares fit of the fundamental). */
    double thdOf (const float* x, int length, double frequency, double sampleRate)
    {
        double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0, sum = 0;
        for (int i = 0; i < length; ++i)
        {
            const auto w = 2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate;
            const auto s = std::sin (w), c = std::cos (w);
            ss += s * s; cc += c * c; sc += s * c; xs += x[i] * s; xc += x[i] * c; sum += x[i];
        }
        const auto det = ss * cc - sc * sc;
        const auto a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det, mean = sum / length;
        double residual = 0, fundamental = 0;
        for (int i = 0; i < length; ++i)
        {
            const auto w = 2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate;
            const auto f = a * std::sin (w) + b * std::cos (w);
            residual += juce::square (x[i] - mean - f);
            fundamental += f * f;
        }
        return std::sqrt (residual / std::max (1.0e-30, fundamental));
    }

    double modelThd (Model model, PhaseMode mode, float levelDb, float driveDb, double frequency = 100.0)
    {
        constexpr double sampleRate = 48000.0;
        DooblrQAudioProcessor processor;
        setParameter (processor, params::phaseMode, (float) mode);
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::type, (float) dsp::FilterType::bell);
        setBand (processor, 2, params::band::frequency, 1000.0f);
        setBand (processor, 2, params::band::gain, 0.0f); // flat: only the colour stage acts
        setBand (processor, 2, params::band::model, (float) model);
        setBand (processor, 2, params::band::drive, driveDb);
        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

        const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
        const int numSamples = 48000;
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
        processInBlocks (processor, buffer, { 512 });
        return thdOf (buffer.getReadPointer (0, numSamples / 2), numSamples / 2, frequency, sampleRate);
    }

    void testModelColour()
    {
        std::string table = "THD+N of a 100 Hz sine through a flat band (Zero Latency), at -40 / -20 / -6 / 0 dBFS:";
        auto risesWithLevel = true, cleanWhenQuiet = true, subtle = true;
        for (int m = 1; m < dsp::numModels; ++m)
        {
            table += std::string ("\n      ") + dsp::modelName ((Model) m) + ":";
            auto previous = 0.0;
            for (const auto level : { -40.0f, -20.0f, -6.0f, 0.0f })
            {
                const auto thd = modelThd ((Model) m, PhaseMode::zeroLatency, level, 0.0f);
                table += " " + format (100.0 * thd, 4) + " %";
                risesWithLevel = risesWithLevel && thd > previous;
                previous = thd;
                if (juce::exactlyEqual (level, -40.0f)) cleanWhenQuiet = cleanWhenQuiet && thd < 5.0e-4;
                if (juce::exactlyEqual (level, -6.0f))  subtle = subtle && thd < 0.03;
            }
        }
        note (table);
        check (risesWithLevel, "every analogue model: distortion rises with level");
        check (cleanWhenQuiet, "every analogue model: < 0.05 % THD+N (below -66 dB) at -40 dBFS (near clean when quiet)");
        check (subtle, "every analogue model: < 3 % THD+N at -6 dBFS (subtle at nominal level)");

        const auto digital = modelThd (Model::digital, PhaseMode::zeroLatency, 0.0f, 24.0f);
        note ("Digital THD+N at 0 dBFS with Drive +24: " + format (100.0 * digital, 6) + " %");
        check (digital < 1.0e-5, "Digital stays clean (Drive does nothing on Digital)");

        const auto neve0 = modelThd (Model::neve, PhaseMode::zeroLatency, -20.0f, 0.0f);
        const auto neve12 = modelThd (Model::neve, PhaseMode::zeroLatency, -20.0f, 12.0f);
        const auto neveMinus = modelThd (Model::neve, PhaseMode::zeroLatency, -20.0f, -12.0f);
        note ("Neve at -20 dBFS, Drive -12 / 0 / +12 dB: " + format (100.0 * neveMinus, 4) + " / " + format (100.0 * neve0, 4)
              + " / " + format (100.0 * neve12, 4) + " %");
        check (neve12 > 4.0 * neve0 && neveMinus < neve0, "Drive works: more drive = more colour, less drive = cleaner");

        // The colour is applied the same way in the FIR modes (after the linear-phase EQ).
        std::string modes = "Neve THD+N at -6 dBFS per mode:";
        auto consistent = true;
        const auto reference = modelThd (Model::neve, PhaseMode::zeroLatency, -6.0f, 0.0f);
        for (const auto mode : allPhaseModes)
        {
            const auto thd = modelThd (Model::neve, mode, -6.0f, 0.0f);
            modes += std::string (" ") + phaseName (mode) + " " + format (100.0 * thd, 4) + " %";
            consistent = consistent && std::abs (thd / reference - 1.0) < 0.1;
        }
        note (modes);
        check (consistent, "the colour stage gives the same distortion in all three phase modes (within 10 %)");

        // Even harmonics: Pultec (tube) has a 2nd harmonic, SSL E is nearly odd-only.
        const auto secondHarmonic = [] (Model model)
        {
            constexpr double sampleRate = 48000.0;
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::gain, 0.0f);
            setBand (processor, 2, params::band::model, (float) model);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            juce::AudioBuffer<float> buffer (2, 48000);
            for (int i = 0; i < 48000; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, 0.7f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sampleRate));
            processInBlocks (processor, buffer, { 512 });
            const auto amplitudeAt = [&] (double hz)
            {
                double s = 0, c = 0;
                for (int i = 24000; i < 48000; ++i)
                {
                    const auto w = 2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate;
                    s += buffer.getSample (0, i) * std::sin (w);
                    c += buffer.getSample (0, i) * std::cos (w);
                }
                return std::sqrt (s * s + c * c) * 2.0 / 24000.0;
            };
            return juce::Decibels::gainToDecibels (amplitudeAt (2000.0) / amplitudeAt (1000.0), -200.0);
        };
        const auto pultecH2 = secondHarmonic (Model::pultec), sslH2 = secondHarmonic (Model::sslE);
        note ("2nd harmonic at -3 dBFS: Pultec " + format (pultecH2, 1) + " dBc, SSL E " + format (sslH2, 1) + " dBc");
        check (pultecH2 > sslH2 + 10.0, "Pultec (tube make-up) has clearly more 2nd harmonic than SSL E");
    }

    void testModelExtremesAndModes()
    {
        constexpr double sampleRate = 48000.0;
        auto finite = true, bounded = true;
        for (int m = 0; m < dsp::numModels; ++m)
            for (const auto mode : allPhaseModes)
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                for (int b = 0; b < params::numBands; ++b)
                {
                    setBand (processor, b, params::band::enabled, 1.0f);
                    setBand (processor, b, params::band::type, (float) (b % dsp::numFilterTypes));
                    setBand (processor, b, params::band::frequency, b % 2 == 0 ? 20.0f : 20000.0f);
                    setBand (processor, b, params::band::gain, b % 3 == 0 ? -24.0f : 24.0f);
                    setBand (processor, b, params::band::q, b % 2 == 0 ? params::maxQ : params::minQ);
                    setBand (processor, b, params::band::model, (float) m);
                    setBand (processor, b, params::band::drive, params::maxDriveDb);
                    setBand (processor, b, params::band::atten, params::maxAttenDb);
                    setBand (processor, b, params::band::stepped, b % 2 == 0 ? 1.0f : 0.0f);
                    setBand (processor, b, params::band::dynamicsEnabled, 1.0f);
                    setBand (processor, b, params::band::range, 24.0f);
                    setBand (processor, b, params::band::threshold, -60.0f);
                }
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                juce::AudioBuffer<float> buffer (2, 24000);
                fillWithNoise (buffer, 77 + m);
                for (int i = 0; i < 24000; i += 997)
                    buffer.setSample (0, i, 1.0f); // impulses
                processInBlocks (processor, buffer, { 512, 1, 77 });
                finite = finite && allFinite (buffer);
                bounded = bounded && buffer.getMagnitude (0, buffer.getNumSamples()) < 1.0e4f;
            }
        check (finite, "every model x every phase mode: output finite at extreme settings (+-24 dB, Q extremes, Drive +24, Atten 18)");
        check (bounded, "every model x every phase mode: output bounded at extreme settings");

        // Latency: the models add none.
        auto latencySame = true;
        for (const auto mode : allPhaseModes)
        {
            DooblrQAudioProcessor digital, neve;
            for (auto* p : { &digital, &neve })
            {
                setParameter (*p, params::phaseMode, (float) mode);
                setBand (*p, 2, params::band::enabled, 1.0f);
            }
            setBand (neve, 2, params::band::model, (float) Model::neve);
            prepare (digital, juce::AudioChannelSet::stereo(), sampleRate, 512);
            prepare (neve, juce::AudioChannelSet::stereo(), sampleRate, 512);
            latencySame = latencySame && digital.getLatencySamples() == neve.getLatencySamples();
        }
        check (latencySame, "models do not change the reported latency in any phase mode");
        {
            dsp::ColourStage stage;
            stage.prepare (sampleRate);
            note ("colour stage: dry path 0 samples; distortion residual delayed by the oversampling filters ("
                  + format (stage.getResidualLatency(), 2) + " samples @ 48 kHz)");
        }

        // Natural / Linear: the FIR carries the model's linear magnitude (Pultec boost + atten, quiet signal).
        std::string line = "Pultec 60 Hz boost 10 + atten 10, quiet sine, measured vs model (dB):";
        auto matches = true;
        const auto proto = dsp::makeModelPrototype (Model::pultec, dsp::FilterType::lowShelf, dsp::Slope::db12, 0.707, 10.0, 10.0);
        for (const auto mode : allPhaseModes)
            for (const auto hz : { 30.0, 60.0, 150.0, 1000.0 })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                setBand (processor, 1, params::band::enabled, 1.0f);
                setBand (processor, 1, params::band::type, (float) dsp::FilterType::lowShelf);
                setBand (processor, 1, params::band::frequency, 60.0f);
                setBand (processor, 1, params::band::gain, 10.0f);
                setBand (processor, 1, params::band::atten, 10.0f);
                setBand (processor, 1, params::band::model, (float) Model::pultec);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                const auto measured = measureSineGainDb (processor, hz, 0.01f, sampleRate, 1.5);
                const auto expected = protoDb (proto, 60.0, hz);
                line += std::string (" ") + phaseName (mode) + "@" + format (hz, 0) + " " + format (measured, 2) + "/" + format (expected, 2);
                matches = matches && std::abs (measured - expected) < (mode == PhaseMode::zeroLatency ? 0.35 : 0.2);
            }
        note (line);
        check (matches, "every phase mode reproduces the model's magnitude (Pultec shelf + dip)");

        // Dynamics work with every model.
        auto dynamicsWork = true;
        for (int m = 0; m < dsp::numModels; ++m)
        {
            DooblrQAudioProcessor processor;
            setBand (processor, 2, params::band::enabled, 1.0f);
            setBand (processor, 2, params::band::type, (float) dsp::FilterType::bell);
            setBand (processor, 2, params::band::frequency, 1000.0f);
            setBand (processor, 2, params::band::model, (float) m);
            setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
            setBand (processor, 2, params::band::range, -9.0f);
            setBand (processor, 2, params::band::threshold, -40.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto gain = measureSineGainDb (processor, 1000.0, 0.3f, sampleRate, 1.0);
            dynamicsWork = dynamicsWork && gain < -4.0 && processor.getBandDynamicGainDb (2) < -4.0f;
        }
        check (dynamicsWork, "dynamics (downward, -9 dB range) work on every model");
    }

    void testModelStateUndoAndSwitch()
    {
        // State round trip.
        {
            DooblrQAudioProcessor a;
            for (int b = 0; b < params::numBands; ++b)
            {
                setBand (a, b, params::band::model, (float) ((b * 3 + 1) % dsp::numModels));
                setBand (a, b, params::band::stepped, (float) (b % 2));
                setBand (a, b, params::band::drive, -6.0f + 4.0f * (float) b);
                setBand (a, b, params::band::atten, 2.5f * (float) b);
            }
            juce::MemoryBlock state;
            a.getStateInformation (state);
            DooblrQAudioProcessor c;
            c.setStateInformation (state.getData(), (int) state.getSize());
            check (sameValues (allParameterValues (a), allParameterValues (c)), "model, Stepped, Drive and Atten round-trip through the saved state");
        }

        // Undo.
        {
            DooblrQAudioProcessor processor;
            auto& history = processor.getUndoHistory();
            history.clear();
            const auto id = params::bandId (3, params::band::model);
            uiGesture (processor, id, (float) Model::pultec);
            check (history.canUndo() && juce::roundToInt (getParameter (processor, id)) == (int) Model::pultec, "a model change is recorded");
            history.undo();
            check (juce::roundToInt (getParameter (processor, id)) == 0, "undo restores Digital");
            history.redo();
            check (juce::roundToInt (getParameter (processor, id)) == (int) Model::pultec, "redo re-applies the model");
            uiGesture (processor, params::bandId (3, params::band::stepped), 1.0f);
            history.undo();
            check (getParameter (processor, params::bandId (3, params::band::stepped)) < 0.5f, "undo works for Stepped");
        }

        // Click-free switching: a steady 1 kHz sine through a +6 dB bell; switching model mid-stream
        // must not produce a step larger than the signal's own sample-to-sample movement.
        constexpr double sampleRate = 48000.0;
        std::string line = "model switch, max |y[n]-y[n-1]| during switch / steady:";
        auto clickFree = true;
        for (const auto mode : allPhaseModes)
            for (const auto [from, to] : { std::pair { Model::digital, Model::neve }, std::pair { Model::sslE, Model::pultec },
                                           std::pair { Model::api, Model::digital } })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                setBand (processor, 2, params::band::enabled, 1.0f);
                setBand (processor, 2, params::band::frequency, 1000.0f);
                setBand (processor, 2, params::band::gain, 6.0f);
                setBand (processor, 2, params::band::model, (float) from);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                const int numSamples = 48000;
                juce::AudioBuffer<float> buffer (2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sampleRate));
                juce::AudioBuffer<float> first (buffer.getArrayOfWritePointers(), 2, 0, 24000);
                juce::AudioBuffer<float> second (buffer.getArrayOfWritePointers(), 2, 24000, 24000);
                processInBlocks (processor, first, { 512 });
                setBand (processor, 2, params::band::model, (float) to);
                processInBlocks (processor, second, { 512 });
                const auto* y = buffer.getReadPointer (0);
                auto steady = 0.0f, during = 0.0f;
                for (int i = 12001; i < 24000; ++i) steady = std::max (steady, std::abs (y[i] - y[i - 1]));
                for (int i = 24001; i < 24000 + 9600; ++i) during = std::max (during, std::abs (y[i] - y[i - 1]));
                line += " " + format (during / steady, 3);
                clickFree = clickFree && during < 1.1f * steady;
            }
        note (line);
        check (clickFree, "model switches are click-free in every phase mode");
    }


    void testModelUi()
    {
        DooblrQAudioProcessor processor;
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* modelBox = dynamic_cast<juce::ComboBox*> (findById (*editor, params::band::model));
        auto* slopeBox = dynamic_cast<juce::ComboBox*> (findById (*editor, params::band::slope));
        auto* drive = dynamic_cast<juce::Slider*> (findById (*editor, params::band::drive));
        auto* atten = dynamic_cast<juce::Slider*> (findById (*editor, params::band::atten));
        auto* curve = dynamic_cast<dooblrq::ui::ResponseCurveComponent*> (findById (*editor, "responseCurve"));
        check (dooblrEditor && modelBox && slopeBox && drive && atten && curve, "editor has Model, Drive and Atten controls");
        if (! (dooblrEditor && modelBox && slopeBox && drive && atten && curve))
            return;

        dooblrEditor->selectBand (1); // band 2: Low Shelf by default
        setBand (processor, 1, params::band::enabled, 1.0f);
        setBand (processor, 1, params::band::type, (float) dsp::FilterType::lowShelf);
        dooblrEditor->selectBand (1);
        check (modelBox->getNumItems() == dsp::numModels && modelBox->getSelectedId() == 1, "Model box lists all 10 models, Digital selected");
        check (slopeBox->isEnabled() && ! drive->isEnabled() && ! atten->isEnabled(), "Digital: Slope enabled, Drive and Atten disabled");
        check (atten->getTooltip().contains ("Pultec"), "disabled Atten explains it is Pultec-only");

        modelBox->setSelectedId ((int) Model::pultec + 1, juce::sendNotificationSync);
        check (juce::roundToInt (getParameter (processor, params::bandId (1, params::band::model))) == (int) Model::pultec, "choosing Pultec in the box sets the parameter");
        dooblrEditor->selectBand (1);
        check (! slopeBox->isEnabled() && slopeBox->getTooltip().contains ("model"), "analogue model: Slope disabled, tooltip says why");
        check (drive->isEnabled() && atten->isEnabled(), "Pultec low shelf: Drive and Atten enabled");

        setBand (processor, 1, params::band::type, (float) dsp::FilterType::bell);
        dooblrEditor->selectBand (1);
        check (! atten->isEnabled(), "Pultec mid band: Atten disabled (Atten is a shelf control)");
        setBand (processor, 1, params::band::model, (float) Model::neve);
        dooblrEditor->selectBand (1);
        check (drive->isEnabled() && ! atten->isEnabled(), "Neve: Drive enabled, Atten disabled");

        // Right-click menu: models ticked, choosing one is a single undo step.
        auto& history = processor.getUndoHistory();
        history.clear();
        const auto menu = curve->createSlopeMenu (1);
        int modelItems = 0, tickedModel = -1, slopeItemsEnabled = 0;
        bool hasStepped = false;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.itemID >= dooblrq::ui::ResponseCurveComponent::modelMenuBase && item.itemID < dooblrq::ui::ResponseCurveComponent::modelMenuBase + dsp::numModels)
            {
                ++modelItems;
                if (item.isTicked) tickedModel = item.itemID - dooblrq::ui::ResponseCurveComponent::modelMenuBase;
            }
            if (item.itemID == dooblrq::ui::ResponseCurveComponent::steppedMenuId) hasStepped = item.isEnabled;
            if (item.itemID > 0 && item.itemID <= dsp::numSlopes && item.isEnabled) ++slopeItemsEnabled;
        }
        check (modelItems == dsp::numModels && tickedModel == (int) Model::neve, "right-click menu lists every model, current one ticked");
        check (hasStepped && slopeItemsEnabled == 0, "right-click menu: Stepped available for Neve, slopes greyed out");
        curve->handleBandMenuResult (1, dooblrq::ui::ResponseCurveComponent::modelMenuBase + (int) Model::api);
        check (juce::roundToInt (getParameter (processor, params::bandId (1, params::band::model))) == (int) Model::api
                   && history.getNumUndoableSteps() == 1, "choosing a model from the menu sets it as one undo step");
        curve->handleBandMenuResult (1, dooblrq::ui::ResponseCurveComponent::steppedMenuId);
        check (getParameter (processor, params::bandId (1, params::band::stepped)) > 0.5f, "menu Stepped toggles the parameter");
        history.undo();
        history.undo();
        check (juce::roundToInt (getParameter (processor, params::bandId (1, params::band::model))) == (int) Model::neve, "two undos restore Neve");

        // The drawn curve follows the model: API's proportional Q makes a +4 dB bell wider than
        // Digital's at the same Width (they meet at about +12 dB).
        setBand (processor, 1, params::band::gain, 4.0f);
        setBand (processor, 1, params::band::frequency, 1000.0f);
        setBand (processor, 1, params::band::stepped, 0.0f);
        setBand (processor, 1, params::band::model, (float) Model::api);
        const auto apiAt2k = curve->responseDecibelsAt (2000.0, false);
        setBand (processor, 1, params::band::model, (float) Model::digital);
        const auto digitalAt2k = curve->responseDecibelsAt (2000.0, false);
        note ("drawn +4 dB bell at 2 kHz (1 oct up): API " + format (apiAt2k, 2) + " dB, Digital " + format (digitalAt2k, 2) + " dB");
        check (apiAt2k > digitalAt2k + 0.3, "the drawn curve shows the model's response (API wider at +4 dB)");

        // Every EQ knob readout fits its text box (squeezed to at most 80 % width) at the default and minimum sizes.
        {
            const auto fits = [&] (const char* id, std::initializer_list<double> values, std::string& worst)
            {
                auto* slider = dynamic_cast<juce::Slider*> (findById (*editor, id));
                juce::Label* box = nullptr;
                for (auto* child : slider->getChildren())
                    if (auto* label = dynamic_cast<juce::Label*> (child)) box = label;
                if (box == nullptr) return false;
                auto ok = true;
                for (const auto v : values)
                {
                    const auto text = slider->getTextFromValue (v);
                    const auto needed = juce::GlyphArrangement::getStringWidth (box->getFont(), text)
                                        + (float) box->getBorderSize().getLeftAndRight();
                    if (needed * box->getMinimumHorizontalScale() > (float) box->getWidth())
                    {
                        ok = false;
                        worst += " " + text.toStdString() + " (" + format (needed, 0) + " > " + std::to_string (box->getWidth()) + " px)";
                    }
                }
                return ok;
            };
            setBand (processor, 1, params::band::model, (float) Model::digital);
            dooblrEditor->selectBand (1);
            for (const auto [w, h] : { std::pair { DooblrQAudioProcessorEditor::defaultWidth, DooblrQAudioProcessorEditor::defaultHeight },
                                       std::pair { DooblrQAudioProcessorEditor::minimumWidth, DooblrQAudioProcessorEditor::minimumHeight } })
            {
                editor->setSize (w, h);
                std::string worst;
                const auto ok = fits (params::band::q, { params::minQ, 0.73, 1.0, 9.99, params::maxQ }, worst)
                                && fits (params::band::frequency, { 20.0, 1234.5, 19999.0 }, worst)
                                && fits (params::band::gain, { -23.9, 23.9 }, worst)
                                && fits (params::band::drive, { -12.0, 24.0 }, worst)
                                && fits (params::band::atten, { 0.0, 17.9 }, worst);
                check (ok, "knob readouts fit their text boxes at " + std::to_string (w) + " x " + std::to_string (h) + worst);
            }
        }

        // Snapshot: Trident stepped low cut, Pultec low shelf boost + atten (shelf with dip), API bell,
        // SSL G bell cut, Helios high shelf, Digital high cut; Pultec band selected.
        {
            struct Setup { dsp::FilterType type; Model model; float freq, gain, q, drive, atten; bool stepped; };
            const Setup setups[] {
                { dsp::FilterType::lowCut,    Model::trident, 25.0f,    0.0f, 0.707f, 0.0f, 0.0f, true },
                { dsp::FilterType::lowShelf,  Model::pultec,  100.0f,  10.0f, 0.707f, 6.0f, 10.0f, true },
                { dsp::FilterType::bell,      Model::api,     600.0f,   6.0f, 1.0f,   0.0f, 0.0f, true },
                { dsp::FilterType::bell,      Model::sslG,    2500.0f, -8.0f, 1.5f,   0.0f, 0.0f, false },
                { dsp::FilterType::highShelf, Model::helios,  10000.0f, 4.0f, 0.707f, 3.0f, 0.0f, true },
                { dsp::FilterType::highCut,   Model::digital, 18000.0f, 0.0f, 0.707f, 0.0f, 0.0f, false },
            };
            for (int b = 0; b < params::numBands; ++b)
            {
                const auto& c = setups[b];
                setBand (processor, b, params::band::enabled, 1.0f);
                setBand (processor, b, params::band::type, (float) c.type);
                setBand (processor, b, params::band::model, (float) c.model);
                setBand (processor, b, params::band::frequency, c.freq);
                setBand (processor, b, params::band::gain, c.gain);
                setBand (processor, b, params::band::q, c.q);
                setBand (processor, b, params::band::drive, c.drive);
                setBand (processor, b, params::band::atten, c.atten);
                setBand (processor, b, params::band::stepped, c.stepped ? 1.0f : 0.0f);
                setBand (processor, b, params::band::dynamicsEnabled, 0.0f);
            }
            dooblrEditor->selectBand (1);
            editor->setSize (1080, 660);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.8.0");
        }
    }


    //==============================================================================
    // 0.9.0: dynamic Low / High Cut and the outer Range ring
    void configureDynamicCut (DooblrQAudioProcessor& processor, int b, dsp::FilterType type, float frequency,
                              dsp::Slope slope, float rangeDb, bool dynamics = true)
    {
        setBand (processor, b, params::band::enabled, 1.0f);
        setBand (processor, b, params::band::type, (float) type);
        setBand (processor, b, params::band::frequency, frequency);
        setBand (processor, b, params::band::q, 0.707f);
        setBand (processor, b, params::band::slope, (float) slope);
        setBand (processor, b, params::band::dynamicsEnabled, dynamics ? 1.0f : 0.0f);
        setBand (processor, b, params::band::range, rangeDb);
        setBand (processor, b, params::band::threshold, -40.0f);
        setBand (processor, b, params::band::attack, 1.0f);
        setBand (processor, b, params::band::release, 100.0f);
    }

    void testDynamicCuts()
    {
        using Band = dsp::DynamicEQBand;
        using T = dsp::FilterType;
        constexpr double sampleRate = 48000.0;

        // The mapping: 1 dB = 1/6 octave, positive = into the passband, clamped to 20 Hz - 20 kHz.
        check (std::abs (Band::dynamicCutFrequency (T::lowCut, 1000.0, 6.0) - 2000.0) < 1.0e-6, "Low Cut +6 dB moves the cutoff up 1 octave");
        check (std::abs (Band::dynamicCutFrequency (T::lowCut, 1000.0, -6.0) - 500.0) < 1.0e-6, "Low Cut -6 dB moves it down 1 octave");
        check (std::abs (Band::dynamicCutFrequency (T::highCut, 1000.0, 6.0) - 500.0) < 1.0e-6, "High Cut +6 dB moves the cutoff down 1 octave");
        check (std::abs (Band::dynamicCutFrequency (T::highCut, 1000.0, -12.0) - 4000.0) < 1.0e-6, "High Cut -12 dB moves it up 2 octaves");
        check (Band::dynamicCutFrequency (T::lowCut, 15000.0, 24.0) == 20000.0
                   && Band::dynamicCutFrequency (T::highCut, 30.0, 24.0) == 20.0, "the moved cutoff is clamped to 20 Hz - 20 kHz");
        check (Band::dynamicCutFrequency (T::bell, 1000.0, 6.0) == 1000.0, "non-cut types keep their frequency");
        check (Band::dynamicCutFrequency (T::lowCut, 1000.0, std::numeric_limits<double>::quiet_NaN()) == 1000.0,
               "a non-finite amount leaves the cutoff alone");
        check (dsp::supportsDynamics (T::lowCut) && dsp::supportsDynamics (T::highCut) && ! dsp::supportsDynamics (T::notch),
               "Low / High Cut support dynamics (Notch still does not)");

        // Direction in the audio, per mode and slope: a loud tone at the cutoff. Positive Range
        // closes the filter on it (more attenuation), negative Range opens it (less).
        const dsp::Slope slopes[] { dsp::Slope::db12, dsp::Slope::db24, dsp::Slope::brickwall };
        std::string table = "gain at the 1 kHz cutoff with a loud 1 kHz tone (static / +12 / -12 dB Range):";

        for (const auto mode : allPhaseModes)
            for (const auto type : { T::lowCut, T::highCut })
                for (const auto slope : slopes)
                {
                    double gains[3] {};
                    double liveHz[3] {};
                    const float ranges[] { 0.0f, 12.0f, -12.0f };

                    for (int r = 0; r < 3; ++r)
                    {
                        DooblrQAudioProcessor processor;
                        setParameter (processor, params::phaseMode, (float) mode);
                        configureDynamicCut (processor, 0, type, 1000.0f, slope, ranges[r], r != 0);
                        prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
                        gains[r] = measureSineGainDb (processor, 1000.0, 0.5f, sampleRate, 1.0);

                        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
                        if (auto* curve = dynamic_cast<dooblrq::ui::ResponseCurveComponent*> (findById (*editor, "responseCurve")))
                        {
                            const auto marker = curve->cutMarker (0);
                            liveHz[r] = marker.visible ? marker.liveHz : 1000.0;
                            if (r != 0)
                                check (marker.visible && std::abs (marker.limitHz - Band::dynamicCutFrequency (type, 1000.0, ranges[r])) < 1.0e-3,
                                       "display marks the full-Range limit");
                        }
                        processor.editorBeingDeleted (editor.get());
                    }

                    const auto label = std::string (phaseName (mode)) + ", " + (type == T::lowCut ? "Low Cut" : "High Cut")
                                     + ", slope " + std::to_string ((int) slope);
                    table += "\n      " + label + ": " + format (gains[0]) + " / " + format (gains[1]) + " / " + format (gains[2]) + " dB";
                    check (std::isfinite (gains[1]) && gains[1] < gains[0] - 1.5, label + ": +Range closes the filter on a loud signal");
                    check (std::isfinite (gains[2]) && gains[2] > gains[0] + 1.0, label + ": -Range opens the filter on a loud signal");

                    // Display: HP rises / LP falls for +Range, the reverse for -Range.
                    const auto intoPassband = type == T::lowCut ? liveHz[1] > 1050.0 : liveHz[1] < 950.0;
                    const auto opening = type == T::lowCut ? liveHz[2] < 950.0 : liveHz[2] > 1050.0;
                    check (intoPassband && opening, label + ": live cutoff marker moves the right way for both signs");
                }

        note (table);

        // A quiet signal leaves a dynamic cut at its static setting.
        {
            DooblrQAudioProcessor processor;
            configureDynamicCut (processor, 0, T::lowCut, 1000.0f, dsp::Slope::db24, 12.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);
            const auto quiet = measureSineGainDb (processor, 1000.0, 0.001f, sampleRate, 0.5);
            check (std::abs (quiet + 3.01) < 0.5, "below threshold the cut stays at its static -3 dB corner (" + format (quiet) + " dB)");
        }

        // No zipper / clicks: fastest attack, a 1 kHz tone with a fast level swing moving a steep
        // Low Cut back and forth. The output's sample-to-sample curvature must stay in proportion
        // to the input's (a stepped or clicking filter would spike it).
        for (const auto mode : allPhaseModes)
            for (const auto slope : { dsp::Slope::db24, dsp::Slope::brickwall })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureDynamicCut (processor, 0, T::lowCut, 400.0f, slope, 12.0f);
                setBand (processor, 0, params::band::attack, params::minAttackMs);
                setBand (processor, 0, params::band::release, params::minReleaseMs);
                setBand (processor, 0, params::band::threshold, -30.0f);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

                const int numSamples = (int) sampleRate * 2;
                juce::AudioBuffer<float> buffer (2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto t = i / sampleRate;
                    const auto envelope = 0.02 + 0.7 * (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 6.0 * t));
                    const auto value = (float) (envelope * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
                    buffer.setSample (0, i, value);
                    buffer.setSample (1, i, value);
                }

                const auto curvature = [] (const juce::AudioBuffer<float>& b, int from)
                {
                    double maximum = 0.0;
                    for (int i = from + 1; i < b.getNumSamples() - 1; ++i)
                        maximum = std::max (maximum, (double) std::abs (b.getSample (0, i + 1) - 2.0f * b.getSample (0, i) + b.getSample (0, i - 1)));
                    return maximum;
                };

                const auto inputCurvature = curvature (buffer, 0);
                processInBlocks (processor, buffer, { 512, 64, 333 });
                const auto outputCurvature = curvature (buffer, numSamples / 4);
                check (allFinite (buffer) && outputCurvature < 1.6 * inputCurvature,
                       std::string (phaseName (mode)) + ", slope " + std::to_string ((int) slope)
                           + ": moving cutoff is smooth (curvature " + format (outputCurvature, 4) + " vs input " + format (inputCurvature, 4) + ")");
            }

        // No NaNs: extreme automation of the range, frequency and slope on both cut types, every
        // mode and model, with loud noise.
        for (const auto mode : allPhaseModes)
            for (const auto model : { dsp::Model::digital, dsp::Model::neve, dsp::Model::helios })
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureDynamicCut (processor, 0, T::lowCut, 25.0f, dsp::Slope::brickwall, 24.0f);
                configureDynamicCut (processor, 5, T::highCut, 19000.0f, dsp::Slope::brickwall, -24.0f);
                setBand (processor, 0, params::band::model, (float) model);
                setBand (processor, 5, params::band::model, (float) model);
                setBand (processor, 0, params::band::attack, params::minAttackMs);
                prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 256);
                juce::AudioBuffer<float> buffer (2, 256);
                juce::MidiBuffer midi;
                auto finite = true;

                for (int block = 0; block < 300; ++block)
                {
                    fillWithNoise (buffer, block, block % 7 == 0 ? 4.0f : 0.8f);
                    if (block % 10 == 0)
                    {
                        setBand (processor, 0, params::band::range, (block / 10) % 2 == 0 ? 24.0f : -24.0f);
                        setBand (processor, 5, params::band::range, (block / 10) % 2 == 0 ? -24.0f : 24.0f);
                        setBand (processor, 0, params::band::frequency, (block / 10) % 3 == 0 ? 20.0f : 18000.0f);
                        setBand (processor, 0, params::band::slope, (float) ((block / 10) % dsp::numSlopes));
                    }
                    processor.processBlock (buffer, midi);
                    finite = finite && allFinite (buffer);
                }

                check (finite, std::string (phaseName (mode)) + ", " + dsp::modelName (model) + ": dynamic cuts stay finite under extreme automation");
            }

        // CPU: all six bands dynamic Brickwall cuts (every cascade redesigned as the cutoff moves).
        std::string cpu = "CPU, 6 dynamic Brickwall cuts, stereo @ 48 kHz, 512-sample blocks:";
        for (const auto mode : allPhaseModes)
        {
            constexpr int seconds = 10;
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            const float freqs[] { 40.0f, 120.0f, 400.0f, 2000.0f, 8000.0f, 15000.0f };
            for (int b = 0; b < params::numBands; ++b)
                configureDynamicCut (processor, b, b < 3 ? T::lowCut : T::highCut, freqs[b], dsp::Slope::brickwall, b % 2 == 0 ? 9.0f : -9.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sampleRate, 512);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (17);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int block = 0; block < seconds * 48000 / 512; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, (random.nextFloat() - 0.5f) * ((block / 40) % 2 == 0 ? 1.0f : 0.05f));
                processor.processBlock (buffer, midi);
            }
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            cpu += std::string (" ") + phaseName (mode) + " " + format (100.0 * elapsed / (seconds * 1000.0), 2) + " %";
            check (elapsed < 0.5 * seconds * 1000.0 && allFinite (buffer),
                   std::string (phaseName (mode)) + ": 6 dynamic Brickwall cuts use under half a core, finite output");
        }
        note (cpu + " (of one core)");
    }

    juce::MouseEvent makeMouseEventWithMods (juce::Component& component, juce::Point<float> position,
                                             juce::Point<float> downPosition, int clicks, bool dragged,
                                             juce::ModifierKeys mods)
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, clicks, dragged };
    }

    /** Drags through the given points (each one a mouseDrag) with the given modifiers. */
    void simulatePathDrag (juce::Component& component, const std::vector<juce::Point<float>>& points, bool shift = false)
    {
        const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier
                                              | (shift ? juce::ModifierKeys::shiftModifier : 0));
        component.mouseDown (makeMouseEventWithMods (component, points.front(), points.front(), 1, false, mods));
        for (size_t i = 1; i < points.size(); ++i)
            component.mouseDrag (makeMouseEventWithMods (component, points[i], points.front(), 1, true, mods));
        component.mouseUp (makeMouseEventWithMods (component, points.back(), points.front(), 1, true, mods));
    }

    void testRangeRing()
    {
        using Ring = dooblrq::ui::RangeRing;
        using T = dsp::FilterType;
        constexpr auto pi = juce::MathConstants<double>::pi;

        // Drag math.
        check (std::abs (Ring::verticalDeltaDb (-10.0, false) - 2.0) < 1.0e-9, "vertical: 10 px up = +2 dB (240 px = 48 dB)");
        check (std::abs (Ring::verticalDeltaDb (10.0, true) + 0.2) < 1.0e-9, "vertical fine (Shift): 10 px down = -0.2 dB");
        const juce::Point<float> o;
        check (std::abs (Ring::clockwiseAngle (o, { 0.0f, -10.0f }, { 10.0f, 0.0f }) - pi / 2.0) < 1.0e-6, "angle: top to right = +90 degrees (clockwise)");
        check (std::abs (Ring::clockwiseAngle (o, { 10.0f, 0.0f }, { 0.0f, -10.0f }) + pi / 2.0) < 1.0e-6, "angle: right to top = -90 degrees");
        check (std::abs (Ring::clockwiseAngle (o, { -1.0f, 10.0f }, { 1.0f, 10.0f }) + 0.2) < 0.01, "angle unwraps across 6 o'clock");
        check (std::abs (Ring::rotaryDeltaDb (pi * 0.8, pi * 1.6, false) - 24.0) < 1.0e-9, "rotary: half the knob's sweep = 24 dB");
        check (std::abs (Ring::rotaryDeltaDb (pi * 0.8, pi * 1.6, true) - 2.4) < 1.0e-9, "rotary fine (Shift) = x0.1");
        check (Ring::chooseDragMode (o, { 0.0f, -30.0f }, { 1.0f, -31.0f }) == Ring::DragMode::undecided, "a 1.4 px move is undecided");
        check (Ring::chooseDragMode (o, { 0.0f, -30.0f }, { 0.0f, -36.0f }) == Ring::DragMode::vertical, "up at the top = vertical");
        check (Ring::chooseDragMode (o, { 0.0f, -30.0f }, { 6.0f, 0.5f - 30.0f }) == Ring::DragMode::rotary, "sideways at the top = rotation");
        check (Ring::chooseDragMode (o, { 30.0f, 0.0f }, { 30.0f, -6.0f }) == Ring::DragMode::vertical, "up at the right = vertical (up/down always wins)");
        check (Ring::chooseDragMode (o, { 30.0f, 0.0f }, { 36.0f, 1.0f }) == Ring::DragMode::vertical, "radial (outwards) = vertical");
        check (Ring::chooseDragMode (o, { -21.0f, -21.0f }, { -16.0f, -26.0f }) == Ring::DragMode::rotary, "along the ring at 10:30 = rotation");

        // In the editor.
        DooblrQAudioProcessor processor;
        constexpr int bell = 2;
        setBand (processor, bell, params::band::enabled, 1.0f);
        setBand (processor, bell, params::band::type, (float) T::bell);
        setBand (processor, bell, params::band::gain, 3.0f);
        setBand (processor, bell, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, bell, params::band::range, -9.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* gainKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::gain));
        auto* frequencyKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::frequency));
        auto* rangeKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::range));
        check (dooblrEditor && gainKnob && frequencyKnob && rangeKnob, "editor and knobs exist");
        if (! (dooblrEditor && gainKnob && frequencyKnob && rangeKnob))
            return;

        editor->setSize (DooblrQAudioProcessorEditor::defaultWidth, DooblrQAudioProcessorEditor::defaultHeight);
        dooblrEditor->selectBand (bell);
        auto& ring = dooblrEditor->getRangeRing();
        const auto rangeId = params::bandId (bell, params::band::range);

        check (ring.getTarget() == gainKnob, "bell: the ring surrounds the Gain knob");
        check (ring.isEnabled() && ring.isVisible(), "ring is enabled with dynamics on");
        check (std::abs (ring.getRangeDb() + 9.0) < 1.0e-4, "ring shows the band's Range");

        // Two-way sync through the parameter.
        setParameter (processor, rangeId, 6.0f);
        check (std::abs (ring.getRangeDb() - 6.0) < 0.01 && std::abs (rangeKnob->getValue() - 6.0) < 0.01, "parameter -> ring and Range knob");
        rangeKnob->setValue (-9.0, juce::sendNotificationSync);
        check (std::abs (ring.getRangeDb() + 9.0) < 0.01, "Range knob -> ring");

        // Geometry: ring outside the (smaller) knob, inside the editor, hit only on the annulus.
        const auto geometry = dooblrq::ui::knobGeometry (*gainKnob);
        auto* thresholdKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::threshold));
        check (thresholdKnob != nullptr, "threshold knob exists");
        if (thresholdKnob == nullptr)
            return;
        const auto plainGeometry = dooblrq::ui::knobGeometry (*thresholdKnob);
        check (geometry.knobRadius < geometry.outerRadius && geometry.ringRadius > geometry.knobRadius + 2.0f,
               "the Gain knob shrinks to make room for its ring");
        check (std::abs (plainGeometry.knobRadius - plainGeometry.outerRadius) < 1.0e-4f, "knobs without a ring are unchanged");
        const auto centre = ring.getCentre();
        const auto r = ring.getRingRadius();
        check (! ring.hitTest ((int) centre.x, (int) centre.y), "centre of the knob is not the ring (the knob keeps its clicks)");
        check (ring.hitTest ((int) centre.x, (int) (centre.y - r)), "the ring itself takes clicks");
        check (! ring.hitTest ((int) centre.x, (int) (centre.y - r - 20.0f)), "outside the ring does not");
        check (ring.getOuterHitRadius() - ring.getInnerHitRadius() >= 10.0f,
               "ring hit area is generous (" + format (ring.getOuterHitRadius() - ring.getInnerHitRadius(), 1) + " px wide)");
        const auto ringInEditor = editor->getLocalPoint (&ring, centre);
        const auto knobInEditor = editor->getLocalPoint (gainKnob, geometry.centre);
        check (ringInEditor.getDistanceFrom (knobInEditor) < 0.01f, "ring is concentric with the knob");

        auto& history = processor.getUndoHistory();
        const auto top = centre + juce::Point<float> (0.0f, -r);

        // Vertical drag: 40 px up = +8 dB, one undo step.
        history.clear();
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (top + juce::Point<float> (0.0f, -4.0f * (float) i));
            simulatePathDrag (ring, path);
        }
        check (std::abs (getParameter (processor, rangeId) + 1.0f) < 0.1f,
               "vertical drag up 40 px: -9 -> -1 dB (got " + format (getParameter (processor, rangeId)) + ")");
        check (std::abs (rangeKnob->getValue() + 1.0) < 0.1, "Range knob follows the ring");
        history.undo();
        check (std::abs (getParameter (processor, rangeId) + 9.0f) < 0.01f && ! history.canUndo(), "the drag is one undo step");

        // Fine vertical drag with Shift.
        history.clear();
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (top + juce::Point<float> (0.0f, -4.0f * (float) i));
            simulatePathDrag (ring, path, true);
        }
        check (std::abs (getParameter (processor, rangeId) + 8.2f) < 0.05f,
               "Shift drag is fine: 40 px = +0.8 dB (got " + format (getParameter (processor, rangeId)) + ")");
        history.undo();

        // Rotation: around the ring, clockwise a quarter turn from the top.
        history.clear();
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 24; ++i)
                path.push_back (centre.getPointOnCircumference (r, (float) (pi / 2.0 * i / 24.0)));
            simulatePathDrag (ring, path);
        }
        const auto rotary = gainKnob->getRotaryParameters();
        const auto expected = -9.0 + 48.0 * (pi / 2.0) / (double) (rotary.endAngleRadians - rotary.startAngleRadians);
        check (std::abs (getParameter (processor, rangeId) - (float) expected) < 0.3f,
               "rotation drag a quarter turn clockwise: -9 -> " + format (expected) + " dB (got " + format (getParameter (processor, rangeId)) + ")");
        check (std::abs (rangeKnob->getValue() - expected) < 0.3, "Range knob follows the rotation");
        history.undo();
        check (std::abs (getParameter (processor, rangeId) + 9.0f) < 0.01f && ! history.canUndo(), "the rotation is one undo step");

        // Anticlockwise lowers it, and it clamps at the range limits.
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 48; ++i)
                path.push_back (centre.getPointOnCircumference (r, (float) (-pi * i / 48.0)));
            simulatePathDrag (ring, path);
        }
        check (std::abs (getParameter (processor, rangeId) + 24.0f) < 0.01f, "anticlockwise half turn lowers Range to its -24 dB limit");

        // Double-click resets to 0 dB (one undo step).
        history.clear();
        simulateDoubleClick (ring, top);
        check (std::abs (getParameter (processor, rangeId)) < 0.01f && std::abs (ring.getRangeDb()) < 0.01, "double-click resets to 0 dB");
        history.undo();
        check (std::abs (getParameter (processor, rangeId) + 24.0f) < 0.01f, "double-click reset is undoable");

        // Hover highlight and tooltip.
        ring.mouseEnter (makeMouseEvent (ring, top, top, 0, false));
        check (ring.isHovered(), "hover highlights the ring");
        ring.mouseExit (makeMouseEvent (ring, top, top, 0, false));
        check (! ring.isHovered(), "the highlight goes on exit");
        check (ring.getTooltip().contains ("Dynamic Range") && ring.getTooltip().contains ("Shift"), "ring has a tooltip");

        // Paint: the excursion arc is drawn in the band colour, on the ring, around the knob.
        setParameter (processor, rangeId, 12.0f);
        dooblrEditor->selectBand (bell);
        {
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            const auto from = gainKnob->getRotaryParameters();
            const auto proportionFor = [&] (double v) { return (float) gainKnob->valueToProportionOfLength (v); };
            const auto mid = from.startAngleRadians + 0.5f * (proportionFor (3.0) + proportionFor (15.0)) * (from.endAngleRadians - from.startAngleRadians);
            const auto onArc = ringInEditor.getPointOnCircumference (r, mid);
            const auto pixel = image.getPixelAt ((int) onArc.x, (int) onArc.y);
            const auto colour = dooblrq::ui::bandColour (bell);
            const auto distance = std::abs (pixel.getRed() - colour.getRed()) + std::abs (pixel.getGreen() - colour.getGreen())
                                + std::abs (pixel.getBlue() - colour.getBlue());
            check (distance < 80, "the ring's excursion arc is painted in the band colour (distance " + std::to_string (distance) + ")");
        }

        // Dynamics off (or a type without dynamics): the ring is disabled, so no dead control.
        setBand (processor, bell, params::band::dynamicsEnabled, 0.0f);
        dooblrEditor->selectBand (bell);
        check (! ring.isEnabled(), "dynamics off: the ring is disabled (dimmed)");
        setBand (processor, bell, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, bell, params::band::type, (float) T::notch);
        dooblrEditor->selectBand (bell);
        check (! ring.isEnabled(), "Notch (no dynamics): the ring is disabled");

        // Low / High Cut: the ring moves to the Frequency knob, mapping Range to the cutoff.
        setBand (processor, bell, params::band::type, (float) T::lowCut);
        setBand (processor, bell, params::band::frequency, 1000.0f);
        dooblrEditor->selectBand (bell);
        check (ring.getTarget() == frequencyKnob && ring.isEnabled(), "Low Cut: the ring surrounds the Frequency knob, enabled");
        check (! (bool) gainKnob->getProperties().getWithDefault ("rangeRing", false)
                   && (bool) frequencyKnob->getProperties().getWithDefault ("rangeRing", false),
               "only the ring's target knob makes room for it");
        check (ring.getTooltip().contains ("cutoff up") && ring.getTooltip().contains ("1/6 oct"),
               "Low Cut, +Range: the tooltip says the cutoff moves up (1 dB = 1/6 oct)");
        setBand (processor, bell, params::band::type, (float) T::highCut);
        dooblrEditor->selectBand (bell);
        check (ring.getTooltip().contains ("cutoff down"), "High Cut, +Range: the tooltip says the cutoff moves down");

        // Same parameter, so a ring drag on a cut band still lands on Range.
        history.clear();
        {
            const auto c = ring.getCentre();
            const auto t = c + juce::Point<float> (0.0f, -ring.getRingRadius());
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (t + juce::Point<float> (0.0f, 2.0f * (float) i));
            simulatePathDrag (ring, path);
        }
        // 0.9.2: down = downward compression for every type; for a cut that is more closing,
        // i.e. a larger positive Range.
        check (std::abs (getParameter (processor, rangeId) - 16.0f) < 0.1f,
               "cut band (0.9.2): ring drag down 20 px = 4 dB more downward compression, Range 12 -> 16 dB (closes more)");
        dooblrEditor->selectBand (bell);
        check (ring.getTooltip().contains ("(downward compression)"), "cut band, +Range: the tooltip calls it downward compression");

        processor.editorBeingDeleted (editor.get());
    }

    void testRangeRingSnapshots()
    {
        // Visual check (DOOBLRQ_EDITOR_SNAPSHOT): the editor with a moving dynamic Low Cut selected,
        // plus a close-up of the rings on a dynamic bell and a dynamic High Cut.
        DooblrQAudioProcessor processor;
        configureDynamicCut (processor, 0, dsp::FilterType::lowCut, 180.0f, dsp::Slope::db24, 12.0f);
        setBand (processor, 0, params::band::threshold, -30.0f);
        setBand (processor, 0, params::band::release, 2000.0f);
        setBand (processor, 2, params::band::enabled, 1.0f);
        setBand (processor, 2, params::band::gain, 3.0f);
        setBand (processor, 2, params::band::frequency, 900.0f);
        setBand (processor, 2, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 2, params::band::range, -9.0f);
        setBand (processor, 2, params::band::threshold, -40.0f);
        setBand (processor, 2, params::band::release, 2000.0f);
        configureDynamicCut (processor, 5, dsp::FilterType::highCut, 9000.0f, dsp::Slope::db24, -12.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        auto* curve = dynamic_cast<dooblrq::ui::ResponseCurveComponent*> (findById (*editor, "responseCurve"));
        check (dooblrEditor != nullptr && curve != nullptr, "editor exists");
        if (dooblrEditor == nullptr || curve == nullptr)
            return;

        editor->setSize (1080, 660);
        // Loud low and mid content and a high tone push all three bands.
        {
            const int numSamples = 24000;
            juce::AudioBuffer<float> buffer (2, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto t = i / 48000.0;
                const auto value = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t)
                                          + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 900.0 * t)
                                          + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 9000.0 * t));
                buffer.setSample (0, i, value);
                buffer.setSample (1, i, value);
            }
            processInBlocks (processor, buffer, { 512 });
        }

        const auto low = curve->cutMarker (0), high = curve->cutMarker (5);
        note ("live Low Cut " + format (low.staticHz, 0) + " -> " + format (low.liveHz, 0) + " Hz (limit " + format (low.limitHz, 0)
              + "), High Cut " + format (high.staticHz, 0) + " -> " + format (high.liveHz, 0) + " Hz (limit " + format (high.limitHz, 0) + ")");
        check (low.visible && low.liveHz > low.staticHz * 1.2 && high.visible && high.liveHz > high.staticHz * 1.2,
               "loud signal: Low Cut (+Range) rises, High Cut (-Range) rises (opens)");

        dooblrEditor->selectBand (0);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.0");

        // Close-up: the EQ knob row for the bell (ring on Gain) and the High Cut (ring on Frequency), 2x.
        const auto closeUp = [&] (int band)
        {
            dooblrEditor->selectBand (band);
            auto* frequencyKnob = findById (*editor, params::band::frequency);
            auto* widthKnob = findById (*editor, params::band::q);
            const auto area = frequencyKnob->getBounds().getUnion (widthKnob->getBounds()).expanded (16, 22)
                                  .withTrimmedRight (widthKnob->getWidth() / 2);
            return editor->createComponentSnapshot (area, true, 2.0f);
        };
        const auto bellImage = closeUp (2), cutImage = closeUp (5);
        juce::Image combined (juce::Image::ARGB, bellImage.getWidth(), bellImage.getHeight() + cutImage.getHeight(), true);
        {
            juce::Graphics g (combined);
            g.drawImageAt (bellImage, 0, 0);
            g.drawImageAt (cutImage, 0, bellImage.getHeight());
        }
        saveSnapshot (combined, "-v0.9.0-ring");
        processor.editorBeingDeleted (editor.get());
    }

    //==============================================================================
    // 0.9.1: band Solo
    void configureBell (DooblrQAudioProcessor& processor, int b, float frequency, float gainDb, float q)
    {
        setBand (processor, b, params::band::enabled, 1.0f);
        setBand (processor, b, params::band::type, (float) dsp::FilterType::bell);
        setBand (processor, b, params::band::frequency, frequency);
        setBand (processor, b, params::band::gain, gainDb);
        setBand (processor, b, params::band::q, q);
    }

    /** Processes a sine in 256-sample blocks; before each segment's first block calls its action. */
    juce::AudioBuffer<float> runSineSegments (DooblrQAudioProcessor& processor, double frequency, float amplitude,
                                              double sampleRate, const std::vector<std::pair<int, std::function<void()>>>& segments)
    {
        int total = 0;
        for (const auto& segment : segments)
            total += segment.first;

        juce::AudioBuffer<float> buffer (2, total);
        for (int i = 0; i < total; ++i)
        {
            const auto value = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }

        juce::MidiBuffer midi;
        int start = 0;
        for (const auto& [length, action] : segments)
        {
            if (action != nullptr)
                action();

            for (int done = 0; done < length;)
            {
                const auto n = std::min (256, length - done);
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start + done, n);
                processor.processBlock (view, midi);
                done += n;
            }
            start += length;
        }
        return buffer;
    }

    float maxStep (const juce::AudioBuffer<float>& buffer, int from, int to)
    {
        float result = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = std::max (1, from); i < to; ++i)
                result = std::max (result, std::abs (buffer.getSample (ch, i) - buffer.getSample (ch, i - 1)));
        return result;
    }

    double rmsDb (const juce::AudioBuffer<float>& buffer, int from, int to)
    {
        return juce::Decibels::gainToDecibels ((double) buffer.getRMSLevel (0, from, to - from), -200.0);
    }

    void testSoloRegions()
    {
        using T = dsp::FilterType;
        constexpr double sr = 48000.0;
        const auto magDb = [] (const dsp::SoloRegion& r, double f) { return 20.0 * std::log10 (dsp::soloMagnitudeAt (r, f, sr)); };

        dsp::BandSettings s;
        s.enabled = true;
        s.type = T::bell;
        s.frequency = 1000.0f;
        s.q = 1.414f;
        auto r = dsp::soloRegionFor (s, 0.0);
        const auto bw = params::qToOctaves (1.414);
        note ("bell region (Q 1.414): centre " + format (magDb (r, 1000.0), 2) + " dB, Width edges "
              + format (magDb (r, 1000.0 / std::pow (2.0, bw / 2.0)), 2) + " / " + format (magDb (r, 1000.0 * std::pow (2.0, bw / 2.0)), 2)
              + " dB, 100 Hz " + format (magDb (r, 100.0), 1) + " dB, 10 kHz " + format (magDb (r, 10000.0), 1) + " dB");
        check (r.shape == dsp::SoloShape::bandPass && std::abs (magDb (r, 1000.0)) < 0.01, "bell: band-pass, unity at the centre");
        check (std::abs (magDb (r, 1000.0 / std::pow (2.0, bw / 2.0)) + 3.0) < 0.3
                   && std::abs (magDb (r, 1000.0 * std::pow (2.0, bw / 2.0)) + 3.0) < 0.3,
               "bell: -3 dB at the band's Width edges");
        check (magDb (r, 100.0) < -30.0 && magDb (r, 10000.0) < -30.0, "bell: a decade away is more than 30 dB down");

        s.type = T::notch;
        check (dsp::soloRegionFor (s, 0.0).shape == dsp::SoloShape::bandPass, "notch: band-pass over the notched region");

        s.type = T::lowShelf;
        r = dsp::soloRegionFor (s, 0.0);
        check (r.shape == dsp::SoloShape::lowPass && std::abs (magDb (r, 50.0)) < 0.1 && magDb (r, 10000.0) < -30.0,
               "low shelf: the low side (low-pass at the shelf)");
        s.type = T::highShelf;
        r = dsp::soloRegionFor (s, 0.0);
        check (r.shape == dsp::SoloShape::highPass && std::abs (magDb (r, 15000.0)) < 0.2 && magDb (r, 100.0) < -30.0,
               "high shelf: the high side (high-pass at the shelf)");
        s.type = T::tilt;
        s.gainDb = 4.0f;
        check (dsp::soloRegionFor (s, 0.0).shape == dsp::SoloShape::highPass, "positive tilt: the raised high side");
        s.gainDb = -4.0f;
        check (dsp::soloRegionFor (s, 0.0).shape == dsp::SoloShape::lowPass, "negative tilt: the raised low side");

        s.type = T::lowCut;
        s.q = 0.707f;
        r = dsp::soloRegionFor (s, 0.0);
        check (r.shape == dsp::SoloShape::lowPass && r.fromInput && std::abs (r.frequency - 1000.0) < 1.0e-6
                   && std::abs (magDb (r, 100.0)) < 0.1 && magDb (r, 10000.0) < -30.0,
               "low cut (0.9.4): what it removes, the input below the cutoff (low-pass at the cutoff)");
        s.type = T::highCut;
        r = dsp::soloRegionFor (s, 0.0);
        check (r.shape == dsp::SoloShape::highPass && r.fromInput && std::abs (r.frequency - 1000.0) < 1.0e-6
                   && std::abs (magDb (r, 10000.0)) < 0.1 && magDb (r, 100.0) < -30.0,
               "high cut (0.9.4): what it removes, the input above the cutoff (high-pass at the cutoff)");
        s.type = T::bell;
        check (! dsp::soloRegionFor (s, 0.0).fromInput, "a bell's region filters the EQ output (not the input)");
        s.type = T::lowCut;
        s.dynamicsEnabled = true;
        r = dsp::soloRegionFor (s, 6.0);
        check (std::abs (r.frequency - dsp::DynamicEQBand::dynamicCutFrequency (T::lowCut, 1000.0, 6.0)) < 1.0e-6,
               "dynamic low cut: the region follows the live cutoff");
        s.type = T::highCut;
        r = dsp::soloRegionFor (s, 6.0);
        check (std::abs (r.frequency - dsp::DynamicEQBand::dynamicCutFrequency (T::highCut, 1000.0, 6.0)) < 1.0e-6 && r.frequency < 1000.0, "dynamic high cut: the region follows the live cutoff");
    }

    void testSoloAudio()
    {
        constexpr double sr = 48000.0;

        for (const auto mode : allPhaseModes)
        {
            const auto name = std::string (phaseName (mode));
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            configureBell (processor, 1, 5000.0f, -6.0f, 1.0f); // another band, not soloed
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            const auto latencyBefore = processor.getLatencySamples();
            const auto normalCentre = measureSineGainDb (processor, 1000.0, 0.25f, sr);

            setBand (processor, 0, params::band::solo, 1.0f);
            check (processor.isBandSoloed (0) && ! processor.isBandSoloed (1) && processor.isAnyBandSoloed(), name + ": band 1 soloed");
            processor.reset();
            const auto centre = measureSineGainDb (processor, 1000.0, 0.25f, sr);
            processor.reset();
            const auto low = measureSineGainDb (processor, 100.0, 0.25f, sr);
            processor.reset();
            const auto high = measureSineGainDb (processor, 10000.0, 0.25f, sr);
            processor.reset();
            const auto other = measureSineGainDb (processor, 5000.0, 0.25f, sr);
            note (name + " solo bell 1 kHz +6 dB: 1 kHz " + format (centre, 2) + " dB (normal " + format (normalCentre, 2)
                  + "), 100 Hz " + format (low, 1) + ", 10 kHz " + format (high, 1) + ", 5 kHz " + format (other, 1) + " dB");
            check (std::abs (centre - normalCentre) < 0.3, name + ": the soloed region plays at its EQ'd level");
            check (low < -30.0 && high < -30.0, name + ": far frequencies are attenuated by more than 30 dB");
            check (other < -15.0, name + ": another band's region is not heard");
            check (processor.getLatencySamples() == latencyBefore, name + ": soloing does not change the latency");

            // Every model.
            auto modelsOk = true;
            for (int m = 0; m < dsp::numModels; ++m)
            {
                setBand (processor, 0, params::band::model, (float) m);
                processor.reset();
                const auto c = measureSineGainDb (processor, 1000.0, 0.25f, sr, 0.5);
                processor.reset();
                const auto f = measureSineGainDb (processor, 12000.0, 0.25f, sr, 0.5);
                modelsOk = modelsOk && c > 0.0 && c < 12.0 && f < -30.0 && f < c - 30.0;
            }
            setBand (processor, 0, params::band::model, 0.0f);
            check (modelsOk, name + ": solo passes the region and rejects far frequencies with every model");
        }

        // Multiple solos sum: A + B == (A and B soloed), sample by sample.
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        {
            const auto run = [&] (bool a, bool b)
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureBell (processor, 0, 300.0f, 4.0f, 1.5f);
                configureBell (processor, 1, 3000.0f, -3.0f, 2.0f);
                setBand (processor, 0, params::band::solo, a ? 1.0f : 0.0f);
                setBand (processor, 1, params::band::solo, b ? 1.0f : 0.0f);
                prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
                juce::AudioBuffer<float> buffer (2, 48000);
                fillWithNoise (buffer, 91, 0.25f);
                processInBlocks (processor, buffer, { 512, 128, 333 });
                return buffer;
            };
            const auto both = run (true, true), onlyA = run (true, false), onlyB = run (false, true);
            float error = 0.0f, peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < both.getNumSamples(); ++i)
                {
                    error = std::max (error, std::abs (both.getSample (ch, i) - onlyA.getSample (ch, i) - onlyB.getSample (ch, i)));
                    peak = std::max (peak, std::abs (both.getSample (ch, i)));
                }
            note (std::string (phaseName (mode)) + ": two solos vs sum of each, max error " + format (error, 8) + " (peak " + format (peak, 3) + ")");
            check (error < 1.0e-5f && peak > 0.05f, std::string (phaseName (mode)) + ": multiple soloed bands sum");
        }

        // Click-free toggling (on, switch band, off) in every phase mode.
        for (const auto mode : allPhaseModes)
        {
            const auto name = std::string (phaseName (mode));
            DooblrQAudioProcessor processor;
            setParameter (processor, params::phaseMode, (float) mode);
            configureBell (processor, 0, 4000.0f, 0.0f, 1.0f);
            configureBell (processor, 1, 200.0f, 0.0f, 1.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 256);

            const int segment = 12000;
            float mixAfterOn = 0.0f;
            const auto out = runSineSegments (processor, 200.0, 0.5f, sr, {
                { segment, nullptr },
                { segment, [&] { setBand (processor, 0, params::band::solo, 1.0f); } },
                { segment, [&] { mixAfterOn = processor.getSoloMix();
                                 setBand (processor, 0, params::band::solo, 0.0f);
                                 setBand (processor, 1, params::band::solo, 1.0f); } },
                { segment, [&] { setBand (processor, 1, params::band::solo, 0.0f); } },
                { segment, nullptr } });

            const auto steady = maxStep (out, segment / 2, segment);
            const auto toggles = std::max ({ maxStep (out, segment - 512, segment + 2048),
                                             maxStep (out, 2 * segment - 512, 2 * segment + 2048),
                                             maxStep (out, 3 * segment - 512, 3 * segment + 2048) });
            const auto soloedOff = rmsDb (out, segment + 4000, 2 * segment);
            const auto soloedOn = rmsDb (out, 2 * segment + 4000, 3 * segment);
            const auto normal = rmsDb (out, 3 * segment + 4000, 4 * segment);
            note (name + " toggles: max step " + format (toggles, 4) + " vs steady " + format (steady, 4)
                  + "; 200 Hz tone with 4 kHz band soloed " + format (soloedOff, 1) + " dB, 200 Hz band soloed "
                  + format (soloedOn, 1) + " dB, normal " + format (normal, 1) + " dB");
            check (mixAfterOn == 1.0f, name + ": solo fades in fully (20 ms)");
            check (toggles < steady * 1.3f, name + ": solo on / switch / off are click-free");
            check (soloedOff < normal - 35.0 && std::abs (soloedOn - normal) < 0.5, name + ": solo switches what is heard");
            check (allFinite (out), name + ": output stays finite");
        }

        // A disabled band cannot be soloed: its Solo is ignored (exact identity here).
        {
            DooblrQAudioProcessor reference, processor;
            setBand (processor, 2, params::band::solo, 1.0f); // band 3 is off by default
            prepare (reference, juce::AudioChannelSet::stereo(), sr, 512);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            check (! processor.isBandSoloed (2) && ! processor.isAnyBandSoloed(), "Solo on a disabled band is not active");
            juce::AudioBuffer<float> a (2, 24000), b (2, 24000);
            fillWithNoise (a, 7, 0.3f);
            b.makeCopyOf (a);
            processInBlocks (reference, a, { 512 });
            processInBlocks (processor, b, { 512 });
            check (bitIdentical (a, b, 2), "a disabled band's Solo leaves the output unchanged");

            setBand (processor, 2, params::band::enabled, 1.0f);
            check (processor.isBandSoloed (2), "enabling the band makes its Solo active");
        }
    }

    void testSoloEditor()
    {
        DooblrQAudioProcessor processor;
        configureBell (processor, 0, 120.0f, 3.0f, 1.0f);
        configureBell (processor, 1, 1000.0f, -4.0f, 2.0f);
        configureBell (processor, 2, 6000.0f, 2.0f, 1.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        check (dooblrEditor != nullptr, "editor exists");
        if (dooblrEditor == nullptr)
            return;
        editor->setSize (1080, 660);

        const auto soloOn = [&] (int b) { return getParameter (processor, params::bandId (b, params::band::solo)) >= 0.5f; };
        std::array<BandSoloButton*, params::numBands> buttons {};
        auto allFound = true;
        for (int b = 0; b < params::numBands; ++b)
        {
            buttons[(size_t) b] = dynamic_cast<BandSoloButton*> (findById (*editor, "bandSolo" + juce::String (b + 1)));
            allFound = allFound && buttons[(size_t) b] != nullptr && buttons[(size_t) b]->isVisible()
                       && buttons[(size_t) b]->getWidth() > 0
                       && buttons[(size_t) b]->getParentComponent() != nullptr
                       && buttons[(size_t) b]->getParentComponent()->getLocalBounds().contains (buttons[(size_t) b]->getBounds());
        }
        auto* panelSolo = dynamic_cast<BandSoloButton*> (findById (*editor, params::band::solo));
        check (allFound && panelSolo != nullptr && panelSolo->getWidth() > 0, "every band tab has an S button, plus the band panel");
        if (! allFound || panelSolo == nullptr)
            return;

        dooblrEditor->selectBand (0); // refreshes the indicators (the timer does not run here)
        check (buttons[0]->isEnabled() && ! buttons[4]->isEnabled(), "Solo is disabled on a disabled band");

        // Plain clicks toggle and stack.
        auto& history = processor.getUndoHistory();
        buttons[0]->clicked ({});
        buttons[1]->clicked ({});
        check (soloOn (0) && soloOn (1) && ! soloOn (2), "clicks solo bands 1 and 2 together");
        check (buttons[0]->getToggleState() && buttons[1]->getToggleState() && panelSolo->getToggleState(),
               "the buttons show the Solo state");

        // Alt-click: exclusive, as one undo step.
        history.clear();
        buttons[2]->clicked (juce::ModifierKeys (juce::ModifierKeys::altModifier));
        check (! soloOn (0) && ! soloOn (1) && soloOn (2), "alt-click solos only that band");
        check (history.getNumUndoableSteps() == 1, "exclusive solo is one undo step");
        history.undo();
        check (soloOn (0) && soloOn (1) && ! soloOn (2), "undo restores the previous solos");
        history.clear();
        buttons[2]->clicked (juce::ModifierKeys (juce::ModifierKeys::altModifier));
        buttons[2]->clicked (juce::ModifierKeys (juce::ModifierKeys::altModifier));
        check (! soloOn (0) && ! soloOn (1) && ! soloOn (2), "alt-click on the only soloed band un-solos it");

        // Disabled band: clicking does nothing.
        buttons[4]->clicked ({});
        check (! soloOn (4), "a disabled band's Solo click does nothing");

        // Panel S follows the selected band.
        dooblrEditor->selectBand (1);
        panelSolo->clicked ({});
        check (soloOn (1) && buttons[1]->getToggleState(), "the panel S solos the selected band");
        panelSolo->clicked ({});
        check (! soloOn (1), "and un-solos it");

        // Snapshot: band 2 soloed (region lit, others dimmed, S badge on its handle).
        buttons[1]->clicked ({});
        dooblrEditor->selectBand (1);
        check (processor.isAnyBandSoloed() && processor.isBandSoloed (1), "band 2 soloed for the snapshot");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.1-solo");
        processor.editorBeingDeleted (editor.get());
    }

    //==============================================================================
    // 0.9.2: hover ring (dynamic Range on the graph) and the blue S.
    void testHoverRing()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        using T = dsp::FilterType;
        constexpr double pi = juce::MathConstants<double>::pi;

        DooblrQAudioProcessor processor;
        configureBell (processor, 0, 1000.0f, 0.0f, 1.0f);
        configureBell (processor, 1, 1250.0f, 0.0f, 1.0f);
        for (int b = 2; b < params::numBands; ++b)
            setBand (processor, b, params::band::enabled, 0.0f);
        setBand (processor, 0, params::band::threshold, -40.0f);
        setBand (processor, 0, params::band::attack, 1.0f);
        setBand (processor, 0, params::band::release, 100.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), 48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        check (dooblrEditor != nullptr, "editor exists");
        if (dooblrEditor == nullptr)
            return;
        editor->setSize (1080, 660);
        auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
        check (curve != nullptr && curve->getWidth() > 0, "response curve found");
        if (curve == nullptr)
            return;

        auto& history = processor.getUndoHistory();
        const auto rangeOf = [&] (int b) { return processor.getBandSettings (b).rangeDb; };
        const auto dynOn = [&] (int b) { return processor.getBandSettings (b).dynamicsEnabled; };
        const auto soloOn = [&] (int b) { return getParameter (processor, params::bandId (b, params::band::solo)) >= 0.5f; };
        const juce::Point<float> far { 5.0f, 5.0f };

        auto h0 = curve->handlePosition (0);
        const auto h1 = curve->handlePosition (1);
        note ("handles 1 / 2 are " + format (h0.getDistanceFrom (h1), 1) + " px apart");
        check (h0.getDistanceFrom (h1) < Curve::hoverRingOuterHit && h0.getDistanceFrom (h1) > 2.0f * Curve::handleHitRadius,
               "test setup: band 2's handle sits inside band 1's ring zone");

        // --- Proximity fade: ~100 ms ease-out in, ~150 ms out, nearest band only.
        check (curve->getHoverBand() < 0 && curve->getHoverAlpha (0) == 0.0f, "no ring before hovering");
        curve->updateHover (h0.translated (0.0f, 30.0f));
        check (curve->getHoverBand() == 0, "30 px below band 1's handle shows its ring");
        curve->advanceHoverAnimation (0.05);
        const auto half = curve->getHoverAlpha (0);
        note ("fade in: alpha " + format (half, 3) + " after 50 ms");
        check (half > 0.6f && half < 0.95f, "fade in is eased-out: well past half way at 50 ms, not yet full");
        check (curve->getHoverAlpha (1) == 0.0f, "only the nearest band's ring fades in");
        curve->advanceHoverAnimation (0.05);
        check (curve->getHoverAlpha (0) > 0.999f, "fully in after 100 ms");
        curve->updateHover (far);
        check (curve->getHoverBand() < 0, "leaving the proximity hides the ring");
        curve->advanceHoverAnimation (0.075);
        const auto out = curve->getHoverAlpha (0);
        check (out > 0.4f && out < 0.6f, "fade out: about half at 75 ms (linear, 150 ms)");
        curve->advanceHoverAnimation (0.075);
        check (curve->getHoverAlpha (0) < 0.001f, "fully out after 150 ms");
        curve->updateHover (h0.translated (0.0f, Curve::hoverProximity + 4.0f));
        check (curve->getHoverBand() < 0, "beyond 40 px: no ring");
        curve->updateHover (h0 + (h1 - h0) * 0.6f);
        check (curve->getHoverBand() == 1, "between two handles the nearer one wins");

        // --- Hit tests: the annulus never takes the handle's own clicks or a neighbour's.
        int hitBand = -1;
        check (curve->hitAt (h0, &hitBand) == Curve::Hit::handle && hitBand == 0, "handle centre -> handle");
        check (curve->hitAt (h0.translated (0.0f, 10.0f), &hitBand) == Curve::Hit::handle && hitBand == 0,
               "inside the handle's 14 px radius -> handle, not ring");
        check (curve->hitAt (h0.translated (0.0f, 25.0f), &hitBand) == Curve::Hit::rangeRing && hitBand == 0,
               "25 px below -> band 1's ring");
        check (curve->hitAt (h0.translated (-25.0f, 0.0f), &hitBand) == Curve::Hit::rangeRing && hitBand == 0,
               "25 px left -> band 1's ring");
        const auto neighbour = h1 + (h0 - h1) / h0.getDistanceFrom (h1) * 12.0f;
        check (neighbour.getDistanceFrom (h0) >= Curve::hoverRingInnerHit && neighbour.getDistanceFrom (h0) <= Curve::hoverRingOuterHit,
               "test setup: the point lies in band 1's annulus");
        check (curve->hitAt (neighbour, &hitBand) == Curve::Hit::handle && hitBand == 1,
               "band 2's handle inside band 1's ring zone still takes the click");
        check (curve->hitAt (h0.translated (0.0f, Curve::hoverRingOuterHit + 3.0f)) != Curve::Hit::rangeRing, "outside the annulus -> no ring");
        setBand (processor, 0, params::band::type, (float) T::notch);
        {
            const auto n = curve->handlePosition (0);
            curve->updateHover (n.translated (0.0f, 25.0f));
            check (curve->hitAt (n.translated (0.0f, 25.0f)) != Curve::Hit::rangeRing && ! curve->hoverRingAvailable (0),
                   "Notch (no dynamics): no ring");
        }
        setBand (processor, 0, params::band::type, (float) T::bell);

        // --- Ring drags. Band 2 off so only band 1 is near.
        setBand (processor, 1, params::band::enabled, 0.0f);
        h0 = curve->handlePosition (0);
        const auto r = Curve::hoverRingRadius;
        const auto arcPath = [&] (double toDegrees)
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 12; ++i)
            {
                const auto a = toDegrees * pi / 180.0 * i / 12.0;
                path.push_back (h0 + juce::Point<float> ((float) (r * std::sin (a)), (float) (-r * std::cos (a))));
            }
            return path;
        };
        const auto measureCopy = [&] (double frequency)
        {
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            DooblrQAudioProcessor copy;
            copy.setStateInformation (state.getData(), (int) state.getSize());
            prepare (copy, juce::AudioChannelSet::stereo(), 48000.0, 512);
            return measureSineGainDb (copy, frequency, 0.5f, 48000.0, 1.0);
        };

        check (! dynOn (0) && rangeOf (0) == 0.0f, "bell starts static, Range 0");
        const auto bellStatic = measureCopy (1000.0);
        history.clear();
        curve->updateHover (h0.translated (0.0f, -r));
        simulatePathDrag (*curve, arcPath (-45.0));
        note ("bell: CCW 45 deg -> Range " + format (rangeOf (0), 2) + " dB");
        check (std::abs (rangeOf (0) + 8.0f) < 0.3f, "bell: counter-clockwise 45 deg = 8 dB downward compression (Range -8)");
        check (dynOn (0), "moving the ring off 0 switches Dynamic on");
        check (history.getNumUndoableSteps() == 1, "Dynamic on + Range change are one undo step");
        const auto bellCcw = measureCopy (1000.0);
        history.undo();
        check (! dynOn (0) && std::abs (rangeOf (0)) < 0.01f, "one undo restores Dynamic off and Range 0");
        history.redo();
        check (dynOn (0) && std::abs (rangeOf (0) + 8.0f) < 0.3f, "redo brings both back");

        simulateDoubleClick (*curve, h0.translated (0.0f, -r));
        check (std::abs (rangeOf (0)) < 0.01f && dynOn (0), "double-click on the ring resets Range to 0 (Dynamic stays on)");

        history.clear();
        simulatePathDrag (*curve, arcPath (45.0));
        check (std::abs (rangeOf (0) - 8.0f) < 0.3f, "bell: clockwise 45 deg = 8 dB upward expansion (Range +8)");
        const auto bellCw = measureCopy (1000.0);
        note ("bell 1 kHz, -9 dBFS sine: static " + format (bellStatic, 2) + " dB, CCW " + format (bellCcw, 2)
              + " dB, CW " + format (bellCw, 2) + " dB");
        check (bellCcw < bellStatic - 5.0 && bellCw > bellStatic + 5.0,
               "bell DSP: CCW turns a loud signal down (compression), CW turns it up (expansion)");
        curve->advanceHoverAnimation (0.2); // no vblank in the headless test
        check (curve->getHoverBand() == 0 && curve->getHoverAlpha (0) > 0.999f, "the ring stays after the drag (pointer still on it)");

        // Vertical fallback: up = upward.
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (h0.translated (0.0f, -r - 2.0f * (float) i));
            simulatePathDrag (*curve, path);
        }
        check (std::abs (rangeOf (0) - 12.0f) < 0.3f, "vertical drag up 20 px on the ring = +4 dB upward (8 -> 12)");
        {
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (h0.translated (0.0f, -r + 30.0f * (float) i));
            simulatePathDrag (*curve, path);
        }
        check (std::abs (rangeOf (0) + 24.0f) < 0.01f, "dragging far down clamps at 24 dB downward");

        // Lock: while the ring is held, moving away keeps it.
        curve->mouseDown (makeMouseEvent (*curve, h0.translated (0.0f, -r), h0.translated (0.0f, -r), 1, false));
        curve->updateHover (far);
        curve->clearHover();
        check (curve->isRingDragging() && curve->getHoverBand() == 0, "the ring stays while it is being dragged, even off the display");
        curve->mouseUp (makeMouseEvent (*curve, far, h0.translated (0.0f, -r), 1, true));
        check (! curve->isRingDragging() && curve->getHoverBand() < 0, "released away from it: the ring fades out");

        // Low Cut: CCW closes the filter (downward), CW opens it (upward).
        configureDynamicCut (processor, 0, T::lowCut, 1000.0f, dsp::Slope::db24, 0.0f, false);
        h0 = curve->handlePosition (0);
        const auto cutStatic = measureCopy (1000.0);
        history.clear();
        curve->updateHover (h0.translated (0.0f, -r));
        simulatePathDrag (*curve, arcPath (-45.0));
        check (std::abs (rangeOf (0) - 8.0f) < 0.3f && dynOn (0),
               "Low Cut: counter-clockwise = downward compression = Range +8 (closes), Dynamic switched on");
        check (history.getNumUndoableSteps() == 1, "Low Cut: one undo step");
        const auto cutCcw = measureCopy (1000.0);
        simulateDoubleClick (*curve, h0.translated (0.0f, -r));
        simulatePathDrag (*curve, arcPath (45.0));
        check (std::abs (rangeOf (0) + 8.0f) < 0.3f, "Low Cut: clockwise = upward expansion = Range -8 (opens)");
        const auto cutCw = measureCopy (1000.0);
        note ("Low Cut 1 kHz 24 dB/oct, -9 dBFS sine at 1 kHz: static " + format (cutStatic, 2) + " dB, CCW " + format (cutCcw, 2)
              + " dB, CW " + format (cutCw, 2) + " dB");
        check (cutCcw < cutStatic - 6.0 && cutCw > cutStatic + 1.5,
               "Low Cut DSP: CCW closes on a loud signal (less level), CW opens (more level)");

        // --- The blue S.
        configureBell (processor, 0, 1000.0f, 0.0f, 1.0f);
        setBand (processor, 0, params::band::dynamicsEnabled, 0.0f);
        setBand (processor, 0, params::band::range, 0.0f);
        setBand (processor, 1, params::band::enabled, 1.0f);
        h0 = curve->handlePosition (0);
        curve->updateHover (h0.translated (0.0f, 25.0f));
        const auto badge = curve->soloBadgeBounds (0);
        note ("blue S: " + format (badge.getWidth(), 1) + " px, centre " + format (badge.getCentre().getDistanceFrom (h0), 1) + " px from the handle");
        check (curve->soloBadgeVisible (0) && badge.getWidth() >= 18.0f && badge.getHeight() >= 18.0f,
               "hovering shows an 18+ px S on the ring");
        check (badge.getCentreX() > h0.x && badge.getCentreY() < h0.y && curve->getLocalBounds().toFloat().contains (badge),
               "the S sits top-right of the ring, on screen");
        check (! badge.intersects (juce::Rectangle<float> (2.0f * Curve::handleHitRadius, 2.0f * Curve::handleHitRadius).withCentre (h0)),
               "the S is clear of the handle's own hit area");
        check (curve->hitAt (badge.getCentre(), &hitBand) == Curve::Hit::soloBadge && hitBand == 0, "clicking the S hits the S");
        check (! curve->soloBadgeVisible (2), "no S for a disabled band");

        const auto click = [&] (juce::Point<float> p, bool alt)
        {
            const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier
                                                  | (alt ? juce::ModifierKeys::altModifier : 0));
            curve->mouseDown (makeMouseEventWithMods (*curve, p, p, 1, false, mods));
            curve->mouseUp (makeMouseEventWithMods (*curve, p, p, 1, false, mods));
        };
        click (badge.getCentre(), false);
        check (soloOn (0) && ! soloOn (1), "the S solos the band");
        curve->updateHover (far);
        curve->advanceHoverAnimation (1.0);
        check (curve->getHoverBand() < 0 && curve->soloBadgeVisible (0), "a soloed band's S stays without hover");
        check (curve->hitAt (badge.getCentre(), &hitBand) == Curve::Hit::soloBadge && hitBand == 0, "and stays clickable");
        click (badge.getCentre(), false);
        check (! soloOn (0) && ! curve->soloBadgeVisible (0), "clicking it again un-solos (and the S goes)");

        setBand (processor, 1, params::band::solo, 1.0f);
        history.clear();
        curve->updateHover (h0.translated (0.0f, 25.0f));
        click (badge.getCentre(), true);
        check (soloOn (0) && ! soloOn (1), "alt-click on the S: exclusive solo");
        check (history.getNumUndoableSteps() == 1, "exclusive solo from the S is one undo step");
        check (processor.getBandSettings (0).rangeDb == 0.0f && ! dynOn (0), "the S click leaves the ring's Range alone");
        click (badge.getCentre(), false);
        check (! soloOn (0), "plain click un-solos");

        // Snapshot: a bell's ring held mid-drag (downward compression), with its blue S.
        configureBell (processor, 0, 900.0f, 3.0f, 1.2f);
        configureBell (processor, 2, 5000.0f, -2.0f, 1.5f);
        setBand (processor, 1, params::band::enabled, 0.0f);
        setBand (processor, 0, params::band::range, -4.0f);
        dooblrEditor->selectBand (0);
        h0 = curve->handlePosition (0);
        curve->updateHover (h0.translated (0.0f, -r));
        curve->advanceHoverAnimation (0.2);
        {
            const auto path = arcPath (-45.0);
            curve->mouseDown (makeMouseEvent (*curve, path.front(), path.front(), 1, false));
            for (size_t i = 1; i < path.size(); ++i)
                curve->mouseDrag (makeMouseEvent (*curve, path[i], path.front(), 1, true));
            check (curve->isRingDragging() && std::abs (rangeOf (0) + 12.0f) < 0.3f, "snapshot: ring held at 12 dB downward");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.2-hover");
            curve->mouseUp (makeMouseEvent (*curve, path.back(), path.front(), 1, true));
        }
        processor.editorBeingDeleted (editor.get());
    }

    //==============================================================================
    // 0.9.3: every band parameter stays editable while Solo is on, and the S only toggles on a
    // click (a drag that starts on it edits whatever is under it instead).
    void testSoloEditing()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        constexpr double sr = 48000.0;

        DooblrQAudioProcessor processor;
        configureBell (processor, 0, 1000.0f, 4.0f, 1.0f);
        configureBell (processor, 1, 4000.0f, -3.0f, 1.5f);
        configureBell (processor, 2, 250.0f, 2.0f, 1.0f);
        for (int b = 3; b < params::numBands; ++b)
            setBand (processor, b, params::band::enabled, 0.0f);
        setBand (processor, 0, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 0, params::band::range, -6.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        check (dooblrEditor != nullptr, "editor exists");
        if (dooblrEditor == nullptr)
            return;
        editor->setSize (1080, 660);
        auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
        check (curve != nullptr && curve->getWidth() > 0, "response curve found");
        if (curve == nullptr)
            return;

        auto& history = processor.getUndoHistory();
        const auto soloOn = [&] (int b) { return getParameter (processor, params::bandId (b, params::band::solo)) >= 0.5f; };
        const auto value = [&] (int b, const char* suffix) { return getParameter (processor, params::bandId (b, suffix)); };
        const auto solosAre = [&] (bool a, bool b, bool c) { return soloOn (0) == a && soloOn (1) == b && soloOn (2) == c; };

        setBand (processor, 0, params::band::solo, 1.0f);
        dooblrEditor->selectBand (0);
        check (processor.isBandSoloed (0) && solosAre (true, false, false), "band 1 soloed");

        // --- Graph handle drags: the soloed band and a band that is not soloed.
        for (const auto b : { 0, 1 })
        {
            const auto label = std::string (b == 0 ? "soloed" : "not soloed") + " band " + std::to_string (b + 1);
            const auto f0 = value (b, params::band::frequency), g0 = value (b, params::band::gain);
            const auto h = curve->handlePosition (b);
            curve->updateHover (h);
            history.clear();
            simulateDrag (*curve, h, h.translated (-40.0f, -30.0f));
            note (label + " handle drag: " + format (f0, 0) + " Hz / " + format (g0, 1) + " dB -> "
                  + format (value (b, params::band::frequency), 0) + " Hz / " + format (value (b, params::band::gain), 1) + " dB");
            check (value (b, params::band::frequency) < f0 * 0.9f && value (b, params::band::gain) > g0 + 1.0f,
                   label + ": the handle drag changes frequency and gain while Solo is on");
            check (curve->handlePosition (b).getDistanceFrom (h.translated (-40.0f, -30.0f)) < 1.5f, label + ": the handle follows the pointer");
            check (solosAre (true, false, false), label + ": the drag leaves Solo as it was");
            check (history.getNumUndoableSteps() == 1, label + ": one undo step");
        }

        // Alt-drag (Q) and the wheel (Q) on the soloed band.
        {
            const auto q0 = value (0, params::band::q);
            const auto h = curve->handlePosition (0);
            const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            curve->mouseDown (makeMouseEventWithMods (*curve, h, h, 1, false, alt));
            for (int i = 1; i <= 8; ++i)
                curve->mouseDrag (makeMouseEventWithMods (*curve, h.translated (0.0f, -6.0f * (float) i), h, 1, true, alt));
            curve->mouseUp (makeMouseEventWithMods (*curve, h.translated (0.0f, -48.0f), h, 1, true, alt));
            check (std::abs (value (0, params::band::q) - 2.0f * q0) < 0.02f * q0, "alt-drag up 48 px doubles the soloed band's Q");

            const auto q1 = value (0, params::band::q);
            juce::MouseWheelDetails wheel {};
            wheel.deltaY = 0.25f;
            curve->mouseWheelMove (makeMouseEvent (*curve, h, h, 0, false), wheel);
            check (value (0, params::band::q) > q1 * 1.3f, "the wheel changes the soloed band's Q");
            const auto q2 = value (1, params::band::q);
            const auto h1 = curve->handlePosition (1);
            curve->mouseWheelMove (makeMouseEvent (*curve, h1, h1, 0, false), wheel);
            check (value (1, params::band::q) > q2 * 1.3f, "and a band that is not soloed");
            check (solosAre (true, false, false), "Q edits leave Solo as it was");
        }

        // --- The hover ring on the soloed band (a vertical drag on its top).
        {
            const auto h = curve->handlePosition (0);
            const auto top = h.translated (0.0f, -Curve::hoverRingRadius);
            curve->updateHover (top);
            check (curve->hitAt (top) == Curve::Hit::rangeRing, "the soloed band's ring is hit (not its S)");
            const auto r0 = value (0, params::band::range);
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (top.translated (0.0f, -2.0f * (float) i));
            simulatePathDrag (*curve, path);
            check (std::abs (value (0, params::band::range) - (r0 + 4.0f)) < 0.3f, "ring drag on the soloed band: +4 dB Range");
            check (solosAre (true, false, false), "the ring drag leaves Solo on");
        }

        // --- 0.9.4: a drag that starts on the S moves its band (auditioning it); Solo does not toggle.
        {
            const auto h = curve->handlePosition (0);
            const auto badge = curve->soloBadgeBounds (0);
            const auto onS = badge.getCentre();
            curve->updateHover (onS);
            note ("S centre " + format (onS.getDistanceFrom (h), 1) + " px from the handle");
            check (curve->hitAt (onS) == Curve::Hit::soloBadge, "test setup: the press lands on the S");
            const auto r0 = value (0, params::band::range), g0 = value (0, params::band::gain);
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (onS.translated (0.0f, 2.0f * (float) i));
            simulatePathDrag (*curve, path);
            note ("drag from the S: gain " + format (g0, 2) + " -> " + format (value (0, params::band::gain), 2) + " dB, Range "
                  + format (r0, 2) + " -> " + format (value (0, params::band::range), 2) + " dB, Solo " + (soloOn (0) ? "on" : "off"));
            check (soloOn (0), "dragging from the soloed band's S does not un-solo it");
            check (value (0, params::band::gain) < g0 - 0.5f && std::abs (value (0, params::band::range) - r0) < 0.01f,
                   "the drag from the S moves the band (gain down), not the ring");
            setBand (processor, 0, params::band::gain, g0);

            // A click without a drag still toggles (on mouse-up).
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            check (soloOn (0), "pressing the S does not toggle yet");
            curve->mouseUp (makeMouseEvent (*curve, onS, onS, 1, false));
            check (! soloOn (0), "releasing without a drag toggles Solo off");
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            curve->mouseUp (makeMouseEvent (*curve, onS, onS, 1, false));
            check (soloOn (0), "a second click solos again");

            // A press on the S, moved off it and released elsewhere, is not a click.
            const auto f0 = value (0, params::band::frequency);
            const auto away = onS.translated (60.0f, 60.0f);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            curve->mouseDrag (makeMouseEvent (*curve, away, onS, 1, true));
            curve->mouseUp (makeMouseEvent (*curve, away, onS, 1, true));
            check (soloOn (0), "a drag off the S does not toggle Solo");
            setBand (processor, 0, params::band::frequency, f0);
            setBand (processor, 0, params::band::gain, g0);
        }

        // --- Another band's ring under the soloed band's S: the S takes the drag (0.9.4), and
        // that ring is still reachable from its annulus away from the S.
        {
            const auto badgeCentre = curve->soloBadgeBounds (0).getCentre();
            const auto target = badgeCentre.translated (Curve::hoverRingRadius, 0.0f);
            curve->updateHover (curve->handlePosition (2));
            simulateDrag (*curve, curve->handlePosition (2), target);
            const auto h2 = curve->handlePosition (2);
            check (h2.getDistanceFrom (target) < 1.5f, "test setup: band 3 sits next to band 1's S, which lies on band 3's ring");
            const auto r0 = value (2, params::band::range);
            const auto f0 = value (0, params::band::frequency), g0 = value (0, params::band::gain);
            curve->updateHover (badgeCentre);
            check (curve->hitAt (badgeCentre) == Curve::Hit::soloBadge, "the press on band 1's S hits the S");
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (badgeCentre.translated (0.0f, -2.0f * (float) i));
            simulatePathDrag (*curve, path);
            note ("drag from band 1's S over band 3's ring: band 1 gain " + format (g0, 2) + " -> "
                  + format (value (0, params::band::gain), 2) + " dB, band 3 Range " + format (r0, 2) + " -> "
                  + format (value (2, params::band::range), 2) + " dB");
            check (value (0, params::band::gain) > g0 + 0.5f && std::abs (value (2, params::band::range) - r0) < 0.01f,
                   "the S drag moves band 1, band 3's ring is untouched");
            check (solosAre (true, false, false), "and band 1 stays soloed");
            setBand (processor, 0, params::band::frequency, f0);
            setBand (processor, 0, params::band::gain, g0);

            const auto top = curve->handlePosition (2).translated (0.0f, -Curve::hoverRingRadius);
            curve->updateHover (top);
            check (curve->hitAt (top) == Curve::Hit::rangeRing, "band 3's ring annulus away from the S is a ring hit");
            path.clear();
            for (int i = 0; i <= 10; ++i)
                path.push_back (top.translated (0.0f, -2.0f * (float) i));
            simulatePathDrag (*curve, path);
            note ("band 3 ring drag from its annulus: Range " + format (r0, 2) + " -> " + format (value (2, params::band::range), 2) + " dB");
            check (std::abs (value (2, params::band::range) - (r0 + 4.0f)) < 0.3f && processor.getBandSettings (2).dynamicsEnabled,
                   "band 3's ring still takes a drag from its own annulus");
            check (solosAre (true, false, false), "and band 1 stays soloed");
            setBand (processor, 2, params::band::frequency, 250.0f);
        }

        // --- Panel knobs and the outer Range ring while soloed (band 1 selected).
        dooblrEditor->selectBand (0);
        {
            auto* gainKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::gain));
            auto* frequencyKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::frequency));
            auto* qKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::q));
            auto* thresholdKnob = dynamic_cast<juce::Slider*> (findById (*editor, params::band::threshold));
            check (gainKnob && frequencyKnob && qKnob && thresholdKnob, "panel knobs exist");
            if (! (gainKnob && frequencyKnob && qKnob && thresholdKnob))
                return;

            for (auto* knob : { frequencyKnob, gainKnob, qKnob, thresholdKnob })
            {
                const auto name = knob->getComponentID().toStdString();
                const auto before = knob->getValue();
                check (knob->isEnabled(), name + " knob is enabled while soloed");
                const auto centre = knob->getLocalBounds().getCentre().toFloat();
                simulateDrag (*knob, centre, centre.translated (0.0f, -40.0f));
                check (std::abs (knob->getValue() - before) > 1.0e-3, name + " knob drag changes the value while soloed ("
                       + format (before, 2) + " -> " + format (knob->getValue(), 2) + ")");
            }
            check (std::abs (value (0, params::band::gain) - (float) gainKnob->getValue()) < 0.01f, "the knob drags reach the parameters");

            auto& ring = dooblrEditor->getRangeRing();
            check (ring.isEnabled() && ring.isVisible(), "the outer Range ring is enabled while soloed");
            const auto r0 = value (0, params::band::range);
            const auto top = ring.getCentre() + juce::Point<float> (0.0f, -ring.getRingRadius());
            std::vector<juce::Point<float>> path;
            for (int i = 0; i <= 10; ++i)
                path.push_back (top.translated (0.0f, 2.0f * (float) i));
            simulatePathDrag (ring, path);
            check (std::abs (value (0, params::band::range) - (r0 - 4.0f)) < 0.3f, "outer Range ring drag: -4 dB while soloed");
            check (solosAre (true, false, false), "panel edits leave Solo as it was");
        }

        // --- The audition follows: region and output track a frequency drag, with no clicks.
        {
            setBand (processor, 0, params::band::dynamicsEnabled, 0.0f);
            setBand (processor, 2, params::band::dynamicsEnabled, 0.0f);
            configureBell (processor, 0, 1000.0f, 4.0f, 2.0f);
            processor.reset();
            const auto regionBefore = processor.getSoloRegion (0).frequency;
            const auto atOne = measureSineGainDb (processor, 1000.0, 0.25f, sr, 0.5);
            const auto atFour = measureSineGainDb (processor, 4000.0, 0.25f, sr, 0.5);
            const auto h = curve->handlePosition (0);
            const auto to = juce::Point<float> (curve->frequencyToX (4000.0), h.y);
            curve->updateHover (h);
            simulateDrag (*curve, h, to);
            note ("soloed band dragged to " + format (value (0, params::band::frequency), 0) + " Hz; region "
                  + format (regionBefore, 0) + " -> " + format (processor.getSoloRegion (0).frequency, 0) + " Hz");
            check (std::abs (value (0, params::band::frequency) - 4000.0f) < 40.0f && soloOn (0), "drag to 4 kHz while soloed");
            check (std::abs (processor.getSoloRegion (0).frequency - 4000.0) < 40.0, "the solo region follows the drag");
            processor.reset();
            const auto oneAfter = measureSineGainDb (processor, 1000.0, 0.25f, sr, 0.5);
            const auto fourAfter = measureSineGainDb (processor, 4000.0, 0.25f, sr, 0.5);
            note ("audition: 1 kHz " + format (atOne, 1) + " -> " + format (oneAfter, 1) + " dB, 4 kHz "
                  + format (atFour, 1) + " -> " + format (fourAfter, 1) + " dB");
            check (atOne > -1.0 && atFour < -15.0 && fourAfter > -1.0 && oneAfter < -15.0, "the audition moves with the band");

            // A slow sweep while soloed (one parameter change per block, as a drag does).
            setBand (processor, 0, params::band::frequency, 1000.0f);
            setBand (processor, 0, params::band::gain, 0.0f);
            processor.reset();
            std::vector<std::pair<int, std::function<void()>>> segments { { 9600, nullptr } };
            for (int i = 1; i <= 75; ++i)
                segments.push_back ({ 256, [&processor, i] { setBand (processor, 0, params::band::frequency,
                                                                       (float) (1000.0 * std::pow (1.6, (double) i / 75.0))); } });
            segments.push_back ({ 4800, nullptr });
            const auto out = runSineSegments (processor, 1250.0, 0.5f, sr, segments);
            const auto sweepFrom = 9600, sweepTo = 9600 + 75 * 256;
            const auto sweep = maxStep (out, sweepFrom, sweepTo);
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = sweepFrom; i < sweepTo; ++i)
                    peak = std::max (peak, std::abs (out.getSample (ch, i)));
            // A clean sine of amplitude A never steps more than 2 A sin (pi f / sr) per sample.
            const auto ideal = 2.0f * peak * (float) std::sin (juce::MathConstants<double>::pi * 1250.0 / sr);
            note ("soloed sweep 1 -> 1.6 kHz, 1.25 kHz tone: max step " + format (sweep, 4) + " vs " + format (ideal, 4)
                  + " for a clean sine at the sweep's peak level " + format (peak, 3));
            check (allFinite (out) && peak > 0.2f && sweep < ideal * 1.1f, "moving a soloed band is click-free (no zipper steps)");
        }

        // Snapshot: band 1 soloed and held mid-drag, a non-soloed band dimmed beside it.
        configureBell (processor, 0, 900.0f, 5.0f, 1.4f);
        setBand (processor, 0, params::band::dynamicsEnabled, 1.0f);
        setBand (processor, 0, params::band::range, -6.0f);
        setBand (processor, 2, params::band::frequency, 200.0f);
        dooblrEditor->selectBand (0);
        {
            const auto h = curve->handlePosition (0);
            curve->updateHover (h);
            curve->advanceHoverAnimation (0.2);
            curve->mouseDown (makeMouseEvent (*curve, h, h, 1, false));
            for (int i = 1; i <= 8; ++i)
                curve->mouseDrag (makeMouseEvent (*curve, h.translated (6.0f * (float) i, -3.0f * (float) i), h, 1, true));
            curve->advanceHoverAnimation (0.2);
            check (soloOn (0) && value (0, params::band::frequency) > 1000.0f, "snapshot: soloed band held mid-drag");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.3-solo-drag");
            curve->mouseUp (makeMouseEvent (*curve, h.translated (48.0f, -24.0f), h, 1, true));
        }

        // Paint cost with Solo on vs off (a note only: a timing, not a pass / fail).
        {
            const auto timePaints = [&]
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 20; ++i)
                    curve->createComponentSnapshot (curve->getLocalBounds());
                return (juce::Time::getMillisecondCounterHiRes() - start) / 20.0;
            };
            const auto soloed = timePaints();
            setBand (processor, 0, params::band::solo, 0.0f);
            const auto normal = timePaints();
            note ("curve paint (software renderer): " + format (soloed, 2) + " ms with Solo on, " + format (normal, 2) + " ms off");
        }
        processor.editorBeingDeleted (editor.get());
    }

    //==============================================================================
    // 0.9.4: press on a band's S and drag: that band is auditioned alone for the gesture and
    // moves like its handle; Solo itself is untouched. Cuts audition what they remove.
    void testSoloDrag()
    {
        using Curve = dooblrq::ui::ResponseCurveComponent;
        constexpr double sr = 48000.0;

        // --- Cut auditions through the processor: the removed side of the input.
        for (const auto mode : allPhaseModes)
        {
            const auto name = std::string (phaseName (mode));
            for (const auto type : { dsp::FilterType::lowCut, dsp::FilterType::highCut })
            {
                const auto low = type == dsp::FilterType::lowCut;
                const auto label = name + (low ? " low cut 1 kHz" : " high cut 1 kHz");
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureBell (processor, 0, 1000.0f, 0.0f, 0.707f);
                setBand (processor, 0, params::band::type, (float) type);
                configureBell (processor, 1, 3000.0f, 3.0f, 1.0f); // a band that is not soloed
                prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
                const auto eqBelow = measureSineGainDb (processor, 200.0, 0.25f, sr, 0.5);
                processor.reset();
                const auto eqAbove = measureSineGainDb (processor, 5000.0, 0.25f, sr, 0.5);

                setBand (processor, 0, params::band::solo, 1.0f);
                processor.reset();
                const auto below = measureSineGainDb (processor, 200.0, 0.25f, sr, 0.5);
                processor.reset();
                const auto above = measureSineGainDb (processor, 5000.0, 0.25f, sr, 0.5);
                note (label + ": EQ 200 Hz " + format (eqBelow, 1) + " / 5 kHz " + format (eqAbove, 1) + " dB; soloed 200 Hz "
                      + format (below, 1) + " / 5 kHz " + format (above, 1) + " dB");
                const auto kept = low ? below : above, removed = low ? above : below;
                check (kept > -1.0 && kept < 0.5 && removed < -30.0,
                       label + ": solo plays what the cut removes (" + std::string (low ? "below" : "above") + " the cutoff) at input level");

                // The S-drag audition does the same, exclusively (Solo param off on this band).
                setBand (processor, 0, params::band::solo, 0.0f);
                setBand (processor, 1, params::band::solo, 1.0f);
                processor.setSoloAudition (0);
                check (processor.isBandHeardSolo (0) && ! processor.isBandHeardSolo (1) && ! processor.isBandSoloed (0),
                       label + ": the audition is heard alone, not a parameter");
                processor.reset();
                const auto aBelow = measureSineGainDb (processor, 200.0, 0.25f, sr, 0.5);
                processor.reset();
                const auto aAbove = measureSineGainDb (processor, 5000.0, 0.25f, sr, 0.5);
                processor.reset();
                const auto aOther = measureSineGainDb (processor, 3000.0, 0.25f, sr, 0.5);
                note (label + " audition: 200 Hz " + format (aBelow, 1) + " / 5 kHz " + format (aAbove, 1) + " / 3 kHz "
                      + format (aOther, 1) + " dB");
                const auto aKept = low ? aBelow : aAbove, aRemoved = low ? aAbove : aBelow;
                check (aKept > -1.0 && aRemoved < -30.0, label + ": the S-drag audition plays the removed side too");
                processor.setSoloAudition (-1);
                check (! processor.isBandHeardSolo (0) && processor.isBandHeardSolo (1), label + ": ending it restores Solo as it was");
            }

            // Alignment: in a latent mode the removed side comes from the delayed input, so an
            // impulse through a soloed low cut peaks at the plug-in latency (plus the filter's
            // own short delay), not before it.
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureBell (processor, 0, 1000.0f, 0.0f, 0.707f);
                setBand (processor, 0, params::band::type, (float) dsp::FilterType::lowCut);
                setBand (processor, 0, params::band::solo, 1.0f);
                prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
                const auto latency = processor.getLatencySamples();
                juce::AudioBuffer<float> buffer (2, 48000);
                buffer.clear();
                processInBlocks (processor, buffer, { 512 }); // the solo fade settles on silence
                buffer.clear();
                buffer.setSample (0, 0, 1.0f);
                buffer.setSample (1, 0, 1.0f);
                processInBlocks (processor, buffer, { 512 });
                int peakAt = 0;
                for (int i = 1; i < buffer.getNumSamples(); ++i)
                    if (std::abs (buffer.getSample (0, i)) > std::abs (buffer.getSample (0, peakAt)))
                        peakAt = i;
                float early = 0.0f;
                for (int i = 0; i < std::max (0, latency - 8); ++i)
                    early = std::max (early, std::abs (buffer.getSample (0, i)));
                note (name + ": soloed low cut impulse peak at " + std::to_string (peakAt) + " samples (latency "
                      + std::to_string (latency) + "), max before latency " + format (early, 6));
                check (peakAt >= latency && peakAt < latency + 48 && early < 1.0e-3f,
                       name + ": the cut audition is aligned with the EQ output (no added latency)");
            }
        }

        // --- Slicing: a 4096-sample block gives what 512-sample blocks give (low cut soloed).
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        {
            const auto run = [&] (std::initializer_list<int> blocks)
            {
                DooblrQAudioProcessor processor;
                setParameter (processor, params::phaseMode, (float) mode);
                configureBell (processor, 0, 800.0f, 0.0f, 0.707f);
                setBand (processor, 0, params::band::type, (float) dsp::FilterType::lowCut);
                setBand (processor, 0, params::band::solo, 1.0f);
                configureBell (processor, 1, 3000.0f, 4.0f, 1.0f);
                setBand (processor, 1, params::band::solo, 1.0f);
                prepare (processor, juce::AudioChannelSet::stereo(), sr, 4096);
                juce::AudioBuffer<float> buffer (2, 32768);
                fillWithNoise (buffer, 17, 0.25f);
                processInBlocks (processor, buffer, blocks);
                return buffer;
            };
            const auto big = run ({ 4096 }), small = run ({ 512 });
            float error = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < big.getNumSamples(); ++i)
                    error = std::max (error, std::abs (big.getSample (ch, i) - small.getSample (ch, i)));
            note (std::string (phaseName (mode)) + ": 4096- vs 512-sample blocks with solos, max difference " + format (error, 8));
            check (error < 1.0e-5f && allFinite (big), std::string (phaseName (mode)) + ": large blocks match small blocks with solos");
        }

        // --- The editor gesture.
        DooblrQAudioProcessor processor;
        configureBell (processor, 0, 1000.0f, 4.0f, 1.0f);
        configureBell (processor, 1, 4000.0f, -3.0f, 1.5f);
        configureBell (processor, 2, 120.0f, 0.0f, 0.707f);
        setBand (processor, 2, params::band::type, (float) dsp::FilterType::lowCut);
        for (int b = 3; b < params::numBands; ++b)
            setBand (processor, b, params::band::enabled, 0.0f);
        prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
        check (dooblrEditor != nullptr, "editor exists");
        if (dooblrEditor == nullptr)
            return;
        editor->setSize (1080, 660);
        auto* curve = dynamic_cast<Curve*> (findById (*editor, "responseCurve"));
        check (curve != nullptr && curve->getWidth() > 0, "response curve found");
        if (curve == nullptr)
            return;

        auto& history = processor.getUndoHistory();
        const auto soloOn = [&] (int b) { return getParameter (processor, params::bandId (b, params::band::solo)) >= 0.5f; };
        const auto value = [&] (int b, const char* suffix) { return getParameter (processor, params::bandId (b, suffix)); };

        setBand (processor, 0, params::band::solo, 1.0f); // band 1 soloed, band 2 not

        // Press, drag, release on band 2 (not soloed) and on band 1 (soloed).
        for (const auto b : { 1, 0 })
        {
            const auto label = std::string (b == 0 ? "soloed" : "not soloed") + " band " + std::to_string (b + 1);
            const auto wasSoloed = soloOn (b);
            const auto f0 = value (b, params::band::frequency), g0 = value (b, params::band::gain);
            const auto h0 = curve->handlePosition (b);
            curve->updateHover (h0);
            curve->advanceHoverAnimation (0.2);
            const auto onS = curve->soloBadgeBounds (b).getCentre();
            const auto sOffset = onS - h0;
            curve->updateHover (onS);
            check (curve->hitAt (onS) == Curve::Hit::soloBadge, label + ": test setup, the press lands on the S");
            history.clear();

            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            check (processor.getSoloAudition() < 0 && curve->getSoloDragBand() < 0, label + ": the press alone does not audition");
            curve->mouseDrag (makeMouseEvent (*curve, onS.translated (2.0f, 0.0f), onS, 1, false));
            check (processor.getSoloAudition() < 0, label + ": within the 3 px click slop, still no audition");

            const auto delta = juce::Point<float> (-50.0f, -25.0f);
            for (int i = 1; i <= 10; ++i)
                curve->mouseDrag (makeMouseEvent (*curve, onS + delta * ((float) i / 10.0f), onS, 1, true));
            const auto end = onS + delta;
            curve->advanceHoverAnimation (0.05);
            const auto heardAlone = processor.isBandHeardSolo (b) && processor.isAnyBandHeardSolo();
            auto othersHeard = false;
            for (int o = 0; o < params::numBands; ++o)
                othersHeard = othersHeard || (o != b && processor.isBandHeardSolo (o));
            const auto badgeNow = curve->soloBadgeBounds (b).getCentre();
            note (label + " S-drag: " + format (f0, 0) + " Hz / " + format (g0, 1) + " dB -> " + format (value (b, params::band::frequency), 0)
                  + " Hz / " + format (value (b, params::band::gain), 1) + " dB; S " + format (badgeNow.getDistanceFrom (end), 2)
                  + " px from the pointer, handle offset kept within " + format ((badgeNow - curve->handlePosition (b)).getDistanceFrom (sOffset), 2) + " px");
            check (processor.getSoloAudition() == b && curve->getSoloDragBand() == b, label + ": dragging auditions the band");
            check (heardAlone && ! othersHeard, label + ": the audition is exclusive (other soloed bands are not heard)");
            check (soloOn (0) && ! soloOn (1), label + ": the Solo parameters are untouched during the drag");
            check (value (b, params::band::frequency) < f0 * 0.85f && value (b, params::band::gain) > g0 + 1.0f,
                   label + ": the drag moves the band (frequency down, gain up)");
            check (badgeNow.getDistanceFrom (end) < 1.5f, label + ": the S stays under the pointer, pinned to its handle");
            check (dooblrEditor->getSelectedBand() == b, label + ": the dragged band is selected");

            curve->mouseUp (makeMouseEvent (*curve, end, onS, 1, true));
            check (processor.getSoloAudition() < 0 && curve->getSoloDragBand() < 0, label + ": release ends the audition");
            check (soloOn (b) == wasSoloed && soloOn (0) && ! soloOn (1), label + ": Solo returns to what it was");
            check (processor.isBandHeardSolo (0) && ! processor.isBandHeardSolo (1), label + ": band 1's own Solo is heard again");
            check (history.getNumUndoableSteps() == 1, label + ": the move is one undo step");
            history.undo();
            check (std::abs (value (b, params::band::frequency) - f0) < 0.5f && std::abs (value (b, params::band::gain) - g0) < 0.01f
                       && soloOn (0) && ! soloOn (1),
                   label + ": undo restores the band and leaves Solo alone");
        }

        // A cut band's S-drag: vertical is Q.
        {
            const auto h0 = curve->handlePosition (2);
            curve->updateHover (h0);
            curve->advanceHoverAnimation (0.2);
            const auto onS = curve->soloBadgeBounds (2).getCentre();
            curve->updateHover (onS);
            const auto q0 = value (2, params::band::q), f0 = value (2, params::band::frequency);
            history.clear();
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            for (int i = 1; i <= 10; ++i)
                curve->mouseDrag (makeMouseEvent (*curve, onS.translated (4.0f * (float) i, -4.8f * (float) i), onS, 1, true));
            const auto auditioned = processor.getSoloAudition() == 2 && processor.getSoloRegion (2).fromInput;
            curve->mouseUp (makeMouseEvent (*curve, onS.translated (40.0f, -48.0f), onS, 1, true));
            note ("low cut S-drag: " + format (f0, 0) + " Hz / Q " + format (q0, 2) + " -> " + format (value (2, params::band::frequency), 0)
                  + " Hz / Q " + format (value (2, params::band::q), 2));
            check (auditioned, "low cut S-drag auditions the removed side (input, low-pass)");
            check (value (2, params::band::frequency) > f0 * 1.1f && value (2, params::band::q) > q0 * 1.2f,
                   "low cut S-drag: horizontal is the cutoff, up raises Q");
            check (history.getNumUndoableSteps() == 1 && processor.getSoloAudition() < 0 && ! soloOn (2), "one undo step, audition ended, Solo off");
            setBand (processor, 2, params::band::frequency, f0);
            setBand (processor, 2, params::band::q, q0);
        }

        // A click (under 3 px) still toggles; Alt-click is exclusive.
        {
            const auto h1 = curve->handlePosition (1);
            curve->updateHover (h1);
            curve->advanceHoverAnimation (0.2);
            const auto onS = curve->soloBadgeBounds (1).getCentre();
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            curve->mouseDrag (makeMouseEvent (*curve, onS.translated (2.0f, 1.0f), onS, 1, false));
            curve->mouseUp (makeMouseEvent (*curve, onS.translated (2.0f, 1.0f), onS, 1, false));
            check (soloOn (0) && soloOn (1) && processor.getSoloAudition() < 0, "a 2 px click on band 2's S adds it to Solo, no audition");
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            curve->mouseUp (makeMouseEvent (*curve, onS, onS, 1, false));
            check (soloOn (0) && ! soloOn (1), "a second click un-solos it");

            const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEventWithMods (*curve, onS, onS, 1, false, alt));
            curve->mouseUp (makeMouseEventWithMods (*curve, onS, onS, 1, false, alt));
            check (! soloOn (0) && soloOn (1), "Alt-click on band 2's S solos it exclusively");
            setBand (processor, 0, params::band::solo, 1.0f);
            setBand (processor, 1, params::band::solo, 0.0f);
        }

        // --- No zipper: auditioning on, sweeping, and off, one change per block as a drag does.
        for (const auto mode : allPhaseModes)
        {
            const auto name = std::string (phaseName (mode));
            DooblrQAudioProcessor audio;
            setParameter (audio, params::phaseMode, (float) mode);
            configureBell (audio, 0, 1000.0f, 3.0f, 1.0f);
            configureBell (audio, 1, 4000.0f, 0.0f, 1.0f);
            setBand (audio, 0, params::band::solo, 1.0f);
            prepare (audio, juce::AudioChannelSet::stereo(), sr, 256);
            constexpr double tone = 1250.0;
            std::vector<std::pair<int, std::function<void()>>> segments { { 9600, nullptr }, { 4800, [&audio] { audio.setSoloAudition (1); } } };
            for (int i = 1; i <= 75; ++i)
                segments.push_back ({ 256, [&audio, i] { setBand (audio, 1, params::band::frequency,
                                                                (float) (4000.0 * std::pow (0.3125, (double) i / 75.0))); } });
            segments.push_back ({ 4800, [&audio] { audio.setSoloAudition (-1); } });
            segments.push_back ({ 4800, nullptr });
            const auto out = runSineSegments (audio, tone, 0.5f, sr, segments);
            const auto sweepFrom = 9600 + 4800, sweepTo = sweepFrom + 75 * 256;
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 9600; i < out.getNumSamples(); ++i)
                    peak = std::max (peak, std::abs (out.getSample (ch, i)));
            const auto ideal = 2.0f * peak * (float) std::sin (juce::MathConstants<double>::pi * tone / sr);
            const auto onStep = maxStep (out, 9600, sweepFrom), sweep = maxStep (out, sweepFrom, sweepTo);
            const auto offStep = maxStep (out, sweepTo, sweepTo + 4800);
            note (name + ": audition on / sweep 4 -> 1.25 kHz / off, 1.25 kHz tone: max steps " + format (onStep, 4) + " / "
                  + format (sweep, 4) + " / " + format (offStep, 4) + " vs " + format (ideal, 4) + " for a clean sine at peak " + format (peak, 3));
            check (allFinite (out) && peak > 0.2f && onStep < ideal * 1.1f && sweep < ideal * 1.1f && offStep < ideal * 1.1f,
                   name + ": the S-drag audition switches and sweeps click-free (no zipper steps)");
        }

        // A cut audition swept the same way (the removed side follows the cutoff smoothly).
        {
            DooblrQAudioProcessor audio;
            configureBell (audio, 0, 2000.0f, 0.0f, 0.707f);
            setBand (audio, 0, params::band::type, (float) dsp::FilterType::highCut);
            prepare (audio, juce::AudioChannelSet::stereo(), sr, 256);
            constexpr double tone = 1600.0;
            std::vector<std::pair<int, std::function<void()>>> segments { { 9600, nullptr }, { 4800, [&audio] { audio.setSoloAudition (0); } } };
            for (int i = 1; i <= 75; ++i)
                segments.push_back ({ 256, [&audio, i] { setBand (audio, 0, params::band::frequency,
                                                                (float) (2000.0 * std::pow (0.5, (double) i / 75.0))); } });
            segments.push_back ({ 4800, [&audio] { audio.setSoloAudition (-1); } });
            segments.push_back ({ 4800, nullptr });
            const auto out = runSineSegments (audio, tone, 0.5f, sr, segments);
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    peak = std::max (peak, std::abs (out.getSample (ch, i)));
            const auto ideal = 2.0f * peak * (float) std::sin (juce::MathConstants<double>::pi * tone / sr);
            const auto step = maxStep (out, 9600, out.getNumSamples());
            note ("high cut audition sweep 2 -> 1 kHz, 1.6 kHz tone: max step " + format (step, 4) + " vs " + format (ideal, 4)
                  + " (peak " + format (peak, 3) + ")");
            check (allFinite (out) && step < ideal * 1.1f, "a cut's audition sweeps click-free");
        }

        // Snapshot: band 2 held mid S-drag (auditioned; band 1's own Solo not heard meanwhile).
        // (2 kHz: clear of the disabled bands' handles, which win over an S.)
        configureBell (processor, 1, 2000.0f, -4.0f, 1.4f);
        dooblrEditor->selectBand (1);
        {
            const auto h = curve->handlePosition (1);
            curve->updateHover (h);
            curve->advanceHoverAnimation (0.2);
            const auto onS = curve->soloBadgeBounds (1).getCentre();
            curve->updateHover (onS);
            check (curve->hitAt (onS) == Curve::Hit::soloBadge, "snapshot setup: the press lands on band 2's S");
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            for (int i = 1; i <= 8; ++i)
                curve->mouseDrag (makeMouseEvent (*curve, onS.translated (-7.0f * (float) i, 3.0f * (float) i), onS, 1, true));
            curve->advanceHoverAnimation (0.2);
            check (processor.getSoloAudition() == 1 && soloOn (0) && ! soloOn (1), "snapshot: band 2 held mid S-drag, auditioned");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.4-sdrag");
            curve->mouseUp (makeMouseEvent (*curve, onS.translated (-56.0f, 24.0f), onS, 1, true));
            check (processor.getSoloAudition() < 0, "snapshot: released");
        }

        // Closing the editor mid S-drag never leaves the audition on.
        {
            const auto onS = curve->soloBadgeBounds (1).getCentre();
            curve->updateHover (curve->handlePosition (1));
            curve->advanceHoverAnimation (0.2);
            curve->updateHover (onS);
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            curve->mouseDrag (makeMouseEvent (*curve, onS.translated (20.0f, 0.0f), onS, 1, true));
            check (processor.getSoloAudition() == 1, "test setup: mid S-drag");
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            check (processor.getSoloAudition() < 0, "deleting the editor mid S-drag ends the audition");
        }
    }

    //==============================================================================
    // 0.9.5: the Solo highlight tracks what Solo plays.
    using SoloCurve = dooblrq::ui::ResponseCurveComponent;

    int soloColumnAt (const SoloCurve& curve, double frequency)
    {
        const auto& h = curve.getSoloHighlight();
        return juce::jlimit (0, std::max (0, h.columns - 1),
                             (int) std::floor ((curve.frequencyToX (frequency) - h.left) / (float) SoloCurve::soloColumnWidth));
    }

    /** Max |drawn - stage| over every column of every band, against the stage's own view. */
    double soloHighlightError (SoloCurve& curve, DooblrQAudioProcessor& processor, bool& regionsMatch)
    {
        const auto view = processor.getSoloView();
        curve.computeSoloHighlight();
        const auto& h = curve.getSoloHighlight();
        double error = std::abs ((double) h.mix - (double) view.mix);
        regionsMatch = std::abs (h.sampleRate - view.sampleRate) < 1.0e-9;

        for (size_t b = 0; b < (size_t) params::numBands; ++b)
        {
            const auto& vb = view.bands[b];
            error = std::max (error, std::abs ((double) h.weight[b] - (double) vb.weight));
            if (vb.weight <= 0.0f)
                continue;
            regionsMatch = regionsMatch && h.region[b].shape == vb.region.shape && h.region[b].fromInput == vb.region.fromInput
                           && std::abs (h.region[b].frequency - vb.region.frequency) < 1.0e-9 * vb.region.frequency
                           && std::abs (h.region[b].q - vb.region.q) < 1.0e-9;
            for (int c = 0; c < h.columns; ++c)
            {
                const auto f = curve.xToFrequency (h.columnCentre (c));
                const auto expected = (double) vb.weight * dsp::soloMagnitudeAt (vb.region, f, view.sampleRate);
                error = std::max (error, std::abs ((double) h.level[b][(size_t) c] - expected));
            }
        }
        return error;
    }

    juce::AudioBuffer<float> processSilenceOrNoise (DooblrQAudioProcessor& processor, int samples, int block = 256, float noise = 0.0f)
    {
        juce::AudioBuffer<float> buffer (2, samples);
        buffer.clear();
        if (noise > 0.0f)
            fillWithNoise (buffer, 5, noise);
        processInBlocks (processor, buffer, { block });
        return buffer;
    }

    float pixelLuma (const juce::Image& image, int x, int y)
    {
        const auto c = image.getPixelAt (x, y);
        return 0.2126f * c.getRed() + 0.7152f * c.getGreen() + 0.0722f * c.getBlue();
    }

    /** Mean luma of a curve-image column over the plot's top quarter (clear of the 0 dB line). */
    float columnLuma (const juce::Image& image, const SoloCurve& curve, double frequency)
    {
        const auto area = curve.plotArea();
        const auto x = (int) std::floor (curve.frequencyToX (frequency));
        float sum = 0.0f;
        int n = 0;
        for (int y = (int) area.getY() + 2; y < (int) (area.getY() + area.getHeight() * 0.25f); ++y, ++n)
            sum += pixelLuma (image, x, y);
        return n > 0 ? sum / (float) n : 0.0f;
    }

    void testSoloGradient()
    {
        using T = dsp::FilterType;
        constexpr double sr = 48000.0;

        // --- 1. Per type, the drawn highlight against what Solo actually plays (sines through
        //        the processor): bell / notch / shelves / tilt = solo vs the EQ'd output, cuts
        //        = solo vs the input (what the cut removes).
        struct Case { const char* name; T type; float frequency, gain, q; };
        const Case cases[] {
            { "bell +6 dB Q 2", T::bell, 1000.0f, 6.0f, 2.0f },
            { "bell -4 dB Q 0.7", T::bell, 1000.0f, -4.0f, 0.7f },
            { "notch Q 4", T::notch, 1000.0f, 0.0f, 4.0f },
            { "low shelf +5", T::lowShelf, 1000.0f, 5.0f, 0.707f },
            { "high shelf -5", T::highShelf, 1000.0f, -5.0f, 0.707f },
            { "tilt +4", T::tilt, 1000.0f, 4.0f, 0.707f },
            { "tilt -4", T::tilt, 1000.0f, -4.0f, 0.707f },
            { "low cut", T::lowCut, 1000.0f, 0.0f, 0.707f },
            { "high cut", T::highCut, 1000.0f, 0.0f, 0.707f },
        };
        const double probes[] { 60.0, 250.0, 600.0, 850.0, 1000.0, 1200.0, 1700.0, 4000.0, 15000.0 };

        for (const auto& k : cases)
        {
            const auto cut = k.type == T::lowCut || k.type == T::highCut;
            DooblrQAudioProcessor processor;
            configureBell (processor, 0, k.frequency, k.gain, k.q);
            setBand (processor, 0, params::band::type, (float) k.type);
            for (int b = 1; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            check (curve != nullptr, std::string (k.name) + ": curve found");
            if (curve == nullptr)
                return;

            double worst = 0.0;
            std::string row = std::string ("highlight vs audio, ") + k.name + " (drawn / heard dB):";
            for (const auto f : probes)
            {
                if (k.type == T::notch && std::abs (f - 1000.0) < 1.0)
                    continue; // the notch's own centre is silent in both: no ratio
                setBand (processor, 0, params::band::solo, 0.0f);
                processor.reset();
                const auto reference = cut ? 0.0 : measureSineGainDb (processor, f, 0.25f, sr, 0.25);
                setBand (processor, 0, params::band::solo, 1.0f);
                processor.reset();
                const auto soloed = measureSineGainDb (processor, f, 0.25f, sr, 0.25);
                const auto heard = soloed - reference;

                bool regionsOk = false;
                const auto consistency = soloHighlightError (*curve, processor, regionsOk);
                check (consistency < 1.0e-6 && regionsOk && processor.isSoloViewLive(),
                       std::string (k.name) + ": every drawn column is the stage's weight * |H| (live)");
                const auto& h = curve->getSoloHighlight();
                // The drawn level at this frequency (column containing it, nearest centre).
                const auto column = soloColumnAt (*curve, f);
                const auto drawn = 20.0 * std::log10 (std::max (1.0e-7, (double) h.level[0][(size_t) column]));
                row += " " + format (f, 0) + " Hz " + format (drawn, 1) + "/" + format (heard, 1);
                // Tolerance: a column is 2 px wide, so allow its own slope across 1 px.
                const auto slopeDb = std::abs (20.0 * std::log10 (std::max (1.0e-7, (double) h.level[0][(size_t) std::min (h.columns - 1, column + 1)]))
                                               - 20.0 * std::log10 (std::max (1.0e-7, (double) h.level[0][(size_t) std::max (0, column - 1)]))) * 0.5;
                if (heard > -30.0 || drawn > -30.0)
                    worst = std::max (worst, std::abs (drawn - heard) - slopeDb);
            }
            note (row);
            check (worst < 0.5, std::string (k.name) + ": the highlight equals the audition's response at every probe (within 0.5 dB + 1 px of slope; worst "
                                    + format (worst, 2) + " dB)");

            const auto& h = curve->getSoloHighlight();
            if (k.type == T::lowCut || k.type == T::highCut)
            {
                const auto low = k.type == T::lowCut;
                const auto edge = low ? h.level[0][0] : h.level[0][(size_t) (h.columns - 1)];
                const auto far = low ? h.level[0][(size_t) (h.columns - 1)] : h.level[0][0];
                const auto inner = h.level[0][(size_t) soloColumnAt (*curve, low ? 300.0 : 3300.0)];
                check (edge > 0.995f && inner > 0.98f && far < 1.0e-3f,
                       std::string (k.name) + ": lit to the graph edge on the removed side (" + (low ? "below" : "above")
                           + " the cutoff), not a band around it");
            }
        }

        // --- 2. Fades: the highlight fades in and out with the 20 ms audio fade.
        {
            DooblrQAudioProcessor processor;
            configureBell (processor, 0, 1000.0f, 3.0f, 1.0f);
            for (int b = 1; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr)
                return;
            processSilenceOrNoise (*&processor, 4800);
            curve->computeSoloHighlight();
            check (curve->getSoloHighlight().mix == 0.0f && curve->getSoloHighlight().weight[0] == 0.0f, "fade: nothing lit before Solo");

            setBand (processor, 0, params::band::solo, 1.0f);
            const auto beforeAudio = processor.getSoloView();
            check (beforeAudio.mix == 0.0f && beforeAudio.bands[0].weight == 0.0f,
                   "fade: toggling Solo lights nothing until the audio starts fading (no jump ahead)");
            processSilenceOrNoise (processor, 256, 256, 0.1f);
            check (curve->soloHighlightStale(), "fade: mid fade-in the painted highlight is stale, so the timer / vblank repaint");
            bool ok = false;
            auto error = soloHighlightError (*curve, processor, ok);
            const auto& h = curve->getSoloHighlight();
            const auto expectedWeight = 256.0f / 960.0f;
            const auto centre = h.level[0][(size_t) soloColumnAt (*curve, 1000.0)];
            note ("fade-in after 256 samples (5.3 ms): drawn weight " + format (h.weight[0], 4) + ", mix " + format (h.mix, 4)
                  + ", centre level " + format (centre, 4) + ", stage mix " + format (processor.getSoloMix(), 4)
                  + ", dim alpha " + format (SoloCurve::soloDimAlpha (h.mix), 3));
            check (error < 1.0e-6 && ok && std::abs (h.weight[0] - expectedWeight) < 1.0e-3f && std::abs (h.mix - processor.getSoloMix()) < 1.0e-6f
                       && std::abs (centre - expectedWeight) < 0.01f,
                   "fade-in: weight, dim and every column follow the stage's 20 ms ramp (27% after 5.3 ms)");
            processSilenceOrNoise (processor, 256 * 3, 256, 0.1f);
            curve->computeSoloHighlight();
            check (curve->getSoloHighlight().weight[0] == 1.0f && curve->getSoloHighlight().mix == 1.0f && ! curve->soloHighlightStale(),
                   "fade-in: full after 20 ms, then steady (no further repaints needed)");

            setBand (processor, 0, params::band::solo, 0.0f);
            processSilenceOrNoise (processor, 256, 256, 0.1f);
            error = soloHighlightError (*curve, processor, ok);
            note ("fade-out after 256 samples: drawn weight " + format (h.weight[0], 4) + ", mix " + format (h.mix, 4)
                  + " (Solo already off: heard " + std::string (processor.isBandHeardSolo (0) ? "yes" : "no") + ")");
            check (error < 1.0e-6 && ok && std::abs (h.weight[0] - (1.0f - expectedWeight)) < 1.0e-3f && ! processor.isBandHeardSolo (0),
                   "fade-out: the highlight keeps fading with the audio after Solo is switched off (it used to vanish at once)");
            processSilenceOrNoise (processor, 1024, 256, 0.1f);
            curve->computeSoloHighlight();
            check (curve->getSoloHighlight().weight[0] == 0.0f && curve->getSoloHighlight().mix == 0.0f && ! curve->soloHighlightStale(),
                   "fade-out: nothing lit once the 20 ms fade ends");

            // Pixels: the drawn fill scales with the fade.
            setBand (processor, 0, params::band::type, (float) T::lowCut);
            setBand (processor, 0, params::band::frequency, 500.0f);
            const auto contrast = [&]
            {
                const auto image = curve->createComponentSnapshot (curve->getLocalBounds());
                return columnLuma (image, *curve, 40.0) - columnLuma (image, *curve, 8000.0);
            };
            const auto off = contrast();
            setBand (processor, 0, params::band::solo, 1.0f);
            processSilenceOrNoise (processor, 256, 256, 0.1f);
            const auto partial = contrast();
            processSilenceOrNoise (processor, 1024, 256, 0.1f);
            const auto full = contrast();
            note ("rendered low cut 500 Hz highlight, luma 40 Hz minus 8 kHz (top quarter): off " + format (off, 2) + ", 27% into the fade "
                  + format (partial, 2) + ", full " + format (full, 2));
            check (full > off + 6.0f && partial > off + 0.5f && partial < off + 0.6f * (full - off),
                   "pixels: the removed side is lit on screen, partially mid-fade, fully after it");
        }

        // --- 3. Glide: during the 10 ms glide the highlight is where the audio filter is.
        {
            DooblrQAudioProcessor processor;
            configureBell (processor, 0, 1000.0f, 3.0f, 2.0f);
            setBand (processor, 0, params::band::solo, 1.0f);
            for (int b = 1; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr)
                return;
            processSilenceOrNoise (processor, 4800, 256, 0.1f);
            setBand (processor, 0, params::band::frequency, 4000.0f);
            processSilenceOrNoise (processor, 256, 256, 0.1f);
            bool ok = false;
            const auto error = soloHighlightError (*curve, processor, ok);
            const auto& h = curve->getSoloHighlight();
            const auto glide = 1.0 - std::exp (-16.0 / (0.01 * sr));
            const auto predicted = std::exp (std::log (4000.0) + (std::log (1000.0) - std::log (4000.0)) * std::pow (1.0 - glide, 16.0));
            int peak = 0;
            for (int c = 1; c < h.columns; ++c)
                if (h.level[0][(size_t) c] > h.level[0][(size_t) peak])
                    peak = c;
            const auto peakHz = curve->xToFrequency (h.columnCentre (peak));
            note ("glide 1 -> 4 kHz, 256 samples in: drawn region " + format (h.region[0].frequency, 1) + " Hz (one-pole prediction "
                  + format (predicted, 1) + " Hz, target " + format (processor.getSoloRegion (0).frequency, 0) + " Hz), drawn peak at "
                  + format (peakHz, 0) + " Hz");
            check (error < 1.0e-6 && ok && std::abs (h.region[0].frequency / predicted - 1.0) < 0.01
                       && std::abs (std::log2 (peakHz / h.region[0].frequency)) < 0.03,
                   "glide: the highlight shows the smoothed region the audio is filtering with, not the target");
            check (curve->soloHighlightStale() == false && processor.getSoloView().isMoving (true), "glide: still moving, so repaints continue");
            processSilenceOrNoise (processor, 4800, 256, 0.1f);
            curve->computeSoloHighlight();
            check (std::abs (h.region[0].frequency / 4000.0 - 1.0) < 1.0e-3 && ! processor.getSoloView().isMoving (true) && ! curve->soloHighlightStale(),
                   "glide: settles on the target, then stops repainting");
        }

        // --- 4. Live dynamics move a cut's cutoff: the highlight follows the stage.
        for (const auto type : { T::lowCut, T::highCut })
        {
            const auto label = std::string (type == T::lowCut ? "dynamic low cut" : "dynamic high cut");
            DooblrQAudioProcessor processor;
            configureDynamicCut (processor, 0, type, 1000.0f, dsp::Slope::db24, 12.0f);
            setBand (processor, 0, params::band::solo, 1.0f);
            for (int b = 1; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr)
                return;
            double worstError = 0.0;
            auto allOk = true;
            double minHz = 1.0e9, maxHz = 0.0, worstLag = 0.0;
            juce::AudioBuffer<float> buffer (2, 256);
            for (int block = 0; block < 120; ++block)
            {
                const auto loud = (block / 30) % 2 == 0;
                for (int i = 0; i < 256; ++i)
                {
                    const auto v = (loud ? 0.5f : 0.001f) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (block * 256 + i) / sr);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }
                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);
                bool ok = false;
                worstError = std::max (worstError, soloHighlightError (*curve, processor, ok));
                allOk = allOk && ok;
                const auto drawnHz = curve->getSoloHighlight().region[0].frequency;
                minHz = std::min (minHz, drawnHz);
                maxHz = std::max (maxHz, drawnHz);
                // How far the drawn cutoff is from the live (unsmoothed) cutoff: the glide lag only.
                worstLag = std::max (worstLag, std::abs (std::log2 (drawnHz / processor.getSoloRegion (0).frequency)));
            }
            note (label + ": drawn cutoff ranged " + format (minHz, 0) + " - " + format (maxHz, 0) + " Hz over loud / quiet bursts, worst lag behind the live cutoff "
                  + format (worstLag, 3) + " oct (the 10 ms glide), drawn vs stage error " + format (worstError, 8));
            check (worstError < 1.0e-6 && allOk && maxHz / minHz > 1.5, label + ": the highlight follows the live cutoff exactly as the stage does, block by block");
            check (curve->soloHighlightStale() == false, label + ": (computed this block)");
        }

        // --- 5. Multiple solos, three window sizes, the log mapping.
        {
            DooblrQAudioProcessor processor;
            configureBell (processor, 0, 120.0f, 0.0f, 0.707f);
            setBand (processor, 0, params::band::type, (float) T::lowCut);
            configureBell (processor, 1, 1000.0f, 4.0f, 1.5f);
            configureBell (processor, 2, 8000.0f, 0.0f, 0.707f);
            setBand (processor, 2, params::band::type, (float) T::highCut);
            for (int b = 0; b < 3; ++b)
                setBand (processor, b, params::band::solo, 1.0f);
            for (int b = 3; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr)
                return;
            processSilenceOrNoise (processor, 4800, 256, 0.1f);

            const std::pair<int, int> sizes[] { { DooblrQAudioProcessorEditor::minimumWidth, DooblrQAudioProcessorEditor::minimumHeight },
                                                { DooblrQAudioProcessorEditor::defaultWidth, DooblrQAudioProcessorEditor::defaultHeight },
                                                { DooblrQAudioProcessorEditor::maximumWidth, DooblrQAudioProcessorEditor::maximumHeight } };
            for (const auto& [w, hgt] : sizes)
            {
                editor->setSize (w, hgt);
                const auto label = std::to_string (w) + " x " + std::to_string (hgt);
                bool ok = false;
                const auto error = soloHighlightError (*curve, processor, ok);
                const auto& h = curve->getSoloHighlight();
                const auto area = curve->plotArea();
                const auto pxPerOctave = area.getWidth() / (float) std::log2 (1000.0);
                double worstMapping = 0.0;
                for (const auto f : { 25.0, 100.0, 1000.0, 5000.0, 19000.0 })
                {
                    const auto c = soloColumnAt (*curve, f);
                    worstMapping = std::max (worstMapping, std::abs (std::log2 (curve->xToFrequency (h.columnCentre (c)) / f)) * pxPerOctave);
                }
                const auto litAll = h.weight[0] == 1.0f && h.weight[1] == 1.0f && h.weight[2] == 1.0f;
                note (label + ": " + std::to_string (h.columns) + " columns over " + format (area.getWidth(), 0) + " px, column-centre mapping off by at most "
                      + format (worstMapping, 2) + " px, drawn vs stage " + format (error, 8));
                check (h.columns == (int) std::ceil (area.getWidth() / 2.0f) && h.columns <= SoloCurve::maxSoloColumns && std::abs (h.left - area.getX()) < 1.0e-4f,
                       label + ": columns span the plot exactly");
                check (worstMapping <= 1.0001 && error < 1.0e-6 && ok && litAll,
                       label + ": three solos drawn from the stage, each column at its log-frequency centre (within 1 px)");
                check (h.level[0][0] > 0.99f && h.level[2][(size_t) (h.columns - 1)] > 0.99f
                           && h.level[1][(size_t) soloColumnAt (*curve, 1000.0)] > 0.99f,
                       label + ": low cut lit to the left edge, high cut to the right edge, bell at its centre");
            }

            // Timing and allocations, the largest window, three bands.
            const auto before = allocationCounter::count.load();
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 200; ++i)
                curve->computeSoloHighlight();
            const auto micros = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 200.0;
            const auto allocations = allocationCounter::count.load() - before;
            const auto image = juce::Image (juce::Image::ARGB, curve->getWidth(), curve->getHeight(), true);
            juce::Image target (image);
            juce::Graphics g (target);
            const auto t1 = std::chrono::steady_clock::now();
            for (int i = 0; i < 20; ++i)
                curve->paint (g);
            const auto paintMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t1).count() / 20.0;
            note ("2400 x 1400, three solos: highlight computation " + format (micros, 1) + " us per paint, "
                  + std::to_string (allocations) + " allocations in 200 runs; whole graph paint (software renderer) " + format (paintMs, 2) + " ms");
            check (allocations == 0, "the highlight computation allocates nothing");
            check (micros < 2000.0, "the highlight computation is cheap (under 2 ms at the largest size)");
        }

        // --- 6. Mid S-drag: the auditioned band fades in, the soloed one fades out, as heard.
        {
            DooblrQAudioProcessor processor;
            configureBell (processor, 0, 1000.0f, 4.0f, 1.0f);
            configureBell (processor, 1, 2000.0f, -4.0f, 1.4f);
            for (int b = 2; b < params::numBands; ++b)
                setBand (processor, b, params::band::enabled, 0.0f);
            setBand (processor, 0, params::band::solo, 1.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            auto* dooblrEditor = dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr || dooblrEditor == nullptr)
                return;
            dooblrEditor->selectBand (1);
            processSilenceOrNoise (processor, 4800, 256, 0.1f);
            const auto hp = curve->handlePosition (1);
            curve->updateHover (hp);
            curve->advanceHoverAnimation (0.2);
            const auto onS = curve->soloBadgeBounds (1).getCentre();
            curve->updateHover (onS);
            check (curve->hitAt (onS) == SoloCurve::Hit::soloBadge, "S-drag setup: the press lands on band 2's S");
            curve->mouseDown (makeMouseEvent (*curve, onS, onS, 1, false));
            for (int i = 1; i <= 6; ++i)
                curve->mouseDrag (makeMouseEvent (*curve, onS.translated (-8.0f * (float) i, 2.0f * (float) i), onS, 1, true));
            check (processor.getSoloAudition() == 1, "S-drag: auditioning band 2");
            processSilenceOrNoise (processor, 256, 256, 0.1f);
            bool ok = false;
            auto error = soloHighlightError (*curve, processor, ok);
            const auto& h = curve->getSoloHighlight();
            note ("mid S-drag, 5.3 ms in: band 1 weight " + format (h.weight[0], 3) + " (fading out), band 2 " + format (h.weight[1], 3)
                  + " (fading in), band 2 region " + format (h.region[1].frequency, 0) + " Hz (param " + format (getParameter (processor, params::bandId (1, params::band::frequency)), 0) + " Hz)");
            check (error < 1.0e-6 && ok && h.weight[0] > 0.6f && h.weight[0] < 0.8f && h.weight[1] > 0.2f && h.weight[1] < 0.4f,
                   "mid S-drag: both bands drawn at the weights the audio crossfades with");
            for (int i = 7; i <= 12; ++i)
            {
                curve->mouseDrag (makeMouseEvent (*curve, onS.translated (-8.0f * (float) i, 2.0f * (float) i), onS, 1, true));
                processSilenceOrNoise (processor, 256, 256, 0.1f);
                error = std::max (error, soloHighlightError (*curve, processor, ok));
            }
            // Let the 10 ms glide settle (100 ms = ~10 time constants) before asking for the parameter.
            processSilenceOrNoise (processor, 4800, 256, 0.1f);
            error = std::max (error, soloHighlightError (*curve, processor, ok));
            check (error < 1.0e-6 && ok && h.weight[0] == 0.0f && h.weight[1] == 1.0f
                       && std::abs (std::log2 (h.region[1].frequency / getParameter (processor, params::bandId (1, params::band::frequency)))) < 0.01,
                   "mid S-drag: the moving band's highlight follows the glided audition; the other band has faded out");
            curve->mouseUp (makeMouseEvent (*curve, onS.translated (-96.0f, 24.0f), onS, 1, true));
        }

        // --- 7. No audio running (stopped host / headless): the target state, never stale.
        {
            DooblrQAudioProcessor processor;
            configureDynamicCut (processor, 0, T::highCut, 3000.0f, dsp::Slope::db24, 0.0f, false);
            setBand (processor, 0, params::band::solo, 1.0f);
            prepare (processor, juce::AudioChannelSet::stereo(), sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            editor->setSize (1080, 660);
            auto* curve = dynamic_cast<SoloCurve*> (findById (*editor, "responseCurve"));
            if (curve == nullptr)
                return;
            curve->computeSoloHighlight();
            const auto& h = curve->getSoloHighlight();
            check (! processor.isSoloViewLive() && h.weight[0] == 1.0f && h.mix == 1.0f && h.region[0].shape == dsp::SoloShape::highPass
                       && std::abs (h.region[0].frequency - 3000.0) < 0.01,
                   "no audio: the highlight shows the region Solo will play (full weight)");
            setBand (processor, 0, params::band::frequency, 6000.0f);
            check (curve->soloHighlightStale(), "no audio: an edit marks the highlight stale (repaint)");
            curve->computeSoloHighlight();
            check (std::abs (h.region[0].frequency - 6000.0) < 1.0e-6, "no audio: edits show at once (nothing to glide)");

            // Snapshot for the release: a high cut soloed live, the removed side lit to the edge.
            setBand (processor, 0, params::band::frequency, 2500.0f);
            configureBell (processor, 1, 400.0f, 3.0f, 1.0f);
            processSilenceOrNoise (processor, 4800, 256, 0.1f);
            dynamic_cast<DooblrQAudioProcessorEditor*> (editor.get())->selectBand (0);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.9.5-solo-gradient");
        }
    }

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
        { "band 1/6 cut defaults, 2D drag per type", testDefaultCutsAnd2dDrag },
        { "per-band dynamic regions",               testDynamicRegions },
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
        { "per-band slope (12-60 dB/oct, Brickwall)", testSlopes },
        { "bell / notch slopes (0.7.1)", testBellNotchSlopes },
        { "Digital model matches 0.7.1 exactly",      testDigitalMatches071 },
        { "analogue model shapes",                  testModelShapes },
        { "Stepped switched positions",             testSteppedPositions },
        { "model colour (THD vs level, Drive)",     testModelColour },
        { "models at extremes, latency, FIR, dynamics", testModelExtremesAndModes },
        { "model state, undo, click-free switch",   testModelStateUndoAndSwitch },
        { "model controls in the editor and menu",  testModelUi },
        { "dynamic Low / High Cut (0.9.0)",          testDynamicCuts },
        { "outer Range ring (0.9.0)",                testRangeRing },
        { "Range ring snapshots (0.9.0)",            testRangeRingSnapshots },
        { "band Solo regions (0.9.1)",               testSoloRegions },
        { "band Solo audio (0.9.1)",                 testSoloAudio },
        { "band Solo editor (0.9.1)",                testSoloEditor },
        { "hover Range ring and blue S (0.9.2)",     testHoverRing },
        { "editing while soloed (0.9.3)",           testSoloEditing },
        { "S-drag audition (0.9.4)",                testSoloDrag },
        { "solo gradient (0.9.5)",                  testSoloGradient },
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
