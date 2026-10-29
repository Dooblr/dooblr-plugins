/*
    dooblr-prophet console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRPROPHET_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "Randomiser.h"
#include "DSP/AnalogModel.h"
#include "DSP/Primitives.h"
#include "DSP/SynthEngine.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <string>
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
    using namespace dooblrprophet;
    namespace id = params::id;
    using Processor = DooblrProphetProcessor;
    using Editor = DooblrProphetEditor;

    int failedChecks = 0, passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition) { ++passedChecks; return; }
        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n"; }

    std::string fmt (double value, int decimals = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (decimals) << value;
        return s.str();
    }

    constexpr double defaultRate = 48000.0;
    constexpr int blockSize = 512;

    std::unique_ptr<Processor> makeProcessor (double rate = defaultRate, int block = blockSize)
    {
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails (0, 2, rate, block);
        p->prepareToPlay (rate, block);
        return p;
    }

    juce::RangedAudioParameter* param (Processor& p, const juce::String& pid)
    {
        auto* r = p.getState().getParameter (pid);
        check (r != nullptr, "parameter exists: " + pid.toStdString());
        return r;
    }

    void set (Processor& p, const juce::String& pid, float plain)
    {
        if (auto* r = param (p, pid))
            r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float get (Processor& p, const juce::String& pid)
    {
        auto* r = param (p, pid);
        return r == nullptr ? 0.0f : r->convertFrom0to1 (r->getValue());
    }

    /** A plain, stable patch for pitch measurements: one saw (osc 1), filter open, no slop. */
    void plainSaw (Processor& p)
    {
        set (p, id::vintage, 0.0f);
        set (p, id::mixOsc2, 0.0f);
        set (p, id::lpfCutoff, 20000.0f);
        set (p, id::lpfReso, 0.0f);
        set (p, id::lpfEnvAmt, 0.0f);
        set (p, id::lpfKeyTrack, 0.0f);
        set (p, id::lfoWheel, 0.0f);
        set (p, id::aenvSustain, 1.0f);
    }

    struct Event { int sample; juce::MidiMessage message; };

    struct Render
    {
        std::vector<float> left, right;
        float peak() const
        {
            float m = 0.0f;
            for (size_t i = 0; i < left.size(); ++i)
                m = std::max ({ m, std::abs (left[i]), std::abs (right[i]) });
            return m;
        }
        bool finite() const
        {
            for (size_t i = 0; i < left.size(); ++i)
                if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                    return false;
            return true;
        }
        int denormals() const
        {
            int n = 0;
            for (size_t i = 0; i < left.size(); ++i)
                for (auto v : { left[i], right[i] })
                    if (v != 0.0f && std::abs (v) < FLT_MIN)
                        ++n;
            return n;
        }
        double rms (size_t from = 0, size_t to = SIZE_MAX) const
        {
            to = std::min (to, left.size());
            double sum = 0.0;
            for (size_t i = from; i < to; ++i)
                sum += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
            return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
        }
    };

    Render render (Processor& p, int numSamples, const std::vector<Event>& events = {}, int block = blockSize)
    {
        Render out;
        out.left.reserve ((size_t) numSamples);
        out.right.reserve ((size_t) numSamples);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;

        for (int start = 0; start < numSamples; start += block)
        {
            const auto n = std::min (block, numSamples - start);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            midi.clear();
            for (const auto& e : events)
                if (e.sample >= start && e.sample < start + n)
                    midi.addEvent (e.message, e.sample - start);
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
            {
                out.left.push_back (buffer.getSample (0, i));
                out.right.push_back (buffer.getSample (1, i));
            }
        }
        return out;
    }

    juce::MidiMessage on (int noteNumber, float velocity = 0.8f) { return juce::MidiMessage::noteOn (1, noteNumber, velocity); }
    juce::MidiMessage off (int noteNumber) { return juce::MidiMessage::noteOff (1, noteNumber); }

    /** Frequency from upward zero crossings (linearly interpolated) of the left channel. */
    double measureHz (const std::vector<float>& x, double rate, size_t from)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (size_t i = std::max<size_t> (from, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const auto t = (double) (i - 1) + (double) x[i - 1] / ((double) x[i - 1] - (double) x[i]);
                if (first < 0.0) first = t;
                last = t;
                ++crossings;
            }
        return crossings > 1 ? (double) (crossings - 1) * rate / (last - first) : 0.0;
    }

    double cents (double hz, double reference) { return 1200.0 * std::log2 (hz / reference); }
    double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

    /** The model's ramp-reset flattening: the period grows by t_r (analog-reference.md, VCO). */
    double flattenedHz (double hz) { return 1.0 / (1.0 / hz + dsp::model::rampResetSeconds); }

    //==============================================================================
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-prophet", "name is dooblr-prophet (got " + p->getName().toStdString() + ")");
        check (p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "a MIDI instrument");
        check (p->getTotalNumInputChannels() == 0 && p->getTotalNumOutputChannels() == 2, "no input, stereo output");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRPROPHET_PROJECT_VERSION, "version string matches the project version");
        note ("version " JucePlugin_VersionString);
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, (int) defaultRate);
        check (r.peak() == 0.0f, "no notes: exact silence (peak " + std::to_string (r.peak()) + ")");
    }

    void testNoteBoundedAndReleases()
    {
        auto p = makeProcessor();
        const auto n = (int) defaultRate;
        const auto r = render (*p, 4 * n, { { 0, on (48) }, { 0, on (55) }, { 0, on (60) }, { 0, on (64) },
                                           { n, off (48) }, { n, off (55) }, { n, off (60) }, { n, off (64) } });
        check (r.finite(), "finite output");
        check (r.peak() > 0.05f, "a chord is audible (peak " + fmt (r.peak(), 3) + ")");
        check (r.peak() < 4.0f, "bounded (peak " + fmt (r.peak(), 3) + ")");
        check (r.rms ((size_t) (3.8 * n)) < 1.0e-6, "silent after release (tail rms " + std::to_string (r.rms ((size_t) (3.8 * n))) + ")");
        check (p->getEngine().getNumActiveVoices() == 0, "all voices idle after release");
        note ("4-note chord peak " + fmt (r.peak(), 3));
    }

    void testVoiceStealing()
    {
        for (int voices = 1; voices <= dsp::maxVoices; ++voices)
        {
            auto p = makeProcessor();
            set (*p, id::voices, (float) voices);
            std::vector<Event> events;
            const auto notes = voices + 3;
            for (int i = 0; i < notes; ++i)
                events.push_back ({ i * 480, on (40 + 2 * i) });
            const auto r = render (*p, 24000, events);
            const auto& engine = p->getEngine();
            int gated = 0;
            bool newestSounds = false;
            for (int v = 0; v < dsp::maxVoices; ++v)
            {
                const auto& voice = engine.getVoice (v);
                if (voice.isGateOn()) ++gated;
                if (voice.isGateOn() && voice.getNote() == 40 + 2 * (notes - 1)) newestSounds = true;
            }
            const auto label = std::to_string (voices) + " voices: ";
            check (engine.getNumActiveVoices() <= voices, label + "no more than the voice count sound (" + std::to_string (engine.getNumActiveVoices()) + ")");
            check (gated == voices, label + "every voice is held (" + std::to_string (gated) + ")");
            check (newestSounds, label + "the newest note took a voice");
            check (r.finite() && r.peak() < 4.0f, label + "finite and bounded through stealing (peak " + fmt (r.peak(), 3) + ")");
        }
    }

    void testVoiceModes()
    {
        {
            auto p = makeProcessor();
            set (*p, id::voiceMode, 1.0f);
            set (*p, id::unisonVoices, 7.0f);
            render (*p, 4800, { { 0, on (60) } });
            check (p->getEngine().getNumActiveVoices() == 7, "unison: one note sounds 7 voices (got "
                                                              + std::to_string (p->getEngine().getNumActiveVoices()) + ")");
        }
        {
            auto p = makeProcessor();
            set (*p, id::voiceMode, 2.0f);
            render (*p, 9600, { { 0, on (60) }, { 1000, on (64) }, { 2000, on (67) }, { 6000, off (67) } });
            const auto& engine = p->getEngine();
            int sounding = 0, note = -1;
            for (int v = 0; v < dsp::maxVoices; ++v)
                if (engine.getVoice (v).isGateOn()) { ++sounding; note = engine.getVoice (v).getNote(); }
            check (sounding == 1, "mono: one voice (got " + std::to_string (sounding) + ")");
            check (note == 64, "mono: releasing the top note returns to the held 64 (got " + std::to_string (note) + ")");
        }
    }

    void testMaxResonance()
    {
        for (int model = 0; model < 2; ++model)
            for (auto cutoff : { 20.0f, 200.0f, 2000.0f, 20000.0f })
            {
                auto p = makeProcessor();
                set (*p, id::lpfModel, (float) model);
                set (*p, id::lpfReso, 1.0f);
                set (*p, id::lpfDrive, 1.0f);
                set (*p, id::hpfReso, 1.0f);
                set (*p, id::hpfCutoff, 120.0f);
                set (*p, id::lpfCutoff, cutoff);
                const auto n = (int) defaultRate;
                const auto r = render (*p, 3 * n, { { 0, on (36, 1.0f) }, { 0, on (60, 1.0f) }, { 0, on (84, 1.0f) },
                                                   { n, off (36) }, { n, off (60) }, { n, off (84) } });
                const auto label = "model " + std::to_string (model) + " cutoff " + fmt (cutoff, 0) + " Hz: ";
                check (r.finite(), label + "no NaN / Inf");
                check (r.peak() < 6.0f, label + "bounded (peak " + fmt (r.peak(), 3) + ")");
                check (r.denormals() == 0, label + "no denormal samples (" + std::to_string (r.denormals()) + ")");
            }
    }

    void testSampleRates()
    {
        for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            auto p = makeProcessor (rate);
            plainSaw (*p);
            const auto n = (int) rate;
            const auto r = render (*p, n, { { 0, on (57) } });
            const auto hz = measureHz (r.left, rate, (size_t) (0.2 * rate));
            const auto expected = flattenedHz (noteHz (57));
            const auto label = fmt (rate / 1000.0, 1) + " kHz: ";
            check (r.finite() && r.peak() < 4.0f && r.peak() > 0.05f, label + "finite, audible and bounded (peak " + fmt (r.peak(), 3) + ")");
            check (std::abs (cents (hz, expected)) < 1.5, label + "A3 within 1.5 cents (" + fmt (cents (hz, expected), 2) + " cents)");
            note (label + "A3 " + fmt (hz, 3) + " Hz (" + fmt (cents (hz, noteHz (57)), 2) + " cents vs 220 Hz)");
        }
    }

    void testStateRoundTrip()
    {
        auto a = makeProcessor();
        juce::Random random (42);
        for (auto* r : a->getParameters())
        {
            // Legal values only: a bool or choice holds a snapped value, not an arbitrary float.
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r);
            const auto value = random.nextFloat();
            r->setValueNotifyingHost (ranged->isDiscrete() || ranged->isBoolean() ? ranged->convertTo0to1 (ranged->convertFrom0to1 (value)) : value);
        }
        juce::MemoryBlock block;
        a->getStateInformation (block);

        auto b = makeProcessor();
        b->setStateInformation (block.getData(), (int) block.getSize());
        int mismatched = 0;
        auto paramsA = a->getParameters(), paramsB = b->getParameters();
        for (int i = 0; i < paramsA.size(); ++i)
            if (std::abs (paramsA[i]->getValue() - paramsB[i]->getValue()) > 1.0e-5f)
            {
                ++mismatched;
                note ("differs: " + paramsA[i]->getName (64).toStdString() + " " + std::to_string (paramsA[i]->getValue())
                      + " -> " + std::to_string (paramsB[i]->getValue()));
            }
        check (paramsA.size() == paramsB.size() && mismatched == 0, "every parameter restored (" + std::to_string (mismatched) + " differ)");

        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        check (xml != nullptr && xml->getIntAttribute ("stateVersion") == params::stateVersion, "state carries stateVersion");
        note (std::to_string (paramsA.size()) + " parameters");
    }

    void testPresets()
    {
        const auto& factory = presets::factory();
        check (factory.size() >= 8 && factory.front().name == "Init", "Init first, then the factory patches");
        std::set<juce::String> names;
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            names.insert (factory[(size_t) i].name);
            auto p = makeProcessor();
            p->setCurrentProgram (i);
            check (p->getPresetName() == factory[(size_t) i].name, "program " + std::to_string (i) + " sets the preset name");
            const auto r = render (*p, 48000, { { 0, on (48) }, { 0, on (55) }, { 0, on (60) }, { 24000, off (48) }, { 24000, off (55) }, { 24000, off (60) } });
            check (r.finite() && r.peak() > 0.02f && r.peak() < 4.0f,
                   factory[(size_t) i].name.toStdString() + ": audible, finite, bounded (peak " + fmt (r.peak(), 3) + ")");
        }
        check (names.size() == factory.size(), "factory names are unique");

        auto p = makeProcessor();
        set (*p, id::lpfCutoff, 1234.0f);
        set (*p, id::voiceMode, 1.0f);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("prophet-test" + juce::String (presets::fileExtension));
        const auto saved = p->saveUserPreset (file);
        check (file.existsAsFile(), "user preset saved (" + saved.toStdString() + ")");
        auto q = makeProcessor();
        const auto loaded = q->loadUserPreset (file);
        check (std::abs (get (*q, id::lpfCutoff) - 1234.0f) < 0.5f && juce::roundToInt (get (*q, id::voiceMode)) == 1,
               "user preset restores its values (" + loaded.toStdString() + ")");
        check (q->getPresetName() == "prophet-test", "user preset name is the file name");
        file.deleteFile();
    }

    /** Every parameter changes the sound in a context where it applies. */
    void testNoDeadParameters()
    {
        // Settings a parameter needs before it can be heard (anything else uses the defaults).
        const std::map<juce::String, std::vector<std::pair<const char*, float>>> contexts {
            { id::osc1PW, { { id::osc1Shape, 0.9f } } },
            { id::osc2PW, { { id::osc2Shape, 0.9f } } },
            { id::osc1Mode, { { id::osc1Shape, 0.0f } } },
            { id::osc2Mode, { { id::osc2Shape, 0.0f } } },
            { id::osc1Saw, { { id::osc1Mode, 1.0f } } },
            { id::osc1Tri, { { id::osc1Mode, 1.0f } } },
            { id::osc1Pulse, { { id::osc1Mode, 1.0f } } },
            { id::osc2Saw, { { id::osc2Mode, 1.0f } } },
            { id::osc2Tri, { { id::osc2Mode, 1.0f } } },
            { id::osc2Pulse, { { id::osc2Mode, 1.0f } } },
            { id::osc1Sync, { { id::osc1Coarse, 7.0f } } },
            { id::hpfReso, { { id::hpfCutoff, 500.0f } } },
            { id::hpfEnvAmt, { { id::hpfCutoff, 300.0f } } },
            { id::lfoDestHpf, { { id::hpfCutoff, 300.0f } } },
            { id::pmodDestFreq1, { { id::pmodOsc2, 0.5f } } },
            { id::pmodDestShape1, { { id::pmodOsc2, 0.5f } } },
            { id::pmodDestFilter, { { id::pmodOsc2, 0.5f } } },
            { id::lfoDivision, { { id::lfoSync, 1.0f } } },
            { id::unisonVoices, { { id::voiceMode, 1.0f } } },
            { id::unisonDetune, { { id::voiceMode, 1.0f } } },
            { id::glide, { { id::voiceMode, 2.0f } } },
        };

        // Mod wheel up, a chord with mixed velocities, then bend, pressure and a legato note.
        const std::vector<Event> events {
            { 0, juce::MidiMessage::controllerEvent (1, 1, 100) },
            { 0, on (48, 0.9f) }, { 0, on (55, 0.5f) }, { 0, on (60, 0.7f) },
            { 4800, juce::MidiMessage::pitchWheel (1, 12288) }, { 4800, juce::MidiMessage::channelPressureChange (1, 90) },
            { 7200, on (64, 0.8f) },
            { 21600, off (48) }, { 21600, off (55) }, { 21600, off (60) }, { 21600, off (64) } };

        const auto renderWith = [&] (const juce::String& pid, float normalised)
        {
            auto p = makeProcessor();
            if (auto it = contexts.find (pid); it != contexts.end())
                for (const auto& [other, value] : it->second)
                    set (*p, other, value);
            if (auto* r = param (*p, pid))
                r->setValueNotifyingHost (normalised);
            return render (*p, 43200, events);
        };

        {
            const auto a = renderWith (id::output, 0.5f), b = renderWith (id::output, 0.5f);
            check (a.left == b.left && a.right == b.right, "renders are deterministic (same settings, same output)");
        }

        auto reference = makeProcessor();
        int dead = 0;
        for (auto* r : reference->getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r);
            const auto pid = ranged->getParameterID();
            const bool discrete = ranged->isDiscrete() || ranged->isBoolean();
            const auto lo = discrete ? 0.0f : 0.2f, hi = discrete ? 1.0f : 0.8f;
            const auto a = renderWith (pid, lo), b = renderWith (pid, hi);
            double diff = 0.0;
            for (size_t i = 0; i < a.left.size(); ++i)
                diff += std::pow ((double) a.left[i] - b.left[i], 2.0) + std::pow ((double) a.right[i] - b.right[i], 2.0);
            diff = std::sqrt (diff / (2.0 * (double) a.left.size()));
            const auto level = std::max (a.rms(), b.rms());
            const bool live = level > 0.0 && diff > 1.0e-3 * level;
            if (! live) ++dead;
            check (live, "parameter " + pid.toStdString() + " changes the sound (rel diff " + std::to_string (level > 0 ? diff / level : 0.0) + ")");
        }
        note (std::to_string (reference->getParameters().size()) + " parameters checked, " + std::to_string (dead) + " dead");
    }

    void testUndoHistory()
    {
        auto p = makeProcessor();
        auto& history = p->getUndoHistory();
        auto* cutoff = param (*p, id::lpfCutoff);
        const auto before = cutoff->getValue();
        cutoff->beginChangeGesture();
        cutoff->setValueNotifyingHost (0.3f);
        cutoff->setValueNotifyingHost (0.35f);
        cutoff->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (cutoff->getValue() - before) < 1.0e-6f, "undo restores the value before the gesture");
        check (history.canRedo(), "redo available");
        history.redo();
        check (std::abs (cutoff->getValue() - 0.35f) < 1.0e-6f, "redo re-applies the whole gesture");

        history.clear();
        cutoff->setValueNotifyingHost (0.9f); // host automation: no gesture
        check (! history.canUndo(), "automation (no gesture) is not an undo step");

        history.clear();
        auto* reso = param (*p, id::lpfReso);
        for (int i = 0; i < 60; ++i)
        {
            auto* target = (i % 2 == 0) ? reso : cutoff; // alternate so steps do not merge
            target->beginChangeGesture();
            target->setValueNotifyingHost ((float) (i % 10) / 10.0f + 0.05f);
            target->endChangeGesture();
        }
        int steps = 0;
        while (history.canUndo() && steps < 100) { history.undo(); ++steps; }
        check (steps == 50, "undo keeps 50 steps (got " + std::to_string (steps) + ")");
    }

    //==============================================================================
    // Analog behaviour (docs/analog-reference.md)

    void testSelfOscillationAtK4()
    {
        constexpr double rate = 96000.0;
        constexpr float fc = 1000.0f;
        dsp::LadderFilter filter;
        filter.setModel (dsp::model::cem3320);
        filter.setCoefficients (juce::MathConstants<float>::pi * fc / (float) rate, 4.0f);
        std::vector<float> y ((size_t) rate);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = filter.process (i == 0 ? 1.0e-3f : 0.0f);
        const auto hz = measureHz (y, rate, (size_t) (0.5 * rate));
        float early = 0.0f, late = 0.0f;
        for (size_t i = (size_t) (0.1 * rate); i < (size_t) (0.2 * rate); ++i) early = std::max (early, std::abs (y[i]));
        for (size_t i = (size_t) (0.9 * rate); i < y.size(); ++i) late = std::max (late, std::abs (y[i]));
        check (std::abs (hz / fc - 1.0) < 0.03, "k = 4 rings at fc (" + fmt (hz, 1) + " Hz for fc " + fmt (fc, 0) + " Hz)");
        check (late > 0.5f * early && late > 1.0e-6f, "k = 4 is the edge of oscillation: the ring is sustained (0.1 s "
                                                         + std::to_string (early) + ", 0.9 s " + std::to_string (late) + ")");
        note ("k = 4: " + fmt (hz, 2) + " Hz, late / early amplitude " + fmt (late / std::max (early, 1.0e-30f), 3));

        for (int m = 0; m < 2; ++m)
        {
            const auto& model = dsp::model::ladderModel (m);
            dsp::LadderFilter f;
            f.setModel (model);
            f.setCoefficients (juce::MathConstants<float>::pi * fc / (float) rate, dsp::model::ladderK (model, 1.0f));
            dsp::Random noise;
            noise.seed (7u);
            std::vector<float> out ((size_t) (2 * rate));
            for (auto& v : out)
                v = f.process (dsp::model::thermalNoise * noise.bipolar());
            const auto start = (size_t) rate;
            const auto osc = measureHz (out, rate, start);
            float peak = 0.0f;
            for (size_t i = start; i < out.size(); ++i) peak = std::max (peak, std::abs (out[i]));

            // THD of the settled oscillation by DFT at the measured fundamental.
            const auto len = (size_t) rate;
            const auto harmonic = [&] (int h)
            {
                std::complex<double> sum;
                for (size_t i = 0; i < len; ++i)
                {
                    const auto w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) len);
                    sum += w * (double) out[start + i] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * h * osc * (double) i / rate);
                }
                return std::abs (sum);
            };
            const auto fundamental = harmonic (1);
            double others = 0.0;
            for (int h = 2; h <= 7; ++h) others += std::pow (harmonic (h), 2.0);
            const auto thd = std::sqrt (others) / std::max (fundamental, 1.0e-30);
            const auto label = std::string (m == 0 ? "3320" : "2040") + " at full resonance (k " + fmt (model.kMax, 2) + "): ";
            check (peak > 0.05f && peak < 6.0f, label + "self-oscillates from the noise floor, bounded (peak " + fmt (peak, 3) + ")");
            check (std::abs (osc / fc - 1.0) < 0.08, label + "near fc (" + fmt (osc, 1) + " Hz)");
            check (thd < 0.10, label + "a near-sine (THD " + fmt (100.0 * thd, 2) + "%)");
            note (label + fmt (osc, 1) + " Hz, peak " + fmt (peak, 3) + ", THD " + fmt (100.0 * thd, 2) + "%");
        }
    }

    void testDcGain()
    {
        for (int m = 0; m < 2; ++m)
            for (auto k : { 0.0f, 1.0f, 2.0f, 3.0f })
            {
                dsp::LadderFilter f;
                f.setModel (dsp::model::ladderModel (m));
                f.setCoefficients (juce::MathConstants<float>::pi * 500.0f / 96000.0f, k);
                float y = 0.0f;
                for (int i = 0; i < 96000; ++i)
                    y = f.process (0.01f);
                const auto gain = y / 0.01f, expected = 1.0f / (1.0f + k);
                check (std::abs (gain / expected - 1.0f) < 0.02f, std::string (m == 0 ? "3320" : "2040") + " k " + fmt (k, 1)
                                                                       + ": DC gain " + fmt (gain, 4) + " vs 1/(1+k) " + fmt (expected, 4));
                if (std::abs (k - 2.0f) < 0.01f)
                    note (std::string (m == 0 ? "3320" : "2040") + " k = 2: DC gain " + fmt (gain, 4) + " (1/(1+k) = " + fmt (expected, 4) + ")");
            }
    }

    void testHfFlattening()
    {
        constexpr double rate = 96000.0;
        auto p = makeProcessor (rate);
        plainSaw (*p);
        const auto r = render (*p, (int) rate, { { 0, on (111) } });
        const auto hz = measureHz (r.left, rate, (size_t) (0.2 * rate));
        const auto flat = cents (hz, noteHz (111));
        const auto modelled = cents (flattenedHz (noteHz (111)), noteHz (111));
        check (std::abs (flat - modelled) < 3.0, "note 111 (" + fmt (noteHz (111), 1) + " Hz) is flat by about "
                                                  + fmt (modelled, 1) + " cents (got " + fmt (flat, 2) + ")");
        note ("note 111: " + fmt (hz, 2) + " Hz, " + fmt (flat, 2) + " cents (t_r model " + fmt (modelled, 2) + " cents)");
    }

    void testPolyModRaisesPitch()
    {
        const auto cycles = [] (float amount)
        {
            auto p = makeProcessor();
            plainSaw (*p);
            set (*p, id::osc2Shape, 0.0f);   // triangle modulator
            set (*p, id::osc2Coarse, -5.0f);
            set (*p, id::pmodOsc2, amount);
            set (*p, id::pmodDestFreq1, 1.0f);
            render (*p, 48000, { { 0, on (60) } });
            uint64_t total = 0;
            for (int v = 0; v < dsp::maxVoices; ++v)
                total += p->getEngine().getVoice (v).getOsc1Cycles();
            return total;
        };
        const auto plain = cycles (0.0f), modulated = cycles (0.6f);
        check (modulated > plain, "exponential Poly-Mod FM raises the mean pitch (" + std::to_string (plain) + " -> "
                                      + std::to_string (modulated) + " osc 1 cycles in 1 s)");
        note ("osc 1 cycles in 1 s: " + std::to_string (plain) + " without, " + std::to_string (modulated) + " with Poly-Mod");
    }

    //==============================================================================
    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        set (*p, id::voices, 10.0f);
        set (*p, id::vintage, 1.0f);
        set (*p, id::pmodOsc2, 0.5f);     // the 4x Poly-Mod FM path
        set (*p, id::lpfReso, 0.9f);
        set (*p, id::lpfDrive, 0.8f);
        set (*p, id::mixSub, 0.5f);
        set (*p, id::mixNoise, 0.2f);
        set (*p, id::lfoInitial, 0.3f);
        std::vector<Event> events;
        for (int i = 0; i < 10; ++i)
            events.push_back ({ 0, on (36 + 4 * i) });
        render (*p, blockSize, events); // notes on (allocation-free too, but measure steady state)

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.ensureSize (256);
        const int blocks = (int) (10.0 * defaultRate / blockSize);
        allocationCount = 0;
        countAllocations = true;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
        }
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        countAllocations = false;
        const auto audioSeconds = blocks * blockSize / defaultRate;
        const auto load = elapsed / audioSeconds;
        check (allocationCount.load() == 0, "no allocation in processBlock (" + std::to_string (allocationCount.load()) + ")");
        check (p->getEngine().getNumActiveVoices() == 10, "10 voices sounding");
        check (load < 0.25, "10 voices (4x Poly-Mod path) under 25% of one core (" + fmt (100.0 * load, 2) + "%)");
        note ("10 voices, 4x path: " + fmt (elapsed * 1000.0, 1) + " ms for " + fmt (audioSeconds, 1) + " s of audio = "
              + fmt (100.0 * load, 2) + "% of one core at 48 kHz");
    }

    //==============================================================================
    void saveSnapshot (juce::Component& c, const juce::String& suffix)
    {
        const auto* prefix = std::getenv ("DOOBLRPROPHET_EDITOR_SNAPSHOT");
        if (prefix == nullptr || *prefix == 0)
            return;
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        const juce::File file (juce::String (prefix) + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            note ("snapshot: " + file.getFullPathName().toStdString());
        else
            check (false, "could not write snapshot " + file.getFullPathName().toStdString());
    }

    void testEditor()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrProphetEditor");
        if (editor == nullptr) return;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080 x 660");
        auto* constrainer = editor->getConstrainer();
        check (constrainer != nullptr && constrainer->getMinimumWidth() == 940 && constrainer->getMinimumHeight() == 600
                   && constrainer->getMaximumWidth() == 2400 && constrainer->getMaximumHeight() == 1400, "resize limits 940 x 600 .. 2400 x 1400");

        auto* versionLabel = dynamic_cast<juce::Label*> (editor->findChildWithID ("version"));
        check (versionLabel != nullptr && versionLabel->getText() == "v" JucePlugin_VersionString, "version label shows v" JucePlugin_VersionString);

        // One front panel in the classic order.
        const std::vector<std::vector<std::string>> expectedRows {
            { "Poly-Mod", "LFO / Wheel-Mod", "Oscillator A", "Oscillator B" },
            { "Mixer", "Filter", "Filter Envelope", "Amp Envelope", "Voices" } };
        const auto& sections = editor->getSections();
        for (size_t r = 0; r < 2; ++r)
        {
            std::vector<std::string> titles;
            for (auto index : editor->getRows()[r])
                titles.push_back (sections[(size_t) index]->getTitle().toStdString());
            check (titles == expectedRows[r], "row " + std::to_string (r + 1) + " sections in panel order");
        }

        // Every parameter has exactly one control, every control is bound to a parameter, all visible at once.
        std::set<juce::String> parameterIds;
        for (auto* r : p->getParameters())
            parameterIds.insert (dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID());

        int missing = 0;
        for (const auto& pid : parameterIds)
        {
            int count = pid == id::output ? 1 : 0; // the header Output slider
            for (auto& section : sections)
                if (section->findControl (pid) != nullptr) ++count;
            if (count != 1) { ++missing; check (false, "parameter " + pid.toStdString() + " has one control (has " + std::to_string (count) + ")"); }
            if (auto* c = editor->findControl (pid))
                check (c->isShowing() || ! editor->isShowing(), "control visible: " + pid.toStdString());
        }
        check (missing == 0, "every parameter has exactly one control on the one panel");
        check (editor->findChildWithID (id::output) != nullptr, "header Output slider exists");

        int unbound = 0;
        for (auto& section : sections)
            for (auto* child : section->getChildren())
                if (dynamic_cast<juce::Slider*> (child) != nullptr || dynamic_cast<juce::ToggleButton*> (child) != nullptr
                    || dynamic_cast<juce::ComboBox*> (child) != nullptr)
                    if (parameterIds.count (child->getComponentID()) == 0) ++unbound;
        check (unbound == 0, "no control without a parameter (" + std::to_string (unbound) + " unbound)");

        // Layout at the three sizes.
        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";

            std::vector<juce::Component*> header;
            for (auto* cid : { "undo", "redo", "preset", "random", "savePreset", "loadPreset", id::output, "version" })
                if (auto* c = editor->findChildWithID (cid)) header.push_back (c);
            check (header.size() == 8, label + "all header items present");
            for (size_t i = 0; i < header.size(); ++i)
            {
                check (editor->getLocalBounds().contains (header[i]->getBounds()) && header[i]->getBottom() <= 60,
                       label + "header item inside the header: " + header[i]->getComponentID().toStdString());
                for (size_t j = i + 1; j < header.size(); ++j)
                    check (! header[i]->getBounds().intersects (header[j]->getBounds()),
                           label + "header items do not overlap: " + header[i]->getComponentID().toStdString() + " / " + header[j]->getComponentID().toStdString());
            }
            check (editor->findChildWithID ("preset")->getWidth() >= 140, label + "preset box at least 140 px");

            const auto panel = editor->getPanelArea();
            check (panel.getY() >= 60, label + "panel below the header");
            int smallestKnob = 10000, smallestToggle = 10000;

            for (size_t i = 0; i < sections.size(); ++i)
            {
                auto& s = *sections[i];
                const auto name = label + s.getTitle().toStdString() + ": ";
                check (panel.contains (s.getBounds()), name + "inside the panel");
                check (s.getHeight() >= s.getPreferredHeight (ui::Section::minKnobRowHeight), name + "tall enough for its controls");
                for (size_t j = i + 1; j < sections.size(); ++j)
                    check (! s.getBounds().intersects (sections[j]->getBounds()), name + "does not overlap " + sections[j]->getTitle().toStdString());

                std::vector<juce::Component*> visible;
                for (auto* child : s.getChildren())
                    if (child->isVisible()) visible.push_back (child);
                for (size_t a = 0; a < visible.size(); ++a)
                {
                    const auto cid = visible[a]->getComponentID().toStdString();
                    check (s.getLocalBounds().contains (visible[a]->getBounds()), name + "inside its section: " + cid);
                    check (! visible[a]->getBounds().intersects (s.getTitleBounds()), name + "clear of the title: " + cid);
                    for (size_t b = a + 1; b < visible.size(); ++b)
                        check (! visible[a]->getBounds().intersects (visible[b]->getBounds()),
                               name + "no overlap: " + cid + " / " + visible[b]->getComponentID().toStdString());
                    if (auto* slider = dynamic_cast<juce::Slider*> (visible[a]))
                        smallestKnob = std::min (smallestKnob, std::min (slider->getWidth(), slider->getHeight() - slider->getTextBoxHeight()));
                    if (dynamic_cast<juce::ToggleButton*> (visible[a]) != nullptr)
                        smallestToggle = std::min (smallestToggle, visible[a]->getHeight());
                }
                if (auto* sw = s.getTitleSwitch())
                    check (sw->getHeight() >= 18 && sw->getWidth() >= 40 * 2, name + "title switch full size (" + std::to_string (sw->getWidth()) + "x" + std::to_string (sw->getHeight()) + ")");
            }
            check (smallestKnob >= 36, label + "knobs at least 36 px (smallest " + std::to_string (smallestKnob) + ")");
            check (smallestToggle >= 20, label + "toggles at least 20 px tall (smallest " + std::to_string (smallestToggle) + ")");
            note (label + "smallest knob " + std::to_string (smallestKnob) + " px");
        }

        editor->setSize (1080, 660);

        // Dimming: ignored controls follow the modes.
        const auto enabled = [&] (const char* pid) { auto* c = editor->findControl (pid); return c != nullptr && c->isEnabled(); };
        set (*p, id::osc1Mode, 1.0f);
        editor->updateEnablement();
        check (! enabled (id::osc1Shape) && enabled (id::osc1Saw), "Switched osc A: Shape dimmed, switches live");
        set (*p, id::osc1Mode, 0.0f);
        editor->updateEnablement();
        check (enabled (id::osc1Shape) && ! enabled (id::osc1Saw), "Variable osc A: switches dimmed, Shape live");
        set (*p, id::voiceMode, 1.0f);
        editor->updateEnablement();
        check (enabled (id::unisonVoices) && ! enabled (id::voices) && ! editor->getVoicePresetButton (0).isEnabled(),
               "Unison: Unison / Detune live, poly Voices dimmed");
        set (*p, id::voiceMode, 0.0f);
        set (*p, id::lfoSync, 1.0f);
        editor->updateEnablement();
        check (! enabled (id::lfoRate) && enabled (id::lfoDivision), "LFO Sync: Rate dimmed, Division live");
        set (*p, id::lfoSync, 0.0f);
        set (*p, id::pmodFiltEnv, 0.0f);
        set (*p, id::pmodOsc2, 0.0f);
        editor->updateEnablement();
        check (! enabled (id::pmodDestFreq1), "Poly-Mod sources at zero: destinations dimmed");
        set (*p, id::pmodOsc2, 0.3f);
        editor->updateEnablement();
        check (enabled (id::pmodDestFreq1), "Poly-Mod source up: destinations live");
        set (*p, id::pmodOsc2, 0.0f);
        editor->updateEnablement();

        // The 5 / 6 / 10 voice buttons set Voices in one undo step.
        p->getUndoHistory().clear();
        auto& ten = editor->getVoicePresetButton (2);
        ten.onClick();
        check (juce::roundToInt (get (*p, id::voices)) == 10 && ten.getToggleState(), "the 10 button sets 10 voices and lights");
        p->getUndoHistory().undo();
        check (juce::roundToInt (get (*p, id::voices)) == 6, "undo returns to 6 voices");

        // The Random button: a new patch, one undo step, shown as "Random".
        auto& random = editor->getRandomButton();
        check (random.getButtonText() == "Random" && random.getTooltip().isEmpty(), "Random button labelled, no hint text");
        p->getUndoHistory().clear();
        std::map<juce::String, float> before;
        for (const auto& pid : parameterIds) before[pid] = get (*p, pid);
        random.onClick();
        int changed = 0;
        for (const auto& pid : parameterIds) if (std::abs (get (*p, pid) - before[pid]) > 1.0e-5f) ++changed;
        check (changed >= 8, "Random button changes several parameters (" + std::to_string (changed) + ")");
        check (p->getPresetName() == "Random", "preset name shows Random");
        p->getUndoHistory().undo();
        int restored = 0;
        for (const auto& pid : parameterIds) if (std::abs (get (*p, pid) - before[pid]) <= 1.0e-5f) ++restored;
        check (restored == (int) parameterIds.size() && ! p->getUndoHistory().canUndo(), "one undo restores the whole patch");

        // Keys: Cmd-Z / Cmd-Shift-Z / Cmd-Y only.
        check (editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)), "Cmd-Z handled");
        check (editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)), "Cmd-Shift-Z handled");
        check (! editor->keyPressed (juce::KeyPress ('a', 0, 0)) && ! editor->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)), "other keys go to the host");

        // Snapshots: the factory init patch with a chord sounding (so the status shows voices).
        p->setCurrentProgram (0);
        p->presetChanged.dispatchPendingMessages(); // the preset box follows the change broadcast
        editor->updateEnablement();
        render (*p, 4800, { { 0, on (48) }, { 0, on (55) }, { 0, on (60) }, { 0, on (64) } });
        editor->setSize (940, 600);
        editor->updateEnablement();
        saveSnapshot (*editor, "-940x600");
        editor->setSize (1080, 660);
        saveSnapshot (*editor, "-1080x660");
        editor->setSize (2400, 1400);
        saveSnapshot (*editor, "-2400x1400");
        base.reset();
    }

    void testRandomPatch()
    {
        // The excluded parameters survive Random, even at non-default values.
        auto p = makeProcessor();
        set (*p, id::output, -4.0f);
        set (*p, id::voices, 8.0f);
        set (*p, id::bendRange, 7.0f);
        set (*p, id::osc1Coarse, -5.0f);
        set (*p, id::lfoSync, 1.0f);
        std::map<juce::String, float> excludedBefore;
        for (const auto& pid : randomiser::excludedParameters()) excludedBefore[pid] = get (*p, pid);

        int minChanged = 1000, excludedMoved = 0;
        for (int seed = 0; seed < 20; ++seed)
        {
            juce::Random random (seed);
            minChanged = std::min (minChanged, p->randomisePatch (random));
            for (const auto& [pid, value] : excludedBefore)
                if (std::abs (get (*p, pid) - value) > 1.0e-5f) ++excludedMoved;
        }
        check (minChanged >= 8, "each Random changes several parameters (fewest " + std::to_string (minChanged) + ")");
        check (excludedMoved == 0, "volume, voices, bend, tuning and LFO sync never randomised");
        check (p->getPresetName() == "Random", "preset name is Random");

        // One undo step restores everything.
        p->getUndoHistory().clear();
        std::vector<float> before;
        for (auto* r : p->getParameters()) before.push_back (r->getValue());
        juce::Random random (1234);
        p->randomisePatch (random);
        check (p->getUndoHistory().canUndo(), "Random is undoable");
        p->getUndoHistory().undo();
        bool same = true;
        for (size_t i = 0; i < before.size(); ++i)
            same = same && std::abs (p->getParameters()[(int) i]->getValue() - before[i]) < 1.0e-6f;
        check (same && ! p->getUndoHistory().canUndo(), "a single undo restores the patch before Random");

        // Musically usable: every random patch sounds, bounded and finite.
        int silent = 0, loud = 0, broken = 0;
        float lowest = 1.0e9f, highest = 0.0f;
        for (int seed = 0; seed < 60; ++seed)
        {
            auto q = makeProcessor();
            juce::Random r (1000 + seed);
            q->randomisePatch (r);
            const auto out = render (*q, 48000, { { 0, on (48) }, { 0, on (60) }, { 0, on (64) }, { 0, on (67) },
                                                  { 36000, off (48) }, { 36000, off (60) }, { 36000, off (64) }, { 36000, off (67) } });
            const auto peak = out.peak();
            if (! out.finite()) ++broken;
            if (peak < 1.0e-3f) ++silent;
            if (peak > 4.0f) ++loud;
            lowest = std::min (lowest, peak);
            highest = std::max (highest, peak);
        }
        check (broken == 0, "no NaN / inf over 60 random patches");
        check (silent == 0, "every random patch sounds (" + std::to_string (silent) + " silent)");
        check (loud == 0, "every random patch bounded below 4.0 (" + std::to_string (loud) + " louder)");
        note ("60 random patches: peak " + fmt (lowest, 3) + " .. " + fmt (highest, 3));
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                  testMetadata },
        { "silence without notes",                     testSilence },
        { "note-on bounded, release to silence",       testNoteBoundedAndReleases },
        { "voice stealing at 1..10 voices",            testVoiceStealing },
        { "unison and mono",                           testVoiceModes },
        { "max resonance: no NaN / denormals",         testMaxResonance },
        { "44.1k..192k",                               testSampleRates },
        { "state round trip",                          testStateRoundTrip },
        { "factory and user presets",                  testPresets },
        { "no dead parameters",                        testNoDeadParameters },
        { "undo history",                              testUndoHistory },
        { "analog: self-oscillation at k = 4",         testSelfOscillationAtK4 },
        { "analog: DC gain 1/(1+k)",                   testDcGain },
        { "analog: HF flattening at 5 kHz",            testHfFlattening },
        { "analog: Poly-Mod exp FM raises pitch",      testPolyModRaisesPitch },
        { "real-time safety and 10-voice CPU",         testRealtimeAndCpu },
        { "random patch",                              testRandomPatch },
        { "editor",                                    testEditor },
    };

    int failedTests = 0;
    for (const auto& test : tests)
    {
        const auto before = failedChecks;
        std::cout << test.name << "\n";
        test.run();
        const bool ok = failedChecks == before;
        if (! ok) ++failedTests;
        std::cout << "  " << (ok ? "[PASS]" : "[FAIL]") << "\n";
    }

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << " / " << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " failed\n";
    return failedChecks == 0 ? 0 : 1;
}
