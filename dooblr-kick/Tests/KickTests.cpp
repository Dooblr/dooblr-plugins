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

    bool isEnvelopeParam (const juce::String& id) { return id.startsWith ("penv") || id.startsWith ("aenv") || id.startsWith ("menv"); }

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
        check (juce::String (JucePlugin_VersionString) == "0.3.1", "version 0.3.1");
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
            setParam (*p, "noise_decay", 400.0f);
            compareNotes (*p, std::string (params::layerNames[(size_t) layer]) + " synth");
        }

        for (int i = 0; i < (int) dooblrkick::presets::getFactoryPresets().size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);
            compareNotes (*p, p->getPresetName().toStdString());
        }

        {
            // Key tracking only works for samples: a synth layer with the (sample) key track
            // switch on still ignores the note.
            auto p = makeProcessor();
            setParam (*p, "tail_on", 1.0f);
            setParam (*p, "noise_on", 1.0f);
            for (const auto* id : { "click_keytrack", "body_keytrack", "tail_keytrack" })
                setParam (*p, id, 1.0f);
            compareNotes (*p, "all synth layers, key track switches on");
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
                setParam (*p, "sub_hold", 800.0f);
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
                setParam (*p, "tail_attack", 0.0f);
                setParam (*p, "tail_hold", 2500.0f);
                setParam (*p, "tail_decay", 5.0f);
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
        check ((int) tree.getProperty (versionKey) == 4, "state version is 4");
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
            setParam (*q, "sub_hold", 1000.0f);
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
                setParam (*p, "noise_decay", 1000.0f);
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
            if (id == "noise_tune") return { { "noise_source", 1 } };
            if (id == "release") return { { "gate", 1 } };
            if (id == "lim_release") return { { "comp_on", 0 }, { "lim_ceiling", -12 } };
            if (id == "noise_hold" || id == "noise_attack") return { { "noise_level", 0 } };
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
        const auto pitchCount = (int) getPlain (*probe, params::envelopeCountId (params::Envelope::pitch));
        const auto ampCount = (int) getPlain (*probe, params::envelopeCountId (params::Envelope::amp));
        const auto masterCount = (int) getPlain (*probe, params::envelopeCountId (params::Envelope::master));
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

                const auto count = id.startsWith ("penv") ? pitchCount : id.startsWith ("aenv") ? ampCount : masterCount;
                const auto node = id.substring (4).getIntValue();

                if (node >= (id.endsWith ("_curve") ? count - 1 : count))
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

        // Every parameter is reachable: union of the controls visible on each page.
        std::set<juce::String> reachable;
        for (int page = 0; page < Editor::numPages; ++page)
        {
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
        for (int page = 0; page < Editor::numPages; ++page)
        {
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
            check (diff <= 1.0e-6f, "render " + std::to_string (r) + " matches v0.2.0 (max diff " + std::to_string (diff) + ")");
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
        check (resavedXml != nullptr && resavedXml->getIntAttribute ("stateVersion") == 4, "states are written as version 4");
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
        for (const auto* id : { "root_note", "vel_sens", "gate", "release", "menv_on", "mix_drywet", "chain_order" })
            playControls = playControls && visible.count (id) == 1;
        check (playControls, "Key, Velocity, Gate, Release and Envelope on / Dry/Wet / Chain order controls are on the Envelope page");

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
        check (editor.getPageButton (Editor::envelopePage).getNumChildComponents() == 0, "the Envelope tab has no processor toggle");

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
        { "golden renders vs v0.2.0",               testGoldenRenders },
        { "master envelope defaults transparent",   testMasterEnvelopeDefaults },
        { "master envelope shapes the output",      testMasterEnvelopeShapes },
        { "dry / wet",                              testDryWet },
        { "chain reorder vs offline reference",     testChainReorder },
        { "chain order state, presets, undo",       testChainOrderStateAndUndo },
        { "chain tab drag and drop",                testChainDragAndDrop },
        { "Envelope page",                          testEnvelopePage },
        { "Drive view",                             testDriveView },
        { "layout fit 940x600 .. 2400x1400",        testLayoutFit },
        { "processor on / off toggles on the tabs", testProcessorTabToggles },
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
