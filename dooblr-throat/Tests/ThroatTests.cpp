/*
    dooblr-throat console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRTHROAT_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"
#include "DSP/ThroatEngine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace
{
    using namespace dooblrthroat;
    namespace id = params::id;
    using Processor = DooblrThroatProcessor;
    using Editor = DooblrThroatEditor;

    int failedChecks = 0, passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition) { ++passedChecks; return; }
        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

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
        p.applyBodyOnMessageThread();
    }

    float get (Processor& p, const juce::String& pid)
    {
        auto* r = param (p, pid);
        return r == nullptr ? 0.0f : r->convertFrom0to1 (r->getValue());
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
        double energy() const
        {
            double s = 0.0;
            for (auto v : left) s += (double) v * v;
            return s;
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

    double midiHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

    double measureHz (const std::vector<float>& x, double rate, size_t from)
    {
        if (x.size() <= from + 16)
            return 0.0;

        const size_t n = x.size() - from;
        const int minLag = std::max (8, (int) std::floor (rate / 800.0));
        const int maxLag = std::min ((int) n / 2, (int) std::ceil (rate / 60.0));
        if (maxLag <= minLag)
            return 0.0;

        double best = -1.0e300;
        int bestLag = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (size_t i = from; i + (size_t) lag < x.size(); ++i)
                acc += (double) x[i] * (double) x[i + (size_t) lag];
            if (acc > best)
            {
                best = acc;
                bestLag = lag;
            }
        }
        return rate / (double) bestLag;
    }

    void writeSnapshot (Editor& editor, const juce::String& path)
    {
        juce::Image image (juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
        juce::Graphics g (image);
        editor.paintEntireComponent (g, true);
        juce::File file (path);
        file.getParentDirectory().createDirectory();
        juce::FileOutputStream stream (file);
        if (stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, stream);
        }
    }

    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-throat", "product name");
        check (p->acceptsMidi() && ! p->producesMidi() && p->isMidiEffect() == false, "MIDI instrument");
        check (p->hasEditor(), "has editor");
        check (p->getNumPrograms() == 8, "Init + 7 factory programs (" + std::to_string (p->getNumPrograms()) + ")");
        check (p->getProgramName (0) == "Init", "first program is Init");
        check (juce::String (JucePlugin_VersionString) == DOOBLRTHROAT_PROJECT_VERSION, "JUCE version string matches CMake");
        check (p->getParameters().size() == 22, "parameter count (" + std::to_string (p->getParameters().size()) + ")");

        for (auto* expected : { id::vowel, id::height, id::back, id::nasal, id::female, id::child,
                                id::breath, id::tension, id::brightness, id::fry, id::roughness, id::pressure, id::voiced,
                                id::vibratoDepth, id::vibratoRate, id::glide, id::bendRange,
                                id::attack, id::decay, id::sustain, id::release, id::output })
            check (p->getState().getParameter (expected) != nullptr, std::string ("parameter id ") + expected);
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, (int) defaultRate / 4);
        check (r.finite() && r.peak() < 1.0e-4f, "silence without notes (peak " + fmt (r.peak(), 6) + ")");
        check (r.denormals() == 0, "no denormals when idle");
    }

    void testNoteSoundsAndPitch()
    {
        auto p = makeProcessor();
        set (*p, id::vibratoDepth, 0.0f);
        set (*p, id::glide, 0.001f);
        set (*p, id::vowel, 1.0f); // heed, a clear voiced vowel

        const auto r = render (*p, (int) defaultRate, { { 0, on (60) } });
        check (r.finite(), "output is finite");
        check (r.peak() > 0.02f && r.peak() <= 1.0f, "audible and bounded (peak " + fmt (r.peak(), 3) + ")");
        check (r.energy() > 1.0e-4, "non-silent energy (" + fmt (r.energy(), 4) + ")");
        check (r.denormals() == 0, "no denormals (" + std::to_string (r.denormals()) + ")");
        check (std::abs (r.left[8000] - r.right[8000]) < 1.0e-6f, "mono-compatible stereo (L = R)");

        const auto hz = measureHz (r.left, defaultRate, (size_t) (defaultRate * 0.25));
        const auto expected = midiHz (60);
        check (std::abs (hz - expected) < 8.0, "pitch of note 60 is " + fmt (hz, 1) + " Hz (expected " + fmt (expected, 1) + ")");
    }

    void testHigherNotePitch()
    {
        auto p = makeProcessor();
        set (*p, id::vibratoDepth, 0.0f);
        set (*p, id::glide, 0.001f);
        set (*p, id::vowel, 1.0f);
        const auto r = render (*p, (int) defaultRate, { { 0, on (69) } });
        const auto hz = measureHz (r.left, defaultRate, (size_t) (defaultRate * 0.25));
        check (r.finite() && r.peak() > 0.02f, "note 69 sounds (peak " + fmt (r.peak(), 3) + ")");
        check (std::abs (hz - 440.0) < 10.0, "pitch of note 69 is " + fmt (hz, 1) + " Hz (expected 440)");
    }

    void testReleaseToSilence()
    {
        auto p = makeProcessor();
        set (*p, id::release, 0.08f);
        const auto rate = (int) defaultRate;
        const auto r = render (*p, rate, { { 0, on (60) }, { rate / 2, off (60) } });
        check (r.finite(), "release is finite");
        check (r.peak() > 0.02f, "note was audible");
        check (r.rms (r.left.size() - 2000) < 2.0e-3, "releases to silence (rms " + fmt (r.rms (r.left.size() - 2000), 6) + ")");
        check (r.denormals() == 0, "no denormals on release");
    }

    void testPresets()
    {
        auto p = makeProcessor();
        check (get (*p, id::vowel) == 14.0f, "Init default vowel is sofa");
        p->setCurrentProgram (1); // Close Front
        check (p->getPresetName() == "Close Front", "preset name after program change");
        check (get (*p, id::vowel) == 1.0f, "Close Front vowel is heed");
        p->setCurrentProgram (0);
        check (get (*p, id::vowel) == 14.0f, "Init restores sofa");

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("dooblr-throat-tests");
        dir.createDirectory();
        const auto file = dir.getChildFile ("roundtrip.dthroat");
        p->setCurrentProgram (5); // Choir Lead
        const auto savedFemale = get (*p, id::female);
        check (p->saveUserPreset (file).startsWith ("Saved"), "save user preset");
        p->setCurrentProgram (0);
        check (get (*p, id::female) == 0.0f, "Init female is 0");
        const auto loaded = p->loadUserPreset (file);
        check (loaded.startsWith ("Loaded"), "load user preset: " + loaded.toStdString());
        check (std::abs (get (*p, id::female) - savedFemale) < 1.0e-5f, "loaded female matches saved");
        file.deleteFile();
    }

    void testEditor()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor type");
        if (editor == nullptr)
            return;

        check (editor->getWidth() >= 940 && editor->getHeight() >= 600, "minimum editor size");
        check (editor->getPanels().size() == 5, "five control panels");
        check (editor->getVowelPad().isVisible(), "vowel pad is visible");
        if (auto* status = dynamic_cast<juce::Label*> (editor->findChildWithID ("status")))
        {
            const auto text = status->getText();
            check (! text.contains (juce::CharPointer_UTF8 ("\xc3\x82")), "status line has no mojibake (" + text.toStdString() + ")");
            const auto idle = juce::String::fromUTF8 ("Idle  \xc2\xb7  MIDI instrument");
            check (text == idle, "status uses a UTF-8 middle dot (" + text.toStdString() + ")");
        }
        else
            check (false, "status label is present");

        const auto snapshotEnv = std::getenv ("DOOBLRTHROAT_EDITOR_SNAPSHOT");
        const juce::String path = snapshotEnv != nullptr ? juce::String (snapshotEnv) + ".png"
                                                         : juce::String (p->getName() + "-editor.png");
        if (snapshotEnv != nullptr || true)
        {
            const auto dest = snapshotEnv != nullptr
                ? juce::String (snapshotEnv) + ".png"
                : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                      .getChildFile ("dooblr-build/dooblr-throat/dooblr-throat.png").getFullPathName();
            writeSnapshot (*editor, dest);
            check (juce::File (dest).existsAsFile() && juce::File (dest).getSize() > 1000,
                   "editor snapshot written to " + dest.toStdString());
        }
    }

    void testNoChimeraName()
    {
        auto p = makeProcessor();
        check (! p->getName().containsIgnoreCase ("chimera"), "product name has no third-party brand");
        for (int i = 0; i < p->getNumPrograms(); ++i)
            check (! p->getProgramName (i).containsIgnoreCase ("chimera"),
                   "preset name is original: " + p->getProgramName (i).toStdString());
    }

    int stressSeconds()
    {
        if (const char* env = std::getenv ("DOOBLRTHROAT_STRESS_SECONDS"))
        {
            const int s = std::atoi (env);
            if (s > 0) return s;
        }
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
        return 60;
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
        return 60;
#endif
        return 120;
    }

    uint32_t nextU32 (uint32_t& rng)
    {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    }

    float unit (uint32_t& rng) { return float (nextU32 (rng) >> 8) / float (1u << 24); }

    void testParamExtremesAndMidi()
    {
        auto p = makeProcessor (44100.0, 64);
        const auto ids = { id::height, id::back, id::nasal, id::female, id::child,
                           id::breath, id::tension, id::brightness, id::fry, id::roughness, id::pressure, id::voiced,
                           id::vibratoDepth, id::vibratoRate, id::glide, id::bendRange,
                           id::attack, id::decay, id::sustain, id::release, id::output };
        for (auto* pid : ids)
        {
            if (auto* r = param (*p, pid))
                r->setValueNotifyingHost (0.0f);
        }
        p->applyBodyOnMessageThread();
        auto lo = render (*p, 2048, { { 0, on (0) }, { 512, juce::MidiMessage::pitchWheel (1, 0) } }, 64);
        check (lo.finite(), "all-min params stay finite (note 0, bend min)");

        for (auto* pid : ids)
        {
            if (auto* r = param (*p, pid))
                r->setValueNotifyingHost (1.0f);
        }
        p->applyBodyOnMessageThread();
        auto hi = render (*p, 2048, { { 0, on (127) }, { 256, juce::MidiMessage::pitchWheel (1, 16383) } }, 64);
        check (hi.finite(), "all-max params stay finite (note 127, bend max)");

        // Rapid note-on/off, legato, then release/reset.
        std::vector<Event> ev;
        int t = 0;
        for (int i = 0; i < 40; ++i)
        {
            ev.push_back ({ t, on (40 + (i % 24), 0.9f) });
            t += 32;
            if (i % 3 == 0)
            {
                ev.push_back ({ t, off (40 + (i % 24)) });
                t += 16;
            }
        }
        ev.push_back ({ t, off (40) });
        auto leg = render (*p, t + 4096, ev, 128);
        check (leg.finite(), "rapid legato / note-off stays finite");
        p->setCurrentProgram (0);
        p->releaseResources();
        p->prepareToPlay (44100.0, 64);
        const auto after = render (*p, 2048, {}, 64);
        check (after.finite() && after.peak() < 1.0e-4f,
               "silent after reset/releaseResources (peak " + fmt (after.peak(), 6) + ")");
    }

    void testSampleRatesAndBlockSizes()
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto p = makeProcessor (rate, 256);
            set (*p, id::vowel, 1.0f);
            const auto r = render (*p, int (rate * 0.25), { { 0, on (60) } }, 256);
            check (r.finite() && r.peak() > 0.01f,
                   "note 60 sounds at " + fmt (rate, 0) + " Hz (peak " + fmt (r.peak(), 3) + ")");
        }

        auto p = makeProcessor (48000.0, 512);
        set (*p, id::vowel, 1.0f);
        auto a = render (*p, 4096, { { 0, on (69) } }, 512);
        check (a.finite() && a.peak() > 0.01f, "pre rate-change audio is finite");
        p->releaseResources();
        p->setPlayConfigDetails (0, 2, 44100.0, 1024);
        p->prepareToPlay (44100.0, 1024);
        auto b = render (*p, 4096, { { 0, on (69) } }, 1024);
        check (b.finite() && b.peak() > 0.01f, "audio is finite after mid-stream 48k->44.1k prepare");

        // Variable block sizes from 1 to 4096, including larger than the prepared max (512).
        p->releaseResources();
        p->setPlayConfigDetails (0, 2, 48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        set (*p, id::vowel, 1.0f);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        uint32_t rng = 7;
        bool finite = true;
        float peak = 0.0f;
        int produced = 0;
        while (produced < 48000)
        {
            const int n = 1 + int (nextU32 (rng) % 4096);
            juce::AudioBuffer<float> buffer (2, n);
            buffer.clear();
            p->processBlock (buffer, midi);
            midi.clear();
            for (int i = 0; i < n; ++i)
            {
                const auto l = buffer.getSample (0, i), r = buffer.getSample (1, i);
                if (! std::isfinite (l) || ! std::isfinite (r)) finite = false;
                peak = std::max ({ peak, std::abs (l), std::abs (r) });
            }
            produced += n;
        }
        check (finite && peak > 0.01f, "variable 1..4096 blocks (incl. > prepared max) stay finite, peak " + fmt (peak, 3));
    }

    void testStressAutomationAndBodySwap()
    {
        const int seconds = stressSeconds();
        std::cout << "    (stress duration " << seconds << " s of simulated audio)\n";

        // Processor-level: randomised automation, MIDI, and block sizes, with a
        // second thread repeatedly toggling Female/Child and rebuilding the airway.
        auto p = makeProcessor (48000.0, 256);
        const auto pids = { id::height, id::back, id::nasal, id::female, id::child,
                            id::breath, id::tension, id::brightness, id::fry, id::roughness, id::pressure, id::voiced,
                            id::vibratoDepth, id::vibratoRate, id::glide, id::bendRange,
                            id::attack, id::decay, id::sustain, id::release, id::output };

        std::atomic<bool> running { true };
        std::atomic<int> faults { 0 };
        std::atomic<uint64_t> samples { 0 };

        std::thread body ([&] {
            uint32_t rng = 99;
            bool female = false, child = false;
            while (running.load (std::memory_order_relaxed))
            {
                female = ! female;
                if (! female) child = ! child;
                if (auto* f = p->getState().getParameter (id::female))
                    f->setValueNotifyingHost (female ? 1.0f : 0.0f);
                if (auto* c = p->getState().getParameter (id::child))
                    c->setValueNotifyingHost (child ? 1.0f : 0.0f);
                p->applyBodyOnMessageThread();
                std::this_thread::sleep_for (std::chrono::microseconds (200 + int (nextU32 (rng) % 800)));
            }
        });

        uint32_t rng = 1;
        juce::MidiBuffer midi;
        int note = 60;
        const auto target = uint64_t (48000) * uint64_t (seconds);
        while (samples.load (std::memory_order_relaxed) < target)
        {
            const int n = 1 + int (nextU32 (rng) % 4096);
            juce::AudioBuffer<float> buffer (2, n);
            buffer.clear();
            midi.clear();
            if ((nextU32 (rng) & 7u) == 0)
            {
                midi.addEvent (off (note), 0);
                note = int (nextU32 (rng) % 128);
                midi.addEvent (on (note, 0.3f + 0.7f * unit (rng)), 0);
            }
            if ((nextU32 (rng) & 15u) == 0)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, int (nextU32 (rng) % 16384)), 0);
            if ((nextU32 (rng) & 3u) == 0)
            {
                auto* pid = pids.begin()[int (nextU32 (rng) % pids.size())];
                if (auto* r = p->getState().getParameter (pid))
                    r->setValueNotifyingHost (unit (rng));
            }
            p->processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (buffer.getSample (0, i)) || ! std::isfinite (buffer.getSample (1, i)))
                    faults.fetch_add (1, std::memory_order_relaxed);
            samples.fetch_add (uint64_t (n), std::memory_order_relaxed);
        }

        running = false;
        body.join();
        check (faults.load() == 0, "processor stress stayed finite (" + std::to_string (faults.load()) + " non-finite, "
               + std::to_string (samples.load()) + " samples)");

        // Engine-level race: the exact 0.1.0 crash class — rebuild the airway on a
        // side thread while process() is running.
        dsp::ThroatEngine engine;
        dsp::ThroatSettings s;
        engine.setSettings (s);
        engine.prepare (48000.0, 512);
        engine.noteOn (60, 0.8f);
        running = true;
        faults = 0;
        std::atomic<uint64_t> blocks { 0 };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (std::max (8, seconds / 4));

        std::thread audio ([&] {
            std::vector<float> L (4096), R (4096);
            uint32_t r = 123;
            while (running.load (std::memory_order_relaxed)
                   && std::chrono::steady_clock::now() < deadline)
            {
                const int n = 1 + int (nextU32 (r) % 4096);
                dsp::AudioCallbackScope scope (engine);
                if ((nextU32 (r) & 7u) == 0)
                {
                    engine.noteOff (60);
                    engine.noteOn (int (nextU32 (r) % 128), 0.9f);
                }
                if ((nextU32 (r) & 15u) == 0)
                    engine.pitchBend (float (int (nextU32 (r) % 49) - 24));
                engine.setSettings (s);
                engine.process (L.data(), R.data(), n);
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (L[size_t (i)]) || ! std::isfinite (R[size_t (i)]))
                        faults.fetch_add (1, std::memory_order_relaxed);
                blocks.fetch_add (1, std::memory_order_relaxed);
            }
        });

        std::thread swapper ([&] {
            bool female = false, child = false;
            while (running.load (std::memory_order_relaxed)
                   && std::chrono::steady_clock::now() < deadline)
            {
                female = ! female;
                if (! female) child = ! child;
                dsp::ThroatSettings body = s;
                body.body.female = female ? 1.0f : 0.0f;
                body.body.child = child ? 1.0f : 0.0f;
                engine.rebuildVoiceOffAudioThread (body);
                std::this_thread::sleep_for (std::chrono::microseconds (150));
            }
        });

        audio.join();
        running = false;
        swapper.join();
        check (faults.load() == 0 && blocks.load() > 0,
               "engine body-swap stress stayed finite (" + std::to_string (faults.load())
               + " non-finite, " + std::to_string (blocks.load()) + " blocks)");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const struct { const char* name; void (*run)(); } tests[] {
        { "metadata",                    testMetadata },
        { "silence without notes",       testSilence },
        { "note sounds and pitch",       testNoteSoundsAndPitch },
        { "higher note pitch",           testHigherNotePitch },
        { "release to silence",          testReleaseToSilence },
        { "factory and user presets",    testPresets },
        { "no third-party brand names",  testNoChimeraName },
        { "editor",                      testEditor },
        { "param extremes and MIDI",     testParamExtremesAndMidi },
        { "sample rates and block sizes", testSampleRatesAndBlockSizes },
        { "stress automation and body swap", testStressAutomationAndBodySwap },
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
              << std::size (tests) << " tests (" << failedTests << " failed)\n";
    return failedChecks == 0 ? 0 : 1;
}
