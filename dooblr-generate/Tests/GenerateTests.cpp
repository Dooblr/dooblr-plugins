/*
    dooblr-generate console tests: the real processor, sampler, sample hand-over, history,
    generation service and the editor. The tests never start Python: the worker is replaced by a
    stub shell script that speaks the worker's JSON protocol (copying a fixture WAV, or simulating
    an error, a hang or a SIGKILL crash). Every stub runs in its own process group and is killed
    or reaped before its test ends.

    Every check prints nothing on success; failures print what was expected. Each test ends with
    "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRGENERATE_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots
    (<prefix>-<w>x<h>.png) of an editor using the real ~/Library/Application Support/dooblr/generate
    folder (its history and newest sample), read-only apart from the worker script.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Engine/Sample.h"
#include "Engine/Sampler.h"
#include "Generation/GenerationService.h"
#include "Generation/History.h"
#include "Generation/ModelCatalog.h"
#include "BinaryData.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/qos.h>

//==============================================================================
// Counts operator new calls made on a thread marked as the audio thread (real-time safety).
namespace
{
    thread_local bool onAudioThread = false;
    std::atomic<bool> countAllocations { false };
    std::atomic<int> allocationCount { 0 };
}

void* operator new (std::size_t size)
{
    if (onAudioThread && countAllocations.load (std::memory_order_relaxed))
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
    using namespace dooblrgenerate;
    using Processor = DooblrGenerateProcessor;
    using Editor = DooblrGenerateEditor;

    int failedChecks = 0, passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition) { ++passedChecks; return; }
        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n"; }

    std::string str (const juce::String& s) { return s.toStdString(); }


    /** A fresh private root folder (never the real Application Support folder). */
    struct TempHome
    {
        juce::TemporaryFile temp;
        juce::File root;
        TempHome() : root (temp.getFile())
        {
            root.createDirectory();
        }
        ~TempHome() { root.deleteRecursively(); }
        gen::Paths paths() const { return gen::Paths::at (root); }
    };

    /** Writes a mono or stereo 24-bit WAV. */
    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr)
            return false;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer == nullptr)
            return false;
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    /**
        A stand-in for the worker's interpreter: a /bin/sh script given the same arguments
        (<worker.py> generate ... --out <wav> --seed <n>). The behaviour comes from <root>/stub-mode:
        "ok" (default) copies <root>/fixture.wav to --out and reports done; "error" reports an error;
        "hang" reports progress and sleeps until killed; "crash" reports progress and SIGKILLs itself
        (no abort(), so macOS writes no crash report).
    */
    juce::File makeStubWorker (const juce::File& root)
    {
        juce::AudioBuffer<float> audio (2, 22050);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const auto v = 0.89f * std::exp (-(float) i / 3000.0f) * std::sin ((float) i * 0.07f);
            audio.setSample (0, i, v);
            audio.setSample (1, i, v * 0.9f);
        }
        writeWav (root.getChildFile ("fixture.wav"), audio, 44100.0);

        const auto stub = root.getChildFile ("stub-python");
        const auto rootPath = root.getFullPathName();
        stub.replaceWithText (
            "#!/bin/sh\n"
            "cmd=\"$2\"; shift 2\n"
            "out=''; seed=0; model=''\n"
            "while [ $# -gt 0 ]; do case \"$1\" in --out) out=\"$2\"; shift;; --seed) seed=\"$2\"; shift;; --model) model=\"$2\"; shift;; esac; shift; done\n"
            "mode=$(cat '" + rootPath + "/stub-mode' 2>/dev/null || echo ok)\n"
            "echo \"$cmd $model $seed\" >> '" + rootPath + "/stub-calls'\n"
            "echo 'some library warning on stderr' 1>&2\n"
            "case \"$mode\" in\n"
            "  crash) echo '{\"event\":\"progress\",\"stage\":\"sampling\",\"fraction\":0.3}'; kill -KILL $$;;\n"
            "  runtime) echo '{\"event\":\"error\",\"code\":\"runtime\",\"message\":\"The Python runtime is broken or incomplete. Reinstall it: run setup-runtime.sh\"}'; exit 5;;\n"
            "  error) echo '{\"event\":\"error\",\"code\":\"generate\",\"message\":\"stub: simulated generation error\"}'; exit 6;;\n"
            "  hang) echo '{\"event\":\"progress\",\"stage\":\"sampling\",\"fraction\":0.1}'; while true; do sleep 1; done;;\n"
            "esac\n"
            "echo '{\"event\":\"progress\",\"stage\":\"sampling\",\"fraction\":0.5}'\n"
            "cp '" + rootPath + "/fixture.wav' \"$out\"\n"
            "printf '{\"event\":\"done\",\"device\":\"stub\",\"total_seconds\":0.01,\"generate_seconds\":0.01,\"is_test\":true,\"seed\":%s}\\n' \"$seed\"\n",
            false, false, "\n");
        stub.setExecutePermission (true);
        return stub;
    }

    void setStubMode (const juce::File& root, const juce::String& mode)
    {
        root.getChildFile ("stub-mode").replaceWithText (mode, false, false, "\n");
    }

    /** No process from this test run is left behind (stubs, sleeps, workers). */
    bool noStrayProcesses (const juce::File& root)
    {
        juce::ChildProcess pgrep;
        if (! pgrep.start (juce::StringArray { "/usr/bin/pgrep", "-f", root.getFullPathName() }))
            return false;
        const auto output = pgrep.readAllProcessOutput().trim();
        return output.isEmpty();
    }

    /** A constant-level (0.5) mono sample: easy to measure gain, length and pitch from. */
    std::shared_ptr<engine::Sample> constantSample (int length, double rate = 48000.0, float level = 0.5f)
    {
        auto s = std::make_shared<engine::Sample>();
        s->audio.setSize (1, length);
        for (int i = 0; i < length; ++i)
            s->audio.setSample (0, i, level);
        s->sampleRate = rate;
        return s;
    }

    /** A ramp 0..1 over the sample, to read back the playback position. */
    std::shared_ptr<engine::Sample> rampSample (int length, double rate = 48000.0)
    {
        auto s = std::make_shared<engine::Sample>();
        s->audio.setSize (2, length);
        for (int i = 0; i < length; ++i)
        {
            s->audio.setSample (0, i, (float) i / (float) length);
            s->audio.setSample (1, i, (float) i / (float) length);
        }
        s->sampleRate = rate;
        return s;
    }

    /** Renders n samples through a Sampler with one note-on at offset. */
    juce::AudioBuffer<float> renderNote (const engine::Sample& sample, const engine::PlaybackSettings& settings, int note,
                                         float velocity, int offset, int total, double rate = 48000.0, int block = 256)
    {
        engine::Sampler sampler;
        sampler.prepare (rate, block);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        out.clear();

        for (int pos = 0; pos < total; pos += block)
        {
            const auto n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            if (offset >= pos && offset < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), offset - pos);
            sampler.process (buf, midi, &sample, settings);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buf, ch, 0, n);
        }

        return out;
    }

    int lastNonZero (const juce::AudioBuffer<float>& b, int ch = 0)
    {
        for (int i = b.getNumSamples() - 1; i >= 0; --i)
            if (std::abs (b.getSample (ch, i)) > 1.0e-6f)
                return i;
        return -1;
    }

    int firstNonZero (const juce::AudioBuffer<float>& b, int ch = 0)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (ch, i)) > 1.0e-6f)
                return i;
        return -1;
    }

    /** Button::triggerClick is asynchronous; tests click synchronously (only enabled buttons). */
    void click (juce::Button& b)
    {
        if (b.isEnabled() && b.isVisible() && b.onClick)
            b.onClick();
    }

    /** Types into a text editor and fires its change callback synchronously. */
    void type (juce::TextEditor& editor, const juce::String& text)
    {
        editor.setText (text, false);
        if (editor.onTextChange)
            editor.onTextChange();
    }

    void setParam (Processor& p, const char* id, float realValue)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    /** A Processor in a temp home whose worker runs under /usr/bin/python3 (test backend only). */
    struct TestRig
    {
        TempHome home;
        std::unique_ptr<Processor> processor;
        TestRig()
        {
            processor = std::make_unique<Processor> (home.paths());
            processor->getService().setPython (makeStubWorker (home.root));
            processor->setModel (gen::ModelCatalog::testBackend);
            processor->prepareToPlay (48000.0, 256);
        }
    };

    bool waitFor (const std::function<bool()>& condition, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! condition())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::Thread::sleep (5);
        }
        return true;
    }

    //==============================================================================
    void testMetadata()
    {
        TempHome home;
        Processor p (home.paths());
        check (p.getName() == "dooblr-generate", "name is dooblr-generate");
        check (juce::String (JucePlugin_VersionString) == DOOBLRGENERATE_PROJECT_VERSION, "plug-in version matches CMake project version");
        check (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "takes MIDI in, no MIDI out");
        check (p.getLatencySamples() == 0, "no latency");
        check (p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2, "no input, stereo output");
        check (p.hasEditor(), "has an editor");

        const auto ids = params::allIds();
        check (ids.size() == 7, "7 automatable parameters");
        check (p.getParameters().size() == 7, "the processor exposes exactly the 7 parameters to the host");

        for (const auto& id : ids)
        {
            auto* param = p.getState().getParameter (id);
            check (param != nullptr, "parameter " + str (id) + " exists");
            if (param != nullptr)
                check (param->isAutomatable(), "parameter " + str (id) + " is automatable");
        }

        struct Expect { const char* id; float lo, hi, def; };
        for (const auto& e : { Expect { "output", -48.0f, 12.0f, 0.0f }, Expect { "tune", -24.0f, 24.0f, 0.0f },
                               Expect { "fine", -100.0f, 100.0f, 0.0f }, Expect { "start", 0.0f, 95.0f, 0.0f },
                               Expect { "attack", 0.0f, 1000.0f, 0.0f }, Expect { "decay", 10.0f, 20000.0f, 20000.0f },
                               Expect { "velocity", 0.0f, 100.0f, 100.0f } })
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (e.id));
            const auto& r = param->getNormalisableRange();
            check (std::abs (r.start - e.lo) < 1e-4f && std::abs (r.end - e.hi) < 1e-4f, std::string (e.id) + " range");
            check (std::abs (r.convertFrom0to1 (param->getDefaultValue()) - e.def) < 1e-3f, std::string (e.id) + " default");
        }

        const auto models = gen::ModelCatalog::all (false);
        check (models.size() == 2, "two real models offered");
        check (models[0].id == "stable-audio-open-small" && models[0].gated, "Stable Audio Open Small first and gated");
        check (models[1].id == "audioldm-s-full-v2" && ! models[1].gated, "AudioLDM second, not gated");
        for (const auto& m : models)
            check (! m.isTest && m.licence.isNotEmpty() && m.licenceUrl.startsWith ("https://"), str (m.id) + " has a licence line and link");
        check (gen::ModelCatalog::all (true).back().isTest, "test backend only with includeTest");
    }

    void testValueText()
    {
        TempHome home;
        Processor p (home.paths());
        auto text = [&] (const char* id, float v)
        {
            auto* param = p.getState().getParameter (id);
            return param->getText (param->convertTo0to1 (v), 32);
        };
        check (text ("decay", 20000.0f) == "Off", "Decay at max reads Off, got " + str (text ("decay", 20000.0f)));
        check (text ("decay", 250.0f) == "250 ms", "Decay 250 ms, got " + str (text ("decay", 250.0f)));
        check (text ("decay", 1500.0f) == "1.50 s", "Decay 1.5 s, got " + str (text ("decay", 1500.0f)));
        check (text ("output", -48.0f) == "-inf dB", "Output min reads -inf dB");
        check (text ("output", 3.0f) == "+3.0 dB", "Output +3.0 dB, got " + str (text ("output", 3.0f)));
        check (text ("tune", -7.0f) == "-7 st", "Tune -7 st, got " + str (text ("tune", -7.0f)));
        check (text ("fine", 12.5f) == "+12.5 ct", "Fine +12.5 ct");
        check (text ("output", 0.0f) == "0.0 dB" && text ("output", -0.01f) == "0.0 dB", "Output 0 reads 0.0 dB (no -0.0)");
        check (text ("fine", -0.01f) == "0.0 ct", "Fine 0 reads 0.0 ct");
        check (text ("start", 25.0f) == "25.0 %", "Start 25.0 %");
        check (text ("velocity", 80.0f) == "80 %", "Velocity 80 %");
    }

    void testSampler()
    {
        engine::PlaybackSettings flat;
        const auto sample = constantSample (4800);

        // Timing: silent before the note, plays exactly the sample length at the root key.
        auto out = renderNote (*sample, flat, 60, 1.0f, 300, 9600);
        check (firstNonZero (out) == 300, "note starts at its MIDI offset (300), got " + std::to_string (firstNonZero (out)));
        check (std::abs (lastNonZero (out) - (300 + 4798)) <= 1, "root key plays the whole sample once, last " + std::to_string (lastNonZero (out)));
        check (std::abs (out.getSample (0, 1000) - 0.5f) < 1e-4f, "unity gain at velocity 127 (sample level 0.5)");
        check (std::abs (out.getSample (1, 1000) - 0.5f) < 1e-4f, "mono sample plays on both channels");

        // Pitch: an octave up plays twice as fast (half as long); Tune does the same.
        out = renderNote (*sample, flat, 72, 1.0f, 0, 9600);
        check (std::abs (lastNonZero (out) - 2398) <= 2, "C4 plays at 2x speed, last " + std::to_string (lastNonZero (out)));
        auto tuned = flat; tuned.tuneSemitones = -12;
        out = renderNote (*sample, tuned, 60, 1.0f, 0, 12000);
        check (std::abs (lastNonZero (out) - 9596) <= 4, "Tune -12 plays at half speed, last " + std::to_string (lastNonZero (out)));
        auto fine = flat; fine.fineCents = 100.0f;
        auto semi = renderNote (*sample, fine, 60, 1.0f, 0, 9600);
        auto semiRef = renderNote (*sample, flat, 61, 1.0f, 0, 9600);
        check (std::abs (lastNonZero (semi) - lastNonZero (semiRef)) <= 1, "Fine +100 ct equals one semitone up");

        // Sample rate conversion: a 24 kHz sample at a 48 kHz host plays at its own pitch (twice the samples).
        const auto slow = constantSample (2400, 24000.0);
        out = renderNote (*slow, flat, 60, 1.0f, 0, 9600);
        check (std::abs (lastNonZero (out) - 4798) <= 2, "24 kHz sample lasts 0.1 s at 48 kHz");

        // Start skips into the sample.
        const auto ramp = rampSample (4800);
        auto started = flat; started.startPercent = 50.0f;
        out = renderNote (*ramp, started, 60, 1.0f, 0, 9600);
        check (std::abs (out.getSample (0, 0) - 0.5f) < 1e-3f, "Start 50 % begins half way (ramp reads 0.5)");
        check (std::abs (lastNonZero (out) - 2398) <= 2, "Start 50 % plays the second half only");

        // Velocity sensitivity.
        out = renderNote (*sample, flat, 60, 0.5f, 0, 2000);
        check (std::abs (out.getSample (0, 500) - 0.25f) < 2e-3f, "velocity 0.5 at 100 % sensitivity halves the level");
        auto insensitive = flat; insensitive.velocityPercent = 0.0f;
        out = renderNote (*sample, insensitive, 60, 0.2f, 0, 2000);
        check (std::abs (out.getSample (0, 500) - 0.5f) < 1e-3f, "Velocity 0 % ignores velocity");

        // Attack ramps linearly from 0.
        auto attack = flat; attack.attackMs = 10.0f; // 480 samples
        out = renderNote (*sample, attack, 60, 1.0f, 0, 2000);
        check (out.getSample (0, 0) < 0.01f, "Attack starts near silence");
        check (std::abs (out.getSample (0, 240) - 0.25f) < 0.01f, "Attack half way at 5 ms");
        check (std::abs (out.getSample (0, 600) - 0.5f) < 1e-3f, "full level after the attack");

        // Decay: -60 dB after Decay ms; Off keeps the level.
        auto decay = flat; decay.decayMs = 50.0f; // 2400 samples
        out = renderNote (*constantSample (48000), decay, 60, 1.0f, 0, 6000);
        const auto atDecay = out.getSample (0, 2400) / 0.5f;
        check (std::abs (juce::Decibels::gainToDecibels (atDecay) + 60.0f) < 1.0f, "Decay reaches -60 dB at 50 ms, got "
               + std::to_string (juce::Decibels::gainToDecibels (atDecay)));
        check (lastNonZero (out) < 5000, "decayed voice stops (below -100 dB)");

        // Output gain (smoothed: check after the ramp).
        auto loud = flat; loud.outputDb = -6.0f;
        out = renderNote (*constantSample (48000), loud, 60, 1.0f, 0, 6000);
        check (std::abs (out.getSample (0, 5000) - 0.5f * juce::Decibels::decibelsToGain (-6.0f)) < 1e-3f, "Output -6 dB");
        loud.outputDb = -48.0f;
        out = renderNote (*constantSample (48000), loud, 60, 1.0f, 0, 6000);
        check (std::abs (out.getSample (0, 5000)) < 1e-6f, "Output at min is silent");

        // One-shot: note-off is ignored; all-notes-off stops; polyphony and stealing.
        {
            engine::Sampler sampler;
            sampler.prepare (48000.0, 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 10);
            sampler.process (buf, midi, sample.get(), flat);
            check (sampler.numActiveVoices() == 1 && std::abs (buf.getSample (0, 200) - 0.5f) < 1e-4f, "note-off does not stop a one-shot");
            midi.clear();
            for (int n = 0; n < 20; ++n)
                midi.addEvent (juce::MidiMessage::noteOn (1, 40 + n, 1.0f), n);
            sampler.process (buf, midi, sample.get(), flat);
            check (sampler.numActiveVoices() == engine::Sampler::numVoices, "20 notes use all 16 voices (oldest stolen)");
            midi.clear();
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            sampler.process (buf, midi, sample.get(), flat);
            check (sampler.numActiveVoices() == 0 && buf.getMagnitude (0, 256) < 1e-6f, "all-notes-off silences");

            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            sampler.process (buf, midi, sample.get(), flat);
            const auto other = constantSample (4800);
            midi.clear();
            sampler.process (buf, midi, other.get(), flat);
            check (sampler.numActiveVoices() == 0, "replacing the sample stops voices of the old one");
            sampler.process (buf, midi, nullptr, flat);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            sampler.process (buf, midi, nullptr, flat);
            check (buf.getMagnitude (0, 256) == 0.0f && sampler.numActiveVoices() == 0, "no sample: notes are silent");
        }
    }

    void testProcessorMidi()
    {
        TestRig rig;
        auto& p = *rig.processor;
        const auto wav = rig.home.root.getChildFile ("in.wav");
        juce::AudioBuffer<float> audio (2, 4800);
        for (int i = 0; i < 4800; ++i) { audio.setSample (0, i, 0.5f); audio.setSample (1, i, -0.25f); }
        check (writeWav (wav, audio, 48000.0), "wrote a stereo WAV");
        juce::String error;
        check (p.loadFile (wav, error), "loaded the WAV as the current sample: " + str (error));
        check (p.getCurrentSample() != nullptr && p.getCurrentSample()->audio.getNumChannels() == 2, "stereo sample");

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 100);
        p.processBlock (buf, midi);
        check (buf.getMagnitude (0, 0, 100) == 0.0f, "silent before the note");
        check (std::abs (buf.getSample (0, 300) - 0.5f) < 1e-3f && std::abs (buf.getSample (1, 300) + 0.25f) < 1e-3f,
               "processor plays the stereo sample from MIDI");

        // Play button (preview) plays the root key without MIDI.
        midi.clear();
        juce::AudioBuffer<float> idle (2, 512);
        for (int i = 0; i < 20; ++i) p.processBlock (idle, midi); // let the first voice end
        p.requestPreview();
        p.processBlock (buf, midi);
        check (std::abs (buf.getSample (0, 10) - 0.5f) < 1e-3f, "Play triggers the sample");

        // Parameters reach the sampler.
        setParam (p, "output", -48.0f);
        for (int i = 0; i < 20; ++i) p.processBlock (idle, midi);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        p.processBlock (buf, midi);
        check (buf.getMagnitude (0, 512) < 1e-6f, "Output parameter applies");
    }

    void testSampleSlot()
    {
        engine::SampleSlot slot;
        check (slot.acquire() == nullptr, "empty slot gives nullptr");
        auto a = constantSample (100);
        std::weak_ptr<engine::Sample> weakA = a;
        slot.publish (a);
        a.reset();
        check (slot.acquire() == slot.current().get(), "audio thread sees the published sample");
        auto b = constantSample (100);
        std::weak_ptr<engine::Sample> weakB = b;
        slot.publish (b);
        b.reset();
        check (! weakA.expired(), "the sample the audio thread is using is kept alive after a swap");
        slot.acquire();
        slot.collect();
        check (weakA.expired(), "freed once the audio thread moved on");
        check (slot.numAlive() == 1, "only the current sample stays alive");

        // Stress: one thread publishes new samples while the "audio thread" reads them; every read
        // must see a complete, live sample and the audio thread must never allocate.
        std::atomic<bool> stop { false };
        std::atomic<int> bad { 0 }, reads { 0 };
        std::thread writer ([&]
        {
            for (int i = 0; i < 3000; ++i)
                slot.publish (constantSample (64 + (i % 7)));
            stop = true;
        });
        std::thread reader ([&]
        {
            onAudioThread = true;
            while (! stop)
            {
                if (const auto* s = slot.acquire())
                {
                    const auto n = s->audio.getNumSamples();
                    // 100: the sample published before the writer starts; 64..70: the writer's.
                    if (((n < 64 || n > 70) && n != 100) || s->audio.getSample (0, n - 1) != 0.5f)
                        ++bad;
                    ++reads;
                }
            }
            onAudioThread = false;
        });
        countAllocations = true;
        allocationCount = 0;
        writer.join();
        reader.join();
        countAllocations = false;
        check (bad == 0, "every concurrent read saw a valid sample (" + std::to_string (reads.load()) + " reads)");
        check (allocationCount == 0, "the reading thread never allocated");
    }

    void testHistory()
    {
        TempHome home;
        gen::History history (home.paths());
        check (history.size() == 0, "starts empty");

        juce::String id1, id2;
        const auto f1 = history.newWavFile ("Bright Clap!!", 42, id1);
        check (f1.getFileName().endsWith ("-bright-clap-s42.wav"), "file name has a slug and the seed: " + str (f1.getFileName()));
        check (f1.getParentDirectory() == home.paths().history(), "in the history folder");
        juce::AudioBuffer<float> audio (1, 100);
        audio.clear();
        writeWav (f1, audio, 44100.0);
        const auto f2 = history.newWavFile ("Bright Clap!!", 42, id2);
        check (f2 != f1 && id2 != id1, "a second file for the same prompt and seed gets a new name");
        writeWav (f2, audio, 44100.0);

        gen::HistoryEntry e1 { id1, "Bright Clap!!", "audioldm-s-full-v2", 42, 1.5, 1000, 8.0, 9.5, "mps", false, f1 };
        gen::HistoryEntry e2 { id2, "909 kick", "test-backend", 7, 2.0, 2000, 0.1, 0.2, "cpu", true, f2 };
        check (history.add (e1) && history.add (e2), "entries saved");
        auto list = history.entries();
        check (list.size() == 2 && list[0].id == id2, "newest first");

        gen::History reloaded (home.paths());
        list = reloaded.entries();
        check (list.size() == 2, "history persists in history.json");
        gen::HistoryEntry found;
        check (reloaded.find (id1, found) && found.prompt == "Bright Clap!!" && found.seed == 42 && found.model == "audioldm-s-full-v2"
               && std::abs (found.seconds - 1.5) < 1e-9 && found.device == "mps" && ! found.isTest && found.wav == f1,
               "every field round-trips");
        check (reloaded.find (id2, found) && found.isTest, "the test flag round-trips");

        f2.deleteFile();
        reloaded.reload();
        check (reloaded.size() == 1, "entries whose WAV is gone are dropped on load");
        check (f1.existsAsFile(), "the plug-in never deletes WAVs");

        for (int i = 0; i < gen::History::maxEntries + 5; ++i)
        {
            auto e = e1;
            e.id = "x" + juce::String (i);
            reloaded.add (e);
        }
        check (reloaded.size() == gen::History::maxEntries, "capped at maxEntries");
    }

    void testStateRoundTrip()
    {
        TestRig rig;
        auto& p = *rig.processor;

        // A history entry to restore by id.
        juce::String id;
        const auto wav = p.getHistory().newWavFile ("ragtime piano note", 99, id);
        juce::AudioBuffer<float> audio (1, 2000);
        for (int i = 0; i < 2000; ++i) audio.setSample (0, i, std::sin ((float) i * 0.05f) * 0.5f);
        writeWav (wav, audio, 32000.0);
        p.getHistory().add ({ id, "ragtime piano note", "audioldm-s-full-v2", 99, 2.0, 1, 1.0, 1.0, "mps", false, wav });

        juce::String error;
        check (p.loadHistoryEntry (id, error), "load history entry: " + str (error));
        p.setPrompt ("ragtime piano note");
        p.setSeed (99);
        p.setSeconds (3.25);
        p.setModel ("audioldm-s-full-v2");
        setParam (p, "tune", -5.0f);
        setParam (p, "fine", 33.0f);
        setParam (p, "start", 12.5f);
        setParam (p, "attack", 20.0f);
        setParam (p, "decay", 700.0f);
        setParam (p, "velocity", 40.0f);
        setParam (p, "output", -3.0f);

        juce::MemoryBlock state;
        p.getStateInformation (state);

        Processor q (rig.home.paths());
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.getPrompt() == "ragtime piano note", "prompt restored");
        check (q.getSeed() == 99, "seed restored");
        check (std::abs (q.getSeconds() - 3.25) < 1e-9, "length restored");
        check (q.getModel() == "audioldm-s-full-v2", "model restored");
        for (const auto& id_ : params::allIds())
        {
            const auto a = p.getState().getParameter (id_)->getValue();
            const auto b = q.getState().getParameter (id_)->getValue();
            check (std::abs (a - b) < 1e-6f, "parameter " + str (id_) + " restored");
        }
        const auto restored = q.getCurrentSample();
        check (restored != nullptr && restored->historyId == id && restored->prompt == "ragtime piano note" && restored->seed == 99,
               "current sample restored from the history (with its prompt and seed)");
        check (restored != nullptr && std::abs (restored->sampleRate - 32000.0) < 1e-9 && restored->audio.getNumSamples() == 2000,
               "restored audio matches the file");

        // Not in this machine's history any more: falls back to the WAV path.
        rig.home.paths().historyIndex().deleteFile();
        q.getHistory().reload();
        Processor r (rig.home.paths());
        r.setStateInformation (state.getData(), (int) state.getSize());
        check (r.getCurrentSample() != nullptr && r.getCurrentSample()->file == wav, "falls back to the saved WAV path");

        // WAV gone too: state still restores and there is no sample.
        wav.deleteFile();
        Processor s (rig.home.paths());
        s.setStateInformation (state.getData(), (int) state.getSize());
        check (s.getCurrentSample() == nullptr && s.getPrompt() == "ragtime piano note", "missing WAV: settings restore, no sample");

        // Garbage is ignored.
        const char junk[] = "not a state";
        s.setStateInformation (junk, (int) sizeof (junk));
        check (s.getPrompt() == "ragtime piano note", "garbage state ignored");

        // Unknown model ids fall back to Stable Audio Open Small.
        s.getSettings().setProperty (params::setting::model, "nope", nullptr);
        check (s.getModel() == "stable-audio-open-small", "unknown model falls back");
        s.setSeconds (100.0);
        check (std::abs (s.getSeconds() - params::maxSeconds) < 1e-9, "length clamped to 10 s");
    }

    void testModelStatus()
    {
        TempHome home;
        gen::History history (home.paths());
        gen::GenerationService service (home.paths(), history, nullptr);
        const juce::String sao = gen::ModelCatalog::stableAudioOpenSmall, ldm = gen::ModelCatalog::audioLdm;

        service.setPython (home.root.getChildFile ("no-such-python"));
        check (service.modelStatus (ldm).state == gen::ModelState::runtimeMissing, "no Python: runtime missing");
        check (service.modelStatus (ldm).details.contains ("setup-runtime.sh"), "runtime missing says how to fix it");

        // The system / Xcode Python is never used, even when chosen explicitly.
        check (gen::GenerationService::isSystemPython (juce::File ("/usr/bin/python3")), "/usr/bin/python3 is a system Python");
        check (gen::GenerationService::isSystemPython (juce::File ("/Applications/Xcode.app/Contents/Developer/usr/bin/python3")), "Xcode's Python is a system Python");
        check (! gen::GenerationService::isSystemPython (home.root.getChildFile ("venv/bin/python3")), "a venv Python is not");
        service.setPython (juce::File ("/usr/bin/python3"));
        check (service.findPython() == juce::File(), "a system Python in settings is refused");
        check (service.modelStatus (ldm).state == gen::ModelState::runtimeMissing, "system Python only: runtime not installed");

        service.setPython (makeStubWorker (home.root));
        check (service.modelStatus (sao).state == gen::ModelState::needsAccess, "gated model without a token: needs access");
        const auto details = service.modelStatus (sao).details;
        check (details.contains ("https://huggingface.co/stabilityai/stable-audio-open-small") && details.contains ("settings/tokens"),
               "needs-access details give the licence page and token page");
        check (details.contains ("dooblr-generate " JucePlugin_VersionString) && details.contains ("Step:"),
               "details are a copyable report (version, OS, step)");
        check (service.modelStatus (ldm).state == gen::ModelState::notInstalled, "AudioLDM not downloaded: not installed");
        check (service.modelStatus (gen::ModelCatalog::testBackend).state == gen::ModelState::ready, "test backend is always ready");
        check (service.modelStatus (gen::ModelCatalog::testBackend).text.contains ("not AI"), "test backend says it is not AI");

        // Token: stored owner-only, never in settings.json; with a token the gated model is just not installed.
        check (! service.saveToken ("has a space"), "rejects a malformed token");
        check (service.saveToken ("hf_dummyTokenForTests"), "saves a token");
        check (service.hasToken(), "has the token");
        check (! home.paths().settings().loadFileAsString().contains ("hf_dummy"), "the token is not in settings.json");
        check ((home.paths().tokenFile().getFullPathName().isNotEmpty()) && [&]
               {
                   struct stat st {};
                   return ::stat (home.paths().tokenFile().getFullPathName().toRawUTF8(), &st) == 0 && (st.st_mode & 0777) == 0600;
               }(), "token file is owner read/write only (0600)");
        check (service.modelStatus (sao).state == gen::ModelState::notInstalled, "with a token: not installed");

        // A folder the user picked with the files in it: ready.
        const auto folder = home.root.getChildFile ("my-sao");
        folder.createDirectory();
        folder.getChildFile ("model_config.json").replaceWithText ("{}", false, false, "\n");
        service.setModelFolder (sao, folder);
        check (service.modelStatus (sao).state == gen::ModelState::notInstalled, "folder missing model.safetensors: not installed");
        check (service.modelStatus (sao).details.contains ("model.safetensors"), "names the missing file");
        folder.getChildFile ("model.safetensors").replaceWithText ("x", false, false, "\n");
        check (service.modelStatus (sao).state == gen::ModelState::ready, "located folder with the files: ready");
        check (service.modelFolder (sao) == folder, "uses the located folder");
        service.setModelFolder (sao, {});
        check (service.modelFolder (sao) == home.paths().models().getChildFile (sao), "reset to the default folder");
        service.clearToken();
        check (! service.hasToken(), "token cleared");

        // Download through a fake worker interpreter: progress, then success / access errors.
        const auto fake = home.root.getChildFile ("fake-python");
        fake.replaceWithText ("#!/bin/sh\n"
                              "echo '{\"event\":\"progress\",\"stage\":\"downloading\",\"fraction\":0.42,\"message\":\"420 / 1000 MB\"}'\n"
                              "echo 'some library warning'\n"
                              "sleep 1\n"
                              "if [ -f \"" + home.root.getChildFile ("deny").getFullPathName() + "\" ]; then\n"
                              "  echo '{\"event\":\"error\",\"code\":\"access\",\"message\":\"gated: accept the licence\"}'; exit 4; fi\n"
                              "mkdir -p \"$6\"; for f in model_index.json unet vae vocoder text_encoder; do :; done\n"
                              "echo '{\"event\":\"done\",\"model\":\"x\"}'\n", false, false, "\n");
        fake.setExecutePermission (true);
        service.setPython (fake);
        check (service.startDownload (ldm), "download starts");
        check (! service.startDownload (ldm), "a second job is refused while one runs");
        check (waitFor ([&] { return service.modelStatus (ldm).state == gen::ModelState::downloading
                                     && service.modelStatus (ldm).fraction > 0.4f; }, 3000),
               "downloading state with the worker's progress");
        check (service.modelStatus (ldm).text == "Downloading 42%", "status reads Downloading 42%, got " + str (service.modelStatus (ldm).text));
        check (service.waitUntilIdle (5000), "download job ends");
        check (service.jobStatus().result == gen::JobStatus::Result::succeeded, "download succeeded: " + str (service.jobStatus().error));

        home.root.getChildFile ("deny").replaceWithText ("1", false, false, "\n");
        check (service.startDownload (sao), "gated download starts");
        check (service.waitUntilIdle (5000), "gated download ends");
        check (service.jobStatus().result == gen::JobStatus::Result::failed, "refused download fails");
        check (service.modelStatus (sao).state == gen::ModelState::needsAccess, "refused download: needs access");
        check (service.modelStatus (sao).details.contains ("gated: accept the licence"), "the worker's message is in the copyable details");

        // A cancelled download.
        home.root.getChildFile ("deny").deleteFile();
        check (service.startDownload (ldm), "download restarts");
        juce::Thread::sleep (200);
        service.cancel();
        check (service.waitUntilIdle (3000), "cancelled download ends quickly");
        check (service.jobStatus().result == gen::JobStatus::Result::cancelled, "download cancelled");
        check (! service.startDownload (gen::ModelCatalog::testBackend), "nothing to download for the test backend");
    }

    void testWorkerScript()
    {
        TempHome home;
        gen::History history (home.paths());
        gen::GenerationService service (home.paths(), history, nullptr);
        const auto installed = service.installWorker();
        const auto source = juce::File (DOOBLRGENERATE_SOURCE_DIR).getChildFile ("worker/dooblr_generate_worker.py");
        check (installed.existsAsFile(), "worker written to runtime/");
        check (installed.loadFileAsString() == source.loadFileAsString(), "embedded worker is the repo's worker script");
        installed.replaceWithText ("stale", false, false, "\n");
        service.installWorker();
        check (installed.loadFileAsString() == source.loadFileAsString(), "a stale worker is replaced");
    }

    void testGenerationEndToEnd()
    {
        TestRig rig;
        auto& p = *rig.processor;
        check (! p.generate (false), "empty prompt does not generate");
        p.setPrompt ("bright clap");
        p.setSeed (777);
        p.setSeconds (0.5);
        check (p.generate (false), "generate starts");
        check (! p.generate (false), "a second generate is refused while busy");
        check (p.getService().waitUntilIdle (20000), "generation finishes");
        const auto job = p.getService().jobStatus();
        check (job.result == gen::JobStatus::Result::succeeded, "generation succeeded: " + str (job.error));

        const auto sample = p.getCurrentSample();
        check (sample != nullptr, "the result became the current sample");
        if (sample == nullptr)
            return;
        check (sample->isTest, "test output is flagged as a test, not AI");
        check (sample->prompt == "bright clap" && sample->seed == 777 && sample->model == "test-backend", "sample carries prompt, seed, model");
        check (std::abs (sample->lengthSeconds() - 0.5) < 0.01, "the worker's WAV is loaded whole (0.5 s fixture)");
        check (rig.home.root.getChildFile ("stub-calls").loadFileAsString().contains ("generate test-backend 777"),
               "the worker got the model and seed");
        check (sample->file.existsAsFile() && sample->file.getParentDirectory() == rig.home.paths().history(), "WAV saved in the history folder");
        check (p.getHistory().size() == 1 && p.getHistory().entries()[0].isTest && p.getHistory().entries()[0].seed == 777,
               "history entry added (test flag, seed)");

        // It plays from MIDI.
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        p.processBlock (buf, midi);
        check (buf.getMagnitude (0, 512) > 0.01f, "generated sample plays from MIDI");

        // Same seed, same output (deterministic backend); Variation changes the seed.
        check (p.generate (false) && p.getService().waitUntilIdle (20000), "second generation with the same seed");
        const auto again = p.getCurrentSample();
        check (again != nullptr && again->file != sample->file, "a new history file each time");
        bool same = again != nullptr && again->audio.getNumSamples() == sample->audio.getNumSamples();
        for (int i = 0; same && i < sample->audio.getNumSamples(); i += 97)
            same = std::abs (again->audio.getSample (0, i) - sample->audio.getSample (0, i)) < 1e-6f;
        check (same, "same prompt + seed gives the same audio");
        check (p.generate (true) && p.getService().waitUntilIdle (20000), "variation generates");
        check (p.getSeed() != 777, "variation picked a new seed");
        check (p.getHistory().size() == 3, "three history entries");

        // Worker failures never take the plug-in down.
        gen::GenerateRequest crash { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "crash");
        check (p.getService().startGenerate (crash), "crashing job starts");
        check (p.getService().waitUntilIdle (20000), "crashed job ends");
        auto status = p.getService().jobStatus();
        check (status.result == gen::JobStatus::Result::failed && status.errorCode == "crash", "worker crash: the job fails (code crash)");
        check (status.error.contains ("stopped unexpectedly") && status.error.contains ("signal 9") && status.error.contains ("dooblr-generate "),
               "crash report is copyable text naming the signal");
        check (status.error.contains ("some library warning"), "the worker's other output is in the report");
        check (p.getCurrentSample() != nullptr && p.getHistory().size() == 3, "crash leaves the current sample and history alone");

        gen::GenerateRequest error { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "error");
        check (p.getService().startGenerate (error) && p.getService().waitUntilIdle (20000), "erroring job ends");
        status = p.getService().jobStatus();
        check (status.result == gen::JobStatus::Result::failed && status.error.contains ("simulated generation error") && status.error.contains ("exit code 6"),
               "worker error message and exit code reach the report");

        gen::GenerateRequest hang { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "hang");
        check (p.getService().startGenerate (hang), "hanging job starts");
        check (waitFor ([&] { return p.getService().jobStatus().stage == "sampling"; }, 10000), "hanging job reports progress");
        const auto t0 = juce::Time::getMillisecondCounter();
        p.getService().cancel();
        check (p.getService().waitUntilIdle (3000), "cancel ends a hung worker");
        check (juce::Time::getMillisecondCounter() - t0 < 2000, "cancel takes under 2 s");
        check (p.getService().jobStatus().result == gen::JobStatus::Result::cancelled, "job reads cancelled");
        check (noStrayProcesses (rig.home.root), "cancel killed the worker and its children (no stray processes)");
        setStubMode (rig.home.root, "ok");
        check (p.generate (false) && p.getService().waitUntilIdle (20000)
                 && p.getService().jobStatus().result == gen::JobStatus::Result::succeeded,
               "generation works again after crash, error and cancel");

        // A real model that is missing or incomplete fails before any process starts.
        const auto callsBefore = rig.home.root.getChildFile ("stub-calls").loadFileAsString();
        const auto partial = rig.home.root.getChildFile ("partial-ldm");
        partial.getChildFile ("unet").createDirectory();
        partial.getChildFile ("model_index.json").replaceWithText ("{}", false, false, "\n");
        p.getService().setModelFolder (gen::ModelCatalog::audioLdm, partial);
        gen::GenerateRequest real { "909 kick", gen::ModelCatalog::audioLdm, 5, 1.0, 0 };
        check (p.getService().startGenerate (real) && p.getService().waitUntilIdle (5000), "generate with a partial model ends");
        status = p.getService().jobStatus();
        check (status.result == gen::JobStatus::Result::failed && status.error.contains ("not installed or incomplete")
                 && status.error.contains ("unet/diffusion_pytorch_model.safetensors") && ! status.error.contains ("model_index.json,"),
               "partial model: copyable report lists exactly the missing files");
        check (rig.home.root.getChildFile ("stub-calls").loadFileAsString() == callsBefore, "no worker process was started for it");
        check (p.getService().modelStatus (gen::ModelCatalog::audioLdm).state == gen::ModelState::notInstalled,
               "partial model reads Not installed");

        // No Python: a clear failure, not a hang.
        p.getService().setPython (rig.home.root.getChildFile ("missing-python"));
        check (! p.generate (false) || (p.getService().waitUntilIdle (5000) && p.getService().jobStatus().result == gen::JobStatus::Result::failed),
               "missing runtime fails cleanly");
    }

    void testAudioThreadDuringGeneration()
    {
        TestRig rig;
        auto& p = *rig.processor;
        const auto wav = rig.home.root.getChildFile ("a.wav");
        juce::AudioBuffer<float> audio (2, 48000);
        for (int i = 0; i < 48000; ++i) { audio.setSample (0, i, 0.3f * std::sin ((float) i * 0.01f)); audio.setSample (1, i, 0.3f); }
        writeWav (wav, audio, 48000.0);
        juce::String error;
        p.loadFile (wav, error);

        // A long-running worker job (the hang) and sample swaps from another thread while the
        // audio thread keeps processing: it must never allocate, lock or stall.
        gen::GenerateRequest hang { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "hang");
        check (p.getService().startGenerate (hang), "background job running");
        waitFor ([&] { return p.getService().jobStatus().stage == "sampling"; }, 10000);

        std::atomic<bool> stop { false };
        std::thread swapper ([&]
        {
            juce::String e;
            while (! stop)
            {
                p.loadFile (wav, e);
                juce::Thread::sleep (2);
            }
        });

        double worstMs = 0.0, totalMs = 0.0;
        int blocks = 0;
        std::thread audioThread ([&]
        {
            // Like a host's audio thread: the highest QoS class (an ordinary thread can be
            // descheduled for milliseconds, which says nothing about the plug-in).
            pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
            onAudioThread = true;
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            midi.ensureSize (64);
            for (int i = 0; i < 4000; ++i)
            {
                midi.clear();
                if (i % 8 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 48 + i % 24, (juce::uint8) 100), i % 200);
                const auto t0 = std::chrono::steady_clock::now();
                p.processBlock (buf, midi);
                const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                worstMs = std::max (worstMs, ms);
                totalMs += ms;
                ++blocks;
            }
            onAudioThread = false;
        });

        countAllocations = true;
        allocationCount = 0;
        audioThread.join();
        countAllocations = false;
        stop = true;
        swapper.join();

        check (p.getService().isBusy(), "the worker job was still running throughout");
        check (allocationCount == 0, "processBlock never allocated during generation and swaps (" + std::to_string (allocationCount.load()) + ")");
        check (worstMs < 2.0, "worst block " + std::to_string (worstMs) + " ms (< 2 ms for a 5.3 ms block)");
        note ("4000 blocks of 256 at 48 kHz with 16 voices: mean " + std::to_string (totalMs / blocks) + " ms, worst "
              + std::to_string (worstMs) + " ms while a worker job ran and samples were swapped");
        p.getService().cancel();
        check (p.getService().waitUntilIdle (3000), "cleanup");
        check (noStrayProcesses (rig.home.root), "no stray worker processes");
    }

    //==============================================================================
    bool insideAndVisible (juce::Component& c, juce::Component& editor)
    {
        return c.isVisible() && editor.getLocalBounds().contains (editor.getLocalArea (c.getParentComponent(), c.getBounds()))
               && c.getWidth() > 0 && c.getHeight() > 0;
    }

    void testEditorLayout()
    {
        TestRig rig;
        auto& p = *rig.processor;
        p.setModel (gen::ModelCatalog::audioLdm);
        std::unique_ptr<Editor> editor (dynamic_cast<Editor*> (p.createEditor()));
        check (editor != nullptr, "editor created");
        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080x660");

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            editor->refresh();
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            check (editor->getWidth() == size.x && editor->getHeight() == size.y, label + ": size accepted");

            std::vector<std::pair<std::string, juce::Component*>> controls {
                { "prompt", &editor->getPromptEditor() }, { "seed", &editor->getSeedEditor() }, { "length", &editor->getLengthSlider() },
                { "generate", &editor->getGenerateButton() }, { "variation", &editor->getVariationButton() },
                { "waveform", &editor->getWaveform() }, { "play", &editor->getPlayButton() }, { "drag", &editor->getDragButton() },
                { "history", &editor->getHistoryList() }, { "playback", &editor->getPlaybackPanel() },
                { "model box", &editor->getModelBox() }, { "status", &editor->getStatusLabel() },
                { "download", &editor->getDownloadButton() }, { "token", &editor->getTokenButton() },
                { "folder", &editor->getFolderButton() }, { "copy", &editor->getCopyButton() }, { "details", &editor->getDetails() } };

            for (auto& [name, c] : controls)
                check (insideAndVisible (*c, *editor), label + ": " + name + " visible inside the window");

            for (size_t i = 0; i < controls.size(); ++i)
                for (size_t j = i + 1; j < controls.size(); ++j)
                {
                    auto* a = controls[i].second; auto* b = controls[j].second;
                    if (a->getParentComponent() == b->getParentComponent())
                        check (! a->getBounds().intersects (b->getBounds()), label + ": " + controls[i].first + " overlaps " + controls[j].first);
                }

            check (editor->getPromptEditor().getWidth() >= 300, label + ": prompt at least 300 px wide");
            check (editor->getWaveform().getHeight() >= 150, label + ": waveform at least 150 px tall (" + std::to_string (editor->getWaveform().getHeight()) + ")");
            check (editor->getHistoryList().getHeight() >= 120, label + ": history at least 120 px tall");
            check (editor->getGenerateButton().getWidth() >= 100 && editor->getGenerateButton().getHeight() >= 26, label + ": Generate button size");
            check (editor->getDetails().getHeight() >= 30, label + ": details box at least 30 px tall");

            auto& panel = editor->getPlaybackPanel();
            for (const auto& id : params::allIds())
            {
                auto* c = panel.findControl (id);
                check (c != nullptr && c->isVisible() && panel.getLocalBounds().contains (c->getBounds()), label + ": knob " + str (id) + " inside PLAYBACK");
            }
        }
    }

    void testEditorBehaviour()
    {
        TestRig rig;
        auto& p = *rig.processor;
        p.setModel (gen::ModelCatalog::audioLdm);
        std::unique_ptr<Editor> editor (dynamic_cast<Editor*> (p.createEditor()));
        editor->refresh();

        // AudioLDM not installed: Generate off, Download on, Token off (not gated), status says so.
        check (editor->getStatusLabel().getText() == "Not installed", "status: Not installed, got " + str (editor->getStatusLabel().getText()));
        check (! editor->getGenerateButton().isEnabled(), "Generate disabled while the model is not installed");
        check (editor->getDownloadButton().isEnabled(), "Download enabled");
        check (! editor->getTokenButton().isEnabled(), "Token disabled for an ungated model");
        check (editor->getDetails().isReadOnly() && editor->getDetails().getText().contains ("Missing in"), "details explain what is missing");

        // Copy copies exactly the details text.
        juce::SystemClipboard::copyTextToClipboard ("before");
        click (editor->getCopyButton());
        check (juce::SystemClipboard::getTextFromClipboard() == editor->getDetails().getText(), "Copy puts the details on the clipboard");

        // Stable Audio Open Small: needs access; Token entry shows and hides; saving a token changes the state.
        editor->getModelBox().setSelectedId (1, juce::sendNotificationSync);
        editor->refresh();
        check (p.getModel() == "stable-audio-open-small", "model box sets the model");
        check (editor->getStatusLabel().getText() == "Needs Hugging Face access", "status: needs access");
        check (editor->getTokenButton().isEnabled(), "Token enabled for the gated model");
        click (editor->getTokenButton());
        check (editor->getTokenEditor().isVisible() && ! editor->getDetails().isVisible(), "Token... shows the token field");
        check (editor->getTokenEditor().getPasswordCharacter() != 0, "token field masks the token");
        editor->getTokenEditor().setText ("hf_dummyTokenForTests");
        for (auto* c : editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Save" && b->isVisible())
                click (*b);
        check (! editor->getTokenEditor().isVisible() && editor->getDetails().isVisible(), "Save hides the token field");
        check (p.getService().hasToken(), "token saved");
        editor->refresh();
        check (editor->getStatusLabel().getText() == "Not installed", "with a token the gated model reads Not installed");

        // Test backend: Generate through the UI, then the waveform, info and history show it.
        p.setModel (gen::ModelCatalog::testBackend);
        type (editor->getPromptEditor(), "909 kick");
        editor->refresh();
        check (p.getPrompt() == "909 kick", "typing sets the prompt");
        check (editor->getGenerateButton().isEnabled() && editor->getVariationButton().isEnabled(), "Generate and Variation enabled");
        type (editor->getSeedEditor(), "4242");
        check (p.getSeed() == 4242, "seed field sets the seed");
        editor->getLengthSlider().setValue (0.75, juce::sendNotificationSync);
        check (std::abs (p.getSeconds() - 0.75) < 1e-9, "length slider sets the length");
        click (editor->getGenerateButton());
        editor->refresh();
        check (editor->getGenerateButton().getButtonText() == "Cancel", "Generate becomes Cancel while running");
        check (! editor->getVariationButton().isEnabled(), "Variation disabled while running");
        check (p.getService().waitUntilIdle (20000), "UI generation finishes");
        editor->refresh();
        check (editor->getGenerateButton().getButtonText() == "Generate", "back to Generate");
        check (editor->getProgressLabel().getText().startsWith ("Generated in"), "progress line reports the time: " + str (editor->getProgressLabel().getText()));
        check (editor->getWaveform().hasSample() && ! editor->getWaveform().getColumns().empty(), "waveform shows the new sample");
        check (editor->getInfoLabel().getText().startsWith ("TEST BACKEND (not AI)"), "info marks a test result as not AI");
        check (editor->getInfoLabel().getText().contains ("seed 4242"), "info shows the seed");
        check (editor->getHistoryList().getNumEntries() == 1, "history list shows the generation");
        check (editor->getPlayButton().isEnabled() && editor->getDragButton().isEnabled(), "Play and Drag WAV enabled with a sample");

        // Drag-out: the history row and the editor hand the WAV path to the OS drag.
        const auto file = editor->getHistoryList().fileForRow (0);
        check (file.existsAsFile() && file.hasFileExtension ("wav"), "history row maps to its WAV");
        juce::StringArray files;
        bool canMove = true;
        juce::DragAndDropTarget::SourceDetails details (juce::var (file.getFullPathName()), nullptr, {});
        check (editor->shouldDropFilesWhenDraggedExternally (details, files, canMove) && files.size() == 1 && files[0] == file.getFullPathName() && ! canMove,
               "dragging a row out of the window drags a copy of the WAV file");
        juce::DragAndDropTarget::SourceDetails bogus (juce::var ("not a path"), nullptr, {});
        files.clear();
        check (! editor->shouldDropFilesWhenDraggedExternally (bogus, files, canMove), "non-file drags are not exported");

        // Start knob moves the waveform's start line.
        setParam (p, "start", 40.0f);
        editor->refresh();
        check (std::abs (editor->getWaveform().getStartFraction() - 0.4f) < 1e-4f, "Start shows on the waveform");

        // A second generation, then choosing the first from the history loads it back.
        click (editor->getVariationButton());
        p.getService().waitUntilIdle (20000);
        editor->refresh();
        check (editor->getHistoryList().getNumEntries() == 2, "two history rows");
        if (editor->getHistoryList().getNumEntries() < 2)
            return;
        const auto first = editor->getHistoryList().getEntries()[1];
        editor->getHistoryList().getListBox().selectRow (1);
        editor->getHistoryList().onChoose (first, false);
        editor->refresh();
        check (p.getCurrentSample() != nullptr && p.getCurrentSample()->historyId == first.id, "clicking a history row loads it");
        check (p.getSeed() == first.seed && editor->getSeedEditor().getText() == juce::String (first.seed), "and restores its seed");

        // A broken Python runtime (the worker's self-check) is named as such, with the fix.
        gen::GenerateRequest broken { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "runtime");
        p.getService().startGenerate (broken);
        p.getService().waitUntilIdle (20000);
        editor->refresh();
        check (p.getService().jobStatus().errorCode == "runtime", "runtime self-check failure has the runtime code");
        check (editor->getProgressLabel().getText().startsWith ("Python runtime broken"), "progress line says the runtime is broken");
        check (editor->getDetails().getText().contains ("Reinstall it") && editor->getDetails().getText().contains ("exit code 5"),
               "details carry the worker's reinstall message and exit code");

        // A failure shows red text and the copyable report.
        gen::GenerateRequest crash { "x", gen::ModelCatalog::testBackend, 1, 0.2, 0 };
        setStubMode (rig.home.root, "crash");
        p.getService().startGenerate (crash);
        p.getService().waitUntilIdle (20000);
        editor->refresh();
        check (editor->getProgressLabel().getText().startsWith ("Generation failed"), "progress line reports the failure");
        check (editor->getDetails().getText().contains ("stopped unexpectedly"), "details show the crash report");
        check (editor->getDetails().isReadOnly(), "details are selectable read-only text");
    }

    void writeSnapshots()
    {
        const auto* prefix = std::getenv ("DOOBLRGENERATE_EDITOR_SNAPSHOT");
        if (prefix == nullptr || *prefix == 0)
            return;

        Processor p; // the real Application Support folder
        p.prepareToPlay (48000.0, 256);
        const auto entries = p.getHistory().entries();
        juce::String error;
        for (const auto& e : entries)
            if (! e.isTest && p.loadHistoryEntry (e.id, error))
            {
                p.setPrompt (e.prompt);
                p.setSeed (e.seed);
                p.setSeconds (e.seconds);
                p.setModel (e.model);
                break;
            }

        std::unique_ptr<Editor> editor (dynamic_cast<Editor*> (p.createEditor()));
        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            editor->refresh();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::File file (juce::String (prefix) + "-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            const auto ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
            note ((ok ? "snapshot: " : "snapshot FAILED: ") + str (file.getFullPathName()));
        }
    }

    /** Not a test: DOOBLRGENERATE_LIVE_PROMPTS="a|b|c" (+ DOOBLRGENERATE_LIVE_MODEL, default
        audioldm-s-full-v2) generates each prompt through the real processor, service and worker
        with the real model in the real Application Support folder, and prints the timings. */
    void liveRun()
    {
        const auto* prompts = std::getenv ("DOOBLRGENERATE_LIVE_PROMPTS");
        if (prompts == nullptr || *prompts == 0)
            return;

        const auto* modelEnv = std::getenv ("DOOBLRGENERATE_LIVE_MODEL");
        const juce::String model = modelEnv != nullptr && *modelEnv != 0 ? juce::String (modelEnv) : juce::String (gen::ModelCatalog::audioLdm);
        Processor p;
        p.prepareToPlay (48000.0, 256);
        p.setModel (model);
        std::cout << "\nLIVE RUN (real model, not part of the test count): " << model << ", status "
                  << p.getService().modelStatus (model).text << ", python " << p.getService().findPython().getFullPathName() << "\n";

        for (const auto& prompt : juce::StringArray::fromTokens (prompts, "|", {}))
        {
            p.setPrompt (prompt);
            p.setSeed (1234);
            p.setSeconds (2.0);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            if (! p.generate (false))
            {
                std::cout << "  " << prompt << ": did not start\n";
                continue;
            }
            p.getService().waitUntilIdle (600000);
            const auto wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const auto job = p.getService().jobStatus();
            const auto sample = p.getCurrentSample();
            if (job.result != gen::JobStatus::Result::succeeded || sample == nullptr)
            {
                std::cout << "  " << prompt << ": FAILED\n" << job.error << "\n";
                continue;
            }
            gen::HistoryEntry e;
            p.getHistory().find (sample->historyId, e);
            std::cout << "  \"" << prompt << "\": " << juce::String (wall, 2) << " s wall (worker: total " << juce::String (e.totalSeconds, 2)
                      << " s, sampling " << juce::String (e.generateSeconds, 2) << " s on " << e.device << "), "
                      << juce::String (sample->lengthSeconds(), 2) << " s at " << sample->sampleRate << " Hz, peak "
                      << juce::String (juce::Decibels::gainToDecibels (sample->audio.getMagnitude (0, sample->audio.getNumSamples())), 1)
                      << " dBFS, test=" << (sample->isTest ? "yes" : "no") << "\n    " << sample->file.getFullPathName() << "\n";
        }
    }

    struct Test { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<Test> tests {
        { "Metadata", testMetadata },
        { "ValueText", testValueText },
        { "Sampler", testSampler },
        { "ProcessorMidi", testProcessorMidi },
        { "SampleSlot", testSampleSlot },
        { "History", testHistory },
        { "StateRoundTrip", testStateRoundTrip },
        { "ModelStatus", testModelStatus },
        { "WorkerScript", testWorkerScript },
        { "GenerationEndToEnd", testGenerationEndToEnd },
        { "AudioThreadDuringGeneration", testAudioThreadDuringGeneration },
        { "EditorLayout", testEditorLayout },
        { "EditorBehaviour", testEditorBehaviour },
    };

    int failedTests = 0;
    for (const auto& t : tests)
    {
        const auto before = failedChecks;
        std::cout << t.name << "\n";
        t.run();
        const auto ok = failedChecks == before;
        failedTests += ok ? 0 : 1;
        std::cout << "  " << (ok ? "[PASS] " : "[FAIL] ") << t.name << "\n";
    }

    liveRun();
    writeSnapshots();

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << "/" << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " failed\n";
    return failedTests == 0 ? 0 : 1;
}
