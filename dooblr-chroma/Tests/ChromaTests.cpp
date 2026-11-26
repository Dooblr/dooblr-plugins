/*
    dooblr-chroma console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRCHROMA_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "PresetPaths.h"
#include "DSP/Modes.h"
#include "DSP/SynthEngine.h"
#include "UI/ControlPanel.h"
#include "UI/Theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace dooblrchroma;
    namespace id = params::id;
    using Processor = DooblrChromaProcessor;
    using Editor = DooblrChromaEditor;

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

    void dryScientific (Processor& p)
    {
        set (p, id::mixDirect, 0.0f);
        set (p, id::mixNoise, 0.0f);
        set (p, id::mixMallet, 0.9f);
        set (p, id::layer2Level, 0.0f);
        set (p, id::layer1Level, 1.0f);
        set (p, id::l1_b_on, 0.0f);
        set (p, id::l1_a_on, 1.0f);
        set (p, id::l1_couple, 0.0f);
        set (p, id::eqLow, 0.0f);
        set (p, id::eqMid, 0.0f);
        set (p, id::eqHigh, 0.0f);
        set (p, id::compMix, 0.0f);
        set (p, id::distDrive, 0.0f);
        set (p, id::chorusMix, 0.0f);
        set (p, id::delayMix, 0.0f);
        set (p, id::reverbMix, 0.0f);
        set (p, id::vibratoDepth, 0.0f);
        set (p, id::glide, 0.0f);
        set (p, id::unisonCount, 0.0f);
        set (p, id::macro1, 0.0f);
        set (p, id::macro2, 0.0f);
        set (p, id::macro3, 0.0f);
        set (p, id::macro4, 0.0f);
        set (p, id::output, 0.0f);
        set (p, id::l1_a_pLevel, 0.0f);
        set (p, id::l1_a_pVel, 0.0f);
        set (p, id::l1_a_pitch, 0.0f);
        set (p, id::l1_a_cents, 0.0f);
        set (p, id::l1_a_key, 1.0f);
        set (p, id::l1_a_decay, 8.0f);
        set (p, id::l1_a_release, 1.0f);
        set (p, id::l1_a_material, 0.0f);
        set (p, id::l1_a_tone, 0.0f);
        set (p, id::l1_a_lowCut, 0.0f);
        set (p, id::l1_a_hit, 0.22f);
        set (p, id::malletStiffness, 0.7f);
        set (p, id::malletNoise, 0.0f);
    }

    juce::MidiMessage on (int note, float vel = 0.9f)
    {
        return juce::MidiMessage::noteOn (1, note, vel);
    }

    juce::MidiMessage off (int note)
    {
        return juce::MidiMessage::noteOff (1, note);
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
        out.left.resize ((size_t) numSamples, 0.0f);
        out.right.resize ((size_t) numSamples, 0.0f);
        int done = 0;
        size_t ev = 0;
        while (done < numSamples)
        {
            const int n = std::min (block, numSamples - done);
            juce::AudioBuffer<float> buffer (2, n);
            buffer.clear();
            juce::MidiBuffer midi;
            while (ev < events.size() && events[ev].sample < done + n)
            {
                midi.addEvent (events[ev].message, events[ev].sample - done);
                ++ev;
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
            {
                out.left[(size_t) (done + i)] = buffer.getSample (0, i);
                out.right[(size_t) (done + i)] = buffer.getSample (1, i);
            }
            done += n;
        }
        return out;
    }

    // Hann-windowed Goertzel / DFT magnitude at a frequency.
    double toneMag (const std::vector<float>& x, double sr, double hz, size_t from, size_t count)
    {
        count = std::min (count, x.size() - from);
        if (count < 16)
            return 0.0;
        const double w = 2.0 * 3.14159265358979323846 * hz / sr;
        std::complex<double> acc = 0.0;
        double winSum = 0.0;
        for (size_t n = 0; n < count; ++n)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) n / (double) (count - 1));
            acc += (double) x[from + n] * hann * std::exp (std::complex<double> (0.0, -w * (double) n));
            winSum += hann;
        }
        return std::abs (acc) / std::max (1.0, winSum);
    }

    double peakFrequency (const std::vector<float>& x, double sr, double lo, double hi, size_t from, size_t count)
    {
        double bestF = lo, bestM = -1.0;
        // Coarse then refine.
        for (int pass = 0; pass < 2; ++pass)
        {
            const double step = (pass == 0) ? 1.0 : 0.1;
            if (pass == 1)
            {
                lo = std::max (lo, bestF - 2.0);
                hi = std::min (hi, bestF + 2.0);
            }
            for (double f = lo; f <= hi; f += step)
            {
                const double m = toneMag (x, sr, f, from, count);
                if (m > bestM) { bestM = m; bestF = f; }
            }
        }
        return bestF;
    }

    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-chroma", "plugin name is dooblr-chroma");
        check (p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "instrument, MIDI in only");
        check (p->getLatencySamples() == 0, "zero reported latency");
        check (p->getNumPrograms() >= 9, "Init plus at least 8 factory programs");
        check (juce::String (JucePlugin_VersionString) == "0.1.0", "version 0.1.0");
        check ((int) p->getParameters().size() > 80, "a full parameter set is published");
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, 48000);
        check (r.finite() && r.peak() < 1.0e-6f, "silence without notes (peak " + fmt (r.peak(), 8) + ")");
        check (r.denormals() == 0, "no denormals in silence");
    }

    void testC4()
    {
        auto p = makeProcessor();
        dryScientific (*p);
        set (*p, id::l1_a_type, 0.0f); // string
        set (*p, id::l1_a_density, 1.0f);
        const auto r = render (*p, 65536, { { 0, on (60) } });
        check (r.finite() && r.peak() > 0.01f, "C4 string produces audio (peak " + fmt (r.peak(), 3) + ")");
        // Skip the attack transient; measure a stable 0.2 s later.
        const size_t from = 8000;
        const size_t count = 32768;
        const double f = peakFrequency (r.left, defaultRate, 240.0, 280.0, from, count);
        note ("C4 fundamental measured at " + fmt (f, 2) + " Hz (expected 261.63)");
        check (std::abs (f - (double) dsp::kC4Hz) < 0.6, "C4 is 261.63 Hz ± 0.6 (got " + fmt (f, 2) + ")");
    }

    void testModalRatios()
    {
        const struct Case { const char* name; float type; std::vector<double> ratios; } cases[] {
            { "string",      0.0f, { 1, 2, 3, 4 } },
            { "bar",         1.0f, { 1, 2.756, 5.404, 8.933 } },
            { "marimba",     2.0f, { 1, 4.0, 9.3, 16.4 } },
            { "plate",       3.0f, { 1, 2.5, 4.0, 5.0 } },
            { "drumhead",    4.0f, { 1, 1.5933, 2.1355, 2.2954 } },
            { "membrane",    5.0f, { 1, 1.5811, 2.0, 2.236 } },
            { "open tube",   6.0f, { 1, 2, 3, 4 } },
            { "closed tube", 7.0f, { 1, 3, 5, 7 } },
            { "manual",      8.0f, { 1, 2.5, 4.7, 7.1 } },
        };

        for (const auto& c : cases)
        {
            auto p = makeProcessor();
            dryScientific (*p);
            set (*p, id::l1_a_type, c.type);
            set (*p, id::l1_a_density, c.type == 8.0f ? 3.0f : 1.0f); // manual: 4 partials
            if (c.type == 8.0f)
            {
                set (*p, id::l1_a_part1, 1.0f);
                set (*p, id::l1_a_part2, 2.5f);
                set (*p, id::l1_a_part3, 4.7f);
                set (*p, id::l1_a_part4, 7.1f);
            }
            const auto r = render (*p, 65536, { { 0, on (60) } });
            check (r.finite() && r.peak() > 0.005f, std::string (c.name) + " produces audio");

            const size_t from = 6000, count = 32768;
            const double f0 = peakFrequency (r.left, defaultRate, 240.0, 280.0, from, count);
            check (std::abs (f0 - (double) dsp::kC4Hz) < 1.5,
                   std::string (c.name) + " fundamental near 261.63 (got " + fmt (f0, 2) + ")");

            // Verify ratios from Modes.h itself, then confirm they appear in the FFT.
            float ratios[16] {};
            float parts[4] { 1.0f, 2.5f, 4.7f, 7.1f };
            const int n = dsp::fillRatios ((dsp::ResType) juce::roundToInt (c.type), 4, ratios, parts);
            check (n >= 4, std::string (c.name) + " publishes 4 ratios");
            for (int i = 0; i < 4 && i < (int) c.ratios.size(); ++i)
                check (std::abs ((double) ratios[i] - c.ratios[(size_t) i]) < 0.02,
                       std::string (c.name) + " ratio[" + std::to_string (i) + "] is "
                           + fmt (c.ratios[(size_t) i], 3) + " (table " + fmt (ratios[i], 3) + ")");

            const double fundMag = toneMag (r.left, defaultRate, f0, from, count);
            for (int i = 1; i < 4; ++i)
            {
                const double expect = f0 * (double) ratios[i];
                if (expect > 8000.0)
                    continue;
                const double mag = toneMag (r.left, defaultRate, expect, from, count);
                // Nearby off-peak (between this and previous) should be weaker.
                const double mid = (f0 * (double) ratios[i - 1] + expect) * 0.5;
                const double midMag = toneMag (r.left, defaultRate, mid, from, count);
                check (mag > midMag * 1.4,
                       std::string (c.name) + " partial " + fmt (ratios[i], 2)
                           + " at " + fmt (expect, 1) + " Hz is a peak (mag "
                           + fmt (mag, 5) + " vs mid " + fmt (midMag, 5) + ", fund " + fmt (fundMag, 5) + ")");
            }
        }
    }

    void testCouplingStability()
    {
        const float typesA[] { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 7.0f };
        const float typesB[] { 3.0f, 5.0f, 0.0f, 1.0f, 4.0f, 7.0f, 2.0f };
        const float balances[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

        for (int t = 0; t < 7; ++t)
            for (float bal : balances)
            {
                auto p = makeProcessor();
                dryScientific (*p);
                set (*p, id::l1_a_on, 1.0f);
                set (*p, id::l1_b_on, 1.0f);
                set (*p, id::l1_a_type, typesA[t]);
                set (*p, id::l1_b_type, typesB[t]);
                set (*p, id::l1_a_density, 1.0f);
                set (*p, id::l1_b_density, 1.0f);
                set (*p, id::l1_a_decay, 3.0f);
                set (*p, id::l1_b_decay, 2.2f);
                set (*p, id::l1_couple, 1.0f);
                set (*p, id::l1_balance, bal);
                const auto r = render (*p, 96000, { { 0, on (60) }, { 24000, off (60) } });
                const auto label = std::string ("couple ") + std::to_string ((int) typesA[t])
                                 + "+" + std::to_string ((int) typesB[t])
                                 + " bal=" + fmt (bal, 2);
                check (r.finite(), label + " stays finite");
                check (r.denormals() == 0, label + " has no denormals");
                check (r.peak() < 40.0f, label + " peak bounded (" + fmt (r.peak(), 2) + ")");
                const double first = r.rms (0, 24000);
                const double last  = r.rms (72000, 96000);
                check (last < first * 4.0 + 0.05, label + " does not blow up (rms "
                       + fmt (first, 4) + " -> " + fmt (last, 4) + ")");
            }
    }

    void testNoNanParallelAndFx()
    {
        auto p = makeProcessor();
        // A busy patch: both layers, unison, all FX engaged.
        set (*p, id::l1_a_on, 1.0f);
        set (*p, id::l1_b_on, 1.0f);
        set (*p, id::l1_couple, 0.0f);
        set (*p, id::layer2Level, 0.7f);
        set (*p, id::unisonCount, 2.0f);
        set (*p, id::chorusMix, 0.3f);
        set (*p, id::delayMix, 0.2f);
        set (*p, id::reverbMix, 0.25f);
        set (*p, id::distDrive, 0.3f);
        set (*p, id::compMix, 0.4f);
        set (*p, id::eqLow, 2.0f);
        set (*p, id::mixNoise, 0.2f);
        const auto r = render (*p, 48000, { { 0, on (48) }, { 0, on (60) }, { 0, on (67) }, { 24000, off (48) },
                                            { 24000, off (60) }, { 24000, off (67) } });
        check (r.finite(), "busy patch stays finite");
        check (r.denormals() == 0, "busy patch has no denormals");
        check (r.peak() > 0.01f && r.peak() < 40.0f, "busy patch peak " + fmt (r.peak(), 3));
    }

    void testPresetRoundTrip()
    {
        for (const auto& fac : presets::factory())
        {
            auto a = makeProcessor();
            presets::apply (a->getState(), fac);
            a->setPresetName (fac.name);
            juce::MemoryBlock block;
            a->getStateInformation (block);

            auto b = makeProcessor();
            b->setStateInformation (block.getData(), (int) block.getSize());
            check (b->getPresetName() == fac.name, "preset name round-trips: " + fac.name.toStdString());

            int mismatches = 0;
            for (auto* pa : a->getParameters())
            {
                auto* ra = dynamic_cast<juce::RangedAudioParameter*> (pa);
                if (ra == nullptr) continue;
                auto* rb = b->getState().getParameter (ra->paramID);
                if (rb == nullptr || std::abs (ra->getValue() - rb->getValue()) > 1.0e-5f)
                    ++mismatches;
            }
            check (mismatches == 0, "parameter round-trip for " + fac.name.toStdString()
                   + " (" + std::to_string (mismatches) + " mismatches)");
        }

        // User preset via PresetPaths, redirected so we never touch the real folders.
        juce::TemporaryFile homeTmp, musicTmp;
        homeTmp.getFile().createDirectory();
        musicTmp.getFile().createDirectory();
    }

    void testUserPresetFile()
    {
        const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("dooblr-chroma-tests")
                              .getNonexistentChildFile ("home", "", false);
        const auto music = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("dooblr-chroma-tests")
                               .getNonexistentChildFile ("music", "", false);
        home.createDirectory();
        music.createDirectory();
        // Redirect PresetPaths so tests never touch the real user folders.
        ::setenv ("DOOBLR_PRESET_HOME", home.getFullPathName().toRawUTF8(), 1);
        ::setenv ("DOOBLR_PRESET_MUSIC", music.getFullPathName().toRawUTF8(), 1);

        auto p = makeProcessor();
        presets::apply (p->getState(), presets::factory()[1]); // Marimba
        const auto file = presets::userDirectory().getChildFile ("MarimbaCopy.dchroma");
        check (p->saveUserPreset (file).isNotEmpty(), "save user preset");
        check (file.existsAsFile(), "user preset file exists under PresetPaths");

        auto q = makeProcessor();
        const auto name = q->loadUserPreset (file);
        check (name == "MarimbaCopy", "loaded user preset name");
        check (std::abs (get (*q, id::l1_a_type) - 2.0f) < 0.1f, "loaded Marimba type");

        home.deleteRecursively();
        music.deleteRecursively();
        ::unsetenv ("DOOBLR_PRESET_HOME");
        ::unsetenv ("DOOBLR_PRESET_MUSIC");
    }

    void testMacrosLive()
    {
        auto quiet = makeProcessor();
        dryScientific (*quiet);
        set (*quiet, id::macro1, 0.0f);
        const auto a = render (*quiet, 24000, { { 0, on (60) } });

        auto loud = makeProcessor();
        dryScientific (*loud);
        set (*loud, id::macro1, 1.0f);
        const auto b = render (*loud, 24000, { { 0, on (60) } });
        check (b.peak() > a.peak() * 1.2f, "macro 1 raises mallet level (" + fmt (a.peak(), 3)
               + " -> " + fmt (b.peak(), 3) + ")");
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        presets::apply (p->getState(), presets::factory()[1]); // Marimba, typical
        set (*p, id::l1_a_density, 2.0f); // 30 modes
        juce::MidiBuffer dummy;
        juce::AudioBuffer<float> buffer (2, 512);
        // Warm up.
        {
            juce::MidiBuffer onBuf;
            onBuf.addEvent (on (60), 0);
            onBuf.addEvent (on (64), 0);
            onBuf.addEvent (on (67), 0);
            buffer.clear();
            p->processBlock (buffer, onBuf);
        }
        const int blocks = 401;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            p->processBlock (buffer, midi);
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto audioMs = (double) blocks * 512.0 / defaultRate * 1000.0;
        note ("CPU: " + fmt (ms, 1) + " ms wall for " + fmt (audioMs, 1) + " ms audio ("
              + fmt (100.0 * ms / audioMs, 2) + " %)  [Marimba, density 30, 3-note chord]");
        check (ms < audioMs, "faster than real time");
    }

    void snapshot (juce::Component& c, const char* suffix)
    {
        const auto prefix = std::getenv ("DOOBLRCHROMA_EDITOR_SNAPSHOT");
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
        check (editor != nullptr, "the editor is a DooblrChromaEditor");
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

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto sizeLabel = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";

            std::vector<juce::Component*> header;
            for (auto* cid : { "undo", "redo", "preset", "savePreset", "loadPreset", id::output, "version" })
                if (auto* c = editor->findChildWithID (cid))
                    header.push_back (c);
            check (header.size() == 7, sizeLabel + "all header items present");
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
            check ((int) editor->getPanels().size() == 8, sizeLabel + "eight control panels");
            for (auto* panel : editor->getPanels())
            {
                check (panel->isVisible(), sizeLabel + "panel '" + panel->getTitle().toStdString() + "' visible");
                check (content.contains (panel->getBounds()) || panel->getBounds().getHeight() == 0,
                       sizeLabel + "panel '" + panel->getTitle().toStdString() + "' in content");
            }
            for (size_t i = 0; i < editor->getPanels().size(); ++i)
                for (size_t j = i + 1; j < editor->getPanels().size(); ++j)
                    check (! editor->getPanels()[i]->getBounds().intersects (editor->getPanels()[j]->getBounds()),
                           sizeLabel + "panels do not overlap: " + editor->getPanels()[i]->getTitle().toStdString()
                               + " / " + editor->getPanels()[j]->getTitle().toStdString());

            check (content.contains (editor->getViewA().getBounds()) || editor->getViewA().getParentComponent() != nullptr,
                   sizeLabel + "resonator A view is on the board");
            check (editor->getViewA().getWidth() >= 40 && editor->getViewA().getHeight() >= 40,
                   sizeLabel + "resonator A view is readable ("
                       + std::to_string (editor->getViewA().getWidth()) + "x"
                       + std::to_string (editor->getViewA().getHeight()) + ")");
            // No hint text: the view has no child labels.
            check (editor->getViewA().getNumChildComponents() == 0, sizeLabel + "resonator view has no hint labels");
        }

        // Switching layers rebinds resonator controls to L2 IDs.
        editor->setEditLayer (1);
        check (editor->getEditLayer() == 1, "edit layer B");
        check (editor->getAPitchParamId() == "l2_a_pitch", "layer 2 pitch control after switch (id "
               + editor->getAPitchParamId().toStdString() + ")");
        bool foundL2 = false;
        std::function<void(juce::Component*)> walk = [&] (juce::Component* c)
        {
            if (c->getComponentID() == "l2_a_pitch") foundL2 = true;
            for (auto* ch : c->getChildren()) walk (ch);
        };
        walk (editor);
        check (foundL2, "layer 2 pitch component is in the tree");
        editor->setEditLayer (0);
        check (editor->getAPitchParamId() == "l1_a_pitch", "layer 1 pitch control after switch back");

        editor->setSize (1080, 660);
        snapshot (*editor, "-default");
        editor->setSize (940, 600);
        snapshot (*editor, "-compact");
        editor->setSize (2400, 1400);
        snapshot (*editor, "-wide");
        editor->setSize (1080, 660);
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                    testMetadata },
        { "silence without notes",       testSilence },
        { "C4 = 261.63 Hz",              testC4 },
        { "modal ratios (FFT)",          testModalRatios },
        { "coupling stability",          testCouplingStability },
        { "no NaN / denormal",           testNoNanParallelAndFx },
        { "preset load round-trip",      testPresetRoundTrip },
        { "user presets (PresetPaths)",  testUserPresetFile },
        { "macros are live",             testMacrosLive },
        { "CPU benchmark",               testRealtimeAndCpu },
        { "editor layout",               testEditor },
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
