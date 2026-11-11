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
#include <chrono>
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

    juce::MouseEvent makeMouseEvent (juce::Component& component, juce::Point<float> position,
                                     juce::Point<float> downPosition, int clicks, bool dragged)
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position,
                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, clicks, dragged };
    }

    void simulateDrag (juce::Component& component, juce::Point<float> from, juce::Point<float> to)
    {
        component.mouseDown (makeMouseEvent (component, from, from, 1, false));
        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            component.mouseDrag (makeMouseEvent (component, from + (to - from) * ((float) i / (float) steps),
                                                 from, 1, true));
        component.mouseUp (makeMouseEvent (component, to, from, 1, true));
    }

    void simulateClick (juce::Component& component)
    {
        const auto c = component.getLocalBounds().getCentre().toFloat();
        component.mouseDown (makeMouseEvent (component, c, c, 1, false));
        component.mouseUp (makeMouseEvent (component, c, c, 1, false));
    }

    void simulateDoubleClick (juce::Component& component, juce::Point<float> position)
    {
        component.mouseDown (makeMouseEvent (component, position, position, 1, false));
        component.mouseUp (makeMouseEvent (component, position, position, 1, false));
        component.mouseDoubleClick (makeMouseEvent (component, position, position, 2, false));
    }

    int bubbleCount (juce::Component& root)
    {
        int n = dynamic_cast<juce::BubbleComponent*> (&root) != nullptr ? 1 : 0;
        for (int i = 0; i < root.getNumChildComponents(); ++i)
            n += bubbleCount (*root.getChildComponent (i));
        return n;
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
                                id::osc3Lfo, id::lfoRate, id::lfoSync, id::lfoDivision, id::lfoRetrigger,
                                id::mixOsc1, id::mixExt, id::cutoff, id::emphasis, id::fAttack, id::aSustain,
                                id::decaySwitch, id::notePriority, id::legato, id::modWheel, id::output })
            check (ids.count (expected) == 1, std::string ("parameter id ") + expected);
        check (ids.count ("a440") == 0, "A-440 parameter is removed");

        auto* resonance = param (*p, id::emphasis);
        check (resonance != nullptr && resonance->paramID == "emphasis",
               "resonance parameter ID stays emphasis");
        check (resonance != nullptr && resonance->getName (64) == "Resonance",
               "host/Ableton display name is Resonance (got "
                   + (resonance != nullptr ? resonance->getName (64).toStdString() : "null") + ")");
        check (resonance != nullptr && resonance->getName (64) != "Emphasis",
               "host display name is no longer Emphasis");
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

    void testOsc3DedicatedLfo()
    {
        auto settle = [] (Processor& p, int samples = 9600)
        {
            render (p, samples, { { 0, on (60) } });
        };

        {
            auto p = makeProcessor();
            set (*p, id::osc3Lfo, 1.0f);
            set (*p, id::lfoSync, 0.0f);
            set (*p, id::lfoRate, 2.0f);
            settle (*p);
            check (std::abs (p->getEngine().getLfoHz() - 2.0f) < 0.08f,
                   "free-run LFO rate is 2 Hz (got " + fmt (p->getEngine().getLfoHz(), 3) + ")");
        }

        {
            auto p = makeProcessor();
            set (*p, id::osc3Lfo, 1.0f);
            set (*p, id::lfoSync, 1.0f);
            set (*p, id::lfoDivision, 2.0f); // 1/4
            settle (*p);
            check (std::abs (p->getEngine().getLfoHz() - 2.0f) < 0.08f,
                   "1/4-note sync at 120 BPM is 2 Hz (got " + fmt (p->getEngine().getLfoHz(), 3) + ")");
        }

        {
            auto low = makeProcessor(), high = makeProcessor();
            for (auto* p : { low.get(), high.get() })
            {
                set (*p, id::osc3Lfo, 1.0f);
                set (*p, id::lfoRate, 3.0f);
                set (*p, id::lfoSync, 0.0f);
                set (*p, id::osc3Control, 1.0f);
                set (*p, id::pitchBend, 1.0f);
                set (*p, id::bendRange, 12.0f);
            }
            render (*low, 9600, { { 0, on (36) } });
            render (*high, 9600, { { 0, on (84) } });
            check (std::abs (low->getEngine().getLfoHz() - high->getEngine().getLfoHz()) < 0.02f,
                   "LFO rate ignores note pitch (" + fmt (low->getEngine().getLfoHz(), 3)
                       + " vs " + fmt (high->getEngine().getLfoHz(), 3) + ")");
            check (std::abs (low->getEngine().getLfoHz() - 3.0f) < 0.1f, "LFO rate ignores pitch wheel");
        }

        {
            auto a = makeProcessor(), b = makeProcessor();
            openFilter (*a);
            openFilter (*b);
            set (*b, id::lfoRate, 27.0f);
            set (*b, id::lfoSync, 1.0f);
            set (*b, id::lfoDivision, 5.0f);
            set (*b, id::lfoRetrigger, 1.0f);
            check (get (*b, id::osc3Lfo) < 0.5f, "audio-mode comparison leaves Osc 3 LFO off");
            const auto ra = render (*a, 24000, { { 0, on (60) } });
            const auto rb = render (*b, 24000, { { 0, on (60) } });
            check (ra.left == rb.left && ra.right == rb.right,
                   "LFO params do not change Osc 3 audio-mode output");
        }

        {
            auto p = makeProcessor();
            openFilter (*p);
            set (*p, id::mixOsc1On, 0.0f);
            set (*p, id::mixOsc3On, 1.0f);
            set (*p, id::mixOsc3, 0.9f);
            set (*p, id::osc3Lfo, 1.0f);
            const auto r = render (*p, 24000, { { 0, on (60) } });
            float tail = 0.0f;
            for (size_t i = 2000; i < r.left.size(); ++i)
                tail = std::max (tail, std::abs (r.left[i]));
            check (tail < 1.0e-3f, "mixer Osc 3 is faded out in LFO mode (tail "
                   + fmt (tail, 6) + ")");
        }

        {
            auto quiet = makeProcessor(), modulated = makeProcessor();
            for (auto* p : { quiet.get(), modulated.get() })
            {
                openFilter (*p);
                set (*p, id::osc3Lfo, 1.0f);
                set (*p, id::lfoRate, 5.0f);
                set (*p, id::modWheel, 1.0f);
                set (*p, id::emphasis, 0.25f);
            }
            set (*modulated, id::filterMod, 1.0f);
            const auto a = render (*quiet, 24000, { { 0, on (48) } });
            const auto b = render (*modulated, 24000, { { 0, on (48) } });
            double diff = 0.0;
            for (size_t i = 0; i < a.left.size(); ++i)
                diff += std::pow ((double) a.left[i] - b.left[i], 2.0);
            diff = std::sqrt (diff / (double) a.left.size());
            check (b.peak() > 0.02f && diff > 0.01 * std::max (a.rms(), b.rms()),
                   "Osc 3 LFO reaches the filter through Filter Mod");
        }

        {
            auto quiet = makeProcessor(), modulated = makeProcessor();
            for (auto* p : { quiet.get(), modulated.get() })
            {
                openFilter (*p);
                set (*p, id::osc3Lfo, 1.0f);
                set (*p, id::lfoRate, 4.0f);
                set (*p, id::modWheel, 1.0f);
            }
            set (*modulated, id::oscMod, 1.0f);
            const auto a = render (*quiet, 24000, { { 0, on (48) } });
            const auto b = render (*modulated, 24000, { { 0, on (48) } });
            double diff = 0.0;
            for (size_t i = 0; i < a.left.size(); ++i)
                diff += std::pow ((double) a.left[i] - b.left[i], 2.0);
            diff = std::sqrt (diff / (double) a.left.size());
            check (b.peak() > 0.02f && diff > 0.01 * std::max (a.rms(), b.rms()),
                   "Osc 3 LFO reaches oscillator pitch through Osc Mod");
        }

        {
            auto p = makeProcessor();
            juce::XmlElement loaded ("DooblrM");
            loaded.setAttribute ("stateVersion", 2);
            loaded.setAttribute ("presetName", "PreLfo");
            auto addParam = [&loaded] (const juce::String& pid, double value)
            {
                auto* child = loaded.createNewChildElement ("PARAM");
                child->setAttribute ("id", pid);
                child->setAttribute ("value", value);
            };
            addParam (id::cutoff, 640.0);
            addParam (id::volume, 0.55);
            juce::MemoryBlock blob;
            juce::AudioProcessor::copyXmlToBinary (loaded, blob);
            p->setStateInformation (blob.getData(), (int) blob.getSize());
            check (get (*p, id::osc3Lfo) < 0.5f, "old state defaults Osc 3 LFO off");
            check (std::abs (get (*p, id::lfoRate) - 1.0f) < 0.02f, "old state defaults LFO Rate to 1 Hz");
            check (get (*p, id::lfoSync) < 0.5f, "old state defaults LFO Sync off");
            check (std::abs (get (*p, id::lfoDivision) - 2.0f) < 0.01f, "old state defaults LFO Division to 1/4");
            check (get (*p, id::lfoRetrigger) < 0.5f, "old state defaults LFO Retrigger off");
            check (std::abs (get (*p, id::cutoff) - 640.0f) < 0.5f, "old state still restores cutoff");
        }

        {
            auto p = makeProcessor();
            set (*p, id::osc3Lfo, 1.0f);
            set (*p, id::lfoRetrigger, 1.0f);
            p->getEngine().setSettings (p->readSettings());
            p->getEngine().noteOn (48, 0.8f);
            check (p->getEngine().getLfoPhase() < 1.0e-4f, "key retrigger resets LFO phase");
        }
    }

    void testA440Removed()
    {
        auto p = makeProcessor();
        check (p->getState().getParameter ("a440") == nullptr, "a440 is not a host parameter");
        check ((int) p->getParameters().size() == params::parameterCount,
               "host parameter count is " + std::to_string (params::parameterCount)
                   + " (got " + std::to_string (p->getParameters().size()) + ")");

        set (*p, id::mixOsc1On, 0.0f);
        const auto silent = render (*p, 24000);
        check (silent.peak() < 1.0e-3f, "no reference tone with no keys down (peak " + fmt (silent.peak(), 6) + ")");

        juce::XmlElement loaded ("DooblrM");
        loaded.setAttribute ("stateVersion", 1);
        loaded.setAttribute ("presetName", "LegacyA440");
        auto addParam = [&loaded] (const juce::String& pid, double value)
        {
            auto* child = loaded.createNewChildElement ("PARAM");
            child->setAttribute ("id", pid);
            child->setAttribute ("value", value);
        };
        addParam (id::cutoff, 333.0);
        addParam ("a440", 1.0);
        addParam (id::volume, 0.4);

        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (loaded, blob);
        p->setStateInformation (blob.getData(), (int) blob.getSize());
        check (p->getState().getParameter ("a440") == nullptr, "a440 stays absent after loading old state");
        check (std::abs (get (*p, id::cutoff) - 333.0f) < 0.5f, "old state restores cutoff beside the leftover a440 PARAM");
        check (std::abs (get (*p, id::volume) - 0.4f) < 0.01f, "old state restores volume");
        check (p->getPresetName() == "LegacyA440", "old state restores the preset name");
        check ((int) p->getState().state.getProperty ("stateVersion") == params::stateVersion,
               "loading old state writes the current stateVersion");

        const auto after = render (*p, 12000);
        check (after.finite(), "processor stays finite after loading state that still contains a440");
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
            { id::osc3Lfo, { { id::mixOsc1On, 0.0f }, { id::mixOsc3On, 1.0f }, { id::mixOsc3, 0.8f } } },
            { id::lfoRate, { { id::osc3Lfo, 1.0f }, { id::filterMod, 1.0f }, { id::modWheel, 1.0f }, { id::emphasis, 0.35f } } },
            { id::lfoSync, { { id::osc3Lfo, 1.0f }, { id::filterMod, 1.0f }, { id::modWheel, 1.0f }, { id::lfoRate, 0.3f } } },
            { id::lfoDivision, { { id::osc3Lfo, 1.0f }, { id::lfoSync, 1.0f }, { id::filterMod, 1.0f }, { id::modWheel, 1.0f } } },
            { id::lfoRetrigger, { { id::osc3Lfo, 1.0f }, { id::filterMod, 1.0f }, { id::modWheel, 1.0f }, { id::lfoRate, 4.0f } } },
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
            check (editor->findControl ("a440") == nullptr, sizeLabel + "A-440 control is gone");
            check (dynamic_cast<ui::BatToggle*> (editor->findControl (id::notePriority)) != nullptr,
                   sizeLabel + "note priority is a 2-position rocker");
            check (dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::notePriority)) == nullptr,
                   sizeLabel + "note priority is not a rotary");
            check (dynamic_cast<ui::BatToggle*> (editor->findControl (id::noiseType)) != nullptr,
                   sizeLabel + "noise colour is a 2-position rocker");
            check (dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::osc1Range)) != nullptr,
                   sizeLabel + "osc 1 range stays a multi-position switch");
            check (dynamic_cast<ui::BatToggle*> (editor->findControl (id::osc3Lfo)) != nullptr,
                   sizeLabel + "Osc 3 LFO is a rocker");
            check (dynamic_cast<ui::BatToggle*> (editor->findControl (id::lfoSync)) != nullptr,
                   sizeLabel + "LFO Sync is a rocker");
            check (dynamic_cast<ui::BatToggle*> (editor->findControl (id::lfoRetrigger)) != nullptr,
                   sizeLabel + "LFO Retrigger is a rocker");
            check (dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::lfoDivision)) != nullptr,
                   sizeLabel + "LFO Division is a multi-position switch");
            check (dynamic_cast<ui::Knob*> (editor->findControl (id::lfoRate)) != nullptr,
                   sizeLabel + "LFO Rate is a knob");

            auto* rateKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::lfoRate));
            auto& pulse = editor->getLfoPulse();
            check (rateKnob != nullptr && pulse.getParentComponent() == rateKnob,
                   sizeLabel + "LFO pulse is parented to the Rate knob");
            if (rateKnob != nullptr)
            {
                check (rateKnob->getLocalBounds().contains (pulse.getBounds()),
                       sizeLabel + "LFO pulse stays inside the Rate knob");
                bool intercepts = true, interceptsChildren = true;
                pulse.getInterceptsMouseClicks (intercepts, interceptsChildren);
                check (! intercepts && ! interceptsChildren, sizeLabel + "LFO pulse does not intercept mouse");
                check (! pulse.hitTest (pulse.getWidth() / 2, pulse.getHeight() / 2),
                       sizeLabel + "LFO pulse hitTest is false");
            }

            auto* cutoffKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::cutoff));
            auto* attackKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::fAttack));
            auto* resonanceKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::emphasis));
            auto& contourIndicator = editor->getCutoffContourIndicator();
            check (cutoffKnob != nullptr && contourIndicator.getParentComponent() == cutoffKnob,
                   sizeLabel + "cutoff contour indicator is parented to the Cutoff knob");
            if (cutoffKnob != nullptr)
            {
                check (contourIndicator.getWidth() > 20 && contourIndicator.getHeight() > 20,
                       sizeLabel + "contour indicator covers the Cutoff slider");
                check (cutoffKnob->getLocalBounds().contains (contourIndicator.getBounds()),
                       sizeLabel + "contour indicator stays inside the Cutoff knob");
                check (editor->getLocalBounds().contains (editor->getLocalArea (cutoffKnob, cutoffKnob->getLocalBounds())),
                       sizeLabel + "cutoff knob stays inside the editor");
                check (editor->getLocalBounds().contains (editor->getLocalArea (&contourIndicator,
                                                                                contourIndicator.getLocalBounds())),
                       sizeLabel + "contour indicator does not overflow the editor");
            }
            check (resonanceKnob != nullptr && resonanceKnob->getCaption().getText() == "Resonance",
                   sizeLabel + "filter resonance knob caption is Resonance");
            check (resonanceKnob != nullptr && resonanceKnob->getCaption().getText() != "Emphasis",
                   sizeLabel + "filter knob is no longer labelled Emphasis");
            check (cutoffKnob != nullptr && cutoffKnob->isLarge(), sizeLabel + "cutoff knob is marked large");

            set (*p, id::osc3Lfo, 1.0f);
            editor->resized();
            auto* osc3Wave = dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::osc3Wave));
            check (osc3Wave != nullptr && osc3Wave->getTickLabels().size() == 6,
                   sizeLabel + "Osc 3 LFO-mode wave has six ticks");
            if (osc3Wave != nullptr)
            {
                const auto& ticks = osc3Wave->getTickLabels();
                check (ticks[1].text == "Dn" && ticks[4].text == "S/H" && ticks[5].text == "Rnd",
                       sizeLabel + "Osc 3 wave ticks switch to LFO names");
                const auto clip = osc3Wave->getLocalBounds().toFloat();
                const auto font = juce::Font (juce::FontOptions (8.0f));
                for (const auto& tick : ticks)
                {
                    check (clip.contains (tick.bounds.reduced (0.51f, 0.51f)) || clip.contains (tick.bounds),
                           sizeLabel + "LFO wave tick '" + tick.text.toStdString() + "' stays inside the switch");
                    const auto textW = juce::GlyphArrangement::getStringWidth (font, tick.text);
                    check (textW <= tick.bounds.getWidth() + 1.0f,
                           sizeLabel + "LFO wave tick '" + tick.text.toStdString() + "' fits its box");
                }
            }
            set (*p, id::osc3Lfo, 0.0f);
            editor->resized();
            check (attackKnob != nullptr, sizeLabel + "filter attack knob exists");
            if (cutoffKnob != nullptr && attackKnob != nullptr)
            {
                const auto cutoffD = juce::jmin (cutoffKnob->getSlider().getWidth(), cutoffKnob->getSlider().getHeight());
                const auto otherD = juce::jmin (attackKnob->getSlider().getWidth(), attackKnob->getSlider().getHeight());
                check (cutoffD >= juce::roundToInt ((float) otherD * 1.55f),
                       sizeLabel + "cutoff knob cell is at least 1.6× a standard knob ("
                           + std::to_string (cutoffD) + " vs " + std::to_string (otherD) + ")");
                const auto drawn = (float) (double) cutoffKnob->getSlider().getProperties()
                                       .getWithDefault ("maxKnobDiameter", 0.0);
                check (drawn >= 74.0f && drawn <= 92.0f,
                       sizeLabel + "cutoff drawn diameter cap is 1.6–2× (got " + fmt (drawn, 1) + ")");
            }

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

            std::function<void (juce::Component&)> checkCaptions = [&] (juce::Component& root)
            {
                if (auto* label = dynamic_cast<juce::Label*> (&root))
                {
                    const auto text = label->getText();
                    const bool skip = text.isEmpty()
                                      || ! label->isVisible()
                                      || label->getWidth() <= 0
                                      || dynamic_cast<juce::Slider*> (label->getParentComponent()) != nullptr
                                      || dynamic_cast<juce::ComboBox*> (label->getParentComponent()) != nullptr;
                    if (! skip)
                    {
                        const auto inner = label->getLookAndFeel().getLabelBorderSize (*label)
                                               .subtractedFrom (label->getLocalBounds());
                        const auto textW = juce::GlyphArrangement::getStringWidth (label->getFont(), text);
                        check (textW <= (float) inner.getWidth() + 1.0f,
                               sizeLabel + "caption '" + text.toStdString() + "' fits ("
                                   + fmt (textW, 1) + " px in " + std::to_string (inner.getWidth()) + " px)");
                    }
                }
                for (int i = 0; i < root.getNumChildComponents(); ++i)
                    checkCaptions (*root.getChildComponent (i));
            };
            checkCaptions (*editor);

            for (auto& rotary : editor->getRotaries())
            {
                const auto clip = rotary->getLocalBounds().toFloat();
                const auto caption = rotary->getCaption().getBounds().toFloat();
                const auto font = juce::Font (juce::FontOptions (rotary->getTickLabels().size() <= 2 ? 9.0f : 8.0f));
                for (const auto& tick : rotary->getTickLabels())
                {
                    const auto id = rotary->getComponentID().toStdString();
                    check (clip.contains (tick.bounds.reduced (0.51f, 0.51f)) || clip.contains (tick.bounds),
                           sizeLabel + id + " tick '" + tick.text.toStdString() + "' stays inside the switch");
                    const auto textW = juce::GlyphArrangement::getStringWidth (font, tick.text);
                    check (textW <= tick.bounds.getWidth() + 1.0f,
                           sizeLabel + id + " tick '" + tick.text.toStdString() + "' fits its box ("
                               + fmt (textW, 1) + " in " + fmt (tick.bounds.getWidth(), 1) + ")");
                    check (! tick.bounds.reduced (0.5f, 0.5f).intersects (caption),
                           sizeLabel + id + " tick '" + tick.text.toStdString()
                               + "' does not overlap the caption");
                }
            }
        }

        editor->setSize (1080, 660);
        snapshot (*editor, "-default");
        editor->setSize (940, 600);
        snapshot (*editor, "-compact");
        editor->setSize (2400, 1400);
        snapshot (*editor, "-wide");
    }

    void testKnobOverlay()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrMEditor");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);

        int knobs = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (dynamic_cast<ui::Knob*> (&c) != nullptr)
                ++knobs;
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                walk (*c.getChildComponent (i));
        };
        walk (*editor);
        check (knobs > 0, "found rotary knobs (" + std::to_string (knobs) + ")");

        {
            juce::Slider probe { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
            editor->addAndMakeVisible (probe);
            probe.setBounds (0, 0, 48, 48);
            probe.setPopupDisplayEnabled (true, false, editor);
            const auto c = probe.getLocalBounds().getCentre().toFloat();
            probe.mouseDown (makeMouseEvent (probe, c, c, 1, false));
            probe.mouseDrag (makeMouseEvent (probe, c + juce::Point<float> (0.0f, -10.0f), c, 1, true));
            check (probe.getCurrentPopupDisplay() != nullptr,
                   "control: JUCE shows a popup overlay when setPopupDisplayEnabled (true) is used");
            probe.mouseUp (makeMouseEvent (probe, c, c, 1, true));
            editor->removeChildComponent (&probe);
        }

        auto* cutoffKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::cutoff));
        check (cutoffKnob != nullptr, "cutoff knob exists");
        if (cutoffKnob == nullptr)
            return;

        auto& slider = cutoffKnob->getSlider();
        auto* cutoff = param (*p, id::cutoff);
        const auto before = cutoff->getValue();
        const auto centre = slider.getLocalBounds().getCentre().toFloat();
        const auto draggedTo = centre + juce::Point<float> (0.0f, -40.0f);
        slider.mouseDown (makeMouseEvent (slider, centre, centre, 1, false));
        slider.mouseDrag (makeMouseEvent (slider, draggedTo, centre, 1, true));
        check (slider.getCurrentPopupDisplay() == nullptr, "getCurrentPopupDisplay is null while the knob is dragging");
        check (bubbleCount (*editor) == 0, "no BubbleComponent overlay is shown while dragging a knob");
        check (bubbleCount (*cutoffKnob) == 0, "the knob itself has no popup child while dragging");
        slider.mouseUp (makeMouseEvent (slider, draggedTo, centre, 1, true));
        check (std::abs (cutoff->getValue() - before) > 1.0e-5f, "dragging a knob still changes its value");
        check (p->getUndoHistory().canUndo(), "knob drag is one undo gesture");

        const auto dragged = cutoff->convertFrom0to1 (cutoff->getValue());
        simulateDoubleClick (slider, centre);
        const auto reset = cutoff->convertFrom0to1 (cutoff->getValue());
        check (std::abs (reset - 1200.0f) < 1.0f,
               "double-click resets the knob (was " + fmt (dragged, 1) + " Hz, now " + fmt (reset, 1) + " Hz)");
    }

    void testSwitchDrag()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrMEditor");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);
        auto& history = p->getUndoHistory();

        auto* decay = dynamic_cast<ui::BatToggle*> (editor->findControl (id::decaySwitch));
        check (decay != nullptr, "Decay rocker exists");
        if (decay != nullptr)
        {
            check (param (*p, id::decaySwitch)->getValue() >= 0.5f, "Decay starts on");
            simulateClick (*decay);
            check (param (*p, id::decaySwitch)->getValue() < 0.5f, "click still toggles the rocker off");
            check (history.canUndo(), "click toggle is one undo gesture");
            history.undo();
            check (param (*p, id::decaySwitch)->getValue() >= 0.5f, "undo restores the click toggle");
        }

        auto* osc1 = dynamic_cast<ui::BatToggle*> (editor->findControl (id::mixOsc1On));
        auto* osc2 = dynamic_cast<ui::BatToggle*> (editor->findControl (id::mixOsc2On));
        check (osc1 != nullptr && osc2 != nullptr, "mixer Osc 1 / Osc 2 rockers exist");
        if (osc1 != nullptr && osc2 != nullptr)
        {
            param (*p, id::mixOsc1On)->setValueNotifyingHost (1.0f);
            param (*p, id::mixOsc2On)->setValueNotifyingHost (1.0f);
            history.clear();

            const auto from = osc1->getLocalBounds().getCentre().toFloat();
            const auto to = osc1->getLocalPoint (osc2, osc2->getLocalBounds().getCentre()).toFloat();
            check (to.y > from.y + 8.0f, "mixer Osc 2 rocker sits below Osc 1 (for a downward rake)");
            simulateDrag (*osc1, from, to);
            check (param (*p, id::mixOsc1On)->getValue() < 0.5f, "dragging down the Osc 1 rocker turns it off");
            check (param (*p, id::mixOsc2On)->getValue() < 0.5f, "the same downward rake turns Osc 2 off");
            check (history.getNumUndoableSteps() == 1, "raking two rockers is one undo step (got "
                   + std::to_string (history.getNumUndoableSteps()) + ")");
            history.undo();
            check (param (*p, id::mixOsc1On)->getValue() >= 0.5f
                       && param (*p, id::mixOsc2On)->getValue() >= 0.5f,
                   "one undo restores both rockers");

            param (*p, id::mixOsc1On)->setValueNotifyingHost (0.0f);
            history.clear();
            const auto upFrom = osc1->getLocalBounds().getCentre().toFloat();
            simulateDrag (*osc1, upFrom, upFrom + juce::Point<float> (0.0f, -20.0f));
            check (param (*p, id::mixOsc1On)->getValue() >= 0.5f, "dragging the rocker up turns it on");
        }

        auto* range = dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::osc1Range));
        check (range != nullptr, "Osc 1 Range rotary switch exists");
        if (range != nullptr)
        {
            history.clear();
            auto& slider = range->getSlider();
            const auto before = slider.getValue();
            const auto centre = slider.getLocalBounds().getCentre().toFloat();
            simulateDrag (slider, centre, centre + juce::Point<float> (30.0f, 80.0f));
            const auto after = slider.getValue();
            check (std::abs (after - before) > 0.5, "dragging a multi-position switch steps it (was "
                   + fmt (before, 2) + ", now " + fmt (after, 2) + ")");
            check (std::abs (after - std::round (after)) < 1.0e-3, "the switch lands on a discrete position");
            check (history.canUndo(), "rotary-switch drag is one undo gesture");
            history.undo();
            check (std::abs (slider.getValue() - before) < 1.0e-3, "undo restores the rotary switch");
        }
    }

    void testPriorityAndNoiseSwitches()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrMEditor");
        if (editor == nullptr)
            return;
        editor->setSize (1080, 660);

        auto* priority = dynamic_cast<ui::BatToggle*> (editor->findControl (id::notePriority));
        auto* noise = dynamic_cast<ui::BatToggle*> (editor->findControl (id::noiseType));
        check (priority != nullptr, "note priority is a BatToggle rocker");
        check (noise != nullptr, "noise colour is a BatToggle rocker");
        check (dynamic_cast<ui::RotarySwitch*> (editor->findControl (id::osc1Wave)) != nullptr,
               "a >2-choice parameter stays a multi-position rotary switch");

        auto* priorityParam = param (*p, id::notePriority);
        auto* noiseParam = param (*p, id::noiseType);
        if (priority == nullptr || noise == nullptr || priorityParam == nullptr || noiseParam == nullptr)
            return;

        check (priorityParam->getValue() < 0.5f, "priority starts at Low");
        simulateClick (*priority);
        check (priorityParam->getValue() >= 0.5f, "clicking the priority rocker selects Last");
        check (p->getUndoHistory().canUndo(), "priority click is one undo gesture");
        p->getUndoHistory().undo();
        check (priorityParam->getValue() < 0.5f, "undo restores Low");

        p->getUndoHistory().clear();
        const auto from = priority->getLocalBounds().getCentre().toFloat();
        simulateDrag (*priority, from, from + juce::Point<float> (0.0f, -20.0f));
        check (priorityParam->getValue() >= 0.5f, "dragging the priority rocker up selects Last");

        check (noiseParam->getValue() < 0.5f, "noise starts at White");
        simulateClick (*noise);
        check (noiseParam->getValue() >= 0.5f, "clicking the noise rocker selects Pink");
        check (p->readSettings().noiseType == 1, "Pink maps to DSP noiseType 1");
        p->getUndoHistory().undo();
        check (noiseParam->getValue() < 0.5f, "undo restores White");
    }

    void testEnvelopeHandles()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrMEditor");
        if (editor == nullptr)
            return;
        editor->setSize (1080, 660);

        auto& graph = editor->getFilterReadout();
        using Handle = ui::FilterReadout::Handle;

        auto* attack = param (*p, id::fAttack);
        auto* decay = param (*p, id::fDecay);
        auto* sustain = param (*p, id::fSustain);
        auto* amount = param (*p, id::contourAmt);
        auto* attackKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::fAttack));
        if (attack == nullptr || decay == nullptr || sustain == nullptr || amount == nullptr)
            return;

        attack->setValueNotifyingHost (attack->convertTo0to1 (0.4f));
        decay->setValueNotifyingHost (decay->convertTo0to1 (0.5f));
        sustain->setValueNotifyingHost (0.45f);
        amount->setValueNotifyingHost (0.8f);
        graph.refresh();

        const auto plot = graph.getPlotArea();
        check (plot.getWidth() > 20.0f && plot.getHeight() > 20.0f, "envelope plot has area");

        const auto aPos = graph.getHandlePosition (Handle::attack);
        const auto dPos = graph.getHandlePosition (Handle::decay);
        const auto sPos = graph.getHandlePosition (Handle::sustain);
        const auto mPos = graph.getHandlePosition (Handle::amount);
        check (graph.handleAt (aPos) == Handle::attack, "attack handle maps to fAttack");
        check (graph.handleAt (dPos) == Handle::decay, "decay handle maps to fDecay");
        check (graph.handleAt (sPos) == Handle::sustain, "sustain handle maps to fSustain");
        check (graph.handleAt (mPos) == Handle::amount, "amount handle maps to contourAmt");
        check (aPos.getDistanceFrom (dPos) > 8.0f && aPos.getDistanceFrom (mPos) > 8.0f
                   && dPos.getDistanceFrom (sPos) > 8.0f,
               "the four envelope handles are distinct");
        check (plot.expanded (2.0f, 2.0f).contains (aPos) && plot.expanded (2.0f, 2.0f).contains (mPos),
               "handles sit on the envelope plot");

        auto& history = p->getUndoHistory();
        history.clear();
        const auto attackBefore = attack->getValue();
        simulateDrag (graph, aPos, aPos + juce::Point<float> (36.0f, 0.0f));
        check (attack->getValue() > attackBefore + 0.02f, "dragging Attack handle increases fAttack");
        if (attackKnob != nullptr)
            check (std::abs ((float) attackKnob->getSlider().getValue() - attack->convertFrom0to1 (attack->getValue())) < 1.0e-3f,
                   "Attack handle stays in sync with the Attack knob");
        check (history.getNumUndoableSteps() == 1, "handle drag is one undo gesture");
        history.undo();
        check (std::abs (attack->getValue() - attackBefore) < 1.0e-4f, "undo restores Attack");

        history.clear();
        const auto decayBefore = decay->getValue();
        simulateDrag (graph, graph.getHandlePosition (Handle::decay),
                      graph.getHandlePosition (Handle::decay) + juce::Point<float> (28.0f, 0.0f));
        check (decay->getValue() > decayBefore + 0.02f, "dragging Decay handle increases fDecay");

        history.clear();
        const auto sustainBefore = sustain->getValue();
        simulateDrag (graph, graph.getHandlePosition (Handle::sustain),
                      graph.getHandlePosition (Handle::sustain) + juce::Point<float> (0.0f, -24.0f));
        check (sustain->getValue() > sustainBefore + 0.02f, "dragging Sustain handle raises sustain level");

        history.clear();
        const auto amountBefore = amount->getValue();
        simulateDrag (graph, graph.getHandlePosition (Handle::amount),
                      graph.getHandlePosition (Handle::amount) + juce::Point<float> (0.0f, 30.0f));
        check (amount->getValue() < amountBefore - 0.02f, "dragging Amount handle down lowers contourAmt");

        simulateDoubleClick (graph, graph.getHandlePosition (Handle::amount));
        check (std::abs (amount->getValue() - amount->getDefaultValue()) < 1.0e-4f,
               "double-clicking Amount resets contourAmt");

        amount->setValueNotifyingHost (0.85f);
        set (*p, id::fAttack, 0.08f);
        set (*p, id::fDecay, 0.4f);
        set (*p, id::fSustain, 0.5f);
        graph.refresh();
        const auto idleEnv = graph.getLiveEnvelope();
        check (idleEnv < 0.05f, "live envelope is idle with no notes");

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        for (int i = 0; i < 8; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
            midi.clear();
        }
        graph.refresh();
        check (graph.getLiveEnvelope() > idleEnv + 0.2f,
               "playhead tracks DSP filter envelope while a note plays (env "
                   + fmt (graph.getLiveEnvelope(), 3) + ")");
        check (graph.getPlayheadPosition().y < graph.getHandlePosition (Handle::attack).y - 2.0f,
               "live playhead lifts off the baseline during attack");
    }

    int opaquePixels (juce::Component& c)
    {
        if (c.getWidth() <= 0 || c.getHeight() <= 0)
            return 0;
        juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
        juce::Graphics g (image);
        c.paint (g);
        int n = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 12)
                    ++n;
        return n;
    }

    void testCutoffContourIndicator()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "the editor is a DooblrMEditor");
        if (editor == nullptr)
            return;

        check (juce::approximatelyEqual (ui::CutoffContourIndicator::contourOctaves, dsp::contourOctaves),
               "indicator octave span matches the DSP filter contour");

        auto* cutoff = param (*p, id::cutoff);
        auto* amount = param (*p, id::contourAmt);
        auto* cutoffKnob = dynamic_cast<ui::Knob*> (editor->findControl (id::cutoff));
        auto& indicator = editor->getCutoffContourIndicator();
        if (cutoff == nullptr || amount == nullptr || cutoffKnob == nullptr)
            return;

        bool intercepts = true, interceptsChildren = true;
        indicator.getInterceptsMouseClicks (intercepts, interceptsChildren);
        check (! intercepts && ! interceptsChildren, "contour indicator does not intercept mouse clicks");
        check (! indicator.hitTest (indicator.getWidth() / 2, indicator.getHeight() / 2),
               "hitTest is false so the Cutoff knob still receives drags");
        check (indicator.getNumChildComponents() == 0, "contour indicator has no child labels");

        amount->setValueNotifyingHost (0.0f);
        set (*p, id::cutoff, 400.0f);
        indicator.refresh();
        check (! indicator.isVisible() && ! indicator.isRangeVisible() && ! indicator.isLiveMarkerVisible(),
               "indicator is hidden at contour amount 0");
        check (opaquePixels (indicator) == 0, "paint draws nothing at contour amount 0");

        const auto beforeDrag = cutoff->getValue();
        auto& slider = cutoffKnob->getSlider();
        const auto centre = slider.getLocalBounds().getCentre().toFloat();
        simulateDrag (slider, centre, centre + juce::Point<float> (0.0f, -36.0f));
        check (std::abs (cutoff->getValue() - beforeDrag) > 1.0e-5f,
               "dragging Cutoff still changes the value with the overlay present");
        cutoff->setValueNotifyingHost (cutoff->convertTo0to1 (400.0f));

        amount->setValueNotifyingHost (0.30f);
        indicator.refresh();
        check (indicator.isVisible() && indicator.isRangeVisible(),
               "indicator is visible when contour amount is non-zero");
        check (! indicator.isLiveMarkerVisible(), "live marker stays hidden while the envelope is idle");
        const auto oct30 = std::log2 (indicator.getRangeEndHz() / indicator.getRangeStartHz());
        const auto span30 = std::abs (indicator.getRangeEndAngle() - indicator.getRangeStartAngle());
        check (std::abs (oct30 / (0.30f * dsp::contourOctaves) - 1.0f) < 0.02f,
               "range at 30% contour is 1.5 octaves (got " + fmt (oct30, 3) + ")");
        check (opaquePixels (indicator) > 20, "visible range arc actually paints pixels");

        amount->setValueNotifyingHost (0.60f);
        indicator.refresh();
        const auto oct60 = std::log2 (indicator.getRangeEndHz() / indicator.getRangeStartHz());
        const auto span60 = std::abs (indicator.getRangeEndAngle() - indicator.getRangeStartAngle());
        check (std::abs (oct60 / (0.60f * dsp::contourOctaves) - 1.0f) < 0.02f,
               "range at 60% contour is 3 octaves (got " + fmt (oct60, 3) + ")");
        check (std::abs (oct60 / oct30 - 2.0f) < 0.05f, "octave span doubles when contour amount doubles");
        check (span60 > span30 * 1.5f, "angle span grows with contour amount ("
               + fmt (span30, 3) + " -> " + fmt (span60, 3) + ")");

        amount->setValueNotifyingHost (1.0f);
        set (*p, id::cutoff, 10000.0f);
        indicator.refresh();
        check (std::abs (indicator.getRangeEndHz() - 20000.0f) < 0.5f,
               "sweep clamps to the cutoff knob maximum");
        set (*p, id::cutoff, 400.0f);

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            amount->setValueNotifyingHost (0.55f);
            indicator.refresh();
            const auto sizeLabel = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";
            check (cutoffKnob->getLocalBounds().contains (indicator.getBounds()),
                   sizeLabel + "contour indicator stays inside the Cutoff knob");
            check (editor->getLocalBounds().contains (editor->getLocalArea (&indicator, indicator.getLocalBounds())),
                   sizeLabel + "contour indicator does not overflow the editor");
            const auto ring = indicator.getRingRadius() + 1.6f;
            const auto c = indicator.getCentre();
            const auto box = indicator.getLocalBounds().toFloat().expanded (1.5f, 1.5f);
            check (box.contains (juce::Point<float> (c.x - ring, c.y))
                       && box.contains (juce::Point<float> (c.x + ring, c.y))
                       && box.contains (juce::Point<float> (c.x, c.y - ring))
                       && box.contains (juce::Point<float> (c.x, c.y + ring)),
                   sizeLabel + "contour arc stays inside the knob cell");
            ui::SectionPanel* modifiers = nullptr;
            for (auto& sec : editor->getSections())
                if (sec->getTitle() == "MODIFIERS")
                    modifiers = sec.get();
            check (modifiers != nullptr, sizeLabel + "modifiers section exists");
            if (modifiers != nullptr)
                check (modifiers->getLocalBounds().contains (modifiers->getLocalArea (&indicator, indicator.getLocalBounds())),
                       sizeLabel + "contour indicator stays inside Modifiers");
        }

        editor->setSize (1080, 660);
        amount->setValueNotifyingHost (0.85f);
        set (*p, id::cutoff, 400.0f);
        set (*p, id::fAttack, 0.08f);
        set (*p, id::fDecay, 0.4f);
        set (*p, id::fSustain, 0.65f);
        set (*p, id::velocityOn, 0.0f);
        set (*p, id::kybd1, 0.0f);
        set (*p, id::kybd2, 0.0f);
        set (*p, id::filterMod, 0.0f);
        indicator.refresh();
        const auto idleEnv = indicator.getLiveEnvelope();
        check (idleEnv < 0.05f && ! indicator.isLiveMarkerVisible(), "live marker idle with no notes");

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        for (int i = 0; i < 8; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
            midi.clear();
        }
        indicator.refresh();
        editor->getFilterReadout().refresh();
        const auto env = indicator.getLiveEnvelope();
        check (env > idleEnv + 0.2f, "live envelope rises while a note plays (env " + fmt (env, 3) + ")");
        check (indicator.isLiveMarkerVisible(), "live marker is visible while the envelope moves");
        check (std::abs (env - editor->getFilterReadout().getLiveEnvelope()) < 1.0e-5f,
               "indicator reads the same lock-free envelope tap as the filter graph");
        const auto expectedHz = 400.0f * std::exp2 (0.85f * dsp::contourOctaves * env);
        check (std::abs (indicator.getLiveHz() / expectedHz - 1.0f) < 0.02f,
               "live marker Hz follows cutoff * 2^(contour * 5 * env) (got "
                   + fmt (indicator.getLiveHz(), 1) + ", expected " + fmt (expectedHz, 1) + ")");
        check (indicator.getLiveAngle() > indicator.getRangeStartAngle() + 0.01f
                   && indicator.getLiveAngle() <= indicator.getRangeEndAngle() + 0.02f,
               "live marker sits on the sweep arc between the set cutoff and the contour extreme");
        check (opaquePixels (indicator) > 20, "live marker and range arc paint while a note plays");

        amount->setValueNotifyingHost (0.0f);
        indicator.refresh();
        check (! indicator.isVisible() && ! indicator.isRangeVisible() && ! indicator.isLiveMarkerVisible(),
               "indicator hides again when contour amount returns to 0");
        check (opaquePixels (indicator) == 0, "paint is empty again at contour amount 0");

        amount->setValueNotifyingHost (0.5f);
        indicator.refresh();
        const auto requests = indicator.getRepaintRequests();
        indicator.refresh();
        indicator.refresh();
        indicator.refresh();
        check (indicator.getRepaintRequests() == requests, "unchanged refresh does not repaint the knob");
        check (cutoffKnob->getLocalBounds().contains (indicator.getLastRepaintBounds()),
               "repaint region is the Cutoff knob overlay, not the whole editor");
        check (indicator.getWidth() * indicator.getHeight() < editor->getWidth() * editor->getHeight() / 8,
               "overlay area is a small fraction of the editor");

        {
            juce::Image image (juce::Image::ARGB, juce::jmax (1, indicator.getWidth()),
                               juce::jmax (1, indicator.getHeight()), true);
            const auto paintOnce = [&]
            {
                juce::Graphics g (image);
                indicator.paint (g);
            };
            paintOnce();
            const auto start = std::chrono::steady_clock::now();
            constexpr int runs = 40;
            for (int i = 0; i < runs; ++i)
                paintOnce();
            const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count()
                            / (double) runs;
            note ("cutoff contour indicator paint: " + fmt (ms, 3) + " ms");
            check (ms < 8.0, "indicator paint stays well under a 60 Hz frame (got " + fmt (ms, 3) + " ms)");
        }
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
        { "osc 3 dedicated LFO mode",                  testOsc3DedicatedLfo },
        { "A-440 removed, old state still loads",      testA440Removed },
        { "oscillator aliasing sanity",                testAliasing },
        { "44.1k / 48k / 96k pitch",                   testSampleRates },
        { "finite output under extreme settings",      testExtremeFinite },
        { "state round trip",                          testStateRoundTrip },
        { "factory and user presets",                  testPresets },
        { "no dead parameters",                        testNoDeadParameters },
        { "undo history",                              testUndoHistory },
        { "real-time safety and CPU",                  testRealtimeAndCpu },
        { "editor",                                    testEditor },
        { "knob overlay",                              testKnobOverlay },
        { "switch drag",                               testSwitchDrag },
        { "priority and noise rockers",                testPriorityAndNoiseSwitches },
        { "filter envelope handles",                   testEnvelopeHandles },
        { "cutoff contour indicator",                  testCutoffContourIndicator },
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
