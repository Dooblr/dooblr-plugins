/*
    dooblr-throat console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRTHROAT_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "DSP/Phonetics/Phrase.h"
#include "DSP/Phonetics/Coarticulation.h"
#include "DSP/Articulation.h"
#include "DSP/HumanVoice.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "Presets.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"
#include "DSP/ThroatEngine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
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
        check (p->getNumPrograms() == 10, "Init + 9 factory programs (" + std::to_string (p->getNumPrograms()) + ")");
        check (p->getProgramName (0) == "Init", "first program is Init");
        check (juce::String (JucePlugin_VersionString) == DOOBLRTHROAT_PROJECT_VERSION, "JUCE version string matches CMake");
        check (p->getParameters().size() == 27, "parameter count (" + std::to_string (p->getParameters().size()) + ")");

        for (auto* expected : { id::vowel, id::height, id::back, id::nasal, id::female, id::child,
                                id::breath, id::tension, id::brightness, id::fry, id::roughness, id::pressure, id::voiced,
                                id::vibratoDepth, id::vibratoRate, id::glide, id::bendRange,
                                id::attack, id::decay, id::sustain, id::release, id::output,
                                id::mode, id::rate, id::effort, id::formantTune })
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

        check (editor->getWidth() >= 900 && editor->getHeight() >= 560, "minimum editor size");
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

    // ----- 0.2.0 phrase / G2P tests (appended) -----

    std::string joinIpas (const std::vector<dsp::phonetics::Segment>& segs)
    {
        std::string o;
        for (const auto& s : segs) { if (! o.empty()) o += " "; o += s.ipa; }
        return o;
    }

    dsp::phonetics::EnglishG2P& testG2P()
    {
        static dsp::phonetics::EnglishG2P g;
        static bool once = false;
        if (! once)
        {
            loadBundledCmudict (g);
            dsp::phonetics::initInventory();
            once = true;
        }
        return g;
    }

    void testGraphemeToPhoneme()
    {
        auto& g = testG2P();
        check (g.isLoaded(), "CMUdict loaded");
        const auto hello = joinIpas (g.word ("hello", false));
        check (hello.find ("h") != std::string::npos && hello.find ("oʊ") != std::string::npos,
               "hello contains h and oʊ (" + hello + ")");
        const auto world = joinIpas (g.word ("world", false));
        check (world.find ("w") != std::string::npos && world.find ("ɝ") != std::string::npos,
               "world contains w and ɝ (" + world + ")");
        const auto the = joinIpas (g.word ("the", true));
        check (the.find ("ə") != std::string::npos || the.find ("ð") != std::string::npos,
               "the reduces or keeps a dental (" + the + ")");
        check (! g.toPhrases ("water").empty(), "water phrases");
        const auto water = joinIpas (g.toPhrases ("water")[0].segments);
        check (water.find ("ɾ") != std::string::npos, "water flaps t (" + water + ")");
        const auto cat = joinIpas (g.word ("cat", false));
        check (cat.find ("k") != std::string::npos && cat.find ("æ") != std::string::npos && cat.find ("t") != std::string::npos,
               "cat is k ae t (" + cat + ")");
        const auto funkSegs = g.word ("funk", false);
        const auto funk = joinIpas (funkSegs);
        check (funk == "f ʌ ŋ k" || (funk.find ("f") != std::string::npos && funk.find ("ʌ") != std::string::npos
               && funk.find ("ŋ") != std::string::npos && funk.find ("k") != std::string::npos),
               "funk is f ʌ ŋ k (" + funk + ")");
        bool funkStress = false;
        for (const auto& s : funkSegs)
            if (s.ipa == "ʌ" && s.stress && *s.stress == 1) funkStress = true;
        check (funkStress, "funk has primary stress on ʌ");
        const auto madeup = joinIpas (dsp::phonetics::letterToSound ("blorpt"));
        check (! madeup.empty(), "letter-to-sound returns phones for blorpt (" + madeup + ")");
        const auto twelve = g.toPhrases ("12");
        check (! twelve.empty() && ! twelve[0].segments.empty(), "number 12 expands to speech");
    }

    void testSingSyllableAdvance()
    {
        Processor p;
        p.setPhraseText ("hello world");
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 512);
        auto* mode = p.getState().getParameter (id::mode);
        check (mode != nullptr, "mode parameter exists");
        mode->setValueNotifyingHost (mode->convertTo0to1 (0.0f));
        p.applyBodyOnMessageThread();

        juce::AudioBuffer<float> buf (2, 2048);
        juce::MidiBuffer midi;
        buf.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p.processBlock (buf, midi);
        check (p.getEngine().syllableIndex() == 0, "first note-on is syllable 0");
        check (p.getEngine().syllableCount() >= 2, "hello world has at least two syllables");

        buf.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        p.processBlock (buf, midi);

        buf.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
        p.processBlock (buf, midi);
        check (p.getEngine().syllableIndex() == 1, "second note-on advances to syllable 1 (got "
               + std::to_string (p.getEngine().syllableIndex()) + ")");
        p.releaseResources();
    }

    void testSpeakRendersFinite()
    {
        Processor p;
        p.setPhraseText ("hello world");
        p.getState().getParameter (id::mode)->setValueNotifyingHost (
            p.getState().getParameter (id::mode)->convertTo0to1 (1.0f));
        p.getState().getParameter (id::rate)->setValueNotifyingHost (
            p.getState().getParameter (id::rate)->convertTo0to1 (1.2f));
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 512);
        p.applyBodyOnMessageThread();

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer first;
        first.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        double peak = 0;
        bool finite = true, ended = false;
        const int blocks = int (48000 * 3 / 512) + 8;
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear();
            juce::MidiBuffer m;
            if (i == 0) m = first;
            p.processBlock (buf, m);
            for (int n = 0; n < buf.getNumSamples(); ++n)
            {
                const auto x = buf.getSample (0, n);
                if (! std::isfinite (x)) finite = false;
                peak = std::max (peak, double (std::abs (x)));
            }
            if (! p.getEngine().isSounding() && i > 4) { ended = true; break; }
        }
        check (finite, "Speak render stays finite");
        check (peak > 1.0e-4, "Speak render is not silent (peak " + std::to_string (peak) + ")");
        check (ended || ! p.getEngine().phraseActive(), "Speak ends in finite time");
        p.releaseResources();
    }

    void testPhraseStateRoundTrip()
    {
        Processor a;
        a.setPhraseText ("hello world");
        juce::MemoryBlock block;
        a.getStateInformation (block);
        Processor b;
        b.setStateInformation (block.getData(), int (block.getSize()));
        check (b.getPhraseText() == "hello world", "phrase text survives state round-trip (got '"
               + b.getPhraseText().toStdString() + "')");

        Processor fresh;
        auto tree = fresh.getState().copyState();
        tree.removeProperty (params::phraseTextProperty, nullptr);
        juce::MemoryBlock empty;
        if (auto xml = tree.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, empty);
        Processor c;
        c.setStateInformation (empty.getData(), int (empty.getSize()));
        check (c.getPhraseText().isEmpty(), "missing phraseText loads as empty (got '"
               + c.getPhraseText().toStdString() + "')");
    }

    void testEditTextWhileNotesPlay()
    {
        Processor p;
        p.setPhraseText ("hello world");
        p.getState().getParameter (id::mode)->setValueNotifyingHost (
            p.getState().getParameter (id::mode)->convertTo0to1 (1.0f));
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        const char* phrases[] = { "hello world", "come and go", "a", "", "testing one two", "hello world" };
        bool finite = true;
        for (int i = 0; i < 200; ++i)
        {
            juce::MidiBuffer midi;
            if (i % 20 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (i % 24), (juce::uint8) 90), 0);
            if (i % 20 == 12)
                midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (i % 24)), 0);
            if (i % 15 == 7)
                p.setPhraseText (phrases[(i / 15) % 6]);
            buf.clear();
            p.processBlock (buf, midi);
            for (int n = 0; n < buf.getNumSamples(); ++n)
                if (! std::isfinite (buf.getSample (0, n))) finite = false;
        }
        check (finite, "editing text while notes play stays finite");
        p.releaseResources();
    }

    void testWriteHelloWavAndScreenshot()
    {
        const char* wavEnv = std::getenv ("DOOBLRTHROAT_HELLO_WAV");
        const char* shotEnv = std::getenv ("DOOBLRTHROAT_SCREENSHOT");
        if (wavEnv != nullptr && wavEnv[0] != 0)
        {
            Processor p;
            p.setPhraseText ("hello world");
            p.getState().getParameter (id::mode)->setValueNotifyingHost (
                p.getState().getParameter (id::mode)->convertTo0to1 (1.0f));
            p.setNonRealtime (true);
            p.prepareToPlay (48000.0, 512);
            p.applyBodyOnMessageThread();
            const int n = 48000 * 2;
            juce::AudioBuffer<float> all (1, n);
            all.clear();
            int done = 0;
            bool on = false;
            while (done < n)
            {
                juce::AudioBuffer<float> buf (2, 512);
                buf.clear();
                juce::MidiBuffer midi;
                if (! on)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
                    on = true;
                }
                p.processBlock (buf, midi);
                const int take = std::min (512, n - done);
                all.copyFrom (0, done, buf, 0, 0, take);
                done += take;
            }
            p.releaseResources();
            juce::File f (wavEnv);
            f.getParentDirectory().createDirectory();
            f.deleteFile();
            juce::WavAudioFormat wavFmt;
            auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream());
            check (os != nullptr, "open hello wav for write");
            std::unique_ptr<juce::AudioFormatWriter> w (
                wavFmt.createWriterFor (os.release(), 48000.0, 1u, 16, {}, 0));
            check (w != nullptr, "wav writer");
            check (w->writeFromAudioSampleBuffer (all, 0, n), "write hello wav");
        }
        if (shotEnv != nullptr && shotEnv[0] != 0)
        {
            Processor p;
            p.setPhraseText ("hello world");
            p.getState().getParameter (id::mode)->setValueNotifyingHost (
                p.getState().getParameter (id::mode)->convertTo0to1 (1.0f));
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            check (ed != nullptr, "editor for screenshot");
            ed->setSize (980, 600);
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            juce::File f (shotEnv);
            f.getParentDirectory().createDirectory();
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat png;
            check (os.openedOk() && png.writeImageToStream (img, os), "write UI screenshot");
        }
    }


    struct BandEnergy
    {
        double rms = 0, low = 0, high = 0;
        int n = 0;
    };

    BandEnergy measureBand (const juce::AudioBuffer<float>& buf, int start, int end, double sr)
    {
        BandEnergy e;
        const int n = buf.getNumSamples();
        start = std::max (0, start);
        end = std::min (n, end);
        if (end <= start) return e;
        // One-pole lowpass ~400 Hz and a first-difference highpass (~3 kHz emphasis).
        const auto a = 1.0 - std::exp (-2.0 * 3.141592653589793 * 400.0 / sr);
        double lp = 0, prev = 0, s = 0, sl = 0, sh = 0;
        for (int i = start; i < end; ++i)
        {
            const auto x = double (buf.getSample (0, i));
            lp += a * (x - lp);
            const auto hp = x - prev;
            prev = x;
            s += x * x;
            sl += lp * lp;
            sh += hp * hp;
        }
        e.n = end - start;
        e.rms = std::sqrt (s / e.n);
        e.low = std::sqrt (sl / e.n);
        e.high = std::sqrt (sh / e.n);
        return e;
    }

    void setEngineChoice (Processor& p, int index)
    {
        auto* e = p.getState().getParameter (id::engine);
        if (e != nullptr)
            e->setValueNotifyingHost (e->convertTo0to1 (float (index)));
    }

    juce::AudioBuffer<float> renderPhraseWav (const juce::String& text, double sr, double seconds,
                                              int engine = -1)
    {
        Processor p;
        if (engine >= 0)
            setEngineChoice (p, engine);
        p.setPhraseText (text);
        p.getState().getParameter (id::mode)->setValueNotifyingHost (
            p.getState().getParameter (id::mode)->convertTo0to1 (1.0f));
        p.getState().getParameter (id::rate)->setValueNotifyingHost (
            p.getState().getParameter (id::rate)->convertTo0to1 (1.0f));
        p.setNonRealtime (true);
        p.prepareToPlay (sr, 512);
        p.applyBodyOnMessageThread();
        const int n = int (sr * seconds);
        juce::AudioBuffer<float> all (1, n);
        all.clear();
        int done = 0;
        bool on = false;
        while (done < n)
        {
            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            juce::MidiBuffer midi;
            if (! on)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
                on = true;
            }
            p.processBlock (buf, midi);
            const int take = std::min (512, n - done);
            all.copyFrom (0, done, buf, 0, 0, take);
            done += take;
            if (done > int (sr * 0.25) && ! p.getEngine().isSounding() && ! p.getEngine().phraseActive())
                break;
        }
        p.releaseResources();
        if (done < n)
            all.setSize (1, juce::jmax (done, 1), true, true, true);
        return all;
    }

    const dsp::phonetics::PlannedSegment* findSeg (const std::vector<dsp::phonetics::PlannedSegment>& segs,
                                                   const char* ipa)
    {
        for (const auto& s : segs)
            if (s.ipa == ipa) return &s;
        return nullptr;
    }

    void testFunkG2PAndDiction()
    {
        auto& g = testG2P();
        dsp::BodyMix body;
        auto phrase = dsp::phonetics::buildPhrase (g, "funk", body);
        check (phrase != nullptr && ! phrase->speak.segments.empty(), "funk phrase planned");
        if (phrase == nullptr) return;

        std::string seq;
        for (const auto& s : phrase->speak.segments)
        {
            if (! seq.empty()) seq += " ";
            seq += s.ipa;
        }
        check (seq.find ("f") != std::string::npos && seq.find ("ʌ") != std::string::npos
               && seq.find ("ŋ") != std::string::npos && seq.find ("k") != std::string::npos,
               "planned funk phones include f ʌ ŋ k (" + seq + ")");

        const double sr = 48000.0;
        auto buf = renderPhraseWav ("funk", sr, 1.5, 1); // Tract: these checks are of the waveguide
        const auto* fSeg = findSeg (phrase->speak.segments, "f");
        const auto* ngSeg = findSeg (phrase->speak.segments, "ŋ");
        const auto* kSeg = findSeg (phrase->speak.segments, "k");
        check (fSeg && ngSeg && kSeg, "funk plan has f, ŋ and k");
        if (! fSeg || ! ngSeg || ! kSeg) return;

        const auto fE = measureBand (buf, int (fSeg->t0 * sr), int (fSeg->t1 * sr), sr);
        const auto ngE = measureBand (buf, int (ngSeg->t0 * sr), int (ngSeg->t1 * sr), sr);
        const int k0 = int (kSeg->t0 * sr);
        const int k1 = int (kSeg->t1 * sr);
        const int kEnd = std::min (buf.getNumSamples(), k1 + int (0.08 * sr));
        // Closure: interior of the stop. Burst: late in the stop and a short window after.
        const auto closE = measureBand (buf, k0 + (k1 - k0) / 5, k0 + (k1 - k0) / 2, sr);
        const auto burstE = measureBand (buf, k0 + (k1 - k0) / 2, kEnd, sr);

        check (fE.high > 1.0e-4, "/f/ has frication-band energy (high=" + std::to_string (fE.high) + ")");
        check (fE.high > closE.high * 1.2 || fE.high > 2.0e-4,
               "/f/ high-band exceeds /k/ closure (f=" + std::to_string (fE.high)
               + " close=" + std::to_string (closE.high) + ")");
        check (ngE.low > 1.0e-4, "ŋ has low-band nasal murmur (low=" + std::to_string (ngE.low) + ")");
        check (burstE.rms > closE.rms * 1.2 || burstE.high > closE.high * 1.3,
               "/k/ has a closure then a burst (close rms=" + std::to_string (closE.rms)
               + " burst rms=" + std::to_string (burstE.rms) + ")");
        check (closE.rms < 0.05, "/k/ closure is a quiet gap (rms=" + std::to_string (closE.rms) + ")");
    }

    bool boundsInside (juce::Rectangle<int> child, juce::Rectangle<int> parent)
    {
        // 1 px tolerance for outline drawing.
        return child.getX() >= parent.getX() - 1
            && child.getY() >= parent.getY() - 1
            && child.getRight() <= parent.getRight() + 1
            && child.getBottom() <= parent.getBottom() + 1;
    }

    void assertLayout (Editor& editor, const juce::String& tag)
    {
        const auto outer = editor.getLocalBounds();
        check (outer.getWidth() > 0 && outer.getHeight() > 0, "editor has size (" + tag.toStdString() + ")");

        std::function<void(juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (int i = 0; i < c.getNumChildComponents(); ++i)
            {
                auto* child = c.getChildComponent (i);
                if (child == nullptr || ! child->isVisible()) continue;
                const auto b = child->getBounds();
                if (b.isEmpty()) continue;
                const auto inEditor = child->getBounds().translated (c.getX(), c.getY());
                // Containment vs the editor in editor coordinates.
                auto r = child->getBounds();
                juce::Component* p = child->getParentComponent();
                while (p != nullptr && p != &editor)
                {
                    r.translate (p->getX(), p->getY());
                    p = p->getParentComponent();
                }
                check (boundsInside (r, outer),
                       "child '" + child->getName().toStdString()
                       + "' (" + child->getComponentID().toStdString() + ") at "
                       + std::to_string (r.getX()) + "," + std::to_string (r.getY())
                       + " " + std::to_string (r.getWidth()) + "x" + std::to_string (r.getHeight())
                       + " is inside the editor " + std::to_string (outer.getWidth()) + "x"
                       + std::to_string (outer.getHeight()) + " (" + tag.toStdString() + ")");
                walk (*child);
                (void) inEditor;
            }
        };
        walk (editor);

        std::vector<std::pair<juce::String, juce::Rectangle<int>>> top;
        for (int i = 0; i < editor.getNumChildComponents(); ++i)
        {
            auto* child = editor.getChildComponent (i);
            if (child == nullptr || ! child->isVisible()) continue;
            const auto b = child->getBounds();
            if (b.getWidth() <= 1 || b.getHeight() <= 1) continue;
            top.push_back ({ child->getComponentID().isNotEmpty() ? child->getComponentID()
                                                                  : child->getName(), b });
        }
        for (size_t i = 0; i < top.size(); ++i)
            for (size_t j = i + 1; j < top.size(); ++j)
            {
                auto overlap = top[i].second.getIntersection (top[j].second);
                // Allow a 1 px shared edge.
                if (overlap.getWidth() <= 1 || overlap.getHeight() <= 1)
                    continue;
                check (false, "top-level overlap " + top[i].first.toStdString()
                       + " vs " + top[j].first.toStdString()
                       + " (" + std::to_string (overlap.getWidth()) + "x"
                       + std::to_string (overlap.getHeight()) + ") at "
                       + tag.toStdString());
            }
    }

    void testEditorLayoutNoClipOrOverlap()
    {
        Processor p;
        p.setPhraseText ("hello world");
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for layout");
        if (editor == nullptr) return;

        const int sizes[][2] = { { 980, 600 }, { 900, 560 }, { 1100, 720 }, { 1200, 800 } };
        for (const auto& sz : sizes)
        {
            editor->setSize (sz[0], sz[1]);
            editor->resized();
            assertLayout (*editor, juce::String (sz[0]) + "x" + juce::String (sz[1]));
        }

        check (editor->findChildWithID ("phraseText") != nullptr, "phrase text field is present");
        check (editor->findChildWithID (id::mode) != nullptr, "mode combo is a top-level control");
        check (editor->findChildWithID (id::rate) != nullptr, "rate slider is a top-level control");
    }


    juce::AudioBuffer<float> renderIpaSyllableWav (const char* consonant, const char* vowel,
                                                   double sr, double seconds)
    {
        dsp::phonetics::initInventory();
        std::vector<dsp::phonetics::Segment> segs (2);
        segs[0].ipa = consonant;
        segs[1].ipa = vowel;
        segs[1].stress = 1;
        dsp::phonetics::PlanOptions o;
        o.f0 = 110.0;
        o.rate = 1.0;
        o.intonation = dsp::phonetics::Intonation::Statement;
        auto plan = dsp::phonetics::planUtterance (segs, o);

        auto voice = dsp::HumanVoice::with_defaults (sr);
        auto src = dsp::DEFAULT_SOURCE;
        src.f0 = 110.0;
        src.pressure = 8.0;
        src.effort = 0.5;
        voice.set_source (src);
        voice.set_gate (1.0);
        voice.scoreGestures = plan.gestures.data();
        voice.scoreCount = plan.gestures.size();
        voice.scoreT = 0.0;
        voice.scoreRate = 1.0;
        voice.scoreActive = ! plan.gestures.empty();

        const int n = int (sr * seconds);
        juce::AudioBuffer<float> all (1, n);
        all.clear();
        std::vector<float> block (512);
        int done = 0;
        const int tail = int (sr * (plan.end + 0.08));
        while (done < n)
        {
            const int take = std::min (512, n - done);
            std::fill (block.begin(), block.begin() + take, 0.0f);
            voice.render (block.data(), size_t (take));
            all.copyFrom (0, done, block.data(), take);
            done += take;
            if (done > tail)
                break;
        }
        if (done < n)
            all.setSize (1, juce::jmax (done, 1), true, true, true);
        return all;
    }

    struct MidConsonantArtic
    {
        double tbX = 0, tbY = 0, lipProt = 0, lipAp = 0, jaw = 0;
        bool ok = false;
    };

    MidConsonantArtic midConsonantArtic (const char* consonant, const char* vowel)
    {
        MidConsonantArtic out;
        dsp::phonetics::initInventory();
        std::vector<dsp::phonetics::Segment> segs (2);
        segs[0].ipa = consonant;
        segs[1].ipa = vowel;
        segs[1].stress = 1;
        auto plan = dsp::phonetics::planUtterance (segs, {});
        const dsp::phonetics::PlannedSegment* cSeg = nullptr;
        for (const auto& s : plan.segments)
            if (s.ipa == consonant) { cSeg = &s; break; }
        if (cSeg == nullptr)
            return out;
        dsp::phonetics::Blended b[dsp::phonetics::N_CONTROL];
        dsp::phonetics::blendAll (plan.gestures.data(), plan.gestures.size(),
                                  0.5 * (cSeg->t0 + cSeg->t1), b);
        auto val = [&] (dsp::ArticKey k) {
            const auto i = dsp::artic_index (k);
            return b[i].weight > 0.0 ? b[i].value : 0.0;
        };
        out.tbX = val (dsp::ArticKey::TbX);
        out.tbY = val (dsp::ArticKey::TbY);
        out.lipProt = val (dsp::ArticKey::LipProt);
        out.lipAp = val (dsp::ArticKey::LipAp);
        out.jaw = val (dsp::ArticKey::Jaw);
        out.ok = b[dsp::artic_index (dsp::ArticKey::TbY)].weight > 0.0;
        return out;
    }

    void testCoarticulationAndTargetApproximation()
    {
        using namespace dsp::phonetics;
        initInventory();
        const dsp::BodyMix male {};

        auto va = vowelArtic ("a", male);
        auto vi = vowelArtic ("i", male);
        auto vu = vowelArtic ("u", male);
        check (va && vi && vu, "corner vowels a, i, u are in the inventory");
        if (! (va && vi && vu))
            return;

        const auto wa = vowelCornerWeights (*va, male);
        const auto wi = vowelCornerWeights (*vi, male);
        const auto wu = vowelCornerWeights (*vu, male);
        check (std::abs (wa.a1) < 0.08 && std::abs (wa.a2) < 0.08, " /a/ maps to the /a/ corner (a1=a2=0)");
        check (std::abs (wi.a1 - 1.0) < 0.08 && std::abs (wi.a2) < 0.08, " /i/ maps to the /i/ corner (a1=1)");
        check (std::abs (wu.a2 - 1.0) < 0.08 && std::abs (wu.a1) < 0.08, " /u/ maps to the /u/ corner (a2=1)");
        // /u/ lips should sit near the /u/ labial corner.
        check (wu.b2 > 0.7, " /u/ labial weight is mostly /u/");

        auto* kDef = phonemeByIpa ("k");
        check (kDef != nullptr, "inventory has /k/");
        if (kDef != nullptr)
        {
            const auto ka = contextConsonantTarget (*kDef, &*va, male);
            const auto ki = contextConsonantTarget (*kDef, &*vi, male);
            const auto ku = contextConsonantTarget (*kDef, &*vu, male);
            const auto kaX = ka.get (dsp::ArticKey::TbX).value_or (0);
            const auto kiX = ki.get (dsp::ArticKey::TbX).value_or (0);
            const auto kuX = ku.get (dsp::ArticKey::TbX).value_or (0);
            check (kiX > kaX + 0.25, "/k/ tongue body is fronter next to /i/ than /a/");
            check (kiX > kuX + 0.25, "/k/ tongue body is fronter next to /i/ than /u/");
            const auto kuLip = ku.get (dsp::ArticKey::LipProt).value_or (0);
            const auto kiLip = ki.get (dsp::ArticKey::LipProt).value_or (0);
            check (kuLip > kiLip + 0.3, "/k/ lips are more protruded next to /u/ than /i/");
            // Constriction height (dominant TbY) stays a velar closure in every context.
            const auto kyA = ka.get (dsp::ArticKey::TbY).value_or (0);
            const auto kyI = ki.get (dsp::ArticKey::TbY).value_or (0);
            check (std::abs (kyA - kyI) < 0.15, "/k/ keeps a similar velar height in /a/ and /i/");
        }

        const auto midKa = midConsonantArtic ("k", "a");
        const auto midKi = midConsonantArtic ("k", "i");
        const auto midKu = midConsonantArtic ("k", "u");
        check (midKa.ok && midKi.ok && midKu.ok, "planned /k/ in ka, ki, ku has a tongue-body target");
        check (midKi.tbX > midKa.tbX + 0.2, "planned /k/ in ki is fronter than in ka");
        check (midKi.tbX > midKu.tbX + 0.2, "planned /k/ in ki is fronter than in ku");
        check (midKu.lipProt > midKi.lipProt + 0.2, "planned /k/ in ku is more rounded than in ki");

        // Third-order target approximation: no overshoot, lips faster than tongue body.
        dsp::ArticulatorBank bank;
        dsp::ArticPatch start;
        start.set (dsp::ArticKey::LipAp, 0.2);
        start.set (dsp::ArticKey::TbX, 2.3);
        bank.snap (start);
        dsp::ArticPatch goal;
        goal.set (dsp::ArticKey::LipAp, 1.2);
        goal.set (dsp::ArticKey::TbX, 3.3);
        bank.set_target (goal);
        bank.effortScale = 1.0;
        for (auto& s : bank.speed) s = 1.0;
        bank.tip_free = false;
        const auto lip0 = bank.x.lip_ap, tb0 = bank.x.tb_x;
        const auto lipGoal = 1.2, tbGoal = 3.3;
        double lipHalf = -1, tbHalf = -1, lipMin = lip0, lipMax = lip0, tbMin = tb0, tbMax = tb0;
        const double dt = 0.001;
        for (int n = 0; n < 800; ++n)
        {
            bank.step (dt);
            const auto t = (n + 1) * dt;
            lipMin = std::min (lipMin, bank.x.lip_ap);
            lipMax = std::max (lipMax, bank.x.lip_ap);
            tbMin = std::min (tbMin, bank.x.tb_x);
            tbMax = std::max (tbMax, bank.x.tb_x);
            if (lipHalf < 0.0 && (bank.x.lip_ap - lip0) >= 0.5 * (lipGoal - lip0))
                lipHalf = t;
            if (tbHalf < 0.0 && (bank.x.tb_x - tb0) >= 0.5 * (tbGoal - tb0))
                tbHalf = t;
        }
        check (lipHalf > 0.0 && tbHalf > 0.0, "both articulators reach halfway to the target");
        check (lipHalf < tbHalf, "lips approach a new target faster than the tongue body");
        check (lipMin >= lip0 - 0.02 && lipMax <= lipGoal + 0.05, "lip approach does not overshoot or reverse");
        check (tbMin >= tb0 - 0.02 && tbMax <= tbGoal + 0.05, "tongue-body approach does not overshoot or reverse");
        check (std::abs (bank.x.lip_ap - lipGoal) < 0.05, "lips settle near the target by 800 ms");
        check (std::abs (bank.x.tb_x - tbGoal) < 0.08, "tongue body settles near the target by 800 ms");
    }

    void testWriteDictionWavs()
    {
        const char* dirEnv = std::getenv ("DOOBLRTHROAT_DICTION_DIR");
        const char* tagEnv = std::getenv ("DOOBLRTHROAT_DICTION_TAG");
        if (dirEnv == nullptr || dirEnv[0] == 0)
            return;
        const juce::String tag = (tagEnv != nullptr && tagEnv[0] != 0) ? tagEnv : "after";
        juce::File dir (dirEnv);
        dir.createDirectory();
        const char* words[] = { "funk", "hello world", "street", "thinks", "jump", "sixth" };
        for (const char* w : words)
        {
            auto buf = renderPhraseWav (w, 48000.0, 2.0);
            juce::String stem (w);
            stem = stem.replaceCharacter (' ', '-');
            juce::File f = dir.getChildFile (stem + "_" + tag + ".wav");
            f.deleteFile();
            juce::WavAudioFormat wavFmt;
            auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream());
            check (os != nullptr, std::string ("open diction wav ") + w);
            if (os == nullptr) continue;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFmt.createWriterFor (os.release(), 48000.0, 1u, 16, {}, 0));
            check (writer != nullptr, std::string ("diction wav writer ") + w);
            if (writer != nullptr)
                check (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()),
                       std::string ("write diction wav ") + w);
        }
        const char* cvs[][3] = { { "ka", "k", "a" }, { "ki", "k", "i" }, { "ku", "k", "u" } };
        for (const auto& cv : cvs)
        {
            auto buf = renderIpaSyllableWav (cv[1], cv[2], 48000.0, 1.2);
            juce::File f = dir.getChildFile (juce::String (cv[0]) + "_" + tag + ".wav");
            f.deleteFile();
            juce::WavAudioFormat wavFmt;
            auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream());
            check (os != nullptr, std::string ("open diction wav ") + cv[0]);
            if (os == nullptr) continue;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFmt.createWriterFor (os.release(), 48000.0, 1u, 16, {}, 0));
            check (writer != nullptr, std::string ("diction wav writer ") + cv[0]);
            if (writer != nullptr)
                check (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()),
                       std::string ("write diction wav ") + cv[0]);
        }
    }


    void writeBufWav (const juce::File& f, const juce::AudioBuffer<float>& all, double sr)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::WavAudioFormat wavFmt;
        auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream());
        check (os != nullptr, "open wav " + f.getFileName().toStdString());
        std::unique_ptr<juce::AudioFormatWriter> w (
            wavFmt.createWriterFor (os.release(), sr, 1u, 16, {}, 0));
        check (w != nullptr, "wav writer " + f.getFileName().toStdString());
        check (w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()),
               "write wav " + f.getFileName().toStdString());
    }

    juce::AudioBuffer<float> renderHeldVowel (double sr, double seconds)
    {
        Processor p;
        // Sing mode, no phrase, /a/ (choice 0 is the pad, 6 is "a" / patte), vibrato off.
        p.getState().getParameter (id::mode)->setValueNotifyingHost (
            p.getState().getParameter (id::mode)->convertTo0to1 (0.0f));
        p.getState().getParameter (id::vowel)->setValueNotifyingHost (
            p.getState().getParameter (id::vowel)->convertTo0to1 (6.0f));
        p.getState().getParameter (id::vibratoDepth)->setValueNotifyingHost (
            p.getState().getParameter (id::vibratoDepth)->convertTo0to1 (0.0f));
        p.setNonRealtime (true);
        p.prepareToPlay (sr, 512);
        p.applyBodyOnMessageThread();
        const int n = int (sr * seconds);
        juce::AudioBuffer<float> all (1, n);
        all.clear();
        int done = 0;
        bool on = false;
        while (done < n)
        {
            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            juce::MidiBuffer midi;
            if (! on)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
                on = true;
            }
            p.processBlock (buf, midi);
            const int take = std::min (512, n - done);
            all.copyFrom (0, done, buf, 0, 0, take);
            done += take;
        }
        p.releaseResources();
        return all;
    }

    void testWriteAbAndHeldWavs()
    {
        if (std::getenv ("DOOBLRTHROAT_KLATT_DEBUG"))
        {
            dsp::KlattSynth synth;
            synth.setFs (48000.0);
            synth.reset();
            const dsp::BodyMix body {};
            auto fr = dsp::klattVowelFrame ("ɑ", body);
            std::cout << "klattVowel ɑ av=" << fr.av << " ah=" << fr.ah << " af=" << fr.af
                      << " f1=" << fr.f1 << " f2=" << fr.f2 << " f3=" << fr.f3
                      << " oq=" << fr.oq << " tilt=" << fr.tilt << "\n";
            synth.setFrame (fr);
            double pk = 0, acc = 0, ugAcc = 0;
            int nz = 0;
            for (int i = 0; i < 48000; ++i)
            {
                const auto y = synth.tick (220.0, 1.0);
                if (std::abs (y) > 1e-6) ++nz;
                pk = std::max (pk, std::abs (y));
                acc += y * y;
            }
            std::cout << "KlattSynth 1s /ɑ/ 220Hz peak=" << pk
                      << " rms=" << std::sqrt (acc / 48000.0)
                      << " nonzero=" << nz << "\n";

            auto phrase = dsp::phonetics::buildPhrase (testG2P(), "hello world", body);
            const auto& tr = phrase->speak.klatt;
            std::cout << "hello track n=" << tr.frames.size() << " t0=" << tr.t0
                      << " end=" << tr.end << " hop=" << tr.hop << "\n";
            for (size_t i = 0; i < tr.frames.size(); i += 2)
            {
                const auto& f = tr.frames[i];
                const auto t = tr.t0 + double (i) * tr.hop;
                if (f.av < 0.01 && f.ah < 0.01 && f.af < 0.01 && i > 4 && t > 0.05 && t < tr.end - 0.08)
                    continue;
                std::cout << "  t=" << t << " av=" << f.av << " ah=" << f.ah << " af=" << f.af
                          << " f1=" << f.f1 << " f2=" << f.f2 << " f3=" << f.f3 << "\n";
            }
        }
        if (const char* held = std::getenv ("DOOBLRTHROAT_HELD_WAV"); held != nullptr && held[0] != 0)
        {
            auto buf = renderHeldVowel (48000.0, 1.5);
            writeBufWav (juce::File (held), buf, 48000.0);
        }
        if (const char* dir = std::getenv ("DOOBLRTHROAT_AB_DIR"); dir != nullptr && dir[0] != 0)
        {
            const char* tag = std::getenv ("DOOBLRTHROAT_AB_TAG");
            const std::string prefix = (tag != nullptr && tag[0] != 0) ? std::string (tag) : std::string ("X");
            const char* phrases[][2] = {
                { "hello world", "hello_world" },
                { "funky monkey", "funky_monkey" },
                { "the quick brown fox", "quick_brown_fox" },
                { "I love you", "i_love_you" },
                { "good morning", "good_morning" },
            };
            juce::File d (dir);
            d.createDirectory();
            for (auto& ph : phrases)
            {
                auto buf = renderPhraseWav (ph[0], 48000.0, 3.0);
                writeBufWav (d.getChildFile (juce::String (prefix) + "_" + ph[1] + ".wav"), buf, 48000.0);
            }
        }
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
        { "G2P common words",            testGraphemeToPhoneme },
        { "Sing syllable advance",       testSingSyllableAdvance },
        { "Speak render finite",         testSpeakRendersFinite },
        { "phrase state round-trip",     testPhraseStateRoundTrip },
        { "edit text while notes play",  testEditTextWhileNotesPlay },
        { "hello wav / screenshot",      testWriteHelloWavAndScreenshot },
        { "funk G2P and diction",         testFunkG2PAndDiction },
        { "coarticulation and TA",         testCoarticulationAndTargetApproximation },
        { "editor layout no clip",        testEditorLayoutNoClipOrOverlap },
        { "diction wavs",                 testWriteDictionWavs },
        { "AB / held wavs",              testWriteAbAndHeldWavs },
        { "sample rates and block sizes", testSampleRatesAndBlockSizes },
        { "stress automation and body swap", testStressAutomationAndBodySwap },
    };

    const bool abOnly = (std::getenv ("DOOBLRTHROAT_HELD_WAV") != nullptr && std::getenv ("DOOBLRTHROAT_HELD_WAV")[0] != 0)
                     || (std::getenv ("DOOBLRTHROAT_AB_DIR") != nullptr && std::getenv ("DOOBLRTHROAT_AB_DIR")[0] != 0);

    int failedTests = 0;
    for (const auto& t : tests)
    {
        if (abOnly && std::string (t.name) != "AB / held wavs")
            continue;
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
