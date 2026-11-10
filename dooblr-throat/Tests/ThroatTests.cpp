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
