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
#include "UI/ControlPanel.h"
#include "UI/Theme.h"
#include "UI/Displays.h"
#include "DSP/AnalogModel.h"
#include "DSP/Effects.h"
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
#include <tuple>
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
            // FX controls act while their module is on (the Division while synced, the Time while free).
            { id::echoPreset, { { id::echoOn, 1.0f } } },
            { id::echoMix, { { id::echoOn, 1.0f } } },
            { id::echoTime, { { id::echoOn, 1.0f } } },
            { id::echoSync, { { id::echoOn, 1.0f } } },
            { id::echoDivision, { { id::echoOn, 1.0f }, { id::echoSync, 1.0f } } },
            { id::echoFeedback, { { id::echoOn, 1.0f } } },
            { id::echoPingPong, { { id::echoOn, 1.0f } } },
            { id::reverbPreset, { { id::reverbOn, 1.0f } } },
            { id::reverbMix, { { id::reverbOn, 1.0f } } },
            { id::reverbSize, { { id::reverbOn, 1.0f } } },
            { id::reverbTone, { { id::reverbOn, 1.0f } } },
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
        // FX on too: the heaviest echo (Dub Feedback) into the longest reverb (Dark Pad Wash).
        p->applyFxPreset (0, 4);
        p->applyFxPreset (1, 6);
        set (*p, id::echoOn, 1.0f);
        set (*p, id::reverbOn, 1.0f);
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
        check (load < 0.25, "10 voices (4x Poly-Mod path) + Echo + Reverb under 25% of one core (" + fmt (100.0 * load, 2) + "%)");
        note ("10 voices, 4x path, Echo + Reverb: " + fmt (elapsed * 1000.0, 1) + " ms for " + fmt (audioSeconds, 1) + " s of audio = "
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

        auto* version = editor->findChildWithID ("version");
        auto* versionLabel = dynamic_cast<juce::Label*> (version);
        check (versionLabel != nullptr && versionLabel->getText() == "v" JucePlugin_VersionString, "version label shows v" JucePlugin_VersionString);

        // Every parameter has exactly one control, and every control is bound to a parameter.
        std::map<juce::String, int> controls;
        for (int page = 0; page < Editor::numPages; ++page)
        {
            for (auto& panel : editor->getPanels (page))
                for (auto* r : p->getParameters())
                {
                    const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
                    if (panel->findControl (pid) != nullptr) ++controls[pid];
                }
            for (auto& t : editor->getModeToggles (page))
                ++controls[t->getComponentID()];
        }
        ++controls[id::output]; // the header Output slider
        // The FX preset pickers (not attachments: picking one also sets that preset's macros).
        for (int module = 0; module < 2; ++module)
        {
            auto& picker = editor->getFxPresetPicker (module);
            const auto& names = module == 0 ? dsp::echoPresetNames() : dsp::reverbPresetNames();
            bool listed = picker.box.getNumItems() == names.size();
            for (int i = 0; listed && i < names.size(); ++i)
                listed = picker.box.getItemText (i) == names[i];
            check (listed, std::string (module == 0 ? "echo" : "reverb") + " preset box lists the 10 presets");
            if (listed && picker.getParentComponent() != nullptr)
                ++controls[module == 0 ? id::echoPreset : id::reverbPreset];
        }
        check (editor->findChildWithID (id::output) != nullptr, "header Output slider exists");
        int missing = 0;
        for (auto* r : p->getParameters())
        {
            const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
            if (controls[pid] != 1) { ++missing; check (false, "parameter " + pid.toStdString() + " has one control (has " + std::to_string (controls[pid]) + ")"); }
        }
        check (missing == 0, "every parameter has exactly one control");
        check (controls.size() == (size_t) p->getParameters().size(), "no control without a parameter");

        // Layout at the three sizes: inside the editor, nothing overlapping.
        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto sizeLabel = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";

            std::vector<juce::Component*> header;
            for (auto* cid : { "undo", "redo", "preset", "random", "savePreset", "loadPreset", id::output, "version" })
                if (auto* c = editor->findChildWithID (cid)) header.push_back (c);
            check (header.size() == 8, sizeLabel + "all header items present");
            check (editor->findChildWithID ("preset")->getWidth() >= 140, sizeLabel + "preset box at least 140 px");
            for (size_t i = 0; i < header.size(); ++i)
            {
                check (editor->getLocalBounds().contains (header[i]->getBounds()) && header[i]->getBottom() <= 60,
                       sizeLabel + "header item inside the header: " + header[i]->getComponentID().toStdString());
                for (size_t j = i + 1; j < header.size(); ++j)
                    check (! header[i]->getBounds().intersects (header[j]->getBounds()),
                           sizeLabel + "header items do not overlap: " + header[i]->getComponentID().toStdString() + " / " + header[j]->getComponentID().toStdString());
            }

            for (int page = 0; page < Editor::numPages; ++page)
            {
                editor->selectPage (page);
                const auto label = sizeLabel + Editor::pageName (page) + ": ";
                const auto content = editor->getContentArea();
                std::vector<juce::Rectangle<int>> blocks { editor->getGraph (page).getBounds() };
                check (editor->getGraph (page).getHeight() >= 120, label + "graph at least 120 px (" + std::to_string (editor->getGraph (page).getHeight()) + ")");
                for (auto& panel : editor->getPanels (page))
                {
                    blocks.push_back (panel->getBounds());
                    check (panel->getHeight() >= panel->getPreferredHeight (panel->getWidth()), label + "panel fits its controls: " + panel->getTitle().toStdString());
                    for (auto* child : panel->getChildren())
                        if (child->isVisible())
                            check (panel->getLocalBounds().contains (child->getBounds()), label + "control inside its panel: " + child->getComponentID().toStdString()
                                                                                              + " in " + panel->getTitle().toStdString());
                }
                for (auto& t : editor->getModeToggles (page))
                {
                    blocks.push_back (t->getBounds());
                    check (t->getWidth() >= ui::SegmentedToggle::minWidth && t->getHeight() >= ui::SegmentedToggle::minHeight - 4,
                           label + "mode switch full size: " + t->getComponentID().toStdString());
                }
                for (size_t i = 0; i < blocks.size(); ++i)
                {
                    check (content.contains (blocks[i]), label + "block " + std::to_string (i) + " inside the content area");
                    for (size_t j = i + 1; j < blocks.size(); ++j)
                        check (! blocks[i].intersects (blocks[j]), label + "blocks " + std::to_string (i) + " and " + std::to_string (j) + " do not overlap");
                }
            }
        }

        editor->setSize (1080, 660);

        // Dimming: ignored controls follow the modes.
        const auto enabled = [&] (int page, const char* pid)
        {
            for (auto& panel : editor->getPanels (page))
                if (auto* c = panel->findControl (pid)) return ! ui::isDimmed (*c);
            return false;
        };
        set (*p, id::osc1Mode, 1.0f);
        editor->updateEnablement();
        check (! enabled (Editor::oscillatorsPage, id::osc1Shape) && enabled (Editor::oscillatorsPage, id::osc1Saw), "Switched osc 1: Shape dimmed, switches live");
        set (*p, id::osc1Mode, 0.0f);
        editor->updateEnablement();
        check (enabled (Editor::oscillatorsPage, id::osc1Shape) && ! enabled (Editor::oscillatorsPage, id::osc1Saw), "Variable osc 1: switches dimmed, Shape live");
        set (*p, id::voiceMode, 1.0f);
        editor->updateEnablement();
        check (enabled (Editor::voicesPage, id::unisonVoices) && ! enabled (Editor::voicesPage, id::voices)
                   && ui::isDimmed (editor->getVoicePresetButton (0)) && editor->getVoicePresetButton (0).isEnabled(), "Unison: Unison / Detune live, poly Voices dimmed");
        set (*p, id::voiceMode, 0.0f);
        set (*p, id::lfoSync, 1.0f);
        editor->updateEnablement();
        check (! enabled (Editor::modulationPage, id::lfoRate) && enabled (Editor::modulationPage, id::lfoDivision), "LFO Sync: Rate dimmed, Division live");
        set (*p, id::lfoSync, 0.0f);
        editor->updateEnablement();

        // The 5 / 6 / 10 voice buttons set Voices in one undo step.
        p->getUndoHistory().clear();
        auto& ten = editor->getVoicePresetButton (2);
        ten.onClick();
        check (juce::roundToInt (get (*p, id::voices)) == 10 && ten.getToggleState(), "the 10 button sets 10 voices and lights");
        p->getUndoHistory().undo();
        check (juce::roundToInt (get (*p, id::voices)) == 6, "undo returns to 6 voices");

        // The Random button: a new patch in one undo step, named "Random"; undo brings both back.
        {
            auto& random = editor->getRandomButton();
            check (random.getButtonText() == "Random" && random.getTooltip().isEmpty(), "Random button labelled, no hint text");
            p->setCurrentProgram (1);
            const auto nameBefore = p->getPresetName();
            p->getUndoHistory().clear();
            std::vector<float> before;
            for (auto* r : p->getParameters()) before.push_back (r->getValue());
            random.onClick();
            int changed = 0;
            for (size_t i = 0; i < before.size(); ++i)
                if (std::abs (p->getParameters()[(int) i]->getValue() - before[i]) > 1.0e-6f) ++changed;
            check (changed >= 8, "Random button changes several parameters (" + std::to_string (changed) + ")");
            check (p->getPresetName() == "Random", "preset name shows Random");
            editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
            bool same = true;
            for (size_t i = 0; i < before.size(); ++i)
                same = same && std::abs (p->getParameters()[(int) i]->getValue() - before[i]) < 1.0e-6f;
            check (same && ! p->getUndoHistory().canUndo(), "one Cmd-Z restores the whole patch");
            check (p->getPresetName() == nameBefore, "undo restores the preset name (" + p->getPresetName().toStdString() + ")");
            p->getUndoHistory().redo();
            check (p->getPresetName() == "Random", "redo re-applies Random and its name");
            p->setCurrentProgram (0);
            p->presetChanged.dispatchPendingMessages();
        }

        // Keys: Cmd-Z / Cmd-Shift-Z / Cmd-Y only.
        check (editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)), "Cmd-Z handled");
        check (editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)), "Cmd-Shift-Z handled");
        check (! editor->keyPressed (juce::KeyPress ('a', 0, 0)) && ! editor->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)), "other keys go to the host");

        // Graphs: no hint text; readouts only on hover.
        for (int page = 0; page < Editor::numPages; ++page)
            check (editor->getGraph (page).getReadout().isEmpty(), std::string ("no readout without hover: ") + Editor::pageName (page));

        // The FX snapshot shows both modules on (a ping-pong echo into a hall).
        p->applyFxPreset (0, 2);
        p->applyFxPreset (1, 4);
        set (*p, id::echoOn, 1.0f);
        set (*p, id::reverbOn, 1.0f);
        editor->updateEnablement();
        // Snapshots: a chord sounding so the live graphs show something.
        render (*p, 24000, { { 0, on (48) }, { 0, on (55) }, { 0, on (60) }, { 0, on (64) } });
        for (int page = 0; page < Editor::numPages; ++page)
        {
            editor->selectPage (page);
            editor->getGraph (page).refresh();
            saveSnapshot (*editor, juce::String ("-") + Editor::pageName (page));
        }
        editor->setSize (940, 600);
        editor->selectPage (Editor::oscillatorsPage);
        editor->getGraph (Editor::oscillatorsPage).refresh();
        saveSnapshot (*editor, "-Oscillators-940x600");

        // Hover readout last, so the snapshots show the graphs without one.
        editor->setSize (1080, 660);
        editor->selectPage (Editor::filtersPage);
        auto& filterGraph = editor->getGraph (Editor::filtersPage);
        filterGraph.setHoverPoint (filterGraph.getPlotArea().getCentre());
        check (filterGraph.getReadout().isNotEmpty(), "filter graph hover readout ("
                                                          + filterGraph.getReadout().toStdString() + ")");
        base.reset();
    }

    //==============================================================================
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> position, juce::Point<float> down, int clicks, bool dragged,
                              juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, Source::defaultPressure, Source::defaultOrientation,
                 Source::defaultRotation, Source::defaultTiltX, Source::defaultTiltY, &c, &c, now, down, now, clicks, dragged };
    }

    void dragGraph (juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool fine = false)
    {
        auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | (fine ? juce::ModifierKeys::shiftModifier : 0));
        c.mouseDown (mouseAt (c, from, from, 1, false, mods));
        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            c.mouseDrag (mouseAt (c, from + (to - from) * ((float) i / (float) steps), from, 1, true, mods));
        c.mouseUp (mouseAt (c, to, from, 1, true, mods));
    }

    float paramValue (DooblrProphetProcessor& p, const char* pid)
    {
        auto* r = p.getState().getParameter (pid);
        return r->convertFrom0to1 (r->getValue());
    }

    void testFilterHandles()
    {
        using FD = dooblrprophet::ui::FilterDisplay;
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrProphetEditor");
        if (editor == nullptr) return;
        editor->selectPage (Editor::filtersPage);
        auto* graph = dynamic_cast<FD*> (&editor->getGraph (Editor::filtersPage));
        check (graph != nullptr, "the Filters graph is a FilterDisplay");
        if (graph == nullptr) return;

        juce::Slider* lpKnob = nullptr;
        juce::Slider* lpResoKnob = nullptr;
        for (auto& panel : editor->getPanels (Editor::filtersPage))
        {
            if (auto* c = dynamic_cast<juce::Slider*> (panel->findControl (id::lpfCutoff))) lpKnob = c;
            if (auto* c = dynamic_cast<juce::Slider*> (panel->findControl (id::lpfReso))) lpResoKnob = c;
        }
        check (lpKnob != nullptr && lpResoKnob != nullptr, "low-pass Cutoff and Resonance knobs found");

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";
            const auto plot = graph->getPlotArea();

            // Mapping round trip: value -> position -> value.
            double worstOct = 0.0, worstRes = 0.0;
            for (float hz : { 20.0f, 31.0f, 100.0f, 440.0f, 1000.0f, 4000.0f, 12345.0f, 20000.0f })
                for (float res : { 0.0f, 0.1f, 0.37f, 0.5f, 0.9f, 1.0f })
                {
                    const auto at = FD::positionFor (hz, res, plot);
                    worstOct = std::max (worstOct, std::abs (std::log2 ((double) FD::cutoffAt (at.x, plot) / hz)));
                    worstRes = std::max (worstRes, (double) std::abs (FD::resonanceAt (at.y, plot) - res));
                }
            check (worstOct < 0.001 && worstRes < 0.001, label + "position <-> cutoff / resonance round trip (worst " + fmt (worstOct * 1200.0, 3)
                                                             + " cents, " + fmt (worstRes * 100.0, 3) + "% res)");
            // Position -> value -> position, within a pixel.
            float worstPx = 0.0f;
            for (float tx : { 0.0f, 0.13f, 0.5f, 0.77f, 1.0f })
                for (float ty : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
                {
                    const auto at0 = FD::positionFor (FD::cutoffAt (plot.getX() + plot.getWidth() * tx, plot), FD::resonanceAt (plot.getY() + plot.getHeight() * ty, plot), plot);
                    const auto back = FD::positionFor (FD::cutoffAt (at0.x, plot), FD::resonanceAt (at0.y, plot), plot);
                    worstPx = std::max (worstPx, at0.getDistanceFrom (back));
                }
            check (worstPx < 0.5f, label + "graph position round trip within half a pixel (" + fmt (worstPx, 4) + " px)");

            // Handles inside the graph at the extremes.
            const auto bounds = graph->getLocalBounds().toFloat();
            bool inside = true;
            for (float hz : { FD::minHz, FD::maxHz })
                for (float res : { 0.0f, 1.0f })
                {
                    const auto at = FD::positionFor (hz, res, plot);
                    const auto circle = juce::Rectangle<float> (2.0f * FD::handleRadius, 2.0f * FD::handleRadius).withCentre (at);
                    inside = inside && bounds.contains (circle) && at.x >= plot.getX() && at.x <= plot.getRight()
                             && circle.getY() >= plot.getY() && circle.getBottom() <= plot.getBottom();
                }
            check (inside, label + "handles inside the graph at 20 Hz / 20 kHz, 0 / 100% resonance");
        }

        editor->setSize (1080, 660);
        auto& undo = p->getUndoHistory();
        undo.clear();
        graph->refresh();
        const auto plot = graph->getPlotArea();

        // Hover highlight picks the nearest handle; no hover, no readout.
        check (graph->getReadout().isEmpty(), "no readout without hover");
        const auto lpAt = graph->getHandlePosition (FD::lowPassHandle);
        check (lpAt.getDistanceFrom (FD::positionFor (4000.0f, 0.15f, plot)) < 0.01f, "low-pass handle at 4 kHz / 15%");
        graph->mouseMove (mouseAt (*graph, lpAt + juce::Point<float> (5.0f, 3.0f), {}, 0, false, {}));
        check (graph->getHoverHandle() == FD::lowPassHandle, "hovering near the low-pass handle highlights it");
        check (graph->getReadout().startsWith ("Low-pass"), "hover readout names the filter (" + graph->getReadout().toStdString() + ")");
        graph->mouseExit (mouseAt (*graph, { -5.0f, -5.0f }, {}, 0, false, {}));
        check (graph->getHoverHandle() == FD::noHandle, "hover cleared on exit");

        // Drag: right one octave and up: higher cutoff and resonance; one undo step; knobs follow.
        const auto octave = plot.getWidth() / std::log2 (FD::maxHz / FD::minHz);
        dragGraph (*graph, lpAt, lpAt + juce::Point<float> (octave, -60.0f));
        const auto hz = paramValue (*p, id::lpfCutoff), res = paramValue (*p, id::lpfReso);
        check (std::abs (std::log2 (hz / 8000.0f)) < 0.01f, "drag one octave right: 4 kHz -> 8 kHz (" + fmt (hz, 1) + " Hz)");
        check (res > 0.3f, "drag up raises resonance (" + fmt (res * 100.0f, 1) + "%)");
        check (std::abs (lpKnob->getValue() - hz) < 0.5 && std::abs (lpResoKnob->getValue() - res) < 1.0e-4, "knobs follow the handle");
        check (undo.getNumUndoableSteps() == 1, "one drag is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");
        check (graph->getHandlePosition (FD::lowPassHandle).getDistanceFrom (lpAt + juce::Point<float> (octave, -60.0f)) < 0.5f, "handle followed the pointer");
        undo.undo();
        check (std::abs (paramValue (*p, id::lpfCutoff) - 4000.0f) < 0.5f && std::abs (paramValue (*p, id::lpfReso) - 0.15f) < 1.0e-4f
                   && ! undo.canUndo(), "one undo restores cutoff and resonance");

        // Shift: a tenth of the movement.
        dragGraph (*graph, lpAt, lpAt + juce::Point<float> (octave, 0.0f), true);
        check (std::abs (std::log2 (paramValue (*p, id::lpfCutoff) / 4000.0f) - 0.1f) < 0.01f,
               "Shift-drag one octave moves a tenth of an octave (" + fmt (paramValue (*p, id::lpfCutoff), 1) + " Hz)");

        // Knob -> handle.
        lpKnob->setValue (500.0, juce::sendNotificationSync);
        graph->refresh();
        check (std::abs (graph->getHandlePosition (FD::lowPassHandle).x - FD::positionFor (500.0f, paramValue (*p, id::lpfReso), plot).x) < 0.5f,
               "handle follows the Cutoff knob");

        // High-pass handle (at 20 Hz, 0%): its own drag.
        undo.clear();
        const auto hpAt = graph->getHandlePosition (FD::highPassHandle);
        check (graph->handleAt (hpAt) == FD::highPassHandle, "high-pass handle hit at its position");
        dragGraph (*graph, hpAt, hpAt + juce::Point<float> (3.0f * octave, -40.0f));
        check (std::abs (std::log2 (paramValue (*p, id::hpfCutoff) / 160.0f)) < 0.01f && paramValue (*p, id::hpfReso) > 0.1f,
               "high-pass drag: 20 Hz -> 160 Hz, resonance up (" + fmt (paramValue (*p, id::hpfCutoff), 1) + " Hz, " + fmt (paramValue (*p, id::hpfReso) * 100.0f, 1) + "%)");
        check (undo.getNumUndoableSteps() == 1, "high-pass drag is one undo step");

        // Overlapping handles: the nearer one wins.
        p->getState().getParameter (id::hpfCutoff)->setValueNotifyingHost (p->getState().getParameter (id::hpfCutoff)->convertTo0to1 (500.0f));
        p->getState().getParameter (id::hpfReso)->setValueNotifyingHost (p->getState().getParameter (id::hpfReso)->convertTo0to1 (paramValue (*p, id::lpfReso)));
        graph->refresh();
        const auto lpNow = graph->getHandlePosition (FD::lowPassHandle), hpNow = graph->getHandlePosition (FD::highPassHandle);
        check (lpNow.getDistanceFrom (hpNow) < 1.0f, "handles can coincide");
        p->getState().getParameter (id::hpfCutoff)->setValueNotifyingHost (p->getState().getParameter (id::hpfCutoff)->convertTo0to1 (560.0f));
        graph->refresh();
        const auto hpNear = graph->getHandlePosition (FD::highPassHandle);
        const auto mid = (graph->getHandlePosition (FD::lowPassHandle) + hpNear) * 0.5f;
        check (graph->handleAt (hpNear + juce::Point<float> (1.0f, 0.0f)) == FD::highPassHandle
                   && graph->handleAt (graph->getHandlePosition (FD::lowPassHandle) - juce::Point<float> (1.0f, 0.0f)) == FD::lowPassHandle,
               "close handles: the nearest takes the click");
        check (graph->handleAt (mid + juce::Point<float> (0.0f, 100.0f)) == FD::noHandle, "a click away from both hits nothing");

        // Wheel over the low-pass handle: resonance only, one step.
        undo.clear();
        const auto cutoffBefore = paramValue (*p, id::lpfCutoff), resBefore = paramValue (*p, id::lpfReso);
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.2f;
        graph->mouseWheelMove (mouseAt (*graph, graph->getHandlePosition (FD::lowPassHandle) - juce::Point<float> (2.0f, 0.0f), {}, 0, false, {}), wheel);
        check (std::abs (paramValue (*p, id::lpfReso) - (resBefore + 0.1f)) < 1.0e-4f && std::abs (paramValue (*p, id::lpfCutoff) - cutoffBefore) < 0.01f,
               "wheel up over the handle: +10% resonance, cutoff unchanged");
        check (undo.getNumUndoableSteps() == 1, "wheel tick is one undo step");

        // Double-click resets that filter's cutoff and resonance, as one undo step.
        undo.clear();
        const auto hpCutoffBefore = paramValue (*p, id::hpfCutoff);
        const auto lpHere = graph->getHandlePosition (FD::lowPassHandle) - juce::Point<float> (2.0f, 0.0f);
        graph->mouseDown (mouseAt (*graph, lpHere, lpHere, 2, false));
        graph->mouseUp (mouseAt (*graph, lpHere, lpHere, 2, false));
        graph->mouseDoubleClick (mouseAt (*graph, lpHere, lpHere, 2, false));
        check (std::abs (paramValue (*p, id::lpfCutoff) - 4000.0f) < 0.5f && std::abs (paramValue (*p, id::lpfReso) - 0.15f) < 1.0e-4f,
               "double-click resets low-pass cutoff and resonance to 4 kHz / 15%");
        check (std::abs (paramValue (*p, id::hpfCutoff) - hpCutoffBefore) < 0.01f, "double-click leaves the high-pass alone");
        check (undo.getNumUndoableSteps() == 1, "double-click reset is one undo step");
        const auto hpHere = graph->getHandlePosition (FD::highPassHandle);
        graph->mouseDoubleClick (mouseAt (*graph, hpHere, hpHere, 2, false));
        check (std::abs (paramValue (*p, id::hpfCutoff) - 20.0f) < 0.01f && paramValue (*p, id::hpfReso) < 1.0e-4f, "double-click resets the high-pass to 20 Hz / 0%");

        // A click on empty graph changes nothing.
        undo.clear();
        dragGraph (*graph, { plot.getCentreX(), plot.getBottom() - 2.0f }, { plot.getCentreX() + 50.0f, plot.getY() + 5.0f });
        check (! undo.canUndo() && std::abs (paramValue (*p, id::lpfCutoff) - 4000.0f) < 0.5f, "dragging empty graph space changes nothing");

        // Snapshot with a resonant low-pass and an audible high-pass, low-pass handle hovered.
        p->getState().getParameter (id::lpfReso)->setValueNotifyingHost (0.55f);
        p->getState().getParameter (id::hpfCutoff)->setValueNotifyingHost (p->getState().getParameter (id::hpfCutoff)->convertTo0to1 (120.0f));
        p->getState().getParameter (id::hpfReso)->setValueNotifyingHost (0.3f);
        graph->refresh();
        graph->mouseMove (mouseAt (*graph, graph->getHandlePosition (FD::lowPassHandle), {}, 0, false, {}));
        saveSnapshot (*editor, "-Filters-handles");
        base.reset();
    }

    //==============================================================================
    /** Raw per-sample envelope levels of every voice, rendering one sample per block. */
    struct EnvTrace { std::vector<std::array<float, dsp::maxVoices>> filter, amp; };

    EnvTrace traceEnvelopes (Processor& p, int numSamples, const std::vector<Event>& events)
    {
        EnvTrace t;
        juce::AudioBuffer<float> buffer (2, 1);
        juce::MidiBuffer midi;
        for (int n = 0; n < numSamples; ++n)
        {
            buffer.clear();
            midi.clear();
            for (const auto& e : events)
                if (e.sample == n)
                    midi.addEvent (e.message, 0);
            p.processBlock (buffer, midi);
            std::array<float, dsp::maxVoices> f {}, a {};
            for (int v = 0; v < dsp::maxVoices; ++v)
            {
                f[(size_t) v] = p.getEngine().getVoice (v).getFilterEnvLevel();
                a[(size_t) v] = p.getEngine().getVoice (v).getAmpEnvLevel();
            }
            t.filter.push_back (f);
            t.amp.push_back (a);
        }
        return t;
    }

    void testFilterEnvSync()
    {
        struct Adsr { float a, d, s, r; };
        const Adsr ampSets[] { { 0.003f, 0.8f, 0.8f, 0.35f }, { 0.25f, 0.1f, 0.3f, 0.6f }, { 0.001f, 0.05f, 0.0f, 0.05f }, { 0.6f, 1.2f, 1.0f, 0.02f } };
        const Adsr ownSet { 0.02f, 0.4f, 0.55f, 0.9f };
        const std::vector<Event> chord { { 0, on (48, 0.9f) }, { 0, on (55, 0.4f) }, { 0, on (62, 0.7f) },
                                         { 30000, off (48) }, { 30000, off (55) }, { 30000, off (62) } };
        const auto setAdsr = [] (Processor& p, const char* a, const char* d, const char* su, const char* r, Adsr x)
        {
            set (p, a, x.a); set (p, d, x.d); set (p, su, x.s); set (p, r, x.r);
        };

        int setIndex = 0;
        for (const auto& amp : ampSets)
        {
            auto p = makeProcessor();
            set (*p, id::vintage, 0.4f);             // per-voice envelope-time slop applies to both alike
            set (*p, id::pmodFiltEnv, 0.3f);         // Poly-Mod from the filter env still applies
            setAdsr (*p, id::aenvAttack, id::aenvDecay, id::aenvSustain, id::aenvRelease, amp);
            setAdsr (*p, id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease, ownSet);
            set (*p, id::fenvSync, 1.0f);
            const auto t = traceEnvelopes (*p, 48000, chord);
            int differ = 0, nonzero = 0;
            for (size_t n = 0; n < t.filter.size(); ++n)
                for (size_t v = 0; v < (size_t) dsp::maxVoices; ++v)
                {
                    if (! juce::exactlyEqual (t.filter[n][v], t.amp[n][v])) ++differ;
                    if (t.amp[n][v] > 0.0f) ++nonzero;
                }
            const auto label = "ADSR set " + std::to_string (setIndex++) + ": ";
            check (nonzero > 10000 && differ == 0, label + "synced filter env equals amp env sample for sample (" + std::to_string (differ)
                                                       + " of " + std::to_string (nonzero) + " active samples differ)");
            check (std::abs (get (*p, id::fenvAttack) - ownSet.a) < 1.0e-4f && std::abs (get (*p, id::fenvDecay) - ownSet.d) < 1.0e-4f
                       && std::abs (get (*p, id::fenvSustain) - ownSet.s) < 1.0e-4f && std::abs (get (*p, id::fenvRelease) - ownSet.r) < 1.0e-4f,
                   label + "the filter env's own ADSR values are kept while synced");
            const auto eff = p->readSettings().effectiveFilterEnv();
            check (juce::exactlyEqual (eff.attack, get (*p, id::aenvAttack)) && juce::exactlyEqual (eff.sustain, get (*p, id::aenvSustain))
                       && juce::exactlyEqual (eff.velocity, get (*p, id::fenvVelAmt)),
                   label + "effective filter env: amp ADSR, the filter's own velocity amount");
        }

        // Off again: the filter env runs its own ADSR, independent of the amp env (vintage 0: exact reference).
        {
            auto p = makeProcessor();
            set (*p, id::vintage, 0.0f);
            setAdsr (*p, id::aenvAttack, id::aenvDecay, id::aenvSustain, id::aenvRelease, ampSets[1]);
            setAdsr (*p, id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease, ownSet);
            set (*p, id::fenvSync, 1.0f);
            traceEnvelopes (*p, 2000, { { 0, on (60) }, { 1000, off (60) } });
            set (*p, id::fenvSync, 0.0f);
            render (*p, 6 * 48000); // let the release (about 11 time constants to idle) finish
            const auto t = traceEnvelopes (*p, 48000, { { 0, on (60) }, { 30000, off (60) } });

            int voice = -1;
            for (int v = 0; v < dsp::maxVoices && voice < 0; ++v)
                if (t.amp[100][(size_t) v] > 0.0f) voice = v;
            check (voice >= 0, "a voice plays after sync off");
            if (voice >= 0)
            {
                dsp::Envelope reference;
                reference.setTimes (defaultRate, ownSet.a, ownSet.d, ownSet.s, ownSet.r);
                reference.noteOn();
                size_t start = 0;
                while (start < t.filter.size() && t.filter[start][(size_t) voice] == 0.0f) ++start;
                int differ = 0, ampDiffers = 0;
                for (size_t n = start; n < t.filter.size(); ++n)
                {
                    if (n == 30000) reference.noteOff();
                    const auto expected = reference.next();
                    if (std::abs (t.filter[n][(size_t) voice] - expected) > 1.0e-6f) ++differ;
                    if (std::abs (t.filter[n][(size_t) voice] - t.amp[n][(size_t) voice]) > 1.0e-3f) ++ampDiffers;
                }
                check (differ == 0, "sync off: filter env follows its own ADSR exactly (" + std::to_string (differ) + " samples off)");
                check (ampDiffers > 20000, "sync off: filter env independent of the amp env");
            }
            const auto eff = p->readSettings().effectiveFilterEnv();
            check (juce::exactlyEqual (eff.attack, get (*p, id::fenvAttack)) && juce::exactlyEqual (eff.release, get (*p, id::fenvRelease)),
                   "sync off restores the filter env's own ADSR");
        }

        // Audible: with Env Amount up, sync changes the sound; Env Amount 0 makes it silent-equal.
        {
            const auto run = [&] (float sync, float envAmt)
            {
                auto p = makeProcessor();
                set (*p, id::vintage, 0.0f);
                set (*p, id::lpfCutoff, 300.0f);
                set (*p, id::lpfEnvAmt, envAmt);
                set (*p, id::fenvSync, sync);
                return render (*p, 24000, { { 0, on (48) }, { 12000, off (48) } });
            };
            const auto a = run (0.0f, 0.8f), b = run (1.0f, 0.8f);
            double diff = 0.0;
            for (size_t i = 0; i < a.left.size(); ++i) diff = std::max (diff, (double) std::abs (a.left[i] - b.left[i]));
            check (diff > 1.0e-3, "sync changes the sound when Env Amount is up (max diff " + fmt (diff, 4) + ")");
        }

        // State: saved and restored, in sessions and user presets; old sessions without it load it off.
        {
            auto a = makeProcessor();
            set (*a, id::fenvSync, 1.0f);
            juce::MemoryBlock block;
            a->getStateInformation (block);
            auto b = makeProcessor();
            b->setStateInformation (block.getData(), (int) block.getSize());
            check (get (*b, id::fenvSync) > 0.5f, "session state restores Sync to Amp on");

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("prophet-sync-test" + juce::String (presets::fileExtension));
            a->saveUserPreset (file);
            auto c = makeProcessor();
            c->loadUserPreset (file);
            check (get (*c, id::fenvSync) > 0.5f, "user preset restores Sync to Amp on");
            file.deleteFile();

            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") == id::fenvSync) { xml->removeChildElement (child, true); break; }
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            set (*b, id::fenvSync, 1.0f);
            b->setStateInformation (old.getData(), (int) old.getSize());
            check (get (*b, id::fenvSync) < 0.5f, "a session saved before 0.1.4 loads with Sync to Amp off");
        }

        // Random turns it on now and then.
        {
            auto p = makeProcessor();
            int onCount = 0;
            for (int seed = 0; seed < 200; ++seed)
            {
                juce::Random r (seed);
                p->randomisePatch (r);
                if (get (*p, id::fenvSync) > 0.5f) ++onCount;
            }
            check (onCount >= 20 && onCount <= 70, "Random syncs the filter env in about 20% of patches (" + std::to_string (onCount) + " of 200)");
        }

        // Editor: the filter ADSR knobs dim while synced; the graph shows the effective envelope.
        {
            auto p = makeProcessor();
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
            auto* editor = dynamic_cast<Editor*> (base.get());
            const auto enabled = [&] (const char* pid)
            {
                for (auto& panel : editor->getPanels (Editor::envelopesPage))
                    if (auto* c = panel->findControl (pid)) return ! ui::isDimmed (*c);
                return false;
            };
            bool found = false;
            for (auto& panel : editor->getPanels (Editor::envelopesPage))
                if (panel->getTitle() == "FILTER ENVELOPE" && panel->findControl (id::fenvSync) != nullptr) found = true;
            check (found, "Sync to Amp sits in the Filter Envelope panel");
            editor->updateEnablement();
            check (enabled (id::fenvAttack) && enabled (id::fenvRelease), "sync off: filter ADSR knobs live");
            set (*p, id::fenvSync, 1.0f);
            editor->updateEnablement();
            check (! enabled (id::fenvAttack) && ! enabled (id::fenvDecay) && ! enabled (id::fenvSustain) && ! enabled (id::fenvRelease)
                       && enabled (id::fenvVelAmt) && enabled (id::fenvSync) && enabled (id::aenvAttack),
                   "sync on: filter Attack/Decay/Sustain/Release dimmed, Velocity and the amp ADSR live");
            editor->setSize (1080, 660);
            editor->selectPage (Editor::envelopesPage);
            auto& graph = editor->getGraph (Editor::envelopesPage);
            graph.refresh();
            const auto spanBefore = ui::EnvelopeDisplay::spanSeconds (graph.getShownSettings());
            set (*p, id::aenvRelease, 3.0f);
            check (graph.refresh(), "moving the amp Release redraws the envelope graph");
            const auto& shown = graph.getShownSettings();
            check (juce::exactlyEqual (shown.effectiveFilterEnv().release, get (*p, id::aenvRelease)) && get (*p, id::aenvRelease) > 2.5f && ui::EnvelopeDisplay::spanSeconds (shown) > spanBefore,
                   "the graph's filter envelope follows the amp Release");
            set (*p, id::aenvAttack, 0.3f);
            set (*p, id::aenvDecay, 1.0f);
            set (*p, id::aenvSustain, 0.6f);
            set (*p, id::aenvRelease, 1.2f);
            set (*p, id::fenvVelAmt, 0.4f);
            graph.refresh();
            saveSnapshot (*editor, "-Envelopes-sync");
            set (*p, id::fenvSync, 0.0f);
            editor->updateEnablement();
            graph.refresh();
            check (enabled (id::fenvAttack) && std::abs (graph.getShownSettings().effectiveFilterEnv().release - get (*p, id::fenvRelease)) < 1.0e-6f,
                   "sync off: knobs live again and the graph shows the filter's own envelope");
        }
    }


    //==============================================================================
    /** A window for the real-mouse tests: JUCE's own mouse dispatch (MouseInputSource: the component
        under the pointer, enter / exit, down / drag / up, isMouseOver) runs exactly as for a window
        on screen, but the window is virtual, so the test neither flashes a window nor depends on
        what else is on the user's screen (the macOS peer refuses points under other apps' windows). */
    class TestPeer final : public juce::ComponentPeer
    {
    public:
        TestPeer (juce::Component& c, int flags) : juce::ComponentPeer (c, flags), bounds (c.getBounds()) {}

        void* getNativeHandle() const override { return nullptr; }
        void setVisible (bool) override {}
        void setTitle (const juce::String&) override {}
        void setBounds (const juce::Rectangle<int>& b, bool) override { bounds = b; }
        juce::Rectangle<int> getBounds() const override { return bounds; }
        juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + bounds.getPosition().toFloat(); }
        juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - bounds.getPosition().toFloat(); }
        void setMinimised (bool) override {}
        bool isMinimised() const override { return false; }
        bool isShowing() const override { return true; }
        void setFullScreen (bool) override {}
        bool isFullScreen() const override { return false; }
        void setIcon (const juce::Image&) override {}
        bool contains (juce::Point<int> p, bool) const override { return bounds.withZeroOrigin().contains (p); }
        OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
        juce::BorderSize<int> getFrameSize() const override { return {}; }
        bool setAlwaysOnTop (bool) override { return true; }
        void toFront (bool) override {}
        void toBehind (juce::ComponentPeer*) override {}
        bool isFocused() const override { return true; }
        void grabFocus() override {}
        void repaint (const juce::Rectangle<int>&) override {}
        void performAnyPendingRepaintsNow() override {}
        void setAlpha (float) override {}
        juce::StringArray getAvailableRenderingEngines() override { return { "Software" }; }
        void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}

    private:
        juce::Rectangle<int> bounds;
    };

    /** Holds the editor in a TestPeer window at the screen origin. */
    class TestWindow final : public juce::Component
    {
    public:
        explicit TestWindow (juce::Component& content)
        {
            setBounds (content.getLocalBounds().withPosition (100, 80));
            addAndMakeVisible (content);
            content.setTopLeftPosition (0, 0);
            addToDesktop (0);
            setVisible (true);
        }
        ~TestWindow() override { removeFromDesktop(); removeAllChildren(); }
        juce::ComponentPeer* createNewPeer (int flags, void*) override { return new TestPeer (*this, flags); }
    };

    //==============================================================================
    /** Real mouse input through the window's peer: the same path a click on screen takes
        (JUCE's MouseInputSource finds the component under the pointer, then enter / down / up). */
    struct PeerMouse
    {
        juce::ComponentPeer& peer;
        juce::int64 time = 1000000;

        void event (juce::Point<float> pos, juce::ModifierKeys mods)
        {
            time += 400; // far enough apart that no two clicks make a double-click
            peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods, juce::MouseInputSource::defaultPressure,
                                   juce::MouseInputSource::defaultOrientation, time);
        }
        void click (juce::Point<float> pos)
        {
            event (pos, juce::ModifierKeys());
            event (pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            event (pos, juce::ModifierKeys());
        }
        void drag (juce::Point<float> from, juce::Point<float> to)
        {
            event (from, juce::ModifierKeys());
            event (from, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            for (int i = 1; i <= 6; ++i)
                event (from + (to - from) * ((float) i / 6.0f), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            event (to, juce::ModifierKeys());
        }
    };

    template <typename Fn>
    void forEachShowing (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isShowing())
                continue;
            fn (*child);
            forEachShowing (*child, fn);
        }
    }

    void testRealMouse()
    {
        const std::pair<int, int> sizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
        for (const auto& [w, h] : sizes)
        {
            for (int mode = 0; mode < 2; ++mode)
            {
                auto p = makeProcessor();
                auto& state = p->getState();
                set (*p, id::osc1Mode, (float) mode);
                set (*p, id::osc2Mode, (float) mode);
                std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
                auto* editor = dynamic_cast<Editor*> (base.get());
                if (editor == nullptr) { check (false, "editor"); return; }
                editor->setSize (w, h);
                TestWindow window (*editor);
                auto* peer = window.getPeer();
                check (peer != nullptr, "the editor opens in a window");
                if (peer == nullptr) return;
                PeerMouse mouse { *peer };

                const auto label = std::to_string (w) + "x" + std::to_string (h) + (mode == 0 ? " Variable: " : " Switched: ");
                int buttons = 0, buttonsOk = 0, knobs = 0, knobsOk = 0, segments = 0, segmentsOk = 0, hits = 0, hitsOk = 0;
                std::vector<std::string> failures;
                const auto fail = [&] (const juce::String& what) { if (failures.size() < 12) failures.push_back (what.toStdString()); };

                for (int page = 0; page < Editor::numPages; ++page)
                {
                    editor->selectPage (page);
                    editor->updateEnablement();
                    const auto where = juce::String (Editor::pageName (page)) + " ";

                    std::vector<juce::Component*> found;
                    forEachShowing (*editor, [&] (juce::Component& c)
                    {
                        if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr
                            || dynamic_cast<juce::ComboBox*> (&c) != nullptr || dynamic_cast<ui::SegmentedToggle*> (&c) != nullptr)
                            found.push_back (&c);
                    });

                    for (auto* c : found)
                    {
                        const auto name = where + (c->getComponentID().isNotEmpty() ? c->getComponentID() : c->getName());
                        const auto centre = editor->getLocalPoint (c, c->getLocalBounds().getCentre()).toFloat();

                        // Every control is the one under the pointer at its centre (nothing on top of it).
                        ++hits;
                        auto* hit = editor->getComponentAt (centre.roundToInt());
                        if (hit == c || c->isParentOf (hit)) ++hitsOk;
                        else fail (name + " is covered by " + (hit != nullptr ? hit->getComponentID() + " / " + hit->getName() : juce::String ("nothing")));

                        auto* parameter = state.getParameter (c->getComponentID());

                        if (auto* toggle = dynamic_cast<ui::SegmentedToggle*> (c))
                        {
                            for (int i = 0; i < toggle->getNumSegments(); ++i)
                            {
                                const auto before = toggle->getSelectedIndex();
                                if (i == before) continue;
                                ++segments;
                                mouse.click (editor->getLocalPoint (c, toggle->getSegmentBounds (i).getCentre()).toFloat());
                                if (toggle->getSelectedIndex() == i) ++segmentsOk;
                                else fail (name + " segment " + toggle->getSegmentText (i) + " does not select");
                                mouse.click (editor->getLocalPoint (c, toggle->getSegmentBounds (before).getCentre()).toFloat());
                                editor->updateEnablement();
                            }
                        }
                        else if (auto* button = dynamic_cast<juce::ToggleButton*> (c); button != nullptr && parameter != nullptr)
                        {
                            ++buttons;
                            const auto before = parameter->getValue() > 0.5f;
                            mouse.click (centre);
                            const auto after = parameter->getValue() > 0.5f;
                            if (after != before && button->getToggleState() == after) ++buttonsOk;
                            else fail (name + juce::String (ui::isDimmed (*c) ? " (dimmed)" : "") + " does not toggle");
                            mouse.click (centre);
                            if ((parameter->getValue() > 0.5f) != before) fail (name + " does not toggle back");
                        }
                        else if (auto* textButton = dynamic_cast<juce::TextButton*> (c); textButton != nullptr && c->getComponentID().startsWith ("page_"))
                        {
                            ++buttons;
                            mouse.click (centre);
                            if (textButton->getToggleState()) ++buttonsOk;
                            else fail (name + " does not open its page");
                            editor->selectPage (page);
                        }
                        else if (auto* preset = dynamic_cast<juce::TextButton*> (c); preset != nullptr && c->getComponentID().startsWith ("voicesPreset"))
                        {
                            // The 5 / 6 / 10 voice buttons: dimmed outside Poly, still clickable.
                            ++buttons;
                            const auto count = c->getComponentID().fromFirstOccurrenceOf ("voicesPreset", false, false).getIntValue();
                            const auto before = get (*p, id::voices);
                            set (*p, id::voices, count == 5 ? 8.0f : 5.0f);
                            mouse.click (centre);
                            if (juce::roundToInt (get (*p, id::voices)) == count) ++buttonsOk;
                            else fail (name + juce::String (ui::isDimmed (*c) ? " (dimmed)" : "") + " does not set Voices");
                            set (*p, id::voices, before);
                        }
                        else if (c == &editor->getRandomButton())
                        {
                            // Random, then Undo and Redo through their own buttons.
                            ++buttons;
                            const auto snapshot = [&] { std::vector<float> v; for (auto* q : p->getParameters()) v.push_back (q->getValue()); return v; };
                            // Undo / Redo write normalised values back through the parameter's range, so compare within a hair.
                            const auto same = [] (const std::vector<float>& a, const std::vector<float>& b)
                            { for (size_t k = 0; k < a.size(); ++k) if (std::abs (a[k] - b[k]) > 1.0e-5f) return false; return true; };
                            const auto before = snapshot();
                            mouse.click (centre);
                            const auto randomised = snapshot();
                            if (! same (randomised, before)) ++buttonsOk;
                            else fail (name + " does not change the patch");
                            for (auto* other : found)
                                if (other->getName() == "Undo" && other->isShowing())
                                {
                                    ++buttons;
                                    mouse.click (editor->getLocalPoint (other, other->getLocalBounds().getCentre()).toFloat());
                                    if (same (snapshot(), before)) ++buttonsOk; else fail (where + "Undo does not restore the patch");
                                }
                            for (auto* other : found)
                                if (other->getName() == "Redo" && other->isShowing())
                                {
                                    ++buttons;
                                    mouse.click (editor->getLocalPoint (other, other->getLocalBounds().getCentre()).toFloat());
                                    if (same (snapshot(), randomised)) ++buttonsOk; else fail (where + "Redo does not reapply the patch");
                                    mouse.click (editor->getLocalPoint (other, other->getLocalBounds().getCentre()).toFloat());
                                }
                            for (auto* other : found)
                                if (other->getName() == "Undo" && other->isShowing())
                                    mouse.click (editor->getLocalPoint (other, other->getLocalBounds().getCentre()).toFloat());
                            if (! same (snapshot(), before)) fail (where + "Random could not be undone after the test");
                        }
                        else if (auto* slider = dynamic_cast<juce::Slider*> (c); slider != nullptr && parameter != nullptr
                                                                                && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
                        {
                            ++knobs;
                            const auto before = parameter->getValue();
                            const auto knobCentre = editor->getLocalPoint (c, juce::Point<int> (c->getWidth() / 2, (c->getHeight() - 16) / 2)).toFloat();
                            const auto up = before < 0.5f ? -40.0f : 40.0f;
                            mouse.drag (knobCentre, knobCentre + juce::Point<float> (0.0f, up));
                            if (std::abs (parameter->getValue() - before) > 1.0e-4f) ++knobsOk;
                            else fail (name + juce::String (ui::isDimmed (*c) ? " (dimmed)" : "") + " does not turn");
                            parameter->setValueNotifyingHost (before);
                        }
                    }
                    editor->updateEnablement();
                }

                check (hitsOk == hits, label + "every button, switch, knob and box is under the pointer at its centre (" + std::to_string (hitsOk) + " of " + std::to_string (hits) + ")");
                check (buttons > 0 && buttonsOk == buttons, label + "every parameter toggle and page button clicks, dimmed or not (" + std::to_string (buttonsOk) + " of " + std::to_string (buttons) + ")");
                check (segments > 0 && segmentsOk == segments, label + "every mode switch segment clicks (" + std::to_string (segmentsOk) + " of " + std::to_string (segments) + ")");
                check (knobs > 0 && knobsOk == knobs, label + "every knob drags, dimmed or not (" + std::to_string (knobsOk) + " of " + std::to_string (knobs) + ")");
                note (label + std::to_string (hitsOk) + "/" + std::to_string (hits) + " hit-tests, " + std::to_string (buttonsOk) + "/" + std::to_string (buttons)
                      + " buttons, " + std::to_string (segmentsOk) + "/" + std::to_string (segments) + " switch segments, " + std::to_string (knobsOk) + "/" + std::to_string (knobs) + " knobs");
                for (const auto& f : failures)
                    note ("  " + f);
            }
        }

        // The reported case, explicitly: Variable mode (the default), Oscillators page, both oscillators.
        {
            auto p = makeProcessor();
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
            auto* editor = dynamic_cast<Editor*> (base.get());
            editor->setSize (1080, 660);
            TestWindow window (*editor);
            PeerMouse mouse { *window.getPeer() };
            editor->selectPage (Editor::oscillatorsPage);
            editor->updateEnablement();
            for (auto* pid : { id::osc1Tri, id::osc1Pulse, id::osc1Sync, id::osc2Tri, id::osc2Pulse })
            {
                juce::Component* c = nullptr;
                for (auto& panel : editor->getPanels (Editor::oscillatorsPage))
                    if (auto* f = panel->findControl (pid)) c = f;
                const auto before = get (*p, pid);
                mouse.click (editor->getLocalPoint (c, c->getLocalBounds().getCentre()).toFloat());
                check (! juce::exactlyEqual (get (*p, pid), before), juce::String ("Variable mode: clicking ").toStdString() + pid + " selects it");
            }
        }
    }


    //==============================================================================
    // FX (0.2.0): Echo then Reverb.
    std::vector<float> noiseBurst (int length, int burst, unsigned seed = 7)
    {
        std::vector<float> x ((size_t) length, 0.0f);
        juce::Random r ((juce::int64) seed);
        for (int i = 0; i < burst && i < length; ++i)
            x[(size_t) i] = 0.5f * (r.nextFloat() * 2.0f - 1.0f);
        return x;
    }

    /** Runs the FX chain over a stereo signal in blocks; returns the output. */
    Render runFx (dsp::FxChain& fx, const std::vector<float>& inL, const std::vector<float>& inR, const dsp::FxSettings& s,
                  double bpm = 120.0, int block = blockSize)
    {
        Render out { inL, inR };
        for (size_t start = 0; start < inL.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, inL.size() - start);
            fx.process (out.left.data() + start, out.right.data() + start, n, s, bpm);
        }
        return out;
    }

    /** Index of the largest |x| in [from, to). */
    size_t peakIndex (const std::vector<float>& x, size_t from, size_t to)
    {
        size_t best = from;
        for (size_t i = from; i < std::min (to, x.size()); ++i)
            if (std::abs (x[i]) > std::abs (x[best])) best = i;
        return best;
    }

    double windowRms (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        to = std::min (to, x.size());
        for (size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
    }

    void testFxBypass()
    {
        // Off (the default) is bit-identical to no FX stage at all, including after use.
        const auto n = 48000;
        auto inL = noiseBurst (n, n, 1), inR = noiseBurst (n, n, 2);
        dsp::FxChain fx;
        fx.prepare (defaultRate);
        dsp::FxSettings idle;
        auto out = runFx (fx, inL, inR, idle);
        check (out.left == inL && out.right == inR, "both modules off: output bit-identical to the input");

        auto both = idle;
        both.echoOn = both.reverbOn = true;
        runFx (fx, inL, inR, both);                       // used
        runFx (fx, std::vector<float> (2048, 0.0f), std::vector<float> (2048, 0.0f), idle); // the 20 ms fade-out
        out = runFx (fx, inL, inR, idle);
        check (out.left == inL && out.right == inR, "switched off after use: bit-identical again once faded (20 ms)");

        // Through the processor: FX off renders exactly what the engine renders.
        auto a = makeProcessor(), b = makeProcessor();
        set (*b, id::echoMix, 0.9f);
        set (*b, id::reverbSize, 0.9f);
        set (*b, id::echoPingPong, 1.0f);
        const std::vector<Event> chord { { 0, on (48) }, { 0, on (60) }, { 12000, off (48) }, { 12000, off (60) } };
        const auto ra = render (*a, 24000, chord), rb = render (*b, 24000, chord);
        check (ra.left == rb.left && ra.right == rb.right, "processor with FX off: bit-identical whatever the FX settings");

        // A mono bus: the stereo FX result is summed back into the one channel, still bounded.
        dsp::FxChain mono;
        mono.prepare (defaultRate);
        auto x = noiseBurst (24000, 2400, 3);
        for (size_t i = 0; i < x.size(); i += blockSize)
            mono.process (x.data() + i, x.data() + i, (int) std::min<size_t> (blockSize, x.size() - i), both, 120.0);
        bool finite = true; float peak = 0.0f;
        for (auto v : x) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        check (finite && peak < 2.0f && windowRms (x, 12000, 24000) > 1.0e-4, "mono bus (L == R): finite, bounded, with a tail");
    }

    void testFxPresets()
    {
        // Every preset with its Feedback (echo) or Size (reverb) at the maximum, fully wet:
        // finite, bounded, and the tail decays.
        const auto rate = defaultRate;
        const auto length = (int) (30.0 * rate);
        const auto in = noiseBurst (length, (int) (0.3 * rate), 11);

        for (int i = 0; i < dsp::numEchoPresets; ++i)
        {
            const auto& e = dsp::echoPresets()[(size_t) i];
            dsp::FxSettings s;
            s.echoOn = true; s.echoPreset = i; s.echoMix = 1.0f; s.echoTimeMs = e.timeMs; s.echoSync = e.sync;
            s.echoDivision = e.division; s.echoPingPong = e.pingPong; s.echoFeedback = dsp::maxEchoFeedback;
            dsp::FxChain fx;
            fx.prepare (rate);
            const auto out = runFx (fx, in, in, s);
            const auto early = out.rms ((size_t) (2.0 * rate), (size_t) (8.0 * rate));
            const auto late = out.rms ((size_t) (24.0 * rate), (size_t) (30.0 * rate));
            const auto label = std::string ("echo ") + e.name + ": ";
            check (out.finite() && out.denormals() == 0, label + "finite, no denormals");
            check (out.peak() < 1.5f, label + "bounded at 95% feedback (peak " + fmt (out.peak(), 3) + ")");
            check (early > 1.0e-4 && late < 0.8 * early, label + "the tail decays (" + fmt (20.0 * std::log10 (late / early), 1) + " dB from 2-8 s to 24-30 s)");
            note (label + "peak " + fmt (out.peak(), 3) + ", tail " + fmt (20.0 * std::log10 (std::max (late, 1.0e-12) / early), 1) + " dB");
        }

        for (int i = 0; i < dsp::numReverbPresets; ++i)
        {
            const auto& v = dsp::reverbPresets()[(size_t) i];
            dsp::FxSettings s;
            s.reverbOn = true; s.reverbPreset = i; s.reverbMix = 1.0f; s.reverbSize = 1.0f; s.reverbTone = v.tone;
            dsp::FxChain fx;
            fx.prepare (rate);
            const auto out = runFx (fx, in, in, s);
            const auto rt60 = dsp::reverbDecaySeconds (s);
            const auto from = (size_t) ((0.4 + 0.1 * rt60) * rate), to = (size_t) ((0.4 + 0.5 * rt60) * rate);
            const auto a = out.rms (from, from + (size_t) (0.1 * rate)), b = out.rms (to, to + (size_t) (0.1 * rate));
            const auto dbPerRt60 = 20.0 * std::log10 (std::max (b, 1.0e-12) / a) / 0.4; // measured over 0.4 x RT60
            const auto label = std::string ("reverb ") + v.name + ": ";
            check (out.finite() && out.denormals() == 0, label + "finite, no denormals");
            check (out.peak() < 1.5f, label + "bounded at maximum size (peak " + fmt (out.peak(), 3) + ")");
            check (a > 1.0e-4 && b < a, label + "the tail decays");
            check (dbPerRt60 < -30.0 && dbPerRt60 > -120.0, label + "decay near its RT60 (" + fmt (dbPerRt60, 1) + " dB per RT60 " + fmt (rt60, 2) + " s)");
            check (out.rms ((size_t) (29.0 * rate)) < 1.0e-3 * a || rt60 > 14.5, label + "silent by 30 s");
            check (out.rms (0, (size_t) (0.5 * rate)) > 0.0 && std::abs (out.left[(size_t) (0.6 * rate)] - out.right[(size_t) (0.6 * rate)]) > 0.0f,
                   label + "stereo from a mono input");
            note (label + "RT60 " + fmt (rt60, 2) + " s, measured " + fmt (dbPerRt60, 1) + " dB per RT60, peak " + fmt (out.peak(), 3));
        }

        // A preset choice sets the macros it starts from (and Ping-Pong) as one undo step.
        auto p = makeProcessor();
        p->getUndoHistory().clear();
        p->applyFxPreset (0, 2); // Ping-Pong Pad
        const auto& e = dsp::echoPresets()[2];
        check (std::abs (get (*p, id::echoPreset) - 2.0f) < 1.0e-4f && std::abs (get (*p, id::echoMix) - e.mix) < 1.0e-3f
                   && std::abs (get (*p, id::echoFeedback) - e.feedback) < 1.0e-3f && get (*p, id::echoPingPong) > 0.5f
                   && get (*p, id::echoSync) > 0.5f && juce::roundToInt (get (*p, id::echoDivision)) == e.division,
               "Ping-Pong Pad sets Mix, Feedback, Sync, Division and Ping-Pong");
        set (*p, id::echoPingPong, 0.0f);
        check (get (*p, id::echoPingPong) < 0.5f && juce::roundToInt (get (*p, id::echoPreset)) == 2, "Ping-Pong switches independently of the preset");
        p->getUndoHistory().clear();
        p->applyFxPreset (1, 5); // Big Hall
        check (std::abs (get (*p, id::reverbSize) - dsp::reverbPresets()[5].size) < 1.0e-3f, "Big Hall sets the reverb Size");
        p->getUndoHistory().undo();
        check (juce::roundToInt (get (*p, id::reverbPreset)) == 0 && std::abs (get (*p, id::reverbSize) - dsp::reverbPresets()[0].size) < 1.0e-3f
                   && ! p->getUndoHistory().canUndo(), "one undo restores the reverb preset and its macros");

        // Saved in the patch state.
        p->applyFxPreset (0, 9);
        set (*p, id::echoOn, 1.0f);
        juce::MemoryBlock block;
        p->getStateInformation (block);
        auto q = makeProcessor();
        q->setStateInformation (block.getData(), (int) block.getSize());
        check (juce::roundToInt (get (*q, id::echoPreset)) == 9 && get (*q, id::echoOn) > 0.5f
                   && std::abs (get (*q, id::echoFeedback) - dsp::echoPresets()[9].feedback) < 1.0e-3f, "FX preset, macros and on state restored from the patch state");

        // Factory patches leave FX off.
        int fxOn = 0;
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            if (get (*p, id::echoOn) > 0.5f || get (*p, id::reverbOn) > 0.5f) ++fxOn;
        }
        check (fxOn == 0, "factory presets load with the FX off (" + std::to_string (fxOn) + " on)");
    }

    void testFxPingPong()
    {
        const auto rate = defaultRate;
        const auto timeMs = 250.0f;
        const auto T = (size_t) std::lround (timeMs * 0.001 * rate);
        const auto tolerance = (size_t) (0.002 * rate);

        const auto run = [&] (bool pingPong, bool impulseLeft)
        {
            dsp::FxSettings s;
            s.echoOn = true; s.echoPreset = 2; s.echoMix = 1.0f; s.echoSync = false; s.echoTimeMs = timeMs;
            s.echoFeedback = 0.6f; s.echoPingPong = pingPong;
            dsp::FxChain fx;
            fx.prepare (rate);
            std::vector<float> l ((size_t) (1.2 * rate), 0.0f), r = l;
            (impulseLeft ? l : r)[0] = 1.0f;
            return runFx (fx, l, r, s);
        };

        const auto energyAround = [&] (const std::vector<float>& x, size_t centre) { return windowRms (x, centre - tolerance, centre + tolerance); };

        {
            const auto out = run (true, true);
            const auto l1 = peakIndex (out.left, T - tolerance, T + tolerance);
            const auto r2 = peakIndex (out.right, 2 * T - tolerance, 2 * T + tolerance);
            const auto l3 = peakIndex (out.left, 3 * T - tolerance, 3 * T + tolerance);
            const auto ms = [&] (size_t i) { return fmt (1000.0 * (double) i / rate, 2) + " ms"; };
            check (std::abs ((double) l1 - (double) T) < (double) tolerance && energyAround (out.left, T) > 10.0 * energyAround (out.right, T),
                   "ping-pong: 1st repeat on the left at T (" + ms (l1) + ")");
            check (std::abs ((double) r2 - 2.0 * (double) T) < (double) tolerance && energyAround (out.right, 2 * T) > 10.0 * energyAround (out.left, 2 * T),
                   "ping-pong: 2nd repeat on the right at 2T (" + ms (r2) + ")");
            check (std::abs ((double) l3 - 3.0 * (double) T) < (double) tolerance && energyAround (out.left, 3 * T) > 10.0 * energyAround (out.right, 3 * T),
                   "ping-pong: 3rd repeat back on the left at 3T (" + ms (l3) + ")");
            check (energyAround (out.right, 2 * T) < energyAround (out.left, T) && energyAround (out.left, 3 * T) < energyAround (out.right, 2 * T),
                   "ping-pong: each repeat quieter than the last");
            note ("ping-pong at 250 ms: L " + ms (l1) + ", R " + ms (r2) + ", L " + ms (l3));
        }
        {
            const auto out = run (true, false); // a right-only input is mono-summed into the first (left) side
            check (energyAround (out.left, T) > 10.0 * energyAround (out.right, T) && energyAround (out.right, 2 * T) > 10.0 * energyAround (out.left, 2 * T),
                   "ping-pong: a right-only input still starts on the left (mono-summed input)");
        }
        {
            const auto out = run (false, true); // stereo echo: each side repeats on its own side
            check (energyAround (out.left, T) > 0.0 && energyAround (out.left, 2 * T) > 0.0 && windowRms (out.right, 0, out.right.size()) < 1.0e-9,
                   "Ping-Pong off: a left input repeats only on the left");
        }
    }

    void testFxTempoAndRates()
    {
        // Tempo sync: the division's length at the host tempo.
        dsp::FxSettings s;
        s.echoSync = true;
        s.echoDivision = dsp::echoDivisionNames().indexOf ("1/4");
        check (std::abs (dsp::echoSeconds (s, 120.0) - 0.5) < 1.0e-9, "1/4 at 120 bpm = 500 ms");
        s.echoDivision = dsp::echoDivisionNames().indexOf ("1/8.");
        check (std::abs (dsp::echoSeconds (s, 90.0) - 0.5) < 1.0e-9, "1/8. at 90 bpm = 500 ms");
        s.echoDivision = dsp::echoDivisionNames().indexOf ("1/16T");
        check (std::abs (dsp::echoSeconds (s, 100.0) - 0.1) < 1.0e-9, "1/16T at 100 bpm = 100 ms");
        s.echoDivision = dsp::echoDivisionNames().indexOf ("1/1");
        check (std::abs (dsp::echoSeconds (s, 40.0) - dsp::maxEchoSeconds) < 1.0e-9, "1/1 at 40 bpm clamps to 4 s");

        for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            const auto label = fmt (rate / 1000.0, 1) + " kHz: ";
            for (const auto& [bpm, division, expected] : { std::tuple<double, const char*, double> { 120.0, "1/4", 0.5 }, { 150.0, "1/8", 0.2 } })
            {
                dsp::FxSettings e;
                e.echoOn = true; e.echoPreset = 6; e.echoMix = 1.0f; e.echoFeedback = 0.0f;
                e.echoSync = true; e.echoDivision = dsp::echoDivisionNames().indexOf (division);
                dsp::FxChain fx;
                fx.prepare (rate);
                std::vector<float> l ((size_t) (0.8 * rate), 0.0f);
                l[0] = 1.0f;
                const auto out = runFx (fx, l, l, e, bpm);
                const auto at = (double) peakIndex (out.left, (size_t) (0.05 * rate), out.left.size()) / rate;
                check (std::abs (at - expected) < 0.002, label + division + " at " + fmt (bpm, 0) + " bpm repeats at " + fmt (1000.0 * at, 2)
                                                             + " ms (expected " + fmt (1000.0 * expected, 0) + ")");
            }

            dsp::FxSettings both;
            both.echoOn = both.reverbOn = true;
            both.echoPreset = 4; both.echoFeedback = dsp::maxEchoFeedback; both.echoSync = true; both.echoDivision = 9; both.echoMix = 0.5f;
            both.reverbPreset = 5; both.reverbSize = 1.0f; both.reverbMix = 0.5f;
            dsp::FxChain fx;
            fx.prepare (rate);
            const auto in = noiseBurst ((int) (4.0 * rate), (int) (0.2 * rate), 5);
            const auto out = runFx (fx, in, in, both, 120.0, rate > 100000.0 ? 1024 : 333);
            check (out.finite() && out.peak() < 1.5f && out.rms ((size_t) (3.0 * rate)) > 1.0e-4, label + "Echo + Reverb finite, bounded, with a tail (peak "
                                                                                                    + fmt (out.peak(), 3) + ")");
        }

        // The processor's tail covers the longest reverb.
        auto p = makeProcessor();
        check (p->getTailLengthSeconds() >= 14.0, "tail length covers the longest reverb (" + fmt (p->getTailLengthSeconds(), 0) + " s)");
        check (p->getLatencySamples() == 0, "FX add no latency");
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
        check (p->getPresetName() == "Random", "undo returns to the name before this Random (the earlier Random)");

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
        { "filter graph handles",                      testFilterHandles },
        { "filter env Sync to Amp",                    testFilterEnvSync },
        { "real mouse: every control clicks",          testRealMouse },
        { "FX: off is bit-identical (bypass)",         testFxBypass },
        { "FX: 20 presets bounded, tails decay",       testFxPresets },
        { "FX: echo ping-pong timing",                 testFxPingPong },
        { "FX: tempo sync and 44.1k..192k",            testFxTempoAndRates },
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
