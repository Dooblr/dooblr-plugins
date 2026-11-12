/*
    dooblr-rhythm console tests: the real processor, rhythm engine, synth and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.

    Set DOOBLRRHYTHM_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots
    at 940x600, 1080x660 and 2400x1400.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Engine/Clave.h"
#include "Engine/RhythmEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <tuple>
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
    using namespace dooblrrhythm;
    using namespace dooblrrhythm::params;
    using Processor = DooblrRhythmProcessor;
    using Editor = DooblrRhythmEditor;

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
    constexpr int defaultBlock = 512;

    std::unique_ptr<Processor> makeProcessor (double rate = defaultRate, int block = defaultBlock)
    {
        auto p = std::make_unique<Processor>();
        p->setRateAndBufferSizeDetails (rate, block);
        p->prepareToPlay (rate, block);
        return p;
    }

    juce::RangedAudioParameter* param (Processor& p, const juce::String& pid)
    {
        auto* q = p.getState().getParameter (pid);
        if (q == nullptr)
            check (false, "parameter " + pid.toStdString() + " exists");
        return q;
    }

    void set (Processor& p, const juce::String& pid, float value)
    {
        if (auto* q = param (p, pid))
            q->setValueNotifyingHost (q->convertTo0to1 (value));
    }

    float get (Processor& p, const juce::String& pid)
    {
        auto* q = param (p, pid);
        return q != nullptr ? q->convertFrom0to1 (q->getValue()) : 0.0f;
    }

    void defaults (Processor& p)
    {
        for (auto* q : p.getParameters())
            q->setValueNotifyingHost (q->getDefaultValue());
    }

    GenerateRequest baseRequest (Style style = Style::sonSalsa, uint32_t seed = 1)
    {
        GenerateRequest r;
        r.style = style;
        r.seed = seed;
        r.bars = 4;
        r.timeSignature = style == Style::afroCuban68 ? TimeSignature::sixEight
                        : style == Style::westAfrican ? TimeSignature::twelveEight
                        : TimeSignature::fourFour;
        r.clave = ClaveDirection::threeTwo;
        r.section = Section::main;
        for (int i = 0; i < numInstruments; ++i)
        {
            r.instruments[(size_t) i].density = inStyleEnsemble (style, (Instrument) i) ? 80.0f : 0.0f;
            r.instruments[(size_t) i].complexity = 40.0f;
            r.instruments[(size_t) i].syncopation = 35.0f;
            r.instruments[(size_t) i].velocity = 80.0f;
        }
        return r;
    }

    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-rhythm", "name is dooblr-rhythm (got " + p->getName().toStdString() + ")");
        check (JucePlugin_IsSynth == 1 && p->acceptsMidi() && p->producesMidi() && ! p->isMidiEffect(),
               "an instrument with MIDI in and out");
        check (p->getMainBusNumOutputChannels() == 2, "stereo out by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRRHYTHM_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.1.1", "version 0.1.1");
        check ((int) p->getParameters().size() == numHostParameters,
               "host parameter count " + std::to_string (p->getParameters().size())
                   + " expected " + std::to_string (numHostParameters));
        note ("version " JucePlugin_VersionString ", " + std::to_string (p->getParameters().size()) + " params");
    }

    void testClavePresentAndCorrect()
    {
        for (auto style : { Style::sonSalsa, Style::rumba, Style::afrobeat })
        {
            for (auto dir : { ClaveDirection::threeTwo, ClaveDirection::twoThree })
            {
                auto r = baseRequest (style, 7);
                r.clave = dir;
                r.instruments[(size_t) Instrument::clave].density = 100.0f;
                const auto p = generate (r);
                const auto grid = keyPatternFor (style, dir, r.timeSignature);
                int missing = 0, extra = 0, found = 0;
                for (int step = 0; step < p.totalSteps(); ++step)
                {
                    const bool should = isKeyStroke (grid, step);
                    const bool has = p.hasHit ((int) Instrument::clave, step);
                    if (should && ! has) ++missing;
                    if (has) ++found;
                    if (has && ! should) ++extra;
                }
                const auto tag = std::string (styleNames[(int) style]) + " " + claveNames[(int) dir];
                check (found > 0, tag + " has clave hits");
                check (missing == 0, tag + " clave has every canonical stroke (missing " + std::to_string (missing) + ")");
                check (extra == 0, tag + " clave has no extra strokes (extra " + std::to_string (extra) + ")");
            }
        }

        auto six = baseRequest (Style::afroCuban68, 3);
        six.instruments[(size_t) Instrument::gankogui].density = 100.0f;
        const auto p6 = generate (six);
        const auto bell = keyPatternFor (Style::afroCuban68, ClaveDirection::threeTwo, TimeSignature::sixEight);
        int miss = 0;
        for (int step = 0; step < p6.totalSteps(); ++step)
            if (isKeyStroke (bell, step) && ! p6.hasHit ((int) Instrument::gankogui, step)
                && ! p6.hasHit ((int) Instrument::clave, step))
                ++miss;
        check (miss == 0, "Afro-Cuban 6/8 has the 6/8 bell/clave on every key stroke");
    }

    void testTumbaoOpensClaveConsistent()
    {
        for (auto dir : { ClaveDirection::threeTwo, ClaveDirection::twoThree })
        {
            auto r = baseRequest (Style::sonSalsa, 11);
            r.clave = dir;
            r.instruments[(size_t) Instrument::tumba].density = 100.0f;
            const auto p = generate (r);
            int opens = 0, consistent = 0, inconsistent = 0;
            for (const auto& h : p.hits)
            {
                if (h.instrument != (uint8_t) Instrument::tumba)
                    continue;
                if (h.articulation != (uint8_t) Articulation::open)
                    continue;
                ++opens;
                if (isClaveConsistentOpen (r.style, r.clave, r.timeSignature, (int) h.step))
                    ++consistent;
                else
                    ++inconsistent;
            }
            const auto tag = std::string ("tumbao ") + claveNames[(int) dir];
            check (opens >= p.bars, tag + " has open tones (" + std::to_string (opens) + ")");
            check (inconsistent == 0, tag + " every open is clave-consistent (bad " + std::to_string (inconsistent) + ")");
            check (consistent == opens, tag + " opens match clave-consistent steps");
        }
    }

    void testNoStackingBeyondThreshold()
    {
        constexpr int maxSame = 2;
        for (int style = 0; style < (int) Style::count; ++style)
        {
            auto r = baseRequest ((Style) style, 19 + (uint32_t) style);
            r.complexity = 80.0f;
            r.syncopation = 70.0f;
            for (auto& c : r.instruments) c.density = 90.0f;
            const auto p = generate (r);
            int violations = 0;
            const int total = p.totalSteps();
            for (int step = 0; step < total; ++step)
            {
                int bass = 0, mid = 0, high = 0;
                for (const auto& h : p.hits)
                {
                    if ((int) h.step != step) continue;
                    switch (instrumentRegister ((Instrument) h.instrument))
                    {
                        case Register::bass: ++bass; break;
                        case Register::mid:  ++mid;  break;
                        case Register::high: ++high; break;
                    }
                }
                if (bass > maxSame || mid > maxSame || high > maxSame)
                    ++violations;
            }
            check (violations == 0, std::string (styleNames[style]) + " never stacks more than "
                   + std::to_string (maxSame) + " same-register onsets on a step (" + std::to_string (violations) + ")");
        }
    }

    void testDensityComplexitySyncopationMonotonic()
    {
        auto run = [] (float dens, float comp, float sync)
        {
            auto r = baseRequest (Style::sonSalsa, 23);
            r.complexity = comp;
            r.syncopation = sync;
            for (auto& c : r.instruments)
            {
                c.density = dens;
                c.complexity = comp;
                c.syncopation = sync;
            }
            r.instruments[(size_t) Instrument::clave].density = 100.0f;
            return generate (r);
        };

        const auto d20 = run (20, 40, 40);
        const auto d50 = run (50, 40, 40);
        const auto d80 = run (80, 40, 40);
        check (d20.hits.size() <= d50.hits.size() && d50.hits.size() <= d80.hits.size(),
               "density monotonically increases hit count "
                   + std::to_string (d20.hits.size()) + " <= " + std::to_string (d50.hits.size())
                   + " <= " + std::to_string (d80.hits.size()));

        auto ornaments = [] (const Pattern& p)
        {
            int n = 0;
            for (const auto& h : p.hits) n += h.structural == 0;
            return n;
        };
        const auto c20 = run (70, 15, 40);
        const auto c50 = run (70, 50, 40);
        const auto c80 = run (70, 85, 40);
        check (ornaments (c20) <= ornaments (c50) && ornaments (c50) <= ornaments (c80),
               "complexity monotonically increases ornaments "
                   + std::to_string (ornaments (c20)) + " <= " + std::to_string (ornaments (c50))
                   + " <= " + std::to_string (ornaments (c80)));

        auto offbeats = [] (const Pattern& p)
        {
            int n = 0;
            const int spb = juce::jmax (1, p.stepsPerBar);
            const int beats = juce::jmax (1, beatsPerBar (p.timeSignature));
            const int spBeat = juce::jmax (1, spb / beats);
            for (const auto& h : p.hits)
                if (h.structural < 2 && ((int) h.step % spBeat) != 0)
                    ++n;
            return n;
        };
        const auto s10 = run (70, 40, 10);
        const auto s50 = run (70, 40, 50);
        const auto s90 = run (70, 40, 90);
        check (offbeats (s10) <= offbeats (s50) && offbeats (s50) <= offbeats (s90),
               "syncopation monotonically increases offbeat hits "
                   + std::to_string (offbeats (s10)) + " <= " + std::to_string (offbeats (s50))
                   + " <= " + std::to_string (offbeats (s90)));
    }

    void testPatternLock()
    {
        auto r = baseRequest (Style::sonSalsa, 31);
        auto first = generate (r);
        r.previous = &first;
        r.instruments[(size_t) Instrument::tumba].locked = true;
        r.seed = 99;
        auto second = generate (r);

        std::vector<Hit> a, b;
        first.forInstrument ((int) Instrument::tumba, [&] (const Hit& h) { a.push_back (h); });
        second.forInstrument ((int) Instrument::tumba, [&] (const Hit& h) { b.push_back (h); });
        check (a.size() == b.size() && a.size() > 0, "locked tumba is copied (" + std::to_string (a.size()) + " hits)");
        bool same = a.size() == b.size();
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            same = same && a[i].step == b[i].step && a[i].articulation == b[i].articulation
                && std::abs (a[i].velocity - b[i].velocity) < 1.0e-5f;
        check (same, "locked tumba is identical across regenerate");

        bool claveMoved = false;
        std::vector<int> c1, c2;
        first.forInstrument ((int) Instrument::conga, [&] (const Hit& h) { c1.push_back ((int) h.step); });
        second.forInstrument ((int) Instrument::conga, [&] (const Hit& h) { c2.push_back ((int) h.step); });
        claveMoved = c1 != c2;
        check (claveMoved || c1.empty(), "unlocked conga is allowed to change when the seed changes");
    }

    void testFillsResolveOnDownbeat()
    {
        auto r = baseRequest (Style::sonSalsa, 41);
        r.section = Section::main;
        const auto mainP = generate (r);
        r.section = Section::fill;
        r.previous = nullptr;
        const auto fillP = generate (r);

        const int spb = fillP.stepsPerBar;
        const int lastBar = (fillP.bars - 1) * spb;
        int fillLast = 0, mainLast = 0, fillDown = 0, extraDown = 0;
        for (const auto& h : fillP.hits)
        {
            if ((int) h.step >= lastBar) ++fillLast;
            if ((int) h.step == 0) ++fillDown;
        }
        for (const auto& h : mainP.hits)
        {
            if ((int) h.step >= lastBar) ++mainLast;
            if ((int) h.step == 0 && h.structural == 0) ++extraDown;
        }
        int fillOrnamentsOnZero = 0;
        for (const auto& h : fillP.hits)
            if ((int) h.step == 0 && h.structural == 0)
                ++fillOrnamentsOnZero;

        check (fillP.hits.size() > 0, "fill generates hits");
        check (fillLast >= mainLast, "fill densifies the last bar (" + std::to_string (fillLast)
               + " vs main " + std::to_string (mainLast) + ")");
        check (fillOrnamentsOnZero == 0, "fill does not put extra ornaments on the loop downbeat");
        juce::ignoreUnused (fillDown, extraDown);

        for (const auto& h : fillP.hits)
            check ((int) h.step < fillP.totalSteps(), "fill hits stay inside the pattern");
    }

    void testDeterministicSeed()
    {
        auto r = baseRequest (Style::sambaBatucada, 53);
        const auto a = generate (r);
        const auto b = generate (r);
        check (a == b, "same seed produces an identical pattern");
        r.seed = 54;
        const auto c = generate (r);
        check (! (a == c), "a different seed produces a different pattern");
    }

    void testMidiRoundTrip()
    {
        auto r = baseRequest (Style::sonSalsa, 61);
        const auto p = generate (r);
        const auto midi = patternToMidi (p, 120.0);
        auto back = patternFromMidi (midi, p);
        check (back.hits.size() == p.hits.size(),
               "MIDI round-trip hit count " + std::to_string (back.hits.size()) + " == " + std::to_string (p.hits.size()));

        auto key = [] (const Hit& h)
        {
            return std::tuple<int, int, int> ((int) h.step, (int) h.instrument,
                                              midiNoteFor ((Instrument) h.instrument, (Articulation) h.articulation));
        };
        std::vector<Hit> a = p.hits, b = back.hits;
        std::sort (a.begin(), a.end(), [&] (auto x, auto y) { return key (x) < key (y); });
        std::sort (b.begin(), b.end(), [&] (auto x, auto y) { return key (x) < key (y); });
        int mismatch = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        {
            if (a[i].instrument != b[i].instrument || a[i].step != b[i].step)
                ++mismatch;
            else
            {
                const int na = midiNoteFor ((Instrument) a[i].instrument, (Articulation) a[i].articulation);
                const int nb = midiNoteFor ((Instrument) b[i].instrument, (Articulation) b[i].articulation);
                if (na != nb) ++mismatch;
            }
        }
        check (mismatch == 0, "MIDI round-trip preserves instrument, step and note (" + std::to_string (mismatch) + " mismatches)");
    }

    void testStateRoundTrip()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::style, 3.0f);
        set (*p, id::complexity, 77.0f);
        set (*p, instrumentParam ((int) Instrument::tumba, "density"), 90.0f);
        p->generateArrangement (123);
        const auto before = p->currentPattern();
        juce::MemoryBlock blob;
        p->getStateInformation (blob);
        defaults (*p);
        p->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*p, id::complexity) - 77.0f) < 0.2f, "state restores Complexity");
        check (std::abs (get (*p, id::style) - 3.0f) < 0.2f, "state restores Style");
        check (p->currentPattern() == before, "state restores the generated pattern");
        check (p->currentSeed() == 123, "state restores the seed");
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        defaults (*p);
        p->generateArrangement (3);
        p->setPreviewPlaying (true);

        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        midi.ensureSize (1 << 16);
        // Warm up (prepare voices, first-hit caches, MIDI buffer capacity).
        for (int i = 0; i < 8; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
        }

        allocationCount.store (0);
        countAllocations.store (true);
        const auto nBlocks = (int) std::ceil (2.0 * defaultRate / defaultBlock);
        const auto t0 = std::chrono::steady_clock::now();
        bool finite = true;
        for (int i = 0; i < nBlocks; ++i)
        {
            buffer.clear();
            midi.clear();
            p->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < defaultBlock; ++s)
                    finite = finite && std::isfinite (buffer.getSample (c, s));
        }
        countAllocations.store (false);
        const auto t1 = std::chrono::steady_clock::now();
        const auto allocs = allocationCount.load();
        const auto audioSec = (double) nBlocks * defaultBlock / defaultRate;
        const auto wallSec = std::chrono::duration<double> (t1 - t0).count();
        check (finite, "CPU run stays finite");
        check (allocs == 0, "processBlock allocates nothing after prepare (" + std::to_string (allocs) + ")");
        check (wallSec < audioSec * 2.5, "CPU sanity: " + fmt (wallSec, 3) + " s wall for " + fmt (audioSec, 2) + " s audio");
        note ("CPU " + fmt (100.0 * wallSec / audioSec, 1) + " % of realtime, " + std::to_string (allocs) + " allocations");
        p->setPreviewPlaying (false);
    }

    bool isControl (juce::Component& c)
    {
        return dynamic_cast<juce::Slider*> (&c) != nullptr
            || dynamic_cast<juce::ToggleButton*> (&c) != nullptr
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr
            || dynamic_cast<ui::SegmentedToggle*> (&c) != nullptr;
    }

    void forEachVisible (juce::Component& root, const std::function<void (juce::Component&)>& fn)
    {
        if (! root.isVisible()) return;
        fn (root);
        for (auto* child : root.getChildren())
            if (child != nullptr)
                forEachVisible (*child, fn);
    }

    void snapshot (juce::Component& c, const char* suffix)
    {
        const auto prefix = std::getenv ("DOOBLRRHYTHM_EDITOR_SNAPSHOT");
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
        check (editor != nullptr, "createEditor returns DooblrRhythmEditor");
        if (editor == nullptr)
            return;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660,
               "default size 1080 x 660 (" + std::to_string (editor->getWidth()) + " x " + std::to_string (editor->getHeight()) + ")");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600
                   && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400,
                   "resize limits 940 x 600 .. 2400 x 1400");
        else
            check (false, "the editor is resizable with limits");

        auto* version = dynamic_cast<juce::Label*> (editor->findChildWithID ("version"));
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString),
               "the version label shows " JucePlugin_VersionString);

        int missing = 0;
        for (auto* raw : p->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            if (editor->findControl (pid) == nullptr)
            {
                ++missing;
                check (false, "parameter " + pid.toStdString() + " has a control");
            }
        }
        check (missing == 0, "every parameter has a control");

        const juce::Point<int> sizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
        const char* suffixes[] { "-compact", "-default", "-wide" };
        int si = 0;
        for (auto size : sizes)
        {
            editor->setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);

            if (auto* meter = editor->findChildWithID ("outputMeter"))
            {
                const auto r = editor->getLocalArea (meter->getParentComponent(), meter->getBounds());
                check (r.getWidth() >= 32, label + ": output meter width " + r.toString().toStdString());
                check (editor->getWidth() - r.getRight() >= 24,
                       label + ": output meter right padding " + r.toString().toStdString());
            }
            else
                check (false, label + ": output meter is present");

            if (auto* caption = editor->findChildWithID ("outputCaption"))
            {
                const auto r = editor->getLocalArea (caption->getParentComponent(), caption->getBounds());
                check (r.getWidth() >= 48, label + ": Output caption width " + r.toString().toStdString());
                if (auto* slider = editor->findChildWithID ("output"))
                {
                    const auto s = editor->getLocalArea (slider->getParentComponent(), slider->getBounds());
                    check (r.getIntersection (s).isEmpty(),
                           label + ": Output caption does not overlap the slider "
                           + r.toString().toStdString() + " / " + s.toString().toStdString());
                }
            }
            else
                check (false, label + ": Output caption is present");

            if (auto* variation = editor->findChildWithID ("variation"))
            {
                const auto r = editor->getLocalArea (variation->getParentComponent(), variation->getBounds());
                check (r.getWidth() >= 100 && r.getHeight() >= 24,
                       label + ": Variation button large enough (" + r.toString().toStdString() + ")");
            }
            else
                check (false, label + ": Variation button is present");
            std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> rects;
            forEachVisible (*editor, [&] (juce::Component& c)
            {
                if (! isControl (c)) return;
                const auto r = editor->getLocalArea (c.getParentComponent(), c.getBounds());
                // Scrolled-off sequencer rows sit at y < 0 or below the grid; skip those.
                if (r.getY() < 0 || r.getBottom() > editor->getHeight() || r.getX() < 0)
                    return;
                rects.push_back ({ &c, r });
            });
            int overlaps = 0, outside = 0, tooSmall = 0;
            for (size_t i = 0; i < rects.size(); ++i)
            {
                const auto& [c, r] = rects[i];
                if (! editor->getLocalBounds().contains (r))
                {
                    ++outside;
                    check (false, label + ": " + c->getComponentID().toStdString() + " inside the window "
                           + r.toString().toStdString());
                }
                if (r.getWidth() < 16 || r.getHeight() < 14)
                {
                    ++tooSmall;
                    check (false, label + ": " + c->getComponentID().toStdString() + " large enough ("
                           + r.toString().toStdString() + ")");
                }
                for (size_t j = i + 1; j < rects.size(); ++j)
                {
                    const auto& [d, s] = rects[j];
                    if (c->isParentOf (d) || d->isParentOf (c)) continue;
                    if (! r.getIntersection (s).isEmpty())
                    {
                        ++overlaps;
                        check (false, label + ": " + c->getComponentID().toStdString() + " overlaps "
                               + d->getComponentID().toStdString() + " " + r.toString().toStdString()
                               + " / " + s.toString().toStdString());
                    }
                }
            }
            check (overlaps == 0 && outside == 0 && tooSmall == 0,
                   label + " layout fit (" + std::to_string (rects.size()) + " visible controls)");
            snapshot (*editor, suffixes[si++]);
        }
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                      testMetadata },
        { "clave present and canonical",                   testClavePresentAndCorrect },
        { "tumbao opens are clave-consistent",             testTumbaoOpensClaveConsistent },
        { "no same-register stacking above 2",             testNoStackingBeyondThreshold },
        { "density / complexity / syncopation monotonic",  testDensityComplexitySyncopationMonotonic },
        { "pattern lock survives regenerate",              testPatternLock },
        { "fills stay in bounds and resolve on downbeat",  testFillsResolveOnDownbeat },
        { "generation is deterministic in the seed",       testDeterministicSeed },
        { "MIDI export round-trips",                       testMidiRoundTrip },
        { "state save/load round-trips",                   testStateRoundTrip },
        { "realtime-safe processBlock and CPU budget",     testRealtimeAndCpu },
        { "editor: size, version, controls, snapshots",    testEditor },
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
