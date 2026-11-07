/*
    dooblr-0coast console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLR0COAST_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
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
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // Only news on the thread that is inside processBlock are counted (JUCE's
    // timer / message thread can allocate while the test runs).
    thread_local bool onAudioThread = false;
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
    using namespace dooblr0coast;
    namespace id = params::id;
    using Processor = Dooblr0CoastProcessor;
    using Editor = Dooblr0CoastEditor;

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

    void openVoice (Processor& p)
    {
        set (p, id::contourOnset, 0.001f);
        set (p, id::contourSustain, 1.0f);
        set (p, id::contourDecay, 0.4f);
        set (p, id::dynamics, 1.0f);
        set (p, id::balance, 0.0f);
        set (p, id::overtone, 0.0f);
        set (p, id::multiply, 0.0f);
        set (p, id::linFm, 0.0f);
        set (p, id::level, 0.72f);
        set (p, id::output, 0.0f);
        set (p, id::slopeCycle, 0.0f);
        set (p, id::multiplyCv, 0.0f);
        set (p, id::arpOn, 0.0f);
        set (p, id::portamento, 0.0f);
        set (p, id::pitch, 0.0f);
        set (p, id::fine, 0.0f);
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
        check (p->getName() == "dooblr-0coast", "product name");
        check (p->acceptsMidi() && ! p->producesMidi() && p->isMidiEffect() == false, "MIDI instrument");
        check (JucePlugin_IsSynth != 0, "IS_SYNTH");
        check (p->hasEditor(), "has editor");
        check (p->getNumPrograms() == 9, "Init + 8 factory programs (" + std::to_string (p->getNumPrograms()) + ")");
        check (p->getProgramName (0) == "Init", "first program is Init");
        check (juce::String (JucePlugin_VersionString) == DOOBLR0COAST_PROJECT_VERSION, "JUCE version string matches CMake");
        check (juce::String (JucePlugin_VersionString) == "0.1.0", "version 0.1.0");
        check ((int) p->getParameters().size() == params::parameterCount,
               "parameter count " + std::to_string (p->getParameters().size()));

        std::set<juce::String> ids;
        for (auto* r : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (r))
                ids.insert (ranged->paramID);
        check ((int) ids.size() == params::parameterCount, "parameter IDs are unique");
        for (auto* expected : { id::pitch, id::fine, id::portamento, id::linFm, id::overtone, id::overtoneCv,
                                id::multiply, id::multiplyCv, id::slopeCycle, id::slopeRise, id::slopeFall,
                                id::slopeResponse, id::contourOnset, id::contourSustain, id::contourDecay,
                                id::contourResponse, id::balance, id::balanceCv, id::dynamics, id::level,
                                id::arpOn, id::arpType, id::legato, id::midiBCv, id::midiBGate, id::clockDiv,
                                id::mathAmt, id::midiBLfoRate, id::bendRange, id::output })
            check (ids.count (expected) == 1, std::string ("parameter id ") + expected);

        auto* pitch = param (*p, id::pitch);
        check (pitch != nullptr && pitch->getName (64) == "Pitch", "Pitch name visible to the host");
        check (pitch->isAutomatable(), "Pitch is automatable");
    }

    void testSilence()
    {
        auto p = makeProcessor();
        const auto r = render (*p, 48000);
        check (r.finite() && r.peak() < 1.0e-4f, "silence without notes (peak " + fmt (r.peak(), 6) + ")");
        check (r.denormals() == 0, "no denormals at rest");
    }

    void testPitchTracking()
    {
        auto p = makeProcessor();
        openVoice (*p);
        const auto r = render (*p, 48000, { { 0, on (69) } });
        check (r.finite() && r.peak() > 0.02f, "A4 is audible (peak " + fmt (r.peak(), 3) + ")");
        const auto hz = measureHz (r.left, defaultRate, 8000);
        check (std::abs (hz - 440.0) < 3.0, "A4 tracks 440 Hz (got " + fmt (hz, 2) + ")");

        auto q = makeProcessor();
        openVoice (*q);
        set (*q, id::pitch, 1.0f);
        const auto up = render (*q, 48000, { { 0, on (69) } });
        const auto hzUp = measureHz (up.left, defaultRate, 8000);
        check (std::abs (hzUp - 880.0) < 8.0, "Pitch +1 oct is 880 Hz (got " + fmt (hzUp, 2) + ")");

        auto f = makeProcessor();
        openVoice (*f);
        set (*f, id::fine, 100.0f);
        const auto fine = render (*f, 48000, { { 0, on (69) } });
        const auto hzFine = measureHz (fine.left, defaultRate, 8000);
        const auto expected = 440.0 * std::pow (2.0, 100.0 / 1200.0);
        check (std::abs (hzFine - expected) < 4.0, "Fine +100 ct (got " + fmt (hzFine, 2) + ", expect " + fmt (expected, 2) + ")");
        note ("A4 " + fmt (hz, 2) + " Hz, +1 oct " + fmt (hzUp, 2) + " Hz, +100 ct " + fmt (hzFine, 2) + " Hz");
    }

    void testOvertoneMultiplySpectrum()
    {
        auto renderTimbre = [] (float overtone, float multiply) {
            auto p = makeProcessor();
            openVoice (*p);
            set (*p, id::balance, 1.0f);
            set (*p, id::overtone, overtone);
            set (*p, id::multiply, multiply);
            return render (*p, 48000, { { 0, on (48) } });
        };

        const auto odd = renderTimbre (0.05f, 0.0f);
        const auto even = renderTimbre (0.5f, 0.0f);
        const auto folded = renderTimbre (0.5f, 0.85f);
        check (odd.finite() && even.finite() && folded.finite(), "timbre renders are finite");
        check (odd.peak() > 0.01f && even.peak() > 0.01f && folded.peak() > 0.01f, "overtone path is audible");

        const auto fund = 130.81;
        const auto odd3 = bandEnergy (odd.left, defaultRate, fund * 2.6, fund * 3.6);
        const auto odd2 = bandEnergy (odd.left, defaultRate, fund * 1.6, fund * 2.4);
        const auto even2 = bandEnergy (even.left, defaultRate, fund * 1.6, fund * 2.4);
        const auto even3 = bandEnergy (even.left, defaultRate, fund * 2.6, fund * 3.6);
        check (odd3 > odd2 * 0.5, "odd overtone emphasises the 3rd over the 2nd");
        check (even2 > even3 * 0.6, "even overtone emphasises the 2nd over the 3rd");

        const auto oddHigh = bandEnergy (odd.left, defaultRate, 2000.0, 8000.0);
        const auto foldHigh = bandEnergy (folded.left, defaultRate, 2000.0, 8000.0);
        check (foldHigh > oddHigh * 1.4, "Multiply raises high-frequency energy (ratio "
               + fmt (oddHigh > 0 ? foldHigh / oddHigh : 0.0, 2) + ")");
        note ("odd 3/2 " + fmt (odd2 > 0 ? odd3 / odd2 : 0.0, 2)
              + ", even 2/3 " + fmt (even3 > 0 ? even2 / even3 : 0.0, 2));
    }

    void testSlopeCycle()
    {
        auto run = [] (float rise, float fall, float response) {
            auto p = makeProcessor (defaultRate, 1);
            set (*p, id::slopeCycle, 1.0f);
            set (*p, id::slopeRise, rise);
            set (*p, id::slopeFall, fall);
            set (*p, id::slopeResponse, response);
            std::vector<float> slope;
            slope.reserve (24000);
            juce::AudioBuffer<float> buffer (2, 1);
            juce::MidiBuffer midi;
            for (int i = 0; i < 24000; ++i)
            {
                buffer.clear();
                p->processBlock (buffer, midi);
                slope.push_back (p->getEngine().lastSlope());
            }
            int peaks = 0;
            for (size_t i = 2; i + 2 < slope.size(); ++i)
                if (slope[i] > 0.92f && slope[i] >= slope[i - 1] && slope[i] >= slope[i + 1]
                    && slope[i - 1] > slope[i - 2])
                    ++peaks;
            return peaks;
        };

        const auto lin = run (0.05f, 0.05f, 0.5f);
        const auto expo = run (0.05f, 0.05f, 1.0f);
        const auto logv = run (0.05f, 0.05f, 0.0f);
        // 0.5 s window, 0.1 s linear cycle => ~5 peaks
        check (lin >= 3 && lin <= 8, "linear slope cycles near 10 Hz (" + std::to_string (lin) + " peaks / 0.5 s)");
        check (expo > lin, "exponential Vari-Response is faster than linear ("
               + std::to_string (expo) + " vs " + std::to_string (lin) + ")");
        check (logv < lin, "logarithmic Vari-Response is slower than linear ("
               + std::to_string (logv) + " vs " + std::to_string (lin) + ")");
        note ("slope peaks in 0.5 s: log " + std::to_string (logv) + " lin " + std::to_string (lin)
              + " expo " + std::to_string (expo));
    }

    void testContourTiming()
    {
        auto p = makeProcessor (defaultRate, 64);
        set (*p, id::contourOnset, 0.10f);
        set (*p, id::contourSustain, 0.0f);
        set (*p, id::contourDecay, 0.20f);
        set (*p, id::contourResponse, 0.5f);
        set (*p, id::dynamics, 1.0f);
        set (*p, id::balance, 0.0f);
        set (*p, id::level, 0.72f);
        set (*p, id::overtone, 0.0f);
        set (*p, id::multiply, 0.0f);
        set (*p, id::multiplyCv, 0.0f);

        std::vector<float> env;
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        for (int i = 0; i < 24000; i += 64)
        {
            buffer.clear();
            if (i > 0)
                midi.clear();
            p->processBlock (buffer, midi);
            for (int s = 0; s < 64; ++s)
                env.push_back (p->getEngine().lastContour());
        }

        size_t peakAt = 0;
        float peak = 0.0f;
        for (size_t i = 0; i < env.size(); ++i)
            if (env[i] > peak)
            {
                peak = env[i];
                peakAt = i;
            }
        const auto onsetMs = 1000.0 * (double) peakAt / defaultRate;
        check (peak > 0.9f, "contour reaches full scale (" + fmt (peak, 2) + ")");
        check (onsetMs > 60.0 && onsetMs < 160.0, "onset ~100 ms (got " + fmt (onsetMs, 1) + " ms)");

        size_t quiet = env.size();
        for (size_t i = peakAt; i < env.size(); ++i)
            if (env[i] < 0.05f)
            {
                quiet = i;
                break;
            }
        const auto decayMs = 1000.0 * (double) (quiet - peakAt) / defaultRate;
        check (decayMs > 120.0 && decayMs < 320.0, "decay ~200 ms after peak (got " + fmt (decayMs, 1) + " ms)");
        note ("onset " + fmt (onsetMs, 1) + " ms, decay " + fmt (decayMs, 1) + " ms");
    }

    void testDynamicsGate()
    {
        auto closed = makeProcessor();
        openVoice (*closed);
        set (*closed, id::dynamics, 0.0f);
        const auto silent = render (*closed, 24000, { { 0, on (60) } });
        check (silent.finite() && silent.peak() < 0.002f, "Dynamics at 0 gates the voice (peak "
               + fmt (silent.peak(), 5) + ")");

        auto open = makeProcessor();
        openVoice (*open);
        set (*open, id::dynamics, 1.0f);
        const auto loud = render (*open, 24000, { { 0, on (60) } });
        check (loud.finite() && loud.peak() > 0.03f, "Dynamics at 1 lets the note through (peak "
               + fmt (loud.peak(), 3) + ")");

        auto release = makeProcessor();
        openVoice (*release);
        const auto r = render (*release, 48000, { { 0, on (60) }, { 20000, off (60) } });
        const auto during = r.rms (4000, 16000);
        const auto after = r.rms (44000, 48000);
        check (during > 0.01, "note is open while gated");
        check (after < during * 0.15, "LPG closes after note-off (during " + fmt (during, 4)
               + ", after " + fmt (after, 4) + ")");
    }

    void testPatchCablesAndNormals()
    {
        auto p = makeProcessor();
        check (p->getEngine().numCables() == 0, "no cables by default");
        check (! p->getEngine().inputIsPatched (dsp::InJack::MultiplyCv), "Multiply CV starts on its Slope normal");

        check (p->getEngine().connect (dsp::OutJack::Random, dsp::InJack::MultiplyCv), "Random -> Multiply CV");
        check (p->getEngine().inputIsPatched (dsp::InJack::MultiplyCv), "Multiply CV is patched");
        check (p->getEngine().sourceOf (dsp::InJack::MultiplyCv) == (int8_t) dsp::OutJack::Random, "source is Random");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeProcessor();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getEngine().numCables() == 1, "cable count round-trips");
        check (q->getEngine().sourceOf (dsp::InJack::MultiplyCv) == (int8_t) dsp::OutJack::Random,
               "Random -> Multiply CV survives get/setState");

        check (q->getEngine().disconnectInput (dsp::InJack::MultiplyCv), "double-click equivalent removes the cable");
        check (! q->getEngine().inputIsPatched (dsp::InJack::MultiplyCv), "normal restored after removing the cable");

        auto a = makeProcessor();
        openVoice (*a);
        set (*a, id::balance, 1.0f);
        set (*a, id::overtone, 0.4f);
        set (*a, id::multiply, 0.15f);
        set (*a, id::multiplyCv, 0.9f);
        set (*a, id::slopeCycle, 1.0f);
        set (*a, id::slopeRise, 0.04f);
        set (*a, id::slopeFall, 0.04f);
        const auto normalled = render (*a, 48000, { { 0, on (48) } });

        auto b = makeProcessor();
        openVoice (*b);
        set (*b, id::balance, 1.0f);
        set (*b, id::overtone, 0.4f);
        set (*b, id::multiply, 0.15f);
        set (*b, id::multiplyCv, 0.9f);
        set (*b, id::slopeCycle, 1.0f);
        set (*b, id::slopeRise, 0.04f);
        set (*b, id::slopeFall, 0.04f);
        b->getEngine().connect (dsp::OutJack::MidiACv, dsp::InJack::MultiplyCv); // steady CV, overrides Slope
        const auto overridden = render (*b, 48000, { { 0, on (48) } });

        auto variance = [] (const std::vector<float>& x)
        {
            const auto n = x.size() / 2;
            double mean = 0.0;
            for (size_t i = n; i < x.size(); ++i) mean += std::abs (x[i]);
            mean /= (double) (x.size() - n);
            double v = 0.0;
            for (size_t i = n; i < x.size(); ++i)
            {
                const auto d = std::abs (x[i]) - mean;
                v += d * d;
            }
            return v / (double) (x.size() - n);
        };
        check (variance (normalled.left) > variance (overridden.left) * 1.3,
               "overriding Multiply's Slope normal reduces cyclic AM");
        note ("normal var " + fmt (variance (normalled.left), 6) + ", overridden "
              + fmt (variance (overridden.left), 6));
    }

    void testArpeggiatorTiming()
    {
        auto p = makeProcessor (defaultRate, 64);
        openVoice (*p);
        set (*p, id::arpOn, 1.0f);
        set (*p, id::arpType, 0.0f);
        set (*p, id::clockDiv, 2.0f); // 1/4 at 120 BPM = 0.5 s
        set (*p, id::legato, 0.0f);

        const auto r = render (*p, (int) defaultRate * 2, { { 0, on (60) }, { 10, on (72) } }, 64);
        check (r.finite() && r.peak() > 0.02f, "arp is audible");

        const auto hzA = measureHz (std::vector<float> (r.left.begin() + 4000, r.left.begin() + 18000), defaultRate, 0);
        const auto hzB = measureHz (std::vector<float> (r.left.begin() + 28000, r.left.begin() + 42000), defaultRate, 0);
        const bool swapped = (std::abs (hzA - 261.6) < 20.0 && std::abs (hzB - 523.3) < 40.0)
                          || (std::abs (hzA - 523.3) < 40.0 && std::abs (hzB - 261.6) < 20.0);
        check (std::abs (hzA - hzB) > 80.0, "arp steps between two pitches (" + fmt (hzA, 1) + " then " + fmt (hzB, 1) + " Hz)");
        check (swapped, "the two pitches are C4 and C5 in some order");
        note ("arp window A " + fmt (hzA, 1) + " Hz, B " + fmt (hzB, 1) + " Hz");
    }

    void testNoDeadParameters()
    {
        const std::map<juce::String, std::function<void (Processor&)>> contexts {
            { id::linFm, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::SlopeCv, dsp::InJack::OscLinFm);
                set (p, id::slopeCycle, 1.0f);
                set (p, id::slopeRise, 0.01f);
                set (p, id::slopeFall, 0.01f);
            } },
            { id::overtoneCv, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::ContourCv, dsp::InJack::OvertoneCv);
                set (p, id::overtone, 0.3f);
                set (p, id::balance, 1.0f);
            } },
            { id::multiplyCv, [] (Processor& p) {
                set (p, id::slopeCycle, 1.0f);
                set (p, id::slopeRise, 0.03f);
                set (p, id::slopeFall, 0.03f);
                set (p, id::balance, 1.0f);
                set (p, id::overtone, 0.4f);
                set (p, id::multiply, 0.2f);
            } },
            { id::slopeCycle, [] (Processor& p) {
                set (p, id::multiplyCv, 0.8f);
                set (p, id::balance, 1.0f);
                set (p, id::overtone, 0.4f);
            } },
            { id::slopeRise, [] (Processor& p) {
                set (p, id::slopeCycle, 1.0f);
                set (p, id::multiplyCv, 0.8f);
                set (p, id::balance, 1.0f);
            } },
            { id::slopeFall, [] (Processor& p) {
                set (p, id::slopeCycle, 1.0f);
                set (p, id::multiplyCv, 0.8f);
                set (p, id::balance, 1.0f);
            } },
            { id::slopeResponse, [] (Processor& p) {
                set (p, id::slopeCycle, 1.0f);
                set (p, id::multiplyCv, 0.8f);
                set (p, id::balance, 1.0f);
            } },
            { id::mathAmt, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::Math1, dsp::InJack::DynamicsCv);
                set (p, id::contourSustain, 0.0f);
            } },
            { id::midiBCv, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::MidiBCv, dsp::InJack::Osc1VOct);
            } },
            { id::midiBGate, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::MidiBGate, dsp::InJack::ContourGate);
                set (p, id::midiBGate, 0.0f);
            } },
            { id::midiBLfoRate, [] (Processor& p) {
                set (p, id::midiBCv, 3.0f);
                p.getEngine().connect (dsp::OutJack::MidiBCv, dsp::InJack::Osc1VOct);
            } },
            { id::clockDiv, [] (Processor& p) {
                set (p, id::arpOn, 1.0f);
            } },
            { id::arpOn, [] (Processor&) {} },
            { id::arpType, [] (Processor& p) { set (p, id::arpOn, 1.0f); } },
            { id::balanceCv, [] (Processor& p) {
                p.getEngine().connect (dsp::OutJack::ContourCv, dsp::InJack::BalanceCv);
                set (p, id::balance, 0.2f);
            } },
            { id::portamento, [] (Processor& p) { set (p, id::legato, 1.0f); } },
            { id::bendRange, [] (Processor&) {} },
            { id::overtone, [] (Processor& p) { set (p, id::balance, 1.0f); } },
            { id::multiply, [] (Processor& p) {
                set (p, id::balance, 1.0f);
                set (p, id::overtone, 0.45f);
            } },
            { id::legato, [] (Processor& p) {
                set (p, id::contourOnset, 0.06f);
                set (p, id::contourSustain, 0.35f);
            } },
        };

        const std::vector<Event> events {
            { 0, juce::MidiMessage::controllerEvent (1, 1, 100) },
            { 0, juce::MidiMessage::pitchWheel (1, 12288) },
            { 0, on (48, 0.9f) }, { 0, on (55, 0.7f) },
            { 8000, on (60, 0.8f) },
            { 24000, off (48) }, { 24000, off (55) }, { 24000, off (60) }
        };

        const auto renderWith = [&] (const juce::String& pid, float normalised)
        {
            auto p = makeProcessor();
            openVoice (*p);
            if (auto it = contexts.find (pid); it != contexts.end())
                it->second (*p);
            if (auto* r = param (*p, pid))
                r->setValueNotifyingHost (normalised);
            return render (*p, 36000, events);
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
            const bool live = (level > 0.0 && diff > 1.0e-3 * level) || (a.peak() < 1.0e-4f && b.peak() > 0.01f)
                              || (b.peak() < 1.0e-4f && a.peak() > 0.01f);
            if (! live) ++dead;
            check (live, "parameter " + pid.toStdString() + " changes the sound (rel diff "
                   + std::to_string (level > 0 ? diff / level : 0.0) + ")");
        }
        note (std::to_string (reference->getParameters().size()) + " parameters checked, " + std::to_string (dead) + " dead");
    }

    void testUndoAndState()
    {
        auto p = makeProcessor();
        auto& history = p->getUndoHistory();
        auto* overtone = param (*p, id::overtone);
        const auto before = overtone->getValue();
        overtone->beginChangeGesture();
        overtone->setValueNotifyingHost (0.3f);
        overtone->setValueNotifyingHost (0.7f);
        overtone->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (overtone->getValue() - before) < 1.0e-6f, "undo restores the value before the gesture");
        history.redo();
        check (std::abs (overtone->getValue() - 0.7f) < 1.0e-6f, "redo re-applies the whole gesture");

        history.clear();
        overtone->setValueNotifyingHost (0.9f);
        check (! history.canUndo(), "automation (no gesture) is not an undo step");

        p->setCurrentProgram (3);
        check (p->getPresetName() == "Simple Bass", "factory program name");
        juce::MemoryBlock block;
        p->getStateInformation (block);
        auto q = makeProcessor();
        q->setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (get (*q, id::pitch) - get (*p, id::pitch)) < 1.0e-4f, "state restores Pitch");
        check (q->getEngine().numCables() == p->getEngine().numCables(), "state restores cable count");
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.ensureSize (256);
        midi.addEvent (on (36), 0);
        p->processBlock (buffer, midi); // warm up

        midi.clear();
        int news = 0;
        for (int i = 0; i < 64; ++i)
        {
            buffer.clear();
            if (i == 8)
                midi.addEvent (on (40), 10);
            else if (i == 16)
                midi.addEvent (off (36), 0);
            else
                midi.clear();
            allocationCount = 0;
            onAudioThread = true;
            countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            onAudioThread = false;
            news += allocationCount.load();
            midi.clear();
        }
        check (news == 0, "processBlock allocates nothing after prepare ("
               + std::to_string (news) + " news)");

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
        const auto prefix = std::getenv ("DOOBLR0COAST_EDITOR_SNAPSHOT");
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
        check (editor != nullptr, "the editor is a Dooblr0CoastEditor");
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
        check (editor->getScope().getComponentID() == "scope", "live scope is present");

        std::map<juce::String, int> controls;
        for (auto* r : p->getParameters())
        {
            const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
            if (editor->findControl (pid) != nullptr)
                ++controls[pid];
        }
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
        check (editor->getJacks().size() == (size_t) (dsp::outCount + dsp::inCount),
               "all documented jacks are on the board (" + std::to_string (editor->getJacks().size()) + ")");

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            editor->setSize (size.x, size.y);
            const auto sizeLabel = std::to_string (size.x) + "x" + std::to_string (size.y) + " ";

            std::vector<juce::Component*> header;
            for (auto* cid : { "undo", "redo", "preset", "savePreset", "loadPreset", id::output, "version" })
                if (auto* c = editor->findChildWithID (cid))
                    header.push_back (c);
            check (header.size() == 7, sizeLabel + "all header items present");
            for (size_t i = 0; i < header.size(); ++i)
            {
                check (editor->getLocalBounds().contains (header[i]->getBounds()) && header[i]->getBottom() <= 60,
                       sizeLabel + "header item inside the header: " + header[i]->getComponentID().toStdString());
                for (size_t j = i + 1; j < header.size(); ++j)
                    check (! header[i]->getBounds().intersects (header[j]->getBounds()),
                           sizeLabel + "header items do not overlap: " + header[i]->getComponentID().toStdString()
                               + " / " + header[j]->getComponentID().toStdString());
            }

            const auto content = editor->getContentArea().expanded (2, 2);
            check (content.contains (editor->getScope().getBounds()), sizeLabel + "scope in content");
            check (editor->getScope().getHeight() >= 40, sizeLabel + "scope has height");

            std::vector<juce::Component*> board;
            for (auto* r : p->getParameters())
            {
                const auto pid = dynamic_cast<juce::RangedAudioParameter*> (r)->getParameterID();
                if (auto* c = editor->findControl (pid))
                    if (c->getComponentID() != id::output)
                        board.push_back (c);
            }
            for (auto& jack : editor->getJacks())
                board.push_back (jack.get());

            for (auto* c : board)
            {
                check (c->getWidth() > 0 && c->getHeight() > 0, sizeLabel + c->getComponentID().toStdString() + " has size");
                check (editor->getLocalBounds().contains (c->getBounds()),
                       sizeLabel + c->getComponentID().toStdString() + " inside the editor");
            }
        }

        snapshot (*editor, "-default");
        snapshot (*editor, "-board");
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                      testMetadata },
        { "silence without notes",                         testSilence },
        { "pitch tracking and tuning",                     testPitchTracking },
        { "Overtone / Multiply spectral changes",          testOvertoneMultiplySpectrum },
        { "Slope cycle frequency and shape",               testSlopeCycle },
        { "Contour timing",                                testContourTiming },
        { "Dynamics gate behaviour",                       testDynamicsGate },
        { "patch-cable state round-trip and normals",      testPatchCablesAndNormals },
        { "arpeggiator timing",                            testArpeggiatorTiming },
        { "no dead parameters",                            testNoDeadParameters },
        { "undo and state",                                testUndoAndState },
        { "real-time safety and CPU",                      testRealtimeAndCpu },
        { "editor",                                        testEditor },
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
