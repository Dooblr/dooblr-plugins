/*
    dooblr-m console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRM_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
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
    using namespace dooblrm;
    namespace id = params::id;
    using Processor = DooblrMProcessor;
    using Editor = DooblrMEditor;

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
        set (p, id::cutoff, 20000.0f);
        set (p, id::emphasis, 0.0f);
        set (p, id::contourAmt, 0.0f);
        set (p, id::kybd1, 0.0f);
        set (p, id::kybd2, 0.0f);
        set (p, id::oscMod, 0.0f);
        set (p, id::filterMod, 0.0f);
        set (p, id::modWheel, 0.0f);
        set (p, id::pitchBend, 0.0f);
        set (p, id::aAttack, 0.001f);
        set (p, id::aSustain, 1.0f);
        set (p, id::mixOsc1, 0.85f);
        set (p, id::mixOsc1On, 1.0f);
        set (p, id::mixOsc2On, 0.0f);
        set (p, id::mixOsc3On, 0.0f);
        set (p, id::mixNoiseOn, 0.0f);
        set (p, id::mixExtOn, 0.0f);
        set (p, id::a440, 0.0f);
        set (p, id::glide, 0.0f);
        set (p, id::volume, 0.8f);
        set (p, id::output, 0.0f);
        set (p, id::osc1Range, 2.0f); // 8'
        set (p, id::osc1Wave, 2.0f);  // saw
        set (p, id::tune, 0.0f);
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

    /** Period via autocorrelation around an expected frequency. Gibbs ringing on a BLIT saw
        creates extra zero-crossings; the lag of the autocorrelation peak is the true period. */
    double measureHzAutocorr (const std::vector<float>& x, double rate, double expectedHz, size_t from)
    {
        if (expectedHz <= 0.0 || from >= x.size())
            return 0.0;
        const auto expectedLag = rate / expectedHz;
        const int lo = std::max (2, (int) std::floor (expectedLag * 0.8));
        const int hi = std::min ((int) (x.size() - from - 1), (int) std::ceil (expectedLag * 1.2));
        if (hi <= lo)
            return 0.0;

        double best = -1.0e100;
        int bestLag = lo;
        for (int lag = lo; lag <= hi; ++lag)
        {
            double acc = 0.0;
            const auto n = (int) (x.size() - from) - lag;
            for (int i = 0; i < n; ++i)
                acc += (double) x[from + (size_t) i] * (double) x[from + (size_t) i + (size_t) lag];
            if (acc > best)
            {
                best = acc;
                bestLag = lag;
            }
        }
        return rate / (double) bestLag;
    }

    double bandEnergy (const std::vector<float>& x, double rate, double lo, double hi)
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
    }

    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-m", "product name");
        check (p->acceptsMidi() && ! p->producesMidi() && p->isMidiEffect() == false, "MIDI instrument");
        check (p->hasEditor(), "has editor");
        check (p->getNumPrograms() >= 9, "Init + factory programs (" + std::to_string (p->getNumPrograms()) + ")");
        check (p->getProgramName (0) == "Init", "first program is Init");
        check (juce::String (JucePlugin_VersionString) == DOOBLRM_PROJECT_VERSION, "JUCE version string matches CMake");
        {
            char code[5] {};
            const auto packed = (int) JucePlugin_PluginCode;
            code[0] = (char) ((packed >> 24) & 0xff);
            code[1] = (char) ((packed >> 16) & 0xff);
            code[2] = (char) ((packed >> 8) & 0xff);
            code[3] = (char) (packed & 0xff);
            check (juce::String (code) == "Dbmm", std::string ("plugin code is Dbmm (got ") + code + ")");
        }
        check ((int) p->getParameters().size() == params::parameterCount,
               "parameter count is " + std::to_string (params::parameterCount)
                   + " not a MIDI-CC dump (got " + std::to_string (p->getParameters().size()) + ")");
        check (p->getParameters().size() < 80, "host-visible params stay well under the ~2100 CC-emulation dump");

        std::set<juce::String> ids;
        for (auto* r : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r))
                ids.insert (ranged->paramID);
        check ((int) ids.size() == params::parameterCount, "parameter IDs are unique");
        for (auto* expected : { id::tune, id::glide, id::osc1Range, id::osc1Wave, id::osc2Freq, id::osc3Control,
                                id::mixOsc1, id::mixExt, id::cutoff, id::emphasis, id::fAttack, id::aSustain,
                                id::decaySwitch, id::notePriority, id::legato, id::a440, id::modWheel, id::output })
            check (ids.count (expected) == 1, std::string ("parameter id ") + expected);
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, 48000);
        check (r.finite() && r.peak() < 1.0e-3f, "silence without notes (peak " + fmt (r.peak(), 6) + ")");
        check (r.denormals() == 0, "no denormals at rest");
    }

    void testPitchA4()
    {
        auto p = makeProcessor();
        openFilter (*p);
        const auto r = render (*p, 48000, { { 0, on (69) } });
        check (r.finite() && r.peak() > 0.05f, "A4 is audible (peak " + fmt (r.peak(), 3) + ")");
        const auto hz = measureHz (r.left, defaultRate, 8000);
        check (std::abs (hz - 440.0) / 440.0 < 0.015, "A4 = 440 Hz at 8' (got " + fmt (hz, 2) + " Hz)");

        set (*p, id::osc1Range, 1.0f); // 16'
        p->prepareToPlay (defaultRate, blockSize);
        const auto r16 = render (*p, 48000, { { 0, on (69) } });
        const auto hz16 = measureHz (r16.left, defaultRate, 8000);
        check (std::abs (hz16 - 220.0) / 220.0 < 0.02, "A4 at 16' is 220 Hz (got " + fmt (hz16, 2) + ")");
    }

    void testBlitOscillator()
    {
        dsp::BlitOsc osc;
        osc.prepare (48000.0);
        std::vector<float> x (48000);
        for (auto& s : x)
            s = osc.process (440.0f, 48000.0f, 2);
        const auto hz = measureHzAutocorr (x, 48000.0, 440.0, 4000);
        check (std::abs (hz - 440.0) / 440.0 < 0.005, "BLIT saw A4 (got " + fmt (hz, 3) + ")");
        float peak = 0;
        for (auto s : x) peak = std::max (peak, std::abs (s));
        check (peak > 0.3f && peak < 1.5f, "BLIT saw amplitude is in range (peak " + fmt (peak, 3) + ")");
        check (std::isfinite (peak), "BLIT output finite");
    }

    void testNoteBoundedAndReleases()
    {
        auto p = makeProcessor();
        openFilter (*p);
        set (*p, id::decaySwitch, 0.0f); // fast dump, so the last 2000 samples are actually silent
        const auto r = render (*p, 48000, { { 0, on (36) }, { 24000, off (36) } });
        check (r.finite(), "output is finite");
        check (r.peak() > 0.05f && r.peak() < 4.0f, "audible and bounded (peak " + fmt (r.peak(), 3) + ")");
        check (r.rms (r.left.size() - 2000) < 2.0e-3, "releases to silence");
        check (r.denormals() == 0, "no denormals (" + std::to_string (r.denormals()) + ")");
        check (std::abs (r.left[1000] - r.right[1000]) < 1.0e-6f, "mono-compatible stereo (L = R)");
    }

    void testSelfOscillation()
    {
        auto p = makeProcessor();
        set (*p, id::mixOsc1On, 0.0f);
        set (*p, id::mixOsc2On, 0.0f);
        set (*p, id::mixOsc3On, 0.0f);
        set (*p, id::mixNoiseOn, 0.0f);
        set (*p, id::mixExtOn, 0.0f);
        set (*p, id::emphasis, 1.0f);
        set (*p, id::contourAmt, 0.0f);
        set (*p, id::kybd1, 0.0f);
        set (*p, id::kybd2, 0.0f);
        set (*p, id::aSustain, 1.0f);
        set (*p, id::aAttack, 0.001f);
        set (*p, id::cutoff, 440.0f);
        set (*p, id::volume, 0.9f);

        const auto r = render (*p, 72000, { { 0, on (60, 1.0f) } });
        check (r.finite() && r.peak() > 0.02f, "self-oscillates from thermal noise (peak " + fmt (r.peak(), 3) + ")");
        const auto hz = measureHz (r.left, defaultRate, 36000);
        check (hz > 200.0 && hz < 900.0, "whistle near cutoff (got " + fmt (hz, 1) + " Hz)");
    }

    void testCutoffTracking()
    {
        const auto whistle = [] (Processor& p, int note)
        {
            set (p, id::mixOsc1On, 0.0f);
            set (p, id::mixOsc2On, 0.0f);
            set (p, id::mixOsc3On, 0.0f);
            set (p, id::mixNoiseOn, 0.0f);
            set (p, id::mixExtOn, 0.0f);
            set (p, id::emphasis, 1.0f);
            set (p, id::contourAmt, 0.0f);
            set (p, id::aSustain, 1.0f);
            set (p, id::cutoff, 400.0f);
            set (p, id::volume, 0.9f);
            const auto r = render (p, 60000, { { 0, on (note, 1.0f) } });
            return measureHz (r.left, defaultRate, 30000);
        };

        auto none = makeProcessor();
        set (*none, id::kybd1, 0.0f);
        set (*none, id::kybd2, 0.0f);
        const auto fNone48 = whistle (*none, 48);
        auto none2 = makeProcessor();
        set (*none2, id::kybd1, 0.0f);
        set (*none2, id::kybd2, 0.0f);
        const auto fNone60 = whistle (*none2, 60);
        check (fNone48 > 50 && fNone60 > 50, "tracking-off whistle is audible");
        check (std::abs (fNone60 / fNone48 - 1.0) < 0.12, "no keyboard tracking: cutoff stays put (ratio "
               + fmt (fNone60 / juce::jmax (1.0, fNone48), 3) + ")");

        auto third = makeProcessor();
        set (*third, id::kybd1, 1.0f);
        set (*third, id::kybd2, 0.0f);
        const auto a = whistle (*third, 48);
        auto third2 = makeProcessor();
        set (*third2, id::kybd1, 1.0f);
        set (*third2, id::kybd2, 0.0f);
        const auto b = whistle (*third2, 60);
        const auto ratio = b / juce::jmax (1.0, a);
        const auto expected = std::exp2 (12.0 * (1.0 / 3.0) / 12.0); // 2^(1/3)
        check (std::abs (ratio - expected) / expected < 0.2,
               "kybd 1/3: 12 st moves cutoff by ~4 st (ratio " + fmt (ratio, 3) + " vs " + fmt (expected, 3) + ")");

        auto twoThirds = makeProcessor();
        set (*twoThirds, id::kybd1, 0.0f);
        set (*twoThirds, id::kybd2, 1.0f);
        const auto c = whistle (*twoThirds, 48);
        auto twoThirds2 = makeProcessor();
        set (*twoThirds2, id::kybd1, 0.0f);
        set (*twoThirds2, id::kybd2, 1.0f);
        const auto d = whistle (*twoThirds2, 60);
        const auto ratio2 = d / juce::jmax (1.0, c);
        const auto expected2 = std::exp2 (2.0 / 3.0);
        check (std::abs (ratio2 - expected2) / expected2 < 0.2,
               "kybd 2/3: 12 st moves cutoff by ~8 st (ratio " + fmt (ratio2, 3) + " vs " + fmt (expected2, 3) + ")");
    }

    void testEnvelopeTiming()
    {
        auto p = makeProcessor();
        openFilter (*p);
        set (*p, id::aAttack, 0.1f);
        set (*p, id::aDecay, 0.2f);
        set (*p, id::aSustain, 0.5f);
        set (*p, id::decaySwitch, 1.0f);

        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        int attackAt = -1;
        float peakEnv = 0;
        const int total = (int) (defaultRate * 0.6);
        for (int start = 0; start < total; start += 64)
        {
            buffer.clear();
            if (start != 0)
                midi.clear();
            p->processBlock (buffer, midi);
            const auto env = p->getEngine().getAmpEnv();
            peakEnv = std::max (peakEnv, env);
            if (attackAt < 0 && env >= 0.9f)
                attackAt = start;
            if (start == (int) (defaultRate * 0.35))
                midi.addEvent (off (60), 0);
        }
        check (peakEnv > 0.9f, "attack reaches ~1 (peak env " + fmt (peakEnv, 3) + ")");
        check (attackAt > (int) (defaultRate * 0.04) && attackAt < (int) (defaultRate * 0.18),
               "100 ms attack lands near 100 ms (got " + fmt (attackAt / defaultRate * 1000.0, 1) + " ms)");

        // Decay switch off: note-off dumps quickly even with a long Decay time.
        auto q = makeProcessor();
        openFilter (*q);
        set (*q, id::aAttack, 0.001f);
        set (*q, id::aDecay, 2.0f);
        set (*q, id::aSustain, 1.0f);
        set (*q, id::decaySwitch, 0.0f);
        const auto fast = render (*q, 24000, { { 0, on (60) }, { 8000, off (60) } });
        check (fast.rms (fast.left.size() - 2000) < 0.02, "Decay switch off: release is a fast dump");

        auto s = makeProcessor();
        openFilter (*s);
        set (*s, id::aAttack, 0.001f);
        set (*s, id::aDecay, 0.08f);
        set (*s, id::aSustain, 1.0f);
        set (*s, id::decaySwitch, 1.0f);
        const auto shared = render (*s, 24000, { { 0, on (60) }, { 8000, off (60) } });
        check (shared.rms (shared.left.size() - 2000) < 0.03, "Decay switch on: release follows decay");
    }

    void testNotePriorityAndGlide()
    {
        auto low = makeProcessor();
        set (*low, id::notePriority, 0.0f);
        set (*low, id::legato, 1.0f);
        set (*low, id::glide, 0.0f);
        low->getEngine().setSettings (low->readSettings());
        low->getEngine().noteOn (48, 0.8f);
        low->getEngine().noteOn (60, 0.8f);
        check (low->getEngine().getCurrentNote() == 48, "low priority (default): stays on 48 when 60 is added");
        low->getEngine().noteOn (36, 0.8f);
        check (low->getEngine().getCurrentNote() == 36, "low priority: jumps to 36");

        auto last = makeProcessor();
        set (*last, id::notePriority, 1.0f);
        set (*last, id::legato, 1.0f);
        last->getEngine().setSettings (last->readSettings());
        last->getEngine().noteOn (48, 0.8f);
        last->getEngine().noteOn (36, 0.8f);
        check (last->getEngine().getCurrentNote() == 36, "last priority: most recent note");

        auto g = makeProcessor();
        openFilter (*g);
        set (*g, id::glide, 0.25f);
        set (*g, id::legato, 1.0f);
        set (*g, id::notePriority, 1.0f); // last-note so 48 → 60 is a glide up
        g->getEngine().setSettings (g->readSettings());
        g->getEngine().noteOn (48, 0.8f);
        const auto startPitch = g->getEngine().getGlidePitch();
        g->getEngine().noteOn (60, 0.8f);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        g->processBlock (buffer, midi);
        const auto mid = g->getEngine().getGlidePitch();
        check (mid > startPitch + 0.5f && mid < 59.5f, "glide moves pitch part-way (from "
               + fmt (startPitch, 2) + " toward 60, now " + fmt (mid, 2) + ")");
    }

    void testOsc3LfoMode()
    {
        auto p = makeProcessor();
        openFilter (*p);
        set (*p, id::mixOsc1On, 0.0f);
        set (*p, id::mixOsc3On, 1.0f);
        set (*p, id::mixOsc3, 0.9f);
        set (*p, id::osc3Control, 0.0f);
        set (*p, id::osc3Range, 5.0f); // LO
        set (*p, id::osc3Freq, 0.0f);
        set (*p, id::osc3Wave, 0.0f);
        const auto r = render (*p, 96000, { { 0, on (72) } });
        const auto hz = measureHz (r.left, defaultRate, 8000);
        // Osc 3 Control off, LO, C3 reference: ~2 Hz. Keyboard C5 must not pull it up.
        check (hz > 0.4 && hz < 8.0, "Osc 3 LO + Control off is a free-running LFO (got "
               + fmt (hz, 3) + " Hz)");
    }

    void testA440()
    {
        auto p = makeProcessor();
        set (*p, id::a440, 1.0f);
        set (*p, id::mixOsc1On, 0.0f);
        const auto r = render (*p, 48000);
        const auto hz = measureHz (r.left, defaultRate, 4000);
        check (r.peak() > 0.05f, "A-440 is audible with no keys down");
        check (std::abs (hz - 440.0) / 440.0 < 0.01, "A-440 tone is 440 Hz (got " + fmt (hz, 2) + ")");
    }

    void testAliasing()
    {
        auto p = makeProcessor (44100.0, 512);
        openFilter (*p);
        set (*p, id::osc1Range, 2.0f);
        set (*p, id::osc1Wave, 2.0f); // saw
        const auto s = render (*p, 44100, { { 0, on (84) } }, 512); // C6 at 8' = 1046.5 Hz
        const auto fund = bandEnergy (s.left, 44100.0, 800.0, 1400.0);
        const auto high = bandEnergy (s.left, 44100.0, 16000.0, 22000.0);
        check (fund > 0.0, "fundamental band has energy");
        check (high < fund * 0.12, "HF alias energy is well below the fundamental (ratio "
               + fmt (fund > 0 ? high / fund : 0.0, 4) + ")");
    }

    void testSampleRates()
    {
        for (auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto p = makeProcessor (rate);
            openFilter (*p);
            const auto n = (int) (rate * 0.4);
            const auto r = render (*p, n, { { 0, on (69) } }, 512);
            const auto hz = measureHz (r.left, rate, (size_t) (rate * 0.08));
            check (r.finite() && r.peak() > 0.05f, fmt (rate, 0) + " Hz: audible and finite");
            check (std::abs (hz - 440.0) / 440.0 < 0.02, fmt (rate, 0) + " Hz: A4 " + fmt (hz, 2));
        }
    }

    void testExtremeFinite()
    {
        auto p = makeProcessor();
        set (*p, id::emphasis, 1.0f);
        set (*p, id::cutoff, 20000.0f);
        set (*p, id::mixOsc1, 1.0f);
        set (*p, id::mixOsc2, 1.0f);
        set (*p, id::mixOsc3, 1.0f);
        set (*p, id::mixOsc1On, 1.0f);
        set (*p, id::mixOsc2On, 1.0f);
        set (*p, id::mixOsc3On, 1.0f);
        set (*p, id::mixExt, 1.0f);
        set (*p, id::mixExtOn, 1.0f);
        set (*p, id::mixNoise, 1.0f);
        set (*p, id::mixNoiseOn, 1.0f);
        set (*p, id::volume, 1.0f);
        set (*p, id::output, 12.0f);
        set (*p, id::contourAmt, 1.0f);
        set (*p, id::oscMod, 1.0f);
        set (*p, id::filterMod, 1.0f);
        set (*p, id::modWheel, 1.0f);
        const auto r = render (*p, 24000, { { 0, on (108, 1.0f) }, { 200, on (21, 1.0f) } });
        check (r.finite(), "extreme settings stay finite");
        check (r.peak() < 8.0f, "extreme settings stay bounded (peak " + fmt (r.peak(), 3) + ")");
        check (r.denormals() == 0, "extreme settings produce no denormals");
    }

    void testStateRoundTrip()
    {
        auto a = makeProcessor();
        set (*a, id::cutoff, 777.0f);
        set (*a, id::osc2Freq, 3.5f);
        set (*a, id::decaySwitch, 0.0f);
        a->setPresetName ("RoundTrip");

        juce::MemoryBlock blob;
        a->getStateInformation (blob);

        auto b = makeProcessor();
        b->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*b, id::cutoff) - 777.0f) < 0.5f, "cutoff restored");
        check (std::abs (get (*b, id::osc2Freq) - 3.5f) < 0.01f, "osc 2 freq restored");
        check (get (*b, id::decaySwitch) < 0.5f, "decay switch restored");
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
            check (r.finite() && r.peak() > 0.02f && r.peak() < 8.0f,
                   factory[(size_t) i].name.toStdString() + ": audible, finite, bounded (peak " + fmt (r.peak(), 3) + ")");
        }

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-m-tests");
        dir.createDirectory();
        const auto file = dir.getChildFile ("UserPatch.dm");
        file.deleteFile();
        set (*p, id::cutoff, 1234.0f);
        check (p->saveUserPreset (file).startsWith ("Saved"), "save user preset");
        set (*p, id::cutoff, 200.0f);
        check (p->loadUserPreset (file).startsWith ("Loaded"), "load user preset");
        check (std::abs (get (*p, id::cutoff) - 1234.0f) < 1.0f, "user preset restores cutoff");
        file.deleteFile();
        dir.deleteRecursively();
    }

    void testNoDeadParameters()
    {
        const std::map<juce::String, std::vector<std::pair<const char*, float>>> contexts {
            { id::osc2Freq, { { id::mixOsc2On, 1.0f }, { id::mixOsc2, 0.8f } } },
            { id::osc2Range, { { id::mixOsc2On, 1.0f }, { id::mixOsc2, 0.8f } } },
            { id::osc2Wave, { { id::mixOsc2On, 1.0f }, { id::mixOsc2, 0.8f } } },
            { id::osc3Freq, { { id::mixOsc1On, 0.0f }, { id::mixOsc3On, 1.0f }, { id::mixOsc3, 0.8f } } },
            { id::osc3Range, { { id::mixOsc1On, 0.0f }, { id::mixOsc3On, 1.0f }, { id::mixOsc3, 0.8f } } },
            { id::osc3Wave, { { id::mixOsc1On, 0.0f }, { id::mixOsc3On, 1.0f }, { id::mixOsc3, 0.8f } } },
            { id::osc3Control, { { id::mixOsc1On, 0.0f }, { id::mixOsc3On, 1.0f }, { id::mixOsc3, 0.8f }, { id::osc3Range, 5.0f } } },
            { id::mixOsc2, { { id::mixOsc2On, 1.0f } } },
            { id::mixOsc2On, { { id::mixOsc2, 0.8f } } },
            { id::mixOsc3, { { id::mixOsc3On, 1.0f } } },
            { id::mixOsc3On, { { id::mixOsc3, 0.8f } } },
            { id::mixExt, { { id::mixExtOn, 1.0f } } },
            { id::mixExtOn, { { id::mixExt, 0.8f } } },
            { id::mixNoise, { { id::mixNoiseOn, 1.0f } } },
            { id::mixNoiseOn, { { id::mixNoise, 0.8f } } },
            { id::noiseType, { { id::mixNoiseOn, 1.0f }, { id::mixNoise, 0.8f } } },
            { id::modMix, { { id::oscMod, 1.0f }, { id::modWheel, 1.0f } } },
            { id::modWheel, { { id::oscMod, 1.0f } } },
            { id::oscMod, { { id::modWheel, 1.0f } } },
            { id::filterMod, { { id::modWheel, 1.0f }, { id::emphasis, 0.4f } } },
            { id::glide, { { id::legato, 1.0f } } },
            { id::notePriority, { { id::legato, 0.0f } } },
            { id::decaySwitch, { { id::aSustain, 1.0f }, { id::aDecay, 0.05f } } },
            { id::kybd1, { { id::emphasis, 0.8f }, { id::cutoff, 300.0f } } },
            { id::kybd2, { { id::emphasis, 0.8f }, { id::cutoff, 300.0f } } },
            { id::velocityOn, {} },
            { id::a440, { { id::mixOsc1On, 0.0f } } },
            { id::pitchBend, { { id::bendRange, 12.0f } } },
            { id::bendRange, {} },
            { id::contourAmt, { { id::fSustain, 1.0f } } },
            { id::fAttack, { { id::contourAmt, 1.0f }, { id::fSustain, 0.0f } } },
            { id::fDecay, { { id::contourAmt, 1.0f }, { id::fSustain, 0.0f } } },
            { id::fSustain, { { id::contourAmt, 1.0f } } },
            { id::legato, { { id::aAttack, 0.001f }, { id::aDecay, 0.08f }, { id::aSustain, 0.0f } } },
        };

        const std::vector<Event> events {
            { 0, juce::MidiMessage::controllerEvent (1, 1, 100) },
            { 0, on (36, 0.9f) }, { 0, on (43, 0.4f) },
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
        auto* cutoff = param (*p, id::cutoff);
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
        auto* reso = param (*p, id::emphasis);
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

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (on (36), 0);
        p->processBlock (buffer, midi);
        midi.clear();
        midi.addEvent (on (40), 10);
        p->processBlock (buffer, midi);
        midi.clear();
        midi.addEvent (off (36), 0);
        p->processBlock (buffer, midi);
        midi.clear();
        for (int i = 0; i < 32; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
        }

        allocationCount = 0;
        for (int i = 0; i < 64; ++i)
        {
            buffer.clear();
            if (i == 8)
                midi.addEvent (on (40), 10);
            else if (i == 16)
                midi.addEvent (off (36), 0);
            else
                midi.clear();
            countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            midi.clear();
        }
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
        const auto prefix = std::getenv ("DOOBLRM_EDITOR_SNAPSHOT");
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
        check (editor != nullptr, "the editor is a DooblrMEditor");
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

        int missing = 0;
        for (auto* r : p->getParameters())
        {
            const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
            auto* control = editor->findControl (pid);
            if (control == nullptr)
            {
                ++missing;
                check (false, "parameter " + pid.toStdString() + " has a control");
            }
        }
        check (missing == 0, "every parameter has exactly one control");

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
            check (editor->getScope().isVisible(), sizeLabel + "scope visible");
            check (editor->getFilterReadout().isVisible(), sizeLabel + "filter readout visible");
            check (editor->getFilterReadout().getHeight() >= 40, sizeLabel + "filter readout has height");
            check ((int) editor->getSections().size() == 7, sizeLabel + "seven board sections");

            for (size_t i = 0; i < editor->getSections().size(); ++i)
            {
                auto& sec = *editor->getSections()[i];
                check (sec.isVisible(), sizeLabel + "section '" + sec.getTitle().toStdString() + "' visible");
                check (content.contains (sec.getBounds()) || sec.getBounds().getHeight() == 0,
                       sizeLabel + "section '" + sec.getTitle().toStdString() + "' in content");
            }
            for (size_t i = 0; i < editor->getSections().size(); ++i)
                for (size_t j = i + 1; j < editor->getSections().size(); ++j)
                    check (! editor->getSections()[i]->getBounds().intersects (editor->getSections()[j]->getBounds()),
                           sizeLabel + "sections do not overlap: " + editor->getSections()[i]->getTitle().toStdString()
                               + " / " + editor->getSections()[j]->getTitle().toStdString());
        }

        editor->setSize (1080, 660);
        snapshot (*editor, "-default");
        editor->setSize (940, 600);
        snapshot (*editor, "-compact");
        editor->setSize (2400, 1400);
        snapshot (*editor, "-wide");
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                  testMetadata },
        { "silence without notes",                     testSilence },
        { "BLIT oscillator pitch",                     testBlitOscillator },
        { "A4 = 440 Hz and 16' octave",                testPitchA4 },
        { "note-on bounded, release to silence",       testNoteBoundedAndReleases },
        { "filter self-oscillation",                   testSelfOscillation },
        { "keyboard tracking 1/3 and 2/3",             testCutoffTracking },
        { "envelope timing and Decay switch",          testEnvelopeTiming },
        { "note priority and glide",                   testNotePriorityAndGlide },
        { "osc 3 LO free-running LFO",                 testOsc3LfoMode },
        { "A-440 tone",                                testA440 },
        { "oscillator aliasing sanity",                testAliasing },
        { "44.1k / 48k / 96k pitch",                   testSampleRates },
        { "finite output under extreme settings",      testExtremeFinite },
        { "state round trip",                          testStateRoundTrip },
        { "factory and user presets",                  testPresets },
        { "no dead parameters",                        testNoDeadParameters },
        { "undo history",                              testUndoHistory },
        { "real-time safety and CPU",                  testRealtimeAndCpu },
        { "editor",                                    testEditor },
    };

    int failedTests = 0;
    for (const auto& test : tests)
    {
        const auto failedBefore = failedChecks;
        std::cout << test.name << " ...\n";
        test.run();
        const auto failed = failedChecks > failedBefore;
        std::cout << (failed ? "[FAIL] " : "[PASS] ") << test.name << "\n";
        if (failed)
            ++failedTests;
    }

    std::cout << "\n" << passedChecks << " checks passed, " << failedChecks << " failed, "
              << tests.size() - failedTests << "/" << tests.size() << " tests passed.\n";
    return failedTests == 0 ? 0 : 1;
}
