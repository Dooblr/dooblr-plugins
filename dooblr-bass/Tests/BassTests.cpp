/*
    dooblr-bass console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRBASS_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "Randomiser.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"
#include "UI/Displays.h"
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

namespace
{
    std::atomic<bool> countAllocations { false };
    std::atomic<int> allocationCount { 0 };
}

void* operator new (std::size_t size);
void* operator new[] (std::size_t size);
void operator delete (void* p) noexcept;
void operator delete[] (void* p) noexcept;
void operator delete (void* p, std::size_t) noexcept;
void operator delete[] (void* p, std::size_t) noexcept;

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
    using namespace dooblrbass;
    namespace id = params::id;
    using Processor = DooblrBassProcessor;
    using Editor = DooblrBassEditor;

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

    void openFilter (Processor& p)
    {
        set (p, id::lpfCutoff, 20000.0f);
        set (p, id::lpfReso, 0.0f);
        set (p, id::lpfEnvAmt, 0.0f);
        set (p, id::lpfKeyTrack, 0.0f);
        set (p, id::lfoToPitch, 0.0f);
        set (p, id::lfoToFilter, 0.0f);
        set (p, id::lfoWheel, 0.0f);
        set (p, id::aenvSustain, 1.0f);
        set (p, id::mixOsc2, 0.0f);
        set (p, id::mixOsc3, 0.0f);
        set (p, id::mixSub, 0.0f);
        set (p, id::mixNoise, 0.0f);
        set (p, id::mixOverload, 0.0f);
        set (p, id::mixDrive, 0.0f);
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

    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-bass", "product name");
        check (p->acceptsMidi() && ! p->producesMidi() && p->isMidiEffect() == false, "MIDI instrument");
        check (p->hasEditor(), "has editor");
        check (p->getNumPrograms() >= 9, "Init + 8 factory programs (" + std::to_string (p->getNumPrograms()) + ")");
        check (p->getProgramName (0) == "Init", "first program is Init");
        check (juce::String (JucePlugin_VersionString) == DOOBLRBASS_PROJECT_VERSION, "JUCE version string matches CMake");
        check (p->getParameters().size() > 30, "parameter count (" + std::to_string (p->getParameters().size()) + ")");

        std::set<juce::String> ids;
        for (auto* r : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r))
                ids.insert (ranged->paramID);
        for (auto* expected : { id::osc1Wave, id::osc2Tune, id::mixOverload, id::lpfCutoff, id::lpfReso,
                                id::fenvAttack, id::aenvRelease, id::sharedDecay, id::glide, id::legato,
                                id::notePriority, id::lfoRate, id::lfoSync, id::output })
            check (ids.count (expected) == 1, std::string ("parameter id ") + expected);
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, 48000);
        check (r.finite() && r.peak() < 1.0e-4f, "silence without notes (peak " + fmt (r.peak(), 6) + ")");
    }

    void testNoteBoundedAndReleases()
    {
        auto p = makeProcessor();
        const std::vector<Event> events { { 0, on (36) }, { 24000, off (36) } };
        const auto r = render (*p, 48000, events);
        check (r.finite(), "output is finite");
        check (r.peak() > 0.05f && r.peak() < 4.0f, "audible and bounded (peak " + fmt (r.peak(), 3) + ")");
        check (r.rms (r.left.size() - 2000) < 1.0e-3, "releases to silence");
        check (r.denormals() == 0, "no denormals (" + std::to_string (r.denormals()) + ")");
        check (std::abs (r.left[1000] - r.right[1000]) < 1.0e-6f, "mono-compatible stereo (L = R)");
    }

    void testMaxResonance()
    {
        for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto p = makeProcessor (rate);
            set (*p, id::lpfReso, 1.0f);
            set (*p, id::mixDrive, 0.5f);
            set (*p, id::aenvSustain, 1.0f);
            for (auto cutoff : { 80.0f, 400.0f, 2000.0f, 8000.0f })
            {
                set (*p, id::lpfCutoff, cutoff);
                const auto r = render (*p, (int) (rate * 0.4), { { 0, on (36) } }, 512);
                const auto label = fmt (rate, 0) + " Hz  fc " + fmt (cutoff, 0) + ": ";
                check (r.finite(), label + "finite");
                check (r.peak() < 8.0f, label + "bounded (peak " + fmt (r.peak(), 3) + ")");
                check (r.denormals() == 0, label + "no denormals");
            }
        }
    }

    void testSelfOscillationAtK4()
    {
        check (dsp::model::ladderK (1.0f) > 4.0f, "full resonance k > 4 (" + fmt (dsp::model::ladderK (1.0f), 3) + ")");

        auto p = makeProcessor();
        set (*p, id::mixOsc1, 0.0f);
        set (*p, id::mixOsc2, 0.0f);
        set (*p, id::mixOsc3, 0.0f);
        set (*p, id::mixSub, 0.0f);
        set (*p, id::mixNoise, 0.0f);
        set (*p, id::mixOverload, 0.0f);
        set (*p, id::mixDrive, 0.0f);
        set (*p, id::lpfReso, 1.0f);
        set (*p, id::lpfEnvAmt, 0.0f);
        set (*p, id::lpfKeyTrack, 0.0f);
        set (*p, id::aenvSustain, 1.0f);
        set (*p, id::lpfCutoff, 440.0f);

        const auto r = render (*p, 48000, { { 0, on (60, 1.0f) } });
        check (r.finite() && r.peak() > 0.02f, "self-oscillates from thermal noise (peak " + fmt (r.peak(), 3) + ")");
        const auto hz = measureHz (r.left, defaultRate, 24000);
        check (hz > 200.0 && hz < 900.0, "whistle near cutoff (got " + fmt (hz, 1) + " Hz)");
    }

    void testAnalogModel()
    {
        check (dsp::model::ladderK (1.0f) > 4.0f, "full resonance k > 4");
        check (std::abs (dsp::model::expoHz (69.0f) - 440.0f) < 2.0f, "expo converter at A4");
        check (dsp::model::virtualEarth (0.2f, 0.0f) > 0.1f, "mixer summer passes a small signal");
        check (std::abs (dsp::model::virtualEarth (8.0f, 1.0f)) < 2.0f, "mixer summer saturates a hot signal");
        check (dsp::model::earlyEffect > 0.0f && dsp::model::vcaBleed > 0.0f, "Early bow and VCA bleed are on");
    }

    void testDcGain()
    {
        // Linear core: H(0) = 1 / (1 + k). Drive the ladder with a DC step at 4x the
        // oversampled rate so the TPT one-poles settle, then read the last sample.
        for (auto reso : { 0.0f, 0.25f, 0.5f, 0.75f })
        {
            dsp::LadderFilter f;
            f.prepare();
            f.reset();
            const auto k = dsp::model::ladderK (reso);
            f.setCoefficients (0.02f, k);
            constexpr float in = 0.01f;
            float y = 0.0f;
            for (int i = 0; i < 8000; ++i)
                y = f.process (in);
            const auto gain = y / in;
            const auto expected = 1.0f / (1.0f + k);
            check (std::abs (gain - expected) < 0.08f,
                   "DC gain at reso " + fmt (reso, 2) + ": " + fmt (gain, 3) + " vs 1/(1+k)=" + fmt (expected, 3));
        }
    }

    void testSampleRates()
    {
        for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto p = makeProcessor (rate);
            openFilter (*p);
            const auto n = (int) (rate * 0.35);
            const auto r = render (*p, n, { { 0, on (48) } }, 512);
            const auto hz = measureHz (r.left, rate, (size_t) (rate * 0.05));
            const auto expected = 440.0 * std::exp2 ((48.0 - 12.0 - 69.0) / 12.0); // 16' default, MIDI 48
            check (r.finite() && r.peak() > 0.05f, fmt (rate, 0) + " Hz: audible and finite");
            check (std::abs (hz - expected) / expected < 0.03, fmt (rate, 0) + " Hz: pitch " + fmt (hz, 2)
                   + " vs " + fmt (expected, 2));
        }
    }

    void testStateRoundTrip()
    {
        auto a = makeProcessor();
        set (*a, id::lpfCutoff, 777.0f);
        set (*a, id::osc2Tune, 3.5f);
        set (*a, id::mixOverload, 0.4f);
        set (*a, id::sharedDecay, 1.0f);
        a->setPresetName ("RoundTrip");

        juce::MemoryBlock blob;
        a->getStateInformation (blob);

        auto b = makeProcessor();
        b->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*b, id::lpfCutoff) - 777.0f) < 0.5f, "cutoff restored");
        check (std::abs (get (*b, id::osc2Tune) - 3.5f) < 0.01f, "tune restored");
        check (get (*b, id::sharedDecay) >= 0.5f, "shared decay restored");
        check (b->getPresetName() == "RoundTrip", "preset name restored");
        check ((int) b->getState().state.getProperty ("stateVersion") == params::stateVersion, "stateVersion written");
    }

    void testPresets()
    {
        auto p = makeProcessor();
        const auto& factory = presets::factory();
        check (factory.size() >= 9, "at least Init + 8 factory patches");
        check (factory.front().name == "Init", "Init is first");

        for (int i = 0; i < (int) factory.size(); ++i)
        {
            p->setCurrentProgram (i);
            const auto r = render (*p, 24000, { { 0, on (36) }, { 16000, off (36) } });
            check (r.finite() && r.peak() > 0.02f && r.peak() < 6.0f,
                   factory[(size_t) i].name.toStdString() + ": audible, finite, bounded (peak " + fmt (r.peak(), 3) + ")");
        }

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("dooblr-bass-tests");
        dir.createDirectory();
        const auto file = dir.getChildFile ("UserPatch.dbass");
        file.deleteFile();
        set (*p, id::lpfCutoff, 1234.0f);
        check (p->saveUserPreset (file).startsWith ("Saved"), "save user preset");
        set (*p, id::lpfCutoff, 200.0f);
        check (p->loadUserPreset (file).startsWith ("Loaded"), "load user preset");
        check (std::abs (get (*p, id::lpfCutoff) - 1234.0f) < 1.0f, "user preset restores cutoff");
        file.deleteFile();
        dir.deleteRecursively();
    }

    void testNoDeadParameters()
    {
        const std::map<juce::String, std::vector<std::pair<const char*, float>>> contexts {
            { id::osc1PW, { { id::osc1Wave, 1.0f } } },
            { id::osc2PW, { { id::osc2Wave, 1.0f }, { id::mixOsc2, 0.8f } } },
            { id::osc2Tune, { { id::mixOsc2, 0.8f } } },
            { id::osc2Wave, { { id::mixOsc2, 0.8f } } },
            { id::osc2Octave, { { id::mixOsc2, 0.8f } } },
            { id::osc3Wave, { { id::mixOsc3, 0.8f } } },
            { id::osc3Octave, { { id::mixOsc3, 0.8f } } },
            { id::osc3Tune, { { id::mixOsc3, 0.8f } } },
            { id::osc3LoFreq, { { id::mixOsc3, 0.8f } } },
            { id::osc3KeyTrack, { { id::mixOsc3, 0.8f } } },
            { id::subOctave, { { id::mixSub, 0.8f } } },
            { id::mixOsc3, { { id::mixOsc3, 0.0f } } },
            { id::lfoDivision, { { id::lfoSync, 1.0f }, { id::lfoToFilter, 0.6f } } },
            { id::lfoRate, { { id::lfoToFilter, 0.6f } } },
            { id::lfoWave, { { id::lfoToFilter, 0.6f } } },
            { id::lfoToPitch, { { id::lfoToPitch, 0.0f } } },
            { id::lfoToFilter, { { id::lpfReso, 0.4f } } },
            { id::lfoWheel, { { id::lfoToPitch, 0.0f } } },
            { id::lfoSync, { { id::lfoToFilter, 0.6f } } },
            { id::glide, { { id::legato, 1.0f } } },
            { id::sharedDecay, { { id::aenvSustain, 1.0f }, { id::aenvDecay, 0.05f }, { id::aenvRelease, 2.0f } } },
            { id::notePriority, { { id::legato, 0.0f } } },
            { id::mixNoise, {} },
        };

        const std::vector<Event> events {
            { 0, juce::MidiMessage::controllerEvent (1, 1, 100) },
            { 0, on (36, 0.9f) }, { 0, on (43, 0.6f) },
            { 4800, juce::MidiMessage::pitchWheel (1, 12288) },
            { 7200, on (48, 0.8f) },
            { 21600, off (36) }, { 21600, off (43) }, { 21600, off (48) } };

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
            check (a.left == b.left && a.right == b.right, "renders are deterministic");
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
            check (live, "parameter " + pid.toStdString() + " changes the sound (rel diff "
                   + std::to_string (level > 0 ? diff / level : 0.0) + ")");
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
        cutoff->setValueNotifyingHost (0.9f);
        check (! history.canUndo(), "automation (no gesture) is not an undo step");

        history.clear();
        auto* reso = param (*p, id::lpfReso);
        for (int i = 0; i < 60; ++i)
        {
            auto* target = (i % 2 == 0) ? reso : cutoff;
            target->beginChangeGesture();
            target->setValueNotifyingHost ((float) (i % 10) / 10.0f + 0.05f);
            target->endChangeGesture();
        }
        int steps = 0;
        while (history.canUndo() && steps < 100) { history.undo(); ++steps; }
        check (steps == 50, "undo keeps 50 steps (got " + std::to_string (steps) + ")");
    }

    void testNotePriorityAndLegato()
    {
        auto last = makeProcessor();
        set (*last, id::notePriority, 0.0f);
        set (*last, id::legato, 1.0f);
        set (*last, id::glide, 0.0f);
        last->getEngine().setSettings (last->readSettings());
        last->getEngine().noteOn (48, 0.8f);
        last->getEngine().noteOn (36, 0.8f);
        check (last->getEngine().getCurrentNote() == 36, "last priority: most recent note (36 after 48)");
        last->getEngine().noteOff (36);
        check (last->getEngine().getCurrentNote() == 48, "last priority: falls back to 48");

        auto low = makeProcessor();
        set (*low, id::notePriority, 1.0f);
        set (*low, id::legato, 1.0f);
        low->getEngine().setSettings (low->readSettings());
        low->getEngine().noteOn (48, 0.8f);
        low->getEngine().noteOn (60, 0.8f);
        check (low->getEngine().getCurrentNote() == 48, "low priority: stays on 48 when 60 is added");
        low->getEngine().noteOn (36, 0.8f);
        check (low->getEngine().getCurrentNote() == 36, "low priority: jumps to 36");

        auto retrig = makeProcessor();
        set (*retrig, id::legato, 0.0f);
        openFilter (*retrig);
        set (*retrig, id::aenvAttack, 0.2f);
        const auto r = render (*retrig, 24000, { { 0, on (36) }, { 4000, on (48) } });
        check (r.finite() && r.peak() > 0.02f, "retrigger overlapping notes is finite and audible");
    }

    void testSharedDecay()
    {
        auto p = makeProcessor();
        set (*p, id::sharedDecay, 1.0f);
        set (*p, id::aenvAttack, 0.001f);
        set (*p, id::aenvDecay, 0.08f);
        set (*p, id::aenvSustain, 1.0f);
        set (*p, id::aenvRelease, 2.0f); // ignored while shared
        openFilter (*p);
        const auto r = render (*p, 24000, { { 0, on (48) }, { 8000, off (48) } });
        check (r.rms (r.left.size() - 2000) < 0.02, "shared decay: long Release is ignored, note dies with Decay");
    }

    void testAliasingSanity()
    {
        auto p = makeProcessor (44100.0, 512);
        openFilter (*p);
        set (*p, id::osc1Octave, 3.0f); // 4'
        set (*p, id::osc1Wave, 0.0f);
        const auto r = render (*p, 44100, { { 0, on (84) } }, 512); // C6 at 4' is very high; use C5 = 72
        // Re-run at a still-audible high bass/tenor note: MIDI 60 at 4' = 523 Hz.
        auto q = makeProcessor (44100.0, 512);
        openFilter (*q);
        set (*q, id::osc1Octave, 3.0f);
        const auto s = render (*q, 44100, { { 0, on (60) } }, 512);

        // DFT bins: energy above 12 kHz vs the fundamental neighbourhood.
        auto bandEnergy = [] (const std::vector<float>& x, double rate, double lo, double hi)
        {
            const auto n = std::min<size_t> (4096, x.size() / 2);
            const auto start = x.size() / 4;
            double e = 0.0;
            for (int k = 1; k < (int) n / 2; ++k)
            {
                const auto hz = (double) k * rate / (double) n;
                if (hz < lo || hz > hi)
                    continue;
                std::complex<double> acc (0.0, 0.0);
                for (size_t i = 0; i < n; ++i)
                {
                    const auto ang = -2.0 * 3.14159265358979 * (double) k * (double) i / (double) n;
                    acc += (double) x[start + i] * std::complex<double> (std::cos (ang), std::sin (ang));
                }
                e += std::norm (acc);
            }
            return e;
        };

        const auto fund = bandEnergy (s.left, 44100.0, 400.0, 800.0);
        const auto high = bandEnergy (s.left, 44100.0, 12000.0, 20000.0);
        check (fund > 0.0, "fundamental band has energy");
        check (high < fund * 0.15, "HF alias energy is well below the fundamental (ratio "
               + fmt (fund > 0 ? high / fund : 0.0, 4) + ")");
        juce::ignoreUnused (r);
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (on (36), 0);
        p->processBlock (buffer, midi); // warm up

        allocationCount = 0;
        countAllocations = true;
        midi.clear();
        for (int i = 0; i < 64; ++i)
        {
            buffer.clear();
            if (i == 8)
                midi.addEvent (on (40), 10);
            else if (i == 16)
                midi.addEvent (off (36), 0);
            else
                midi.clear();
            p->processBlock (buffer, midi);
            midi.clear();
        }
        countAllocations = false;
        check (allocationCount.load() == 0, "processBlock allocates nothing after prepare ("
               + std::to_string (allocationCount.load()) + " news)");

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        midi.clear();
        midi.addEvent (on (36), 0);
        buffer.clear();
        p->processBlock (buffer, midi);
        midi.clear();
        for (int i = 0; i < 400; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto audioMs = 401.0 * 512.0 / defaultRate * 1000.0;
        note ("CPU: " + fmt (ms, 1) + " ms wall for " + fmt (audioMs, 1) + " ms audio ("
              + fmt (100.0 * ms / audioMs, 2) + " %)");
        check (ms < audioMs, "faster than real time");
    }

    void snapshot (juce::Component& c, const char* suffix)
    {
        const auto prefix = std::getenv ("DOOBLRBASS_EDITOR_SNAPSHOT");
        if (prefix == nullptr)
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
        check (editor != nullptr, "the editor is a DooblrBassEditor");
        if (editor == nullptr)
            return;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080 x 660");
        auto* constrainer = editor->getConstrainer();
        check (constrainer != nullptr && constrainer->getMinimumWidth() == 940 && constrainer->getMinimumHeight() == 600
                   && constrainer->getMaximumWidth() == 2400 && constrainer->getMaximumHeight() == 1400,
               "resize limits 940 x 600 .. 2400 x 1400");

        auto* version = editor->findChildWithID ("version");
        auto* versionLabel = dynamic_cast<juce::Label*> (version);
        check (versionLabel != nullptr && versionLabel->getText() == "v" JucePlugin_VersionString,
               "version label shows v" JucePlugin_VersionString);

        std::map<juce::String, int> controls;
        for (auto& panel : editor->getPanels())
            for (auto* r : p->getParameters())
            {
                const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
                if (panel->findControl (pid) != nullptr)
                    ++controls[pid];
            }
        for (auto& t : editor->getModeToggles())
            ++controls[t->getComponentID()];
        for (auto& b : editor->getSwitchButtons())
            ++controls[b->getComponentID()];
        ++controls[id::output];
        check (editor->findChildWithID (id::output) != nullptr, "header Output slider exists");
        check (editor->findChildWithID ("page_Oscillators") == nullptr, "no page-column buttons");

        int missing = 0;
        for (auto* r : p->getParameters())
        {
            const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
            if (controls[pid] != 1)
            {
                ++missing;
                check (false, "parameter " + pid.toStdString() + " has one control (has " + std::to_string (controls[pid]) + ")");
            }
        }
        check (missing == 0, "every parameter has exactly one control");
        check (controls.size() == (size_t) p->getParameters().size(), "no control without a parameter");

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto sizeLabel = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";

            std::vector<juce::Component*> header;
            for (auto* cid : { "undo", "redo", "preset", "random", "savePreset", "loadPreset", id::output, "version" })
                if (auto* c = editor->findChildWithID (cid))
                    header.push_back (c);
            check (header.size() == 8, sizeLabel + "all header items present");
            check (editor->findChildWithID ("preset")->getWidth() >= 140, sizeLabel + "preset box at least 140 px");
            for (size_t i = 0; i < header.size(); ++i)
            {
                check (editor->getLocalBounds().contains (header[i]->getBounds()) && header[i]->getBottom() <= 60,
                       sizeLabel + "header item inside the header: " + header[i]->getComponentID().toStdString());
                for (size_t j = i + 1; j < header.size(); ++j)
                    check (! header[i]->getBounds().intersects (header[j]->getBounds()),
                           sizeLabel + "header items do not overlap: " + header[i]->getComponentID().toStdString()
                               + " / " + header[j]->getComponentID().toStdString());
            }

            const auto content = editor->getContentArea().expanded (1, 1);
            for (auto* graph : { (juce::Component*) &editor->getOscGraph(), (juce::Component*) &editor->getFilterGraph(),
                                 (juce::Component*) &editor->getEnvGraph(), (juce::Component*) &editor->getLfoGraph() })
            {
                check (graph->getHeight() >= 56, sizeLabel + graph->getComponentID().toStdString() + " graph at least 56 px ("
                       + std::to_string (graph->getHeight()) + ")");
                check (content.contains (graph->getBounds()), sizeLabel + graph->getComponentID().toStdString() + " in content");
                check (graph->isVisible(), sizeLabel + graph->getComponentID().toStdString() + " visible");
            }
            for (auto& panel : editor->getPanels())
            {
                check (panel->isVisible(), sizeLabel + "panel '" + panel->getTitle().toStdString() + "' visible");
                check (content.contains (panel->getBounds()) || panel->getBounds().getHeight() == 0,
                       sizeLabel + "panel '" + panel->getTitle().toStdString() + "' in content");
            }
            check ((int) editor->getPanels().size() == 9, sizeLabel + "nine control panels on the board");

            for (size_t i = 0; i < editor->getPanels().size(); ++i)
                for (size_t j = i + 1; j < editor->getPanels().size(); ++j)
                    check (! editor->getPanels()[i]->getBounds().intersects (editor->getPanels()[j]->getBounds()),
                           sizeLabel + "panels do not overlap: " + editor->getPanels()[i]->getTitle().toStdString()
                               + " / " + editor->getPanels()[j]->getTitle().toStdString());
        }

        snapshot (*editor, "-default");
        snapshot (*editor, "-board");
    }

    void testRandomPatch()
    {
        auto p = makeProcessor();
        juce::Random random (42);
        const auto changed = p->randomisePatch (random);
        check (changed > 10, "random changes many parameters (" + std::to_string (changed) + ")");
        check (p->getPresetName() == "Random", "preset name is Random");
        check (std::abs (get (*p, id::output) - 0.0f) < 1.0e-4f, "random leaves Output alone");
        const auto r = render (*p, 12000, { { 0, on (36) } });
        check (r.finite() && r.peak() > 0.01f && r.peak() < 8.0f, "random patch is playable (peak " + fmt (r.peak(), 3) + ")");
    }

    void testFilterHandle()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        if (editor == nullptr)
            return;
        editor->setSize (1080, 660);
        auto& graph = editor->getFilterGraph();
        check (graph.isVisible(), "filter graph is on the board");

        const auto plot = graph.getPlotArea();
        const auto atDefault = graph.getHandlePosition();
        check (plot.expanded (2.0f, 2.0f).contains (atDefault), "handle sits in the plot");

        set (*p, id::lpfCutoff, 2000.0f);
        set (*p, id::lpfReso, 0.8f);
        graph.refresh();
        const auto moved = graph.getHandlePosition();
        check (moved.x > atDefault.x + 8.0f, "higher cutoff moves the handle right");
        check (moved.y < atDefault.y - 4.0f, "higher resonance moves the handle up");
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
        { "max resonance: no NaN / denormals",         testMaxResonance },
        { "self-oscillation at k = 4",                 testSelfOscillationAtK4 },
        { "analog circuit model",                      testAnalogModel },
        { "DC gain 1/(1+k)",                           testDcGain },
        { "44.1k..192k pitch",                         testSampleRates },
        { "state round trip",                          testStateRoundTrip },
        { "factory and user presets",                  testPresets },
        { "no dead parameters",                        testNoDeadParameters },
        { "undo history",                              testUndoHistory },
        { "note priority and legato",                  testNotePriorityAndLegato },
        { "shared decay",                              testSharedDecay },
        { "oscillator aliasing sanity",                testAliasingSanity },
        { "real-time safety and CPU",                  testRealtimeAndCpu },
        { "random patch",                              testRandomPatch },
        { "editor",                                    testEditor },
        { "filter graph handle",                       testFilterHandle },
    };

    int failedTests = 0;
    for (const auto& t : tests)
    {
        const auto beforeFail = failedChecks, beforePass = passedChecks;
        std::cout << t.name << "\n";
        t.run();
        const auto failed = failedChecks > beforeFail;
        std::cout << "  [" << (failed ? "FAIL" : "PASS") << "] "
                  << (passedChecks - beforePass) << " checks";
        if (failed)
            std::cout << ", " << (failedChecks - beforeFail) << " failed";
        std::cout << "\n";
        failedTests += failed ? 1 : 0;
    }

    std::cout << "\n" << passedChecks << " passed, " << failedChecks << " failed, "
              << tests.size() << " tests (" << failedTests << " failed)\n";
    return failedChecks == 0 ? 0 : 1;
}
