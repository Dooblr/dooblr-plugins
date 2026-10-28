/*
    dooblr-kick tests: run the real processor, DSP, presets, samples and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRKICK_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "UI/Theme.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/KickEngine.h"
#include "DSP/Effects.h"
#include "DSP/ProcessingChain.h"
#include "Samples/SampleBank.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

//==============================================================================
// Counts operator new calls made while a processBlock is being measured (real-time safety).
namespace
{
    std::atomic<bool> countAllocations { false };
    std::atomic<int> allocationCount { 0 };
}

void* operator new (std::size_t size)
{
    if (countAllocations.load (std::memory_order_relaxed))
        allocationCount.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    namespace params = dooblrkick::params;
    namespace dsp = dooblrkick::dsp;
    using Processor = DooblrKickAudioProcessor;
    using Editor = DooblrKickAudioProcessorEditor;

    int failedChecks = 0;
    int passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition)
        {
            ++passedChecks;
            return;
        }

        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n"; }

    std::string format (double value, int decimals = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (decimals) << value;
        return s.str();
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    std::unique_ptr<Processor> makeProcessor (double rate = sampleRate, int block = blockSize)
    {
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails (0, 2, rate, block);
        p->prepareToPlay (rate, block);
        return p;
    }

    juce::RangedAudioParameter* getParam (Processor& p, const juce::String& id)
    {
        auto* param = p.getParameterState().getParameter (id);
        check (param != nullptr, "parameter exists: " + id.toStdString());
        return param;
    }

    void setParam (Processor& p, const juce::String& id, float plainValue)
    {
        if (auto* param = getParam (p, id))
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    float getPlain (Processor& p, const juce::String& id)
    {
        if (auto* param = getParam (p, id))
            return param->convertFrom0to1 (param->getValue());

        return 0.0f;
    }

    void setValues (Processor& p, const dooblrkick::presets::ValueList& values)
    {
        for (const auto& [id, value] : values)
            setParam (p, id, value);
    }

    struct NoteEvent
    {
        int sample;
        juce::MidiMessage message;
    };

    /** Runs processBlock with the events; the buffer is cleared before every block like a host. */
    juce::AudioBuffer<float> renderMidi (Processor& p, const std::vector<NoteEvent>& events, int numSamples,
                                         int block = blockSize, int* allocations = nullptr)
    {
        juce::AudioBuffer<float> out (2, numSamples), scratch (2, block);
        out.clear();

        for (int pos = 0; pos < numSamples; pos += block)
        {
            const auto n = std::min (block, numSamples - pos);
            juce::MidiBuffer midi;

            for (const auto& e : events)
                if (e.sample >= pos && e.sample < pos + n)
                    midi.addEvent (e.message, e.sample - pos);

            scratch.setSize (2, n, false, false, true);
            scratch.clear();

            if (allocations != nullptr)
            {
                allocationCount.store (0);
                countAllocations.store (true);
            }

            p.processBlock (scratch, midi);

            if (allocations != nullptr)
            {
                countAllocations.store (false);
                *allocations += allocationCount.load();
            }

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, scratch, ch, 0, n);
        }

        return out;
    }

    juce::AudioBuffer<float> renderNote (Processor& p, int noteNumber = 36, float velocity = 1.0f,
                                         double seconds = 1.0, int block = blockSize)
    {
        return renderMidi (p, { { 0, juce::MidiMessage::noteOn (1, noteNumber, velocity) } },
                           (int) (seconds * p.getSampleRate()), block);
    }

    float peakOf (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
    {
        end = end < 0 ? b.getNumSamples() : std::min (end, b.getNumSamples());
        float peak = 0.0f;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i)
                peak = std::max (peak, std::abs (b.getSample (ch, i)));

        return peak;
    }

    float rmsOf (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
    {
        end = end < 0 ? b.getNumSamples() : std::min (end, b.getNumSamples());
        double sum = 0.0;
        int count = 0;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i, ++count)
                sum += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);

        return count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;

        return true;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float diff = 0.0f;
        const auto n = std::min (a.getNumSamples(), b.getNumSamples());

        for (int ch = 0; ch < std::min (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = 0; i < n; ++i)
                diff = std::max (diff, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

        return diff + (a.getNumSamples() == b.getNumSamples() ? 0.0f : 1.0f);
    }

    /** Positive-going zero crossings (interpolated, in samples) of channel 0 within [start, end). */
    std::vector<double> zeroCrossings (const juce::AudioBuffer<float>& b, int start, int end)
    {
        std::vector<double> crossings;
        const auto* x = b.getReadPointer (0);

        for (int i = std::max (1, start); i < std::min (end, b.getNumSamples()); ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
                crossings.push_back ((double) (i - 1) + (double) (-x[i - 1] / (x[i] - x[i - 1])));

        return crossings;
    }

    /** Mean frequency from the zero crossings in [startSeconds, endSeconds). */
    double measureFrequency (const juce::AudioBuffer<float>& b, double rate, double startSeconds, double endSeconds)
    {
        const auto c = zeroCrossings (b, (int) (startSeconds * rate), (int) (endSeconds * rate));

        if (c.size() < 3)
            return 0.0;

        return rate * (double) (c.size() - 1) / (c.back() - c.front());
    }

    /** Leaves only one layer on; drive, EQ, dynamics and the limiter off. */
    void isolateLayer (Processor& p, int layer)
    {
        for (int l = 0; l < params::numLayers; ++l)
        {
            setParam (p, params::layerId (l, "on"), l == layer ? 1.0f : 0.0f);
            setParam (p, params::layerId (l, "solo"), 0.0f);
            setParam (p, params::layerId (l, "start"), 0.0f);
            setParam (p, params::layerId (l, "ftype"), 0.0f);
        }

        for (const auto* id : { "dist_on", "eq_on", "shape_on", "comp_on", "lim_mode" })
            setParam (p, id, 0.0f);
    }

    /** Flat body pitch envelope at hz and a flat full-level amp envelope (2 nodes each). */
    void flatBody (Processor& p, float hz, float lengthMs = 1500.0f)
    {
        using params::Envelope;
        setParam (p, params::envelopeCountId (Envelope::pitch), 2.0f);
        setParam (p, params::nodeValueId (Envelope::pitch, 0), hz);
        setParam (p, params::nodeValueId (Envelope::pitch, 1), hz);
        setParam (p, params::nodeTimeId (Envelope::pitch, 1), 100.0f);
        setParam (p, params::envelopeCountId (Envelope::amp), 2.0f);
        setParam (p, params::nodeValueId (Envelope::amp, 0), 1.0f);
        setParam (p, params::nodeValueId (Envelope::amp, 1), 1.0f);
        setParam (p, params::nodeCurveId (Envelope::amp, 0), 0.0f);
        setParam (p, params::nodeTimeId (Envelope::amp, 1), lengthMs);
        setParam (p, "body_wave", 0.0f);
        setParam (p, "body_harm", 0.0f);
        setParam (p, "body_drive", 0.0f);
        setParam (p, "body_tune", 0.0f);
        setParam (p, "body_source", 0.0f);
    }

    /** v0.3.6: writes an envelope's points (count and every node's time / value / curve). */
    struct EnvNode { float timeMs, value, curve; };
    void setEnvelopeNodes (Processor& p, params::Envelope e, std::initializer_list<EnvNode> nodes)
    {
        setParam (p, params::envelopeCountId (e), (float) nodes.size());
        int n = 0;

        for (const auto& node : nodes)
        {
            if (n > 0) setParam (p, params::nodeTimeId (e, n), node.timeMs);
            setParam (p, params::nodeValueId (e, n), node.value);
            if (n < params::maxNodes - 1) setParam (p, params::nodeCurveId (e, n), node.curve);
            ++n;
        }
    }

    /** The old Attack / Hold / Decay / Curve of a layer (Sub, Tail, Noise) as its amp envelope. */
    void setLayerAhd (Processor& p, int layer, float attackMs, float holdMs, float decayMs, float curve = 0.5f)
    {
        setEnvelopeNodes (p, params::layerEnvelope (layer, true),
                          { { 0, 0, 0 }, { attackMs, 1, 0 }, { attackMs + holdMs, 1, curve }, { attackMs + holdMs + decayMs, 0, 0 } });
    }

    /** The old Click Length: full level falling linearly to silence at lengthMs. */
    void setClickLength (Processor& p, float lengthMs)
    {
        setEnvelopeNodes (p, params::Envelope::clickAmp, { { 0, 1, 0 }, { lengthMs, 0, 0 } });
    }

    /** The end time of a layer's amp envelope (its last point). */
    float ampEndMs (Processor& p, int layer)
    {
        const auto e = params::layerEnvelope (layer, true);
        const auto count = juce::roundToInt (getPlain (p, params::envelopeCountId (e)));
        return getPlain (p, params::nodeTimeId (e, count - 1));
    }

    juce::File makeTempFile (const juce::String& extension)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("dooblr-kick-test", extension, false);
    }

    /** Writes a decaying mono sine as a 24-bit WAV. */
    bool writeTestWav (const juce::File& file, double rate, double hz, double seconds)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();

        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::OutputStream> out (stream.release());
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (rate)
                                                     .withNumChannels (1)
                                                     .withBitsPerSample (24));

        if (writer == nullptr)
            return false;

        const auto n = (int) (rate * seconds);
        juce::AudioBuffer<float> b (1, n);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (double) i / rate;
            b.setSample (0, i, (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t) * std::exp (-4.0 * t)));
        }

        return writer->writeFromAudioSampleBuffer (b, 0, n);
    }

    /** Writes a constant-level mono sine (sharp end, so its length is measurable). */
    bool writeSteadyWav (const juce::File& file, double rate, double hz, double seconds)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();

        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::OutputStream> out (stream.release());
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (rate)
                                                     .withNumChannels (1)
                                                     .withBitsPerSample (24));

        if (writer == nullptr)
            return false;

        const auto n = (int) (rate * seconds);
        juce::AudioBuffer<float> b (1, n);

        for (int i = 0; i < n; ++i)
            b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate)));

        return writer->writeFromAudioSampleBuffer (b, 0, n);
    }

    /** Index of the last sample above the threshold (0 if none). */
    int lastAudible (const juce::AudioBuffer<float>& b, float threshold = 1.0e-3f)
    {
        for (int i = b.getNumSamples() - 1; i > 0; --i)
            if (std::abs (b.getSample (0, i)) > threshold)
                return i;
        return 0;
    }

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
        component.mouseDown (makeMouseEvent (component, position, position, 1, false));
        component.mouseUp (makeMouseEvent (component, position, position, 1, false));
        component.mouseDoubleClick (makeMouseEvent (component, position, position, 2, false));
    }

    void collectComponents (juce::Component& parent, std::map<juce::String, juce::Component*>& out, bool visibleOnly)
    {
        for (auto* child : parent.getChildren())
        {
            if (visibleOnly && ! child->isVisible())
                continue;

            if (child->getComponentID().isNotEmpty())
                out[child->getComponentID()] = child;

            collectComponents (*child, out, visibleOnly);
        }
    }

    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->getComponentID() == id)
                return child;
            if (auto* found = findById (*child, id))
                return found;
        }
        return nullptr;
    }

    /** The tab power toggles (v0.3.1) and the parameters each one switches. */
    const std::vector<std::pair<juce::String, juce::StringArray>>& processorPowerParameters()
    {
        static const std::vector<std::pair<juce::String, juce::StringArray>> map {
            { "power_drive", { "dist_on" } }, { "power_eq", { "eq_on" } },
            { "power_dynamics", { "shape_on", "comp_on" } }, { "power_limiter", { "lim_mode" } } };
        return map;
    }

    juce::StringArray allParameterIds (Processor& p)
    {
        juce::StringArray ids;

        for (auto* param : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                ids.add (withId->paramID);

        return ids;
    }

    /** Which envelope (and node) an envelope parameter id belongs to; node is -1 for a count. */
    struct EnvelopeParamInfo { bool isEnvelope = false; params::Envelope envelope {}; int node = -1; bool isCurve = false; };
    EnvelopeParamInfo envelopeParamInfo (const juce::String& id)
    {
        for (int i = 0; i < params::numEnvelopes; ++i)
        {
            const auto e = (params::Envelope) i;
            if (id == params::envelopeCountId (e))
                return { true, e, -1, false };

            for (int node = 0; node < params::maxNodes; ++node)
            {
                if (id == params::nodeValueId (e, node) || (node > 0 && id == params::nodeTimeId (e, node)))
                    return { true, e, node, false };
                if (node < params::maxNodes - 1 && id == params::nodeCurveId (e, node))
                    return { true, e, node, true };
            }
        }

        return {};
    }

    bool isEnvelopeParam (const juce::String& id) { return envelopeParamInfo (id).isEnvelope; }

    void saveSnapshot (const juce::Image& image, const juce::String& suffix)
    {
        const auto* prefix = std::getenv ("DOOBLRKICK_EDITOR_SNAPSHOT");

        if (prefix == nullptr || *prefix == 0)
            return;

        const juce::File file (juce::String (prefix) + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            note ("snapshot: " + file.getFullPathName().toStdString());
        else
            check (false, "could not write snapshot " + file.getFullPathName().toStdString());
    }

    //==============================================================================
    void testInstantiationAndMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-kick", "name is dooblr-kick (got " + p->getName().toStdString() + ")");
        check (p->acceptsMidi(), "accepts MIDI");
        check (! p->producesMidi() && ! p->isMidiEffect(), "not a MIDI effect");
        check (p->hasEditor(), "has an editor");
        check (p->getTotalNumInputChannels() == 0, "no audio input");
        check (p->getTotalNumOutputChannels() == 2, "stereo output");
        check (juce::String (JucePlugin_VersionString) == DOOBLRKICK_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.3.6", "version 0.3.6");
        check (JucePlugin_IsSynth == 1, "built as a synth / instrument");

        juce::AudioProcessor::BusesLayout stereo, mono, withInput;
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        withInput.inputBuses.add (juce::AudioChannelSet::stereo());
        withInput.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p->checkBusesLayoutSupported (stereo), "stereo out supported");
        check (! p->checkBusesLayoutSupported (mono), "mono out rejected");
        check (! p->checkBusesLayoutSupported (withInput), "audio input rejected");

        check (p->getNumPrograms() == 7, "7 host programs (Init + 6 factory kicks)");
        const juce::StringArray expected { "Init", "Hardstyle", "Techno Rumble", "Dubstep Punch", "House Thump", "Trap 808-ish", "DnB" };

        for (int i = 0; i < expected.size(); ++i)
            check (p->getProgramName (i) == expected[i], "program " + std::to_string (i) + " is " + expected[i].toStdString()
                                                            + " (got " + p->getProgramName (i).toStdString() + ")");

        note (std::to_string (p->getParameters().size()) + " parameters");
    }

    void testNoteOnProducesAudio()
    {
        auto p = makeProcessor();
        const auto before = renderMidi (*p, {}, 4096);
        check (peakOf (before) == 0.0f, "silent before any note");

        const auto hit = renderNote (*p, 36, 1.0f, 1.0);
        const auto peak = peakOf (hit);
        check (allFinite (hit), "note-on output is finite");
        check (peak > 0.2f, "note-on output is loud (peak " + format (peak, 3) + ")");
        check (peak <= std::pow (10.0f, -0.3f / 20.0f) + 0.01f, "default limiter holds the -0.3 dB ceiling (peak " + format (peak, 4) + ")");
        check (rmsOf (hit, 0, 4800) > 0.05f, "energy in the first 100 ms");

        // Sample-accurate start: a note at sample 300 of a block leaves 0..299 silent.
        auto q = makeProcessor();
        const auto offset = renderMidi (*q, { { 300, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 2048);
        check (peakOf (offset, 0, 300) == 0.0f && peakOf (offset, 300, 2048) > 0.01f, "the hit starts at the MIDI event's sample offset");

        // Different sample rates and block sizes.
        for (const auto rate : { 44100.0, 96000.0 })
        {
            auto r = makeProcessor (rate, 256);
            const auto b = renderNote (*r, 36, 1.0f, 0.5, 256);
            check (allFinite (b) && peakOf (b) > 0.2f, "non-silent and finite at " + format (rate, 0) + " Hz");
        }

        float referencePeak = 0.0f;

        for (const auto block : { 512, 1, 17, 64, 2048 })
        {
            auto r = makeProcessor (sampleRate, block);
            const auto b = renderNote (*r, 36, 1.0f, 0.5, block);
            const auto pk = peakOf (b);

            if (block == 512)
                referencePeak = pk;

            check (allFinite (b) && std::abs (juce::Decibels::gainToDecibels (pk) - juce::Decibels::gainToDecibels (referencePeak)) < 0.5f,
                   "block size " + std::to_string (block) + " gives the same level (" + format (pk, 3) + " vs " + format (referencePeak, 3) + ")");
        }

        // Zero-length block is harmless.
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        p->processBlock (empty, midi);
        check (true, "zero-length block accepted");
    }

    void testSynthIgnoresMidiNote()
    {
        const int notes[] = { 0, 24, 29, 36, 48, 60, 72, 96, 127 };

        // Every synthesised layer, every factory preset (all synth), the default patch with the
        // tail and noise switched on: the same kick whatever key is played.
        // Each note starts from a quiet engine, so the previous hit's choke fade does not overlap it.
        auto renderAt = [] (Processor& p, int noteNumber) { p.releaseResources(); return renderNote (p, noteNumber, 1.0f, 0.8); };
        int identical = 0, compared = 0;

        auto compareNotes = [&] (Processor& p, const std::string& name)
        {
            const auto reference = renderAt (p, notes[0]);
            check (peakOf (reference) > 1.0e-3f && allFinite (reference), name + ": renders");

            for (const auto n : notes)
            {
                const auto difference = maxDifference (renderAt (p, n), reference);
                ++compared;
                if (difference < 1.0e-6f)
                    ++identical;
                else
                    note ("  " + name + " at note " + std::to_string (n) + " differs by " + format (difference, 6));
            }
        };

        for (int layer = 0; layer < params::numLayers; ++layer)
        {
            auto p = makeProcessor();
            isolateLayer (*p, layer);
            setParam (*p, params::layerId (layer, "level"), 0.0f);
            setLayerAhd (*p, dsp::noiseLayer, 0.5f, 5.0f, 400.0f, 0.6f);
            compareNotes (*p, std::string (params::layerNames[(size_t) layer]) + " synth");
        }

        for (int i = 0; i < (int) dooblrkick::presets::getFactoryPresets().size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);
            compareNotes (*p, p->getPresetName().toStdString());
        }

        {
            // v0.3.5: Key Track is an opt-in exception (tested in testKeyTrackSwitches); with
            // every switch off (the default) all synth layers together ignore the note.
            auto p = makeProcessor();
            setParam (*p, "tail_on", 1.0f);
            setParam (*p, "noise_on", 1.0f);
            for (int l = 0; l < params::numLayers; ++l)
                setParam (*p, params::layerId (l, "keytrack"), 0.0f);
            compareNotes (*p, "all synth layers, key track switches off");
        }

        check (identical == compared, "synth output is sample-identical across MIDI notes 0..127 ("
                                          + std::to_string (identical) + "/" + std::to_string (compared) + " renders)");

        // Velocity still applies.
        auto v = makeProcessor();
        setParam (*v, "vel_sens", 1.0f);
        check (peakOf (renderNote (*v, 60, 0.4f)) < 0.8f * peakOf (renderNote (*v, 60, 1.0f)), "velocity still changes the level");

        // Body: the fundamental follows Tune, never the note.
        for (const auto tune : { -12.0f, 0.0f, 7.0f, 12.0f })
        {
            const auto expected = 60.0 * std::exp2 (tune / 12.0);
            for (const auto noteNumber : { 24, 29, 41, 60 })
            {
                auto p = makeProcessor();
                isolateLayer (*p, dsp::bodyLayer);
                flatBody (*p, 60.0f);
                setParam (*p, "body_tune", tune);
                const auto measured = measureFrequency (renderNote (*p, noteNumber, 1.0f, 0.8), sampleRate, 0.1, 0.6);
                check (std::abs (measured / expected - 1.0) < 0.005,
                       "body tune " + format (tune, 0) + " st at note " + std::to_string (noteNumber) + ": " + format (measured)
                           + " Hz, expected " + format (expected));
            }
        }

        // Sub: a sine at Key + Tune + Fine, whatever note is played.
        for (const auto [key, tune, fine] : { std::tuple { 29, 0.0f, 0.0f }, std::tuple { 29, 12.0f, 0.0f },
                                              std::tuple { 41, 0.0f, 0.0f }, std::tuple { 29, -5.0f, 50.0f } })
        {
            const auto expected = 440.0 * std::exp2 ((key + tune + fine / 100.0 - 69.0) / 12.0);
            for (const auto noteNumber : { 29, 36, 48 })
            {
                auto p = makeProcessor();
                isolateLayer (*p, dsp::subLayer);
                setParam (*p, "sub_level", 0.0f);
                setLayerAhd (*p, dsp::subLayer, 1.0f, 800.0f, 350.0f, 0.4f);
                setParam (*p, "sub_envamt", 0.0f);
                setParam (*p, "root_note", (float) key);
                setParam (*p, "sub_tune", tune);
                setParam (*p, "sub_fine", fine);
                const auto measured = measureFrequency (renderNote (*p, noteNumber, 1.0f, 0.8), sampleRate, 0.1, 0.6);
                check (std::abs (measured / expected - 1.0) < 0.005,
                       "sub (key " + std::to_string (key) + ", " + format (tune, 0) + " st, " + format (fine, 0) + " ct) at note "
                           + std::to_string (noteNumber) + ": " + format (measured) + " Hz, expected " + format (expected));
            }
        }

        // Sample layers: key tracking is opt-in (default off) and relative to the Key.
        const auto file = makeTempFile (".wav");
        check (writeSteadyWav (file, 48000.0, 220.0, 0.5), "steady test WAV written");
        for (const auto keyTrack : { false, true })
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 60.0f, 3000.0f);
            juce::String error;
            check (p->loadUserSample (dsp::bodyLayer, file, error), "user sample loads");
            setParam (*p, "body_source", 1.0f);
            setParam (*p, "body_sample", (float) params::userSampleChoice);
            setParam (*p, "body_level", 0.0f);
            if (keyTrack)
                setParam (*p, "body_keytrack", 1.0f);
            const auto atKey = measureFrequency (renderNote (*p, 29, 1.0f, 0.5), sampleRate, 0.05, 0.2);
            const auto up = measureFrequency (renderNote (*p, 41, 1.0f, 0.5), sampleRate, 0.05, 0.2);
            check (std::abs (atKey / 220.0 - 1.0) < 0.005, "body sample at the Key plays at its own pitch (" + format (atKey) + " Hz)");
            check (std::abs (up / (keyTrack ? 440.0 : 220.0) - 1.0) < 0.005,
                   std::string (keyTrack ? "sample key track on: " : "sample key track off (default): ") + "note +12 plays at "
                       + format (up) + " Hz");
        }
        file.deleteFile();
    }

    void testSampleTuneRange()
    {
        auto probe = makeProcessor();
        for (const auto* id : { "click_pitch", "body_tune", "tail_tune", "noise_tune" })
        {
            const auto& range = getParam (*probe, id)->getNormalisableRange();
            check (std::abs (range.start + 48.0f) < 1.0e-6f && std::abs (range.end - 24.0f) < 1.0e-6f,
                   std::string (id) + " spans -48..+24 st");
        }

        // Display and typing: semitones and cents.
        auto* tune = getParam (*probe, "body_tune");
        const auto text = [&] (float st) { return tune->getText (tune->convertTo0to1 (st), 32); };
        check (text (0.0f) == "0 st", "0 st displays as \"0 st\" (" + text (0.0f).toStdString() + ")");
        check (text (-48.0f) == juce::String ("-48 st"), "-48 displays as \"-48 st\" (" + text (-48.0f).toStdString() + ")");
        check (text (7.0f) == "+7 st", "+7 displays as \"+7 st\" (" + text (7.0f).toStdString() + ")");
        check (text (-12.25f) == "-12 st -25 ct", "-12.25 displays as \"-12 st -25 ct\" (" + text (-12.25f).toStdString() + ")");
        check (text (3.5f).contains ("ct"), "fractional tunes show cents (" + text (3.5f).toStdString() + ")");
        int roundTrips = 0, tried = 0;
        for (float st = -48.0f; st <= 24.0f; st += 0.37f)
        {
            ++tried;
            const auto back = tune->convertFrom0to1 (tune->getValueForText (text (st)));
            if (std::abs (back - st) <= 0.005f + 1.0e-4f)
                ++roundTrips;
        }
        check (roundTrips == tried, "text round-trips to the cent (" + std::to_string (roundTrips) + "/" + std::to_string (tried) + ")");
        const auto typed = [&] (const char* t) { return tune->convertFrom0to1 (tune->getValueForText (t)); };
        check (std::abs (typed ("-30") + 30.0f) < 1.0e-3f && std::abs (typed ("-7 st 50 ct") + 6.5f) < 1.0e-3f
                   && std::abs (typed ("12.5 st") - 12.5f) < 1.0e-3f && std::abs (typed ("-99") + 48.0f) < 1.0e-3f,
               "typed values parse (-30, -7 st 50 ct, 12.5 st, -99 clamps to -48)");

        // -48 st: 16x slower, pitch and length, finite, not truncated (live, preview and export).
        const auto file = makeTempFile (".wav");
        check (writeSteadyWav (file, 48000.0, 440.0, 0.1), "0.1 s 440 Hz test WAV written");

        for (const auto layer : { dsp::bodyLayer, dsp::tailLayer })
        {
            const auto name = std::string (params::layerNames[(size_t) layer]);
            auto p = makeProcessor();
            isolateLayer (*p, layer);
            if (layer == dsp::bodyLayer)
                flatBody (*p, 60.0f, 3000.0f); // a 3 s flat amp envelope: longer than the stretched sample (sets synth source, so first)
            juce::String error;
            check (p->loadUserSample (layer, file, error), name + ": user sample loads (" + error.toStdString() + ")");
            setParam (*p, params::layerId (layer, "source"), 1.0f);
            setParam (*p, params::layerId (layer, "sample"), (float) params::userSampleChoice);
            setParam (*p, params::layerId (layer, "level"), 0.0f);

            if (layer != dsp::bodyLayer)
            {
                setLayerAhd (*p, dsp::tailLayer, 0.0f, 2500.0f, 5.0f);
                setParam (*p, "tail_start", 0.0f);
                setParam (*p, "tail_ftype", 0.0f);
                setParam (*p, "tail_spread", 0.0f);
            }

            auto measure = [&] (float tuneSt, const juce::AudioBuffer<float>& b, const std::string& where)
            {
                const auto ratio = std::exp2 (tuneSt / 12.0);
                const auto expectedHz = 440.0 * ratio, expectedLength = 0.1 * 48000.0 / ratio;
                const auto length = (double) lastAudible (b, 0.02f);
                const auto hz = measureFrequency (b, sampleRate, 0.1 * expectedLength / 48000.0, 0.9 * expectedLength / 48000.0);
                check (allFinite (b), name + " " + format (tuneSt, 0) + " st " + where + ": finite");
                check (std::abs (hz / expectedHz - 1.0) < 0.01, name + " " + format (tuneSt, 0) + " st " + where + ": " + format (hz, 3)
                                                                    + " Hz, expected " + format (expectedHz, 3));
                check (std::abs (length / expectedLength - 1.0) < 0.01, name + " " + format (tuneSt, 0) + " st " + where
                                                                            + ": plays " + format (length / 48000.0, 3) + " s, expected "
                                                                            + format (expectedLength / 48000.0, 3));
                const auto level = rmsOf (b, (int) (0.4 * expectedLength), (int) (0.6 * expectedLength));
                check (level > 0.1f, name + " " + format (tuneSt, 0) + " st " + where + ": full level mid-sample (rms " + format (level, 3) + ")");
                return std::pair { hz, length };
            };

            setParam (*p, params::layerId (layer, "tune"), 0.0f);
            const auto [hz0, len0] = measure (0.0f, renderNote (*p, 29, 1.0f, 2.5), "live");
            setParam (*p, params::layerId (layer, "tune"), -48.0f);
            const auto [hz48, len48] = measure (-48.0f, renderNote (*p, 29, 1.0f, 2.5), "live");
            check (std::abs (hz0 / hz48 - 16.0) < 0.16 && std::abs (len48 / len0 - 16.0) < 0.16,
                   name + ": -48 st is 16x lower and 16x longer (pitch x" + format (hz0 / hz48, 3) + ", length x" + format (len48 / len0, 3) + ")");

            const auto preview = p->renderPreview (48000.0);
            measure (-48.0f, preview, "preview");
            const auto exported = makeTempFile (".wav");
            check (p->exportWav (exported, 48000.0, error), name + ": -48 st export succeeds");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (exported));
            if (reader != nullptr)
            {
                juce::AudioBuffer<float> b (2, (int) reader->lengthInSamples);
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                measure (-48.0f, b, "export");
            }
            exported.deleteFile();
            p->clearUserSample (layer);
        }

        // A factory sample at -48 st: finite and audible.
        auto f = makeProcessor();
        isolateLayer (*f, dsp::bodyLayer);
        flatBody (*f, 60.0f, 3000.0f);
        setParam (*f, "body_source", 1.0f);
        setParam (*f, "body_level", 0.0f);
        setParam (*f, "body_tune", -48.0f);
        const auto slow = renderNote (*f, 29, 1.0f, 3.0);
        check (allFinite (slow) && peakOf (slow) > 1.0e-2f, "factory body sample at -48 st is finite and audible");
        file.deleteFile();
    }

    void testStateMigration()
    {
        // A v0.1.0 state: written by this build, then marked as version 1 with the old
        // defaults (key tracking on for the synth body and tail).
        auto p = makeProcessor();
        p->loadFactoryPreset (5, false);
        setParam (*p, "body_level", -3.5f);
        setParam (*p, "body_tune", -7.25f);
        setParam (*p, "tail_on", 1.0f);
        setParam (*p, "root_note", 33.0f);
        juce::MemoryBlock state;
        p->getStateInformation (state);

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr, "state is XML");
        if (xml == nullptr)
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        check (tree.hasProperty ("stateVersion") || tree.hasProperty ("version"), "state carries a version");
        const auto versionKey = tree.hasProperty ("stateVersion") ? "stateVersion" : "version";
        check ((int) tree.getProperty (versionKey) == 6, "state version is 6");
        tree.setProperty (versionKey, 1, nullptr);

        auto setStored = [&] (const juce::String& id, float value)
        {
            for (auto child : tree)
                if (child.getProperty ("id").toString() == id)
                    child.setProperty ("value", value, nullptr);
        };
        setStored ("body_keytrack", 1.0f);
        setStored ("tail_keytrack", 1.0f);
        setStored ("click_keytrack", 1.0f);
        // v1 / v2 "User file" was choice 11 (now "Synth Kick Punch"); factory choices are unchanged.
        setStored ("tail_sample", (float) params::legacyUserSampleChoice);
        setStored ("noise_sample", 9.0f);
        // Old parameters that no longer exist are ignored.
        juce::ValueTree glide ("PARAM");
        glide.setProperty ("id", "glide_mode", nullptr).setProperty ("value", 2.0f, nullptr);
        tree.appendChild (glide, nullptr);

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        auto q = makeProcessor();
        q->setStateInformation (old.getData(), (int) old.getSize());
        check (std::abs (getPlain (*q, "body_level") + 3.5f) < 1.0e-3f && std::abs (getPlain (*q, "body_tune") + 7.25f) < 1.0e-3f
                   && std::abs (getPlain (*q, "root_note") - 33.0f) < 0.01f,
               "v1 state: values load unchanged (tune -7.25 st, key 33)");
        check (getPlain (*q, "body_keytrack") < 0.5f && getPlain (*q, "tail_keytrack") < 0.5f && getPlain (*q, "click_keytrack") < 0.5f,
               "v1 state: key tracking of synth layers is cleared");
        check (q->getPresetName() == p->getPresetName(), "v1 state: preset name kept");
        check (std::abs (getPlain (*q, "tail_sample") - (float) params::userSampleChoice) < 0.01f,
               "v1 state: the old User file choice (11) moves to the new User file index (21)");
        check (std::abs (getPlain (*q, "noise_sample") - 9.0f) < 0.01f, "v1 state: factory sample choice 9 (Hand Clap) is unchanged");
        check (maxDifference (renderNote (*q, 29, 1.0f, 1.0), renderNote (*p, 29, 1.0f, 1.0)) < 1.0e-5f, "v1 state sounds like the saved kick");

        // A sampled layer keeps its key tracking from a v1 state.
        auto r = makeProcessor();
        setParam (*r, "body_source", 1.0f);
        r->getStateInformation (state);
        auto xml2 = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto tree2 = juce::ValueTree::fromXml (*xml2);
        tree2.setProperty (versionKey, 1, nullptr);
        for (auto child : tree2)
            if (child.getProperty ("id").toString() == "body_keytrack")
                child.setProperty ("value", 1.0f, nullptr);
        juce::AudioProcessor::copyXmlToBinary (*tree2.createXml(), old);
        auto t = makeProcessor();
        t->setStateInformation (old.getData(), (int) old.getSize());
        check (getPlain (*t, "body_keytrack") > 0.5f, "v1 state: a sampled body keeps its key tracking");
    }

    void testParameterSmoothing()
    {
        // A held, gated 60 Hz body: a sudden parameter change must not step the waveform.
        auto run = [] (const juce::String& id, float from, float to, float& stepAtChange, float& steadyStep,
                       float& before, float& early, float& after)
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 60.0f, 3000.0f);
            setParam (*p, "body_level", 0.0f);
            setParam (*p, "gate", 1.0f);
            setParam (*p, "lim_mode", 0.0f);
            setParam (*p, id, from);

            const int blocks = 60, changeBlock = 30;
            juce::AudioBuffer<float> out (2, blocks * blockSize), block (2, blockSize);
            for (int i = 0; i < blocks; ++i)
            {
                juce::MidiBuffer midi;
                if (i == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
                if (i == changeBlock)
                    setParam (*p, id, to);
                block.clear();
                p->processBlock (block, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, i * blockSize, block, ch, 0, blockSize);
            }

            const auto change = changeBlock * blockSize;
            auto maxStep = [&] (int a, int b)
            {
                float m = 0.0f;
                for (int i = a + 1; i < b; ++i)
                    m = std::max (m, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                return m;
            };
            stepAtChange = maxStep (change - 4, change + 2400);
            steadyStep = maxStep (change - 4800, change - 4);
            before = rmsOf (out, change - 4800, change);
            early = rmsOf (out, change, change + 48);           // first 1 ms
            after = rmsOf (out, change + 4800, change + 9600);  // 100..200 ms later
        };

        float step, steady, before, early, after;
        run ("output_gain", 0.0f, -24.0f, step, steady, before, early, after);
        check (step <= steady * 1.05f, "output -24 dB jump: no step at the change (max diff " + format (step, 5) + " vs steady " + format (steady, 5) + ")");
        check (early > before * 0.3f && std::abs (juce::Decibels::gainToDecibels (after / before) + 24.0f) < 0.5f,
               "output gain ramps (first ms at " + format (juce::Decibels::gainToDecibels (early / before), 1) + " dB) and settles at "
                   + format (juce::Decibels::gainToDecibels (after / before), 2) + " dB");

        run ("body_level", 0.0f, -30.0f, step, steady, before, early, after);
        check (step <= steady * 1.05f, "body level -30 dB jump: no step (max diff " + format (step, 5) + " vs steady " + format (steady, 5) + ")");
        check (std::abs (juce::Decibels::gainToDecibels (after / before) + 30.0f) < 0.5f, "body level settles at -30 dB");

        run ("body_tune", 0.0f, 12.0f, step, steady, before, early, after);
        check (step <= steady * 2.2f, "body tune +12 st jump glides instead of jumping (max diff " + format (step, 5) + " vs steady " + format (steady, 5) + ")");

        run ("body_pan", 0.0f, 1.0f, step, steady, before, early, after);
        check (std::isfinite (step) && step <= steady * 1.05f, "body pan hard-right jump: no step on the left channel");
    }

    void testPitchEnvelopeEndpoints()
    {
        using params::Envelope;
        auto sweep = [] (float curve)
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 100.0f);
            setParam (*p, params::nodeValueId (Envelope::pitch, 0), 800.0f);
            setParam (*p, params::nodeCurveId (Envelope::pitch, 0), curve);
            setParam (*p, params::nodeValueId (Envelope::pitch, 1), 50.0f);
            setParam (*p, params::nodeTimeId (Envelope::pitch, 1), 150.0f);
            return renderNote (*p, 29, 1.0f, 0.6);
        };

        const auto linear = sweep (0.0f);
        const auto crossings = zeroCrossings (linear, 0, linear.getNumSamples());
        check (crossings.size() > 20, "the sweep has zero crossings");

        if (crossings.size() > 2)
        {
            const auto firstHz = sampleRate / (crossings[1] - crossings[0]);
            check (std::abs (firstHz / 800.0 - 1.0) < 0.06, "starts at the 800 Hz start node (" + format (firstHz, 1) + " Hz over the first cycle)");

            // Every cycle in the sweep follows 800 * 16^(-t/150 ms) (exponential in Hz).
            double worst = 0.0, worstMs = 0.0;

            for (size_t i = 1; i < crossings.size(); ++i)
            {
                const auto midMs = 1000.0 * 0.5 * (crossings[i] + crossings[i - 1]) / sampleRate;

                if (1000.0 * crossings[i] / sampleRate > 150.0) // the sweep holds at 50 Hz after the end node
                    break;

                const auto measured = sampleRate / (crossings[i] - crossings[i - 1]);
                const auto expected = 800.0 * std::pow (16.0, -midMs / 150.0);
                if (std::abs (measured / expected - 1.0) > worst)
                {
                    worst = std::abs (measured / expected - 1.0);
                    worstMs = midMs;
                }
            }

            check (worst < 0.02, "sweep follows the exponential curve (worst cycle error " + format (worst * 100.0) + " % at " + format (worstMs, 1) + " ms)");
        }

        const auto endHz = measureFrequency (linear, sampleRate, 0.2, 0.55);
        check (std::abs (endHz / 50.0 - 1.0) < 0.005, "ends at the 50 Hz end node (" + format (endHz) + " Hz)");

        // Curve tension: positive drops early (lower pitch at 30 ms), negative drops late.
        const auto hzAt30 = [] (const juce::AudioBuffer<float>& b) { return measureFrequency (b, sampleRate, 0.027, 0.033); };
        const auto fast = hzAt30 (sweep (0.8f)), lin = hzAt30 (linear), slow = hzAt30 (sweep (-0.8f));
        check (fast < lin * 0.9 && slow > lin * 1.1,
               "curve tension bends the sweep (30 ms: +0.8 -> " + format (fast, 0) + " Hz, 0 -> " + format (lin, 0) + " Hz, -0.8 -> " + format (slow, 0) + " Hz)");

        // DSP-level envelope maths.
        dsp::MultiPointEnvelope env;
        env.numNodes = 3;
        env.nodes[0] = { 0.0f, 1.0f, 0.0f };
        env.nodes[1] = { 10.0f, 0.5f, 0.0f };
        env.nodes[2] = { 30.0f, 0.0f, 0.0f };
        check (std::abs (env.evaluate (5.0f) - 0.75f) < 1.0e-5f && std::abs (env.evaluate (20.0f) - 0.25f) < 1.0e-5f
                   && env.evaluate (1000.0f) == 0.0f,
               "multi-point envelope interpolates and holds its last value");
    }

    void testEnvelopesDecayToSilence()
    {
        const auto& presets = dooblrkick::presets::getFactoryPresets();

        for (int i = 0; i < (int) presets.size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);
            const auto settings = p->getCurrentSettings();

            dsp::KickEngine engine;
            engine.prepare (sampleRate, blockSize);
            engine.setSettings (settings);
            engine.noteOn (36, 1.0f);

            juce::AudioBuffer<float> b (2, blockSize);
            const juce::MidiBuffer midi;
            int blocks = 0;
            float lastPeak = 0.0f;

            while (engine.isPlaying() && blocks < 2000)
            {
                engine.process (b, midi);
                lastPeak = peakOf (b);
                ++blocks;
            }

            const auto seconds = blocks * blockSize / sampleRate;
            const auto limit = dsp::KickEngine::getRenderLengthSeconds (settings);
            check (! engine.isPlaying() && seconds <= limit + 0.05,
                   presets[(size_t) i].name.toStdString() + ": voice ends after " + format (seconds) + " s (render length " + format (limit) + " s)");
            check (lastPeak < 1.0e-3f, presets[(size_t) i].name.toStdString() + ": last block before the end is below -60 dB ("
                                         + format (juce::Decibels::gainToDecibels (lastPeak, -200.0f), 1) + " dB)");

            engine.process (b, midi);
            check (peakOf (b) < 1.0e-5f, presets[(size_t) i].name.toStdString() + ": silent once finished");
        }

        dsp::AhdEnvelope ahd { 2.0f, 10.0f, 100.0f, 0.5f };
        check (ahd.evaluate (1.0f) > 0.4f && ahd.evaluate (1.0f) < 0.6f, "AHD attack ramps");
        check (ahd.evaluate (8.0f) == 1.0f, "AHD hold is at full level");
        check (ahd.evaluate (60.0f) > 0.0f && ahd.evaluate (60.0f) < 1.0f, "AHD decays");
        check (ahd.evaluate (ahd.getLengthMs() + 0.01f) == 0.0f, "AHD ends at zero");
    }

    void testPresetsLoad()
    {
        const auto& presets = dooblrkick::presets::getFactoryPresets();
        std::vector<juce::AudioBuffer<float>> renders;

        for (int i = 0; i < (int) presets.size(); ++i)
        {
            auto p = makeProcessor();
            p->setCurrentProgram (i);
            const auto& preset = presets[(size_t) i];
            check (p->getCurrentProgram() == i, "host program " + std::to_string (i) + " selected");
            check (p->getPresetName() == preset.name, "preset name " + preset.name.toStdString());

            int mismatches = 0;
            for (const auto& [id, value] : dooblrkick::presets::resolve (p->getParameterState(), preset.values))
            {
                auto* param = p->getParameterState().getParameter (id);
                const auto actual = param->convertFrom0to1 (param->getValue());
                const auto snapped = param->convertFrom0to1 (param->convertTo0to1 (value));

                if (std::abs (actual - snapped) > 1.0e-3f * std::max (1.0f, std::abs (snapped)))
                {
                    ++mismatches;
                    note ("  " + id.toStdString() + " = " + format (actual, 3) + ", preset " + format (value, 3));
                }
            }

            check (mismatches == 0, preset.name.toStdString() + ": every parameter has the preset value");

            const auto b = renderNote (*p, (int) getPlain (*p, "root_note"), 1.0f, 1.5);
            const auto ceiling = getPlain (*p, "lim_mode") > 0.5f ? juce::Decibels::decibelsToGain (getPlain (*p, "lim_ceiling") + getPlain (*p, "output_gain")) : 2.0f;
            const auto peak = peakOf (b);
            check (allFinite (b) && peak > 0.2f, preset.name.toStdString() + ": loud and finite (peak " + format (peak, 3) + ")");
            check (peak <= ceiling * 1.02f, preset.name.toStdString() + ": peak within the ceiling");

            juce::String sampleInfo;
            for (int layer = 0; layer < params::numLayers; ++layer)
                if (params::layerHasSample (layer) && getPlain (*p, params::layerId (layer, "on")) > 0.5f
                    && getPlain (*p, params::layerId (layer, "source")) > 0.5f)
                    sampleInfo << " " << params::layerNames[(size_t) layer] << "="
                               << params::factorySampleNames[(size_t) (int) getPlain (*p, params::layerId (layer, "sample"))];

            note (preset.name.toStdString() + ": root " + params::noteName ((int) getPlain (*p, "root_note")).toStdString()
                  + ", peak " + format (juce::Decibels::gainToDecibels (peak), 1) + " dBFS, samples:"
                  + (sampleInfo.isEmpty() ? std::string (" none") : sampleInfo.toStdString()));
            renders.push_back (b);
        }

        for (size_t a = 0; a < renders.size(); ++a)
            for (size_t b = a + 1; b < renders.size(); ++b)
                check (maxDifference (renders[a], renders[b]) > 0.05f, "presets " + std::to_string (a) + " and " + std::to_string (b) + " sound different");

        // The in-plugin load (with a gesture) is one undo step and undoes completely.
        auto p = makeProcessor();
        setParam (*p, "body_level", -7.0f);
        p->getUndoHistory().clear();
        p->loadFactoryPreset (1, true);
        check (p->getUndoHistory().getNumUndoableSteps() == 1, "preset load is one undo step");
        p->getUndoHistory().undo();
        check (std::abs (getPlain (*p, "body_level") + 7.0f) < 0.01f && std::abs (getPlain (*p, "root_note") - 29.0f) < 0.01f,
               "undoing the preset load restores the previous sound");

        // Randomise: always a usable kick.
        juce::Random random (1234);
        int bad = 0;
        p->getUndoHistory().clear();

        for (int i = 0; i < 30; ++i)
        {
            p->randomise (random);
            const auto b = renderNote (*p, (int) getPlain (*p, "root_note"), 1.0f, 1.0);

            if (! allFinite (b) || peakOf (b) < 0.1f || peakOf (b) > 1.0f)
                ++bad;
        }

        check (bad == 0, "30 random kicks are all non-silent, finite and below 0 dBFS (" + std::to_string (bad) + " bad)");
        check (p->getUndoHistory().getNumUndoableSteps() == 30, "each randomise is one undo step");
    }

    void testRandomiseClickBody()
    {
        using dooblrkick::presets::ValueList;
        const juce::StringArray clickIds { "click_on", "click_level", "click_pan", "click_start", "click_send", "click_source",
                                           "click_sample", "click_type", "click_pitch", "click_tone", "click_smp_penv",
                                           "click_ftype", "click_cutoff", "click_reso",
                                           // v0.3.6: the Click's own envelopes (its amp envelope is its length)
                                           params::envelopeCountId (params::Envelope::clickAmp), params::nodeTimeId (params::Envelope::clickAmp, 1),
                                           params::envelopeCountId (params::Envelope::clickPitch), params::nodeValueId (params::Envelope::clickPitch, 0) };
        const juce::StringArray bodyIds { "body_on", "body_level", "body_pan", "body_start", "body_send", "body_source",
                                          "body_sample", "body_wave", "body_harm", "body_drive", "body_phase", "body_tune",
                                          "body_smp_penv", "body_ftype", "body_cutoff", "body_reso" };
        const juce::StringArray untouched { "click_keytrack", "body_keytrack", "sub_keytrack", "tail_keytrack", "noise_keytrack", "click_solo", "body_solo", "output_gain",
                                            "root_note", "lim_mode", "lim_ceiling", "eq_on", "comp_on", "shape_on" };

        // The value lists: every id exists, every value is inside its range (no snapping needed),
        // both layers are fully covered, key tracking / solo / output are not touched.
        {
            auto p = makeProcessor();
            juce::Random random (77);
            bool idsOk = true, inRange = true, coversClick = true, coversBody = true, leavesAlone = true;
            std::set<int> clickSources, bodySources, clickSamples, bodySamples;
            int clickOff = 0, bodyOff = 0, clickAndBodyOff = 0, allOff = 0, quietAlone = 0;

            for (int i = 0; i < 200; ++i)
            {
                const auto values = dooblrkick::presets::randomise (p->getParameterState(), random);
                std::set<juce::String> seen;

                for (const auto& [id, value] : values)
                {
                    seen.insert (id);
                    auto* param = p->getParameterState().getParameter (id);
                    idsOk = idsOk && param != nullptr;

                    if (param != nullptr)
                    {
                        const auto& range = param->getNormalisableRange();
                        inRange = inRange && std::isfinite (value) && value >= range.start - 1.0e-4f && value <= range.end + 1.0e-4f;
                    }

                    if (id == "click_source") clickSources.insert ((int) value);
                    if (id == "body_source")  bodySources.insert ((int) value);
                    if (id == "click_sample") clickSamples.insert ((int) value);
                    if (id == "body_sample")  bodySamples.insert ((int) value);
                }

                // v0.3.3: on / off across all five layers.
                std::map<juce::String, float> byId (values.begin(), values.end());
                int onCount = 0, lone = -1;
                for (int layer = 0; layer < params::numLayers; ++layer)
                    if (byId[params::layerId (layer, "on")] > 0.5f) { ++onCount; lone = layer; }
                clickOff += byId["click_on"] < 0.5f ? 1 : 0;
                bodyOff += byId["body_on"] < 0.5f ? 1 : 0;
                clickAndBodyOff += byId["click_on"] < 0.5f && byId["body_on"] < 0.5f ? 1 : 0;
                allOff += onCount == 0 ? 1 : 0;
                if (onCount == 1 && byId[params::layerId (lone, "level")] < dooblrkick::presets::soloLevelFloorDb - 1.0e-3f)
                    ++quietAlone;

                for (const auto& id : clickIds) coversClick = coversClick && seen.count (id) > 0;
                for (const auto& id : bodyIds)  coversBody = coversBody && seen.count (id) > 0;
                for (const auto& id : untouched) leavesAlone = leavesAlone && seen.count (id) == 0;
            }

            check (idsOk, "randomise only sets parameters that exist");
            check (inRange, "200 randomisations: every value is finite and inside its parameter's range");
            check (coversClick, "randomise sets every Click sound parameter (source, sample, type, pitch, length, tone, level, filter ...)");
            check (coversBody, "randomise sets every Body sound parameter (source, sample, wave, tune, drive, level, filter ...)");
            check (leavesAlone, "randomise leaves key tracking, solo, the Key, output gain, EQ, Dynamics and the Limiter alone");
            check (clickOff > 0 && bodyOff > 0 && clickAndBodyOff > 0, "200 randomisations: Click is sometimes off ("
                   + std::to_string (clickOff) + "), Body too (" + std::to_string (bodyOff) + "), and sometimes both ("
                   + std::to_string (clickAndBodyOff) + ")");
            check (clickOff < 100 && bodyOff < 100, "Click and Body are still on most of the time");
            check (allOff == 0, "200 randomisations: never all five layers off");
            check (quietAlone == 0, "a layer that is on alone is never below -9 dB");
            check (clickSources.size() == 2 && bodySources.size() == 2, "both Synth and Sample sources come up for Click and Body");
            check (clickSamples.size() >= 4 && bodySamples.size() >= 4 && clickSamples.count (params::userSampleChoice) == 0
                       && bodySamples.count (params::userSampleChoice) == 0,
                   "several factory samples are chosen (never the User file slot)");
        }

        // Seeded randomisations on a processor: Click and Body change every time, each press is
        // one undo step, and every result renders finite, non-silent audio within 0 dBFS, with
        // both Click and Body audible in it. Key tracking stays opt-in (as the user left it).
        for (const auto seed : { 1, 2, 3, 4, 5, 6 })
        {
            auto p = makeProcessor();
            setParam (*p, "body_keytrack", 1.0f); // a user choice that randomise must keep
            p->getUndoHistory().clear();
            juce::Random random (seed);
            const auto tag = "seed " + std::to_string (seed) + ": ";

            for (int i = 0; i < 4; ++i)
            {
                std::map<juce::String, float> before;
                for (const auto& id : clickIds) before[id] = getPlain (*p, id);
                for (const auto& id : bodyIds)  before[id] = getPlain (*p, id);

                const auto steps = p->getUndoHistory().getNumUndoableSteps();
                p->randomise (random);
                check (p->getUndoHistory().getNumUndoableSteps() == steps + 1, tag + "randomise is one undo step");

                int clickChanged = 0, bodyChanged = 0;
                for (const auto& id : clickIds) clickChanged += std::abs (getPlain (*p, id) - before[id]) > 1.0e-4f ? 1 : 0;
                for (const auto& id : bodyIds)  bodyChanged += std::abs (getPlain (*p, id) - before[id]) > 1.0e-4f ? 1 : 0;
                check (clickChanged >= 3 && bodyChanged >= 3, tag + "Click and Body parameters change ("
                       + std::to_string (clickChanged) + " / " + std::to_string (bodyChanged) + ")");
                int layersOn = 0;
                for (int layer = 0; layer < params::numLayers; ++layer)
                    layersOn += getPlain (*p, params::layerId (layer, "on")) > 0.5f ? 1 : 0;
                check (layersOn >= 1, tag + "at least one of the five layers is on");
                check (getPlain (*p, "body_keytrack") > 0.5f && getPlain (*p, "click_keytrack") < 0.5f, tag + "key tracking is left as it was");

                const auto full = renderNote (*p, (int) getPlain (*p, "root_note"), 1.0f, 1.0);
                check (allFinite (full) && peakOf (full) > 0.1f && peakOf (full) <= 1.0f,
                       tag + "renders finite, non-silent audio within 0 dBFS (peak " + format (peakOf (full), 3) + ")");

                // Audibility of Click and Body when they are on; switched on when off, they sound
                // (their parameters were randomised too). These edits are undone again below.
                for (const auto* layer : { "click", "body" })
                {
                    const auto onId = juce::String (layer) + "_on";
                    const auto wasOn = getPlain (*p, onId) > 0.5f;
                    setParam (*p, onId, wasOn ? 0.0f : 1.0f);
                    const auto toggled = renderNote (*p, (int) getPlain (*p, "root_note"), 1.0f, 1.0);
                    setParam (*p, onId, wasOn ? 1.0f : 0.0f);
                    check (allFinite (toggled) && maxDifference (full, toggled) > (juce::String (layer) == "click" ? 0.01f : 0.05f),
                           tag + layer + (wasOn ? " is on and audible" : " is off, and switching it on is audible"));
                }
            }

        }

        // v0.3.3: results with a single layer on (each of the five, found across seeds) and with a
        // soloed layer render finite, clearly audible audio.
        {
            std::array<bool, params::numLayers> rendered {};
            int lonelyResults = 0, bad = 0;
            juce::Random random (2026);
            auto p = makeProcessor();

            for (int i = 0; i < 4000 && lonelyResults < params::numLayers; ++i)
            {
                const auto values = dooblrkick::presets::randomise (p->getParameterState(), random);
                std::map<juce::String, float> byId (values.begin(), values.end());
                int onCount = 0, lone = -1;
                for (int layer = 0; layer < params::numLayers; ++layer)
                    if (byId[params::layerId (layer, "on")] > 0.5f) { ++onCount; lone = layer; }
                if (onCount != 1 || rendered[(size_t) lone])
                    continue;

                rendered[(size_t) lone] = true;
                ++lonelyResults;
                auto q = makeProcessor();
                dooblrkick::presets::apply (q->getParameterState(), values, false);
                const auto b = renderNote (*q, (int) getPlain (*q, "root_note"), 1.0f, 1.0);
                if (! allFinite (b) || peakOf (b) < 0.1f || peakOf (b) > 1.0f)
                {
                    ++bad;
                    note ("  " + std::string (params::layerNames[(size_t) lone]) + " alone: peak " + format (peakOf (b), 3));
                }
            }
            check (lonelyResults == params::numLayers, "each of the five layers comes up as the only one on ("
                   + std::to_string (lonelyResults) + " of 5 found)");
            check (bad == 0, "a layer on its own is clearly audible (peak > 0.1), finite and within 0 dBFS");

            for (int layer = 0; layer < params::numLayers; ++layer)
            {
                auto q = makeProcessor();
                setParam (*q, params::layerId (layer, "solo"), 1.0f);
                juce::Random soloRandom (300 + layer);
                bool keptOn = true, audible = true;
                for (int i = 0; i < 6; ++i)
                {
                    q->randomise (soloRandom);
                    keptOn = keptOn && getPlain (*q, params::layerId (layer, "on")) > 0.5f;
                    const auto b = renderNote (*q, (int) getPlain (*q, "root_note"), 1.0f, 1.0);
                    const auto ok = allFinite (b) && peakOf (b) > 0.1f && peakOf (b) <= 1.0f;
                    if (! ok)
                        note ("  " + std::string (params::layerNames[(size_t) layer]) + " soloed draw " + std::to_string (i) + ": peak "
                              + format (peakOf (b), 3) + ", source " + format (getPlain (*q, params::layerId (layer, "source")), 0)
                              + ", level " + format (getPlain (*q, params::layerId (layer, "level")), 1) + " dB"
                              + (layer == dsp::clickLayer ? ", type " + format (getPlain (*q, "click_type"), 0) + ", amp end "
                                    + format (ampEndMs (*q, layer), 2) + " ms, pitch0 "
                                    + format (getPlain (*q, params::nodeValueId (params::Envelope::clickPitch, 0)), 2) + " st, ftype "
                                    + format (getPlain (*q, "click_ftype"), 0) + ", cutoff " + format (getPlain (*q, "click_cutoff"), 0)
                                    + ", tune " + format (getPlain (*q, "click_pitch"), 2) + ", start " + format (getPlain (*q, "click_start"), 1)
                                    + ", tone " + format (getPlain (*q, "click_tone"), 2) : std::string()));
                    audible = audible && ok;
                }
                check (keptOn && audible, std::string (params::layerNames[(size_t) layer]) + " soloed: Random keeps it on and it is audible");
            }
        }

        // Undo restores the previous Click and Body completely.
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (2, false);
            std::map<juce::String, float> values;
            for (const auto& id : allParameterIds (*p)) values[id] = getPlain (*p, id);

            p->getUndoHistory().clear();
            juce::Random random (99);
            p->randomise (random);
            check (p->getUndoHistory().getNumUndoableSteps() == 1, "one randomise = one undo step");
            p->getUndoHistory().undo();

            juce::StringArray differing;
            for (const auto& [id, value] : values)
                if (std::abs (getPlain (*p, id) - value) >= 1.0e-3f)
                    differing.add (id);

            // The sound: fresh processors (no voice / filter state carried over) for both.
            auto reference = makeProcessor();
            reference->loadFactoryPreset (2, false);
            juce::MemoryBlock undone;
            p->getStateInformation (undone);
            auto restoredProcessor = makeProcessor();
            restoredProcessor->setStateInformation (undone.getData(), (int) undone.getSize());
            const auto difference = maxDifference (renderNote (*restoredProcessor, 36, 1.0f, 1.0), renderNote (*reference, 36, 1.0f, 1.0));

            check (differing.isEmpty() && difference < 1.0e-4f,
                   "undoing a randomise restores the previous Click, Body and sound (differing: "
                       + differing.joinIntoString (", ").toStdString() + "; render diff " + std::to_string (difference) + ")");
        }
    }

    void testUserPresets()
    {
        auto p = makeProcessor();
        p->loadFactoryPreset (3, false);
        setParam (*p, "body_level", -4.5f);
        const auto file = makeTempFile (dooblrkick::presets::userPresetExtension);
        juce::String error;
        check (p->saveUserPreset (file, error) && file.existsAsFile(), "user preset saved (" + error.toStdString() + ")");

        const auto expected = renderNote (*p, 36, 1.0f, 1.0);

        auto q = makeProcessor();
        q->getUndoHistory().clear();
        check (q->loadUserPreset (file, error), "user preset loaded (" + error.toStdString() + ")");
        check (std::abs (getPlain (*q, "body_level") + 4.5f) < 0.01f, "user preset restores a value");
        check (q->getPresetName() == file.getFileNameWithoutExtension(), "preset name is the file name");
        check (q->getUndoHistory().getNumUndoableSteps() == 1, "loading a user preset is one undo step");
        check (maxDifference (renderNote (*q, 36, 1.0f, 1.0), expected) < 1.0e-5f, "user preset sounds identical");

        const auto garbage = makeTempFile (".dkick");
        garbage.replaceWithText ("not a preset");
        check (! q->loadUserPreset (garbage, error) && error.isNotEmpty(), "a broken preset file is refused with a message");
        file.deleteFile();
        garbage.deleteFile();
    }

    void testStateRoundTrip()
    {
        auto p = makeProcessor();
        p->loadFactoryPreset (2, false);
        setParam (*p, "body_level", -3.25f);
        setParam (*p, "tail_on", 1.0f);
        setParam (*p, "tail_source", 1.0f);
        setParam (*p, "tail_sample", 3.0f);
        setParam (*p, "dist_type", 4.0f);
        setParam (*p, "body_tune", -31.5f);
        setParam (*p, params::nodeTimeId (params::Envelope::pitch, 2), 77.0f);

        juce::MemoryBlock state;
        p->getStateInformation (state);

        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());

        int mismatches = 0;
        for (const auto& id : allParameterIds (*p))
            if (std::abs (p->getParameterState().getParameter (id)->getValue() - q->getParameterState().getParameter (id)->getValue()) > 1.0e-6f)
            {
                ++mismatches;
                note ("  differs: " + id.toStdString());
            }

        check (mismatches == 0, "every parameter survives a state round-trip");
        check (q->getPresetName() == p->getPresetName(), "preset name survives (" + q->getPresetName().toStdString() + ")");
        check (q->getUndoHistory().getNumUndoableSteps() == 0, "a state load leaves no undo history");
        check (maxDifference (renderNote (*p, 36, 1.0f, 1.0), renderNote (*q, 36, 1.0f, 1.0)) < 1.0e-5f, "restored state sounds identical");

        // Garbage state is ignored.
        const char junk[] = "definitely not a plugin state";
        q->setStateInformation (junk, (int) sizeof (junk));
        check (std::abs (getPlain (*q, "body_level") + 3.25f) < 0.01f, "garbage state is ignored");
    }

    void testLayerMuteAndSolo()
    {
        auto renderWith = [] (const std::function<void (Processor&)>& setup)
        {
            auto p = makeProcessor();
            setParam (*p, "lim_mode", 0.0f);
            for (int l = 0; l < params::numLayers; ++l)
                setParam (*p, params::layerId (l, "on"), 1.0f);
            setup (*p);
            return renderNote (*p, 36, 1.0f, 1.2);
        };

        const auto all = renderWith ([] (Processor&) {});
        const auto again = renderWith ([] (Processor&) {});
        check (maxDifference (all, again) == 0.0f, "rendering is deterministic (noise included)");

        for (int layer = 0; layer < params::numLayers; ++layer)
        {
            const auto name = std::string (params::layerNames[(size_t) layer]);
            const auto muted = renderWith ([layer] (Processor& p) { setParam (p, params::layerId (layer, "on"), 0.0f); });
            check (maxDifference (all, muted) > 1.0e-3f, "muting " + name + " changes the output");

            const auto soloed = renderWith ([layer] (Processor& p) { setParam (p, params::layerId (layer, "solo"), 1.0f); });
            const auto only = renderWith ([layer] (Processor& p)
                                          {
                                              for (int l = 0; l < params::numLayers; ++l)
                                                  setParam (p, params::layerId (l, "on"), l == layer ? 1.0f : 0.0f);
                                          });
            check (maxDifference (soloed, only) < 1.0e-6f && peakOf (only) > 1.0e-3f, "solo " + name + " equals that layer alone");

            const auto level = renderWith ([layer] (Processor& p) { setParam (p, params::layerId (layer, "level"), -60.0f); });
            check (maxDifference (level, muted) < 1.0e-6f, name + " level -60 dB is silent");
        }

        const auto none = renderWith ([] (Processor& p)
                                      {
                                          for (int l = 0; l < params::numLayers; ++l)
                                              setParam (p, params::layerId (l, "on"), 0.0f);
                                      });
        check (peakOf (none) == 0.0f, "all layers off is silent");
    }

    double thd (const juce::AudioBuffer<float>& b, double fundamental, int start, int length)
    {
        auto power = [&] (double hz)
        {
            double re = 0.0, im = 0.0;
            const auto* x = b.getReadPointer (0);

            for (int i = 0; i < length; ++i)
            {
                const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / sampleRate;
                re += x[start + i] * std::cos (phase);
                im += x[start + i] * std::sin (phase);
            }

            return re * re + im * im;
        };

        const auto f = power (fundamental);
        double harmonics = 0.0;

        for (int h = 2; h <= 15; ++h)
            harmonics += power (fundamental * h);

        return f > 0.0 ? std::sqrt (harmonics / f) : 0.0;
    }

    void testDistortionThdRisesWithDrive()
    {
        auto render = [] (int type, float driveDb, float bits = 8.0f)
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 100.0f);
            setParam (*p, "body_level", -12.0f);
            setParam (*p, "dist_on", 1.0f);
            setParam (*p, "dist_type", (float) type);
            setParam (*p, "dist_drive", driveDb);
            setParam (*p, "dist_tone", 1.0f);
            setParam (*p, "dist_out", 0.0f);
            setParam (*p, "dist_mix", 1.0f);
            setParam (*p, "dist_bits", bits);
            setParam (*p, "dist_rate", 48000.0f);
            setParam (*p, "body_send", 1.0f);
            return renderNote (*p, 29, 1.0f, 0.6);
        };

        const std::array<const char*, 5> names { "soft", "hard", "tube", "fold", "bitcrush" };

        for (int type = 0; type < 4; ++type)
        {
            std::vector<double> values;
            std::string line;

            for (const auto drive : { 0.0f, 6.0f, 12.0f, 24.0f, 36.0f })
            {
                values.push_back (thd (render (type, drive), 100.0, 9600, 14400));
                line += format (values.back() * 100.0, 2) + "% ";
            }

            note (std::string (names[(size_t) type]) + " THD at 0/6/12/24/36 dB drive: " + line);

            bool rising = true;
            if (type != 3) // folding wraps around, so only the overall trend is required
                for (size_t i = 1; i < values.size(); ++i)
                    rising = rising && values[i] >= values[i - 1] - 1.0e-4;

            check (rising, std::string (names[(size_t) type]) + ": THD never falls as drive rises");
            check (values.back() > values.front() + 0.1, std::string (names[(size_t) type]) + ": heavy drive adds > 10 % THD");
        }

        const auto fine = thd (render (4, 0.0f, 16.0f), 100.0, 9600, 14400);
        const auto coarse = thd (render (4, 0.0f, 4.0f), 100.0, 9600, 14400);
        note ("bitcrush THD at 16 / 4 bits: " + format (fine * 100.0) + "% / " + format (coarse * 100.0) + "%");
        check (coarse > fine * 5.0 && coarse > 0.02, "bitcrush: fewer bits, more distortion");

        // Static curves are odd-symmetric (soft / hard) and bounded.
        bool bounded = true;
        for (int type = 0; type < 4; ++type)
            for (float x = -4.0f; x <= 4.0f; x += 0.01f)
                bounded = bounded && std::abs (dsp::Distortion::shape (type, x, 8.0f)) <= 1.7f;

        // Tube is deliberately asymmetric: its negative side saturates at -1.67.
        check (bounded, "shaper curves stay bounded (|y| <= 1.7) at 8x gain");
    }

    void testVelocityAndGate()
    {
        auto peakAt = [] (float sens, float velocity)
        {
            auto p = makeProcessor();
            setParam (*p, "lim_mode", 0.0f);
            setParam (*p, "vel_sens", sens);
            return peakOf (renderNote (*p, 36, velocity, 0.5));
        };

        check (peakAt (1.0f, 0.5f) < peakAt (1.0f, 1.0f) * 0.7f, "full velocity sensitivity: soft notes are quieter");
        check (std::abs (peakAt (0.0f, 0.3f) - peakAt (0.0f, 1.0f)) < 1.0e-6f, "zero velocity sensitivity: velocity is ignored");

        // Every note retriggers the hit, at the same pitch.
        auto p = makeProcessor();
        dsp::KickEngine engine;
        engine.prepare (sampleRate, blockSize);
        engine.setSettings (p->getCurrentSettings());
        juce::AudioBuffer<float> b (2, 480);
        const juce::MidiBuffer midi;
        engine.noteOn (29, 1.0f);
        for (int i = 0; i < 10; ++i) engine.process (b, midi);
        engine.noteOn (41, 1.0f);
        engine.process (b, midi);
        check (engine.getCurrentVoice().getElapsedSeconds() < 0.02, "an overlapping note retriggers the hit");

        // Gate: note-off fades the hit out over the release time.
        auto gated = [] (bool gate)
        {
            auto q = makeProcessor();
            setParam (*q, "gate", gate ? 1.0f : 0.0f);
            setParam (*q, "release", 30.0f);
            setLayerAhd (*q, dsp::subLayer, 1.0f, 1000.0f, 350.0f, 0.4f);
            return renderMidi (*q, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) },
                                     { 4800, juce::MidiMessage::noteOff (1, 36) } }, 24000);
        };

        const auto withGate = gated (true), withoutGate = gated (false);
        check (peakOf (withGate, 4800 + 2400, 24000) < 1.0e-4f, "gate: silent 50 ms after note-off with a 30 ms release");
        check (rmsOf (withoutGate, 9600, 14400) > 0.02f, "no gate: the hit rings on after note-off");
    }

    //==============================================================================
    void testFactorySamples()
    {
        juce::SharedResourcePointer<dooblrkick::samples::FactoryBank> bank;

        // 21 samples; the 0.1.0 set keeps its indices (saved states and presets refer to them).
        check (params::numFactorySamples == 21 && params::userSampleChoice == 21, "21 factory samples, User file is choice 21");
        const char* v010[] = { "Kit Kick Close", "Kit Kick Mid", "Concert BD Muted", "Concert BD Open", "Concert BD Boom",
                               "Cajon Bass", "Frame Drum Mute", "Slapstick", "Woodblock", "Hand Clap", "Shaker Hit" };
        bool stable = true;
        for (int i = 0; i < 11; ++i)
            stable = stable && juce::String (params::factorySampleNames[(size_t) i]) == v010[i];
        check (stable, "the 11 v0.1.0 samples keep indices 0 .. 10");
        {
            auto choices = params::sampleChoices();
            juce::StringArray unique (choices);
            unique.removeDuplicates (false);
            check (choices.size() == params::numFactorySamples + 1 && unique.size() == choices.size() && choices[params::userSampleChoice] == "User file",
                   "sample selector lists 21 distinct samples plus User file");
            auto proc = makeProcessor();
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc->getParameterState().getParameter ("body_sample"));
            check (choice != nullptr && choice->choices.size() == params::numFactorySamples + 1, "Body Sample parameter has 22 choices");
        }

        for (int i = 0; i < params::numFactorySamples; ++i)
        {
            const auto* s = bank->get (i);
            check (s != nullptr && ! s->samples.empty(), "factory sample " + std::to_string (i) + " decoded");

            if (s == nullptr || s->samples.empty())
                continue;

            float peak = 0.0f;
            bool finite = true;
            for (auto v : s->samples)
            {
                peak = std::max (peak, std::abs (v));
                finite = finite && std::isfinite (v);
            }

            // Every file is peak-normalised to -1 dBFS and at least 40 ms long.
            check (s->getLengthMs() >= 40.0 && std::abs (juce::Decibels::gainToDecibels (peak) + 1.0f) < 0.2f,
                   std::string (params::factorySampleNames[(size_t) i]) + ": -1 dBFS peak and >= 40 ms");
            for (int j = 0; j < i; ++j)
                if (const auto* o = bank->get (j); o != nullptr && o->samples == s->samples)
                    check (false, std::string (params::factorySampleNames[(size_t) i]) + " duplicates sample " + std::to_string (j));
            check (finite && peak > 0.5f && peak <= 1.0f, std::string (params::factorySampleNames[(size_t) i]) + ": finite, peak " + format (peak, 3));
            note (std::string (params::factorySampleNames[(size_t) i]) + ": " + format (s->getLengthMs(), 0) + " ms at "
                  + format (s->sampleRate, 0) + " Hz, peak " + format (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
        }

        check (bank->get (-1) == nullptr && bank->get (params::numFactorySamples) == nullptr, "out-of-range sample index gives nullptr");

        // The reader resamples (44.1 kHz source at 48 kHz) and re-pitches without aliasing artefacts.
        dsp::SampleReader::prepareTables();
        dsp::SampleData sine;
        sine.sampleRate = 44100.0;
        sine.samples.resize (44100);
        for (size_t i = 0; i < sine.samples.size(); ++i)
            sine.samples[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (double) i / 44100.0);

        for (const auto pitch : { 1.0, 2.0, 0.5 })
        {
            dsp::SampleReader reader;
            juce::AudioBuffer<float> out (1, 24000);
            for (int i = 0; i < out.getNumSamples(); ++i)
                out.setSample (0, i, reader.read (&sine, pitch * 44100.0 / sampleRate));

            const auto hz = measureFrequency (out, sampleRate, 0.05, 0.4);
            check (std::abs (hz / (100.0 * pitch) - 1.0) < 0.002, "sample reader at x" + format (pitch, 1) + " plays " + format (hz) + " Hz");
            check (std::abs (peakOf (out, 2400, 19000) - 0.5f) < 0.01f, "sample reader keeps the level at x" + format (pitch, 1));
        }
    }

    void testSampleLayers()
    {
        juce::SharedResourcePointer<dooblrkick::samples::FactoryBank> bank;

        for (int layer = 0; layer < params::numLayers; ++layer)
        {
            if (! params::layerHasSample (layer))
                continue;

            const auto name = std::string (params::layerNames[(size_t) layer]);
            int bad = 0;

            for (int i = 0; i < params::numFactorySamples; ++i)
            {
                auto p = makeProcessor();
                isolateLayer (*p, layer);
                setParam (*p, params::layerId (layer, "level"), 0.0f);
                setParam (*p, params::layerId (layer, "source"), 1.0f);
                setParam (*p, params::layerId (layer, "sample"), (float) i);
                setLayerAhd (*p, dsp::noiseLayer, 0.5f, 5.0f, 1000.0f, 0.6f);
                const auto b = renderNote (*p, 29, 1.0f, 1.0);

                if (! allFinite (b) || peakOf (b) < 1.0e-3f)
                {
                    ++bad;
                    note ("  silent: " + name + " / " + params::factorySampleNames[(size_t) i]);
                }
            }

            check (bad == 0, name + ": all " + std::to_string (params::numFactorySamples) + " factory samples render non-silent and finite");

            auto synth = makeProcessor(), sampled = makeProcessor();
            for (auto* p : { synth.get(), sampled.get() })
            {
                isolateLayer (*p, layer);
                setParam (*p, params::layerId (layer, "level"), 0.0f);
            }
            setParam (*sampled, params::layerId (layer, "source"), 1.0f);
            const auto a = renderNote (*synth, 29, 1.0f, 0.6), b = renderNote (*sampled, 29, 1.0f, 0.6);
            check (maxDifference (a, b) > 0.05f && peakOf (a) > 1.0e-3f && peakOf (b) > 1.0e-3f, name + ": Synth / Sample switch changes the sound");

            // A different factory sample sounds different.
            auto other = makeProcessor();
            isolateLayer (*other, layer);
            setParam (*other, params::layerId (layer, "level"), 0.0f);
            setParam (*other, params::layerId (layer, "source"), 1.0f);
            setParam (*other, params::layerId (layer, "sample"), (getPlain (*sampled, params::layerId (layer, "sample")) + 1.0f));
            check (maxDifference (renderNote (*other, 29, 1.0f, 0.6), b) > 0.05f, name + ": the sample choice changes the sound");
        }

        // Body sample: with Sample key track on, the note re-pitches (an octave up plays twice as fast).
        auto p = makeProcessor();
        isolateLayer (*p, dsp::bodyLayer);
        flatBody (*p, 43.65f, 3000.0f); // long flat amp envelope, so the sample's own length shows
        setParam (*p, "body_source", 1.0f);
        setParam (*p, "body_level", 0.0f);
        setParam (*p, "body_keytrack", 1.0f);
        const auto length = [] (const juce::AudioBuffer<float>& b)
        {
            for (int i = b.getNumSamples() - 1; i > 0; --i)
                if (std::abs (b.getSample (0, i)) > 1.0e-3f)
                    return i;
            return 0;
        };
        const auto rootLength = length (renderNote (*p, 29, 1.0f, 2.0)), upLength = length (renderNote (*p, 41, 1.0f, 2.0));
        check (rootLength > 0 && std::abs ((double) upLength / rootLength - 0.5) < 0.1,
               "body sample an octave up lasts half as long (" + std::to_string (rootLength) + " -> " + std::to_string (upLength) + " samples)");

        // Sample pitch envelope amount bends the sample.
        setParam (*p, params::nodeValueId (params::Envelope::pitch, 0), 175.0f); // a two-octave drop to bend
        const auto straight = renderNote (*p, 29, 1.0f, 0.5);
        setParam (*p, "body_smp_penv", 1.0f);
        check (maxDifference (renderNote (*p, 29, 1.0f, 0.5), straight) > 0.05f, "body sample pitch-envelope amount changes the sound");

        // Sample choice survives the state.
        auto q = makeProcessor();
        setParam (*q, "body_source", 1.0f);
        setParam (*q, "body_sample", 3.0f);
        setParam (*q, "click_source", 1.0f);
        setParam (*q, "click_sample", 8.0f);
        juce::MemoryBlock state;
        q->getStateInformation (state);
        auto r = makeProcessor();
        r->setStateInformation (state.getData(), (int) state.getSize());
        check (getPlain (*r, "body_source") > 0.5f && std::abs (getPlain (*r, "body_sample") - 3.0f) < 0.01f
                   && std::abs (getPlain (*r, "click_sample") - 8.0f) < 0.01f,
               "source and sample choices persist in the state");
        check (maxDifference (renderNote (*q, 36), renderNote (*r, 36)) < 1.0e-5f, "restored sample state sounds identical");
    }

    void testUserSample()
    {
        const auto file = makeTempFile (".wav");
        check (writeTestWav (file, 44100.0, 110.0, 0.8), "test WAV written");

        auto p = makeProcessor();
        isolateLayer (*p, dsp::bodyLayer);
        juce::String error;
        check (p->loadUserSample (dsp::bodyLayer, file, error), "user sample loads (" + error.toStdString() + ")");
        check (p->getUserSamplePath (dsp::bodyLayer) == file.getFullPathName(), "user sample path stored");
        check (p->getUserSample (dsp::bodyLayer) != nullptr
                   && std::abs (p->getUserSample (dsp::bodyLayer)->getLengthMs() - 800.0f) < 1.0f,
               "user sample decoded at its own length");

        setParam (*p, "body_source", 1.0f);
        setParam (*p, "body_sample", (float) params::userSampleChoice);
        setParam (*p, "body_level", 0.0f);
        const auto b = renderNote (*p, 29, 1.0f, 0.6);
        const auto hz = measureFrequency (b, sampleRate, 0.05, 0.3);
        check (allFinite (b) && peakOf (b) > 0.1f, "user sample plays");
        check (std::abs (hz / 110.0 - 1.0) < 0.005, "user sample at the root note keeps its pitch (" + format (hz) + " Hz)");

        int allocations = 0;
        renderMidi (*p, { { 0, juce::MidiMessage::noteOn (1, 29, 1.0f) } }, 24000, blockSize, &allocations);
        check (allocations == 0, "playing the user sample does not allocate (" + std::to_string (allocations) + ")");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getUserSamplePath (dsp::bodyLayer) == file.getFullPathName() && q->getUserSample (dsp::bodyLayer) != nullptr,
               "user sample is reloaded from the state");
        check (maxDifference (renderNote (*q, 29, 1.0f, 0.6), b) < 1.0e-5f, "restored user sample sounds identical");

        // Missing file: no crash, the layer is silent, the path is kept for when it comes back.
        file.deleteFile();
        auto r = makeProcessor();
        r->setStateInformation (state.getData(), (int) state.getSize());
        const auto missing = renderNote (*r, 29, 1.0f, 0.5);
        check (r->getUserSample (dsp::bodyLayer) == nullptr && allFinite (missing) && peakOf (missing) < 1.0e-6f,
               "missing user sample: silent and finite");

        check (! p->loadUserSample (dsp::bodyLayer, file, error) && error.isNotEmpty(), "loading a missing file fails with a message");
        p->clearUserSample (dsp::bodyLayer);
        check (p->getUserSample (dsp::bodyLayer) == nullptr && p->getUserSamplePath (dsp::bodyLayer).isEmpty(), "user sample cleared");
        p->getCurrentSettings(); // retired data is freed later by the message thread timer
    }

    /** Every non-envelope parameter (and the active envelope nodes) audibly changes the kick. */
    void testNoDeadParameters()
    {
        using ValueList = dooblrkick::presets::ValueList;

        ValueList base { { "dist_on", 1 }, { "dist_type", 0 }, { "dist_drive", 12 }, { "dist_tone", 0.6f },
                         { "eq_on", 1 }, { "eq_low_gain", 4 }, { "eq_mid_gain", -4 }, { "eq_high_gain", 4 },
                         { "shape_on", 1 }, { "shape_attack", 0.3f }, { "shape_sustain", -0.3f },
                         { "comp_on", 1 }, { "comp_threshold", -24 }, { "lim_mode", 2 }, { "lim_ceiling", -3 },
                         { "vel_sens", 0.5f } };

        for (int l = 0; l < params::numLayers; ++l)
        {
            base.push_back ({ params::layerId (l, "on"), 1 });
            base.push_back ({ params::layerId (l, "level"), -6 });
            base.push_back ({ params::layerId (l, "ftype"), 1 });
            base.push_back ({ params::layerId (l, "cutoff"), 3000 });
            base.push_back ({ params::layerId (l, "send"), 0.5f });
        }

        auto contextFor = [] (const juce::String& id) -> ValueList
        {
            for (int l = 0; l < params::numLayers; ++l)
                if (params::layerHasSample (l) && id == params::layerId (l, "sample"))
                    return { { params::layerId (l, "source"), 1 } };

            if (id == "body_smp_penv") return { { "body_source", 1 } };
            if (id == "dist_bits" || id == "dist_rate") return { { "dist_type", 4 } };
            for (const auto* layer : { "click", "body", "tail" })
                if (id == juce::String (layer) + "_keytrack") return { { juce::String (layer) + "_source", 1 } };
            if (id == "noise_tune" || id == "noise_keytrack") return { { "noise_source", 1 } };
            if (id == "release") return { { "gate", 1 } };
            if (id == "lim_release") return { { "comp_on", 0 }, { "lim_ceiling", -12 } };
            // v0.3.6 layer envelopes. The semitone envelopes are flat at 0 st by default: give them
            // a drop so their other points (and the sample amount) matter; Sample-only amounts
            // need the sample source. The short Noise attack needs the noise louder.
            const auto info = envelopeParamInfo (id);
            for (int l = 0; l < params::numLayers; ++l)
            {
                if (l == dsp::bodyLayer)
                    continue;

                const auto pitch = params::layerEnvelope (l, false);
                ValueList drop { { params::nodeValueId (pitch, 0), 12.0f } };

                if (id == params::layerId (l, "smp_penv"))
                    return { { params::layerId (l, "source"), 1 }, drop[0] };

                if (info.isEnvelope && info.envelope == pitch && id != params::nodeValueId (pitch, 0))
                    return drop;
            }
            // Flat hold segments (Sub's and Noise's default hold): bend the hold so its curve matters.
            for (const auto e : { params::Envelope::subAmp, params::Envelope::noiseAmp })
                if (id == params::nodeCurveId (e, 1))
                    return { { params::nodeValueId (e, 2), 0.5f }, { "noise_level", 0 } };
            if (info.isEnvelope && info.envelope == params::Envelope::noiseAmp) return { { "noise_level", 0 } };
            // The master envelope is flat at 100 % by default: give it a slope so its other points matter.
            if (id.startsWith ("menv") && id != params::nodeValueId (params::Envelope::master, 1))
                return { { params::nodeValueId (params::Envelope::master, 1), 0.2f } };
            return {};
        };

        // Notes away from the Key so sample key tracking matters, then note-offs so gate / release matter.
        const std::vector<NoteEvent> events { { 0, juce::MidiMessage::noteOn (1, 36, 0.7f) },
                                              { 4800, juce::MidiMessage::noteOn (1, 43, 0.7f) },
                                              { 7200, juce::MidiMessage::noteOff (1, 36) },
                                              { 14400, juce::MidiMessage::noteOff (1, 43) } };

        auto render = [&] (const ValueList& context, const juce::String& id, float normalised)
        {
            auto p = makeProcessor();
            setValues (*p, base);
            setValues (*p, context);
            if (id.isNotEmpty())
                p->getParameterState().getParameter (id)->setValueNotifyingHost (normalised);
            return renderMidi (*p, events, 48000);
        };

        auto probe = makeProcessor();
        int tested = 0;
        juce::StringArray dead;

        for (const auto& id : allParameterIds (*probe))
        {
            if (isEnvelopeParam (id))
            {
                // Point counts are covered by the editor add / remove tests; only nodes inside
                // the default point counts are active.
                if (id.endsWith ("_count"))
                    continue;

                const auto info = envelopeParamInfo (id);
                const auto count = juce::roundToInt (getPlain (*probe, params::envelopeCountId (info.envelope)));

                if (info.node >= (info.isCurve ? count - 1 : count))
                    continue;
            }

            const auto context = contextFor (id);
            const auto reference = render (context, {}, 0.0f);
            auto* param = probe->getParameterState().getParameter (id);
            const auto current = [&]
            {
                auto p = makeProcessor();
                setValues (*p, base);
                setValues (*p, context);
                return p->getParameterState().getParameter (id)->getValue();
            }();

            bool alive = false;
            for (const auto candidate : { 0.0f, 1.0f, 0.25f, 0.75f, 0.5f })
            {
                const auto snapped = param->convertTo0to1 (param->convertFrom0to1 (candidate));
                if (std::abs (snapped - current) < 0.05f)
                    continue;

                if (maxDifference (render (context, id, snapped), reference) > 1.0e-5f)
                {
                    alive = true;
                    break;
                }
            }

            ++tested;
            if (! alive)
                dead.add (id);
        }

        check (dead.isEmpty(), "every parameter changes the sound in its context; dead: " + dead.joinIntoString (", ").toStdString());
        note (std::to_string (tested) + " parameters probed");
    }

    void testUndoHistory()
    {
        auto p = makeProcessor();
        auto& undo = p->getUndoHistory();
        undo.clear();

        auto gesture = [&] (const juce::String& id, float plain)
        {
            auto* param = p->getParameterState().getParameter (id);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
            param->endChangeGesture();
        };

        gesture ("body_level", -9.0f);
        check (undo.canUndo() && undo.getNumUndoableSteps() == 1, "a gesture is one undo step");
        undo.undo();
        check (std::abs (getPlain (*p, "body_level")) < 0.01f, "undo restores the value");
        undo.redo();
        check (std::abs (getPlain (*p, "body_level") + 9.0f) < 0.01f, "redo re-applies it");

        setParam (*p, "body_level", -2.0f); // host automation: no gesture
        check (undo.getNumUndoableSteps() == 1, "host automation is not recorded");

        undo.clear();
        for (int i = 0; i < 60; ++i)
            gesture (i % 2 == 0 ? "sub_level" : "tail_level", -1.0f - (float) i * 0.5f);

        check (undo.getNumUndoableSteps() == 50, "history keeps the last 50 steps (" + std::to_string (undo.getNumUndoableSteps()) + ")");

        int undone = 0;
        while (undo.undo())
            ++undone;

        check (undone == 50, "50 steps can be undone");
        check (std::abs (getPlain (*p, "sub_level") + 5.0f) < 0.01f, "undoing 50 of 60 steps keeps steps 0-9: sub level from step 8 (-5 dB)");
        check (std::abs (getPlain (*p, "tail_level") + 5.5f) < 0.01f, "and step 9's tail level (-5.5 dB)");
    }

    void testWavExport()
    {
        auto p = makeProcessor();
        p->loadFactoryPreset (1, false);
        const auto file = makeTempFile (".wav");
        juce::String error;
        check (p->exportWav (file, 48000.0, error), "export succeeded (" + error.toStdString() + ")");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        check (reader != nullptr, "exported file reads back as audio");

        if (reader != nullptr)
        {
            const auto preview = p->renderPreview (48000.0);
            check (reader->numChannels == 2 && reader->bitsPerSample == 24 && std::abs (reader->sampleRate - 48000.0) < 0.1,
                   "24-bit stereo 48 kHz");
            check ((int) reader->lengthInSamples == preview.getNumSamples(), "length matches the rendered hit ("
                                                                                + std::to_string (reader->lengthInSamples) + " samples)");
            juce::AudioBuffer<float> b (2, (int) reader->lengthInSamples);
            reader->read (&b, 0, b.getNumSamples(), 0, true, true);
            check (maxDifference (b, preview) < 1.0e-4f, "file content equals the rendered hit (24-bit precision)");
            check (peakOf (b) > 0.2f && allFinite (b), "exported hit is loud and finite");
            note ("exported " + format (b.getNumSamples() / 48000.0, 2) + " s, peak " + format (juce::Decibels::gainToDecibels (peakOf (b)), 1) + " dBFS");
        }

        check (Processor::getDefaultExportFolder() == juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("dooblr-kick"),
               "default export folder is ~/Music/dooblr-kick");
        check (! p->exportWav (juce::File ("/nonexistent-dir-xyz/kick.wav"), 48000.0, error) && error.isNotEmpty(),
               "an unwritable location fails with a message");
        file.deleteFile();
    }

    void testRealtimeSafetyAndCpu()
    {
        const auto& presets = dooblrkick::presets::getFactoryPresets();
        double worstPercent = 0.0;

        for (int i = 0; i < (int) presets.size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);

            std::vector<NoteEvent> events;
            const auto seconds = 10.0;
            const auto total = (int) (seconds * sampleRate);
            for (int s = 0, n = 0; s < total; s += 12000, ++n) // a hit every 250 ms (each one chokes the last)
                events.push_back ({ s, juce::MidiMessage::noteOn (1, 29 + (n % 3) * 5, 1.0f) });

            renderMidi (*p, events, 24000); // warm up
            int allocations = 0;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto b = renderMidi (*p, events, total, blockSize, &allocations);
            const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
            const auto percent = 100.0 * elapsedMs / (seconds * 1000.0);
            worstPercent = std::max (worstPercent, percent);

            check (allocations == 0, presets[(size_t) i].name.toStdString() + ": no allocations in processBlock (" + std::to_string (allocations) + ")");
            check (allFinite (b), presets[(size_t) i].name.toStdString() + ": finite under fast retriggering");
            note (presets[(size_t) i].name.toStdString() + ": " + format (percent, 2) + " % of one core at 48 kHz / 512 ("
                  + format (1000.0 * elapsedMs / (total / (double) blockSize), 1) + " us per block, "
                  + format (100.0 / std::max (percent, 1.0e-9), 0) + "x real time)");
        }

        check (worstPercent < 10.0, "worst preset under 10 % of one core (" + format (worstPercent, 2) + " %)");
    }

    //==============================================================================
    void testEditor()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor created");

        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);

        // v0.3.2: a layer shows only its current source's controls, so look with every layer on
        // Synth and then on Sample.
        auto setSources = [&] (bool sample)
        {
            for (int layer = 0; layer < params::numLayers; ++layer)
                if (params::layerHasSample (layer))
                    setParam (*p, params::layerId (layer, "source"), sample ? 1.0f : 0.0f);
            editor->refreshLayerSourceLayoutNow();
        };

        // Every parameter is reachable: union of the controls visible on each page.
        std::set<juce::String> reachable;
        for (const auto sampleSources : { false, true })
        for (int page = 0; page < Editor::numPages; ++page)
        {
            setSources (sampleSources);
            editor->selectPage (page);
            check (editor->getSelectedPage() == page, "page " + std::to_string (page) + " selected");
            std::map<juce::String, juce::Component*> visible;
            collectComponents (*editor, visible, true);
            for (const auto& [id, component] : visible)
            {
                reachable.insert (id);

                // v0.3.1: processors are switched on / off by the power toggles on their tabs.
                for (const auto& [powerId, paramIds] : processorPowerParameters())
                    if (id == powerId)
                        for (const auto& paramId : paramIds)
                            reachable.insert (paramId);

                // v0.3.3: the Envelope tab's power toggle is the master envelope's on switch.
                if (id == "power_envelope")
                    reachable.insert ("menv_on");
            }

            // Nothing visible is cut off by its parent.
            std::function<void (juce::Component&)> checkBounds = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                    if (child->isVisible())
                    {
                        if (! parent.getLocalBounds().contains (child->getBounds()) && child->getComponentID().isNotEmpty())
                            check (false, "page " + std::to_string (page) + ": " + child->getComponentID().toStdString() + " is clipped");
                        checkBounds (*child);
                    }
            };
            checkBounds (*editor);
        }

        juce::StringArray missing;
        for (const auto& id : allParameterIds (*p))
            if (! isEnvelopeParam (id) && reachable.count (id) == 0)
                missing.add (id);

        check (missing.isEmpty(), "every parameter has a control: missing " + missing.joinIntoString (", ").toStdString());

        // Every control is wired: moving it changes its parameter.
        juce::StringArray unwired;
        for (const auto sampleSources : { false, true })
        for (int page = 0; page < Editor::numPages; ++page)
        {
            setSources (sampleSources);
            editor->selectPage (page);
            std::map<juce::String, juce::Component*> visible;
            collectComponents (*editor, visible, true);

            for (const auto& [id, component] : visible)
            {
                auto* param = p->getParameterState().getParameter (id);
                if (param == nullptr)
                    continue;

                const auto before = param->getValue();

                if (auto* slider = dynamic_cast<juce::Slider*> (component))
                {
                    const auto old = slider->getValue();
                    slider->setValue (slider->getMinimum() + (slider->getMaximum() - slider->getMinimum()) * (before < 0.5f ? 0.8 : 0.2), juce::sendNotificationSync);
                    if (std::abs (param->getValue() - before) < 1.0e-4f) // the value happened to be the default: try another
                        slider->setValue (slider->getMinimum() + (slider->getMaximum() - slider->getMinimum()) * 0.65, juce::sendNotificationSync);
                    if (std::abs (param->getValue() - before) < 1.0e-4f) unwired.add (id);
                    slider->setValue (old, juce::sendNotificationSync);
                }
                else if (auto* combo = dynamic_cast<juce::ComboBox*> (component))
                {
                    const auto old = combo->getSelectedItemIndex();
                    combo->setSelectedItemIndex ((old + 1) % combo->getNumItems(), juce::sendNotificationSync);
                    if (std::abs (param->getValue() - before) < 1.0e-4f) unwired.add (id);
                    combo->setSelectedItemIndex (old, juce::sendNotificationSync);
                }
                else if (auto* button = dynamic_cast<juce::Button*> (component))
                {
                    button->setToggleState (! button->getToggleState(), juce::sendNotificationSync);
                    if (std::abs (param->getValue() - before) < 1.0e-4f) unwired.add (id);
                    button->setToggleState (! button->getToggleState(), juce::sendNotificationSync);
                }

                check (std::abs (param->getValue() - before) < 1.0e-4f, "control " + id.toStdString() + " restored");
            }
        }

        setSources (false);
        check (unwired.isEmpty(), "every control moves its parameter: unwired " + unwired.joinIntoString (", ").toStdString());

        // Envelope editor: drag, add, remove and curve, each one undo step.
        using params::Envelope;
        editor->selectPage (dsp::bodyLayer);
        editor->showEnvelope (Envelope::pitch);
        auto& env = editor->getEnvelopeEditor();
        check (env.isVisible() && env.getWidth() > 200 && env.getHeight() > 100, "envelope editor visible");
        check (env.getNumNodes() == (int) getPlain (*p, params::envelopeCountId (Envelope::pitch)), "editor shows the pitch envelope's points");

        auto& undo = p->getUndoHistory();
        undo.clear();
        const auto t1 = getPlain (*p, params::nodeTimeId (Envelope::pitch, 1));
        const auto f1 = getPlain (*p, params::nodeValueId (Envelope::pitch, 1));
        const auto from = env.getNodePosition (1);
        simulateDrag (env, from, from + juce::Point<float> (6.0f, 25.0f));
        check (getPlain (*p, params::nodeTimeId (Envelope::pitch, 1)) > t1 && getPlain (*p, params::nodeValueId (Envelope::pitch, 1)) < f1,
               "dragging node 2 right/down moves it later and lower");
        check (undo.getNumUndoableSteps() == 1, "node drag is one undo step");
        undo.undo();
        check (std::abs (getPlain (*p, params::nodeTimeId (Envelope::pitch, 1)) - t1) < 0.01f, "node drag undoes");

        const auto curve = getPlain (*p, params::nodeCurveId (Envelope::pitch, 1));
        const auto handle = env.getCurveHandlePosition (1);
        simulateDrag (env, handle, handle + juce::Point<float> (0.0f, 40.0f));
        check (std::abs (getPlain (*p, params::nodeCurveId (Envelope::pitch, 1)) - curve) > 0.1f, "dragging a curve handle changes the tension");

        const auto count = env.getNumNodes();
        const auto plot = env.getPlotArea();
        const auto p0 = env.getNodePosition (count - 2), p1 = env.getNodePosition (count - 1);
        simulateDoubleClick (env, { (p0.x + p1.x) * 0.5f, plot.getY() + plot.getHeight() * 0.2f });
        check (env.getNumNodes() == count + 1 && (int) getPlain (*p, params::envelopeCountId (Envelope::pitch)) == count + 1,
               "double-click on empty space adds a point");
        simulateDoubleClick (env, env.getNodePosition (count - 1));
        check (env.getNumNodes() == count, "double-click on a point removes it");

        editor->showEnvelope (Envelope::amp);
        check (env.getEnvelope() == Envelope::amp && env.getNumNodes() == (int) getPlain (*p, params::envelopeCountId (Envelope::amp)),
               "amp envelope shown");
        const auto ampFrom = env.getNodePosition (1);
        const auto ampLevel = getPlain (*p, params::nodeValueId (Envelope::amp, 1));
        simulateDrag (env, ampFrom, ampFrom + juce::Point<float> (0.0f, 20.0f));
        check (getPlain (*p, params::nodeValueId (Envelope::amp, 1)) < ampLevel, "dragging an amp point down lowers its level");

        // Keyboard undo / redo.
        const auto steps = undo.getNumUndoableSteps();
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (undo.getNumUndoableSteps() == steps - 1, "Cmd-Z undoes");
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
        check (undo.getNumUndoableSteps() == steps, "Cmd-Shift-Z redoes");

        // Preset menu.
        auto& presetBox = editor->getPresetBox();
        bool hasAll = true;
        for (const auto& preset : dooblrkick::presets::getFactoryPresets())
        {
            bool found = false;
            for (int i = 0; i < presetBox.getNumItems(); ++i)
                found = found || presetBox.getItemText (i) == preset.name;
            hasAll = hasAll && found;
        }
        check (hasAll, "preset menu lists every factory preset");
        presetBox.setSelectedId (1 + 5, juce::sendNotificationSync);
        check (p->getPresetName() == "Trap 808-ish", "choosing a menu entry loads it (" + p->getPresetName().toStdString() + ")");

        // Waveform display.
        auto& wave = editor->getWaveformView();
        wave.refreshNow();
        check (wave.hasContent() && wave.getLengthMs() > 100.0f && wave.getPeakDb() > -12.0f,
               "waveform shows the hit (" + format (wave.getLengthMs(), 0) + " ms, " + format (wave.getPeakDb(), 1) + " dB)");

        // Background preview: parameter changes render off the message thread, latest wins.
        {
            const auto rendersBefore = wave.getNumBackgroundRenders();
            for (const auto level : { -20.0f, -10.0f, -3.0f, 0.0f, -6.0f })
            {
                setParam (*p, "output_gain", level);
                wave.pollNow();
            }
            check (wave.waitForBackgroundRender (5000), "background preview finishes");
            check (wave.getNumBackgroundRenders() > rendersBefore, "the preview was rendered on the worker ("
                                                                       + std::to_string (wave.getNumBackgroundRenders() - rendersBefore) + " renders for 5 changes)");
            const auto backgroundPeak = wave.getPeakDb(), backgroundLength = wave.getLengthMs();
            wave.refreshNow();
            check (std::abs (backgroundPeak - wave.getPeakDb()) < 0.01f && std::abs (backgroundLength - wave.getLengthMs()) < 0.5f,
                   "the shown preview is the latest settings' render (" + format (backgroundPeak, 2) + " dB, " + format (backgroundLength, 0) + " ms)");
            setParam (*p, "output_gain", 0.0f);
            wave.refreshNow();
        }

        // Resizing and snapshots.
        for (const auto [w, h] : { std::pair { 940, 600 }, std::pair { 2400, 1400 }, std::pair { 1080, 660 } })
        {
            editor->setSize (w, h);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            check (image.getWidth() == w && image.getHeight() == h, "paints at " + std::to_string (w) + "x" + std::to_string (h));
        }

        editor->setSize (1600, 1000);
        check (editor->getWidth() == 1600, "editor is resizable");
        p->loadFactoryPreset (1, false);
        editor->selectPage (dsp::bodyLayer);
        editor->showEnvelope (Envelope::pitch);
        editor->setSize (1080, 660);
        wave.refreshNow();
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.2.0");
        editor->selectPage (Editor::drivePage);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.2.0-drive");

        p->editorBeingDeleted (editor);
        base.reset();
        check (true, "editor closed");
    }

    //==============================================================================
    // v0.2.0: meter tap and the EQ / Dynamics / Limiter views.

    juce::MouseEvent makeMouseEventWithMods (juce::Component& component, juce::Point<float> position,
                                             juce::Point<float> downPosition, bool dragged, juce::ModifierKeys mods)
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods.withFlags (juce::ModifierKeys::leftButtonModifier),
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, 1, dragged };
    }

    float linearToDb (float x) { return x > 1.0e-6f ? 20.0f * std::log10 (x) : -120.0f; }

    void testMeterTap()
    {
        auto p = makeProcessor();
        auto& tap = p->getMeterTap();
        check (tap.getSamplesPerFrame() == 256, "a meter frame is 256 samples (5.3 ms) at 48 kHz (" + std::to_string (tap.getSamplesPerFrame()) + ")");
        setParam (*p, "output_gain", 0.0f);

        int allocations = 0;
        const auto out = renderMidi (*p, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 48000, blockSize, &allocations);
        check (allocations == 0, "metering adds no allocations to processBlock (" + std::to_string (allocations) + ")");

        std::vector<dsp::MeterFrame> frames (4096);
        const auto numFrames = tap.popFrames (frames.data(), (int) frames.size());
        check (numFrames == 48000 / 256, "1 s of playback gives " + std::to_string (48000 / 256) + " frames (" + std::to_string (numFrames) + ")");

        std::vector<float> spectrum (1 << 15);
        const auto numSpectrum = tap.popSpectrum (spectrum.data(), (int) spectrum.size());
        check (numSpectrum >= 32000, "spectrum samples arrive (" + std::to_string (numSpectrum) + ", FIFO holds 32767)");
        check (std::any_of (spectrum.begin(), spectrum.begin() + numSpectrum, [] (float s) { return std::abs (s) > 0.01f; }), "spectrum samples carry the kick");

        auto maxOut = -120.0f, maxIn = -120.0f;
        for (int i = 0; i < numFrames; ++i)
        {
            maxOut = std::max (maxOut, frames[(size_t) i].limiterOutDb);
            maxIn = std::max (maxIn, frames[(size_t) i].compressorInDb);
        }
        const auto outputPeakDb = linearToDb (peakOf (out));
        check (std::abs (maxOut - outputPeakDb) < 0.01f, "limiter-out meter peak equals the output peak at 0 dB output gain ("
                                                             + format (maxOut, 2) + " vs " + format (outputPeakDb, 2) + " dB)");
        check (maxIn > -24.0f, "compressor-in meter sees the hit (" + format (maxIn, 1) + " dBFS)");

        // Nothing reads the FIFO: it fills, drops new data, and processBlock still does not allocate or block.
        allocations = 0;
        renderMidi (*p, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 48000 * 12, blockSize, &allocations);
        check (allocations == 0, "a full meter FIFO does not allocate");
        const auto kept = tap.popFrames (frames.data(), (int) frames.size());
        check (kept == 2047, "a full FIFO keeps the oldest 2047 frames and drops the rest (" + std::to_string (kept) + ")");
        tap.discardAll();
        check (tap.popFrames (frames.data(), (int) frames.size()) == 0, "discardAll empties the frames");
    }

    /** The view's GR must be what the DSP applied: compare against renders with the stage bypassed. */
    void testMeterGainReductionMatchesDsp()
    {
        // Static curve.
        check (std::abs (dsp::Compressor::staticReductionDb (-40.0f, -12.0f, 4.0f)) < 1.0e-6f, "no reduction well below the threshold");
        check (std::abs (dsp::Compressor::staticReductionDb (-2.0f, -12.0f, 4.0f) - (-7.5f)) < 1.0e-4f, "10 dB over at 4:1 reduces 7.5 dB");
        check (std::abs (dsp::Compressor::staticReductionDb (-12.0f, -12.0f, 4.0f) - (-0.75f * dsp::Compressor::kneeDb / 8.0f)) < 1.0e-4f,
               "soft knee: at the threshold the reduction is (1 - 1/ratio) * knee / 8");

        struct Case { const char* name; std::vector<std::pair<const char*, float>> on, off; float tolerance; bool limiter; };
        const std::vector<Case> cases {
            { "compressor", { { "comp_on", 1 }, { "comp_threshold", -30 }, { "comp_ratio", 6 }, { "comp_makeup", 0 }, { "lim_mode", 0 } },
                            { { "comp_on", 0 }, { "lim_mode", 0 } }, 0.5f, false },
            { "limiter",    { { "comp_on", 0 }, { "lim_mode", 2 }, { "lim_ceiling", -12 } },
                            { { "comp_on", 0 }, { "lim_mode", 0 } }, 0.1f, true },
        };

        for (const auto& c : cases)
        {
            auto on = makeProcessor(), off = makeProcessor();
            for (auto* proc : { on.get(), off.get() })
                setParam (*proc, "output_gain", 0.0f);
            for (const auto& [id, v] : c.on)  setParam (*on, id, v);
            for (const auto& [id, v] : c.off) setParam (*off, id, v);

            std::vector<NoteEvent> events;
            for (int s = 0; s < 48000; s += 12000)
                events.push_back ({ s, juce::MidiMessage::noteOn (1, 36, 1.0f) });

            const auto a = renderMidi (*on, events, 48000);
            const auto b = renderMidi (*off, events, 48000);
            std::vector<dsp::MeterFrame> frames (4096);
            const auto numFrames = on->getMeterTap().popFrames (frames.data(), (int) frames.size());
            const auto spf = on->getMeterTap().getSamplesPerFrame();

            auto worst = 0.0f, deepest = 0.0f;
            int compared = 0;

            for (int f = 0; f < numFrames; ++f)
            {
                auto measured = 0.0f;
                for (int i = f * spf; i < (f + 1) * spf; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto ref = b.getSample (ch, i);
                        if (std::abs (ref) >= 1.0e-3f)
                            measured = std::min (measured, linearToDb (std::abs (a.getSample (ch, i) / ref)));
                    }

                const auto reported = c.limiter ? frames[(size_t) f].limiterGrDb : frames[(size_t) f].compressorGrDb;
                deepest = std::min (deepest, reported);

                if (reported < -0.5f || measured < -0.5f)
                {
                    worst = std::max (worst, std::abs (reported - measured));
                    ++compared;
                }
            }

            check (deepest < -3.0f, std::string (c.name) + ": the test drives real gain reduction (deepest " + format (deepest, 1) + " dB)");
            check (compared > 20 && worst < c.tolerance, std::string (c.name) + ": reported GR matches the applied gain within "
                                                             + format (c.tolerance, 1) + " dB over " + std::to_string (compared)
                                                             + " frames (worst " + format (worst, 3) + " dB)");
        }
    }

    Editor* findPageButtonsAndCheckRename (Editor& editor)
    {
        std::map<juce::String, juce::Component*> all;
        collectComponents (editor, all, false);
        auto* limiterTab = dynamic_cast<juce::Button*> (all.count ("page_Limiter") ? all["page_Limiter"] : nullptr);
        check (limiterTab != nullptr && limiterTab->getButtonText() == "Limiter", "the last tab is named Limiter");
        check (all.count ("page_Output") == 0, "no tab is named Output any more");
        check (all.count ("page_EQ") == 1 && all.count ("page_Dynamics") == 1 && all.count ("page_Drive") == 1, "the other tabs are unchanged");
        return &editor;
    }

    /** Plays hits through processBlock in host-sized blocks and polls the editor between blocks. */
    void playAndPoll (Processor& p, Editor& editor, double seconds, int hitEvery = 12000, bool audition = false)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto total = (int) (seconds * sampleRate);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::MidiBuffer midi;
            if (audition && pos == 0)
                p.triggerAudition();
            else if (! audition && hitEvery > 0)
            {
                const auto nextHit = (pos + hitEvery - 1) / hitEvery * hitEvery;
                if (nextHit < pos + blockSize)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), nextHit - pos);
            }

            buffer.clear();
            p.processBlock (buffer, midi);

            if ((pos / blockSize) % 4 == 3)
                editor.pollMetersNow();
        }

        editor.pollMetersNow();
    }

    void testOutputViews()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor created");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);
        findPageButtonsAndCheckRename (*editor);
        auto& undo = p->getUndoHistory();

        //------------------------------------------------------------------ EQ
        editor->selectPage (Editor::eqPage);
        auto& eq = editor->getEqDisplay();
        check (eq.isVisible() && eq.getWidth() > 300 && eq.getHeight() > 150, "EQ page shows the EQ view");
        check (! editor->getEnvelopeEditor().isVisible(), "the envelope editor is hidden on the EQ page");
        check (eq.getPeakHoldButton().isVisible(), "peak hold toggle is on the EQ page");

        eq.getPeakHoldButton().setToggleState (true, juce::sendNotificationSync);
        playAndPoll (*p, *editor, 0.6);
        check (eq.spectrumHasSignal() && eq.getAnalyzer().getFramesAnalysed() > 10,
               "the FFT runs during playback (" + std::to_string (eq.getAnalyzer().getFramesAnalysed()) + " frames)");
        const auto lowLevel = eq.getAnalyzer().levelAt (eq.getAnalyzer().getPeakDb(), 60.0f, 1.0f);
        const auto highLevel = eq.getAnalyzer().levelAt (eq.getAnalyzer().getPeakDb(), 15000.0f, 1.0f);
        check (lowLevel > -30.0f && lowLevel > highLevel + 10.0f, "spectrum peak hold shows a kick: lows " + format (lowLevel, 1)
                                                                    + " dBFS, 15 kHz " + format (highLevel, 1) + " dBFS");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.2.0-eq");

        // Drag the mid handle: frequency right, gain up, one undo step.
        setParam (*p, "eq_on", 0.0f);
        undo.clear();
        const auto f0 = getPlain (*p, "eq_mid_freq"), g0 = getPlain (*p, "eq_mid_gain");
        const auto from = eq.getHandlePosition (dooblrkick::ui::EqDisplay::mid);
        simulateDrag (eq, from, from + juce::Point<float> (80.0f, -40.0f));
        const auto f1 = getPlain (*p, "eq_mid_freq"), g1 = getPlain (*p, "eq_mid_gain");
        check (f1 > f0 * 1.3f && g1 > g0 + 3.0f, "dragging the Mid handle right/up raises its frequency and gain ("
                                                     + format (f0, 0) + " -> " + format (f1, 0) + " Hz, " + format (g0, 1) + " -> " + format (g1, 1) + " dB)");
        check (getPlain (*p, "eq_on") < 0.5f, "dragging a handle leaves the EQ off (only its tab toggle switches it)");
        const auto handleNow = eq.getHandlePosition (dooblrkick::ui::EqDisplay::mid);
        check (handleNow.getDistanceFrom (from + juce::Point<float> (80.0f, -40.0f)) < 1.5f, "the handle follows the mouse (and the knobs' parameters)");
        check (undo.getNumUndoableSteps() == 1, "a handle drag is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");
        undo.undo();
        check (std::abs (getPlain (*p, "eq_mid_freq") - f0) < 0.5f && std::abs (getPlain (*p, "eq_mid_gain") - g0) < 0.01f
                   && getPlain (*p, "eq_on") < 0.5f, "undo restores frequency and gain");

        // Parameters move the handle (knob -> view).
        setParam (*p, "eq_low_freq", 100.0f);
        setParam (*p, "eq_low_gain", 6.0f);
        const auto lowPos = eq.getHandlePosition (dooblrkick::ui::EqDisplay::low);
        const auto plot = eq.getPlotArea();
        check (std::abs (lowPos.y - (plot.getCentreY() - 6.0f / 24.0f * plot.getHeight() * 0.5f)) < 0.5f, "Low handle sits at its gain");

        // Low shelf drag left/down.
        undo.clear();
        simulateDrag (eq, lowPos, lowPos + juce::Point<float> (-30.0f, 30.0f));
        check (getPlain (*p, "eq_low_freq") < 100.0f && getPlain (*p, "eq_low_gain") < 6.0f && undo.getNumUndoableSteps() == 1,
               "dragging the Low handle moves its frequency and gain in one step");

        // Mid Q: mouse wheel and alt-drag; shelves ignore the wheel.
        undo.clear();
        const auto q0 = getPlain (*p, "eq_mid_q");
        const auto mid = eq.getHandlePosition (dooblrkick::ui::EqDisplay::mid);
        juce::MouseWheelDetails wheel { 0.0f, 0.25f, false, false, false };
        eq.mouseWheelMove (makeMouseEvent (eq, mid, mid, 0, false), wheel);
        const auto q1 = getPlain (*p, "eq_mid_q");
        check (std::abs (q1 / q0 - std::sqrt (2.0f)) < 0.05f, "wheel up on the Mid handle raises Q by half an octave ("
                                                                  + format (q0, 2) + " -> " + format (q1, 2) + ")");
        check (undo.getNumUndoableSteps() == 1, "a wheel step is one undo step");

        // (An instant synthetic gesture right after a wheel tick on the same parameter merges
        // with it by design, see ParameterUndoHistory::mergeWindowSeconds, so start clean.)
        undo.clear();
        const auto alt = juce::ModifierKeys (juce::ModifierKeys::altModifier);
        const auto midFreqBefore = getPlain (*p, "eq_mid_freq");
        eq.mouseDown (makeMouseEventWithMods (eq, mid, mid, false, alt));
        for (int i = 1; i <= 8; ++i)
            eq.mouseDrag (makeMouseEventWithMods (eq, mid + juce::Point<float> (0.0f, -6.0f * (float) i), mid, true, alt));
        eq.mouseUp (makeMouseEventWithMods (eq, mid + juce::Point<float> (0.0f, -48.0f), mid, true, alt));
        const auto q2 = getPlain (*p, "eq_mid_q");
        check (std::abs (q2 / q1 - 2.0f) < 0.05f && std::abs (getPlain (*p, "eq_mid_freq") - midFreqBefore) < 0.01f,
               "alt-drag up 48 px doubles Q and leaves the frequency alone (" + format (q1, 2) + " -> " + format (q2, 2) + ")");
        check (undo.getNumUndoableSteps() == 1, "the alt-drag is one undo step");

        const auto highPos = eq.getHandlePosition (dooblrkick::ui::EqDisplay::high);
        const auto stepsBefore = undo.getNumUndoableSteps();
        eq.mouseWheelMove (makeMouseEvent (eq, highPos, highPos, 0, false), wheel);
        check (undo.getNumUndoableSteps() == stepsBefore, "the shelves have no Q: wheel over them changes nothing");

        simulateDoubleClick (eq, eq.getHandlePosition (dooblrkick::ui::EqDisplay::low));
        check (std::abs (getPlain (*p, "eq_low_gain")) < 0.01f, "double-click on a handle resets its gain to 0 dB");

        eq.getPeakHoldButton().setToggleState (false, juce::sendNotificationSync);
        check (eq.getAnalyzer().getPeakDb()[100] <= -119.0f, "switching peak hold off clears it");

        //------------------------------------------------------------------ Dynamics
        editor->selectPage (Editor::dynamicsPage);
        auto& dyn = editor->getDynamicsDisplay();
        check (dyn.isVisible() && ! eq.isVisible() && ! eq.getPeakHoldButton().isVisible() && ! editor->getEnvelopeEditor().isVisible(),
               "Dynamics page shows the dynamics view only");
        setParam (*p, "comp_on", 1.0f);
        setParam (*p, "comp_threshold", -30.0f);
        setParam (*p, "comp_ratio", 6.0f);
        playAndPoll (*p, *editor, 0.6);
        check (dyn.getFramesReceived() > 80, "the dynamics view receives meter frames during playback (" + std::to_string (dyn.getFramesReceived()) + ")");
        check (dyn.getShownGainReductionDb() < -1.0f && dyn.getShownInputDb() > -30.0f, "the dot shows the hit and its reduction (in "
                   + format (dyn.getShownInputDb(), 1) + " dBFS, GR " + format (dyn.getShownGainReductionDb(), 1) + " dB)");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.2.0-dynamics");

        {
            // The transfer curve is actually drawn: above the threshold it bends to
            // T + (in - T) / R, away from the unity line.
            const auto image = dyn.createComponentSnapshot (dyn.getLocalBounds(), true, 1.0f);
            auto curvePixelNear = [&image] (juce::Point<float> at)
            {
                const auto target = dooblrkick::ui::Palette::dynamic;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const auto c = image.getPixelAt (juce::roundToInt (at.x) + dx, juce::roundToInt (at.y) + dy);
                        if (std::abs (c.getRed() - target.getRed()) + std::abs (c.getGreen() - target.getGreen())
                              + std::abs (c.getBlue() - target.getBlue()) < 60)
                            return true;
                    }
                return false;
            };
            check (curvePixelNear (dyn.transferPoint (-12.0f, -30.0f + 18.0f / 6.0f)) && curvePixelNear (dyn.transferPoint (-50.0f, -50.0f)),
                   "the compressor transfer curve is drawn (above and below the threshold)");
        }

        // Threshold line in the history.
        undo.clear();
        const auto hist = dyn.getHistoryArea().toFloat();
        const auto lineFrom = juce::Point<float> (hist.getCentreX(), dyn.getThresholdLineY());
        simulateDrag (dyn, lineFrom, lineFrom + juce::Point<float> (0.0f, -30.0f));
        const auto t1 = getPlain (*p, "comp_threshold");
        check (t1 > -30.0f + 5.0f && undo.getNumUndoableSteps() == 1, "dragging the threshold line up raises the threshold in one step ("
                                                                           + format (t1, 1) + " dB)");
        undo.undo();
        check (std::abs (getPlain (*p, "comp_threshold") + 30.0f) < 0.05f, "undo restores the threshold");

        // Threshold handle on the curve, with the compressor off: edits the threshold, leaves it off, one step.
        setParam (*p, "comp_on", 0.0f);
        undo.clear();
        const auto handle = dyn.getThresholdHandlePosition();
        simulateDrag (dyn, handle, handle + juce::Point<float> (40.0f, 0.0f));
        check (getPlain (*p, "comp_threshold") > -28.0f && getPlain (*p, "comp_on") < 0.5f && undo.getNumUndoableSteps() == 1,
               "dragging the curve's threshold handle right raises the threshold and leaves the compressor off (one step)");
        undo.undo();
        check (getPlain (*p, "comp_on") < 0.5f && std::abs (getPlain (*p, "comp_threshold") + 30.0f) < 0.05f, "undo restores the threshold");

        // Ratio: drag the curve so it passes through (T + 20, T + 2.5) -> 8:1.
        setParam (*p, "comp_on", 1.0f);
        setParam (*p, "comp_makeup", 0.0f);
        undo.clear();
        const auto threshold = getPlain (*p, "comp_threshold");
        const auto ratioNow = getPlain (*p, "comp_ratio");
        const auto onCurve = dyn.transferPoint (threshold + 20.0f, threshold + 20.0f / ratioNow);
        simulateDrag (dyn, onCurve, dyn.transferPoint (threshold + 20.0f, threshold + 2.5f));
        const auto ratio = getPlain (*p, "comp_ratio");
        check (std::abs (ratio - 8.0f) < 0.4f && undo.getNumUndoableSteps() == 1, "dragging the curve sets the ratio it passes through ("
                                                                                       + format (ratioNow, 2) + " -> " + format (ratio, 2) + ":1, want 8)");

        //------------------------------------------------------------------ Limiter
        editor->selectPage (Editor::outputPage);
        auto& lim = editor->getLimiterDisplay();
        check (lim.isVisible() && ! dyn.isVisible() && ! editor->getEnvelopeEditor().isVisible(), "Limiter page shows the limiter view only");
        setParam (*p, "comp_on", 0.0f);
        setParam (*p, "lim_mode", 2.0f);
        setParam (*p, "lim_ceiling", -9.0f);
        playAndPoll (*p, *editor, 0.6);
        check (lim.getFramesReceived() > 80, "the limiter view receives frames during playback (" + std::to_string (lim.getFramesReceived()) + ")");
        check (lim.getShownGainReductionDb() < -1.0f && std::abs (lim.getShownPeakOutDb() + 9.0f) < 0.3f,
               "limiter GR and peak readouts move (GR " + format (lim.getShownGainReductionDb(), 1) + " dB, peak "
                   + format (lim.getShownPeakOutDb(), 2) + " dBFS at a -9 dB ceiling)");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-v0.2.0-limiter");

        undo.clear();
        const auto ceilFrom = juce::Point<float> (lim.getHistoryArea().toFloat().getCentreX(), lim.getCeilingY());
        simulateDrag (lim, ceilFrom, ceilFrom + juce::Point<float> (0.0f, 40.0f));
        const auto c1 = getPlain (*p, "lim_ceiling");
        check (c1 < -12.0f && undo.getNumUndoableSteps() == 1, "dragging the ceiling line down lowers the ceiling in one step (" + format (c1, 1) + " dB)");
        undo.undo();
        check (std::abs (getPlain (*p, "lim_ceiling") + 9.0f) < 0.05f, "undo restores the ceiling");

        setParam (*p, "lim_mode", 0.0f);
        undo.clear();
        simulateDrag (lim, ceilFrom, ceilFrom + juce::Point<float> (0.0f, -20.0f));
        check (juce::roundToInt (getPlain (*p, "lim_mode")) == 0 && getPlain (*p, "lim_ceiling") > -9.0f && undo.getNumUndoableSteps() == 1,
               "with the limiter off, dragging the ceiling edits it and leaves the limiter off (one step)");
        undo.undo();
        check (juce::roundToInt (getPlain (*p, "lim_mode")) == 0 && std::abs (getPlain (*p, "lim_ceiling") + 9.0f) < 0.05f, "undo restores the ceiling");

        // Waveform-click audition animates the view too (no MIDI).
        setParam (*p, "lim_mode", 2.0f);
        lim.reset();
        const auto framesBefore = lim.getFramesReceived();
        playAndPoll (*p, *editor, 0.5, 0, true);
        check (lim.getFramesReceived() > framesBefore + 80 && lim.getShownPeakOutDb() > -20.0f,
               "audition (waveform click) feeds the view (peak " + format (lim.getShownPeakOutDb(), 1) + " dBFS)");

        // Each page paints at the size limits.
        for (const auto page : { (int) Editor::eqPage, (int) Editor::dynamicsPage, (int) Editor::outputPage })
            for (const auto [w, h] : { std::pair { 940, 600 }, std::pair { 2400, 1400 }, std::pair { 1080, 660 } })
            {
                editor->selectPage (page);
                editor->setSize (w, h);
                playAndPoll (*p, *editor, 0.1);
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                check (image.getWidth() == w && image.getHeight() == h, "page " + std::to_string (page) + " paints at " + std::to_string (w) + "x" + std::to_string (h));
            }

        // Hidden views drop meter data (the FIFO does not stay full of stale frames).
        editor->selectPage (Editor::drivePage);
        playAndPoll (*p, *editor, 0.2);
        std::vector<dsp::MeterFrame> frames (64);
        check (p->getMeterTap().popFrames (frames.data(), 64) == 0, "on other pages the editor drains the meter FIFO");

        p->editorBeingDeleted (editor);
        base.reset();
        check (true, "editor closed");
    }

    //==============================================================================
    /** Renders of the factory presets and two all-effects states. The same code ran on v0.2.0 to
        write the reference file (DOOBLRKICK_GOLDEN_WRITE=path); v0.3.0 compares against it
        (DOOBLRKICK_GOLDEN=path), so "old presets sound identical" is checked bit for bit. */
    std::vector<juce::AudioBuffer<float>> goldenRenders()
    {
        using ValueList = dooblrkick::presets::ValueList;
        std::vector<juce::AudioBuffer<float>> renders;
        const std::vector<NoteEvent> events { { 0, juce::MidiMessage::noteOn (1, 36, 0.9f) },
                                              { 9000, juce::MidiMessage::noteOn (1, 41, 0.6f) },
                                              { 15000, juce::MidiMessage::noteOff (1, 36) },
                                              { 21000, juce::MidiMessage::noteOff (1, 41) } };

        for (int i = 0; i < (int) dooblrkick::presets::getFactoryPresets().size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);
            renders.push_back (renderMidi (*p, events, 36000));
        }

        const ValueList everything { { "dist_on", 1 }, { "dist_type", 2 }, { "dist_drive", 18 }, { "dist_tone", 0.6f },
                                     { "dist_mix", 0.8f }, { "eq_on", 1 }, { "eq_low_gain", 5 }, { "eq_mid_gain", -4 },
                                     { "eq_high_gain", 3 }, { "shape_on", 1 }, { "shape_attack", 0.4f },
                                     { "shape_sustain", -0.3f }, { "comp_on", 1 }, { "comp_threshold", -20 },
                                     { "comp_makeup", 4 }, { "lim_mode", 2 }, { "lim_ceiling", -2 }, { "output_gain", -1.5f },
                                     { "gate", 1 }, { "release", 80 }, { "body_send", 0.7f }, { "tail_send", 0.4f } };
        const ValueList crushClip { { "dist_on", 1 }, { "dist_type", 4 }, { "dist_bits", 6 }, { "dist_rate", 9000 },
                                    { "eq_on", 1 }, { "eq_mid_gain", 6 }, { "lim_mode", 1 }, { "lim_ceiling", -6 },
                                    { "click_send", 1 }, { "noise_send", 1 }, { "output_gain", 2 } };

        for (const auto* values : { &everything, &crushClip })
        {
            auto p = makeProcessor();
            setValues (*p, *values);
            renders.push_back (renderMidi (*p, events, 36000));
        }

        return renders;
    }

    void testGoldenRenders()
    {
        const auto renders = goldenRenders();
        const auto writePath = juce::SystemStats::getEnvironmentVariable ("DOOBLRKICK_GOLDEN_WRITE", {});
        const auto readPath = juce::SystemStats::getEnvironmentVariable ("DOOBLRKICK_GOLDEN", {});

        if (writePath.isNotEmpty())
        {
            juce::FileOutputStream out { juce::File (writePath) };
            out.setPosition (0);
            out.truncate();
            out.writeInt ((int) renders.size());

            for (const auto& b : renders)
            {
                out.writeInt (b.getNumSamples());
                for (int ch = 0; ch < 2; ++ch)
                    out.write (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
            }

            check (out.getStatus().wasOk(), "golden file written");
            note ("wrote " + std::to_string (renders.size()) + " golden renders to " + writePath.toStdString());
        }

        if (readPath.isEmpty())
        {
            note ("DOOBLRKICK_GOLDEN not set: bit-identity against v0.2.0 not checked in this run");
            return;
        }

        juce::FileInputStream in { juce::File (readPath) };
        check (in.openedOk(), "golden file opens");
        const auto count = in.readInt();
        check (count == (int) renders.size(), "golden render count matches");
        float worst = 0.0f;
        int identical = 0;

        for (int r = 0; r < std::min (count, (int) renders.size()); ++r)
        {
            const auto n = in.readInt();
            juce::AudioBuffer<float> reference (2, n);
            for (int ch = 0; ch < 2; ++ch)
                in.read (reference.getWritePointer (ch), (int) (sizeof (float) * (size_t) n));

            const auto diff = maxDifference (renders[(size_t) r], reference);
            worst = std::max (worst, diff);
            identical += diff == 0.0f ? 1 : 0;
            // v0.3.6: the Click length and the Sub / Tail / Noise AHD became multi-point amp
            // envelopes; presets that used a Click sample with a length shape are now a 4-point
            // approximation, and node times / curves go through the envelope parameters. Bit
            // identity with v0.2.0 no longer holds, so each render is held to within -46 dBFS
            // (5e-3 peak sample difference) and its exact difference is printed.
            if (diff > 0.0f)
                note ("render " + std::to_string (r) + ": max diff vs v0.2.0 " + std::to_string (diff)
                      + " (" + format (juce::Decibels::gainToDecibels (diff, -200.0f), 1) + " dBFS)");
            check (allFinite (renders[(size_t) r]) && renders[(size_t) r].getNumSamples() == n,
                   "render " + std::to_string (r) + " is finite and as long as v0.2.0's");
            check (diff <= 5.0e-3f, "render " + std::to_string (r) + " within 5e-3 of v0.2.0 (max diff " + std::to_string (diff) + ")");
        }

        note (std::to_string (identical) + "/" + std::to_string (count) + " renders bit-identical to v0.2.0, worst diff " + std::to_string (worst));
    }

    //==============================================================================
    // v0.3.0: master envelope, dry / wet, chain order, drive view, layout.

    void linearChain (Processor& p)
    {
        for (const auto* id : { "dist_on", "eq_on", "shape_on", "comp_on", "lim_mode" })
            setParam (p, id, 0.0f);
    }

    const std::vector<NoteEvent>& twoHits()
    {
        static const std::vector<NoteEvent> events { { 0, juce::MidiMessage::noteOn (1, 36, 0.9f) },
                                                     { 9000, juce::MidiMessage::noteOn (1, 41, 0.6f) },
                                                     { 15000, juce::MidiMessage::noteOff (1, 36) },
                                                     { 21000, juce::MidiMessage::noteOff (1, 41) } };
        return events;
    }

    float exactDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
            return 1.0e9f;
        return maxDifference (a, b);
    }

    void testMasterEnvelopeDefaults()
    {
        using params::Envelope;
        auto p = makeProcessor();
        check (getPlain (*p, "menv_on") > 0.5f && (int) getPlain (*p, params::envelopeCountId (Envelope::master)) == 2,
               "master envelope defaults: on, 2 points");
        check (getPlain (*p, params::nodeValueId (Envelope::master, 0)) == 1.0f && getPlain (*p, params::nodeValueId (Envelope::master, 1)) == 1.0f,
               "both default points are at 100 %");
        check (getPlain (*p, "mix_drywet") == 1.0f && (int) getPlain (*p, "chain_order") == 0, "Dry/Wet defaults to 100 % wet, chain to Drive > EQ > Dynamics > Limiter");
        check (p->getParameterState().getParameter ("chain_order")->getCurrentValueAsText() == "Drive > EQ > Dynamics > Limiter",
               "chain order label (" + p->getParameterState().getParameter ("chain_order")->getCurrentValueAsText().toStdString() + ")");

        // Default = transparent: identical to the envelope switched off, for the default and every factory preset.
        int identical = 0, presets = (int) dooblrkick::presets::getFactoryPresets().size();
        for (int i = -1; i < presets; ++i)
        {
            auto a = makeProcessor();
            auto b = makeProcessor();
            if (i >= 0)
            {
                a->loadFactoryPreset (i, false);
                b->loadFactoryPreset (i, false);
            }
            setParam (*b, "menv_on", 0.0f);
            if (exactDifference (renderMidi (*a, twoHits(), 36000), renderMidi (*b, twoHits(), 36000)) == 0.0f)
                ++identical;
        }
        check (identical == presets + 1, "default master envelope is bit-identical to bypass (init + "
                                             + std::to_string (presets) + " presets: " + std::to_string (identical) + " identical)");

        // More points, all still at 100 %: still transparent.
        auto a = makeProcessor();
        auto b = makeProcessor();
        setParam (*b, params::envelopeCountId (Envelope::master), 6.0f);
        setParam (*b, params::nodeCurveId (Envelope::master, 2), 0.7f);
        check (exactDifference (renderMidi (*a, twoHits(), 36000), renderMidi (*b, twoHits(), 36000)) == 0.0f,
               "a flat 6-point envelope with a curved segment is still bit-identical");

        // Old (version 3) states load with the new controls at their transparent defaults.
        auto writer = makeProcessor();
        writer->loadFactoryPreset (3, false);
        juce::MemoryBlock state;
        writer->getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("stateVersion", 3, nullptr);
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            const auto id = tree.getChild (i).getProperty ("id").toString();
            if (id.startsWith ("menv") || id == "mix_drywet" || id == "chain_order")
                tree.removeChild (i, nullptr);
        }
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldState);
        auto q = makeProcessor();
        setParam (*q, "chain_order", 9.0f);
        setParam (*q, "mix_drywet", 0.25f);
        setParam (*q, params::nodeValueId (Envelope::master, 1), 0.0f);
        q->setStateInformation (oldState.getData(), (int) oldState.getSize());
        check ((int) getPlain (*q, "chain_order") == 0 && getPlain (*q, "mix_drywet") == 1.0f
                   && getPlain (*q, params::nodeValueId (Envelope::master, 1)) == 1.0f,
               "a version 3 state resets chain order, Dry/Wet and the master envelope to their defaults");
        check (exactDifference (renderMidi (*writer, twoHits(), 36000), renderMidi (*q, twoHits(), 36000)) == 0.0f,
               "and sounds bit-identical to the processor that saved it");
        juce::MemoryBlock resaved;
        q->getStateInformation (resaved);
        auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        check (resavedXml != nullptr && resavedXml->getIntAttribute ("stateVersion") == 6, "states are written as version 6");
    }

    void testMasterEnvelopeShapes()
    {
        using params::Envelope;
        auto make = [] (bool shaped)
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 60.0f, 1500.0f);
            setParam (*p, "body_level", -6.0f);
            if (shaped)
            {
                // 100 % -> 50 % at 200 ms (linear) -> 0 % at 400 ms.
                setParam (*p, params::envelopeCountId (Envelope::master), 3.0f);
                setParam (*p, params::nodeTimeId (Envelope::master, 1), 200.0f);
                setParam (*p, params::nodeValueId (Envelope::master, 1), 0.5f);
                setParam (*p, params::nodeCurveId (Envelope::master, 0), 0.0f);
                setParam (*p, params::nodeTimeId (Envelope::master, 2), 400.0f);
                setParam (*p, params::nodeValueId (Envelope::master, 2), 0.0f);
                setParam (*p, params::nodeCurveId (Envelope::master, 1), 0.0f);
            }
            return renderNote (*p, 36, 1.0f, 0.8);
        };

        const auto flat = make (false), shaped = make (true);
        auto expected = [] (double ms) { return ms < 200.0 ? 1.0 - 0.5 * ms / 200.0 : ms < 400.0 ? 0.5 * (400.0 - ms) / 200.0 : 0.0; };
        double worst = 0.0;
        int compared = 0;
        for (int i = 0; i < (int) (0.39 * sampleRate); ++i)
        {
            const auto x = flat.getSample (0, i);
            if (std::abs (x) < 0.02f)
                continue;
            worst = std::max (worst, std::abs (shaped.getSample (0, i) / x - expected (1000.0 * i / sampleRate)));
            ++compared;
        }
        check (compared > 5000 && worst < 0.01, "output / unshaped = the drawn envelope within 1 % over " + std::to_string (compared)
                                                    + " samples (worst " + format (worst, 4) + ")");
        check (peakOf (shaped, (int) (0.401 * sampleRate)) < 1.0e-6f, "nothing after the envelope's final 0 % point (peak "
                                                                         + format (linearToDb (peakOf (shaped, (int) (0.401 * sampleRate))), 1) + " dB)");
        check (peakOf (flat, (int) (0.45 * sampleRate), (int) (0.6 * sampleRate)) > 0.1f, "the unshaped body is still sounding there");

        // Off = bypass even with a shape drawn.
        auto p = makeProcessor();
        isolateLayer (*p, dsp::bodyLayer);
        flatBody (*p, 60.0f, 1500.0f);
        setParam (*p, "body_level", -6.0f);
        setParam (*p, params::nodeValueId (Envelope::master, 1), 0.0f);
        setParam (*p, "menv_on", 0.0f);
        check (exactDifference (renderNote (*p, 36, 1.0f, 0.8), flat) == 0.0f, "Envelope off bypasses a drawn shape");

        // It shapes the processed sum: with the chain on, the envelope still scales the layers before it.
        auto withFx = [] (float level)
        {
            auto q = makeProcessor();
            setParam (*q, params::nodeValueId (Envelope::master, 0), level);
            setParam (*q, params::nodeValueId (Envelope::master, 1), level);
            setParam (*q, "lim_mode", 0.0f);
            setParam (*q, "comp_on", 0.0f);
            setParam (*q, "dist_on", 0.0f);
            setParam (*q, "shape_on", 0.0f);
            return renderNote (*q, 36, 1.0f, 0.5);
        };
        const auto full = withFx (1.0f), half = withFx (0.5f);
        check (std::abs (linearToDb (rmsOf (half) / rmsOf (full)) + 6.02f) < 0.05f, "a flat 50 % envelope is -6.02 dB on the summed kick ("
                                                                                       + format (linearToDb (rmsOf (half) / rmsOf (full)), 3) + " dB)");
    }

    void testDryWet()
    {
        using ValueList = dooblrkick::presets::ValueList;
        const ValueList fx { { "dist_on", 1 }, { "dist_type", 2 }, { "dist_drive", 20 }, { "eq_on", 1 }, { "eq_low_gain", 6 },
                             { "eq_high_gain", -5 }, { "shape_on", 1 }, { "shape_attack", 0.5f }, { "comp_on", 1 },
                             { "comp_threshold", -24 }, { "lim_mode", 2 }, { "lim_ceiling", -4 }, { "body_send", 0.6f } };

        auto render = [&] (bool effects, float mix, bool setMix = true)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (1, false);
            setValues (*p, fx);
            if (! effects)
                linearChain (*p); // same layers and sends, every stage off
            if (setMix)
                setParam (*p, "mix_drywet", mix);
            return renderMidi (*p, twoHits(), 36000);
        };

        const auto dryReference = render (false, 1.0f), processed = render (true, 1.0f, false);
        const auto dry = render (true, 0.0f), wet = render (true, 1.0f), half = render (true, 0.5f);
        check (exactDifference (dry, dryReference) == 0.0f, "Dry/Wet 0 % = the layers with every stage off (bit-identical)");
        check (exactDifference (wet, processed) == 0.0f, "Dry/Wet 100 % = the processed chain (bit-identical)");
        check (maxDifference (dry, wet) > 0.05f, "the processed and dry signals really differ (max " + format (maxDifference (dry, wet), 3) + ")");

        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < half.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (half.getSample (ch, i) - 0.5f * (dry.getSample (ch, i) + wet.getSample (ch, i))));
        check (worst < 1.0e-5f, "Dry/Wet 50 % = 0.5 dry + 0.5 wet: an equal-gain crossfade (max error " + format (worst, 8) + ")");

        // A change mid-note ramps (no step).
        auto p = makeProcessor();
        p->loadFactoryPreset (1, false);
        setValues (*p, fx);
        juce::AudioBuffer<float> buffer (2, blockSize), out (2, 9 * blockSize);
        for (int b = 0; b < 9; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
            if (b == 4)
                setParam (*p, "mix_drywet", 0.0f);
            buffer.clear();
            p->processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * blockSize, buffer, ch, 0, blockSize);
        }
        float stepAt = 0.0f, stepBefore = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            const auto d = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));
            auto& target = (i >= 4 * blockSize - 2 && i < 4 * blockSize + 64) ? stepAt : stepBefore;
            target = std::max (target, d);
        }
        check (stepAt <= stepBefore * 1.05f, "switching 100 % -> 0 % mid-note makes no larger sample step than the kick itself ("
                                                 + format (stepAt, 4) + " vs " + format (stepBefore, 4) + ")");
    }

    /** The chain applied offline with fresh effect objects, in 32-sample blocks like the engine. */
    juce::AudioBuffer<float> offlineChain (const juce::AudioBuffer<float>& input, const dsp::KickSettings& s, const dsp::ChainOrder& order)
    {
        dsp::Distortion distortion;
        dsp::ThreeBandEq eq;
        dsp::TransientShaper shaper;
        dsp::Compressor compressor;
        dsp::OutputStage limiter;
        distortion.prepare (sampleRate, 32);
        eq.prepare (sampleRate);
        shaper.prepare (sampleRate);
        compressor.prepare (sampleRate);
        limiter.prepare (sampleRate);
        auto limiterSettings = s.output;
        limiterSettings.outputDb = 0.0f;

        juce::AudioBuffer<float> out (input);
        juce::AudioBuffer<float> block (2, 32);

        for (int start = 0; start < out.getNumSamples(); start += 32)
        {
            const auto n = std::min (32, out.getNumSamples() - start);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, out, ch, start, n);

            for (const auto stage : order)
            {
                if (stage == dsp::driveStage)         distortion.process (block, 0, n, s.drive);
                else if (stage == dsp::eqStage)       eq.process (block, 0, n, s.eq);
                else if (stage == dsp::dynamicsStage) { shaper.process (block, 0, n, s.shaper); compressor.process (block, 0, n, s.compressor); }
                else                                  limiter.process (block, 0, n, limiterSettings);
            }

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, start, block, ch, 0, n);
        }

        return out;
    }

    void testChainReorder()
    {
        using ValueList = dooblrkick::presets::ValueList;
        ValueList fx { { "dist_on", 1 }, { "dist_type", 0 }, { "dist_drive", 18 }, { "dist_tone", 0.7f }, { "eq_on", 1 },
                       { "eq_low_gain", 6 }, { "eq_mid_gain", -5 }, { "eq_high_gain", 4 }, { "shape_on", 1 },
                       { "shape_attack", 0.6f }, { "shape_sustain", -0.4f }, { "comp_on", 1 }, { "comp_threshold", -26 },
                       { "comp_makeup", 3 }, { "lim_mode", 1 }, { "lim_ceiling", -3 } };
        // Every layer fully through the Drive send, so the whole kick goes through every stage in order.
        for (int l = 0; l < params::numLayers; ++l)
            fx.push_back ({ params::layerId (l, "send"), 1.0f });

        auto dryOf = [&]
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (1, false);
            setValues (*p, fx);
            setParam (*p, "mix_drywet", 0.0f);
            return renderMidi (*p, twoHits(), 36000);
        };
        const auto dry = dryOf();

        float worst = 0.0f;
        std::vector<juce::AudioBuffer<float>> outputs;
        dsp::KickSettings settings;
        for (int index = 0; index < dsp::numChainOrders; ++index)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (1, false);
            setValues (*p, fx);
            setParam (*p, "chain_order", (float) index);
            settings = params::SettingsReader (p->getParameterState()).read();
            const auto order = dsp::chainOrderFromIndex (index);
            const auto label = p->getParameterState().getParameter ("chain_order")->getCurrentValueAsText();
            const auto expectLabel = juce::String (dsp::chainStageName (order[0])) + " > " + dsp::chainStageName (order[1]) + " > "
                                   + dsp::chainStageName (order[2]) + " > " + dsp::chainStageName (order[3]);
            check (label == expectLabel, "order " + std::to_string (index) + " label is " + expectLabel.toStdString() + " (" + label.toStdString() + ")");
            check (settings.master.chainOrder == index, "the order reaches the engine settings");
            outputs.push_back (renderMidi (*p, twoHits(), 36000));
            worst = std::max (worst, maxDifference (outputs.back(), offlineChain (dry, settings, order)));
        }
        check (worst < 1.0e-4f, "all 24 orders match an offline chain of fresh effects in that order (worst " + format (worst, 7) + ")");

        int distinct = 0;
        for (int i = 1; i < dsp::numChainOrders; ++i)
            distinct += maxDifference (outputs[(size_t) i], outputs[0]) > 1.0e-3f ? 1 : 0;
        check (distinct == dsp::numChainOrders - 1, "every other order sounds different from the default (" + std::to_string (distinct) + " of 23)");
        const auto limiterFirst = dsp::chainOrderToIndex ({ dsp::limiterStage, dsp::driveStage, dsp::eqStage, dsp::dynamicsStage });
        check (peakOf (outputs[0]) <= juce::Decibels::decibelsToGain (-3.0f) + 1.0e-4f,
               "Limiter last: the output respects the -3 dB ceiling (" + format (linearToDb (peakOf (outputs[0])), 2) + " dB)");
        check (peakOf (outputs[(size_t) limiterFirst]) > juce::Decibels::decibelsToGain (-3.0f) + 0.01f,
               "Limiter first: EQ after it can exceed the ceiling, as moved (" + format (linearToDb (peakOf (outputs[(size_t) limiterFirst])), 2) + " dB)");

        // With partial sends (the factory default routing) the default order is unchanged from v0.2.0: see the golden test.
        // Switching while a hit rings crossfades over 15 ms: no click, real-time safe, ends on the new order.
        auto p = makeProcessor();
        p->loadFactoryPreset (1, false);
        setValues (*p, fx);
        const auto newOrder = dsp::chainOrderToIndex ({ dsp::eqStage, dsp::limiterStage, dsp::dynamicsStage, dsp::driveStage });
        juce::AudioBuffer<float> buffer (2, blockSize), out (2, 24 * blockSize);
        int allocations = 0;
        for (int b = 0; b < 24; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
            if (b == 6)
                setParam (*p, "chain_order", (float) newOrder);
            buffer.clear();
            allocationCount.store (0);
            countAllocations.store (true);
            p->processBlock (buffer, midi);
            countAllocations.store (false);
            allocations += allocationCount.load();
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * blockSize, buffer, ch, 0, blockSize);
        }
        check (allocations == 0, "reordering while playing allocates nothing (" + std::to_string (allocations) + " allocations)");

        auto maxStep = [&] (int from, int to)
        {
            float m = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = std::max (1, from); i < to; ++i)
                    m = std::max (m, std::abs (out.getSample (ch, i) - out.getSample (ch, i - 1)));
            return m;
        };
        const auto switchAt = 6 * blockSize;
        const auto stepDuring = maxStep (switchAt - 4, switchAt + 800), stepAround = std::max (maxStep (switchAt - 2000, switchAt - 4), maxStep (switchAt + 800, switchAt + 3000));
        check (stepDuring <= stepAround * 1.25f, "the reorder crossfade makes no click: largest sample step " + format (stepDuring, 4)
                                                     + " vs " + format (stepAround, 4) + " around it");
        auto other = makeProcessor();
        other->loadFactoryPreset (1, false);
        setValues (*other, fx);
        setParam (*other, "chain_order", (float) newOrder);
        const auto reference = renderMidi (*other, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 24 * blockSize);
        const auto lateStart = switchAt + 4800;
        float lateDiff = 0.0f, oldDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = lateStart; i < out.getNumSamples(); ++i)
            {
                lateDiff = std::max (lateDiff, std::abs (out.getSample (ch, i) - reference.getSample (ch, i)));
                oldDiff = std::max (oldDiff, std::abs (reference.getSample (ch, i)));
            }
        check (lateDiff < 0.1f * oldDiff, "100 ms after the switch the output follows the new order (max diff "
                                              + format (lateDiff, 4) + " vs level " + format (oldDiff, 3) + ")");
    }

    void testChainOrderStateAndUndo()
    {
        auto p = makeProcessor();
        auto& undo = p->getUndoHistory();
        undo.clear();
        auto* param = p->getParameterState().getParameter ("chain_order");
        param->beginChangeGesture();
        for (const auto index : { 3, 7, 11 })
            param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
        check ((int) getPlain (*p, "chain_order") == 11 && undo.getNumUndoableSteps() == 1, "a reorder gesture is one undo step");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check ((int) getPlain (*q, "chain_order") == 11, "the order survives a state save / load");
        check (exactDifference (renderMidi (*p, twoHits(), 30000), renderMidi (*q, twoHits(), 30000)) == 0.0f, "and sounds identical");

        // User presets carry it too.
        const auto file = makeTempFile (".dkpreset");
        juce::String error;
        check (p->saveUserPreset (file, error), "user preset saved");
        auto r = makeProcessor();
        check (r->loadUserPreset (file, error) && (int) getPlain (*r, "chain_order") == 11, "a user preset restores the order");
        file.deleteFile();
        r->loadFactoryPreset (0, false);
        check ((int) getPlain (*r, "chain_order") == 0, "a factory preset returns to the default order");

        undo.undo();
        check ((int) getPlain (*p, "chain_order") == 0, "undo restores the previous order");
        undo.redo();
        check ((int) getPlain (*p, "chain_order") == 11, "redo re-applies it");

        setParam (*p, "chain_order", 5.0f); // host automation
        check (undo.getNumUndoableSteps() == 1 && (int) getPlain (*p, "chain_order") == 5, "automating the order works and is not an undo step");
        check (p->getParameterState().getParameter ("chain_order")->isAutomatable(), "the order is automatable");
    }

    struct EditorHolder
    {
        std::unique_ptr<Processor> processor = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base { processor->createEditorAndMakeActive() };
        Editor* editor = dynamic_cast<Editor*> (base.get());

        ~EditorHolder()
        {
            if (editor != nullptr)
                processor->editorBeingDeleted (editor);
            base.reset();
        }
    };

    /** Drags a chain tab by its handle so its centre lands on slot toSlot (editor coordinates move with the tab). */
    void dragChainTab (Editor& editor, int stage, int toSlot, bool release = true, float overshoot = 0.0f)
    {
        auto& button = editor.getPageButton (Editor::pageForStage (stage));
        const auto shown = editor.getShownChainOrder();
        const auto& targetButton = editor.getPageButton (Editor::pageForStage (shown[(size_t) toSlot]));
        const auto handle = button.getHandleArea().toFloat().getCentre();
        const auto start = button.getPosition().toFloat() + handle;
        const auto dy = targetButton.getBounds().toFloat().getCentreY() - button.getBounds().toFloat().getCentreY() + overshoot;

        button.mouseDown (makeMouseEvent (button, handle, handle, 1, false));
        constexpr int steps = 16;
        for (int i = 1; i <= steps; ++i)
        {
            const auto at = start + juce::Point<float> (0.0f, dy * (float) i / (float) steps);
            button.mouseDrag (makeMouseEvent (button, at - button.getPosition().toFloat(), handle, 1, true));
        }
        if (release)
            button.mouseUp (makeMouseEvent (button, start + juce::Point<float> (0.0f, dy) - button.getPosition().toFloat(), handle, 1, true));
    }

    std::string orderText (const dsp::ChainOrder& o)
    {
        std::string s;
        for (const auto stage : o)
            s += std::string (s.empty() ? "" : " > ") + dsp::chainStageName (stage);
        return s;
    }

    void testChainDragAndDrop()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        auto& undo = p.getUndoHistory();
        undo.clear();

        // Tabs show their routing slot and a handle.
        bool numbered = true;
        for (int stage = 0; stage < dsp::numChainStages; ++stage)
        {
            auto& b = editor.getPageButton (Editor::pageForStage (stage));
            numbered = numbered && b.getChainSlot() == stage && ! b.getHandleArea().isEmpty() && b.getHandleArea().getRight() <= b.getWidth();
        }
        check (numbered, "the four processing tabs carry slot numbers 1-4 and a handle");
        check (editor.getPageButton (Editor::envelopePage).getChainSlot() < 0 && editor.getPageButton (0).getChainSlot() < 0,
               "the Envelope and layer tabs are not part of the chain (no handle)");

        // Drag EQ (slot 2) below Limiter (slot 4).
        const auto eqY = editor.getPageButton (Editor::eqPage).getY();
        dragChainTab (editor, dsp::eqStage, 3, false);
        check (editor.getPageButton (Editor::eqPage).getY() > eqY + 40, "while dragging, the EQ tab follows the mouse");
        check (editor.getShownChainOrder() == dsp::ChainOrder { dsp::driveStage, dsp::dynamicsStage, dsp::limiterStage, dsp::eqStage },
               "the other tabs make room during the drag: " + orderText (editor.getShownChainOrder()));
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.0-chain-dragging");
        {
            auto& b = editor.getPageButton (Editor::eqPage);
            b.mouseUp (makeMouseEvent (b, b.getHandleArea().toFloat().getCentre(), b.getHandleArea().toFloat().getCentre(), 1, true));
        }
        const auto expected = dsp::chainOrderToIndex ({ dsp::driveStage, dsp::dynamicsStage, dsp::limiterStage, dsp::eqStage });
        check ((int) getPlain (p, "chain_order") == expected, "dropping sets the order parameter: "
                                                                 + p.getParameterState().getParameter ("chain_order")->getCurrentValueAsText().toStdString());
        check (undo.getNumUndoableSteps() == 1, "the whole drag is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");
        check (editor.getPageButton (Editor::eqPage).getChainSlot() == 3 && editor.getPageButton (Editor::outputPage).getChainSlot() == 2
                   && editor.getPageButton (Editor::dynamicsPage).getChainSlot() == 1,
               "the tabs are renumbered: Drive 1, Dynamics 2, Limiter 3, EQ 4");
        bool stacked = true;
        for (int slot = 1; slot < dsp::numChainStages; ++slot)
        {
            const auto order = editor.getShownChainOrder();
            stacked = stacked && editor.getPageButton (Editor::pageForStage (order[(size_t) slot])).getY()
                                     > editor.getPageButton (Editor::pageForStage (order[(size_t) slot - 1])).getBottom();
        }
        check (stacked, "after the drop the tabs sit in slot order, top to bottom");

        // Drag Limiter to the top, far past the first slot (clamped).
        const auto firstSlotY = editor.getPageButton (Editor::drivePage).getY();
        dragChainTab (editor, dsp::limiterStage, 0, true, -300.0f);
        check (editor.getShownChainOrder() == dsp::ChainOrder { dsp::limiterStage, dsp::driveStage, dsp::dynamicsStage, dsp::eqStage },
               "Limiter can move first: " + orderText (editor.getShownChainOrder()));
        check (editor.getPageButton (Editor::outputPage).getY() == firstSlotY
                   && editor.getPageButton (Editor::outputPage).getY() > editor.getPageButton (Editor::envelopePage).getBottom(),
               "an overshoot stays inside the chain's slots");
        check (undo.getNumUndoableSteps() == 2, "second drag: second undo step");

        // Drive can move too.
        dragChainTab (editor, dsp::driveStage, 3);
        check (editor.getShownChainOrder() == dsp::ChainOrder { dsp::limiterStage, dsp::dynamicsStage, dsp::eqStage, dsp::driveStage },
               "Drive can move last: " + orderText (editor.getShownChainOrder()));

        // Undo / automation move the tabs.
        undo.undo();
        editor.syncChainOrderNow();
        check (editor.getShownChainOrder() == dsp::ChainOrder { dsp::limiterStage, dsp::driveStage, dsp::dynamicsStage, dsp::eqStage },
               "undo puts the tabs back: " + orderText (editor.getShownChainOrder()));
        setParam (p, "chain_order", 0.0f);
        editor.syncChainOrderNow();
        check (editor.getShownChainOrder() == dsp::ChainOrder { 0, 1, 2, 3 } && editor.getPageButton (Editor::drivePage).getChainSlot() == 0,
               "automation / presets move the tabs");

        // A click on the tab body (not the handle) still selects the page and does not reorder.
        auto& dyn = editor.getPageButton (Editor::dynamicsPage);
        const auto steps = undo.getNumUndoableSteps();
        const juce::Point<float> body { 30.0f, (float) dyn.getHeight() * 0.5f };
        check (! dyn.getHandleArea().toFloat().contains (body), "the tab body is outside the handle");
        dyn.mouseDown (makeMouseEvent (dyn, body, body, 1, false));
        dyn.mouseDrag (makeMouseEvent (dyn, body + juce::Point<float> (0.0f, 90.0f), body, 1, true));
        dyn.mouseUp (makeMouseEvent (dyn, body + juce::Point<float> (0.0f, 90.0f), body, 1, true));
        check ((int) getPlain (p, "chain_order") == 0 && undo.getNumUndoableSteps() == steps, "dragging the tab body does not reorder");
        dyn.setToggleState (true, juce::sendNotificationSync); // what a click on the radio tab does
        check (editor.getSelectedPage() == Editor::dynamicsPage, "clicking a chain tab selects its page");

        // The chain-order menu on the Envelope page is the same parameter.
        editor.selectPage (Editor::envelopePage);
        std::map<juce::String, juce::Component*> visible;
        collectComponents (editor, visible, true);
        auto* combo = dynamic_cast<juce::ComboBox*> (visible.count ("chain_order") ? visible["chain_order"] : nullptr);
        check (combo != nullptr && combo->getNumItems() == 24, "the Envelope page has a 24-entry chain order menu");
        if (combo != nullptr)
        {
            combo->setSelectedItemIndex (expected, juce::sendNotificationSync);
            editor.syncChainOrderNow();
            check ((int) getPlain (p, "chain_order") == expected && editor.getShownChainOrder()[3] == dsp::eqStage, "choosing from the menu reorders the tabs");
            const auto textWidth = juce::GlyphArrangement::getStringWidth (combo->getLookAndFeel().getComboBoxFont (*combo), combo->getText());
            check ((float) combo->getWidth() >= textWidth + 16.0f, "the menu is wide enough for its labels (" + std::to_string (combo->getWidth())
                                                                     + " px for " + format (textWidth, 0) + " px of text)");
        }

        editor.selectPage (Editor::dynamicsPage);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.0-chain");
    }

    // v0.3.3 ----------------------------------------------------------------------------------

    std::vector<float> allPlainValues (Processor& p)
    {
        std::vector<float> values;
        for (auto* param : p.getParameters())
            values.push_back (param->getValue());
        return values;
    }

    void testMasterEnvelopeLongRange()
    {
        using params::Envelope;
        auto p = makeProcessor();
        auto& state = p->getParameterState();
        bool masterRange = true, otherRange = true;
        for (int n = 1; n < params::maxNodes; ++n) // node 0 is fixed at 0 ms (no time parameter)
        {
            masterRange = masterRange && state.getParameter (params::nodeTimeId (Envelope::master, n))->getNormalisableRange().end == 6000.0f;
            otherRange = otherRange && state.getParameter (params::nodeTimeId (Envelope::amp, n))->getNormalisableRange().end == 2000.0f
                         && state.getParameter (params::nodeTimeId (Envelope::pitch, n))->getNormalisableRange().end == 2000.0f;
        }
        check (masterRange, "master envelope points reach 6000 ms");
        check (otherRange, "Body pitch / amp envelope points still reach 2000 ms");

        // A 6 s master envelope over a long Tail renders to its end and stops there.
        setLayerAhd (*p, dsp::tailLayer, 30.0f, 2000.0f, 3970.0f); // the Tail's amp envelope reaches 6000 ms too
        setValues (*p, { { "tail_on", 1.0f },
                         { params::envelopeCountId (Envelope::master), 3.0f },
                         { params::nodeValueId (Envelope::master, 0), 1.0f },
                         { params::nodeTimeId (Envelope::master, 1), 3000.0f }, { params::nodeValueId (Envelope::master, 1), 1.0f },
                         { params::nodeTimeId (Envelope::master, 2), 6000.0f }, { params::nodeValueId (Envelope::master, 2), 0.0f } });
        check (std::abs (getPlain (*p, params::nodeTimeId (Envelope::master, 2)) - 6000.0f) < 0.5f, "a master point can be set to 6000 ms");
        const auto settings = p->getCurrentSettings();
        const auto length = dsp::KickEngine::getRenderLengthSeconds (settings);
        const auto render = dsp::KickEngine::renderOneShot (settings, sampleRate, 36, 1.0f);
        const auto at = [] (double seconds) { return (int) (seconds * sampleRate); };
        check (length >= 6.0 && length <= 30.0 && render.getNumSamples() >= at (6.0), "the render covers the whole 6 s envelope ("
                                                                                          + format (length, 2) + " s, " + format (render.getNumSamples() / sampleRate, 2) + " s rendered)");
        check (allFinite (render) && peakOf (render, at (5.5), at (5.9)) > 1.0e-4f, "still sounding at 5.5 - 5.9 s ("
                                                                                     + format (juce::Decibels::gainToDecibels (peakOf (render, at (5.5), at (5.9)), -200.0f), 1) + " dB)");
        check (peakOf (render, at (6.01)) < 1.0e-4f, "and silent after the envelope's 6 s end point ("
                                                      + format (juce::Decibels::gainToDecibels (peakOf (render, at (6.01)), -200.0f), 1) + " dB)");

        // The live voice too.
        const auto live = renderNote (*p, 36, 1.0f, 6.5);
        check (allFinite (live) && peakOf (live, at (5.5), at (5.9)) > 1.0e-4f && peakOf (live, at (6.05)) < 1.0e-4f,
               "the played note follows the 6 s envelope as well");
    }

    void testEnvelopeZoom()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        using params::Envelope;
        using EE = dooblrkick::ui::EnvelopeEditor;
        editor.setSize (1080, 660);
        auto& undo = p.getUndoHistory();

        const auto exercise = [&] (EE& env, const std::string& name, float maxMs)
        {
            check (env.getMaxTimeMs() == maxMs, name + ": zoom range up to " + format (maxMs, 0) + " ms");
            env.resetView();
            const auto plot = env.getPlotArea();
            const auto span0 = env.getViewSpanMs();
            check (env.getViewStartMs() == 0.0f && ! env.isViewZoomed() && span0 >= 100.0f && span0 <= maxMs, name + ": starts fitted (0 - " + format (span0, 0) + " ms)");

            // What zooms and what does not.
            const auto node = env.getNodePosition (env.getNumNodes() - 1);
            const auto handle = env.getCurveHandlePosition (0);
            juce::Point<float> empty, onLine;
            bool foundEmpty = false, foundLine = false;
            for (float fy = 0.05f; fy < 0.96f && ! (foundEmpty && foundLine); fy += 0.01f)
                for (float fx = 0.1f; fx < 0.9f; fx += 0.05f)
                {
                    const juce::Point<float> q { plot.getX() + plot.getWidth() * fx, plot.getY() + plot.getHeight() * fy };
                    if (! foundEmpty && env.isZoomAreaAt (q) && q.getDistanceFrom (node) > 30.0f && q.getDistanceFrom (handle) > 30.0f)
                        empty = q, foundEmpty = true;
                }
            // A point on the drawn curve: halfway between the first two points, at the curve's height.
            {
                // The middle of the non-zooming band around the curve at that x.
                const auto a = env.getNodePosition (env.getNumNodes() - 2), b = env.getNodePosition (env.getNumNodes() - 1);
                const auto x = a.x + (b.x - a.x) * 0.3f;
                float first = -1.0f, last = -1.0f;
                for (float y = plot.getY(); y <= plot.getBottom(); y += 0.5f)
                {
                    const juce::Point<float> q { x, y };
                    if (! env.isZoomAreaAt (q) && q.getDistanceFrom (env.getCurveHandlePosition (env.getNumNodes() - 2)) > 12.0f)
                    {
                        if (first < 0.0f) first = y;
                        last = y;
                    }
                    else if (first >= 0.0f)
                        break;
                }
                if (first >= 0.0f)
                    onLine = { x, (first + last) * 0.5f }, foundLine = true;
            }
            check (foundEmpty, name + ": empty plot space found");
            check (! env.isZoomAreaAt (node) && ! env.isZoomAreaAt (handle), name + ": points and curve handles do not zoom");
            check (foundLine && ! env.isZoomAreaAt (onLine + juce::Point<float> (0.0f, 3.0f)) && ! env.isZoomAreaAt (onLine + juce::Point<float> (0.0f, -3.0f)),
                   name + ": the curve (+/- a few px) does not zoom");
            if (! env.isZoomAreaAt (onLine + juce::Point<float> (0.0f, onLine.y < plot.getCentreY() ? 25.0f : -25.0f)))
            {
                juce::String nodes;
                for (int n = 0; n < env.getNumNodes(); ++n)
                    nodes << env.getNodePosition (n).toString() << " / " << env.getCurveHandlePosition (juce::jmin (n, env.getNumNodes() - 2)).toString() << "; ";
                note ("  " + name + ": on line " + onLine.toString().toStdString() + ", plot " + plot.toString().toStdString() + ", nodes / handles " + nodes.toStdString());
            }
            check (env.isZoomAreaAt (onLine + juce::Point<float> (0.0f, onLine.y < plot.getCentreY() ? 25.0f : -25.0f)), name + ": just clear of the curve does");

            env.mouseMove (makeMouseEvent (env, empty, empty, 0, false));
            const auto emptyCursor = env.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
            env.mouseMove (makeMouseEvent (env, node, node, 0, false));
            const auto nodeCursor = env.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::DraggingHandCursor);
            check (emptyCursor && nodeCursor, name + ": move (zoom / pan) cursor over empty space, hand over a point");

            // Drag up: zoom in around the mouse; view only.
            const auto before = allPlainValues (p);
            undo.clear();
            const auto anchorX = plot.getX() + plot.getWidth() * 0.25f;
            const auto anchorMs = env.getViewStartMs() + 0.25f * span0;
            simulateDrag (env, { anchorX, empty.y }, { anchorX, empty.y - 120.0f });
            check (env.isViewZoomed() && std::abs (env.getViewSpanMs() - span0 * 0.5f) < span0 * 0.01f,
                   name + ": dragging up 120 px halves the span (" + format (span0, 0) + " -> " + format (env.getViewSpanMs(), 1) + " ms)");
            check (std::abs (env.getViewStartMs() + 0.25f * env.getViewSpanMs() - anchorMs) < span0 * 0.002f, name + ": anchored at the mouse");
            check (allPlainValues (p) == before && undo.getNumUndoableSteps() == 0, name + ": no parameter change, no undo step");

            // Clamping (each drag starts on empty space at the current zoom).
            const auto zoomPoint = [&] (float fromY, float stepY)
            {
                for (float y = fromY; y >= plot.getY() && y <= plot.getBottom(); y += stepY)
                    for (float fx : { 0.25f, 0.5f, 0.75f, 0.1f, 0.9f })
                        if (const juce::Point<float> q { plot.getX() + plot.getWidth() * fx, y }; env.isZoomAreaAt (q))
                            return q;
                return empty;
            };
            const auto low = zoomPoint (plot.getBottom() - 4.0f, -2.0f);
            simulateDrag (env, low, { low.x, low.y - 3000.0f });
            check (env.getViewSpanMs() == EE::minViewMs, name + ": zoom in stops at 20 ms (" + format (env.getViewSpanMs(), 2) + ")");
            const auto deepLabel = env.getViewStartMs();
            const auto high = zoomPoint (plot.getY() + 4.0f, 2.0f);
            simulateDrag (env, high, { high.x, high.y + 3000.0f });
            check (env.getViewSpanMs() == maxMs && env.getViewStartMs() == 0.0f, name + ": zoom out stops at the full " + format (maxMs, 0) + " ms ("
                                                                                     + format (env.getViewSpanMs(), 0) + ", from " + format (deepLabel, 1) + " ms)");
            check (allPlainValues (p) == before && undo.getNumUndoableSteps() == 0, name + ": still no parameter change or undo step");

            // Dragging on the curve neither zooms nor edits.
            env.resetView();
            simulateDrag (env, onLine, onLine + juce::Point<float> (0.0f, -60.0f));
            check (! env.isViewZoomed() && allPlainValues (p) == before, name + ": a drag on the curve line does nothing");

            // Points still drag while zoomed (zoom anchored under the last point keeps it in place).
            const auto last = env.getNumNodes() - 1;
            const auto timeId = params::nodeTimeId (env.getEnvelope(), last);
            const auto lastPos = env.getNodePosition (last);
            const juce::Point<float> below { lastPos.x, lastPos.y < plot.getCentreY() ? plot.getBottom() - 6.0f : plot.getY() + 6.0f };
            check (env.isZoomAreaAt (below), name + ": space under / over the last point zooms");
            simulateDrag (env, below, below + juce::Point<float> (0.0f, -120.0f));
            check (env.isViewZoomed() && env.getNodePosition (last).getDistanceFrom (lastPos) < 1.0f, name + ": the point under the mouse stays put");
            const auto t0 = getPlain (p, timeId);
            const auto pixelMs = env.getViewSpanMs() / plot.getWidth();
            undo.clear();
            simulateDrag (env, env.getNodePosition (last), env.getNodePosition (last) + juce::Point<float> (-40.0f, 0.0f));
            check (std::abs ((t0 - getPlain (p, timeId)) - 40.0f * pixelMs) < 3.0f * pixelMs + 0.5f && undo.getNumUndoableSteps() == 1,
                   name + ": a point drag still edits in the zoomed scale (" + format (t0, 1) + " -> " + format (getPlain (p, timeId), 1) + " ms, one undo step)");
            undo.undo();

            // Reset: double-click the time ruler; double-click empty space still adds a point.
            simulateDoubleClick (env, { env.getRulerArea().getCentreX(), env.getRulerArea().getBottom() - 2.0f });
            check (! env.isViewZoomed() && env.getViewStartMs() == 0.0f && std::abs (env.getViewSpanMs() - span0) < 0.5f,
                   name + ": double-clicking the time ruler fits the view again");
        };

        editor.selectPage (Editor::envelopePage);
        exercise (editor.getMasterEnvelopeEditor(), "master", 6000.0f);
        editor.selectPage (dsp::bodyLayer);
        auto& body = editor.getEnvelopeEditor();
        for (const auto e : { Envelope::pitch, Envelope::amp })
        {
            body.setEnvelope (e);
            exercise (body, e == Envelope::pitch ? "body pitch" : "body amp", 2000.0f);
        }

        // Snapshot: Body amp envelope zoomed into its first 100 ms.
        body.setEnvelope (Envelope::amp);
        editor.selectPage (dsp::bodyLayer);
        {
            const auto plot = body.getPlotArea();
            const juce::Point<float> from { plot.getX() + 2.0f, plot.getBottom() - 4.0f };
            const auto octaves = std::log2 (body.getViewSpanMs() / 100.0f);
            if (body.isZoomAreaAt (from))
                simulateDrag (body, from, from + juce::Point<float> (0.0f, -120.0f * octaves));
            check (body.isViewZoomed() && std::abs (body.getViewSpanMs() - 100.0f) < 1.0f, "body amp zoomed to 100 ms for the snapshot ("
                                                                                             + format (body.getViewSpanMs(), 1) + " ms)");
            saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.3-layer-env-zoom");
        }

        // Snapshot: a long master envelope, zoomed out to the full 6 s.
        editor.selectPage (Editor::envelopePage);
        auto& master = editor.getMasterEnvelopeEditor();
        setValues (p, { { params::envelopeCountId (Envelope::master), 5.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 400.0f }, { params::nodeValueId (Envelope::master, 1), 0.7f },
                        { params::nodeTimeId (Envelope::master, 2), 2200.0f }, { params::nodeValueId (Envelope::master, 2), 0.55f },
                        { params::nodeTimeId (Envelope::master, 3), 4200.0f }, { params::nodeValueId (Envelope::master, 3), 0.3f },
                        { params::nodeTimeId (Envelope::master, 4), 5400.0f }, { params::nodeValueId (Envelope::master, 4), 0.0f } });
        setValues (p, { { params::nodeCurveId (Envelope::master, 0), 0.5f }, { params::nodeCurveId (Envelope::master, 3), -0.4f } });
        master.resetView();
        {
            const auto plot = master.getPlotArea();
            juce::Point<float> from;
            bool found = false;
            for (float fx = 0.05f; fx < 0.95f && ! found; fx += 0.05f)
            {
                const juce::Point<float> q { plot.getX() + plot.getWidth() * fx, plot.getBottom() - 4.0f };
                if (master.isZoomAreaAt (q))
                    from = q, found = true;
            }
            simulateDrag (master, from, from + juce::Point<float> (0.0f, 400.0f));
            check (found && master.getViewSpanMs() == 6000.0f, "master zoomed out to 6000 ms for the snapshot");
        }
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.3-envelope");
    }

    void testEnvelopePowerToggle()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        using params::Envelope;
        editor.setSize (1080, 660);
        editor.refreshPowerButtonsNow();

        auto& tab = editor.getPageButton (Editor::envelopePage);
        auto& power = editor.getEnvelopePowerButton();
        const auto b = power.getBounds();
        check (getPlain (p, "menv_on") > 0.5f && power.getToggleState() && tab.isProcessorOn(), "on by default (menv_on unchanged)");
        check (power.getComponentID() == "power_envelope" && power.getParentComponent() == &tab && power.isVisible()
                   && tab.getLocalBounds().contains (b) && b.getWidth() >= 18 && b.getHeight() >= 18 && tab.getComponentAt (b.getCentre()) == &power,
               "the Envelope tab has a round power toggle (inside the tab, hit first)");

        auto& undo = p.getUndoHistory();
        editor.selectPage (dsp::bodyLayer);
        undo.clear();
        power.setToggleState (false, juce::sendNotificationSync); // what a click does (ButtonAttachment)
        editor.refreshPowerButtonsNow();
        check (getPlain (p, "menv_on") < 0.5f && ! tab.isProcessorOn(), "the toggle switches the master envelope off; the tab dims");
        check (editor.getSelectedPage() == dsp::bodyLayer && ! tab.getToggleState(), "toggling does not select the tab");
        check (undo.getNumUndoableSteps() == 1, "one undo step");
        check (editor.getMasterEnvelopeEditor().isBypassedLook(), "the envelope graph draws dimmed while off");

        // Still editable while off.
        editor.selectPage (Editor::envelopePage);
        auto& env = editor.getMasterEnvelopeEditor();
        const auto from = env.getNodePosition (1);
        simulateDrag (env, from, from + juce::Point<float> (-40.0f, 60.0f));
        check (getPlain (p, params::nodeValueId (Envelope::master, 1)) < 0.95f, "the graph is still editable while off");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.3-envelope-off");

        undo.clear();
        setParam (p, "menv_on", 1.0f);
        editor.refreshPowerButtonsNow();
        check (power.getToggleState() && tab.isProcessorOn() && ! env.isBypassedLook(), "parameter -> toggle: on again, undimmed");

        // Off = bypassed at unity: identical to the default flat envelope.
        const auto shaped = [] (Processor& q)
        {
            setValues (q, { { params::envelopeCountId (Envelope::master), 3.0f },
                            { params::nodeTimeId (Envelope::master, 1), 80.0f }, { params::nodeValueId (Envelope::master, 1), 0.3f },
                            { params::nodeTimeId (Envelope::master, 2), 300.0f }, { params::nodeValueId (Envelope::master, 2), 0.0f } });
        };
        auto flat = makeProcessor();
        auto off = makeProcessor();
        auto on = makeProcessor();
        shaped (*off);
        shaped (*on);
        setParam (*off, "menv_on", 0.0f);
        const auto rf = renderMidi (*flat, twoHits(), 36000), ro = renderMidi (*off, twoHits(), 36000), rn = renderMidi (*on, twoHits(), 36000);
        check (exactDifference (rf, ro) == 0.0f, "off with a shaped envelope renders bit-identical to the default flat envelope");
        check (exactDifference (rf, rn) > 0.05f, "and on, the same envelope shapes the hit");
    }

    //==============================================================================
    // v0.3.4: the envelope graphs carry no help / status text: no grey caption pixels inside the
    // plot (away from the points and curve handles); the ruler and axis labels sit outside it.
    //==============================================================================
    // v0.3.4: envelope pan, axis lock and the animated fit.
    juce::Point<float> findEmptyPoint (dooblrkick::ui::EnvelopeEditor& env)
    {
        const auto plot = env.getPlotArea();
        for (float y = plot.getY() + 6.0f; y < plot.getCentreY(); y += 4.0f)
            for (float x = plot.getRight() - 30.0f; x > plot.getCentreX(); x -= 4.0f)
                if (env.isZoomAreaAt ({ x, y }) && env.isZoomAreaAt ({ x - 120.0f, y }) && env.isZoomAreaAt ({ x, y - 4.0f }))
                    return { x, y };
        return plot.getCentre();
    }

    void testEnvelopePanAndFit()
    {
        using params::Envelope;
        using EE = dooblrkick::ui::EnvelopeEditor;
        using Axis = EE::DragAxis;

        // Axis lock.
        check (EE::decideDragAxis ({ 2.0f, 1.0f }) == Axis::undecided, "axis: undecided under the threshold");
        check (EE::decideDragAxis ({ 10.0f, 3.0f }) == Axis::pan && EE::decideDragAxis ({ -10.0f, 9.0f }) == Axis::pan, "axis: mostly horizontal pans");
        check (EE::decideDragAxis ({ 3.0f, -10.0f }) == Axis::zoom && EE::decideDragAxis ({ 5.0f, 5.0f }) == Axis::zoom, "axis: mostly vertical (or a tie) zooms");

        // Clamp and fit math.
        check (EE::clampViewStart (-10.0f, 100.0f, 6000.0f) == 0.0f && EE::clampViewStart (5950.0f, 100.0f, 6000.0f) == 5900.0f
                   && EE::clampViewStart (300.0f, 100.0f, 6000.0f) == 300.0f && EE::clampViewStart (50.0f, 6000.0f, 6000.0f) == 0.0f,
               "pan clamps the view to 0 .. max");
        check (std::abs (EE::fitSpanFor (700.0f, 6000.0f) - 760.0f) < 0.01f && EE::fitSpanFor (10.0f, 6000.0f) == 50.0f
                   && EE::fitSpanFor (6000.0f, 6000.0f) == 6000.0f && EE::fitSpanFor (590.0f, 600.0f) == 600.0f,
               "fit: last point + 8 % + 4 ms, at least 50 ms, at most the range");

        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        setValues (p, { { params::envelopeCountId (Envelope::master), 4.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 60.0f }, { params::nodeValueId (Envelope::master, 1), 0.8f },
                        { params::nodeTimeId (Envelope::master, 2), 250.0f }, { params::nodeValueId (Envelope::master, 2), 0.4f },
                        { params::nodeTimeId (Envelope::master, 3), 700.0f }, { params::nodeValueId (Envelope::master, 3), 0.0f },
                        { "menv_on", 1.0f }, { "menv_tscale", 1.0f }, { "menv_depth", 1.0f } });
        editor.selectPage (Editor::envelopePage);
        auto& env = editor.getMasterEnvelopeEditor();
        env.checkForChanges();
        env.resetView();
        const auto maxMs = env.getMaxTimeMs();
        check (env.getViewStartMs() == 0.0f && std::abs (env.getViewSpanMs() - 760.0f) < 0.5f && ! env.isViewZoomed(),
               "master fits 0 .. 700 ms + margin (" + format (env.getViewSpanMs(), 1) + " ms)");

        // Zoom in (vertical), then pan (horizontal): the span stays, the start follows the mouse.
        auto from = findEmptyPoint (env);
        simulateDrag (env, from, from + juce::Point<float> (0.0f, 120.0f * std::log2 (0.5f)));
        const auto span = env.getViewSpanMs();
        check (env.isViewZoomed() && std::abs (span - 380.0f) < 4.0f, "vertical drag still zooms (span " + format (span, 1) + ")");
        const auto start0 = env.getViewStartMs();
        const auto msPerPixel = span / env.getPlotArea().getWidth();
        from = findEmptyPoint (env);
        env.mouseDown (makeMouseEvent (env, from, from, 1, false));
        env.mouseDrag (makeMouseEvent (env, from + juce::Point<float> (-30.0f, 0.0f), from, 1, true));
        const auto panCursor = env.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::DraggingHandCursor);
        env.mouseDrag (makeMouseEvent (env, from + juce::Point<float> (-80.0f, 6.0f), from, 1, true));
        env.mouseUp (makeMouseEvent (env, from + juce::Point<float> (-80.0f, 6.0f), from, 1, true));
        check (panCursor, "the hand (grab) cursor while panning");
        check (std::abs (env.getViewSpanMs() - span) < 0.01f && std::abs (env.getViewStartMs() - EE::clampViewStart (start0 + 80.0f * msPerPixel, span, maxMs)) < 0.05f,
               "dragging left 80 px pans later by 80 px of time (" + format (start0, 1) + " -> " + format (env.getViewStartMs(), 1) + " ms), span kept");

        // Mostly horizontal with some vertical: still a pan, no zoom.
        from = findEmptyPoint (env);
        simulateDrag (env, from, from + juce::Point<float> (40.0f, 15.0f));
        check (std::abs (env.getViewSpanMs() - span) < 0.01f, "a mostly horizontal drag doesn't zoom");

        // Clamping both ways.
        from = findEmptyPoint (env);
        simulateDrag (env, from, from + juce::Point<float> (4000.0f, 0.0f));
        check (env.getViewStartMs() == 0.0f, "panning stops at 0 ms");
        for (int i = 0; i < 40 && env.getViewStartMs() < maxMs - span - 0.01f; ++i)
        {
            from = findEmptyPoint (env);
            simulateDrag (env, from, from + juce::Point<float> (-4000.0f, 0.0f));
        }
        check (std::abs (env.getViewStartMs() - (maxMs - span)) < 0.01f, "panning stops at the end of the range (" + format (env.getViewStartMs(), 1) + " ms)");

        // Wheel: horizontal pans, plain vertical is left alone; a pinch zooms.
        env.resetView();
        simulateDrag (env, findEmptyPoint (env), findEmptyPoint (env) + juce::Point<float> (0.0f, -120.0f));
        env.panBy (100.0f);
        const auto beforeWheel = env.getViewStartMs();
        juce::MouseWheelDetails wheel {};
        wheel.deltaX = -0.1f;
        const auto inside = env.getPlotArea().getCentre();
        env.mouseWheelMove (makeMouseEvent (env, inside, inside, 0, false), wheel);
        check (std::abs (env.getViewStartMs() - (beforeWheel + 0.1f * env.getViewSpanMs())) < 0.05f, "a horizontal swipe pans");
        wheel.deltaX = 0.0f;
        wheel.deltaY = 0.2f;
        const auto beforeVertical = juce::Point<float> (env.getViewStartMs(), env.getViewSpanMs());
        env.mouseWheelMove (makeMouseEvent (env, inside, inside, 0, false), wheel);
        check (beforeVertical == juce::Point<float> (env.getViewStartMs(), env.getViewSpanMs()), "a plain vertical wheel doesn't move the view");
        const auto spanBeforePinch = env.getViewSpanMs();
        env.mouseMagnify (makeMouseEvent (env, inside, inside, 0, false), 2.0f);
        check (std::abs (env.getViewSpanMs() - spanBeforePinch * 0.5f) < 0.05f, "a pinch (x2) halves the span");
        const auto cmdEvent = makeMouseEventWithMods (env, inside, inside, false, juce::ModifierKeys (juce::ModifierKeys::commandModifier));
        const auto spanBeforeCmd = env.getViewSpanMs();
        env.mouseWheelMove (cmdEvent, wheel);
        check (env.getViewSpanMs() < spanBeforeCmd * 0.9f, "Cmd + wheel zooms in");

        // Auto-fit suspended while zoomed / panned, back after the ruler reset.
        setValues (p, { { params::nodeTimeId (Envelope::master, 3), 1500.0f } });
        const auto manual = juce::Point<float> (env.getViewStartMs(), env.getViewSpanMs());
        env.checkForChanges();
        check (manual == juce::Point<float> (env.getViewStartMs(), env.getViewSpanMs()), "a manual view suspends the auto-fit");
        simulateDoubleClick (env, env.getRulerArea().getCentre());
        check (! env.isViewZoomed() && env.getViewStartMs() == 0.0f && std::abs (env.getViewSpanMs() - EE::fitSpanFor (1500.0f, maxMs)) < 0.5f,
               "double-clicking the ruler fits again (0 - " + format (env.getViewSpanMs(), 0) + " ms)");
        setValues (p, { { params::nodeTimeId (Envelope::master, 3), 900.0f } });
        env.checkForChanges();
        check (std::abs (env.getViewSpanMs() - EE::fitSpanFor (900.0f, maxMs)) < 0.5f, "auto-fit follows a new extent (point moved by a parameter)");
        setValues (p, { { "menv_tscale", 2.0f } });
        env.checkForChanges();
        check (std::abs (env.getViewSpanMs() - EE::fitSpanFor (env.getEffectiveEnvelope().getEndTimeMs(), maxMs)) < 0.5f
                   && env.getEffectiveEnvelope().getEndTimeMs() > 1700.0f,
               "auto-fit follows Time Scale (" + format (env.getViewSpanMs(), 0) + " ms)");
        setValues (p, { { "menv_tscale", 1.0f } });
        env.checkForChanges();

        // The animation: eased, converging, inside the range on the way.
        env.setAnimateOffscreen (true);
        const auto fromSpan = env.getViewSpanMs();
        setValues (p, { { params::nodeTimeId (Envelope::master, 3), 2500.0f } });
        env.checkForChanges();
        const auto target = EE::fitSpanFor (2500.0f, maxMs);
        check (env.isViewAnimating() && std::abs (env.getViewTarget().y - target) < 0.5f && std::abs (env.getViewSpanMs() - fromSpan) < 0.01f,
               "a new extent starts an animation (not a jump)");
        env.advanceViewAnimation (1000.0);
        auto previous = env.getViewSpanMs();
        bool monotonic = true, inRange = true;
        for (double t = 1020.0; t <= 1180.0; t += 20.0)
        {
            env.advanceViewAnimation (t);
            monotonic = monotonic && env.getViewSpanMs() >= previous;
            inRange = inRange && env.getViewStartMs() >= 0.0f && env.getViewStartMs() + env.getViewSpanMs() <= maxMs + 0.01f;
            previous = env.getViewSpanMs();
        }
        check (monotonic && inRange && env.isViewAnimating() && previous < target, "it moves steadily towards the fit, inside 0 .. max");
        env.advanceViewAnimation (1000.0 + EE::fitAnimationMs);
        check (! env.isViewAnimating() && std::abs (env.getViewSpanMs() - target) < 0.01f && env.getViewStartMs() == 0.0f,
               "and lands exactly on it after " + format ((float) EE::fitAnimationMs, 0) + " ms");
        // An ease-out: more than half way by a third of the time.
        setValues (p, { { params::nodeTimeId (Envelope::master, 3), 700.0f } });
        env.checkForChanges();
        env.advanceViewAnimation (2000.0);
        env.advanceViewAnimation (2000.0 + EE::fitAnimationMs / 3.0);
        check ((target - env.getViewSpanMs()) / (target - EE::fitSpanFor (700.0f, maxMs)) > 0.55f, "eased out (fast start, soft landing)");
        env.advanceViewAnimation (3000.0);
        env.setAnimateOffscreen (false);

        // Dragging a point past the edge keeps it in view; the refit waits for mouse-up.
        env.resetView();
        const auto viewEnd = env.getViewSpanMs();
        const auto last = env.getNumNodes() - 1;
        const auto nodePos = env.getNodePosition (last);
        env.mouseDown (makeMouseEvent (env, nodePos, nodePos, 1, false));
        const auto outside = juce::Point<float> (env.getPlotArea().getRight() + 60.0f, nodePos.y);
        env.mouseDrag (makeMouseEvent (env, outside, nodePos, 1, true));
        const auto dragged = getPlain (p, params::nodeTimeId (Envelope::master, last));
        const auto visible = env.getNodePosition (last).x <= env.getPlotArea().getRight() + 0.5f;
        env.mouseUp (makeMouseEvent (env, outside, nodePos, 1, true));
        check (dragged > viewEnd && visible, "a point dragged past the edge stays in view (" + format (dragged, 0) + " ms)");
        check (std::abs (env.getViewSpanMs() - EE::fitSpanFor (dragged, maxMs)) < 0.5f, "mouse-up fits the new extent");

        // The Body graphs share it.
        editor.selectPage (1);
        editor.showEnvelope (Envelope::pitch);
        auto& body = editor.getEnvelopeEditor();
        body.resetView();
        from = findEmptyPoint (body);
        simulateDrag (body, from, from + juce::Point<float> (0.0f, -120.0f));
        const auto bodyStart = body.getViewStartMs();
        from = findEmptyPoint (body);
        simulateDrag (body, from, from + juce::Point<float> (-50.0f, 0.0f));
        check (body.isViewZoomed() && body.getViewStartMs() > bodyStart, "Body pitch: zoom, then a horizontal drag pans");
        body.resetView();
        check (! body.isViewZoomed() && body.getViewStartMs() == 0.0f, "Body pitch: the ruler reset fits again");
    }

    //==============================================================================
    // v0.3.5: the playhead dims with the level it plays.
    //==============================================================================
    // v0.3.5: Key Track on every layer, in Synth and Sample mode.
    void testKeyTrackSwitches()
    {
        const auto ratio = [] (double a, double b) { return b > 0.0 ? a / b : 0.0; };
        const auto semitone = std::pow (2.0, 1.0 / 12.0);
        // Each compared hit starts from a quiet engine, so the previous hit's choke fade does not overlap it.
        const auto fresh = [] (Processor& proc, int n, double secs) { proc.releaseResources(); return renderNote (proc, n, 1.0f, secs); };

        // Names, defaults.
        {
            auto p = makeProcessor();
            bool named = true, off = true;
            for (int l = 0; l < params::numLayers; ++l)
            {
                auto* param = getParam (*p, params::layerId (l, "keytrack"));
                named = named && param != nullptr && param->getName (64) == juce::String (params::layerNames[(size_t) l]) + " Key Track";
                off = off && param != nullptr && param->getValue() < 0.5f;
            }
            check (named && off, "each layer has an automatable \"<Layer> Key Track\" parameter, off by default");
        }

        // Body synth: on tracks by the semitone relative to the Key (29); off ignores the note.
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::bodyLayer);
            flatBody (*p, 43.65f, 3000.0f);
            setParam (*p, "body_level", 0.0f);
            const auto offRoot = fresh (*p, 29, 0.6), offUp = fresh (*p, 41, 0.6);
            check (maxDifference (offRoot, offUp) == 0.0f, "Body synth, Key Track off: note +12 renders bit-identically");
            setParam (*p, "body_keytrack", 1.0f);
            const auto root = measureFrequency (renderNote (*p, 29, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            const auto up = measureFrequency (renderNote (*p, 41, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            const auto one = measureFrequency (renderNote (*p, 30, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            const auto down = measureFrequency (renderNote (*p, 22, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            check (std::abs (root - 43.65) < 0.5 && std::abs (ratio (up, root) - 2.0) < 0.01 && std::abs (ratio (one, root) - semitone) < 0.004
                       && std::abs (ratio (down, root) - std::pow (2.0, -7.0 / 12.0)) < 0.005,
                   "Body synth, Key Track on: Key " + format (root, 2) + " Hz, +1 st " + format (one, 2) + ", +12 st " + format (up, 2) + ", -7 st " + format (down, 2));
        }

        // Sub and Tail synth.
        for (const auto layer : { dsp::subLayer, dsp::tailLayer })
        {
            auto p = makeProcessor();
            isolateLayer (*p, layer);
            const auto id = [&] (const char* s) { return params::layerId (layer, s); };
            setParam (*p, id ("level"), 0.0f);
            setLayerAhd (*p, layer, 0.0f, 600.0f, 1000.0f);
            if (layer == dsp::subLayer)
                setParam (*p, "sub_envamt", 0.0f);
            else
            {
                setParam (*p, "tail_wave", 0.0f);
                setParam (*p, "tail_spread", 0.0f);
            }
            const auto name = std::string (params::layerNames[(size_t) layer]);
            check (maxDifference (fresh (*p, 29, 0.5), fresh (*p, 36, 0.5)) == 0.0f, name + " synth, Key Track off: the note is ignored");
            setParam (*p, id ("keytrack"), 1.0f);
            const auto root = measureFrequency (renderNote (*p, 29, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            const auto up = measureFrequency (renderNote (*p, 36, 1.0f, 0.6), 48000.0, 0.1, 0.5);
            check (root > 10.0 && std::abs (ratio (up, root) - std::pow (2.0, 7.0 / 12.0)) < 0.01,
                   name + " synth, Key Track on: +7 st plays " + format (ratio (up, root), 4) + "x (" + format (root, 2) + " -> " + format (up, 2) + " Hz)");
        }

        // Click synth: on changes the sound with the note, off doesn't.
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::clickLayer);
            setParam (*p, "click_level", 0.0f);
            check (maxDifference (fresh (*p, 29, 0.2), fresh (*p, 41, 0.2)) == 0.0f, "Click synth, Key Track off: the note is ignored");
            setParam (*p, "click_keytrack", 1.0f);
            check (maxDifference (renderNote (*p, 29, 1.0f, 0.2), renderNote (*p, 41, 1.0f, 0.2)) > 0.01f, "Click synth, Key Track on: the note changes the click");
        }

        // Noise: no pitch in Synth mode; the sample follows the note in Sample mode.
        {
            auto p = makeProcessor();
            isolateLayer (*p, dsp::noiseLayer);
            setParam (*p, "noise_level", 0.0f);
            setParam (*p, "noise_keytrack", 1.0f);
            const auto a = renderNote (*p, 29, 1.0f, 0.3), b = renderNote (*p, 41, 1.0f, 0.3);
            check (rmsOf (a) > 1.0e-3f && std::abs (rmsOf (a) / rmsOf (b) - 1.0f) < 0.1f, "Noise synth: the note doesn't change the level (the synth noise has no pitch)");
            setParam (*p, "noise_source", 1.0f);
            setParam (*p, "noise_keytrack", 0.0f);
            check (maxDifference (fresh (*p, 29, 0.3), fresh (*p, 41, 0.3)) == 0.0f, "Noise sample, Key Track off: the note is ignored");
            setParam (*p, "noise_keytrack", 1.0f);
            const auto rootLength = lastAudible (renderNote (*p, 29, 1.0f, 1.0)), upLength = lastAudible (renderNote (*p, 41, 1.0f, 1.0));
            check (maxDifference (renderNote (*p, 29, 1.0f, 0.3), renderNote (*p, 41, 1.0f, 0.3)) > 0.01f && upLength < rootLength,
                   "Noise sample, Key Track on: the note re-pitches the sample (" + std::to_string (rootLength) + " -> " + std::to_string (upLength) + " samples)");
        }

        // The editor: a vertical switch under each layer's fader, bound, undoable, and the Noise one disabled in Synth mode.
        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);

        for (int l = 0; l < params::numLayers; ++l)
        {
            editor.selectPage (l);
            auto& key = editor.getKeyTrackSwitch (l);
            const auto name = std::string (params::layerNames[(size_t) l]);
            const auto b = key.getBounds();
            check (key.isVisible() && b.getHeight() > b.getWidth() && key.getHeight() >= 50,
                   name + ": the Key switch is shown, vertical, medium sized (" + b.toString().toStdString() + ")");

            p.getUndoHistory().clear();
            key.setToggleState (true, juce::sendNotificationSync);
            const auto on = getPlain (p, params::layerId (l, "keytrack")) > 0.5f;
            p.getUndoHistory().undo();
            check (on && getPlain (p, params::layerId (l, "keytrack")) < 0.5f && ! key.getToggleState(),
                   name + ": the switch sets Key Track and undo clears it");
        }

        setParam (p, "noise_source", 0.0f);
        editor.selectPage (dsp::noiseLayer);
        const auto synthDisabled = ! editor.getKeyTrackSwitch (dsp::noiseLayer).isEnabled();
        setParam (p, "noise_source", 1.0f);
        editor.selectPage (dsp::noiseLayer);
        check (synthDisabled && editor.getKeyTrackSwitch (dsp::noiseLayer).isEnabled(), "Noise Key switch: disabled in Synth mode, enabled in Sample mode");
        setParam (p, "noise_source", 0.0f);

        auto& key = editor.getKeyTrackSwitch (dsp::bodyLayer);
        editor.selectPage (dsp::bodyLayer);
        setParam (p, "body_keytrack", 1.0f);
        for (int i = 0; i < 40; ++i)
            key.advanceAnimation (1000.0 + i * 10.0);
        check (key.getThumbPosition() > 0.99f, "the Key switch thumb moves up when on");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-keytrack");
        setParam (p, "body_keytrack", 0.0f);
        for (int i = 0; i < 40; ++i)
            key.advanceAnimation (2000.0 + i * 10.0);
        check (key.getThumbPosition() < 0.01f, "and back down when off");
    }

    //==============================================================================
    // v0.3.5 fix: each layer page shows its own envelope. v0.3.6: every layer page has its own
    // Pitch and Amp envelopes, picked with the switch and remembered per layer.
    void testLayerEnvelopes()
    {
        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        auto& env = editor.getEnvelopeEditor();
        auto& toggle = editor.getEnvelopeToggle();
        editor.setSize (1080, 660);
        using params::Envelope;

        // Distinct shapes per layer, so a wrong binding shows up in the point count / positions.
        setLayerAhd (p, dsp::subLayer, 3, 80, 420);
        setLayerAhd (p, dsp::tailLayer, 12, 30, 900);
        setLayerAhd (p, dsp::noiseLayer, 1, 5, 150);
        setClickLength (p, 18);
        setEnvelopeNodes (p, Envelope::clickPitch, { { 0, 12, 0.5f }, { 9, 0, 0 } });
        setEnvelopeNodes (p, Envelope::subPitch, { { 0, 7, 0.3f }, { 40, 2, 0 }, { 300, 0, 0 } });
        setEnvelopeNodes (p, Envelope::tailPitch, { { 0, -5, 0 }, { 120, 3, 0.2f }, { 500, 1, 0 }, { 800, 0, 0 } });
        setEnvelopeNodes (p, Envelope::noisePitch, { { 0, -24, 0.4f }, { 60, 0, 0 } });

        const auto showFor = [&] (int layer, bool amp)
        {
            editor.selectPage (layer);
            editor.showEnvelope (params::layerEnvelope (layer, amp));
        };

        // Three per-layer view choices: all Pitch, all Amp, and mixed (odd layers on Amp).
        int orders = 0, wrong = 0;
        for (const auto mode : { 0, 1, 2 })
        {
            const auto wantsAmp = [mode] (int layer) { return mode == 1 || (mode == 2 && layer % 2 == 1); };
            for (int l = 0; l < params::numLayers; ++l)
                showFor (l, wantsAmp (l));

            std::vector<int> order { 0, 1, 2, 3, 4 };
            do
            {
                for (const auto page : order)
                {
                    editor.selectPage (page);
                    const auto e = params::layerEnvelope (page, wantsAmp (page));
                    const auto ids = env.getBoundParameterIds();
                    const auto count = juce::roundToInt (getPlain (p, params::envelopeCountId (e)));
                    bool ok = env.getLayer() == page && env.getEnvelope() == e && toggle.isVisible() && toggle.isRight() == wantsAmp (page)
                              && env.getNumNodes() == count && ids.contains (params::nodeTimeId (e, count - 1));

                    // Not bound to any other envelope.
                    for (int other = 0; other < params::numEnvelopes; ++other)
                        if ((Envelope) other != e)
                            ok = ok && ! ids.contains (params::nodeTimeId ((Envelope) other, 1));

                    // Points at their parameter times (relative to the 0 ms point).
                    for (int n = 1; ok && n < count; ++n)
                    {
                        const auto t = getPlain (p, params::nodeTimeId (e, n));
                        ok = std::abs (env.getNodePosition (n).x - env.getNodePosition (0).x - env.getPlotArea().getWidth() * t / env.getViewSpanMs()) < 1.5f;
                    }

                    const auto title = editor.getEnvelopeTitle();
                    ok = ok && title.containsIgnoreCase (params::layerNames[(size_t) page])
                         && title.containsIgnoreCase (wantsAmp (page) ? "AMP" : "PITCH");

                    if (! ok && ++wrong < 4)
                        note ("  mode " + std::to_string (mode) + " page " + std::to_string (page) + " shows envelope "
                              + std::to_string ((int) env.getEnvelope()) + " / layer " + std::to_string (env.getLayer()) + ", title " + title.toStdString());
                }
                ++orders;
            } while (std::next_permutation (order.begin(), order.end()));
        }
        check (orders == 360 && wrong == 0, "every page shows its own Pitch or Amp envelope in all 120 tab orders x 3 view choices ("
               + std::to_string (orders) + " orders, " + std::to_string (wrong) + " wrong)");

        // Each layer remembers its own view while the others change.
        showFor (dsp::subLayer, false);
        showFor (dsp::tailLayer, true);
        showFor (dsp::clickLayer, true);
        editor.selectPage (dsp::subLayer);
        check (env.getEnvelope() == Envelope::subPitch && ! toggle.isRight(), "the Sub page remembers Pitch after Tail and Click switched to Amp");
        editor.selectPage (dsp::clickLayer);
        check (env.getEnvelope() == Envelope::clickAmp && toggle.isRight(), "the Click page remembers Amp");

        // Editing on the Sub amp page edits the Sub: drag its last point right.
        showFor (dsp::subLayer, true);
        const auto bodyAmp = getPlain (p, params::nodeTimeId (Envelope::amp, 1));
        const auto tailEnd = ampEndMs (p, dsp::tailLayer);
        const auto endBefore = ampEndMs (p, dsp::subLayer);
        p.getUndoHistory().clear();
        const auto end = env.getNodePosition (3);
        env.checkForChanges();
        const auto fitBefore = env.getFitSpanMs();
        simulateDrag (env, end, end + juce::Point<float> (40.0f, 0.0f));
        const auto endAfter = ampEndMs (p, dsp::subLayer);
        check (endAfter > endBefore + 1.0f && std::abs (getPlain (p, params::nodeTimeId (Envelope::amp, 1)) - bodyAmp) < 1.0e-4f
                   && std::abs (ampEndMs (p, dsp::tailLayer) - tailEnd) < 1.0e-4f,
               "dragging the Sub amp end point edits the Sub Amp Env (" + format (endBefore, 0) + " -> " + format (endAfter, 0) + " ms), not the Body or Tail");
        env.checkForChanges();
        const auto fitAfter = env.getFitSpanMs();
        check (fitAfter > fitBefore, "the graph follows the drag (fit span " + format (fitBefore, 0) + " -> " + format (fitAfter, 0) + " ms)");
        p.getUndoHistory().undo();
        check (std::abs (ampEndMs (p, dsp::subLayer) - endBefore) < 0.5f, "undo restores the Sub amp end");
        env.checkForChanges();
        check (std::abs (env.getFitSpanMs() - fitBefore) < 1.0f, "the graph follows the undo (fit span " + format (env.getFitSpanMs(), 0) + " ms)");
        check (env.isNodeEditable (0) && env.isNodeEditable (3) && env.isHandleEditable (0) && env.isHandleEditable (2),
               "a layer amp envelope is a full multi-point envelope: every point and curve is editable");

        // Editing on the Noise pitch page edits the Noise pitch (drag its first point up).
        showFor (dsp::noiseLayer, false);
        const auto stBefore = getPlain (p, params::nodeValueId (Envelope::noisePitch, 0));
        const auto first = env.getNodePosition (0);
        simulateDrag (env, first, first + juce::Point<float> (0.0f, -30.0f));
        check (getPlain (p, params::nodeValueId (Envelope::noisePitch, 0)) > stBefore + 0.5f
                   && std::abs (getPlain (p, params::nodeValueId (Envelope::subPitch, 0)) - 7.0f) < 1.0e-3f,
               "dragging the Noise pitch start up raises the Noise Pitch Env (" + format (stBefore, 1) + " -> "
                   + format (getPlain (p, params::nodeValueId (Envelope::noisePitch, 0)), 1) + " st) and nothing else");

        // A preset load rebinds without a page switch; the playhead level follows the layer's amp
        // envelope on both views.
        for (const auto amp : { true, false })
        {
            showFor (dsp::subLayer, amp);
            p.loadFactoryPreset (1, false);
            bool levelsOk = env.getLayer() == dsp::subLayer && env.getEnvelope() == params::layerEnvelope (dsp::subLayer, amp);
            const auto count = juce::roundToInt (getPlain (p, params::envelopeCountId (Envelope::subAmp)));
            for (int n = 0; n < count; ++n)
                levelsOk = levelsOk && std::abs (env.getPlayheadLevelGainAt (n == 0 ? 0.0f : getPlain (p, params::nodeTimeId (Envelope::subAmp, n)))
                                                 - getPlain (p, params::nodeValueId (Envelope::subAmp, n))) < 1.0e-3f;
            check (levelsOk, std::string ("after a preset load the Sub ") + (amp ? "Amp" : "Pitch") + " view's playhead level follows the Sub's own amp points");
        }

        // Snapshots: the Sub in its Pitch view, the Click in its Amp view.
        p.loadFactoryPreset (0, false);
        setEnvelopeNodes (p, Envelope::subPitch, { { 0, 12, 0.6f }, { 35, 3, 0.2f }, { 180, 0, 0 } });
        setValues (p, { { "sub_on", 1 }, { "tail_on", 1 } });
        showFor (dsp::subLayer, false);
        env.checkForChanges();
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.6-sub-pitch");
        showFor (dsp::clickLayer, true);
        env.checkForChanges();
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.6-click-amp");
        check (env.getEnvelope() == Envelope::clickAmp && toggle.isVisible() && toggle.isRight()
                   && editor.getEnvelopeTitle() == "CLICK AMP ENVELOPE",
               "the Click page: its Amp envelope (its length), with the Pitch / Amp switch");
    }

    //==============================================================================
    // v0.3.6: each layer's pitch envelope moves its pitch (or the noise tone filter) and its amp
    // envelope shapes its level, in Synth and Sample modes.
    void testLayerPitchAndAmpEnvelopes()
    {
        using params::Envelope;
        const auto prepared = [] (int layer, int source)
        {
            auto p = makeProcessor();
            isolateLayer (*p, layer);
            setParam (*p, params::layerId (layer, "level"), 0.0f);
            if (params::layerHasSample (layer))
                setParam (*p, params::layerId (layer, "source"), (float) source);
            setValues (*p, { { "click_type", (float) dsp::ClickSettings::knock }, { "tail_spread", 0 }, { "tail_drive", 0 },
                             { "tail_wave", 0 }, { "sub_envamt", 0 }, { "noise_width", 0 } });
            // Long, flat amp envelopes so the pitch can be measured.
            if (layer == dsp::clickLayer)
                setEnvelopeNodes (*p, Envelope::clickAmp, { { 0, 1, 0 }, { 400, 1, 0 }, { 450, 0, 0 } });
            else
                setLayerAhd (*p, layer, 0.0f, 800.0f, 100.0f);
            return p;
        };
        const auto tuneId = [] (int layer) { return juce::String (layer == dsp::clickLayer ? "click_pitch" : params::layerId (layer, "tune")); };

        // 1. A flat +12 st pitch envelope plays exactly like Tune +12 st (synth Click / Sub / Tail,
        //    sampled Click / Tail / Noise), and a real octave up on the Sub and Tail.
        for (const auto layer : { dsp::clickLayer, dsp::subLayer, dsp::tailLayer, dsp::noiseLayer })
        for (const auto source : { 0, 1 })
        {
            if ((layer == dsp::subLayer && source == 1) || (layer == dsp::noiseLayer && source == 0))
                continue;
            const auto name = std::string (params::layerNames[(size_t) layer]) + (source == 1 ? " sample" : " synth");
            auto flat = prepared (layer, source);
            const auto plain = renderNote (*flat, 29, 1.0f, 0.6);
            auto tuned = prepared (layer, source);
            setParam (*tuned, tuneId (layer), 12.0f);
            const auto byTune = renderNote (*tuned, 29, 1.0f, 0.6);
            auto enveloped = prepared (layer, source);
            setEnvelopeNodes (*enveloped, params::layerEnvelope (layer, false), { { 0, 12, 0 }, { 100, 12, 0 } });
            const auto byEnv = renderNote (*enveloped, 29, 1.0f, 0.6);
            check (peakOf (plain) > 1.0e-3f && maxDifference (plain, byEnv) > 1.0e-2f && maxDifference (byEnv, byTune) < 2.0e-3f,
                   name + ": a flat +12 st " + params::layerNames[(size_t) layer] + " Pitch Env plays like Tune +12 (diff to Tune "
                       + format (maxDifference (byEnv, byTune), 5) + ", to 0 st " + format (maxDifference (plain, byEnv), 3) + ")");

            if (source == 0 && layer != dsp::clickLayer)
            {
                const auto f0 = measureFrequency (plain, sampleRate, 0.1, 0.5), f1 = measureFrequency (byEnv, sampleRate, 0.1, 0.5);
                check (f0 > 20.0 && std::abs (f1 / f0 - 2.0) < 0.02, name + ": +12 st doubles the frequency (" + format (f0, 1) + " -> " + format (f1, 1) + " Hz)");
            }

            if (source == 1)
            {
                // The sample amount: 50 % of +24 st is +12 st; 0 % leaves the sample alone.
                auto half = prepared (layer, source);
                setParam (*half, params::layerId (layer, "smp_penv"), 0.5f);
                setEnvelopeNodes (*half, params::layerEnvelope (layer, false), { { 0, 24, 0 }, { 100, 24, 0 } });
                auto off = prepared (layer, source);
                setParam (*off, params::layerId (layer, "smp_penv"), 0.0f);
                setEnvelopeNodes (*off, params::layerEnvelope (layer, false), { { 0, 24, 0 }, { 100, 24, 0 } });
                check (maxDifference (renderNote (*half, 29, 1.0f, 0.6), byTune) < 2.0e-3f && maxDifference (renderNote (*off, 29, 1.0f, 0.6), plain) < 1.0e-6f,
                       name + ": Smp Pitch Env 50 % of +24 st = +12 st; 0 % leaves the sample unbent");
            }
        }

        // 2. A pitch drop: the Sub starts an octave up and glides to its note.
        {
            auto p = prepared (dsp::subLayer, 0);
            setEnvelopeNodes (*p, Envelope::subPitch, { { 0, 12, 0 }, { 150, 12, 0 }, { 300, 0, 0 } });
            const auto b = renderNote (*p, 29, 1.0f, 0.8);
            const auto early = measureFrequency (b, sampleRate, 0.02, 0.14), late = measureFrequency (b, sampleRate, 0.35, 0.7);
            check (std::abs (early / late - 2.0) < 0.03, "Sub Pitch Env: +12 st early, 0 st late (" + format (early, 1) + " -> " + format (late, 1) + " Hz)");
        }

        // 3. The synth Noise has no pitch: its pitch envelope moves a tone filter. -36 st closes a
        //    low-pass to 2.5 kHz (fewer zero crossings, less level); +36 st opens a high-pass to 160 Hz.
        {
            const auto zcr = [] (const juce::AudioBuffer<float>& b) { return (double) zeroCrossings (b, 480, 9600).size() / 9120.0; };
            auto flat = prepared (dsp::noiseLayer, 0);
            const auto plain = renderNote (*flat, 29, 1.0f, 0.3);
            auto dark = prepared (dsp::noiseLayer, 0);
            setEnvelopeNodes (*dark, Envelope::noisePitch, { { 0, -36, 0 }, { 100, -36, 0 } });
            const auto low = renderNote (*dark, 29, 1.0f, 0.3);
            auto bright = prepared (dsp::noiseLayer, 0);
            setParam (*bright, "noise_color", 2.0f); // brown: most energy is low, so the high-pass shows
            auto brownFlat = prepared (dsp::noiseLayer, 0);
            setParam (*brownFlat, "noise_color", 2.0f);
            setEnvelopeNodes (*bright, Envelope::noisePitch, { { 0, 36, 0 }, { 100, 36, 0 } });
            const auto brownPlain = renderNote (*brownFlat, 29, 1.0f, 0.3), high = renderNote (*bright, 29, 1.0f, 0.3);
            check (zcr (low) < 0.6 * zcr (plain) && rmsOf (low, 480, 9600) < 0.7f * rmsOf (plain, 480, 9600),
                   "Noise Pitch Env -36 st darkens the noise (zero-crossing rate " + format (zcr (plain), 3) + " -> " + format (zcr (low), 3)
                       + ", rms " + format (rmsOf (plain, 480, 9600), 3) + " -> " + format (rmsOf (low, 480, 9600), 3) + ")");
            check (zcr (high) > 1.3 * zcr (brownPlain) && rmsOf (high, 480, 9600) < rmsOf (brownPlain, 480, 9600),
                   "Noise Pitch Env +36 st thins brown noise (zero-crossing rate " + format (zcr (brownPlain), 4) + " -> " + format (zcr (high), 4) + ")");

            auto sweep = prepared (dsp::noiseLayer, 0);
            setEnvelopeNodes (*sweep, Envelope::noisePitch, { { 0, -48, 0 }, { 250, 0, 0 } });
            const auto s = renderNote (*sweep, 29, 1.0f, 0.3);
            check (allFinite (s) && zeroCrossings (s, 480, 2400).size() < zeroCrossings (s, 9600, 11520).size(),
                   "a Noise filter sweep (-48 -> 0 st) opens up over time, smoothly and finite");
        }

        // 4. Amp envelopes: levels follow the points (vs. the same layer at a flat 100 %).
        for (int layer = 0; layer < params::numLayers; ++layer)
        for (const auto source : { 0, 1 })
        {
            if (source == 1 && ! params::layerHasSample (layer))
                continue;
            if (source == 1 && layer != dsp::tailLayer && layer != dsp::bodyLayer)
                continue; // the short factory click / noise samples end before the windows below
            const auto name = std::string (params::layerNames[(size_t) layer]) + (source == 1 ? " sample" : " synth");
            const auto e = params::layerEnvelope (layer, true);
            auto reference = prepared (layer, source);
            if (layer == dsp::bodyLayer)
                flatBody (*reference, 55.0f, 300.0f);
            if (source == 1)
                setParam (*reference, params::layerId (layer, "source"), 1.0f);
            setEnvelopeNodes (*reference, e, { { 0, 1, 0 }, { 300, 1, 0 } });
            auto shaped = prepared (layer, source);
            if (layer == dsp::bodyLayer)
                flatBody (*shaped, 55.0f, 300.0f);
            if (source == 1)
                setParam (*shaped, params::layerId (layer, "source"), 1.0f);
            setEnvelopeNodes (*shaped, e, { { 0, 1, 0 }, { 100, 1, 0 }, { 101, 0.25f, 0 }, { 200, 0.25f, 0 }, { 300, 0, 0 } });
            const auto a = renderNote (*reference, 29, 1.0f, 0.5), b = renderNote (*shaped, 29, 1.0f, 0.5);
            const auto ms = [] (double t) { return (int) (t * sampleRate / 1000.0); };
            const auto r1 = rmsOf (b, ms (20), ms (95)) / std::max (1.0e-9f, rmsOf (a, ms (20), ms (95)));
            const auto r2 = rmsOf (b, ms (115), ms (195)) / std::max (1.0e-9f, rmsOf (a, ms (115), ms (195)));
            const auto tailPeak = peakOf (b, ms (302));
            check (rmsOf (a, ms (115), ms (195)) > 1.0e-4f && std::abs (r1 - 1.0f) < 0.03f && std::abs (r2 - 0.25f) < 0.03f && tailPeak < 1.0e-6f,
                   name + ": the Amp Env sets the level (100 % -> " + format (r1, 3) + ", 25 % -> " + format (r2, 3)
                       + ", silent after its 0 % end: peak " + format (tailPeak, 7) + ")");
        }

        // 5. Names and automation: "<Layer> Pitch Env 2 Time", "<Layer> Amp Env 3 Level" ...
        {
            auto p = makeProcessor();
            bool named = true;
            std::set<juce::String> names;
            int count = 0;
            for (const auto& id : allParameterIds (*p))
            {
                auto* param = p->getParameterState().getParameter (id);
                names.insert (param->getName (128));
                const auto info = envelopeParamInfo (id);
                const auto layer = info.isEnvelope ? params::envelopeLayer (info.envelope) : -1;
                if (layer < 0)
                    continue;
                ++count;
                const auto prefix = juce::String (params::layerNames[(size_t) layer]) + (params::isAmpEnvelope (info.envelope) ? " Amp Env " : " Pitch Env ");
                if (! (param->getName (128).startsWith (prefix) && param->isAutomatable()))
                {
                    named = false;
                    note ("  " + id.toStdString() + " is named " + param->getName (128).toStdString());
                }
            }
            check (named && count == 10 * (1 + 3 * params::maxNodes - 2), std::to_string (count) + " layer envelope parameters, all automatable and named \"<Layer> Pitch Env ...\" / \"<Layer> Amp Env ...\"");
            check ((int) names.size() == allParameterIds (*p).size(), "every parameter name is unique");
        }

        // 6. The default amp envelopes are the old shapes: the Sub / Tail / Noise attack-hold-decay
        //    (v0.3.5 defaults) and the Click's linear taper over its 12 ms Length.
        {
            const std::array<std::pair<int, dsp::AhdEnvelope>, 3> old { { { dsp::subLayer, { 1.0f, 60.0f, 350.0f, 0.4f } },
                                                                         { dsp::tailLayer, { 30.0f, 0.0f, 900.0f, 0.5f } },
                                                                         { dsp::noiseLayer, { 0.5f, 5.0f, 150.0f, 0.6f } } } };
            float worst = 0.0f;
            for (const auto& [layer, ahd] : old)
            {
                const auto e = dsp::defaultLayerEnvelope (layer, true);
                for (float t = 0.0f; t <= ahd.getLengthMs(); t += 0.05f)
                    worst = std::max (worst, std::abs (e.evaluate (t) - ahd.evaluate (t)));
            }
            const auto click = dsp::defaultLayerEnvelope (dsp::clickLayer, true);
            for (float t = 0.0f; t <= 12.0f; t += 0.05f)
                worst = std::max (worst, std::abs (click.evaluate (t) - (1.0f - t / 12.0f)));
            check (worst < 1.0e-4f, "the default amp envelopes reproduce the old AHD / Click Length shapes (worst " + format (worst, 7) + ")");
        }

        // 7. Defaults are flat: no layer pitch envelope is active on a fresh processor (no filter, no bend).
        {
            auto p = makeProcessor();
            bool flat = true;
            for (int layer = 0; layer < params::numLayers; ++layer)
                if (layer != dsp::bodyLayer)
                {
                    const auto e = params::layerEnvelope (layer, false);
                    for (int n = 0; n < juce::roundToInt (getPlain (*p, params::envelopeCountId (e))); ++n)
                        flat = flat && getPlain (*p, params::nodeValueId (e, n)) == 0.0f;
                }
            check (flat, "the Click / Sub / Tail / Noise pitch envelopes default to flat 0 st");
        }
    }

    //==============================================================================
    // v0.3.6: randomise draws musical Click envelopes and mild Sub / Tail / Noise ones.
    void testRandomiseLayerEnvelopes()
    {
        auto p = makeProcessor();
        juce::Random random (4242);
        int clickPitchMoves = 0;
        bool clickOk = true, mildOk = true, ampsOk = true;
        std::set<int> clickEnds;

        for (int i = 0; i < 200; ++i)
        {
            const auto values = dooblrkick::presets::randomise (p->getParameterState(), random);
            std::map<juce::String, float> v (values.begin(), values.end());
            const auto at = [&] (params::Envelope e, int n, const char* what)
            {
                const auto id = juce::String (what) == "t" ? params::nodeTimeId (e, n) : params::nodeValueId (e, n);
                check (v.count (id) > 0, "randomise sets " + id.toStdString());
                return v[id];
            };

            // Click: 2 or 3 amp points ending at silence after 3 .. 80 ms; a 0 .. +12 st pitch drop.
            const auto clickCount = (int) v[params::envelopeCountId (params::Envelope::clickAmp)];
            const auto clickEnd = at (params::Envelope::clickAmp, clickCount - 1, "t");
            clickOk = clickOk && (clickCount == 2 || clickCount == 3) && at (params::Envelope::clickAmp, clickCount - 1, "v") == 0.0f
                      && clickEnd >= 2.9f && clickEnd <= 80.5f;
            clickEnds.insert ((int) clickEnd);
            const auto drop = at (params::Envelope::clickPitch, 0, "v");
            clickOk = clickOk && drop >= 0.0f && drop <= 12.0f && at (params::Envelope::clickPitch, 1, "v") == 0.0f;
            clickPitchMoves += drop > 0.0f ? 1 : 0;

            // Sub / Tail: pitch within +-2 st; Noise filter within +-12 st; amp envelopes end silent.
            for (const auto layer : { dsp::subLayer, dsp::tailLayer, dsp::noiseLayer })
            {
                const auto limit = layer == dsp::noiseLayer ? 12.0f : 2.0f;
                const auto pe = params::layerEnvelope (layer, false), ae = params::layerEnvelope (layer, true);
                for (int n = 0; n < (int) v[params::envelopeCountId (pe)]; ++n)
                    mildOk = mildOk && std::abs (at (pe, n, "v")) <= limit;
                const auto count = (int) v[params::envelopeCountId (ae)];
                ampsOk = ampsOk && count >= 3 && at (ae, count - 1, "v") == 0.0f && at (ae, count - 1, "t") > 20.0f;
            }
        }

        check (clickOk, "200 randomisations: Click amp envelopes end at 0 % within 3 .. 80 ms, pitch drops within 0 .. +12 st");
        check (clickEnds.size() > 30 && clickPitchMoves > 60 && clickPitchMoves < 180,
               "Click lengths vary (" + std::to_string (clickEnds.size()) + " distinct) and the pitch drop comes up sometimes ("
                   + std::to_string (clickPitchMoves) + " / 200)");
        check (mildOk, "Sub / Tail pitch envelopes stay within +-2 st and the Noise filter within +-12 st");
        check (ampsOk, "Sub / Tail / Noise amp envelopes are attack / hold / decay shapes that end silent");
    }

    //==============================================================================
    // v0.3.5: the Pitch / Amp sliding switch.
    void testEnvelopeToggle()
    {
        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& env = editor.getEnvelopeEditor();
        auto& toggle = editor.getEnvelopeToggle();

        for (const auto [w, hgt] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor.setSize (w, hgt);
            for (int page = 0; page < params::numLayers; ++page) // v0.3.6: on every layer page
            {
                editor.selectPage (page);
                const auto tb = editor.getLocalArea (toggle.getParentComponent(), toggle.getBounds());
                const auto eb = editor.getLocalArea (env.getParentComponent(), env.getBounds());
                // The title ("NOISE PITCH ENVELOPE") has the bar's first 180 px.
                check (toggle.isVisible() && tb.getWidth() >= 100 && tb.getHeight() >= 18 && editor.getLocalBounds().contains (tb) && ! tb.intersects (eb)
                           && tb.getRight() <= eb.getRight() + 1 && tb.getBottom() <= eb.getY() && tb.getX() >= eb.getX() + 180,
                       std::to_string (w) + "x" + std::to_string (hgt) + " " + params::layerNames[(size_t) page]
                           + ": the switch fits above the graph, right of the title (" + tb.toString().toStdString() + " graph " + eb.toString().toStdString() + ")");
            }
        }

        // v0.3.6: every layer page's switch flips that layer's own Pitch / Amp envelope.
        editor.setSize (1080, 660);
        {
            bool allOk = true;
            for (int page = 0; page < params::numLayers; ++page)
            {
                editor.selectPage (page);
                toggle.setRight (false, true, false);
                const auto c = toggle.getLocalBounds().getCentre().toFloat();
                const auto pitchOk = env.getEnvelope() == params::layerEnvelope (page, false) && editor.getEnvelopeTitle().endsWith ("PITCH ENVELOPE");
                toggle.mouseDown (makeMouseEvent (toggle, c, c, 1, false));
                toggle.mouseUp (makeMouseEvent (toggle, c, c, 1, false));
                for (double t = 0.0; t <= 300.0; t += 30.0) { toggle.advanceAnimation (20000.0 + page * 1000.0 + t); env.advanceCrossfade (20000.0 + page * 1000.0 + t); }
                const auto ampOk = toggle.isRight() && env.getEnvelope() == params::layerEnvelope (page, true) && editor.getEnvelopeTitle().endsWith ("AMP ENVELOPE")
                                   && env.getBoundParameterIds().contains (params::envelopeCountId (params::layerEnvelope (page, true)));
                if (! (pitchOk && ampOk))
                {
                    allOk = false;
                    note ("  page " + std::to_string (page) + ": pitch " + std::to_string (pitchOk) + ", amp " + std::to_string (ampOk));
                }
            }
            // Leaving every layer on Amp, then putting the Body back on Pitch, leaves the others on Amp.
            editor.selectPage (dsp::bodyLayer);
            toggle.setRight (false, true, false);
            editor.selectPage (dsp::noiseLayer);
            allOk = allOk && toggle.isRight() && env.getEnvelope() == params::Envelope::noiseAmp;
            check (allOk, "on every layer page a click on the switch shows that layer's Amp envelope, and each page keeps its own choice");
            for (int page = 0; page < params::numLayers; ++page)
            {
                editor.selectPage (page);
                toggle.setRight (false, true, false);
            }
        }

        editor.setSize (1080, 660);
        editor.selectPage (dsp::bodyLayer);
        toggle.setRight (false, true, false);
        check (! toggle.isRight() && env.getEnvelope() == params::Envelope::pitch && toggle.getThumbPosition() < 0.01f, "starts on Pitch");

        // Hit areas: the thumb sits in its half of the track.
        const auto track = toggle.getTrackBounds(), thumb = toggle.getThumbBounds();
        check (track.contains (thumb) && thumb.getCentreX() < track.getCentreX(), "the thumb is inside the track, on the Pitch side");

        // Click: toggles, the thumb eases over ~180 ms, the graph cross-fades over ~200 ms.
        const auto centre = toggle.getLocalBounds().getCentre().toFloat();
        toggle.mouseDown (makeMouseEvent (toggle, centre, centre, 1, false));
        toggle.mouseUp (makeMouseEvent (toggle, centre, centre, 1, false));
        check (toggle.isRight() && env.getEnvelope() == params::Envelope::amp && toggle.isAnimating() && env.getCrossfadeAmount() > 0.99f,
               "a click switches to Amp and starts the slide + cross-fade (right " + std::to_string (toggle.isRight()) + ", animating " + std::to_string (toggle.isAnimating()) + ", fade " + format (env.getCrossfadeAmount(), 2) + ")");
        std::vector<float> positions;
        for (double t = 0.0; t <= 240.0; t += 15.0)
        {
            toggle.advanceAnimation (5000.0 + t);
            env.advanceCrossfade (5000.0 + t);
            positions.push_back (toggle.getThumbPosition());
            if (std::abs (t - 90.0) < 1.0)
            {
                check (positions.back() > 0.2f && positions.back() < 0.99f && env.getCrossfadeAmount() > 0.05f && env.getCrossfadeAmount() < 0.95f,
                       "mid-way (90 ms): thumb " + format (positions.back(), 2) + ", cross-fade " + format (env.getCrossfadeAmount(), 2));
                saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-envtoggle");
            }
        }
        check (std::is_sorted (positions.begin(), positions.end()) && positions.back() > 0.999f && ! toggle.isAnimating() && env.getCrossfadeAmount() == 0.0f,
               "the thumb rises monotonically to Amp and the cross-fade finishes by 240 ms");

        // Drag the thumb back to the left.
        const auto right = toggle.getThumbBounds().getCentre();
        simulateDrag (toggle, right, { toggle.getTrackBounds().getX() + 4.0f, right.y });
        for (double t = 0.0; t <= 300.0; t += 30.0)
            toggle.advanceAnimation (8000.0 + t);
        check (! toggle.isRight() && env.getEnvelope() == params::Envelope::pitch && toggle.getThumbPosition() < 0.01f, "dragging the thumb left selects Pitch");

        // Keys.
        toggle.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        const auto byRight = toggle.isRight();
        toggle.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        check (byRight && ! toggle.isRight() && env.getEnvelope() == params::Envelope::pitch, "Right arrow selects Amp, Space toggles back");

        // Accessibility.
        auto handler = toggle.createAccessibilityHandler();
        check (handler != nullptr && handler->getRole() == juce::AccessibilityRole::toggleButton && handler->getActions().contains (juce::AccessibilityActionType::toggle),
               "accessible as a toggle button");

        // Not a parameter (a view choice): nothing in the undo history.
        h.processor->getUndoHistory().clear();
        toggle.setRight (true, true, false);
        check (! h.processor->getUndoHistory().canUndo(), "switching the view adds no undo step");
    }

    //==============================================================================
    // v0.3.5: the envelope / waveform divider.
    void testSplitter()
    {
        using E = Editor;
        bool clamped = true;
        for (const auto w : { 500, 700, 900, 1500, 2300 })
            for (const auto r : { 0.0f, 0.3f, 0.6f, 0.8f, 1.0f, std::numeric_limits<float>::quiet_NaN() })
            {
                const auto ew = E::envelopeWidthFor (w, r);
                const auto ww = w - dooblrkick::ui::SplitterBar::width - ew;
                clamped = clamped && ew >= 0 && ww >= 0
                          && (w - dooblrkick::ui::SplitterBar::width < E::minEnvelopeWidth + E::minWaveformWidth || (ew >= E::minEnvelopeWidth && ww >= E::minWaveformWidth));
            }
        check (clamped, "envelopeWidthFor keeps both sides at least their minimum widths at every ratio");
        check (E::envelopeWidthFor (1500, std::numeric_limits<float>::quiet_NaN()) == E::envelopeWidthFor (1500, 0.6f), "a bad ratio falls back to 0.6");

        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        auto& split = editor.getSplitter();
        auto& env = editor.getEnvelopeEditor();
        auto& wave = editor.getWaveformView();
        editor.selectPage (dsp::bodyLayer);

        for (const auto [w, hgt] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
            for (const auto r : { DooblrKickAudioProcessor::minEnvelopeSplit, DooblrKickAudioProcessor::maxEnvelopeSplit })
            {
                editor.setSize (w, hgt);
                editor.setEnvelopeSplit (r);
                const auto in = [&] (juce::Component& c) { return editor.getLocalArea (c.getParentComponent(), c.getBounds()); };
                const auto eb = in (env), sb = in (split), wb = in (wave), tb = in (editor.getEnvelopeToggle());
                check (! eb.intersects (sb) && ! sb.intersects (wb) && ! eb.intersects (wb) && eb.getRight() <= sb.getX() && sb.getRight() <= wb.getX()
                           && editor.getLocalBounds().contains (wb) && wb.getWidth() >= E::minWaveformWidth && eb.getWidth() >= E::minEnvelopeWidth
                           && tb.getX() >= eb.getX() && tb.getRight() <= eb.getRight() && sb.getHeight() > 100,
                       std::to_string (w) + "x" + std::to_string (hgt) + " ratio " + format (r, 1) + ": graph " + std::to_string (eb.getWidth()) + " | bar | waveform "
                           + std::to_string (wb.getWidth()) + ", no overlaps");
            }

        // Drag: the divider follows the mouse; clamped at the ends.
        editor.setSize (1080, 660);
        editor.setEnvelopeSplit (0.6f);
        const auto before = env.getWidth();
        const auto mid = split.getLocalBounds().getCentre().toFloat();
        simulateDrag (split, mid, mid + juce::Point<float> (-80.0f, 0.0f));
        check (std::abs (env.getWidth() - (before - 80)) <= 2 && p.getEnvelopeSplit() < 0.6f,
               "dragging the bar 80 px left narrows the graph (" + std::to_string (before) + " -> " + std::to_string (env.getWidth()) + " px)");
        simulateDrag (split, mid, mid + juce::Point<float> (2000.0f, 0.0f));
        check (wave.getWidth() >= E::minWaveformWidth && p.getEnvelopeSplit() <= DooblrKickAudioProcessor::maxEnvelopeSplit + 1.0e-4f, "dragging far right stops at the waveform's minimum");
        simulateDrag (split, mid, mid + juce::Point<float> (-2000.0f, 0.0f));
        check (env.getWidth() >= E::minEnvelopeWidth && p.getEnvelopeSplit() >= DooblrKickAudioProcessor::minEnvelopeSplit - 1.0e-4f, "dragging far left stops at the graph's minimum");

        // Window resizes keep the ratio.
        editor.setEnvelopeSplit (0.7f);
        const auto ratioAt = [&] { return (float) env.getWidth() / (float) (env.getWidth() + split.getWidth() + wave.getWidth()); };
        const auto r1 = ratioAt();
        editor.setSize (1600, 1000);
        const auto r2 = ratioAt();
        check (std::abs (r1 - r2) < 0.02f && std::abs (p.getEnvelopeSplit() - 0.7f) < 1.0e-4f, "a bigger window keeps the split (" + format (r1, 3) + " vs " + format (r2, 3) + ")");

        // Double-click resets; hover highlights; the cursor says resize.
        simulateDoubleClick (split, mid);
        check (std::abs (p.getEnvelopeSplit() - 0.6f) < 1.0e-4f, "double-click resets the split to 0.6");
        check (split.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor), "the bar shows a left-right resize cursor");
        split.mouseEnter (makeMouseEvent (split, mid, mid, 0, false));
        check (split.isHot(), "the grip highlights on hover");

        // Persisted in the state; not a parameter, not undoable.
        p.getUndoHistory().clear();
        editor.setEnvelopeSplit (0.45f);
        check (! p.getUndoHistory().canUndo(), "resizing adds no undo step");
        bool isParam = false;
        for (const auto& id : allParameterIds (p))
            isParam = isParam || id.containsIgnoreCase ("split");
        check (! isParam, "the split is not a host parameter");
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q->getEnvelopeSplit() - 0.45f) < 1.0e-4f, "the split survives the plugin state");
        q->setEnvelopeSplit (5.0f);
        check (std::abs (q->getEnvelopeSplit() - DooblrKickAudioProcessor::maxEnvelopeSplit) < 1.0e-4f, "an out-of-range split is clamped");

        editor.setSize (1080, 660);
        editor.setEnvelopeSplit (0.5f);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-splitter");
        split.mouseExit (makeMouseEvent (split, mid, mid, 0, false));
    }

    //==============================================================================
    // v0.3.5: Export asks where to save; Play is an icon.
    void testExportAndPlayButtons()
    {
        using P = DooblrKickAudioProcessor;
        check (P::exportDestinationFor (juce::File ("/tmp/kick")) == juce::File ("/tmp/kick.wav"), "\"kick\" gets .wav");
        check (P::exportDestinationFor (juce::File ("/tmp/kick.WAV")) == juce::File ("/tmp/kick.WAV"), "\"kick.WAV\" is kept");
        check (P::exportDestinationFor (juce::File ("/tmp/kick.v2")) == juce::File ("/tmp/kick.v2.wav"), "\"kick.v2\" gets .wav");
        check (P::exportDestinationFor (juce::File()) == juce::File(), "no file (cancel) stays no file");

        auto p = makeProcessor();
        check (p->getExportStartFile() == P::getDefaultExportFolder().getChildFile ("dooblr-kick.wav"), "the dialog starts at ~/Music/dooblr-kick/dooblr-kick.wav");

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dk-export-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        folder.createDirectory();
        juce::File written;
        juce::String error;
        check (p->exportTo (folder.getChildFile ("my kick"), 48000.0, written, error) && written == folder.getChildFile ("my kick.wav") && written.existsAsFile()
                   && written.getSize() > 1000,
               "exporting to \"my kick\" writes \"my kick.wav\" in the chosen folder (" + error.toStdString() + ")");
        check (p->getLastExportFolder() == folder && p->getExportStartFile() == folder.getChildFile ("dooblr-kick.wav"), "the next dialog opens in that folder");
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getLastExportFolder() == folder, "the last folder is kept in the state");
        check (! p->exportTo (juce::File(), 48000.0, written, error) && error.isNotEmpty(), "no file: nothing written, an error");
        check (! p->exportTo (juce::File ("/nonexistent-dir-xyz/sub/k"), 48000.0, written, error) && error.isNotEmpty(), "an unwritable place fails with a message");

        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        editor.setSize (1080, 660);
        auto& exportButton = editor.getExportButton();
        auto& play = editor.getPlayButton();
        check (exportButton.getButtonText() == "Export" && exportButton.isVisible(), "the export button reads \"Export\"");
        check (play.getButtonText().isEmpty() && play.getTooltip() == "Play" && play.isVisible() && std::abs (play.getWidth() - play.getHeight()) <= 8,
               "Play is an icon (no text, tooltip Play, about square: " + play.getBounds().toString().toStdString() + ")");
        check (! exportButton.getBounds().intersects (play.getBounds()), "Export and Play don't overlap");

        // The icon: bright pixels in the middle, none at the corners; centred.
        const auto image = play.createComponentSnapshot (play.getLocalBounds(), true, 1.0f);
        const auto bright = [&] (int x, int y) { return image.getPixelAt (x, y).getPerceivedBrightness() > 0.6f; };
        int lit = 0, litLeft = 0, litRight = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (bright (x, y))
                {
                    ++lit;
                    (x < image.getWidth() / 2 ? litLeft : litRight)++;
                }
        const auto icon = dooblrkick::ui::PlayIconButton::makeIcon (play.getLocalBounds().toFloat()).getBounds();
        check (lit > 20 && bright (image.getWidth() / 2, image.getHeight() / 2) && ! bright (1, 1) && litLeft > litRight
                   && std::abs (icon.getCentreX() - play.getWidth() * 0.5f) < 2.0f && std::abs (icon.getCentreY() - play.getHeight() * 0.5f) < 1.0f,
               "a centred right-pointing triangle is drawn (" + std::to_string (lit) + " px lit, wider on the left)");

        // Writing through the editor reports in the status line.
        juce::File out;
        check (editor.exportToFile (folder.getChildFile ("from editor"), out, error) && out.existsAsFile()
                   && editor.getWaveformView().getStatus().startsWith ("Exported"),
               "the editor's export writes the chosen file and says so");
        check (! editor.exportToFile (juce::File ("/nonexistent-dir-xyz/sub/k"), out, error) && editor.getWaveformView().getStatus().startsWith ("Export failed"),
               "a write error shows a short message (no modal)");

        saveSnapshot (editor.createComponentSnapshot (editor.getHeaderArea()), "-v0.3.5-buttons");
        folder.deleteRecursively();
    }

    //==============================================================================
    // v0.3.5: the layer Level faders use a console taper.
    void testLevelTaper()
    {
        bool monotonic = true, roundTrip = true;
        float previous = -1000.0f;
        for (int i = 0; i <= 1000; ++i)
        {
            const auto x = (float) i / 1000.0f;
            const auto db = params::travelToLevelDb (x);
            monotonic = monotonic && db > previous - 1.0e-6f && (i == 0 || db > previous);
            roundTrip = roundTrip && std::abs (params::levelDbToTravel (db) - x) < 1.0e-4f;
            previous = db;
        }
        check (monotonic && roundTrip, "the taper is strictly increasing and round-trips (1001 points)");
        check (params::travelToLevelDb (0.0f) == params::minLevelDb && params::travelToLevelDb (1.0f) == params::maxLevelDb, "the ends are -inf (-60) and +12 dB");
        const auto zero = params::levelDbToTravel (0.0f);
        check (zero >= 0.75f && zero <= 0.80f, "0 dB sits at " + format (zero * 100.0, 1) + " % of travel");
        const auto above24 = 1.0f - params::levelDbToTravel (-24.0f);
        check (above24 >= 0.70f, format (above24 * 100.0, 1) + " % of travel is above -24 dB");
        const auto band = params::levelDbToTravel (-24.0f) - params::levelDbToTravel (-48.0f);
        check (band > 0.1f && band < 0.2f, "-48 .. -24 dB takes " + format (band * 100.0, 1) + " %");

        // The parameters themselves carry the taper (so host automation feels the same).
        auto p = makeProcessor();
        bool same = true;
        for (int l = 0; l < params::numLayers; ++l)
        {
            auto* param = getParam (*p, params::layerId (l, "level"));
            for (const auto db : { -60.0f, -48.0f, -24.0f, -6.0f, 0.0f, 6.0f, 12.0f })
                same = same && param != nullptr && std::abs (param->convertTo0to1 (db) - params::levelDbToTravel (db)) < 1.0e-5f
                       && std::abs (param->convertFrom0to1 (params::levelDbToTravel (db)) - db) < 1.0e-3f;
        }
        check (same, "all five Level parameters use the taper");

        // The fader: travel follows the taper; double-click and fine drag still work.
        EditorHolder h;
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        editor.setSize (1080, 660);
        editor.selectPage (dsp::bodyLayer);
        auto& slider = editor.getLevelFader (dsp::bodyLayer).getSlider();
        check (std::abs ((float) slider.valueToProportionOfLength (0.0) - zero) < 1.0e-4f, "the fader puts 0 dB at the taper's position");
        check ((bool) slider.getProperties()["faderScale"], "the fader draws its dB marks");

        setParam (*h.processor, "body_level", -30.0f);
        const auto pos = juce::Point<float> ((float) slider.getWidth() * 0.5f, slider.getPositionOfValue (slider.getValue()) - (float) slider.getY());
        simulateDoubleClick (slider, pos);
        check (std::abs (getPlain (*h.processor, "body_level") - 0.0f) < 0.01f, "double-click resets Body Level to its default (0 dB)");

        // Fine drag: Shift turns on the slow velocity mode.
        check (slider.getVelocityModeIsSwappable() && slider.getVelocitySensitivity() < 0.5, "Shift + drag is a fine adjustment");

        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-levelfader");
    }

    void testPlayheadLevel()
    {
        using EE = dooblrkick::ui::EnvelopeEditor;
        using dooblrkick::params::Envelope;
        using dooblrkick::dsp::PlayheadTap;
        namespace params = dooblrkick::params;

        // The mapping: monotonic in gain, floor at / below -60 dB, 1 at 0 dB.
        {
            bool monotonic = true;
            auto previous = EE::playheadAlphaForGain (0.0f);
            for (int i = 0; i <= 400; ++i)
            {
                const auto gain = std::pow (10.0f, -6.0f + 6.5f * (float) i / 400.0f); // -120 dB .. +10 dB
                const auto a = EE::playheadAlphaForGain (gain);
                monotonic = monotonic && a >= previous && a >= EE::playheadAlphaFloor && a <= 1.0f;
                previous = a;
            }
            check (monotonic, "playhead alpha is monotonic in gain and stays in floor .. 1");
            check (std::abs (EE::playheadAlphaForGain (1.0f) - 1.0f) < 1.0e-6f
                       && std::abs (EE::playheadAlphaForGain (0.0f) - EE::playheadAlphaFloor) < 1.0e-6f
                       && std::abs (EE::playheadAlphaForGain (0.001f) - EE::playheadAlphaFloor) < 1.0e-4f
                       && std::abs (EE::playheadAlphaForGain (std::nanf ("")) - EE::playheadAlphaFloor) < 1.0e-6f,
                   "0 dB -> 1, -60 dB / silence / NaN -> the floor");
            const auto mid = EE::playheadAlphaForGain (0.1f); // -20 dB
            check (std::abs (mid - (EE::playheadAlphaFloor + (1.0f - EE::playheadAlphaFloor) * (40.0f / 60.0f))) < 1.0e-4f,
                   "linear in dB: -20 dB is 2/3 of the way up (" + format (mid, 3) + ")");
            check (EE::playheadAlphaFloor >= 0.08f && EE::playheadAlphaFloor <= 0.12f, "floor is about 0.1");
        }

        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        setValues (p, { { params::envelopeCountId (Envelope::master), 4.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 60.0f }, { params::nodeValueId (Envelope::master, 1), 0.8f },
                        { params::nodeTimeId (Envelope::master, 2), 250.0f }, { params::nodeValueId (Envelope::master, 2), 0.4f },
                        { params::nodeTimeId (Envelope::master, 3), 700.0f }, { params::nodeValueId (Envelope::master, 3), 0.0f },
                        { "menv_on", 1.0f }, { "menv_tscale", 1.0f }, { "menv_depth", 1.0f } });
        editor.selectPage (Editor::envelopePage);
        auto& env = editor.getMasterEnvelopeEditor();
        env.checkForChanges();
        env.resetView();

        PlayheadTap local;
        local.setSampleRate (48000.0);
        env.setPlayheadTap (&local);
        env.updatePlayhead (0.0);
        const auto at = [&] (EE& e, std::uint32_t hit, float ms)
        {
            local.publish (hit, true, (std::int64_t) std::llround (ms * 48.0));
            e.updatePlayhead (1000.0 + hit);
            return e.getPlayheadLevelAlpha();
        };

        const auto peak = at (env, 1, 0.0f), early = at (env, 2, 60.0f), late = at (env, 3, 640.0f), end = at (env, 4, 700.0f);
        check (peak > 0.99f, "at the peak the playhead is at full brightness (" + format (peak, 3) + ")");
        check (late < peak && early < peak && late < early, "decaying: peak " + format (peak, 3) + " > 60 ms " + format (early, 3)
                                                                 + " > 640 ms " + format (late, 3));
        check (late > EE::playheadAlphaFloor + 0.05f && late < 0.8f, "late in the decay it is dim but still there (" + format (late, 3) + ")");
        check (std::abs (end - EE::playheadAlphaFloor) < 1.0e-4f && env.getPlayheadAlpha() == 1.0f,
               "at silence it sits on the floor; the end-of-hit fade is separate (still 1)");
        check (std::abs (env.getPlayheadLevelGainAt (250.0f) - 0.4f) < 1.0e-3f, "the gain is the curve's value (250 ms: 40 %)");

        // A rising segment brightens it again.
        setValues (p, { { params::nodeValueId (Envelope::master, 2), 0.95f } });
        env.checkForChanges();
        check (at (env, 5, 250.0f) > at (env, 6, 60.0f), "rising from 80 % to 95 %: brighter at the higher point");
        setValues (p, { { params::nodeValueId (Envelope::master, 2), 0.4f } });

        // The effective envelope: Time Scale and Depth.
        const auto g320 = env.getPlayheadLevelGainAt (320.0f);
        setValues (p, { { "menv_tscale", 2.0f } });
        env.checkForChanges();
        check (std::abs (env.getPlayheadLevelGainAt (640.0f) - g320) < 0.01f,
               "Time Scale 200 %: 640 ms plays what 320 ms did at 100 %");
        setValues (p, { { "menv_tscale", 1.0f }, { "menv_depth", 0.5f } });
        env.checkForChanges();
        check (env.getPlayheadLevelGainAt (640.0f) > 0.4f && env.getPlayheadLevelGainAt (640.0f) > g320 * 0.5f,
               "Depth 50 %: the decay is shallower, so it stays brighter (" + format (env.getPlayheadLevelGainAt (640.0f), 3) + ")");
        check (std::abs (env.getPlayheadLevelGainAt (640.0f) - env.getEffectiveEnvelope().evaluate (640.0f)) < 1.0e-6f,
               "the gain is the effective envelope the DSP plays");
        setValues (p, { { "menv_depth", 1.0f }, { "menv_on", 0.0f } });
        env.checkForChanges();
        check (env.getPlayheadLevelGainAt (640.0f) == 1.0f && std::abs (at (env, 7, 640.0f) - 1.0f) < 1.0e-6f,
               "master envelope off: unity gain, full brightness");
        setValues (p, { { "menv_on", 1.0f } });
        env.checkForChanges();

        // Body graphs: both follow the Body amp envelope.
        setValues (p, { { params::envelopeCountId (Envelope::amp), 3.0f },
                        { params::nodeValueId (Envelope::amp, 0), 1.0f },
                        { params::nodeTimeId (Envelope::amp, 1), 100.0f }, { params::nodeValueId (Envelope::amp, 1), 0.5f },
                        { params::nodeTimeId (Envelope::amp, 2), 400.0f }, { params::nodeValueId (Envelope::amp, 2), 0.0f },
                        { params::layerId (1, "start"), 0.0f } });
        editor.selectPage (1);
        editor.showEnvelope (Envelope::amp);
        auto& body = editor.getEnvelopeEditor();
        body.checkForChanges();
        body.setPlayheadTap (&local);
        const auto ampEarly = at (body, 8, 5.0f), ampLate = at (body, 9, 350.0f);
        const auto ampGain = body.getPlayheadLevelGainAt (100.0f);
        check (ampEarly > ampLate && std::abs (ampGain - 0.5f) < 1.0e-3f, "Body amp graph: dims with the amp envelope");
        editor.showEnvelope (Envelope::pitch);
        auto& pitch = editor.getEnvelopeEditor();
        pitch.checkForChanges();
        pitch.setPlayheadTap (&local);
        check (std::abs (pitch.getPlayheadLevelGainAt (100.0f) - 0.5f) < 1.0e-3f && at (pitch, 10, 5.0f) > at (pitch, 11, 350.0f),
               "Body pitch graph: dims with the Body amp envelope too");
        pitch.setPlayheadTap (&p.getPlayheadTap());

        // Snapshots: near the peak and late in the decay.
        editor.selectPage (Editor::envelopePage);
        env.checkForChanges();
        env.resetView();
        at (env, 12, 20.0f);
        check (env.getPlayheadX().has_value(), "snapshot: playhead at 20 ms in view");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-playhead-peak");
        at (env, 13, 620.0f);
        check (env.getPlayheadX().has_value(), "snapshot: playhead at 620 ms in view");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.5-playhead-decay");
        env.setPlayheadTap (&p.getPlayheadTap());
    }

    //==============================================================================
    // v0.3.4: the playhead.
    void testPlayhead()
    {
        using params::Envelope;
        using EE = dooblrkick::ui::EnvelopeEditor;
        using dooblrkick::dsp::PlayheadTap;

        check (std::abs (EE::playheadTimeFor (24000, 48000.0, 0.0f) - 500.0f) < 1.0e-3f
                   && std::abs (EE::playheadTimeFor (4410, 44100.0, 20.0f) - 80.0f) < 1.0e-3f,
               "playhead ms = samples / rate, minus the Body Start on the Body graphs");

        // The packed hand-off.
        {
            const auto s = PlayheadTap::unpack (PlayheadTap::pack (0x12345u, true, 123456789));
            check (s.hit == 0x2345u && s.active && s.samples == 123456789, "pack / unpack round trip (hit wraps at 16 bits)");
            check (PlayheadTap::unpack (PlayheadTap::pack (1, false, -5)).samples == 0, "negative samples clamp to 0");
            check (std::atomic<std::uint64_t>::is_always_lock_free, "the 64-bit hand-off is lock free on this machine");

            PlayheadTap tap;
            std::atomic<bool> done { false };
            std::thread writer ([&]
            {
                for (std::int64_t i = 1; i <= 300000; ++i)
                    tap.publish ((std::uint32_t) i, (i & 1) != 0, i * 3);
                done = true;
            });
            int torn = 0, reads = 0;
            while (! done.load() || reads < 1000)
            {
                const auto s = tap.read();
                ++reads;
                if (s.samples != 0 && ((std::uint32_t) (s.samples / 3) & 0xffffu) != s.hit)
                    ++torn;
                if (s.samples != 0 && (((s.samples / 3) & 1) != 0) != s.active)
                    ++torn;
            }
            writer.join();
            check (torn == 0, "concurrent publish / read: every snapshot is consistent (" + std::to_string (reads) + " reads)");
        }

        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        setValues (p, { { params::envelopeCountId (Envelope::master), 4.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 60.0f }, { params::nodeValueId (Envelope::master, 1), 0.8f },
                        { params::nodeTimeId (Envelope::master, 2), 250.0f }, { params::nodeValueId (Envelope::master, 2), 0.4f },
                        { params::nodeTimeId (Envelope::master, 3), 700.0f }, { params::nodeValueId (Envelope::master, 3), 0.0f },
                        { "menv_on", 1.0f }, { "menv_tscale", 2.0f }, { "menv_depth", 1.0f } });
        editor.selectPage (Editor::envelopePage);
        auto& env = editor.getMasterEnvelopeEditor();
        env.checkForChanges();
        env.resetView();

        // The live processor: a hit with Time Scale 200 % runs the playhead in real time, and the
        // effective curve under it is the gain the DSP applies there.
        auto& tap = p.getPlayheadTap();
        env.updatePlayhead (0.0); // what was there before the hit isn't shown
        check (env.getPlayheadAlpha() == 0.0f, "no playhead before a hit");
        renderMidi (p, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 12 * 512);
        const auto snap = tap.read();
        env.updatePlayhead (10.0);
        check (snap.active && snap.samples == 12 * 512, "the processor publishes the hit's samples after each block");
        check (std::abs (env.getPlayheadMs() - 128.0f) < 0.01f && env.getPlayheadAlpha() == 1.0f, "playhead at 128 ms after 6144 samples at 48 kHz");
        const auto expectedX = env.getPlotArea().getX() + env.getPlotArea().getWidth() * (128.0f - env.getViewStartMs()) / env.getViewSpanMs();
        check (env.getPlayheadX().has_value() && std::abs (*env.getPlayheadX() - expectedX) < 0.01f, "and drawn at that time's x");
        check (std::abs (env.getEffectiveEnvelope().getEndTimeMs() - 1400.0f) < 0.5f && std::abs (env.getViewSpanMs() - EE::fitSpanFor (1400.0f, env.getMaxTimeMs())) < 0.5f,
               "with Time Scale 200 % the 700 ms point plays at 1400 ms: the graph shows that scaled time, so real time maps 1:1");
        const auto hitBefore = snap.hit;
        renderMidi (p, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 2 * 512);
        check (tap.read().hit == ((hitBefore + 1) & 0xffffu) && tap.read().samples == 1024 && tap.read().active, "a MIDI retrigger publishes a new hit from 0");

        // Fade after the voice ends, retrigger, newest hit.
        PlayheadTap local;
        local.setSampleRate (48000.0);
        local.publish (7, false, 0);
        env.setPlayheadTap (&local);
        env.updatePlayhead (0.0);
        check (env.getPlayheadAlpha() == 0.0f, "an old, finished hit isn't shown when the graph opens");
        local.publish (8, true, 4800);
        env.updatePlayhead (1000.0);
        check (env.getPlayheadAlpha() == 1.0f && std::abs (env.getPlayheadMs() - 100.0f) < 0.01f, "a new hit shows at once (100 ms)");
        local.publish (8, false, 9600);
        env.updatePlayhead (1100.0);
        const auto frozen = env.getPlayheadMs();
        env.updatePlayhead (1100.0 + EE::playheadFadeMs * 0.5);
        check (std::abs (env.getPlayheadAlpha() - 0.5f) < 0.01f && frozen == env.getPlayheadMs(), "after the voice ends it fades (half way at "
                                                                                                    + format ((float) EE::playheadFadeMs * 0.5f, 0) + " ms), standing still");
        env.updatePlayhead (1100.0 + EE::playheadFadeMs + 1.0);
        check (env.getPlayheadAlpha() == 0.0f && ! env.getPlayheadX().has_value(), "and is gone after the fade");
        local.publish (9, true, 480);
        env.updatePlayhead (2000.0);
        check (env.getPlayheadAlpha() == 1.0f && std::abs (env.getPlayheadMs() - 10.0f) < 0.01f, "a retrigger restarts it");
        local.publish (9, true, 14400);
        env.updatePlayhead (2300.0);
        local.publish (10, true, 960);
        env.updatePlayhead (2310.0);
        check (std::abs (env.getPlayheadMs() - 20.0f) < 0.01f && env.getPlayheadAlpha() == 1.0f, "a retrigger mid-sweep jumps back to the newest hit");

        // Off view: clipped (no x); the auto-fit doesn't chase it.
        local.publish (10, true, 48000 * 5);
        env.updatePlayhead (2400.0);
        const auto spanBefore = env.getViewSpanMs();
        env.checkForChanges();
        check (! env.getPlayheadX().has_value() && env.getPlayheadAlpha() == 1.0f && env.getViewSpanMs() == spanBefore,
               "a playhead beyond the view is clipped, and the view doesn't follow it");

        // The Body graphs: offset by the Body Start.
        editor.selectPage (1);
        editor.showEnvelope (Envelope::amp);
        auto& body = editor.getEnvelopeEditor();
        setValues (p, { { params::layerId (1, "start"), 20.0f } });
        body.setPlayheadTap (&local);
        body.updatePlayhead (0.0);
        local.publish (11, true, 4800);
        body.updatePlayhead (10.0);
        check (std::abs (body.getPlayheadMs() - 80.0f) < 0.01f && body.getPlayheadX().has_value(), "Body amp: 100 ms into the hit is 80 ms on a 20 ms-late Body");
        setValues (p, { { params::layerId (1, "start"), 0.0f } });
        body.setPlayheadTap (&p.getPlayheadTap());

        // Snapshot mid-sweep (a simulated position, Time Scale back at 100 %).
        setValues (p, { { "menv_tscale", 1.0f } });
        editor.selectPage (Editor::envelopePage);
        env.checkForChanges();
        env.resetView();
        local.publish (12, true, (std::int64_t) (0.32 * 48000.0));
        env.updatePlayhead (5000.0);
        check (env.getPlayheadX().has_value(), "snapshot: playhead at 320 ms is in view");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.4-playhead");
        env.setPlayheadTap (&p.getPlayheadTap());
        juce::ignoreUnused (hitBefore);
    }

    //==============================================================================
    // v0.3.4: the header's output meter and version.
    void collectLabels (juce::Component& c, juce::StringArray& texts)
    {
        if (auto* label = dynamic_cast<juce::Label*> (&c))
            texts.add (label->getText());
        for (auto* child : c.getChildren())
            collectLabels (*child, texts);
    }

    void testHeaderMeterAndVersion()
    {
        using Meter = dooblrkick::ui::OutputMeter;
        using Zone = Meter::Zone;

        check (Meter::zoneFor (-20.0f) == Zone::green && Meter::zoneFor (-6.01f) == Zone::green, "meter: green below -6 dBFS");
        check (Meter::zoneFor (-6.0f) == Zone::yellow && Meter::zoneFor (-0.01f) == Zone::yellow, "meter: yellow from -6 to 0 dBFS");
        check (Meter::zoneFor (0.0f) == Zone::red && Meter::zoneFor (2.0f) == Zone::red, "meter: red at and above 0 dBFS");
        check (Meter::colourFor (-20.0f) == Meter::zoneColour (Zone::green) && Meter::colourFor (-6.0f) == Meter::zoneColour (Zone::yellow)
                   && Meter::colourFor (0.0f) == Meter::zoneColour (Zone::red),
               "meter: pure zone colours at the thresholds");
        const auto blend = Meter::colourFor (-6.5f);
        check (blend != Meter::zoneColour (Zone::green) && blend != Meter::zoneColour (Zone::yellow), "meter: a short blend just under a threshold (smooth)");

        // The tap: per-channel maxima since the last take.
        dooblrkick::dsp::OutputPeakTap peaks;
        peaks.push (0.3f, 0.6f);
        peaks.push (0.5f, 0.1f);
        const auto taken = peaks.take();
        check (taken.first == 0.5f && taken.second == 0.6f && peaks.take().first == 0.0f, "peak tap: max per channel, cleared on take");

        // Ballistics and the clip latch.
        Meter meter;
        meter.setSize (20, 40);
        meter.pushPeaks (0.5f, 0.5f, 0.0);
        check (std::abs (meter.getLevelDb (0) + 6.02f) < 0.05f && ! meter.isClipLatched(), "meter: instant attack (-6 dB), no clip");
        meter.pushPeaks (1.0f, 0.2f, 16.0);
        check (meter.isClipLatched (0) && ! meter.isClipLatched (1), "meter: a 0 dBFS peak lights the left clip light");
        for (double t = 32.0; t <= 1032.0; t += 16.0)
            meter.pushPeaks (0.0f, 0.0f, t);
        check (meter.isClipLatched (0), "meter: the clip light stays after the signal is gone");
        check (meter.getLevelDb (0) < -20.0f && meter.getLevelDb (0) > -30.0f, "meter: smooth release (~26 dB/s; " + format (meter.getLevelDb (0), 1) + " dB after 1 s)");
        check (meter.getHoldDb (0) < 0.0f && meter.getHoldDb (0) > meter.getLevelDb (0), "meter: the peak hold decays after its hold time, above the bar");
        meter.mouseDown (makeMouseEvent (meter, { 10.0f, 10.0f }, { 10.0f, 10.0f }, 1, false));
        check (! meter.isClipLatched(), "meter: a click clears the clip lights");

        // Measured after the Output gain.
        auto peakFor = [] (float outputDb)
        {
            auto p = makeProcessor();
            setValues (*p, { { "output_gain", outputDb } });
            p->getOutputPeakTap().take();
            renderMidi (*p, { { 0, juce::MidiMessage::noteOn (1, 36, 1.0f) } }, 24000);
            return p->getOutputPeakTap().take();
        };
        const auto loud = peakFor (0.0f), quiet = peakFor (-12.0f);
        const auto ratio = quiet.first / juce::jmax (1.0e-6f, loud.first);
        check (loud.first > 0.01f && ratio > 0.22f && ratio < 0.29f, "the meter reads the final output: -12 dB Output -> " + format (juce::Decibels::gainToDecibels (ratio), 2) + " dB");

        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;

        auto* version = dynamic_cast<juce::Label*> (editor.findChildWithID ("version"));
        check (version != nullptr && version->getText() == "v" + juce::String (JucePlugin_VersionString) && version->getText() == "v0.3.6",
               "header subtitle is the build version (v0.3.6)");
        juce::StringArray texts;
        collectLabels (editor, texts);
        bool tagLine = false;
        for (const auto& t : texts)
            tagLine = tagLine || t.containsIgnoreCase ("kick designer");
        check (! tagLine, "no 'kick designer' text left");

        auto& out = editor.getOutputMeter();
        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor.setSize (size.x, size.y);
            const auto tag = juce::String (size.x) + "x" + juce::String (size.y) + ": ";
            const auto header = editor.getHeaderArea();
            std::vector<juce::Component*> items;
            for (auto* c : editor.getChildren())
                if (c->isVisible() && c->getBounds().intersects (header) && c->getHeight() < header.getHeight())
                    items.push_back (c);
            bool inside = true, overlap = false;
            for (size_t i = 0; i < items.size(); ++i)
            {
                inside = inside && header.contains (items[i]->getBounds());
                for (size_t j = i + 1; j < items.size(); ++j)
                    overlap = overlap || items[i]->getBounds().intersects (items[j]->getBounds());
            }
            bool rightmost = true;
            for (auto* c : items)
                rightmost = rightmost && (c == &out || c->getRight() <= out.getX());
            auto* slider = editor.findChildWithID ("output_gain");
            check (items.size() >= 13 && inside && ! overlap, (tag + "header: " + juce::String ((int) items.size()) + " items, inside, no overlaps").toStdString());
            check (out.isVisible() && rightmost && out.getHeight() >= 36 && out.getWidth() >= 16, (tag + "header: the meter is at the far right, header height").toStdString());
            check (slider != nullptr && slider->getWidth() >= 112, (tag + "header: Output slider " + juce::String (slider != nullptr ? slider->getWidth() : 0) + " px (was 88 at 940)").toStdString());
        }

        // Snapshot: left yellow, right red with its clip light.
        editor.setSize (1080, 660);
        out.pushPeaks (0.0f, 0.0f, 0.0);
        out.pushPeaks (0.79f, 1.12f, 16.0);
        check (Meter::zoneFor (out.getLevelDb (0)) == Zone::yellow && Meter::zoneFor (out.getLevelDb (1)) == Zone::red && out.isClipLatched (1),
               "snapshot: left yellow, right red + clip");
        saveSnapshot (editor.createComponentSnapshot (editor.getHeaderArea()), "-v0.3.4-header");
    }

    void testEnvelopeInfoTextRemoved()
    {
        using params::Envelope;
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        setValues (p, { { params::envelopeCountId (Envelope::master), 4.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 60.0f }, { params::nodeValueId (Envelope::master, 1), 0.8f },
                        { params::nodeTimeId (Envelope::master, 2), 250.0f }, { params::nodeValueId (Envelope::master, 2), 0.4f },
                        { params::nodeTimeId (Envelope::master, 3), 700.0f }, { params::nodeValueId (Envelope::master, 3), 0.0f } });

        auto greyTextPixels = [] (dooblrkick::ui::EnvelopeEditor& env)
        {
            const auto image = env.createComponentSnapshot (env.getLocalBounds(), true, 1.0f);
            const auto plot = env.getPlotArea();
            std::vector<juce::Point<float>> marks;
            for (int n = 0; n < env.getNumNodes(); ++n)
                marks.push_back (env.getNodePosition (n));
            for (int s = 0; s + 1 < env.getNumNodes(); ++s)
                marks.push_back (env.getCurveHandlePosition (s));

            int count = 0;
            for (int y = (int) plot.getY() + 1; y < (int) plot.getBottom() - 1; ++y)
                for (int x = (int) plot.getX() + 1; x < (int) plot.getRight() - 1; ++x)
                {
                    const juce::Point<float> q ((float) x, (float) y);
                    if (std::any_of (marks.begin(), marks.end(), [q] (auto m) { return m.getDistanceFrom (q) < 10.0f; }))
                        continue;
                    const auto c = image.getPixelAt (x, y);
                    if (c.getBrightness() > 0.4f && c.getSaturation() < 0.15f)
                        ++count;
                }
            return count;
        };

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor.setSize (size.x, size.y);
            const auto tag = juce::String (size.x) + "x" + juce::String (size.y) + ": ";
            editor.selectPage (1); // Body
            editor.showEnvelope (Envelope::pitch);
            check (greyTextPixels (editor.getEnvelopeEditor()) == 0, (tag + "no info text in the Body pitch envelope").toStdString());
            editor.showEnvelope (Envelope::amp);
            check (greyTextPixels (editor.getEnvelopeEditor()) == 0, (tag + "no info text in the Body amp envelope").toStdString());
            editor.selectPage (Editor::envelopePage);
            check (greyTextPixels (editor.getMasterEnvelopeEditor()) == 0, (tag + "no info text in the master envelope").toStdString());
            setValues (p, { { "menv_on", 0.0f } });
            editor.getMasterEnvelopeEditor().repaint();
            check (editor.getMasterEnvelopeEditor().isBypassedLook() && greyTextPixels (editor.getMasterEnvelopeEditor()) == 0,
                   (tag + "no status caption on the master envelope while it is off (the graph is just dimmed)").toStdString());
            setValues (p, { { "menv_on", 1.0f } });
        }

        editor.setSize (1080, 660);
        editor.selectPage (Editor::envelopePage);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.4-envelope");
        editor.selectPage (1);
        editor.showEnvelope (Envelope::pitch);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.4-body");
    }

    // v0.3.3: master envelope knobs - per-point Time / Level / Curve knobs bound to the selected
    // graph point's host parameters, plus two automatable macros (Time Scale, Depth).
    void testEnvelopeKnobs()
    {
        using params::Envelope;
        {
            auto p = makeProcessor();
            const auto* tscale = getParam (*p, "menv_tscale");
            const auto* depth = getParam (*p, "menv_depth");
            check (tscale != nullptr && depth != nullptr && tscale->isAutomatable() && depth->isAutomatable()
                       && tscale->getName (64) == "Master Env Time Scale" && depth->getName (64) == "Master Env Depth",
                   "Time Scale and Depth are automatable host parameters with clear names");
            check (getPlain (*p, "menv_tscale") == 1.0f && getPlain (*p, "menv_depth") == 1.0f
                       && std::abs (tscale->getDefaultValue() - 0.5f) < 1.0e-4f,
                   "they default to 100 % (Time Scale's 100 % sits at the knob's centre)");

            // Transparent at the defaults: the envelope the DSP gets is exactly the parameters.
            setValues (*p, { { params::envelopeCountId (Envelope::master), 4.0f },
                             { params::nodeValueId (Envelope::master, 0), 1.0f },
                             { params::nodeTimeId (Envelope::master, 1), 37.3f }, { params::nodeValueId (Envelope::master, 1), 0.81f },
                             { params::nodeTimeId (Envelope::master, 2), 211.7f }, { params::nodeValueId (Envelope::master, 2), 0.33f },
                             { params::nodeTimeId (Envelope::master, 3), 640.1f }, { params::nodeValueId (Envelope::master, 3), 0.0f },
                             { params::nodeCurveId (Envelope::master, 1), 0.4f } });
            auto settings = params::SettingsReader (p->getParameterState()).read();
            bool exact = settings.master.envelope.numNodes == 4;
            for (int n = 1; n < 4; ++n)
                exact = exact && settings.master.envelope.nodes[(size_t) n].timeMs == getPlain (*p, params::nodeTimeId (Envelope::master, n))
                        && settings.master.envelope.nodes[(size_t) n].value == getPlain (*p, params::nodeValueId (Envelope::master, n));
            check (exact, "at 100 % the master envelope is exactly its point parameters (bit-exact)");

            // Time Scale and Depth, set as a host would (automation), change the render as expected.
            setValues (*p, { { params::envelopeCountId (Envelope::master), 3.0f },
                             { params::nodeTimeId (Envelope::master, 1), 100.0f }, { params::nodeValueId (Envelope::master, 1), 1.0f },
                             { params::nodeTimeId (Envelope::master, 2), 200.0f }, { params::nodeValueId (Envelope::master, 2), 0.0f },
                             { params::nodeCurveId (Envelope::master, 0), 0.0f }, { params::nodeCurveId (Envelope::master, 1), 0.0f } });
            const auto sr = p->getSampleRate();
            const auto window = [sr] (const juce::AudioBuffer<float>& b) { return peakOf (b, (int) (0.25 * sr), (int) (0.35 * sr)); };
            const auto plain = renderNote (*p, 36, 1.0f, 0.6);
            setParam (*p, "menv_tscale", 2.0f);
            settings = params::SettingsReader (p->getParameterState()).read();
            const auto scaled = renderNote (*p, 36, 1.0f, 0.6);
            check (std::abs (settings.master.envelope.getEndTimeMs() - 400.0f) < 0.5f && window (plain) < 1.0e-3f && window (scaled) > 0.01f,
                   "Time Scale 200 % doubles the envelope (ends at " + format (settings.master.envelope.getEndTimeMs(), 1)
                       + " ms; 250-350 ms peak " + format (window (plain), 4) + " -> " + format (window (scaled), 3) + ")");
            setParam (*p, "menv_tscale", 0.5f);
            settings = params::SettingsReader (p->getParameterState()).read();
            check (std::abs (settings.master.envelope.getEndTimeMs() - 100.0f) < 0.5f, "Time Scale 50 % halves it");
            setParam (*p, "menv_tscale", 1.0f);

            setParam (*p, "menv_depth", 0.5f);
            settings = params::SettingsReader (p->getParameterState()).read();
            const auto half = renderNote (*p, 36, 1.0f, 0.6);
            check (std::abs (settings.master.envelope.getEndValue() - 0.5f) < 1.0e-5f && window (half) > 0.01f,
                   "Depth 50 % lifts the envelope's floor to 0.5 (audible after the envelope ends)");
            setParam (*p, "menv_depth", 0.0f);
            const auto flat = renderNote (*p, 36, 1.0f, 0.6);
            setParam (*p, "menv_on", 0.0f);
            const auto bypass = renderNote (*p, 36, 1.0f, 0.6);
            check (maxDifference (flat, bypass) < 1.0e-6f, "Depth 0 % is flat: it sounds as the envelope switched off ("
                                                             + format (maxDifference (flat, bypass), 8) + ")");

            // An older (version 4) state, without the two parameters, loads them at 100 %.
            auto writer = makeProcessor();
            writer->loadFactoryPreset (3, false);
            juce::MemoryBlock state;
            writer->getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            auto tree = juce::ValueTree::fromXml (*xml);
            tree.setProperty ("stateVersion", 4, nullptr);
            for (int i = tree.getNumChildren(); --i >= 0;)
                if (const auto id = tree.getChild (i).getProperty ("id").toString(); id == "menv_tscale" || id == "menv_depth")
                    tree.removeChild (i, nullptr);
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldState);
            auto q = makeProcessor();
            setValues (*q, { { "menv_tscale", 3.0f }, { "menv_depth", 0.2f } });
            q->setStateInformation (oldState.getData(), (int) oldState.getSize());
            check (getPlain (*q, "menv_tscale") == 1.0f && getPlain (*q, "menv_depth") == 1.0f
                       && exactDifference (renderMidi (*writer, twoHits(), 36000), renderMidi (*q, twoHits(), 36000)) == 0.0f,
                   "a version 4 state loads Time Scale and Depth at 100 % and sounds bit-identical");
        }

        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        auto& undo = p.getUndoHistory();
        editor.setSize (1080, 660);
        editor.selectPage (Editor::envelopePage);
        auto& graph = editor.getMasterEnvelopeEditor();
        auto& knobs = editor.getEnvelopePointKnobs();
        setValues (p, { { params::envelopeCountId (Envelope::master), 4.0f },
                        { params::nodeValueId (Envelope::master, 0), 1.0f },
                        { params::nodeTimeId (Envelope::master, 1), 60.0f }, { params::nodeValueId (Envelope::master, 1), 0.8f },
                        { params::nodeTimeId (Envelope::master, 2), 250.0f }, { params::nodeValueId (Envelope::master, 2), 0.4f },
                        { params::nodeTimeId (Envelope::master, 3), 700.0f }, { params::nodeValueId (Envelope::master, 3), 0.0f } });
        graph.resetView();

        // Graph -> knobs: selecting a point rebinds the knobs to its parameters.
        graph.setSelectedNode (2);
        check (knobs.getPoint() == 2 && knobs.getBoundParameterId (0) == params::nodeTimeId (Envelope::master, 2)
                   && knobs.getBoundParameterId (1) == params::nodeValueId (Envelope::master, 2)
                   && knobs.getBoundParameterId (2) == params::nodeCurveId (Envelope::master, 1),
               "selecting point 3 in the graph binds Time / Level / Curve to its parameters (curve: the segment into it)");
        check (std::abs (knobs.getTimeKnob().getValue() - 250.0) < 0.01 && std::abs (knobs.getLevelKnob().getValue() - 0.4) < 1.0e-4,
               "and the knobs show its values");
        {
            const auto at = graph.getNodePosition (1);
            graph.mouseDown (makeMouseEvent (graph, at, at, 1, false));
            graph.mouseUp (makeMouseEvent (graph, at, at, 1, false));
            check (knobs.getPoint() == 1 && graph.getSelectedNode() == 1, "clicking a point in the graph selects it for the knobs");
        }
        graph.setSelectedNode (0);
        check (knobs.getPoint() == 0 && ! knobs.getTimeKnob().isEnabled() && ! knobs.getCurveKnob().isEnabled() && knobs.getLevelKnob().isEnabled()
                   && knobs.getBoundParameterId (1) == params::nodeValueId (Envelope::master, 0),
               "the first point: Level only (its time is fixed at 0 ms and it has no segment before it)");

        // Knob selector -> graph.
        graph.setSelectedNode (1);
        knobs.getNextButton().onClick();
        check (knobs.getPoint() == 2 && graph.getSelectedNode() == 2, "> selects the next point in the graph too");
        knobs.getPreviousButton().onClick();
        check (knobs.getPoint() == 1 && graph.getSelectedNode() == 1, "< selects the previous point");

        // Parameter (automation) -> knob, and knob -> parameter -> graph, one undo step.
        graph.setSelectedNode (2);
        setParam (p, params::nodeValueId (Envelope::master, 2), 0.3f);
        check (std::abs (knobs.getLevelKnob().getValue() - 0.3) < 1.0e-4, "an automated point level shows on the Level knob");
        const auto before = graph.getNodePosition (2);
        undo.clear();
        auto& time = knobs.getTimeKnob();
        {
            const auto c = time.getLocalBounds().getCentre().toFloat();
            simulateDrag (time, c, c + juce::Point<float> (0.0f, -60.0f));
        }
        const auto after = graph.getNodePosition (2);
        const auto dragged = getPlain (p, params::nodeTimeId (Envelope::master, 2));
        check (dragged > 260.0f && std::abs (time.getValue() - dragged) < 0.6f && after.x > before.x + 2.0f,
               "dragging the Time knob moves the point's parameter (250 -> " + format (dragged, 1) + " ms) and the graph follows ("
                   + format (before.x, 1) + " -> " + format (after.x, 1) + " px)");
        check (undo.getNumUndoableSteps() == 1, "one knob turn is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");
        undo.undo();
        check (std::abs (getPlain (p, params::nodeTimeId (Envelope::master, 2)) - 250.0f) < 0.6f && std::abs (time.getValue() - 250.0) < 0.6,
               "undo puts the point and the knob back");

        // Time Scale on the panel: the knob is its host parameter and the graph shows the result.
        if (auto* scale = dynamic_cast<juce::Slider*> (findById (editor, "menv_tscale")))
        {
            undo.clear();
            const auto c = scale->getLocalBounds().getCentre().toFloat();
            simulateDrag (*scale, c, c + juce::Point<float> (0.0f, -40.0f));
            const auto effective = graph.getEffectiveEnvelope();
            const auto factor = getPlain (p, "menv_tscale");
            check (factor > 1.05f && graph.showsEffectiveCurve()
                       && std::abs (effective.getEndTimeMs() - 700.0f * factor) < 1.0f && undo.getNumUndoableSteps() == 1,
                   "dragging the Time Scale knob sets its parameter (" + format (factor * 100.0f, 0) + " %, one undo step) and the graph draws the scaled curve (ends at "
                       + format (effective.getEndTimeMs(), 0) + " ms)");
        }
        else
            check (false, "Time Scale knob found");
        check (findById (editor, "menv_depth") != nullptr && findById (editor, "menv_depth")->isVisible(), "Depth knob shown");

        // Everything on the Envelope page fits at the three sizes.
        for (const auto [w, hgt] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor.setSize (w, hgt);
            const auto size = std::to_string (w) + "x" + std::to_string (hgt);
            std::map<juce::String, juce::Component*> visible;
            for (const auto& panel : editor.getPanels (Editor::envelopePage))
                collectComponents (*panel, visible, true);
            int problems = 0;
            std::vector<std::pair<juce::String, juce::Rectangle<int>>> leaves;
            for (const auto& [id, c] : visible)
            {
                if (id == "menv_point_knobs")
                    continue;
                const auto b = editor.getLocalArea (c->getParentComponent(), c->getBounds());
                if (! editor.getLocalBounds().contains (b))
                    ++problems, note ("  " + size + ": " + id.toStdString() + " outside the editor");
                for (const auto& [other, ob] : leaves)
                    if (ob.intersects (b))
                        ++problems, note ("  " + size + ": " + id.toStdString() + " overlaps " + other.toStdString());
                leaves.push_back ({ id, b });
            }
            bool all = true;
            for (const auto* id : { "menv_knob_time", "menv_knob_level", "menv_knob_curve", "menv_point_prev", "menv_point_next", "menv_tscale", "menv_depth" })
                all = all && visible.count (id) == 1;
            check (all && problems == 0, size + ": the envelope knobs are shown, inside the editor, with no overlaps");
        }

        editor.setSize (1080, 660);
        setValues (p, { { "menv_tscale", 1.4f }, { "menv_depth", 0.75f } });
        graph.setSelectedNode (2);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.3-env-knobs");
    }

    void testEnvelopePage()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        using params::Envelope;
        editor.setSize (1080, 660);

        std::map<juce::String, juce::Component*> all;
        collectComponents (editor, all, false);
        check (all.count ("page_Envelope") == 1 && all.count ("page_Play") == 0, "the Play tab is now Envelope");
        check (all.count ("page_Drive") == 1 && all.count ("page_EQ") == 1 && all.count ("page_Dynamics") == 1 && all.count ("page_Limiter") == 1,
               "Drive, EQ, Dynamics, Limiter tabs kept");

        editor.selectPage (Editor::envelopePage);
        auto& env = editor.getMasterEnvelopeEditor();
        auto& mix = editor.getMixDisplay();
        check (env.isVisible() && env.getEnvelope() == Envelope::master && env.getWidth() > 300 && env.getHeight() > 150,
               "Envelope page shows the master envelope editor (" + std::to_string (env.getWidth()) + "x" + std::to_string (env.getHeight()) + ")");
        check (mix.isVisible() && mix.getHeight() >= 72 && mix.getBottom() <= editor.getHeight(), "and the Dry/Wet view");
        check (! editor.getEnvelopeEditor().isVisible() && ! editor.getDriveDisplay().isVisible(), "no layer envelope or drive view on it");

        std::map<juce::String, juce::Component*> visible;
        collectComponents (editor, visible, true);
        bool playControls = true;
        for (const auto* id : { "root_note", "vel_sens", "gate", "release", "mix_drywet", "chain_order" })
            playControls = playControls && visible.count (id) == 1;
        check (playControls, "Key, Velocity, Gate, Release and Dry/Wet / Chain order controls are on the Envelope page");
        check (visible.count ("menv_on") == 0 && visible.count ("power_envelope") == 1,
               "the envelope on / off is the Envelope tab's power toggle, not a switch in the panel (v0.3.3)");

        auto& undo = p.getUndoHistory();
        undo.clear();
        check (env.getNumNodes() == 2, "two points by default");
        const auto from = env.getNodePosition (1);
        simulateDrag (env, from, from + juce::Point<float> (-60.0f, 80.0f));
        check (getPlain (p, params::nodeValueId (Envelope::master, 1)) < 0.9f && getPlain (p, params::nodeTimeId (Envelope::master, 1)) < 1000.0f,
               "dragging the end point left / down shortens and lowers it (" + format (getPlain (p, params::nodeTimeId (Envelope::master, 1)), 0)
                   + " ms, " + format (100.0f * getPlain (p, params::nodeValueId (Envelope::master, 1)), 0) + " %)");
        check (undo.getNumUndoableSteps() == 1, "a point drag is one undo step");
        const auto plot = env.getPlotArea();
        simulateDoubleClick (env, { (env.getNodePosition (0).x + env.getNodePosition (1).x) * 0.5f, plot.getY() + plot.getHeight() * 0.7f });
        check (env.getNumNodes() == 3 && (int) getPlain (p, params::envelopeCountId (Envelope::master)) == 3, "double-click adds a point");
        simulateDrag (env, env.getCurveHandlePosition (0), env.getCurveHandlePosition (0) + juce::Point<float> (0.0f, 30.0f));
        check (std::abs (getPlain (p, params::nodeCurveId (Envelope::master, 0))) > 0.1f, "segment curves are draggable");

        // Live: playing feeds the meters; the shaped hit is quieter.
        playAndPoll (p, editor, 0.6);
        check (mix.getFramesReceived() > 5 && mix.getShownDb()[2] > -40.0f, "Dry/Wet meters animate during playback (out "
                                                                                + format (mix.getShownDb()[2], 1) + " dB, " + std::to_string (mix.getFramesReceived()) + " frames)");
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.0-envelope");

        // Dry/Wet view: drag sets it in one gesture; double-click resets.
        undo.clear();
        const auto area = mix.getPlotArea();
        simulateDrag (mix, { area.getRight() - 2.0f, area.getCentreY() }, { area.getX() + area.getWidth() * 0.25f, area.getCentreY() });
        check (std::abs (getPlain (p, "mix_drywet") - 0.25f) < 0.03f, "dragging the Dry/Wet view to a quarter sets 25 % ("
                                                                         + format (100.0f * getPlain (p, "mix_drywet"), 1) + " %)");
        check (undo.getNumUndoableSteps() == 1, "one undo step");
        check (std::abs (mix.getMarkerX() - (area.getX() + area.getWidth() * 0.25f)) < 3.0f, "the marker follows");
        playAndPoll (p, editor, 0.4);
        check (mix.getShownDb()[0] > mix.getShownDb()[2] - 30.0f && mix.getShownDb()[1] > -60.0f, "dry and wet meters both show signal");
        simulateDoubleClick (mix, area.getCentre());
        check (getPlain (p, "mix_drywet") == 1.0f, "double-click returns to 100 % wet");

        // The knob and the view are the same parameter.
        auto* knob = dynamic_cast<juce::Slider*> (visible["mix_drywet"]);
        if (knob != nullptr)
        {
            knob->setValue (knob->proportionOfLengthToValue (0.5), juce::sendNotificationSync);
            check (std::abs (getPlain (p, "mix_drywet") - 0.5f) < 0.02f, "the Dry/Wet knob drives the same parameter");
        }
    }

    void testDriveView()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        editor.selectPage (Editor::drivePage);
        auto& drive = editor.getDriveDisplay();
        check (drive.isVisible() && drive.getWidth() > 400 && drive.getHeight() > 200, "Drive page shows the drive view ("
                                                                                            + std::to_string (drive.getWidth()) + "x" + std::to_string (drive.getHeight()) + ")");
        check (! editor.getEnvelopeEditor().isVisible() && ! editor.getMasterEnvelopeEditor().isVisible(), "not the body envelope any more");

        std::map<juce::String, juce::Component*> visible;
        collectComponents (editor, visible, true);
        bool controls = true;
        for (const auto* id : { "dist_type", "dist_drive", "dist_tone", "dist_mix", "dist_bits", "dist_rate" })
            controls = controls && visible.count (id) == 1;
        check (controls && visible.count ("power_drive") == 1, "all drive parameters have controls on the page (on / off on the Drive tab)");
        // Sends stay on their layer pages and still decide what reaches Drive.
        bool sends = true;
        for (int layer = 0; layer < params::numLayers; ++layer)
        {
            editor.selectPage (layer);
            std::map<juce::String, juce::Component*> onLayer;
            collectComponents (editor, onLayer, true);
            sends = sends && onLayer.count (params::layerId (layer, "send")) == 1;
        }
        check (sends, "each layer page still has its Drive send");
        {
            auto render = [] (float send, bool driveOn)
            {
                auto q = makeProcessor();
                isolateLayer (*q, dsp::bodyLayer);
                setValues (*q, { { "dist_on", driveOn ? 1.0f : 0.0f }, { "dist_type", 1 }, { "dist_drive", 30 }, { "body_send", send } });
                return renderMidi (*q, twoHits(), 24000);
            };
            check (maxDifference (render (0.0f, true), render (0.0f, false)) < 1.0e-6f, "send 0: Drive leaves the layer alone");
            check (maxDifference (render (1.0f, true), render (1.0f, false)) > 0.05f, "send 100 %: Drive changes the layer");
        }
        editor.selectPage (Editor::drivePage);

        // The curve is the DSP's shaper: a full-scale 1 kHz sine through Distortion peaks where the curve does
        // (1 kHz keeps the 10 Hz DC blocker's droop out of the peak).
        for (const auto type : { 0, 1, 3 })
        {
            setValues (p, { { "dist_on", 1 }, { "dist_type", (float) type }, { "dist_drive", 14 }, { "dist_tone", 1 }, { "dist_mix", 1 } });
            float curvePeak = 0.0f;
            for (int i = -400; i <= 400; ++i)
                curvePeak = std::max (curvePeak, std::abs (drive.transfer ((float) i / 400.0f)));
            dsp::Distortion d;
            d.prepare (sampleRate, 512);
            juce::AudioBuffer<float> sine (2, 48000);
            for (int i = 0; i < 48000; ++i)
                sine.setSample (0, i, std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / 48000.0f)), sine.setSample (1, i, sine.getSample (0, i));
            const auto s = params::SettingsReader (p.getParameterState()).read();
            for (int start = 0; start < 48000; start += 512)
                d.process (sine, start, 512, s.drive);
            const auto measured = peakOf (sine, 24000);
            const auto tolerance = type == 0 ? 0.3f : 1.2f; // hard corners ring in the oversampling filters
            check (std::abs (linearToDb (measured) - linearToDb (curvePeak)) < tolerance, "type " + std::to_string (type) + ": curve peak "
                                                                                       + format (linearToDb (curvePeak), 2) + " dB vs DSP " + format (linearToDb (measured), 2) + " dB");
        }

        auto& undo = p.getUndoHistory();
        setValues (p, { { "dist_on", 0 }, { "dist_type", 0 }, { "dist_drive", 6 }, { "dist_mix", 0.5f } });
        drive.repaint();
        check (std::abs (drive.transfer (0.5f) - 0.5f) < 1.0e-6f, "off: the curve is the identity");
        undo.clear();
        const auto plot = drive.getTransferArea();
        simulateDrag (drive, plot.getCentre(), plot.getCentre() + juce::Point<float> (plot.getWidth() * 0.25f, -plot.getHeight() * 0.25f));
        check (getPlain (p, "dist_on") < 0.5f, "dragging the curve leaves Drive off (only its tab toggle switches it)");
        check (std::abs (getPlain (p, "dist_drive") - 18.0f) < 1.0f && std::abs (getPlain (p, "dist_mix") - 0.75f) < 0.03f,
               "up = more drive, right = more mix (" + format (getPlain (p, "dist_drive"), 1) + " dB, " + format (100.0f * getPlain (p, "dist_mix"), 0) + " %)");
        check (undo.getNumUndoableSteps() == 1, "one undo step for the drag");
        undo.undo();
        check (getPlain (p, "dist_on") < 0.5f && std::abs (getPlain (p, "dist_drive") - 6.0f) < 0.01f && std::abs (getPlain (p, "dist_mix") - 0.5f) < 0.01f,
               "undo restores drive / mix together");
        setValues (p, { { "dist_on", 1 }, { "dist_drive", 18 }, { "dist_mix", 0.75f } });
        check (drive.transfer (0.5f) > 0.5f + 0.05f, "switched on, the curve bends with the dragged settings");

        // Type segments.
        setParam (p, "dist_on", 1.0f);
        const auto seg = drive.getTypeArea (3).getCentre();
        drive.mouseDown (makeMouseEvent (drive, seg, seg, 1, false));
        drive.mouseUp (makeMouseEvent (drive, seg, seg, 1, false));
        check ((int) getPlain (p, "dist_type") == 3, "clicking the Fold segment picks Fold");

        // Harmonics: more drive, more harmonics.
        setValues (p, { { "dist_type", 0 }, { "dist_mix", 1 }, { "dist_drive", 2 } });
        drive.createComponentSnapshot (drive.getLocalBounds());
        const auto lowH3 = drive.getHarmonicDb (3);
        setParam (p, "dist_drive", 30.0f);
        drive.createComponentSnapshot (drive.getLocalBounds());
        const auto highH3 = drive.getHarmonicDb (3);
        check (highH3 > lowH3 + 10.0f, "3rd harmonic rises with drive (" + format (lowH3, 1) + " -> " + format (highH3, 1) + " dBFS)");
        check (drive.getHarmonicDb (2) < highH3 - 20.0f, "soft clip is odd-symmetric: 2nd harmonic far below the 3rd");
        setParam (p, "dist_type", 2.0f);
        drive.createComponentSnapshot (drive.getLocalBounds());
        check (drive.getHarmonicDb (2) > -60.0f, "tube is asymmetric: it shows a 2nd harmonic (" + format (drive.getHarmonicDb (2), 1) + " dBFS)");

        // Live input dot.
        setValues (p, { { "dist_type", 0 }, { "dist_drive", 14 }, { "body_send", 1 } });
        playAndPoll (p, editor, 0.6);
        check (drive.getFramesReceived() > 5 && drive.getShownInputDb() > -40.0f, "the live input level animates during playback ("
                                                                                     + format (drive.getShownInputDb(), 1) + " dB)");
        const auto image = drive.createComponentSnapshot (drive.getLocalBounds());
        int lit = 0;
        for (int y = 0; y < image.getHeight(); y += 3)
            for (int x = 0; x < image.getWidth(); x += 3)
                lit += image.getPixelAt (x, y).getBrightness() > 0.5f ? 1 : 0;
        check (lit > 50, "the drive view paints its curve and readouts (" + std::to_string (lit) + " bright samples)");
        p.loadFactoryPreset (1, false);
        setValues (p, { { "dist_on", 1 }, { "dist_type", 2 }, { "dist_drive", 16 }, { "dist_mix", 0.85f } });
        playAndPoll (p, editor, 0.3);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.0-drive");
    }

    void testLayoutFit()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        int previousHeight = 0;

        for (const auto [w, hgt] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor.setSize (w, hgt);
            const auto size = std::to_string (w) + "x" + std::to_string (hgt);

            // Tabs fill the left column, evenly, without overlapping.
            int top = 1 << 20, bottom = 0, minHeight = 1 << 20, maxHeight = 0;
            bool overlap = false;
            for (int a = 0; a < Editor::numPages; ++a)
            {
                const auto ba = editor.getPageButton (a).getBounds();
                top = std::min (top, ba.getY());
                bottom = std::max (bottom, ba.getBottom());
                minHeight = std::min (minHeight, ba.getHeight());
                maxHeight = std::max (maxHeight, ba.getHeight());
                for (int b = a + 1; b < Editor::numPages; ++b)
                    overlap = overlap || ba.intersects (editor.getPageButton (b).getBounds());
            }
            check (! overlap, size + ": no tabs overlap");
            check (maxHeight - minHeight <= 0, size + ": all tabs share one height (" + std::to_string (minHeight) + " px)");
            check (hgt - bottom <= 12 + 4 + Editor::numPages && bottom <= hgt - 12, size + ": the tabs reach the bottom of the column (last tab ends "
                                                                                   + std::to_string (hgt - bottom) + " px above the edge)");
            check (minHeight > previousHeight, size + ": taller window, taller tabs (" + std::to_string (minHeight) + " px)");
            check (minHeight >= 40, size + ": tabs are at least 40 px tall (were 24-28 px in v0.2.0)");
            previousHeight = minHeight;

            for (int page = 0; page < Editor::numPages; ++page)
            {
                editor.selectPage (page);
                const auto name = size + " page " + std::to_string (page);

                // Nothing visible is clipped by its parent.
                int clipped = 0;
                std::function<void (juce::Component&)> checkBounds = [&] (juce::Component& parent)
                {
                    for (auto* child : parent.getChildren())
                        if (child->isVisible())
                        {
                            if (! parent.getLocalBounds().contains (child->getBounds()) && (child->getComponentID().isNotEmpty() || &parent == &editor))
                            {
                                ++clipped;
                                note ("  " + name + ": clipped " + child->getComponentID().toStdString() + " " + child->getBounds().toString().toStdString()
                                      + " in " + parent.getComponentID().toStdString() + " " + parent.getLocalBounds().toString().toStdString());
                            }
                            checkBounds (*child);
                        }
                };
                checkBounds (editor);
                check (clipped == 0, name + ": nothing is clipped");

                // Top-level siblings do not overlap.
                std::vector<juce::Component*> shown;
                for (auto* child : editor.getChildren())
                    if (child->isVisible() && ! child->getBounds().isEmpty())
                        shown.push_back (child);
                int overlaps = 0;
                for (size_t a = 0; a < shown.size(); ++a)
                    for (size_t b = a + 1; b < shown.size(); ++b)
                        if (shown[a]->getBounds().intersects (shown[b]->getBounds()))
                        {
                            ++overlaps;
                            note ("  " + name + ": overlap " + shown[a]->getComponentID().toStdString() + " " + shown[a]->getBounds().toString().toStdString()
                                  + " / " + shown[b]->getComponentID().toStdString() + " " + shown[b]->getBounds().toString().toStdString());
                        }
                check (overlaps == 0, name + ": no overlapping components");

                // Text controls are big enough to read.
                std::map<juce::String, juce::Component*> visible;
                collectComponents (editor, visible, true);
                int tiny = 0;
                for (const auto& [id, c] : visible)
                    if (dynamic_cast<juce::Slider*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr)
                        tiny += (c->getWidth() < 20 || c->getHeight() < 14) ? 1 : 0;
                check (tiny == 0, name + ": no collapsed controls");
            }

            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            check (image.getWidth() == w && image.getHeight() == hgt, size + ": paints");
            if (w == 940)
            {
                editor.selectPage (dsp::tailLayer); // the widest control rows
                saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.0-min-940x600");
            }
        }
    }

    //==============================================================================
    // v0.3.2: a layer with a Synth / Sample switch shows only its current source's controls.
    void testLayerSourceLayout()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;

        struct Expect { int layer; juce::StringArray synthOnly, sampleOnly, shared; };
        const std::vector<Expect> layers {
            { dsp::clickLayer, { "click_type" }, { "click_sample", "click_smp_penv", "click_loadSample", "click_sampleFile" },
              { "click_pitch", "click_tone", "click_keytrack" } },
            { dsp::bodyLayer, { "body_wave", "body_harm", "body_phase" },
              { "body_sample", "body_smp_penv", "body_loadSample", "body_sampleFile" }, { "body_tune", "body_drive", "body_keytrack" } },
            { dsp::tailLayer, { "tail_wave", "tail_spread", "tail_phase" }, { "tail_sample", "tail_smp_penv", "tail_loadSample", "tail_sampleFile" },
              { "tail_keytrack", "tail_tune", "tail_drive" } },
            { dsp::noiseLayer, { "noise_color", "noise_width" }, { "noise_sample", "noise_tune", "noise_smp_penv", "noise_loadSample", "noise_sampleFile" },
              { "noise_keytrack" } },
        };

        // Hidden values are kept: give the other source's controls distinctive values first.
        setValues (p, { { "click_type", 3 }, { "body_wave", 2 }, { "body_harm", 0.4f }, { "body_sample", 4 }, { "body_smp_penv", 0.6f },
                        { "tail_spread", 30 }, { "noise_width", 0.3f }, { "noise_tune", 5 } });
        const auto kept = [&]
        {
            std::map<juce::String, float> v;
            for (const auto* id : { "click_type", "body_wave", "body_harm", "body_sample", "body_smp_penv", "tail_spread", "noise_width", "noise_tune" })
                v[id] = getPlain (p, id);
            return v;
        }();

        for (const auto [w, hgt] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor.setSize (w, hgt);
            const auto size = std::to_string (w) + "x" + std::to_string (hgt);

            for (const auto& e : layers)
            {
                editor.selectPage (e.layer);
                std::map<juce::String, juce::Rectangle<int>> synthBounds, sampleBounds, synthBoundsFirst;
                std::vector<juce::Rectangle<int>> synthPanels;

                for (const auto sample : { false, true, false })
                {
                    // Like automation / a preset / undo: the parameter only, no click, no refresh call.
                    setParam (p, params::layerId (e.layer, "source"), sample ? 1.0f : 0.0f);
                    const auto name = size + " " + params::layerId (e.layer, sample ? "sample mode" : "synth mode").toStdString();

                    std::map<juce::String, juce::Component*> visible;
                    collectComponents (editor, visible, true);
                    auto shown = [&] (const juce::String& id) { return visible.count (id) == 1; };

                    bool right = true;
                    for (const auto& id : e.synthOnly)  right = right && shown (id) == ! sample;
                    for (const auto& id : e.sampleOnly) right = right && shown (id) == sample;
                    check (right, name + ": only this source's controls are shown");

                    bool shared = true;
                    for (const auto* common : { "source", "level", "pan", "start", "send", "ftype", "cutoff", "reso" })
                        shared = shared && shown (params::layerId (e.layer, common));
                    for (const auto& id : e.shared)
                        shared = shared && shown (id);
                    check (shared, name + ": the shared controls stay visible");

                    // v0.3.3 fixed layout: where every shown control sits, relative to the editor.
                    std::map<juce::String, juce::Rectangle<int>> placed;
                    const auto prefix = params::layerId (e.layer, "");
                    const auto inPage = [&] (juce::Component* c)
                    {
                        for (const auto& panel : editor.getPanels (e.layer))
                            if (panel->isParentOf (c))
                                return true;
                        return editor.getLevelFader (e.layer).isParentOf (c) || c == &editor.getKeyTrackSwitch (e.layer);
                    };
                    for (const auto& [id, c] : visible)
                        if (id.startsWith (prefix) && ! id.endsWith ("_fader") && inPage (c))
                            placed[id] = editor.getLocalArea (c->getParentComponent(), c->getBounds());
                    if (sample)
                        sampleBounds = placed;
                    else
                        synthBounds = placed;

                    // No overlaps, and nothing clipped by any parent up to the editor.
                    int problems = 0;
                    for (auto a = placed.begin(); a != placed.end(); ++a)
                    {
                        for (auto b = std::next (a); b != placed.end(); ++b)
                            if (a->second.intersects (b->second))
                            {
                                ++problems;
                                note ("  " + name + ": " + a->first.toStdString() + " overlaps " + b->first.toStdString());
                            }

                        for (auto* c = visible[a->first]; c != nullptr && c != &editor; c = c->getParentComponent())
                            if (c->getParentComponent() != nullptr && ! c->getParentComponent()->getLocalBounds().contains (c->getBounds()))
                            {
                                ++problems;
                                note ("  " + name + ": " + a->first.toStdString() + " is clipped");
                                break;
                            }
                    }
                    check (! placed.empty() && problems == 0, name + ": no overlaps and nothing clipped");

                    // The Level fader: vertical, at the left of the panels, as tall as them.
                    auto& fader = editor.getLevelFader (e.layer);
                    const auto& panels = editor.getPanels (e.layer);
                    const auto faderBounds = editor.getLocalArea (fader.getParentComponent(), fader.getBounds());
                    const auto inEditor = [&] (juce::Component& c) { return editor.getLocalArea (c.getParentComponent(), c.getBounds()); };
                    int panelsLeft = editor.getWidth();
                    for (const auto& panel : panels)
                        panelsLeft = std::min (panelsLeft, inEditor (*panel).getX());
                    for (const auto& [id, b] : placed)
                        if (! id.endsWith ("_level") && ! id.endsWith ("_keytrack"))
                            panelsLeft = std::min (panelsLeft, b.getX());
                    // v0.3.5: the Key Track switch sits under the fader; together they span the panels.
                    const auto keyBounds = inEditor (editor.getKeyTrackSwitch (e.layer));
                    const auto slider = fader.getSlider().getSliderStyle() == juce::Slider::LinearVertical;
                    if (! (fader.isVisible() && slider && faderBounds.getRight() <= panelsLeft))
                        note ("  " + name + ": panels start at x " + std::to_string (panelsLeft));
                    check (fader.isVisible() && slider && visible.count (params::layerId (e.layer, "level")) == 1 && faderBounds.getRight() <= panelsLeft
                               && faderBounds.getHeight() * 2 >= 5 * faderBounds.getWidth()
                               && std::abs (faderBounds.getY() - inEditor (*panels.front()).getY()) <= 2 && std::abs (keyBounds.getBottom() - inEditor (*panels.back()).getBottom()) <= 2
                               && keyBounds.getY() >= faderBounds.getBottom() && keyBounds.getRight() <= panelsLeft && editor.getKeyTrackSwitch (e.layer).isVisible(),
                           name + ": the Level fader + Key switch column is vertical, left of the panels, and spans them (" + faderBounds.toString().toStdString() + ")");

                    if (sample)
                    {
                        int moved = 0, shared = 0;
                        for (const auto& [id, b] : synthBounds)
                            if (auto other = sampleBounds.find (id); other != sampleBounds.end())
                            {
                                ++shared;
                                if (other->second != b)
                                {
                                    ++moved;
                                    note ("  " + name + ": " + id.toStdString() + " moves " + b.toString().toStdString() + " -> " + other->second.toString().toStdString());
                                }
                            }
                        std::vector<juce::Rectangle<int>> panelNow;
                        for (const auto& panel : panels)
                            panelNow.push_back (panel->getBounds());
                        check (shared >= 8 && moved == 0 && panelNow == synthPanels,
                               name + ": all " + std::to_string (shared) + " shared controls and the panels keep exactly their Synth bounds");
                    }
                    else
                    {
                        std::vector<juce::Rectangle<int>> panelNow;
                        for (const auto& panel : panels)
                            panelNow.push_back (panel->getBounds());
                        if (! synthPanels.empty())
                            check (panelNow == synthPanels && placed == synthBoundsFirst, name + ": back to Synth, the same layout as before");
                        synthPanels = panelNow;
                        synthBoundsFirst = placed;
                    }
                }
            }
        }

        bool same = true;
        for (const auto& [id, value] : kept)
            same = same && std::abs (getPlain (p, id) - value) < 1.0e-4f;
        check (same, "hidden controls keep their parameter values");

        // Undo and Random follow too (the source parameter changes, the page follows at once).
        editor.setSize (1080, 660);
        editor.selectPage (dsp::bodyLayer);
        setParam (p, "body_source", 0.0f);
        auto& undo = p.getUndoHistory();
        undo.clear();
        if (auto* box = dynamic_cast<juce::ComboBox*> (findById (editor, "body_source")))
        {
            box->setSelectedItemIndex (1, juce::sendNotificationSync);
            check (findById (editor, "body_sample") != nullptr && findById (editor, "body_sample")->isVisible()
                       && ! findById (editor, "body_wave")->isVisible(), "picking Sample in the switch shows the sample controls");
            undo.undo();
            check (getPlain (p, "body_source") < 0.5f && findById (editor, "body_wave")->isVisible()
                       && ! findById (editor, "body_sample")->isVisible(), "undo back to Synth shows the synth controls");
        }
        else
            check (false, "body source switch found");

        for (int seed = 1; seed <= 12; ++seed)
        {
            juce::Random random (seed);
            p.randomise (random);
            const auto sampled = getPlain (p, "body_source") > 0.5f;
            if (findById (editor, "body_wave")->isVisible() == sampled || findById (editor, "body_sample")->isVisible() != sampled)
            {
                check (false, "Random seed " + std::to_string (seed) + ": the page follows the randomised source");
                break;
            }
        }

        // Snapshots of the default kick's Body page in each mode.
        EditorHolder shot;
        if (shot.editor != nullptr)
        {
            shot.editor->setSize (1080, 660);
            shot.editor->selectPage (dsp::bodyLayer);
            saveSnapshot (shot.editor->createComponentSnapshot (shot.editor->getLocalBounds()), "-v0.3.3-body-synth");
            setParam (*shot.processor, "body_source", 1.0f);
            saveSnapshot (shot.editor->createComponentSnapshot (shot.editor->getLocalBounds()), "-v0.3.3-body-sample");
        }
    }

    //==============================================================================
    // v0.3.3: the output graph draws an anti-aliased, sub-pixel min/max envelope.
    void testOutputGraph()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        editor.setSize (1080, 660);
        editor.selectPage (dsp::bodyLayer);
        auto& wave = editor.getWaveformView();
        wave.refreshNow();
        check (wave.hasContent(), "the output graph has the rendered kick");

        // A Retina-scale crop of the graph (what the snapshots show before / after).
        const auto crop = wave.createComponentSnapshot (wave.getLocalBounds(), true, 2.0f);
        check (crop.getWidth() == wave.getWidth() * 2, "the graph paints at 2x");
        saveSnapshot (crop, "-v0.3.3-graph");
        const auto expected = juce::jlimit (2, 4096, (int) std::ceil ((wave.getWidth() - 16) * 2.0f)); // the plot is inset 8 px each side
        check (wave.getGraphScale() == 2.0f && wave.getGraphColumns() == expected,
               "min / max per physical pixel column at 2x (" + std::to_string (wave.getGraphColumns()) + " columns for " + std::to_string (wave.getWidth()) + " px)");
        check (wave.getLocalBounds().toFloat().expanded (1.0f).contains (wave.getGraphFill().getBounds()) && ! wave.getGraphFill().isEmpty(),
               "the filled band stays inside the graph");

        // Cached: repaints at the same scale do not rebuild; a rebuild allocates nothing more than a cached paint.
        juce::Image image (juce::Image::ARGB, wave.getWidth() * 2, wave.getHeight() * 2, true);
        const auto paintOnce = [&]
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            allocationCount.store (0);
            countAllocations.store (true);
            wave.paint (g);
            countAllocations.store (false);
            return allocationCount.load();
        };
        const auto rebuilds = wave.getGraphRebuilds();
        const auto cachedAllocs = paintOnce();
        check (wave.getGraphRebuilds() == rebuilds, "repainting at the same scale reuses the cached graph");
        wave.resized(); // marks the graph dirty
        const auto rebuildAllocs = paintOnce();
        check (wave.getGraphRebuilds() == rebuilds + 1 && rebuildAllocs <= cachedAllocs,
               "a rebuild allocates nothing of its own (" + std::to_string (rebuildAllocs) + " vs " + std::to_string (cachedAllocs) + " in the renderer)");

        // Headless paint cost (software renderer, 2x): cached and rebuilding.
        const auto timeIt = [&] (bool rebuild)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 100; ++i)
            {
                if (rebuild) wave.resized();
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                wave.paint (g);
            }
            return (juce::Time::getMillisecondCounterHiRes() - start) / 100.0;
        };
        const auto cachedMs = timeIt (false), rebuildMs = timeIt (true);
        note ("    output graph paint (software, 2x, " + std::to_string (image.getWidth()) + "x" + std::to_string (image.getHeight()) + "): cached "
              + format (cachedMs, 3) + " ms, rebuilding " + format (rebuildMs, 3) + " ms");
        check (rebuildMs < 8.0, "paint stays cheap");
    }

    //==============================================================================
    // v0.3.1: processors are switched on / off by power toggles on their tabs.
    void testProcessorTabToggles()
    {
        EditorHolder h;
        check (h.editor != nullptr, "editor created");
        if (h.editor == nullptr)
            return;
        auto& editor = *h.editor;
        auto& p = *h.processor;
        editor.setSize (1080, 660);
        editor.selectPage (dsp::bodyLayer);
        auto& undo = p.getUndoHistory();

        // Each processing tab carries its own toggle: a child of the tab, left of the label, clear of the handle.
        bool placed = true;
        for (int stage = 0; stage < dsp::numChainStages; ++stage)
        {
            auto& tab = editor.getPageButton (Editor::pageForStage (stage));
            auto& power = editor.getPowerButton (stage);
            const auto b = power.getBounds();
            placed = placed && power.getParentComponent() == &tab && power.isVisible() && tab.getLocalBounds().contains (b)
                         && ! b.intersects (tab.getHandleArea()) && b.getWidth() >= 18 && b.getHeight() >= 18
                         && tab.getComponentAt (b.getCentre()) == &power;
        }
        check (placed, "Drive, EQ, Dynamics and Limiter tabs each have a power toggle (inside the tab, clear of the handle, hit first)");
        check (editor.getPageButton (Editor::envelopePage).getNumChildComponents() == 1
                   && editor.getPageButton (Editor::envelopePage).getChildComponent (0) == &editor.getEnvelopePowerButton(),
               "the Envelope tab has just its envelope power toggle (v0.3.3)");

        const auto clickPower = [&] (int stage)
        {
            // The toggle's click handler (a real click lands on the toggle, a child of the tab: see the hit test above).
            auto& power = editor.getPowerButton (stage);
            if (power.onClick) power.onClick();
        };

        // Drive and EQ: their switch parameter, one undo step, the page stays as it was.
        for (const auto& [stage, id] : { std::pair { (int) dsp::driveStage, "dist_on" }, std::pair { (int) dsp::eqStage, "eq_on" } })
        {
            const auto name = std::string (dsp::chainStageName (stage));
            auto& tab = editor.getPageButton (Editor::pageForStage (stage));
            setParam (p, id, 0.0f);
            editor.refreshPowerButtonsNow();
            check (! editor.getPowerButton (stage).getToggleState() && ! tab.isProcessorOn(), name + ": off shows as off (toggle and dimmed tab)");
            undo.clear();
            clickPower (stage);
            check (getPlain (p, id) > 0.5f && editor.getPowerButton (stage).getToggleState() && tab.isProcessorOn(), name + ": the tab toggle switches it on");
            check (editor.getSelectedPage() == dsp::bodyLayer && ! tab.getToggleState(), name + ": toggling does not select the tab");
            check (undo.getNumUndoableSteps() == 1, name + ": one undo step");
            clickPower (stage);
            check (getPlain (p, id) < 0.5f, name + ": and off again");
            undo.undo();
            check (getPlain (p, id) > 0.5f, name + ": undo restores it");
            setParam (p, id, 0.0f); // automation / presets
            editor.refreshPowerButtonsNow();
            check (! editor.getPowerButton (stage).getToggleState(), name + ": the toggle follows the parameter");
        }

        // Dynamics: one toggle for the transient shaper and the compressor.
        setValues (p, { { "shape_on", 0 }, { "comp_on", 0 } });
        undo.clear();
        clickPower (dsp::dynamicsStage);
        check (getPlain (p, "shape_on") > 0.5f && getPlain (p, "comp_on") > 0.5f && undo.getNumUndoableSteps() == 1,
               "Dynamics on (nothing remembered): shaper and compressor on, one undo step");
        setParam (p, "shape_on", 0.0f);
        editor.refreshPowerButtonsNow();
        check (editor.getPowerButton (dsp::dynamicsStage).getToggleState(), "Dynamics shows on while either part is on");
        undo.clear();
        clickPower (dsp::dynamicsStage);
        check (getPlain (p, "shape_on") < 0.5f && getPlain (p, "comp_on") < 0.5f && ! editor.getPowerButton (dsp::dynamicsStage).getToggleState(),
               "Dynamics off: both off");
        clickPower (dsp::dynamicsStage);
        check (getPlain (p, "shape_on") < 0.5f && getPlain (p, "comp_on") > 0.5f, "Dynamics on again restores what was on (compressor only)");
        check (undo.getNumUndoableSteps() == 2, "one undo step per toggle");
        undo.undo();
        check (getPlain (p, "shape_on") < 0.5f && getPlain (p, "comp_on") < 0.5f, "undo: off again");

        // Limiter: Off <-> the remembered mode; the panel's mode box no longer offers Off.
        editor.selectPage (Editor::outputPage);
        std::map<juce::String, juce::Component*> visible;
        collectComponents (editor, visible, true);
        auto* mode = dynamic_cast<juce::ComboBox*> (visible.count ("lim_mode") ? visible["lim_mode"] : nullptr);
        check (mode != nullptr && mode->getNumItems() == 2 && mode->getItemText (0) == "Clipper" && mode->getItemText (1) == "Limiter",
               "the Limiter panel's mode box offers Clipper / Limiter (no Off)");
        setParam (p, "lim_mode", 1.0f);
        editor.refreshPowerButtonsNow();
        undo.clear();
        clickPower (dsp::limiterStage);
        check (juce::roundToInt (getPlain (p, "lim_mode")) == 0 && undo.getNumUndoableSteps() == 1, "Limiter tab toggle: off (one step)");
        check (editor.getSelectedPage() == Editor::outputPage, "toggling the selected tab keeps its page");
        if (mode != nullptr)
            check (! mode->isEnabled() && mode->getSelectedId() == 0 && mode->getText().isEmpty()
                       && mode->getTextWhenNothingSelected() == "Off",
                   "while off, the mode box is disabled and shows Off (nothing selected)");
        clickPower (dsp::limiterStage);
        check (juce::roundToInt (getPlain (p, "lim_mode")) == 1, "Limiter on restores the mode it had (Clipper)");
        if (mode != nullptr)
        {
            check (mode->isEnabled() && mode->getText() == "Clipper", "the mode box shows it again");
            mode->setSelectedId (2, juce::sendNotificationSync);
            check (juce::roundToInt (getPlain (p, "lim_mode")) == 2, "choosing Limiter in the box sets the mode");
        }

        // No enable switches inside the processor panels; the limiter panel is called Limiter.
        bool noSwitches = true;
        for (const auto page : { (int) Editor::drivePage, (int) Editor::eqPage, (int) Editor::dynamicsPage, (int) Editor::outputPage })
        {
            editor.selectPage (page);
            std::map<juce::String, juce::Component*> shown;
            collectComponents (editor, shown, true);
            for (const auto* id : { "dist_on", "eq_on", "shape_on", "comp_on" })
                noSwitches = noSwitches && shown.count (id) == 0;
            int toggles = 0;
            for (const auto& [id, c] : shown)
                if (dynamic_cast<dooblrkick::ui::ControlPanel*> (c->getParentComponent()) != nullptr)
                    toggles += dynamic_cast<juce::ToggleButton*> (c) != nullptr ? 1 : 0;
            noSwitches = noSwitches && toggles == 0;
        }
        check (noSwitches, "the Drive, EQ, Dynamics and Limiter panels have no on / off switches");
        std::map<juce::String, juce::Component*> all;
        collectComponents (editor, all, false);
        check (all.count ("panel_Limiter") == 1 && all.count ("panel_Output") == 0, "the limiter panel is captioned Limiter");

        // A switched-off processor's tab is dimmed but still selectable.
        setParam (p, "eq_on", 0.0f);
        editor.refreshPowerButtonsNow();
        auto& eqTab = editor.getPageButton (Editor::eqPage);
        check (! eqTab.isProcessorOn(), "EQ off: its tab is dimmed");
        eqTab.setToggleState (true, juce::sendNotificationSync); // what a click on the tab body does
        check (editor.getSelectedPage() == Editor::eqPage, "a switched-off processor's tab still selects its page");

        // The toggle moves with its tab when the chain is reordered, and still works there.
        const auto inEditor = [&] (juce::Component& c) { return editor.getLocalArea (&c, c.getLocalBounds()); };
        const auto before = inEditor (editor.getPowerButton (dsp::driveStage));
        dragChainTab (editor, dsp::driveStage, 3);
        const auto after = inEditor (editor.getPowerButton (dsp::driveStage));
        auto& driveTab = editor.getPageButton (Editor::drivePage);
        check (editor.getShownChainOrder()[3] == dsp::driveStage && after.getY() > before.getY() + 100
                   && driveTab.getBounds().contains (after) && after.getX() == before.getX(),
               "after dragging Drive to slot 4 its toggle sits in the Drive tab, now at the bottom");
        setParam (p, "dist_on", 0.0f);
        clickPower (dsp::driveStage);
        check (getPlain (p, "dist_on") > 0.5f && editor.getSelectedPage() == Editor::eqPage, "and it still switches Drive (page unchanged)");
        setParam (p, "chain_order", 0.0f);
        editor.syncChainOrderNow();

        // Snapshots: the left column with one processor (Drive) off, and a processor page.
        setValues (p, { { "dist_on", 0 }, { "eq_on", 1 }, { "shape_on", 1 }, { "comp_on", 1 }, { "lim_mode", 2 } });
        editor.refreshPowerButtonsNow();
        editor.selectPage (Editor::eqPage);
        const auto firstTab = editor.getPageButton (0).getBounds();
        const auto lastTab = editor.getPageButton (Editor::outputPage).getBounds();
        saveSnapshot (editor.createComponentSnapshot (juce::Rectangle<int> (0, firstTab.getY() - 32, 150, lastTab.getBottom() + 12 - (firstTab.getY() - 32))),
                      "-v0.3.1-tabs");
        editor.selectPage (Editor::dynamicsPage);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.1-panel");
        editor.selectPage (Editor::outputPage);
        saveSnapshot (editor.createComponentSnapshot (editor.getLocalBounds()), "-v0.3.1-limiter");
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "instantiation and metadata",             testInstantiationAndMetadata },
        { "note-on is non-silent and finite",       testNoteOnProducesAudio },
        { "synth layers ignore the MIDI note",      testSynthIgnoresMidiNote },
        { "sample tune range -48..+24 st",          testSampleTuneRange },
        { "v0.1.0 state migration",                 testStateMigration },
        { "parameter smoothing (no zipper)",        testParameterSmoothing },
        { "pitch envelope start / end / curve",     testPitchEnvelopeEndpoints },
        { "envelopes decay to silence",             testEnvelopesDecayToSilence },
        { "factory presets and randomise",          testPresetsLoad },
        { "randomise covers Click and Body",        testRandomiseClickBody },
        { "user presets",                           testUserPresets },
        { "state round-trip",                       testStateRoundTrip },
        { "layer mute / solo / level",              testLayerMuteAndSolo },
        { "distortion THD rises with drive",        testDistortionThdRisesWithDrive },
        { "velocity and gate",                      testVelocityAndGate },
        { "factory samples and the sample reader",  testFactorySamples },
        { "sample layers",                          testSampleLayers },
        { "user sample files",                      testUserSample },
        { "no dead parameters",                     testNoDeadParameters },
        { "undo history",                           testUndoHistory },
        { "WAV export",                             testWavExport },
        { "real-time safety and CPU per voice",     testRealtimeSafetyAndCpu },
        { "editor",                                 testEditor },
        { "meter tap (audio -> UI)",                testMeterTap },
        { "meter GR matches the DSP",               testMeterGainReductionMatchesDsp },
        { "EQ / Dynamics / Limiter views",          testOutputViews },
        { "golden renders vs v0.2.0 (within -46 dBFS since 0.3.6)",               testGoldenRenders },
        { "master envelope defaults transparent",   testMasterEnvelopeDefaults },
        { "master envelope shapes the output",      testMasterEnvelopeShapes },
        { "dry / wet",                              testDryWet },
        { "chain reorder vs offline reference",     testChainReorder },
        { "chain order state, presets, undo",       testChainOrderStateAndUndo },
        { "chain tab drag and drop",                testChainDragAndDrop },
        { "Envelope page",                          testEnvelopePage },
        { "master envelope 6000 ms",                testMasterEnvelopeLongRange },
        { "envelope graph zoom",                    testEnvelopeZoom },
        { "master envelope power toggle",           testEnvelopePowerToggle },
        { "Drive view",                             testDriveView },
        { "layout fit 940x600 .. 2400x1400",        testLayoutFit },
        { "processor on / off toggles on the tabs", testProcessorTabToggles },
        { "layer controls follow Synth / Sample",   testLayerSourceLayout },
        { "master envelope knobs (v0.3.3)",         testEnvelopeKnobs },
        { "no info text on the envelopes (v0.3.4)", testEnvelopeInfoTextRemoved },
        { "envelope pan / axis lock / animated fit (v0.3.4)", testEnvelopePanAndFit },
        { "envelope playhead (v0.3.4)", testPlayhead },
        { "playhead dims with the level (v0.3.5)", testPlayheadLevel },
        { "key track on every layer (v0.3.5)", testKeyTrackSwitches },
        { "each layer page shows its own pitch / amp envelope (v0.3.6)", testLayerEnvelopes },
        { "pitch / amp sliding switch (v0.3.5)", testEnvelopeToggle },
        { "layer pitch / amp envelopes (v0.3.6)", testLayerPitchAndAmpEnvelopes },
        { "randomise layer envelopes (v0.3.6)", testRandomiseLayerEnvelopes },
        { "envelope / waveform divider (v0.3.5)", testSplitter },
        { "Export dialog and Play icon (v0.3.5)", testExportAndPlayButtons },
        { "console taper for the layer faders (v0.3.5)", testLevelTaper },
        { "header output meter + version (v0.3.4)", testHeaderMeterAndVersion },
        { "output graph (anti-aliased)",            testOutputGraph },
    };

    int failedTests = 0;

    for (const auto& test : tests)
    {
        std::cout << "[RUN ] " << test.name << "\n";
        const auto failedBefore = failedChecks, passedBefore = passedChecks;
        test.run();
        const auto failed = failedChecks - failedBefore;
        failedTests += failed > 0 ? 1 : 0;
        std::cout << (failed == 0 ? "[PASS] " : "[FAIL] ") << test.name << " (" << (passedChecks - passedBefore)
                  << " checks passed, " << failed << " failed)\n";
    }

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << "/" << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " checks failed\n";

    return failedChecks == 0 ? 0 : 1;
}
