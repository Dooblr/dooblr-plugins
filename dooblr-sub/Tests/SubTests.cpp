/*
    dooblr-sub console tests: the real processor, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRSUB_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "FixActions.h"
#include "DSP/SubEngine.h"
#include "DSP/SystemCheck.h"
#include "DSP/ToneShift.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iostream>
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
    using namespace dooblrsub;
    using namespace dooblrsub::params;
    using Processor = DooblrSubProcessor;
    using Editor = DooblrSubEditor;

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
    constexpr double twoPi = 6.283185307179586;

    struct Stereo
    {
        std::vector<float> l, r;
        Stereo() = default;
        explicit Stereo (size_t n) : l (n, 0.0f), r (n, 0.0f) {}
        size_t size() const noexcept { return l.size(); }
    };

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

    Stereo render (Processor& p, const Stereo& in, int block = defaultBlock)
    {
        Stereo out (in.size());
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        size_t i = 0;
        while (i < in.size())
        {
            const auto n = (int) std::min ((size_t) block, in.size() - i);
            buffer.setSize (2, n, false, false, true);
            for (int s = 0; s < n; ++s)
            {
                buffer.setSample (0, s, i + (size_t) s < in.l.size() ? in.l[i + (size_t) s] : 0.0f);
                buffer.setSample (1, s, i + (size_t) s < in.r.size() ? in.r[i + (size_t) s] : 0.0f);
            }
            p.processBlock (buffer, midi);
            for (int s = 0; s < n; ++s)
            {
                out.l[i + (size_t) s] = buffer.getSample (0, s);
                out.r[i + (size_t) s] = buffer.getSample (1, s);
            }
            i += (size_t) n;
        }
        return out;
    }

    Stereo sine (size_t n, double hz, double amp, double rate = defaultRate, bool stereoFlip = false)
    {
        Stereo s (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto v = (float) (amp * std::sin (twoPi * hz * (double) i / rate));
            s.l[i] = v;
            s.r[i] = stereoFlip ? -v : v;
        }
        return s;
    }

    bool allFinite (const Stereo& s)
    {
        for (auto v : s.l) if (! std::isfinite (v)) return false;
        for (auto v : s.r) if (! std::isfinite (v)) return false;
        return true;
    }

    double magAt (const std::vector<float>& x, double hz, double rate, size_t start, size_t count)
    {
        std::complex<double> acc { 0.0, 0.0 };
        const auto w = twoPi * hz / rate;
        for (size_t i = 0; i < count; ++i)
            acc += (double) x[start + i] * std::exp (std::complex<double> (0.0, -w * (double) i));
        return std::abs (acc) / (double) count;
    }

    double centroid (const std::vector<float>& x, double rate, size_t start, size_t count, double loHz, double hiHz)
    {
        // Hann-windowed linear-spectrum peak, then a centroid of bins within 12 dB of
        // that peak. A raw rectangular DFT over 50–1600 Hz lets the sinc tails and the
        // noise floor of hundreds of high-frequency bins pull the centroid sharp-side.
        const auto n = (int) count;
        std::vector<double> mag ((size_t) (n / 2), 0.0);
        double peak = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const auto hz = (double) k * rate / (double) n;
            if (hz < loHz || hz > hiHz) continue;
            std::complex<double> acc { 0.0, 0.0 };
            const auto w = twoPi * hz / rate;
            for (size_t i = 0; i < count; ++i)
            {
                const auto hann = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) count);
                acc += (double) x[start + i] * hann * std::exp (std::complex<double> (0.0, -w * (double) i));
            }
            mag[(size_t) k] = std::abs (acc);
            peak = std::max (peak, mag[(size_t) k]);
        }
        const auto floor = peak * 0.25; // ~12 dB down from the modal peak
        double num = 0.0, den = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const auto hz = (double) k * rate / (double) n;
            if (hz < loHz || hz > hiHz || mag[(size_t) k] < floor) continue;
            num += hz * mag[(size_t) k];
            den += mag[(size_t) k];
        }
        return den > 1.0e-12 ? num / den : 0.0;
    }

    int onset (const std::vector<float>& x, float thresh)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) >= thresh)
                return (int) i;
        return -1;
    }

    void defaults (Processor& p)
    {
        for (auto* q : p.getParameters())
            q->setValueNotifyingHost (q->getDefaultValue());
    }

    void quietSafety (Processor& p)
    {
        set (p, id::monoBelow, 0.0f);
        set (p, id::infraHpf, 0.0f);
        set (p, id::subLimit, 0.0f);
    }

    //------------------------------------------------------------------------------
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-sub", "name is dooblr-sub (got " + p->getName().toStdString() + ")");
        check (JucePlugin_IsSynth == 0 && ! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRSUB_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.2.2", "version 0.2.2");
        check (p->getBypassParameter() == p->getState().getParameter (id::bypass), "Bypass is the host bypass parameter");
        check (p->getLatencySamples() == dsp::ToneShift::latencySamples,
               "reported latency is the tone-shift STFT size (" + std::to_string (p->getLatencySamples()) + ")");
        check ((int) p->getParameters().size() == numHostParameters,
               "host parameter count " + std::to_string (p->getParameters().size()));
        note ("version " JucePlugin_VersionString ", " + std::to_string (p->getParameters().size())
              + " params, latency " + std::to_string (p->getLatencySamples()) + " samples");
    }

    void testSubTrackingAndOctave()
    {
        auto p = makeProcessor();
        defaults (*p);
        quietSafety (*p);
        set (*p, id::subAmount, 100.0f);
        set (*p, id::loAmount, 0.0f);
        set (*p, id::frequency, 80.0f);
        set (*p, id::octaveMode, 0.0f);
        set (*p, id::drive, 0.0f);
        set (*p, id::mix, 100.0f);
        set (*p, id::shiftMix, 0.0f);
        set (*p, id::trackingSpeed, 90.0f);

        const auto n = (size_t) (1.6 * defaultRate);
        const auto in = sine (n, 80.0, 0.4);
        const auto out = render (*p, in);
        check (allFinite (out), "80 Hz render is finite");

        const auto skip = (size_t) p->getLatencySamples() + (size_t) (0.45 * defaultRate);
        const auto count = (size_t) (0.5 * defaultRate);
        const auto m40 = magAt (out.l, 40.0, defaultRate, skip, count);
        const auto m80 = magAt (out.l, 80.0, defaultRate, skip, count);
        const auto m55 = magAt (out.l, 55.0, defaultRate, skip, count);
        check (m40 > 0.03 && m40 > 2.0 * m55, "octave-down energy at 40 Hz (40=" + fmt (m40, 4) + " 55=" + fmt (m55, 4) + ")");
        check (p->getEngine().getTrackedHz() > 70.0f && p->getEngine().getTrackedHz() < 90.0f,
               "tracker locks near 80 Hz (got " + fmt (p->getEngine().getTrackedHz(), 1) + ")");
        juce::ignoreUnused (m80);

        // Stepped fundamental 50 -> 90 Hz.
        defaults (*p);
        quietSafety (*p);
        set (*p, id::subAmount, 100.0f);
        set (*p, id::loAmount, 0.0f);
        set (*p, id::octaveMode, 0.0f);
        set (*p, id::frequency, 70.0f);
        set (*p, id::trackingSpeed, 95.0f);
        set (*p, id::mix, 100.0f);
        set (*p, id::drive, 0.0f);

        const auto n2 = (size_t) (2.2 * defaultRate);
        Stereo stepped (n2);
        const auto half = n2 / 2;
        for (size_t i = 0; i < n2; ++i)
        {
            const auto hz = i < half ? 50.0 : 90.0;
            const auto v = (float) (0.4 * std::sin (twoPi * hz * (double) i / defaultRate));
            stepped.l[i] = stepped.r[i] = v;
        }
        const auto out2 = render (*p, stepped);
        const auto lat = (size_t) p->getLatencySamples();
        const auto a = magAt (out2.l, 25.0, defaultRate, lat + (size_t) (0.35 * defaultRate), (size_t) (0.4 * defaultRate));
        const auto b = magAt (out2.l, 45.0, defaultRate, lat + half + (size_t) (0.4 * defaultRate), (size_t) (0.4 * defaultRate));
        check (a > 0.02, "tracks 50 Hz -> sub near 25 Hz (" + fmt (a, 4) + ")");
        check (b > 0.02, "tracks 90 Hz -> sub near 45 Hz (" + fmt (b, 4) + ")");

        // No clicks: high-pass the transition and require a modest peak.
        const auto t0 = (int) (half + lat);
        float clickPeak = 0.0f, prev = 0.0f;
        for (int i = t0 - 64; i < t0 + 2048 && i < (int) out2.l.size(); ++i)
        {
            const auto hp = out2.l[(size_t) i] - prev;
            prev = out2.l[(size_t) i];
            clickPeak = std::max (clickPeak, std::abs (hp));
        }
        check (clickPeak < 0.35f, "no click at the note change (hp peak " + fmt (clickPeak, 3) + ")");
    }

    Stereo dampedTone (size_t n, double f0, bool withClick)
    {
        Stereo in (n);
        const auto tau = 0.18 * defaultRate;
        if (withClick) in.l[0] = in.r[0] = 0.95f;
        for (size_t i = 0; i < n; ++i)
        {
            const auto env = (float) std::exp (-(double) i / tau);
            const auto body = env * (float) (0.7 * std::sin (twoPi * f0 * (double) i / defaultRate));
            in.l[i] += body;
            in.r[i] += body;
        }
        return in;
    }

    void testToneShift()
    {
        const auto n = (size_t) (1.0 * defaultRate);

        {
            auto p = makeProcessor();
            defaults (*p);
            quietSafety (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            set (*p, id::mix, 100.0f);
            set (*p, id::shift, 400.0f);
            set (*p, id::shiftMix, 100.0f);
            set (*p, id::transient, 100.0f);
            set (*p, id::bodyMix, 0.0f);
            const auto in = dampedTone (n, 200.0, true);
            const auto out = render (*p, in);
            const auto lat = p->getLatencySamples();
            const auto cin = onset (in.l, 0.4f);
            const auto cout = onset (out.l, 0.12f);
            check (allFinite (out), "transient-only path is finite");
            check (cout >= 0 && std::abs ((cout - lat) - cin) <= 16,
                   "transient onset aligned within 16 samples (in " + std::to_string (cin)
                   + " out " + std::to_string (cout) + " lat " + std::to_string (lat) + ")");
        }

        for (float cents : { -1200.0f, -300.0f, 300.0f, 1200.0f })
        {
            auto p = makeProcessor();
            defaults (*p);
            quietSafety (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            set (*p, id::mix, 100.0f);
            set (*p, id::shift, cents);
            set (*p, id::shiftMix, 100.0f);
            set (*p, id::transient, 0.0f);
            set (*p, id::bodyMix, 100.0f);
            const auto in = dampedTone (n, 200.0, false);
            const auto out = render (*p, in);
            const auto lat = p->getLatencySamples();
            const auto ratio = std::pow (2.0, (double) cents / 1200.0);
            const auto skip = (size_t) lat + (size_t) (0.04 * defaultRate);
            const auto count = (size_t) (0.30 * defaultRate);
            const auto cIn = centroid (in.l, defaultRate, (size_t) (0.02 * defaultRate), count, 80.0, 800.0);
            const auto cOut = centroid (out.l, defaultRate, skip, count, 50.0, 1600.0);
            check (allFinite (out), "tone shift " + fmt (cents, 0) + " c finite");
            const auto expected = cIn * ratio;
            const auto rel = expected > 1.0 ? std::abs (cOut - expected) / expected : 1.0;
            const auto dirOk = (cents > 0.0f) ? (cOut > cIn * 1.08) : (cOut < cIn * 0.92);
            check (dirOk && rel < 0.45,
                   "body centroid moves with " + fmt (cents, 0) + " c (in " + fmt (cIn, 1)
                   + " out " + fmt (cOut, 1) + " expect ~" + fmt (expected, 1) + " rel " + fmt (rel, 2) + ")");
        }

        auto p = makeProcessor();
        defaults (*p);
        quietSafety (*p);
        set (*p, id::subAmount, 0.0f);
        set (*p, id::loAmount, 0.0f);
        set (*p, id::shift, 0.0f);
        set (*p, id::shiftMix, 0.0f);
        Stereo imp ((size_t) (dsp::ToneShift::latencySamples * 3 + 2048));
        imp.l[0] = imp.r[0] = 1.0f;
        const auto y = render (*p, imp);
        const auto actual = onset (y.l, 0.05f);
        const auto reported = p->getLatencySamples();
        check (actual >= 0 && std::abs (actual - reported) <= dsp::ToneShift::hopSize,
               "reported latency " + std::to_string (reported) + " equals actual impulse delay "
               + std::to_string (actual));
        check ((int) y.size() == (int) imp.size(), "duration unchanged");
    }

    void testSystemCheck()
    {
        using namespace dsp;
        ReasonList reasons;

        const auto n = (int) (1.0 * defaultRate);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto v = (float) (0.35 * std::sin (twoPi * 50.0 * (double) i / defaultRate));
            L[(size_t) i] = R[(size_t) i] = v;
        }
        auto a = analyseBuffer (L.data(), R.data(), n, defaultRate);
        auto v = evaluate (a, Target::club, reasons);
        check (v == Verdict::works, "mono 50 Hz sine: Works on big subs (got " + std::string (verdictLabel (v)) + ")");

        for (int i = 0; i < n; ++i)
        {
            const auto v18 = (float) (0.4 * std::sin (twoPi * 18.0 * (double) i / defaultRate));
            L[(size_t) i] = R[(size_t) i] = v18;
        }
        a = analyseBuffer (L.data(), R.data(), n, defaultRate);
        v = evaluate (a, Target::club, reasons);
        bool infra = false;
        for (int i = 0; i < reasons.count; ++i)
            infra = infra || juce::String (reasons.text[i]).containsIgnoreCase ("below 30");
        check (v == Verdict::wont && infra, "18 Hz content: Won't translate (below 30 Hz)");

        for (int i = 0; i < n; ++i)
        {
            const auto v50 = (float) (0.35 * std::sin (twoPi * 50.0 * (double) i / defaultRate));
            L[(size_t) i] = v50;
            R[(size_t) i] = -v50;
        }
        a = analyseBuffer (L.data(), R.data(), n, defaultRate);
        v = evaluate (a, Target::club, reasons);
        bool stereo = false;
        for (int i = 0; i < reasons.count; ++i)
            stereo = stereo || juce::String (reasons.text[i]).containsIgnoreCase ("Stereo");
        check (v == Verdict::wont && stereo, "out-of-phase stereo sub: Won't translate");

        for (int i = 0; i < n; ++i)
        {
            auto v50 = (float) (1.4 * std::sin (twoPi * 50.0 * (double) i / defaultRate));
            v50 = juce::jlimit (-1.0f, 1.0f, v50);
            L[(size_t) i] = R[(size_t) i] = v50;
        }
        a = analyseBuffer (L.data(), R.data(), n, defaultRate);
        v = evaluate (a, Target::club, reasons);
        bool clip = false;
        for (int i = 0; i < reasons.count; ++i)
            clip = clip || juce::String (reasons.text[i]).containsIgnoreCase ("clipping");
        check (v == Verdict::wont && clip, "clipped sub: Won't translate");

        Analysis cancel = a;
        cancel.cancellation = -0.8f;
        cancel.headroomPeak = 0.4f;
        cancel.subPeak = 0.4f;
        cancel.lrCorrelation = 1.0f;
        cancel.sideRms = 0.0f;
        cancel.infraRms = 0.0f;
        cancel.dc = 0.0f;
        cancel.sustainedDb = -12.0f;
        v = evaluate (cancel, Target::club, reasons);
        bool cancels = false;
        for (int i = 0; i < reasons.count; ++i)
            cancels = cancels || juce::String (reasons.text[i]).containsIgnoreCase ("cancel");
        check (v == Verdict::wont && cancels, "sub cancels fundamental");
        check (reasons.contains (CheckId::cancellation), "cancellation reason carries CheckId");

        Analysis weak {};
        weak.subRms = 0.02f;
        weak.midRms = 0.25f;
        weak.lrCorrelation = 1.0f;
        weak.headroomPeak = 0.3f;
        weak.subPeak = 0.3f;
        weak.sustainedDb = -34.0f;
        v = evaluate (weak, Target::club, reasons);
        check (reasons.contains (CheckId::subWeak), "sub too weak vs mids");

        Analysis strong = weak;
        strong.subRms = 0.40f;
        strong.midRms = 0.04f;
        strong.sustainedDb = -8.0f;
        v = evaluate (strong, Target::club, reasons);
        check (reasons.contains (CheckId::subStrong), "sub too strong vs mids");

        Analysis mudA {};
        mudA.subRms = 0.20f;
        mudA.mudRms = 0.18f;
        mudA.bandRms[1] = 0.02f;
        mudA.bandRms[2] = 0.02f;
        mudA.lrCorrelation = 1.0f;
        mudA.headroomPeak = 0.3f;
        mudA.subPeak = 0.3f;
        mudA.sustainedDb = -14.0f;
        v = evaluate (mudA, Target::club, reasons);
        check (reasons.contains (CheckId::mud), "resonant mud");

        Analysis lowF {};
        lowF.subRms = 0.15f;
        lowF.lrCorrelation = 1.0f;
        lowF.headroomPeak = 0.3f;
        lowF.subPeak = 0.3f;
        lowF.sustainedDb = -16.0f;
        lowF.generatedHz = 18.0f;
        v = evaluate (lowF, Target::club, reasons);
        check (reasons.contains (CheckId::fundamentalLow), "fundamental too low");

        Analysis thin = lowF;
        thin.generatedHz = 0.0f;
        thin.pitchHz = 50.0f;
        thin.harmonicRatio = 0.02f;
        v = evaluate (thin, Target::club, reasons);
        check (reasons.contains (CheckId::harmonicThin), "missing harmonics");
    }

    void testNoDeadParameters()
    {
        const auto n = (size_t) (0.55 * defaultRate);
        Stereo in (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto gate = (i / 2400) % 2 == 0 ? 1.0 : 0.15;
            const auto hz = 55.0 + 25.0 * std::sin (twoPi * 2.0 * (double) i / defaultRate);
            in.l[i] = (float) (gate * 0.4 * std::sin (twoPi * hz * (double) i / defaultRate));
            in.r[i] = (float) (gate * 0.35 * std::sin (twoPi * (hz + 0.7) * (double) i / defaultRate));
        }

        auto snapshot = [&] (Processor& p)
        {
            p.prepareToPlay (defaultRate, defaultBlock);
            return render (p, in);
        };

        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::shift, 400.0f);
        set (*p, id::shiftMix, 40.0f);
        const auto base = snapshot (*p);

        struct Case { const char* pid; float value; const char* why; const char* prepId; float prepValue; };
        const Case cases[] {
            { id::subAmount, 100.0f, "Sub", nullptr, 0.0f },
            { id::loAmount, 80.0f, "Lo", nullptr, 0.0f },
            { id::frequency, 40.0f, "Frequency", nullptr, 0.0f },
            { id::octaveMode, 2.0f, "Octave", nullptr, 0.0f },
            { id::trackingSpeed, 5.0f, "Tracking", nullptr, 0.0f },
            { id::drive, 90.0f, "Drive", nullptr, 0.0f },
            { id::mix, 30.0f, "Mix", nullptr, 0.0f },
            { id::output, 6.0f, "Output", nullptr, 0.0f },
            { id::shift, -900.0f, "Shift", nullptr, 0.0f },
            { id::transient, 10.0f, "Transient", nullptr, 0.0f },
            { id::bodyMix, 20.0f, "Body", nullptr, 0.0f },
            { id::shiftMix, 100.0f, "Shift Mix", nullptr, 0.0f },
            { id::monoBelow, 0.0f, "Mono below", nullptr, 0.0f },
            { id::monoFreq, 70.0f, "Mono freq", nullptr, 0.0f },
            { id::infraHpf, 0.0f, "Infra HPF", nullptr, 0.0f },
            { id::infraFreq, 35.0f, "Infra freq", nullptr, 0.0f },
            { id::infraSlope, 2.0f, "Infra slope", nullptr, 0.0f },
            { id::subLimit, 0.0f, "Sub limit", nullptr, 0.0f },
            { id::target, 1.0f, "Target", nullptr, 0.0f },
            { id::bypass, 1.0f, "Bypass", nullptr, 0.0f },
            { id::eqShelfGain, 6.0f, "Shelf", nullptr, 0.0f },
            { id::eqShelfFreq, 50.0f, "Shelf Hz", id::eqShelfGain, 6.0f },
            { id::eqPeakGain, -6.0f, "Peak", nullptr, 0.0f },
            { id::eqPeakFreq, 110.0f, "Peak Hz", id::eqPeakGain, -6.0f },
            { id::eqPeakQ, 3.0f, "Peak Q", id::eqPeakGain, -6.0f },
            { id::eqPresenceGain, 4.0f, "Presence", nullptr, 0.0f },
            { id::eqPresenceFreq, 200.0f, "Presence Hz", id::eqPresenceGain, 4.0f },
            { id::eqPresenceQ, 2.5f, "Presence Q", id::eqPresenceGain, 4.0f },
            { id::harmAmount, 70.0f, "Harmonic", nullptr, 0.0f },
            { id::phaseInvert, 1.0f, "Invert", nullptr, 0.0f },
            { id::phaseDelay, 6.0f, "Align", nullptr, 0.0f },
        };

        for (const auto& c : cases)
        {
            defaults (*p);
            set (*p, id::shift, 400.0f);
            set (*p, id::shiftMix, 40.0f);
            if (c.prepId != nullptr)
                set (*p, c.prepId, c.prepValue);
            set (*p, c.pid, c.value);
            const auto out = snapshot (*p);
            float diff = 0.0f;
            const auto start = n / 5;
            for (size_t i = start; i < n; ++i)
                diff = std::max (diff, std::abs (out.l[i] - base.l[i]));
            check (diff > 1.0e-4f, std::string (c.why) + " changes the audio (max diff " + fmt (diff, 6) + ")");
        }
    }

    void testStatePresetsUndo()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::subAmount, 77.0f);
        set (*p, id::shift, -250.0f);
        juce::MemoryBlock blob;
        p->getStateInformation (blob);
        defaults (*p);
        p->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*p, id::subAmount) - 77.0f) < 0.2f && std::abs (get (*p, id::shift) + 250.0f) < 1.5f,
               "state round-trip");

        set (*p, id::eqPeakGain, -5.0f);
        set (*p, id::harmAmount, 40.0f);
        set (*p, id::phaseDelay, 3.5f);
        juce::MemoryBlock blob2;
        p->getStateInformation (blob2);
        defaults (*p);
        p->setStateInformation (blob2.getData(), (int) blob2.getSize());
        check (std::abs (get (*p, id::eqPeakGain) + 5.0f) < 0.2f
               && std::abs (get (*p, id::harmAmount) - 40.0f) < 0.2f
               && std::abs (get (*p, id::phaseDelay) - 3.5f) < 0.05f,
               "new-param state round-trip");

        auto pOld = makeProcessor();
        set (*pOld, id::subAmount, 77.0f);
        auto oldTree = pOld->getState().copyState();
        oldTree.setProperty ("stateVersion", 1, nullptr);
        const char* newIds[] {
            id::eqShelfGain, id::eqShelfFreq, id::eqPeakGain, id::eqPeakFreq, id::eqPeakQ,
            id::eqPresenceGain, id::eqPresenceFreq, id::eqPresenceQ, id::harmAmount,
            id::phaseInvert, id::phaseDelay, id::infraSlope
        };
        for (int i = oldTree.getNumChildren(); --i >= 0;)
        {
            auto child = oldTree.getChild (i);
            const auto pid = child.getType().toString();
            const auto named = child.getProperty ("id").toString();
            for (auto* nid : newIds)
                if (pid == nid || named == nid)
                    oldTree.removeChild (i, nullptr);
        }
        auto p2 = makeProcessor();
        p2->getState().replaceState (oldTree);
        check (std::abs (get (*p2, id::subAmount) - 77.0f) < 0.2f
               && std::abs (get (*p2, id::eqShelfGain)) < 0.2f
               && std::abs (get (*p2, id::harmAmount)) < 0.2f
               && get (*p2, id::phaseInvert) < 0.5f,
               "v0.1.0 state loads with new modules at defaults");

        p->setCurrentProgram (1);
        check (p->getProgramName (1) == "808 Weight" && get (*p, id::subAmount) > 70.0f, "factory 808 Weight");
        check (p->getNumPrograms() >= 8, "at least 8 factory presets");

        auto& history = p->getUndoHistory();
        auto* sub = param (*p, id::subAmount);
        const auto before = sub->getValue();
        sub->beginChangeGesture();
        sub->setValueNotifyingHost (0.1f);
        sub->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (sub->getValue() - before) < 1.0e-5f, "undo restores the value");
        history.redo();
        check (std::abs (sub->getValue() - 0.1f) < 1.0e-5f, "redo reapplies the value");
    }

    void testCpuAndNan()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::subAmount, 80.0f);
        set (*p, id::shift, 500.0f);
        set (*p, id::shiftMix, 70.0f);
        const auto n = (size_t) (2.0 * defaultRate);
        const auto in = sine (n, 60.0, 0.3);
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = render (*p, in);
        const auto t1 = std::chrono::steady_clock::now();
        const auto audioSec = (double) n / defaultRate;
        const auto wallSec = std::chrono::duration<double> (t1 - t0).count();
        check (allFinite (out), "CPU run stays finite");
        check (wallSec < audioSec * 2.5, "CPU sanity: " + fmt (wallSec, 3) + " s wall for " + fmt (audioSec, 2) + " s audio");
        note ("CPU " + fmt (100.0 * wallSec / audioSec, 1) + " % of realtime");

        defaults (*p);
        set (*p, id::eqPeakGain, -5.0f);
        set (*p, id::harmAmount, 50.0f);
        set (*p, id::phaseDelay, 4.0f);
        set (*p, id::phaseInvert, 1.0f);
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        for (int s = 0; s < defaultBlock; ++s)
        {
            const auto v = (float) (0.3 * std::sin (twoPi * 55.0 * (double) s / defaultRate));
            buffer.setSample (0, s, v);
            buffer.setSample (1, s, v);
        }
        p->processBlock (buffer, midi); // warm-up
        countAllocations.store (true);
        allocationCount.store (0);
        p->processBlock (buffer, midi);
        const auto allocs = allocationCount.load();
        countAllocations.store (false);
        check (allocs == 0, "processBlock allocates nothing after prepare (" + std::to_string (allocs) + ")");
        check (std::isfinite (buffer.getSample (0, 0)) && std::isfinite (buffer.getSample (1, defaultBlock - 1)),
               "modules-on block stays finite");
    }

    float peakOf (const Stereo& s, size_t start, size_t count)
    {
        float p = 0.0f;
        const auto end = std::min (s.size(), start + count);
        for (size_t i = start; i < end; ++i)
            p = std::max (p, std::max (std::abs (s.l[i]), std::abs (s.r[i])));
        return p;
    }

    float rmsOf (const Stereo& s, size_t start, size_t count)
    {
        double acc = 0.0;
        const auto end = std::min (s.size(), start + count);
        int n = 0;
        for (size_t i = start; i < end; ++i)
        {
            acc += 0.5 * ((double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]);
            ++n;
        }
        return n > 0 ? (float) std::sqrt (acc / (double) n) : 0.0f;
    }

    Stereo mixSines (size_t n, std::initializer_list<std::tuple<double, double, bool>> tones)
    {
        Stereo s (n);
        for (auto [hz, amp, flip] : tones)
        {
            for (size_t i = 0; i < n; ++i)
            {
                const auto v = (float) (amp * std::sin (twoPi * hz * (double) i / defaultRate));
                s.l[i] += v;
                s.r[i] += flip ? -v : v;
            }
        }
        return s;
    }

    bool reasonsHas (const dsp::ReasonList& r, dsp::CheckId id)
    {
        return r.contains (id);
    }

    dsp::Verdict evaluateTail (Processor& p, const Stereo& in, dsp::ReasonList& reasons, dsp::Analysis* outA = nullptr)
    {
        const auto out = render (p, in);
        const auto live = p.getEngine().display();
        // 0.5 s is an integer number of 18 Hz and 50 Hz cycles at 48 kHz, so the
        // rectangular DFT in analyseBuffer does not leak 50 Hz into the 1–25 Hz band.
        const auto count = (int) std::min ((size_t) (0.5 * defaultRate), in.size() * 2 / 3);
        const auto start = (int) out.size() - count;
        auto a = dsp::analyseBuffer (out.l.data() + start, out.r.data() + start, count, defaultRate,
                                     live.analysis.cancellation, live.analysis.pitchHz, live.analysis.pitchStd,
                                     live.analysis.generatedHz);
        if (outA != nullptr) *outA = a;
        return dsp::evaluate (a, p.readSettings().target, reasons);
    }

    std::string formatReasons (const dsp::ReasonList& r)
    {
        juce::String s;
        for (int i = 0; i < r.count; ++i)
        {
            if (i) s += ", ";
            s += r.text[i];
        }
        return s.toStdString();
    }

    std::string formatAnalysis (const dsp::Analysis& a)
    {
        return "infra=" + fmt (a.infraRms, 3) + " sub=" + fmt (a.subRms, 3)
            + " mid=" + fmt (a.midRms, 3) + " mud=" + fmt (a.mudRms, 3)
            + " mudHz=" + fmt (a.mudHz, 0) + " corr=" + fmt (a.lrCorrelation, 2)
            + " side=" + fmt (a.sideRms, 3) + " harmR=" + fmt (a.harmonicRatio, 2)
            + " peak=" + fmt (a.headroomPeak, 2) + " dc=" + fmt (a.dc, 3)
            + " pitch=" + fmt (a.pitchHz, 1) + " genHz=" + fmt (a.generatedHz, 1)
            + " b0=" + fmt (a.bandRms[0], 3);
    }

    void expectSane (const Stereo& before, const Stereo& after, const std::string& label)
    {
        check (allFinite (after), label + " stays finite");
        const auto lat = (size_t) (0.25 * defaultRate);
        const auto count = (size_t) (0.4 * defaultRate);
        const auto peak = peakOf (after, lat, count);
        check (peak < 0.999f, label + " no clipping (peak " + fmt (peak, 3) + ")");
        const auto a = rmsOf (before, lat, count);
        const auto b = rmsOf (after, lat, count);
        const auto ratio = (a > 1.0e-6f && b > 1.0e-6f) ? std::max (a, b) / std::min (a, b) : 1.0f;
        check (ratio < 6.0f, label + " level jump within 15 dB (ratio " + fmt (ratio, 2) + ")");
    }

    void testFixAndModules()
    {
        using namespace dsp;
        const auto n = (size_t) (1.15 * defaultRate);

        {
            auto p = makeProcessor();
            defaults (*p);
            const auto in = sine (n, 50.0, 0.35);
            const auto a = render (*p, in);
            set (*p, id::eqShelfGain, 0.0f);
            set (*p, id::eqPeakGain, 0.0f);
            set (*p, id::eqPresenceGain, 0.0f);
            set (*p, id::harmAmount, 0.0f);
            set (*p, id::phaseInvert, 0.0f);
            set (*p, id::phaseDelay, 0.0f);
            p->prepareToPlay (defaultRate, defaultBlock);
            const auto b = render (*p, in);
            float diff = 0.0f;
            for (size_t i = n / 4; i < n; ++i)
                diff = std::max (diff, std::abs (a.l[i] - b.l[i]));
            check (diff < 1.0e-5f, "new modules at defaults match a fresh 0.1.0-style path (diff " + fmt (diff, 8) + ")");
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            quietSafety (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            const auto in = mixSines (n, { { 18.0, 0.20, false }, { 50.0, 0.40, false } });
            ReasonList beforeR, afterR;
            Analysis afterA;
            evaluateTail (*p, in, beforeR);
            check (reasonsHas (beforeR, CheckId::infra), "infra fails before Fix");
            check (p->getFixActions().apply (CheckId::infra), "infra Fix applied");
            check (get (*p, id::infraHpf) > 0.5f, "infra Fix enables the HPF");
            check (get (*p, id::infraSlope) > 1.5f, "infra Fix sets 24 dB/oct slope");
            evaluateTail (*p, in, afterR, &afterA);
            if (reasonsHas (afterR, CheckId::infra))
                note ("infra after: " + formatReasons (afterR) + " " + formatAnalysis (afterA));
            check (! reasonsHas (afterR, CheckId::infra), "infra passes after Fix");
            expectSane (in, render (*p, in), "infra");
            check (p->getFixActions().canUndo() && p->getFixActions().undoLast(), "Undo Fix restores");
            check (get (*p, id::infraHpf) < 0.5f, "Undo Fix restored infra HPF off");
        }

        auto run = [&] (const char* name, CheckId id, Processor& p, const Stereo& in, const std::function<void()>& extra = {})
        {
            ReasonList beforeR, afterR;
            Analysis afterA;
            evaluateTail (p, in, beforeR);
            check (reasonsHas (beforeR, id), std::string (name) + " fails before Fix");
            check (p.getFixActions().apply (id), std::string (name) + " Fix applied");
            if (extra) extra();
            evaluateTail (p, in, afterR, &afterA);
            if (reasonsHas (afterR, id))
                note (std::string (name) + " after: " + formatReasons (afterR) + " " + formatAnalysis (afterA));
            check (! reasonsHas (afterR, id), std::string (name) + " passes after Fix");
            expectSane (in, render (p, in), name);
        };

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::monoBelow, 0.0f);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            Stereo in (n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto w = twoPi * 50.0 * (double) i / defaultRate;
                in.l[i] = (float) (0.35 * std::sin (w));
                in.r[i] = (float) (0.35 * std::sin (w + 2.2));
            }
            run ("stereo", CheckId::stereoLows, *p, in, [&] {
                check (get (*p, id::monoBelow) > 0.5f, "stereo Fix enables mono-below");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::subLimit, 0.0f);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            run ("clipping", CheckId::clipping, *p, sine (n, 50.0, 1.15), [&] {
                check (get (*p, id::subLimit) > 0.5f, "clipping Fix enables the limiter");
            });
        }

        {
            Analysis fake;
            fake.cancellation = -0.8f;
            fake.pitchHz = 50.0f;
            auto tmp = makeProcessor();
            const auto cur = FixActions::read (tmp->getState());
            const auto proposal = FixActions::propose (CheckId::cancellation, fake, cur);
            check (proposal.values.phaseInvert != cur.phaseInvert, "cancellation recipe toggles invert");

            auto p = makeProcessor();
            defaults (*p);
            quietSafety (*p);
            set (*p, id::octaveMode, 1.0f);
            set (*p, id::subAmount, 100.0f);
            set (*p, id::phaseInvert, 1.0f);
            const auto in = sine (n, 50.0, 0.35);
            render (*p, in);
            check (p->getFixActions().apply (CheckId::cancellation), "cancellation Fix applied");
            check (get (*p, id::phaseInvert) < 0.5f, "cancellation Fix restores polarity");
            expectSane (in, render (*p, in), "cancellation");
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::infraHpf, 0.0f);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            Stereo in (n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto v = (float) (0.25 * std::sin (twoPi * 50.0 * (double) i / defaultRate) + 0.06);
                in.l[i] = in.r[i] = v;
            }
            run ("DC", CheckId::dcOffset, *p, in, [&] {
                check (get (*p, id::infraHpf) > 0.5f, "DC Fix enables the HPF");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::trackingSpeed, 5.0f);
            Stereo in (n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto hz = ((i / 600) % 2 == 0) ? 42.0 : 95.0;
                const auto v = (float) (0.35 * std::sin (twoPi * hz * (double) i / defaultRate));
                in.l[i] = in.r[i] = v;
            }
            render (*p, in);
            Analysis fake = p->getEngine().display().analysis;
            fake.pitchHz = 60.0f;
            fake.pitchStd = 30.0f;
            const auto cur = FixActions::read (p->getState());
            const auto proposal = FixActions::propose (CheckId::unstablePitch, fake, cur);
            check (proposal.values.trackingSpeed > cur.trackingSpeed, "unstable-pitch recipe raises Tracking");
            check (p->getFixActions().apply (CheckId::unstablePitch), "unstable pitch Fix applied");
            check (get (*p, id::trackingSpeed) > 50.0f, "unstable pitch Fix raises Tracking");
            expectSane (in, render (*p, in), "unstable pitch");
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::subLimit, 0.0f);
            set (*p, id::subAmount, 0.0f);
            run ("sustained", CheckId::sustained, *p, sine (n, 50.0, 1.12));
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            set (*p, id::frequency, 80.0f);
            const auto in = mixSines (n, { { 80.0, 0.06, false }, { 400.0, 0.65, false } });
            run ("sub-weak", CheckId::subWeak, *p, in, [&] {
                check (get (*p, id::subAmount) > 40.0f, "sub-weak Fix raises Sub");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            run ("sub-strong", CheckId::subStrong, *p, mixSines (n, { { 50.0, 0.55, false }, { 400.0, 0.06, false } }));
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            run ("mud", CheckId::mud, *p, mixSines (n, { { 110.0, 0.45, false }, { 50.0, 0.08, false } }), [&] {
                check (get (*p, id::eqPeakGain) < -1.0f, "mud Fix cuts the peak band");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::octaveMode, 0.0f);
            set (*p, id::subAmount, 80.0f);
            set (*p, id::frequency, 40.0f);
            set (*p, id::trackingSpeed, 80.0f);
            run ("fundamental-low", CheckId::fundamentalLow, *p, sine (n, 40.0, 0.35), [&] {
                check (std::abs (get (*p, id::octaveMode) - 1.0f) < 0.1f, "fundamental-low Fix switches to Unison");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::drive, 0.0f);
            set (*p, id::harmAmount, 0.0f);
            set (*p, id::subAmount, 0.0f);
            set (*p, id::loAmount, 0.0f);
            run ("harmonics", CheckId::harmonicThin, *p, sine (n, 50.0, 0.35), [&] {
                check (get (*p, id::harmAmount) > 10.0f, "harmonics Fix raises the exciter");
            });
        }

        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::monoBelow, 0.0f);
            set (*p, id::infraHpf, 0.0f);
            set (*p, id::subLimit, 0.0f);
            Stereo in (n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto w18 = twoPi * 18.0 * (double) i / defaultRate;
                const auto w50 = twoPi * 50.0 * (double) i / defaultRate;
                in.l[i] = (float) (0.22 * std::sin (w18) + 0.32 * std::sin (w50));
                in.r[i] = (float) (0.22 * std::sin (w18) + 0.32 * std::sin (w50 + 2.2));
            }
            ReasonList beforeR, afterR;
            evaluateTail (*p, in, beforeR);
            check (beforeR.count >= 2, "Fix All fixture has several failing checks");
            check (p->getFixActions().applyAll(), "Fix All applied");
            check (p->getUndoHistory().canUndo(), "Fix All is one host-undoable gesture");
            evaluateTail (*p, in, afterR);
            check (afterR.count < beforeR.count, "Fix All improves the verdict");
            expectSane (in, render (*p, in), "Fix All");
            const auto infraBefore = get (*p, id::infraHpf);
            p->getUndoHistory().undo();
            check (std::abs (get (*p, id::infraHpf) - infraBefore) > 0.25f
                   || get (*p, id::monoBelow) < 0.5f,
                   "host undo reverts the Fix All gesture");
        }
    }

    struct EditorFixture
    {
        std::unique_ptr<Processor> processor;
        Editor* editor = nullptr;
        EditorFixture()
        {
            processor = makeProcessor();
            editor = dynamic_cast<Editor*> (processor->createEditor());
            if (editor == nullptr)
                check (false, "createEditor returns DooblrSubEditor");
            else
            {
                // Bounds and layout are valid without a native peer. Adding to the desktop on
                // headless Xvfb can raise a fatal X_ChangeProperty error, so skip it.
                editor->setVisible (true);
            }
        }
        ~EditorFixture()
        {
            if (editor != nullptr)
                delete editor;
        }
    };

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

    void testEditor()
    {
        EditorFixture f;
        auto* editor = f.editor;
        if (editor == nullptr) return;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660,
               "default size 1080 x 660 (" + std::to_string (editor->getWidth()) + " x " + std::to_string (editor->getHeight()) + ")");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600
                   && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400,
                   "resize limits 940 x 600 .. 2400 x 1400");
        else
            check (false, "the editor is resizable with limits");

        juce::Component* found = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (c.getComponentID() == "version") found = &c;
            for (auto* child : c.getChildren()) if (child != nullptr) walk (*child);
        };
        walk (*editor);
        auto* version = dynamic_cast<juce::Label*> (found);
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString),
               "the version label shows " JucePlugin_VersionString);

        for (auto* raw : f.processor->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            int count = 0;
            std::function<void (juce::Component&)> countWalk = [&] (juce::Component& c)
            {
                if (c.getComponentID() == pid && isControl (c)) ++count;
                for (auto* child : c.getChildren()) if (child != nullptr) countWalk (*child);
            };
            countWalk (*editor);
            check (count == 1, pid.toStdString() + " has exactly one control (" + std::to_string (count) + ")");
            check (editor->findControl (pid) != nullptr, "findControl finds " + pid.toStdString());
        }

        const juce::Point<int> sizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
        for (auto size : sizes)
        {
            editor->setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> rects;
            forEachVisible (*editor, [&] (juce::Component& c)
            {
                if (! isControl (c)) return;
                rects.push_back ({ &c, editor->getLocalArea (c.getParentComponent(), c.getBounds()) });
            });
            int overlaps = 0, outside = 0, tooSmall = 0;
            for (size_t i = 0; i < rects.size(); ++i)
            {
                const auto& [c, r] = rects[i];
                if (! editor->getLocalBounds().contains (r))
                {
                    ++outside;
                    check (false, label + ": " + c->getComponentID().toStdString() + " inside the window");
                }
                if (r.getWidth() < 16 || r.getHeight() < 14)
                {
                    ++tooSmall;
                    check (false, label + ": " + c->getComponentID().toStdString() + " large enough (" + r.toString().toStdString() + ")");
                }
                for (size_t j = i + 1; j < rects.size(); ++j)
                {
                    const auto& [d, s] = rects[j];
                    if (c->isParentOf (d) || d->isParentOf (c)) continue;
                    if (! r.getIntersection (s).isEmpty())
                    {
                        ++overlaps;
                        check (false, label + ": " + c->getComponentID().toStdString() + " overlaps " + d->getComponentID().toStdString()
                               + " " + r.toString().toStdString() + " / " + s.toString().toStdString());
                    }
                }
            }
            check (overlaps == 0 && outside == 0 && tooSmall == 0, label + " layout fit (" + std::to_string (rects.size()) + " controls)");

            auto* mix = editor->findControl (id::mix);
            check (mix != nullptr, label + ": Mix control exists");
            if (mix != nullptr && mix->getParentComponent() != nullptr)
            {
                auto* panel = mix->getParentComponent();
                const auto mixInPanel = panel->getLocalArea (mix, mix->getLocalBounds());
                check (panel->getLocalBounds().contains (mixInPanel),
                       label + ": Mix slider inside the Sub panel (" + mixInPanel.toString().toStdString()
                       + " in " + panel->getLocalBounds().toString().toStdString() + ")");
                for (auto* child : mix->getChildren())
                {
                    if (dynamic_cast<juce::Label*> (child) == nullptr) continue;
                    const auto box = panel->getLocalArea (child, child->getLocalBounds());
                    check (panel->getLocalBounds().contains (box),
                           label + ": Mix value box inside the Sub panel (" + box.toString().toStdString()
                           + " in " + panel->getLocalBounds().toString().toStdString() + ")");
                }
            }
        }

        auto* fixAll = editor->findControl ("fixAll");
        auto* undoFix = editor->findControl ("undoFix");
        check (fixAll != nullptr && undoFix != nullptr, "Fix All and Undo Fix buttons exist");
        check (! fixAll->isEnabled(), "Fix All is disabled when every check passes");
        check (! undoFix->isEnabled(), "Undo Fix is disabled before a Fix");

        auto snapshot = [&] (const char* suffix)
        {
            const auto prefix = std::getenv ("DOOBLRSUB_EDITOR_SNAPSHOT");
            if (prefix == nullptr)
                return;
            editor->updateMeters();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::File file (juce::String (prefix) + suffix + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
                note ("snapshot: " + file.getFullPathName().toStdString());
            else
                check (false, "could not write snapshot " + file.getFullPathName().toStdString());
        };

        editor->setSize (1080, 660);
        snapshot ("-1080x660");
        editor->setSize (940, 600);
        snapshot ("-940x600");
        editor->setSize (2400, 1400);
        snapshot ("-2400x1400");

        defaults (*f.processor);
        set (*f.processor, id::monoBelow, 0.0f);
        set (*f.processor, id::infraHpf, 0.0f);
        set (*f.processor, id::subLimit, 0.0f);
        const auto failN = (size_t) (1.1 * defaultRate);
        Stereo fail (failN);
        for (size_t i = 0; i < failN; ++i)
        {
            const auto v = (float) (0.45 * std::sin (twoPi * 18.0 * (double) i / defaultRate)
                                    + 0.30 * std::sin (twoPi * 50.0 * (double) i / defaultRate));
            fail.l[i] = v;
            fail.r[i] = -v;
        }
        render (*f.processor, fail);
        editor->setSize (1080, 660);
        editor->updateMeters();
        snapshot ("-failing-1080x660");
        check (editor->findControl ("fixAll") != nullptr && editor->findControl ("fixAll")->isEnabled(),
               "Fix All enables when checks fail");
        check (editor->findControl (dsp::checkFixComponentId (dsp::CheckId::stereoLows)) != nullptr,
               "a per-check Fix button appears for stereo lows");
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                      testMetadata },
        { "sub tracking, octave-down, no clicks",          testSubTrackingAndOctave },
        { "tone shift duration, onset, centroid, latency", testToneShift },
        { "system-check verdicts",                         testSystemCheck },
        { "no dead parameters",                            testNoDeadParameters },
        { "state, presets, undo",                          testStatePresetsUndo },
        { "CPU sanity and finite output",                  testCpuAndNan },
        { "one-click Fix, new modules, neutrality",        testFixAndModules },
        { "editor: size, version, one control each, layout", testEditor },
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
