/*
    dooblr-soothe console tests: the real processor, resonance engine and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRSOOTHE_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "UI/ControlPanel.h"
#include "UI/ReductionGraph.h"
#include "UI/Theme.h"
#include "DSP/ResonanceEngine.h"

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

//==============================================================================
// Counts operator new calls made while a processBlock is being measured (real-time safety).
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
    using namespace dooblrsoothe;
    using namespace dooblrsoothe::params;
    using Processor = DooblrSootheProcessor;
    using Editor = DooblrSootheEditor;
    using Engine = dsp::ResonanceEngine;

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
        Stereo (std::vector<float> left, std::vector<float> right) : l (std::move (left)), r (std::move (right)) {}
        size_t size() const noexcept { return l.size(); }
    };

    std::unique_ptr<Processor> makeProcessor (double rate = defaultRate, int block = defaultBlock, bool sidechain = false)
    {
        auto p = std::make_unique<Processor>();
        if (sidechain)
        {
            auto layout = p->getBusesLayout();
            if (layout.inputBuses.size() > 1)
                layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            check (p->setBusesLayout (layout), "the stereo sidechain layout is accepted");
        }
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
        auto* v = p.getState().getRawParameterValue (pid);
        return v != nullptr ? v->load() : 0.0f;
    }

    /** Re-prepares so settings made before this (Quality) define the starting state. */
    void reprepare (Processor& p, double rate = defaultRate, int block = defaultBlock)
    {
        p.releaseResources();
        p.prepareToPlay (rate, block);
    }

    Stereo render (Processor& p, const Stereo& in, const Stereo* sidechain = nullptr, int block = defaultBlock)
    {
        const auto n = (int) in.size();
        Stereo out ((size_t) n);
        const auto numChannels = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (numChannels, block);
        juce::MidiBuffer midi;
        auto* scBus = p.getBus (true, 1);
        for (int pos = 0; pos < n; pos += block)
        {
            const auto len = std::min (block, n - pos);
            buffer.setSize (numChannels, len, false, false, true);
            buffer.clear();
            buffer.copyFrom (0, 0, in.l.data() + pos, len);
            if (numChannels > 1)
                buffer.copyFrom (1, 0, in.r.data() + pos, len);
            if (sidechain != nullptr && scBus != nullptr && scBus->isEnabled())
                for (int ch = 0; ch < scBus->getNumberOfChannels(); ++ch)
                {
                    const auto index = p.getChannelIndexInProcessBlockBuffer (true, 1, ch);
                    if (index >= 0 && index < numChannels)
                        buffer.copyFrom (index, 0, (ch == 0 ? sidechain->l : sidechain->r).data() + pos, len);
                }
            p.processBlock (buffer, midi);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + len, out.l.begin() + pos);
            const auto right = p.getMainBusNumOutputChannels() > 1 ? 1 : 0;
            std::copy (buffer.getReadPointer (right), buffer.getReadPointer (right) + len, out.r.begin() + pos);
        }
        return out;
    }

    std::vector<float> noise (size_t n, float amplitude, int seed)
    {
        juce::Random random (seed);
        std::vector<float> v (n);
        for (auto& x : v)
            x = amplitude * (random.nextFloat() * 2.0f - 1.0f);
        return v;
    }

    /** Pink noise (Paul Kellet's economy filter) scaled to the RMS of white noise of that amplitude. */
    std::vector<float> pink (size_t n, float amplitude, int seed)
    {
        juce::Random random (seed);
        std::vector<float> v (n);
        double b0 = 0, b1 = 0, b2 = 0, sum = 0;
        for (auto& x : v)
        {
            const double white = random.nextFloat() * 2.0 - 1.0;
            b0 = 0.99765 * b0 + white * 0.0990460;
            b1 = 0.96300 * b1 + white * 0.2965164;
            b2 = 0.57000 * b2 + white * 1.0526913;
            x = (float) (b0 + b1 + b2 + white * 0.1848);
            sum += (double) x * x;
        }
        const auto gain = (float) ((amplitude / std::sqrt (3.0)) / std::sqrt (sum / (double) n));
        for (auto& x : v)
            x *= gain;
        return v;
    }

    void addSine (std::vector<float>& v, double rate, double freq, float amplitude, double phase = 0.0)
    {
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += amplitude * (float) std::sin (twoPi * freq * (double) i / rate + phase);
    }

    /** Amplitude of one sinusoid (Hann-windowed single-bin DFT). */
    double sineAmplitude (const std::vector<float>& x, size_t start, size_t len, double rate, double freq)
    {
        std::complex<double> acc;
        double windowSum = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) (len - 1));
            acc += w * (double) x[start + i] * std::polar (1.0, -twoPi * freq * (double) (start + i) / rate);
            windowSum += w;
        }
        return 2.0 * std::abs (acc) / windowSum;
    }

    /** Power (dB, arbitrary reference) of x[start .. start + len) between lo and hi Hz (averaged periodogram). */
    double bandPowerDb (const std::vector<float>& x, size_t start, size_t len, double rate, double lo, double hi)
    {
        constexpr int order = 12, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buffer ((size_t) size * 2);
        std::vector<double> power ((size_t) size / 2 + 1, 0.0);
        int frames = 0;
        for (size_t pos = start; pos + (size_t) size <= start + len; pos += (size_t) size / 2)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                buffer[(size_t) i] = x[pos + (size_t) i] * (float) (0.5 - 0.5 * std::cos (twoPi * i / size));
            fft.performFrequencyOnlyForwardTransform (buffer.data());
            for (size_t k = 0; k < power.size(); ++k)
                power[k] += (double) buffer[k] * buffer[k];
            ++frames;
        }
        double sum = 0.0;
        for (size_t k = 0; k < power.size(); ++k)
        {
            const auto f = (double) k * rate / size;
            if (f >= lo && f < hi)
                sum += power[k];
        }
        return 10.0 * std::log10 (sum / std::max (1, frames) + 1.0e-30);
    }

    double rmsDb (const std::vector<float>& x, size_t start, size_t len)
    {
        double sum = 0.0;
        for (size_t i = start; i < start + len; ++i)
            sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) len + 1.0e-30);
    }

    /** max |a[aStart + i] - b[bStart + i]| */
    double maxDiff (const std::vector<float>& a, size_t aStart, const std::vector<float>& b, size_t bStart, size_t len)
    {
        double worst = 0.0;
        for (size_t i = 0; i < len; ++i)
            worst = std::max (worst, (double) std::abs (a[aStart + i] - b[bStart + i]));
        return worst;
    }

    double maxDiff (const Stereo& a, const Stereo& b)
    {
        return std::max (maxDiff (a.l, 0, b.l, 0, a.size()), maxDiff (a.r, 0, b.r, 0, a.size()));
    }

    bool finiteAndNormal (const std::vector<float>& x)
    {
        for (auto v : x)
            if (! std::isfinite (v) || (v != 0.0f && std::abs (v) < FLT_MIN))
                return false;
        return true;
    }

    float peak (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (auto v : x) p = std::max (p, std::abs (v));
        return p;
    }

    std::string db (double value) { return fmt (value, 2) + " dB"; }

    /** A mix with resonances: noise in both channels plus sines (2 kHz left, 5 kHz right). */
    Stereo resonantMix (size_t n, double rate = defaultRate)
    {
        Stereo s (noise (n, 0.05f, 11), noise (n, 0.05f, 12));
        addSine (s.l, rate, 2000.0, 0.3f);
        addSine (s.r, rate, 5000.0, 0.3f);
        addSine (s.l, rate, 220.0, 0.15f);
        addSine (s.r, rate, 330.0, 0.15f);
        return s;
    }

    /** Reduction (dB) of a sinusoid at freq in channel l, comparing the output with the input. */
    double sineReductionDb (const Stereo& in, const Stereo& out, int latency, double rate, double freq, bool right = false)
    {
        const auto start = (size_t) rate, len = (size_t) (rate * 1.5);
        const auto& x = right ? in.r : in.l;
        const auto& y = right ? out.r : out.l;
        return 20.0 * std::log10 (sineAmplitude (x, start, len, rate, freq) / std::max (1.0e-12, sineAmplitude (y, start + (size_t) latency, len, rate, freq)));
    }

    double bandChangeDb (const std::vector<float>& in, const std::vector<float>& out, int latency, double rate, double lo, double hi)
    {
        const auto start = (size_t) rate, len = (size_t) (rate * 1.5);
        return bandPowerDb (out, start + (size_t) latency, len, rate, lo, hi) - bandPowerDb (in, start, len, rate, lo, hi);
    }

    juce::Component* findById (juce::Component& root, const juce::String& cid)
    {
        for (auto* child : root.getChildren())
        {
            if (child->getComponentID() == cid)
                return child;
            if (auto* found = findById (*child, cid))
                return found;
        }
        return nullptr;
    }

    bool isControl (juce::Component& c)
    {
        return dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr || dynamic_cast<ui::SegmentedToggle*> (&c) != nullptr;
    }

    template <typename Fn>
    void forEachVisible (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isVisible())
                continue;
            fn (*child);
            forEachVisible (*child, fn);
        }
    }

    template <typename Fn>
    void forEachComponent (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren())
        {
            fn (*child);
            forEachComponent (*child, fn);
        }
    }

    void saveSnapshot (juce::Component& c, const juce::String& suffix)
    {
        const auto* prefix = std::getenv ("DOOBLRSOOTHE_EDITOR_SNAPSHOT");
        if (prefix == nullptr || *prefix == 0)
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

    //==============================================================================
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-soothe", "name is dooblr-soothe (got " + p->getName().toStdString() + ")");
        check (JucePlugin_IsSynth == 0 && ! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        check (p->getBusCount (true) == 2 && p->getBus (true, 1) != nullptr && ! p->getBus (true, 1)->isEnabled(), "a sidechain input bus, off by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRSOOTHE_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.1.3", "version 0.1.3");
        check (p->getBypassParameter() == p->getState().getParameter (id::bypass), "Bypass is the host bypass parameter");

        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->checkBusesLayoutSupported (layout), "mono in / out is supported");
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! p->checkBusesLayoutSupported (layout), "mono in / stereo out is refused");
        note ("version " JucePlugin_VersionString);
    }

    void testLatency()
    {
        std::string summary;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            for (int q = 0; q < 3; ++q)
            {
                auto p = makeProcessor (rate);
                set (*p, id::quality, (float) q);
                set (*p, id::depth, 0.0f);
                reprepare (*p, rate);
                const auto expected = Engine::fftSizeFor (rate, q);
                const auto latency = p->getLatencySamples();
                check (latency == expected && latency > 0, "latency at " + fmt (rate, 0) + " Hz, quality " + std::to_string (q) + " is the FFT size (" + std::to_string (latency) + " vs " + std::to_string (expected) + ")");
                check (std::abs (p->getTailLengthSeconds() - latency / rate) < 1.0e-9, "tail = latency");

                // An impulse comes out exactly `latency` samples later.
                Stereo in ((size_t) (latency + 8192));
                in.l[100] = 1.0f;
                in.r[100] = 0.5f;
                const auto out = render (*p, in);
                const auto it = std::max_element (out.l.begin(), out.l.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
                const auto at = (int) std::distance (out.l.begin(), it);
                check (at == 100 + latency, "impulse arrives at the reported latency (" + std::to_string (at - 100) + " vs " + std::to_string (latency) + ")");
                check (std::abs (*it - 1.0f) < 1.0e-3f && std::abs (out.r[(size_t) at] - 0.5f) < 1.0e-3f, "the impulse keeps its level (" + fmt (*it, 5) + ")");
                if (rate == 48000.0 || rate == 96000.0)
                    summary += fmt (rate / 1000.0, 0) + "k/" + juce::String (qualityNames()[q]).toStdString() + "=" + std::to_string (latency) + " ";
            }
        }
        note ("latency (samples): " + summary);

        // Switching Quality while playing re-reports the latency on the next block.
        auto p = makeProcessor();
        set (*p, id::depth, 0.0f);
        render (*p, Stereo (2048));
        const auto before = p->getLatencySamples();
        set (*p, id::quality, 0.0f);
        render (*p, Stereo (512));
        const auto after = p->getLatencySamples();
        check (before == Engine::fftSizeFor (defaultRate, 1) && after == Engine::fftSizeFor (defaultRate, 0) && after != before, "a Quality change while playing updates the reported latency (" + std::to_string (before) + " -> " + std::to_string (after) + ")");
        Stereo in ((size_t) (after + 8192));
        in.l[300] = 1.0f;
        const auto out = render (*p, in);
        const auto it = std::max_element (out.l.begin(), out.l.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
        check ((int) std::distance (out.l.begin(), it) == 300 + after, "after the switch, audio is delayed by the new latency");
    }

    void testDepthZeroNull()
    {
        const auto n = (size_t) (2.0 * defaultRate);
        const auto in = resonantMix (n);
        const auto compare = [&] (Processor& p, const char* what, double limit)
        {
            const auto out = render (p, in);
            const auto latency = (size_t) p.getLatencySamples();
            const auto err = std::max (maxDiff (out.l, latency, in.l, 0, n - latency), maxDiff (out.r, latency, in.r, 0, n - latency));
            check (err <= limit, std::string (what) + ": output = input delayed (max error " + fmt (err, 9) + ")");
            return err;
        };

        auto p = makeProcessor();
        set (*p, id::depth, 0.0f);
        const auto e1 = compare (*p, "depth 0", 1.0e-4);
        for (int q : { 0, 2 })
        {
            auto pq = makeProcessor();
            set (*pq, id::depth, 0.0f);
            set (*pq, id::quality, (float) q);
            reprepare (*pq);
            compare (*pq, q == 0 ? "depth 0, Standard" : "depth 0, Ultra", 1.0e-4);
        }
        auto ms = makeProcessor();
        set (*ms, id::depth, 0.0f);
        set (*ms, id::stereoMode, 1.0f);
        compare (*ms, "depth 0 in M/S", 1.0e-4);
        auto bypass = makeProcessor();
        set (*bypass, id::bypass, 1.0f);
        reprepare (*bypass);
        const auto e2 = compare (*bypass, "Bypass", 1.0e-7);
        auto dry = makeProcessor();
        set (*dry, id::mix, 0.0f);
        reprepare (*dry);
        compare (*dry, "Mix 0", 1.0e-7);
        note ("depth-0 null: " + db (20.0 * std::log10 (e1 + 1.0e-30)) + " peak error; bypass " + (e2 == 0.0 ? std::string ("bit-exact") : fmt (e2, 9)));
    }

    void testResonantPeak()
    {
        const auto n = (size_t) (3.0 * defaultRate);
        Stereo in (noise (n, 0.05f, 21), noise (n, 0.05f, 22));
        addSine (in.l, defaultRate, 2000.0, 0.3f);
        addSine (in.r, defaultRate, 2000.0, 0.3f, 0.7);

        auto p = makeProcessor();
        const auto out = render (*p, in);
        const auto latency = p->getLatencySamples();
        const auto cut = sineReductionDb (in, out, latency, defaultRate, 2000.0);
        const auto aside = bandChangeDb (in.l, out.l, latency, defaultRate, 300.0, 700.0);
        check (cut >= 4.0, "a 2 kHz resonance 20+ dB above the noise is reduced (" + db (cut) + ")");
        check (std::abs (aside) < 1.5, "noise two octaves away is left alone (" + db (aside) + ")");

        // The display frame shows the cut where the resonance is.
        if (auto* frame = p->getEngine().getDisplay().read())
        {
            int best = 0;
            for (int i = 1; i < dsp::DisplayFrame::numPoints; ++i)
                if (frame->reduction[(size_t) i] > frame->reduction[(size_t) best]) best = i;
            const auto f = dsp::DisplayFrame::pointFrequency (best);
            check (frame->reduction[(size_t) best] > 3.0f && std::abs (std::log2 (f / 2000.0f)) < 0.34f,
                   "the reduction graph peaks at the resonance (" + fmt (f, 0) + " Hz, " + db (frame->reduction[(size_t) best]) + ")");
        }
        else
            check (false, "the engine published a display frame");

        auto deep = makeProcessor();
        set (*deep, id::depth, 100.0f);
        const auto deepCut = sineReductionDb (in, render (*deep, in), latency, defaultRate, 2000.0);
        check (deepCut > cut + 2.0, "more Depth cuts more (" + db (cut) + " at 35 %, " + db (deepCut) + " at 100 %)");

        auto limited = makeProcessor();
        set (*limited, id::depth, 100.0f);
        set (*limited, id::maxCut, 4.0f);
        const auto limitedCut = sineReductionDb (in, render (*limited, in), latency, defaultRate, 2000.0);
        check (limitedCut <= 4.6 && limitedCut > 2.0, "Max Cut 4 dB limits the cut (" + db (limitedCut) + ")");

        // Soft is level-independent; Hard treats the loud version harder.
        Stereo quiet = in;
        for (auto* ch : { &quiet.l, &quiet.r })
            for (auto& v : *ch) v *= 0.1f;
        auto softQuiet = makeProcessor();
        const auto softQuietCut = sineReductionDb (quiet, render (*softQuiet, quiet), latency, defaultRate, 2000.0);
        check (std::abs (softQuietCut - cut) < 1.5, "Soft: the same cut 20 dB quieter (" + db (softQuietCut) + " vs " + db (cut) + ")");
        auto hardLoud = makeProcessor();
        set (*hardLoud, id::mode, 1.0f);
        const auto hardLoudCut = sineReductionDb (in, render (*hardLoud, in), latency, defaultRate, 2000.0);
        auto hardQuiet = makeProcessor();
        set (*hardQuiet, id::mode, 1.0f);
        const auto hardQuietCut = sineReductionDb (quiet, render (*hardQuiet, quiet), latency, defaultRate, 2000.0);
        check (hardLoudCut > hardQuietCut + 1.0 && hardLoudCut > cut, "Hard: louder material is cut harder (" + db (hardLoudCut) + " loud, " + db (hardQuietCut) + " quiet)");

        // The depth curve decides where: a band-pass-only curve at 7 kHz spares 2 kHz.
        auto banded = makeProcessor();
        for (int b = 0; b < numBands; ++b)
            set (*banded, bandActive (b), 0.0f);
        set (*banded, bandActive (0), 1.0f);
        set (*banded, bandShape (0), (float) (int) Shape::bandPass);
        set (*banded, bandFreq (0), 7000.0f);
        set (*banded, bandQ (0), 2.0f);
        Stereo both = in;
        addSine (both.l, defaultRate, 7000.0, 0.3f);
        addSine (both.r, defaultRate, 7000.0, 0.3f);
        const auto bandOut = render (*banded, both);
        const auto at2k = sineReductionDb (both, bandOut, latency, defaultRate, 2000.0);
        const auto at7k = sineReductionDb (both, bandOut, latency, defaultRate, 7000.0);
        check (at2k < 1.0 && at7k > 3.0, "a 7 kHz band-pass curve cuts 7 kHz (" + db (at7k) + ") and spares 2 kHz (" + db (at2k) + ")");

        note ("2 kHz resonance: " + db (cut) + " cut at default settings, " + db (deepCut) + " at depth 100; noise at 300-700 Hz " + db (aside));
    }

    void testBroadband()
    {
        const auto n = (size_t) (3.0 * defaultRate);
        for (int kind = 0; kind < 2; ++kind)
        {
            const Stereo in = kind == 0 ? Stereo (noise (n, 0.3f, 31), noise (n, 0.3f, 32)) : Stereo (pink (n, 0.3f, 33), pink (n, 0.3f, 34));
            auto p = makeProcessor();
            const auto out = render (*p, in);
            const auto latency = (size_t) p->getLatencySamples();
            const auto overall = rmsDb (out.l, (size_t) defaultRate + latency, n - (size_t) defaultRate - latency) - rmsDb (in.l, (size_t) defaultRate, n - (size_t) defaultRate - latency);
            double worst = 0.0;
            std::string bands;
            for (double centre = 63.0; centre < 17000.0; centre *= 2.0)
            {
                const auto change = bandChangeDb (in.l, out.l, (int) latency, defaultRate, centre / std::sqrt (2.0), centre * std::sqrt (2.0));
                worst = std::max (worst, std::abs (change));
                bands += fmt (change, 1) + " ";
            }
            const std::string name = kind == 0 ? "white noise" : "pink noise";
            check (std::abs (overall) < 1.0, name + ": level within 1 dB (" + db (overall) + ")");
            check (worst < 2.0, name + ": every octave band within 2 dB (worst " + db (worst) + ")");
            note (name + ": level " + db (overall) + ", octave bands 63 Hz..16 kHz: " + bands);
        }
    }

    void testDelta()
    {
        const auto n = (size_t) (2.0 * defaultRate);
        const auto in = resonantMix (n);
        for (const auto& setup : std::vector<std::pair<std::string, std::vector<std::pair<const char*, float>>>> {
                 { "default", {} },
                 { "mix 50 %, trim -3 dB", { { id::mix, 50.0f }, { id::wetTrim, -3.0f } } },
                 { "M/S, Hard, depth 80", { { id::stereoMode, 1.0f }, { id::mode, 1.0f }, { id::depth, 80.0f } } } })
        {
            auto normal = makeProcessor();
            auto delta = makeProcessor();
            for (auto& [pid, v] : setup.second) { set (*normal, pid, v); set (*delta, pid, v); }
            set (*delta, id::delta, 1.0f);
            reprepare (*normal);
            reprepare (*delta);
            const auto a = render (*normal, in);
            const auto d = render (*delta, in);
            const auto latency = (size_t) normal->getLatencySamples();
            double err = 0.0, energy = 0.0;
            for (size_t i = latency; i < n; ++i)
            {
                err = std::max ({ err, (double) std::abs (d.l[i] - (in.l[i - latency] - a.l[i])), (double) std::abs (d.r[i] - (in.r[i - latency] - a.r[i])) });
                energy += (double) d.l[i] * d.l[i];
            }
            check (err < 1.0e-5, "Delta = input - output, " + setup.first + " (max error " + fmt (err, 8) + ")");
            check (energy > 1.0, "Delta carries what is removed, " + setup.first);
        }
        auto zero = makeProcessor();
        set (*zero, id::depth, 0.0f);
        set (*zero, id::delta, 1.0f);
        reprepare (*zero);
        const auto z = render (*zero, in);
        check (peak (z.l) < 1.0e-4f && peak (z.r) < 1.0e-4f, "Delta at depth 0 is (near) silence (" + fmt (peak (z.l), 7) + ")");
    }

    void testSidechain()
    {
        const auto n = (size_t) (3.0 * defaultRate);
        const Stereo main (noise (n, 0.1f, 41), noise (n, 0.1f, 42));
        Stereo key (noise (n, 0.02f, 43), noise (n, 0.02f, 44));
        addSine (key.l, defaultRate, 2000.0, 0.5f);
        addSine (key.r, defaultRate, 2000.0, 0.5f);

        auto on = makeProcessor (defaultRate, defaultBlock, true);
        set (*on, id::sidechain, 1.0f);
        check (on->isSidechainConnected(), "the sidechain bus reports connected when enabled");
        const auto outOn = render (*on, main, &key);
        const auto latency = on->getLatencySamples();
        const auto cutOn = -bandChangeDb (main.l, outOn.l, latency, defaultRate, 1975.0, 2025.0);

        auto off = makeProcessor (defaultRate, defaultBlock, true);
        const auto outOff = render (*off, main, &key);
        const auto cutOff = -bandChangeDb (main.l, outOff.l, latency, defaultRate, 1975.0, 2025.0);
        check (cutOn > 3.0, "keyed by a 2 kHz sidechain tone, the main signal is cut at 2 kHz (" + db (cutOn) + ")");
        check (std::abs (cutOff) < 1.5, "with Sidechain off, the key is ignored (" + db (cutOff) + ")");

        auto unplugged = makeProcessor();
        set (*unplugged, id::sidechain, 1.0f);
        check (! unplugged->isSidechainConnected(), "no sidechain bus: not connected");
        const auto outUnplugged = render (*unplugged, main);
        check (maxDiff (outUnplugged, render (*makeProcessor(), main)) < 1.0e-6, "Sidechain on without a key falls back to the input");
        note ("sidechain key: " + db (cutOn) + " cut at 2 kHz (off: " + db (cutOff) + ")");
    }

    void testListen()
    {
        const auto n = (size_t) (2.0 * defaultRate);
        const auto energy = [&] (double freq)
        {
            Stereo in (noise (n, 0.05f, 51), noise (n, 0.05f, 52));
            addSine (in.l, defaultRate, freq, 0.3f);
            addSine (in.r, defaultRate, freq, 0.3f);
            auto p = makeProcessor();
            set (*p, id::listenBand, 2.0f); // Band 2: the 3 kHz bell
            reprepare (*p);
            const auto out = render (*p, in);
            return rmsDb (out.l, (size_t) defaultRate, n - (size_t) defaultRate);
        };
        const auto inside = energy (3000.0), outside = energy (150.0);
        check (inside > outside + 12.0, "Band Listen 2 plays what the 3 kHz region removes (" + db (inside) + ") and not a 150 Hz cut (" + db (outside) + ")");
    }

    void testStereo()
    {
        const auto n = (size_t) (3.0 * defaultRate);
        Stereo in (noise (n, 0.05f, 61), noise (n, 0.05f, 62));
        addSine (in.l, defaultRate, 2000.0, 0.3f);
        const auto run = [&] (std::vector<std::pair<const char*, float>> settings, const Stereo& signal)
        {
            auto p = makeProcessor();
            for (auto& [pid, v] : settings) set (*p, pid, v);
            reprepare (*p);
            return std::make_pair (render (*p, signal), p->getLatencySamples());
        };

        const auto [linked, latency] = run ({ { id::link, 100.0f } }, in);
        const auto [unlinked, latency2] = run ({ { id::link, 0.0f } }, in);
        juce::ignoreUnused (latency2);
        const auto rightLinked = -bandChangeDb (in.r, linked.r, latency, defaultRate, 1800.0, 2200.0);
        const auto rightUnlinked = -bandChangeDb (in.r, unlinked.r, latency, defaultRate, 1800.0, 2200.0);
        check (rightLinked > 1.0, "Link 100 %: the clean right channel follows the left's cut (" + db (rightLinked) + ")");
        check (std::abs (rightUnlinked) < 0.75, "Link 0 %: the right channel is independent (" + db (rightUnlinked) + ")");

        const auto [focusLeft, l1] = run ({ { id::link, 0.0f }, { id::focus, -100.0f } }, in);
        const auto [focusRight, l2] = run ({ { id::link, 0.0f }, { id::focus, 100.0f } }, in);
        juce::ignoreUnused (l1, l2);
        const auto cutFocusLeft = sineReductionDb (in, focusLeft, latency, defaultRate, 2000.0);
        const auto cutFocusRight = sineReductionDb (in, focusRight, latency, defaultRate, 2000.0);
        check (cutFocusLeft > 3.0 && cutFocusRight < 0.5, "Focus picks the channel (left focus " + db (cutFocusLeft) + ", right focus " + db (cutFocusRight) + " on a left resonance)");

        // A resonance only in the side signal: M/S with focus on the side cuts it, on the mid spares it.
        Stereo side (noise (n, 0.05f, 63), {});
        side.r = side.l;
        std::vector<float> s (n, 0.0f);
        addSine (s, defaultRate, 2000.0, 0.3f);
        for (size_t i = 0; i < n; ++i) { side.l[i] += s[i]; side.r[i] -= s[i]; }
        const auto [sideFocus, l3] = run ({ { id::stereoMode, 1.0f }, { id::focus, 100.0f } }, side);
        const auto [midFocus, l4] = run ({ { id::stereoMode, 1.0f }, { id::focus, -100.0f } }, side);
        juce::ignoreUnused (l3, l4);
        const auto cutSide = sineReductionDb (side, sideFocus, latency, defaultRate, 2000.0);
        const auto cutMid = sineReductionDb (side, midFocus, latency, defaultRate, 2000.0);
        check (cutSide > 3.0 && cutMid < 0.5, "M/S: a side-only resonance is cut with side focus (" + db (cutSide) + "), spared with mid focus (" + db (cutMid) + ")");

        // Mono in / out.
        auto mono = std::make_unique<Processor>();
        auto layout = mono->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (mono->setBusesLayout (layout), "mono layout accepted");
        mono->prepareToPlay (defaultRate, defaultBlock);
        Stereo monoIn (in.l, in.l);
        const auto monoOut = render (*mono, monoIn);
        const auto monoCut = sineReductionDb (monoIn, monoOut, mono->getLatencySamples(), defaultRate, 2000.0);
        check (finiteAndNormal (monoOut.l) && monoCut > 3.0, "mono: finite and the resonance is cut (" + db (monoCut) + ")");
    }

    void testExtremes()
    {
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            auto p = makeProcessor (rate);
            set (*p, id::depth, 100.0f);
            set (*p, id::maxCut, 36.0f);
            set (*p, id::mode, 1.0f);
            set (*p, id::sharpness, 10.0f);
            set (*p, id::selectivity, 0.0f);
            set (*p, id::attack, 0.5f);
            set (*p, id::release, 5.0f);
            set (*p, id::quality, 2.0f);
            set (*p, id::wetTrim, 12.0f);
            for (int b = 0; b < numBands; ++b)
            {
                set (*p, bandActive (b), 1.0f);
                set (*p, bandGain (b), 18.0f);
                set (*p, bandQ (b), 10.0f);
            }
            reprepare (*p, rate);

            const auto seg = (size_t) (0.5 * rate);
            Stereo in (seg * 6);
            for (size_t i = 0; i < seg; ++i) // full-scale square
                in.l[i] = in.r[i] = (std::fmod ((double) i * 1000.0 / rate, 1.0) < 0.5) ? 1.0f : -1.0f;
            const auto loud = noise (seg, 1.0f, 71);
            const auto tiny = noise (seg, 1.0e-30f, 72);
            for (size_t i = 0; i < seg; ++i)
            {
                in.l[seg + i] = loud[i];
                in.r[seg + i] = -loud[i];
                in.l[2 * seg + i] = in.r[2 * seg + i] = tiny[i];
                in.l[3 * seg + i] = in.r[3 * seg + i] = (i % (size_t) (0.1 * rate) == 0) ? 1.0f : 0.0f;
            }
            // then 1 s of silence
            const auto out = render (*p, in);
            const auto tail = std::vector<float> (out.l.end() - (std::ptrdiff_t) (0.25 * rate), out.l.end());
            bool reductionOk = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < p->getEngine().getNumBins(); ++k)
                {
                    const auto r = p->getEngine().getReduction (ch)[k];
                    reductionOk = reductionOk && std::isfinite (r) && r >= 0.0f && r <= 36.01f;
                }
            check (finiteAndNormal (out.l) && finiteAndNormal (out.r), "finite, no denormals at " + fmt (rate, 0) + " Hz (square, noise, 1e-30, impulses, silence)");
            check (peak (out.l) < 8.0f, "bounded at " + fmt (rate, 0) + " Hz (peak " + fmt (peak (out.l), 3) + ")");
            check (peak (tail) < 1.0e-6f, "silence after silence at " + fmt (rate, 0) + " Hz");
            check (reductionOk, "per-bin reduction finite and within Max Cut at " + fmt (rate, 0) + " Hz");
        }
    }

    void testValueText()
    {
        // Every float parameter's text, across its range and around zero: fixed decimals,
        // no exponent notation ("1.49012e-08"), no negative zero ("-0.0 dB").
        auto p = makeProcessor();
        int checked = 0, bad = 0;
        const auto badText = [] (const juce::String& t)
        {
            const auto number = t.upToFirstOccurrenceOf (" ", false, false);
            return number.containsChar ('e') || number.containsChar ('E') || number.containsIgnoreCase ("inf")
                || number.containsIgnoreCase ("nan") || (number.startsWithChar ('-') && number.removeCharacters ("-0.").isEmpty());
        };
        for (auto* raw : p->getParameters())
        {
            auto* f = dynamic_cast<juce::AudioParameterFloat*> (raw);
            if (f == nullptr) continue;
            std::vector<float> values;
            for (int k = 0; k <= 40; ++k) values.push_back (f->range.convertFrom0to1 ((float) k / 40.0f));
            for (auto tiny : { 1.49012e-8f, -1.0e-7f, -0.004f, 0.004f, -0.04f })
                values.push_back (juce::jlimit (f->range.start, f->range.end, f->range.snapToLegalValue (0.0f) + tiny));
            values.push_back (f->get());
            for (auto v : values)
            {
                const auto text = raw->getText (f->convertTo0to1 (v), 64);
                ++checked;
                if (badText (text)) { ++bad; check (false, f->getParameterID().toStdString() + " shows \"" + text.toStdString() + "\" for " + fmt (v, 9)); }
                // the plain range value too (the knob text box path)
                const auto plain = raw->getText (f->convertTo0to1 (f->range.snapToLegalValue (v)), 64);
                if (badText (plain)) { ++bad; check (false, f->getParameterID().toStdString() + " shows \"" + plain.toStdString() + "\""); }
            }
        }
        check (params::formatValue (1.49012e-8, 0) == "0" && params::formatValue (-0.01, 1) == "0.0"
                   && params::formatValue (-0.06, 1) == "-0.1" && params::formatValue (2.5, 0) == "3" && params::formatValue (1234.5678, 2) == "1234.57",
               "formatValue rounds, never shows exponents or -0");
        check (bad == 0, std::to_string (checked) + " value texts well formed");
        note (std::to_string (checked) + " value texts checked (e.g. Focus at 0: \"" + param (*p, id::focus)->getText (0.5f, 64).toStdString() + "\")");
    }

    void testStateRoundTrip()
    {
        auto p = makeProcessor();
        auto& params = p->getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (params[i] != p->getBypassParameter())
            {
                // Booleans store only on / off, so give them exact values.
                const bool isBool = dynamic_cast<juce::AudioParameterBool*> (params[i]) != nullptr;
                params[i]->setValueNotifyingHost (isBool ? (float) (i % 2) : (float) std::fmod (0.137 + 0.291 * i, 1.0));
            }
        juce::MemoryBlock block;
        p->getStateInformation (block);

        auto q = makeProcessor();
        q->setStateInformation (block.getData(), (int) block.getSize());
        int mismatches = 0;
        for (int i = 0; i < params.size(); ++i)
            if (std::abs (params[i]->getValue() - q->getParameters()[i]->getValue()) > 1.0e-5f)
            {
                ++mismatches;
                check (false, "parameter " + params[i]->getName (64).toStdString() + " survives the state round trip");
            }
        check (mismatches == 0, std::to_string (params.size()) + " parameters round-trip");

        auto r = makeProcessor();
        r->setCurrentProgram (2);
        r->getStateInformation (block);
        auto s = makeProcessor();
        s->setStateInformation (block.getData(), (int) block.getSize());
        check (s->getPresetName() == r->getPresetName() && r->getPresetName() == presets::factory()[2].name, "the preset name round-trips (" + s->getPresetName().toStdString() + ")");

        auto t = makeProcessor();
        t->setStateInformation ("garbage", 7);
        check (std::abs (get (*t, id::depth) - 35.0f) < 1.0e-4f, "a garbage state is ignored");
        note (std::to_string (params.size()) + " parameters");
    }

    void testPresets()
    {
        const auto& factory = presets::factory();
        check (factory.size() >= 8 && factory.front().name == "Default", "Default plus at least 7 factory presets (" + std::to_string (factory.size()) + ")");
        std::set<juce::String> names;
        for (auto& f : factory) names.insert (f.name);
        check (names.size() == factory.size(), "preset names are unique");

        const auto in = resonantMix ((size_t) defaultRate);
        auto p = makeProcessor();
        check (p->getNumPrograms() == (int) factory.size(), "one program per factory preset");
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            p->setCurrentProgram (i);
            bool applied = true;
            for (auto& [pid, v] : factory[(size_t) i].values)
            {
                auto* q = p->getState().getParameter (pid);
                applied = applied && q != nullptr && std::abs (q->convertFrom0to1 (q->getValue()) - v) < 0.02f + 0.001f * std::abs (v);
            }
            check (applied, factory[(size_t) i].name.toStdString() + " applies its values");
            check (p->getProgramName (i) == factory[(size_t) i].name, "program name " + factory[(size_t) i].name.toStdString());
            const auto out = render (*p, in);
            check (finiteAndNormal (out.l) && finiteAndNormal (out.r) && peak (out.l) < 4.0f, factory[(size_t) i].name.toStdString() + " is finite and bounded");
        }

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-soothe-test-preset" + juce::String (presets::fileExtension));
        set (*p, id::depth, 77.0f);
        set (*p, bandFreq (4), 1234.0f);
        check (presets::save (p->getState(), file, "Test"), "a user preset saves");
        set (*p, id::depth, 10.0f);
        set (*p, bandFreq (4), 500.0f);
        const auto error = presets::load (p->getState(), file);
        check (error.isEmpty() && std::abs (get (*p, id::depth) - 77.0f) < 0.05f && std::abs (get (*p, bandFreq (4)) - 1234.0f) < 1.0f, "a user preset loads back (" + error.toStdString() + ")");
        file.deleteFile();
    }

    void testNoDeadParameters()
    {
        // What a parameter needs before it can be heard (anything else uses the defaults).
        std::map<juce::String, std::vector<std::pair<juce::String, float>>> contexts {
            { id::maxCut, { { id::depth, 100.0f }, { id::selectivity, 0.0f } } },
            // Fully linked L/R and M/S apply one gain to both channels (identical); unlinked they differ.
            { id::stereoMode, { { id::link, 0.0f } } },
        };
        for (int b = 0; b < numBands; ++b)
        {
            const std::vector<std::pair<juce::String, float>> audible { { bandActive (b), 1.0f }, { bandShape (b), 0.0f }, { bandGain (b), -12.0f }, { bandFreq (b), 2000.0f } };
            contexts[bandActive (b)] = { { bandShape (b), 0.0f }, { bandGain (b), -12.0f }, { bandFreq (b), 2000.0f } };
            contexts[bandShape (b)] = audible;
            contexts[bandFreq (b)] = { { bandActive (b), 1.0f }, { bandShape (b), 0.0f }, { bandGain (b), -12.0f }, { bandQ (b), 4.0f } };
            contexts[bandGain (b)] = { { bandActive (b), 1.0f }, { bandShape (b), 0.0f }, { bandFreq (b), 2000.0f } };
            contexts[bandQ (b)] = audible;
        }

        const auto n = (size_t) (0.75 * defaultRate);
        auto in = resonantMix (n);
        for (size_t i = 0; i < n; ++i) // gate the 2 kHz tone so attack / release matter
            if (std::fmod ((double) i / defaultRate, 0.15) > 0.075) in.l[i] *= 0.2f;
        Stereo key (noise (n, 0.05f, 81), noise (n, 0.05f, 82));
        addSine (key.l, defaultRate, 900.0, 0.5f);
        addSine (key.r, defaultRate, 900.0, 0.5f);

        int alive = 0;
        auto reference = makeProcessor (defaultRate, defaultBlock, true);
        for (auto* raw : reference->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            const auto render2 = [&] (bool change)
            {
                auto p = makeProcessor (defaultRate, defaultBlock, true);
                if (auto it = contexts.find (pid); it != contexts.end())
                    for (auto& [cid, v] : it->second) set (*p, cid, v);
                auto* q = param (*p, pid);
                if (change)
                {
                    const auto current = q->getValue();
                    float target;
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (q))
                    {
                        const auto count = choice->choices.size();
                        const auto index = pid == id::listenBand ? 2 : (choice->getIndex() + 1) % count;
                        target = q->convertTo0to1 ((float) index);
                    }
                    else if (dynamic_cast<juce::AudioParameterBool*> (q) != nullptr)
                        target = current > 0.5f ? 0.0f : 1.0f;
                    else
                        target = current < 0.5f ? 0.9f : 0.1f;
                    q->setValueNotifyingHost (target);
                }
                reprepare (*p);
                return render (*p, in, &key);
            };
            const auto diff = maxDiff (render2 (false), render2 (true));
            if (diff > 1.0e-4) ++alive;
            else check (false, pid.toStdString() + " changes the output (max difference " + fmt (diff, 7) + ")");
        }
        check (alive == reference->getParameters().size(), std::to_string (alive) + " / " + std::to_string (reference->getParameters().size()) + " parameters change the sound");
        note (std::to_string (alive) + " parameters, each changes the output");
    }

    void testUndoHistory()
    {
        auto p = makeProcessor();
        auto& history = p->getUndoHistory();
        auto* depth = param (*p, id::depth);
        const auto before = depth->getValue();
        depth->beginChangeGesture();
        depth->setValueNotifyingHost (0.6f);
        depth->setValueNotifyingHost (0.65f);
        depth->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (depth->getValue() - before) < 1.0e-5f, "undo restores the value before the gesture");
        check (history.canRedo(), "redo available");
        history.redo();
        check (std::abs (depth->getValue() - 0.65f) < 1.0e-5f, "redo re-applies the whole gesture");

        history.clear();
        depth->setValueNotifyingHost (0.9f);
        check (! history.canUndo(), "automation (no gesture) is not an undo step");

        history.clear();
        auto* sharp = param (*p, id::sharpness);
        for (int i = 0; i < 60; ++i)
        {
            auto* target = (i % 2 == 0) ? sharp : depth;
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
        auto p = makeProcessor (defaultRate, defaultBlock, true);
        set (*p, id::sidechain, 1.0f);
        const auto numChannels = std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (numChannels, defaultBlock);
        juce::MidiBuffer midi;
        const auto source = noise ((size_t) defaultBlock * 8, 0.3f, 91);
        const std::map<int, std::vector<std::pair<juce::String, float>>> changes {
            { 40, { { id::quality, 2.0f } } }, { 80, { { id::stereoMode, 1.0f }, { id::mode, 1.0f } } },
            { 120, { { id::delta, 1.0f } } }, { 160, { { id::delta, 0.0f }, { id::listenBand, 3.0f } } },
            { 200, { { id::listenBand, 0.0f }, { id::quality, 0.0f } } }, { 240, { { id::bypass, 1.0f } } },
            { 260, { { id::bypass, 0.0f }, { id::sidechain, 0.0f }, { id::depth, 100.0f } } }, { 300, { { id::quality, 1.0f } } },
        };
        allocationCount = 0;
        for (int b = 0; b < 360; ++b)
        {
            if (auto it = changes.find (b); it != changes.end())
                for (auto& [pid, v] : it->second) set (*p, pid, v);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.copyFrom (ch, 0, source.data() + (size_t) ((b + ch) % 7) * defaultBlock, defaultBlock);
            if (b >= 10) countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            if (b % 5 == 0) p->getEngine().getDisplay().read(); // the editor's side
        }
        check (allocationCount.load() == 0, "processBlock does not allocate (incl. Quality, M/S, Delta, Listen, Bypass, sidechain changes): " + std::to_string (allocationCount.load()) + " allocations");

        std::string cpu;
        for (int q = 0; q < 3; ++q)
        {
            auto c = makeProcessor();
            set (*c, id::quality, (float) q);
            reprepare (*c);
            juce::AudioBuffer<float> buf (2, defaultBlock);
            const auto blocks = (int) (10.0 * defaultRate / defaultBlock);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                buf.copyFrom (0, 0, source.data() + (size_t) (b % 7) * defaultBlock, defaultBlock);
                buf.copyFrom (1, 0, source.data() + (size_t) ((b + 3) % 7) * defaultBlock, defaultBlock);
                c->processBlock (buf, midi);
            }
            const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
            const auto factor = 10000.0 / std::max (0.001, ms);
            check (factor > 4.0, "quality " + std::to_string (q) + " runs well faster than real time (" + fmt (factor, 1) + "x)");
            cpu += juce::String (qualityNames()[q]).toStdString() + " " + fmt (100.0 / factor, 2) + " % ";
        }
        note ("CPU (one core, 48 kHz stereo, share of real time): " + cpu);
    }


    //==============================================================================
    /** A window for the real-mouse tests: JUCE's own mouse dispatch (MouseInputSource: the component
        under the pointer, enter / exit, down / drag / up, isMouseOver) runs exactly as for a window
        on screen, but the window is virtual, so the test neither flashes a window nor depends on
        what else is on the user's screen (the macOS peer refuses points under other apps' windows). */
    class TestPeer final : public juce::ComponentPeer
    {
    public:
        TestPeer (juce::Component& c, int flags) : juce::ComponentPeer (c, flags), bounds (c.getBounds()) {}

        void* getNativeHandle() const override { return nullptr; }
        void setVisible (bool) override {}
        void setTitle (const juce::String&) override {}
        void setBounds (const juce::Rectangle<int>& b, bool) override { bounds = b; }
        juce::Rectangle<int> getBounds() const override { return bounds; }
        juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + bounds.getPosition().toFloat(); }
        juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - bounds.getPosition().toFloat(); }
        void setMinimised (bool) override {}
        bool isMinimised() const override { return false; }
        bool isShowing() const override { return true; }
        void setFullScreen (bool) override {}
        bool isFullScreen() const override { return false; }
        void setIcon (const juce::Image&) override {}
        bool contains (juce::Point<int> p, bool) const override { return bounds.withZeroOrigin().contains (p); }
        OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
        juce::BorderSize<int> getFrameSize() const override { return {}; }
        bool setAlwaysOnTop (bool) override { return true; }
        void toFront (bool) override {}
        void toBehind (juce::ComponentPeer*) override {}
        bool isFocused() const override { return true; }
        void grabFocus() override {}
        void repaint (const juce::Rectangle<int>&) override {}
        void performAnyPendingRepaintsNow() override {}
        void setAlpha (float) override {}
        juce::StringArray getAvailableRenderingEngines() override { return { "Software" }; }
        void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}

    private:
        juce::Rectangle<int> bounds;
    };

    /** Holds the editor in a TestPeer window at the screen origin. */
    class TestWindow final : public juce::Component
    {
    public:
        explicit TestWindow (juce::Component& content)
        {
            setBounds (content.getLocalBounds().withPosition (100, 80));
            addAndMakeVisible (content);
            content.setTopLeftPosition (0, 0);
            addToDesktop (0);
            setVisible (true);
        }
        ~TestWindow() override { removeFromDesktop(); removeAllChildren(); }
        juce::ComponentPeer* createNewPeer (int flags, void*) override { return new TestPeer (*this, flags); }
    };

    //==============================================================================
    /** Real mouse input through the window's peer: the same path a click on screen takes
        (JUCE's MouseInputSource finds the component under the pointer, then enter / down / up). */
    struct PeerMouse
    {
        juce::ComponentPeer& peer;
        juce::int64 time = 1000000;

        void event (juce::Point<float> pos, juce::ModifierKeys mods)
        {
            time += 400; // far enough apart that no two clicks make a double-click
            peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods, juce::MouseInputSource::defaultPressure,
                                   juce::MouseInputSource::defaultOrientation, time);
        }
        void click (juce::Point<float> pos)
        {
            event (pos, juce::ModifierKeys());
            event (pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            event (pos, juce::ModifierKeys());
        }
        void drag (juce::Point<float> from, juce::Point<float> to)
        {
            event (from, juce::ModifierKeys());
            event (from, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            for (int i = 1; i <= 6; ++i)
                event (from + (to - from) * ((float) i / 6.0f), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            event (to, juce::ModifierKeys());
        }
    };

    template <typename Fn>
    void forEachShowing (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isShowing())
                continue;
            fn (*child);
            forEachShowing (*child, fn);
        }
    }

    //==============================================================================
    struct EditorFixture
    {
        std::unique_ptr<Processor> processor;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor = nullptr;

        EditorFixture()
        {
            processor = makeProcessor();
            render (*processor, resonantMix ((size_t) defaultRate)); // so the graph has a spectrum
            base.reset (processor->createEditor());
            editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "the editor is a DooblrSootheEditor");
        }
        ~EditorFixture() { base.reset(); }
    };

    const std::vector<juce::Point<int>> editorSizes { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };

    void testEditor()
    {
        EditorFixture f;
        auto* editor = f.editor;
        if (editor == nullptr) return;
        auto& p = *f.processor;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080 x 660 (" + std::to_string (editor->getWidth()) + " x " + std::to_string (editor->getHeight()) + ")");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600 && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400, "resize limits 940 x 600 .. 2400 x 1400");
        else
            check (false, "the editor is resizable with limits");
        check (editor->isResizable(), "resizable");

        auto* version = dynamic_cast<juce::Label*> (findById (*editor, "version"));
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString), "the version label shows " JucePlugin_VersionString " (" + (version != nullptr ? version->getText().toStdString() : std::string ("missing")) + ")");

        // Every parameter has exactly one control (hidden band panels included).
        for (auto* raw : p.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            int count = 0;
            forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == pid && isControl (c)) ++count; });
            check (count == 1, pid.toStdString() + " has exactly one control (" + std::to_string (count) + ")");
            check (editor->findControl (pid) != nullptr, "findControl finds " + pid.toStdString());
        }

        // Layout fit at three sizes, for every band page.
        for (auto size : editorSizes)
        {
            editor->setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            int overlaps = 0, outside = 0, tooSmall = 0, controls = 0;
            for (int page = 0; page <= numBands; ++page)
            {
                // Pages 0..7: the band view for each band; page 8: the Advanced view.
                const auto band = std::min (page, numBands - 1);
                editor->setAdvancedShown (page == numBands);
                editor->selectBand (band);
                editor->refresh();
                std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> rects;
                forEachVisible (*editor, [&] (juce::Component& c)
                {
                    if (! isControl (c)) return;
                    rects.push_back ({ &c, editor->getLocalArea (c.getParentComponent(), c.getBounds()) });
                });
                controls = std::max (controls, (int) rects.size());
                for (size_t i = 0; i < rects.size(); ++i)
                {
                    const auto& [c, r] = rects[i];
                    if (! editor->getLocalBounds().contains (r)) { ++outside; check (false, label + ": " + c->getComponentID().toStdString() + " inside the window"); }
                    if (r.getWidth() < 16 || r.getHeight() < 14) { ++tooSmall; check (false, label + ": " + c->getComponentID().toStdString() + " large enough (" + r.toString().toStdString() + ")"); }
                    for (size_t j = i + 1; j < rects.size(); ++j)
                    {
                        const auto& [d, s] = rects[j];
                        if (c->isParentOf (d) || d->isParentOf (c)) continue;
                        if (! r.getIntersection (s).isEmpty())
                        {
                            ++overlaps;
                            check (false, label + ": " + c->getComponentID().toStdString() + " overlaps " + d->getComponentID().toStdString());
                        }
                    }
                }
                // Panels and the graph do not overlap.
                std::vector<juce::Rectangle<int>> blocks { editor->getLocalArea (editor->getGraph().getParentComponent(), editor->getGraph().getBounds()) };
                for (auto* panel : editor->getPanels())
                    if (panel->isVisible())
                        blocks.push_back (editor->getLocalArea (panel->getParentComponent(), panel->getBounds()));
                for (size_t i = 0; i < blocks.size(); ++i)
                    for (size_t j = i + 1; j < blocks.size(); ++j)
                        if (! blocks[i].getIntersection (blocks[j]).isEmpty()) { ++overlaps; check (false, label + ": panels / graph overlap"); }
            }
            editor->setAdvancedShown (false);

            // The reference layout: the Amount knob (depth) is the big one in the left column, left of
            // the dot-slider strip, which is left of the graph; the toolbar sits above the graph.
            {
                auto* amount = editor->findControl (id::depth);
                auto* sharp = editor->findControl (id::sharpness);
                auto* link = editor->findControl (id::link);
                auto* preset = findById (*editor, "preset");
                const auto graphBounds = editor->getGraph().getBounds();
                const auto amountBounds = amount != nullptr ? editor->getLocalArea (amount->getParentComponent(), amount->getBounds()) : juce::Rectangle<int>();
                const auto sharpBounds = sharp != nullptr ? editor->getLocalArea (sharp->getParentComponent(), sharp->getBounds()) : juce::Rectangle<int>();
                const auto linkBounds = link != nullptr ? editor->getLocalArea (link->getParentComponent(), link->getBounds()) : juce::Rectangle<int>();
                const auto presetBounds = preset != nullptr ? preset->getBounds() : juce::Rectangle<int>();
                check (amount != nullptr && sharp != nullptr && link != nullptr && preset != nullptr
                           && amountBounds.getRight() <= linkBounds.getX() && linkBounds.getRight() <= graphBounds.getX()
                           && amountBounds.getRight() < size.x / 4 && presetBounds.getBottom() <= graphBounds.getY()
                           && amountBounds.getWidth() * amountBounds.getHeight() > 2 * sharpBounds.getWidth() * sharpBounds.getHeight(),
                       label + ": left column (Amount " + amountBounds.toString().toStdString() + " larger than Sharpness " + sharpBounds.toString().toStdString()
                           + "), strip, graph " + graphBounds.toString().toStdString() + ", toolbar above");
                const auto& graph = editor->getGraph();
                check (graph.getCurveArea() == graph.getSpectrumArea() && ! graph.getCurveFillPath().isEmpty(),
                       label + ": the weighting curve is overlaid on the spectrum (one plot, light fill)");
            }
            check (overlaps == 0 && outside == 0 && tooSmall == 0, label + ": " + std::to_string (controls) + " visible controls fit, no overlaps");
            check (editor->getGraph().getHeight() >= 150, label + ": graph height " + std::to_string (editor->getGraph().getHeight()) + " >= 150");
            editor->selectBand (1);
            editor->refresh();
            editor->getGraph().poll();
            check (editor->getGraph().hasSpectrum(), label + ": the graph shows the analysed spectrum");
            saveSnapshot (*editor, "-" + juce::String (label));
        }

        // Band Listen and a different band, for the snapshot set.
        editor->setSize (1080, 660);
        set (p, id::listenBand, 2.0f);
        editor->selectBand (1);
        editor->refresh();
        check (editor->getBandTab (1).getButtonText().contains ("*"), "the listening band's tab is marked");
        saveSnapshot (*editor, "-1080x660-listen");
        set (p, id::listenBand, 0.0f);
        editor->selectBand (6);
        editor->refresh();
        saveSnapshot (*editor, "-1080x660-band7");
        editor->selectBand (1);
        editor->setAdvancedShown (true);
        editor->refresh();
        saveSnapshot (*editor, "-1080x660-advanced");
        editor->setAdvancedShown (false);
    }

    void doubleClick (PeerMouse& mouse, juce::Point<float> pos)
    {
        mouse.event (pos, juce::ModifierKeys());
        const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        for (int i = 0; i < 2; ++i)
        {
            mouse.time += 40;
            mouse.peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, down, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, mouse.time);
            mouse.time += 40;
            mouse.peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, juce::ModifierKeys(), juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, mouse.time);
        }
    }

    void testGraphMouse()
    {
        EditorFixture f;
        auto* editor = f.editor;
        if (editor == nullptr) return;
        auto& p = *f.processor;
        TestWindow window (*editor);
        auto* peer = window.getPeer();
        check (peer != nullptr, "the test window has a peer");
        if (peer == nullptr) return;
        PeerMouse mouse { *peer };
        auto& graph = editor->getGraph();
        graph.poll();
        const auto toWindow = [&] (juce::Point<float> local) { return editor->getLocalPoint (&graph, local); };
        auto& history = p.getUndoHistory();
        history.clear();

        // Drag band 2's node: frequency and gain follow, as one undo step.
        const auto f0 = get (p, bandFreq (1)), g0 = get (p, bandGain (1));
        const auto node = graph.getNodePosition (1);
        auto* hit = editor->getComponentAt (toWindow (node).roundToInt());
        check (hit == &graph || graph.isParentOf (hit), "the band 2 node is under the mouse on the graph");
        const auto target = node + juce::Point<float> (80.0f, -25.0f);
        mouse.drag (toWindow (node), toWindow (target));
        const auto f1 = get (p, bandFreq (1)), g1 = get (p, bandGain (1));
        const auto expectedF = graph.frequencyForX (target.x), expectedG = graph.curveDbForY (target.y);
        check (std::abs (std::log2 (f1 / expectedF)) < 0.05f && std::abs (g1 - expectedG) < 0.4f && f1 > f0 * 1.2f && g1 > g0 + 1.0f,
               "dragging a node sets frequency and gain (" + fmt (f0, 0) + " Hz " + db (g0) + " -> " + fmt (f1, 0) + " Hz " + db (g1) + ", expected " + fmt (expectedF, 0) + " Hz " + db (expectedG) + ")");
        check (editor->getSelectedBand() == 1, "pressing a node selects its band");
        history.undo();
        check (std::abs (get (p, bandFreq (1)) - f0) < 0.5f && std::abs (get (p, bandGain (1)) - g0) < 0.01f, "one undo reverts the whole node drag");
        history.redo();
        check (std::abs (get (p, bandFreq (1)) - f1) < 0.5f, "redo re-applies it");

        // Mouse wheel over a node: Q.
        graph.poll();
        const auto q0 = get (p, bandQ (1));
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.25f;
        mouse.time += 400;
        peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, toWindow (graph.getNodePosition (1)), mouse.time, wheel);
        const auto q1 = get (p, bandQ (1));
        check (std::abs (q1 - q0) > 0.1f * q0, "the wheel over a node changes its Q (" + fmt (q0, 2) + " -> " + fmt (q1, 2) + ")");

        // Click another node: selects it in the editor.
        graph.poll();
        mouse.click (toWindow (graph.getNodePosition (2)));
        check (editor->getSelectedBand() == 2, "clicking band 3's node selects it");

        // Double-click empty curve space: a new bell band there.
        graph.poll();
        const juce::Point<float> empty (graph.xForFrequency (600.0f), graph.yForCurveDb (6.0f));
        check (graph.bandAt (empty) < 0, "600 Hz / +6 dB is empty");
        int activeBefore = 0;
        for (int b = 0; b < numBands; ++b) activeBefore += get (p, bandActive (b)) > 0.5f ? 1 : 0;
        doubleClick (mouse, toWindow (empty));
        int activeAfter = 0, added = -1;
        for (int b = 0; b < numBands; ++b)
            if (get (p, bandActive (b)) > 0.5f) { ++activeAfter; if (b == 3) added = b; }
        check (activeAfter == activeBefore + 1 && added == 3, "double-click adds a band (" + std::to_string (activeBefore) + " -> " + std::to_string (activeAfter) + ")");
        if (added >= 0)
        {
            check (std::abs (std::log2 (get (p, bandFreq (added)) / 600.0f)) < 0.05f && std::abs (get (p, bandGain (added)) - 6.0f) < 0.4f
                       && (int) get (p, bandShape (added)) == (int) Shape::bell,
                   "the new band is a bell at the click (" + fmt (get (p, bandFreq (added)), 0) + " Hz, " + db (get (p, bandGain (added))) + ")");
            check (editor->getSelectedBand() == added, "the new band is selected");
            graph.poll();
            doubleClick (mouse, toWindow (graph.getNodePosition (added)));
            check (get (p, bandActive (added)) < 0.5f, "double-clicking a node removes the band");
        }
    }

    void testRealMouse()
    {
        for (auto size : editorSizes)
        {
            EditorFixture f;
            auto* editor = f.editor;
            if (editor == nullptr) return;
            auto& p = *f.processor;
            editor->setSize (size.x, size.y);
            TestWindow window (*editor);
            auto* peer = window.getPeer();
            if (peer == nullptr) { check (false, "the test window has a peer"); return; }
            PeerMouse mouse { *peer };
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            const auto fail = [&] (const juce::String& what) { check (false, label + ": " + what.toStdString()); };

            int controls = 0, hits = 0, toggles = 0, togglesOk = 0, segments = 0, segmentsOk = 0, knobs = 0, knobsOk = 0, tabs = 0, tabsOk = 0, listens = 0, listensOk = 0;
            std::set<juce::String> seen;
            for (int page = 0; page <= numBands; ++page)
            {
                // Pages 0..7: the band view for each band; page 8: the Advanced view, opened by
                // clicking the Advanced button.
                const auto band = std::min (page, numBands - 1);
                if (page == numBands)
                {
                    if (auto* adv = findById (*editor, "advanced"))
                        mouse.click (editor->getLocalPoint (adv, adv->getLocalBounds().getCentre()).toFloat());
                    if (! editor->isAdvancedShown()) fail ("the Advanced button opens the advanced panel");
                }
                editor->selectBand (band);
                editor->refresh();
                std::vector<juce::Component*> list;
                forEachShowing (*editor, [&] (juce::Component& c) { if (isControl (c)) list.push_back (&c); });
                for (auto* c : list)
                {
                    const auto name = c->getComponentID();
                    // Each control once (the Listen button once per band: it follows the selection).
                    if (seen.count (name) > 0 && c != &editor->getListenButton()) continue;
                    seen.insert (name);
                    ++controls;
                    const auto centre = editor->getLocalPoint (c, c->getLocalBounds().getCentre()).toFloat();
                    auto* hit = editor->getComponentAt (centre.roundToInt());
                    if (hit == c || c->isParentOf (hit)) ++hits;
                    else { fail (name + " is covered by " + (hit != nullptr ? hit->getComponentID() : juce::String ("nothing"))); continue; }

                    auto* parameter = p.getState().getParameter (name);
                    if (auto* seg = dynamic_cast<ui::SegmentedToggle*> (c))
                    {
                        ++segments;
                        const auto original = seg->getSelectedIndex();
                        bool ok = true;
                        for (int i = 0; i < seg->getNumSegments(); ++i)
                        {
                            mouse.click (editor->getLocalPoint (c, seg->getSegmentBounds (i).getCentre()).toFloat());
                            ok = ok && seg->getSelectedIndex() == i && parameter != nullptr && juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())) == i;
                        }
                        mouse.click (editor->getLocalPoint (c, seg->getSegmentBounds (original).getCentre()).toFloat());
                        if (ok && seg->getSelectedIndex() == original) ++segmentsOk; else fail (name + " segments do not select");
                    }
                    else if (c == &editor->getListenButton())
                    {
                        ++listens;
                        mouse.click (centre);
                        const auto on = (int) get (p, id::listenBand);
                        mouse.click (centre);
                        const auto off = (int) get (p, id::listenBand);
                        if (on == band + 1 && off == 0) ++listensOk; else fail ("Listen sets " + juce::String (on) + " / " + juce::String (off) + " on band " + juce::String (band + 1));
                    }
                    else if (name.startsWith ("band_"))
                    {
                        ++tabs;
                        const auto index = name.fromFirstOccurrenceOf ("band_", false, false).getIntValue() - 1;
                        mouse.click (centre);
                        if (editor->getSelectedBand() == index && editor->getBandTab (index).getToggleState()) ++tabsOk;
                        else fail (name + " does not select its band");
                        editor->selectBand (band);
                        editor->refresh();
                    }
                    else if (auto* button = dynamic_cast<juce::ToggleButton*> (c); button != nullptr && parameter != nullptr)
                    {
                        ++toggles;
                        const auto before = parameter->getValue() > 0.5f;
                        mouse.click (centre);
                        const auto after = parameter->getValue() > 0.5f;
                        if (after != before && button->getToggleState() == after) ++togglesOk;
                        else fail (name + juce::String (ui::isDimmed (*c) ? " (dimmed)" : "") + " does not toggle");
                        mouse.click (centre);
                        if ((parameter->getValue() > 0.5f) != before) fail (name + " does not toggle back");
                        editor->refresh();
                    }
                    else if (auto* slider = dynamic_cast<juce::Slider*> (c); slider != nullptr && parameter != nullptr)
                    {
                        ++knobs;
                        const auto before = parameter->getValue();
                        const auto track = slider->getLookAndFeel().getSliderLayout (*slider).sliderBounds.getCentre();
                        const auto from = slider->isRotary() ? editor->getLocalPoint (c, juce::Point<int> (c->getWidth() / 2, (c->getHeight() - 16) / 2)).toFloat()
                                                             : editor->getLocalPoint (c, track).toFloat();
                        const auto step = slider->isRotary() || slider->getSliderStyle() == juce::Slider::LinearVertical
                                              ? juce::Point<float> (0.0f, -40.0f) : juce::Point<float> (40.0f, 0.0f);
                        mouse.drag (from, from + step);
                        if (std::abs (parameter->getValue() - before) < 1.0e-4f)
                            mouse.drag (from, from - step);
                        if (std::abs (parameter->getValue() - before) > 1.0e-4f) ++knobsOk;
                        else fail (name + juce::String (ui::isDimmed (*c) ? " (dimmed)" : "") + " does not move when dragged");
                        parameter->setValueNotifyingHost (before);
                    }
                }
            }

            if (auto* adv = findById (*editor, "advanced"))
                mouse.click (editor->getLocalPoint (adv, adv->getLocalBounds().getCentre()).toFloat());
            if (editor->isAdvancedShown()) fail ("the Advanced button returns to the band view");

            // Preset prev / next by mouse.
            {
                auto* prev = findById (*editor, "presetPrev");
                auto* next = findById (*editor, "presetNext");
                const auto& factory = presets::factory();
                p.setCurrentProgram (0);
                editor->refresh();
                if (prev != nullptr && next != nullptr)
                {
                    mouse.click (editor->getLocalPoint (next, next->getLocalBounds().getCentre()).toFloat());
                    const auto afterNext = p.getPresetName();
                    mouse.click (editor->getLocalPoint (prev, prev->getLocalBounds().getCentre()).toFloat());
                    const auto afterPrev = p.getPresetName();
                    check (afterNext == factory[1].name && afterPrev == factory[0].name,
                           label + ": preset > / < step through the list (" + afterNext.toStdString() + ", " + afterPrev.toStdString() + ")");
                }
                else
                    fail ("preset prev / next buttons found");
            }

            // A / B compare and copy by mouse.
            {
                auto* a = findById (*editor, "compareA");
                auto* b = findById (*editor, "compareB");
                auto* copy = findById (*editor, "compareCopy");
                if (a != nullptr && b != nullptr && copy != nullptr)
                {
                    const auto click = [&] (juce::Component* c) { mouse.click (editor->getLocalPoint (c, c->getLocalBounds().getCentre()).toFloat()); };
                    set (p, id::depth, 20.0f);
                    click (b);
                    const auto bStart = get (p, id::depth);
                    set (p, id::depth, 80.0f);
                    click (a);
                    const auto aBack = get (p, id::depth);
                    click (b);
                    const auto bBack = get (p, id::depth);
                    click (copy); // B -> A
                    click (a);
                    const auto aCopied = get (p, id::depth);
                    check (p.getCompareSlot() == 0 && std::abs (bStart - 20.0f) < 0.05f && std::abs (aBack - 20.0f) < 0.05f
                               && std::abs (bBack - 80.0f) < 0.05f && std::abs (aCopied - 80.0f) < 0.05f,
                           label + ": A / B switch and copy (B starts " + fmt (bStart, 1) + ", A " + fmt (aBack, 1) + ", B " + fmt (bBack, 1) + ", copied " + fmt (aCopied, 1) + ")");
                    set (p, id::depth, 35.0f);
                }
                else
                    fail ("A / B buttons found");
            }

            // Undo / Redo buttons by mouse after a knob drag.
            editor->selectBand (1);
            editor->refresh();
            auto* depthKnob = editor->findControl (id::depth);
            auto* undo = findById (*editor, "undo");
            auto* redo = findById (*editor, "redo");
            if (depthKnob != nullptr && undo != nullptr && redo != nullptr)
            {
                p.getUndoHistory().clear();
                const auto before = get (p, id::depth);
                const auto from = editor->getLocalPoint (depthKnob, juce::Point<int> (depthKnob->getWidth() / 2, (depthKnob->getHeight() - 16) / 2)).toFloat();
                mouse.drag (from, from + juce::Point<float> (0.0f, -40.0f));
                const auto dragged = get (p, id::depth);
                editor->refresh();
                mouse.click (editor->getLocalPoint (undo, undo->getLocalBounds().getCentre()).toFloat());
                const auto undone = get (p, id::depth);
                editor->refresh();
                mouse.click (editor->getLocalPoint (redo, redo->getLocalBounds().getCentre()).toFloat());
                const auto redone = get (p, id::depth);
                check (dragged != before && std::abs (undone - before) < 0.05f && std::abs (redone - dragged) < 0.05f, label + ": the Undo / Redo buttons revert and re-apply a knob drag");
            }
            else
                fail ("depth knob, undo and redo found");

            check (hits == controls, label + ": " + std::to_string (hits) + " / " + std::to_string (controls) + " controls are under the mouse at their centre");
            check (togglesOk == toggles && toggles >= 3 + numBands, label + ": " + std::to_string (togglesOk) + " / " + std::to_string (toggles) + " toggles flip");
            check (segmentsOk == segments && segments >= 1, label + ": segmented Soft / Hard selects");
            check (knobsOk == knobs && knobs >= 12 + 3 * numBands, label + ": " + std::to_string (knobsOk) + " / " + std::to_string (knobs) + " knobs / sliders move");
            check (tabsOk == tabs && tabs >= numBands, label + ": " + std::to_string (tabsOk) + " / " + std::to_string (tabs) + " band tabs select");
            check (listensOk == listens && listens == numBands, label + ": Listen works on all " + std::to_string (listens) + " bands");
            note (label + ": " + std::to_string (controls) + " controls hit-tested; " + std::to_string (toggles) + " toggles, " + std::to_string (knobs) + " knobs, " + std::to_string (tabs) + " tabs, " + std::to_string (listens) + " listen clicks");
        }
    }

    void testWeightingInversion()
    {
        // 0.1.1: band Gain reads like an EQ cut. Pulling a 2 kHz bell down weights the 2 kHz
        // resonance heavier (more reduction); pushing it up weights it lighter.
        const auto n = (size_t) (3.0 * defaultRate);
        Stereo in (noise (n, 0.05f, 41), noise (n, 0.05f, 42));
        addSine (in.l, defaultRate, 2000.0, 0.3f);
        addSine (in.r, defaultRate, 2000.0, 0.3f, 0.7);

        const auto cutWith = [&] (float gain)
        {
            auto p = makeProcessor();
            for (int b = 0; b < numBands; ++b)
                set (*p, bandActive (b), 0.0f);
            if (gain != 0.0f)
            {
                set (*p, bandActive (1), 1.0f);
                set (*p, bandShape (1), (float) (int) Shape::bell);
                set (*p, bandFreq (1), 2000.0f);
                set (*p, bandQ (1), 1.0f);
                set (*p, bandGain (1), gain);
                const auto bands = p->readBands();
                check (gain < 0 ? dsp::curveDb (bands, 2000.0f) < -6.0f && dsp::curveAmount (bands, 2000.0f) > 2.0f
                                : dsp::curveDb (bands, 2000.0f) > 6.0f && dsp::curveAmount (bands, 2000.0f) < 0.5f,
                       "the curve shows " + db (gain) + " at 2 kHz as " + db (dsp::curveDb (bands, 2000.0f)) + " (amount " + fmt (dsp::curveAmount (bands, 2000.0f), 2) + ")");
            }
            return sineReductionDb (in, render (*p, in), p->getLatencySamples(), defaultRate, 2000.0);
        };
        const auto flat = cutWith (0.0f), down = cutWith (-12.0f), up = cutWith (12.0f);
        check (down > flat + 1.0, "a band pulled down at 2 kHz reduces the 2 kHz resonance more (" + db (down) + " vs flat " + db (flat) + ")");
        check (up < flat - 1.0, "a band pushed up at 2 kHz reduces it less (" + db (up) + " vs flat " + db (flat) + ")");
        note ("2 kHz cut: flat " + db (flat) + ", bell -12 dB " + db (down) + ", bell +12 dB " + db (up));
    }

    void testStateMigration()
    {
        // A 0.1.0 session (stateVersion 1, Gain > 0 meant more reduction) loads with Gain negated.
        auto p = makeProcessor();
        set (*p, bandGain (1), 6.0f);
        set (*p, bandGain (2), -4.0f);
        set (*p, id::depth, 50.0f);
        auto tree = p->getState().copyState();
        tree.setProperty ("stateVersion", 1, nullptr);
        juce::MemoryBlock block;
        if (auto xml = tree.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
        auto q = makeProcessor();
        q->setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (get (*q, bandGain (1)) + 6.0f) < 0.01f && std::abs (get (*q, bandGain (2)) - 4.0f) < 0.01f && std::abs (get (*q, id::depth) - 50.0f) < 0.01f,
               "a version-1 session negates band Gain only (" + db (get (*q, bandGain (1))) + ", " + db (get (*q, bandGain (2))) + ")");

        // A current session keeps the sign.
        p->getStateInformation (block);
        auto r = makeProcessor();
        r->setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (get (*r, bandGain (1)) - 6.0f) < 0.01f, "a version-2 session keeps band Gain (" + db (get (*r, bandGain (1))) + ")");

        // A version-1 preset file, too.
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-soothe-v1-preset" + juce::String (presets::fileExtension));
        check (presets::save (p->getState(), file, "Old"), "a preset saves");
        if (auto xml = juce::XmlDocument::parse (file))
        {
            xml->setAttribute ("stateVersion", 1);
            xml->writeTo (file);
        }
        auto s = makeProcessor();
        const auto error = presets::load (s->getState(), file);
        check (error.isEmpty() && std::abs (get (*s, bandGain (1)) + 6.0f) < 0.01f, "a version-1 preset file negates band Gain (" + db (get (*s, bandGain (1))) + ")");
        file.deleteFile();

        // The factory presets read like EQ cuts: Vocal Harshness pulls its 3.5 kHz bell down.
        for (auto& f : presets::factory())
            if (f.name == "Vocal Harshness")
                for (auto& [pid, v] : f.values)
                    if (pid == bandGain (1))
                        check (v < 0.0f, "Vocal Harshness weights 3.5 kHz heavier with a negative gain (" + db (v) + ")");
    }

    /** y of an open, left-to-right path at the centre of every pixel column in [x0, x1]. */
    std::vector<float> sampleColumns (const juce::Path& path, int x0, int x1)
    {
        std::vector<juce::Line<float>> segments;
        for (juce::PathFlatteningIterator it (path, {}, 0.02f); it.next();)
            if (! it.closesSubPath && it.x2 != it.x1)
                segments.emplace_back (it.x1, it.y1, it.x2, it.y2);
        std::vector<float> ys;
        for (int x = x0; x <= x1; ++x)
        {
            const auto fx = (float) x + 0.5f;
            auto y = std::numeric_limits<float>::quiet_NaN();
            for (const auto& s : segments)
                if (fx >= std::min (s.getStartX(), s.getEndX()) && fx <= std::max (s.getStartX(), s.getEndX()))
                {
                    y = s.getStartY() + (s.getEndY() - s.getStartY()) * (fx - s.getStartX()) / (s.getEndX() - s.getStartX());
                    break;
                }
            ys.push_back (y);
        }
        return ys;
    }

    struct StepStats { int steps = 0, missing = 0; float maxBend = 0.0f, maxSlope = 0.0f; };

    /** Staircase detector: a step is a run of >= 3 flat columns (|dy| < 0.05 px) next to a jump
        (|dy| > 0.6 px). maxBend is the largest column-to-column change of the slope. */
    StepStats stepStats (const std::vector<float>& y)
    {
        StepStats st;
        std::vector<float> dy;
        for (size_t i = 0; i + 1 < y.size(); ++i)
        {
            if (! std::isfinite (y[i]) || ! std::isfinite (y[i + 1])) { ++st.missing; continue; }
            dy.push_back (y[i + 1] - y[i]);
        }
        int flat = 0;
        for (size_t i = 0; i < dy.size(); ++i)
        {
            st.maxSlope = std::max (st.maxSlope, std::abs (dy[i]));
            if (i > 0) st.maxBend = std::max (st.maxBend, std::abs (dy[i] - dy[i - 1]));
            if (std::abs (dy[i]) < 0.05f) { ++flat; continue; }
            if (flat >= 3 && std::abs (dy[i]) > 0.6f) ++st.steps;
            flat = 0;
        }
        return st;
    }

    void testAnalyzerSpeed()
    {
        using ui::AnalyzerSpeed;
        // A -80 -> 0 dB step (rise) and back (fall): the time to cover 90 % of it matches each
        // setting at 60 and 144 frames per second (frame-rate independent).
        const auto t90 = [] (float from, float to, double dt, ui::AnalyzerTimes times)
        {
            float target = to, shown = from;
            double t = 0.0;
            while (std::abs (shown - from) < 0.9f * std::abs (to - from) && t < 10.0)
            {
                ui::applyBallistics (&target, &shown, 1, dt, times);
                t += dt;
            }
            return t * 1000.0;
        };
        std::string summary;
        for (int s = 0; s < ui::numAnalyzerSpeeds; ++s)
        {
            const auto speed = ui::analyzerSpeedFromIndex (s);
            const auto times = ui::analyzerTimes (speed);
            const auto name = std::string (ui::analyzerSpeedName (speed));
            check (times.fallMs > times.riseMs, name + ": peaks rise faster than they fall");
            for (const auto fps : { 60.0, 144.0 })
            {
                const auto dt = 1.0 / fps;
                const auto rise = t90 (-80.0f, 0.0f, dt, times), fall = t90 (0.0f, -80.0f, dt, times);
                // Discrete steps land within one frame after the exact 90 % time.
                const auto ok = [&] (double got, float want) { return got >= want - 0.01 && got <= want + dt * 1000.0 + 0.01; };
                check (ok (rise, times.riseMs) && ok (fall, times.fallMs),
                       name + " at " + fmt (fps, 0) + " fps: 90 % rise " + fmt (rise, 1) + " ms (expected " + fmt (times.riseMs, 0)
                           + "), fall " + fmt (fall, 1) + " ms (expected " + fmt (times.fallMs, 0) + ")");
            }
            summary += name + " " + fmt (times.riseMs, 0) + "/" + fmt (times.fallMs, 0) + " ms  ";
        }
        std::cout << "    analyzer 90 % rise / fall: " << summary << "\n";

        // The default (Medium) is slower than 0.1.2, which drew each analysis frame as it came
        // (a step was fully shown on the next frame: <= 1 / 60 s).
        check (ui::defaultAnalyzerSpeed == AnalyzerSpeed::medium, "the default Analyzer Speed is Medium");
        const auto def = ui::analyzerTimes (ui::defaultAnalyzerSpeed);
        check (t90 (-80.0f, 0.0f, 1.0 / 60.0, def) > 4.0 * 1000.0 / 60.0 && t90 (0.0f, -80.0f, 1.0 / 60.0, def) > 30.0 * 1000.0 / 60.0,
               "the default display is much slower than 0.1.2's (one frame): rise " + fmt (def.riseMs, 0) + " ms, fall " + fmt (def.fallMs, 0) + " ms");
        {
            // Same elapsed time in a few big frames or many small ones: the same result.
            float target = 0.0f, a = -60.0f, b = -60.0f;
            for (int i = 0; i < 6; ++i)  ui::applyBallistics (&target, &a, 1, 0.05, def);
            for (int i = 0; i < 43; ++i) ui::applyBallistics (&target, &b, 1, 0.3 / 43.0, def);
            check (std::abs (a - b) < 1.0e-3f, "ballistics depend on elapsed time, not frame count (" + fmt (a, 4) + " vs " + fmt (b, 4) + " dB)");
        }

        // State: saved with the session (by name), restored; older sessions get the default; not
        // a parameter, not in the parameter tree, and untouched by A / B.
        {
            auto p = makeProcessor();
            check (p->getAnalyzerSpeed() == (int) AnalyzerSpeed::medium, "a new instance uses Medium");
            check (p->getState().getParameter ("analyzerSpeed") == nullptr, "Analyzer Speed is not a (automatable) parameter");
            juce::MemoryBlock oldState;
            p->getStateInformation (oldState); // Medium
            p->setAnalyzerSpeed ((int) AnalyzerSpeed::slow);
            juce::MemoryBlock saved;
            p->getStateInformation (saved);
            auto q = makeProcessor();
            q->setStateInformation (saved.getData(), (int) saved.getSize());
            check (q->getAnalyzerSpeed() == (int) AnalyzerSpeed::slow, "Analyzer Speed is restored from the session");
            check (! q->getState().state.hasProperty ("analyzerSpeed"), "Analyzer Speed is kept out of the parameter tree (presets, A / B)");
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            if (xml != nullptr) xml->removeAttribute ("analyzerSpeed");
            juce::MemoryBlock legacy;
            if (xml != nullptr) juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
            q->setStateInformation (legacy.getData(), (int) legacy.getSize());
            check (q->getAnalyzerSpeed() == (int) AnalyzerSpeed::medium, "a session without it (0.1.2) gets the default");
            q->setAnalyzerSpeed ((int) AnalyzerSpeed::fast);
            q->selectCompareSlot (1);
            q->selectCompareSlot (0);
            check (q->getAnalyzerSpeed() == (int) AnalyzerSpeed::fast, "A / B leaves Analyzer Speed alone");
            q->setAnalyzerSpeed (99);
            check (q->getAnalyzerSpeed() == (int) AnalyzerSpeed::veryFast, "out-of-range values are clamped");
        }

        // The graph: after a drop in level the shown spectrum falls at the setting's rate (the
        // fraction of the drop still to go after 0.1 s is 10^(-0.1 s / fall time)).
        EditorFixture f;
        if (f.editor == nullptr) return;
        auto& p = *f.processor;
        auto* editor = f.editor;
        editor->setSize (1080, 660);
        auto& graph = editor->getGraph();
        const auto n = (size_t) (defaultRate / 4);
        const Stereo silence (std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f));
        const auto loud = resonantMix (n);
        std::string graphSummary;
        for (int s = 0; s < ui::numAnalyzerSpeeds; ++s)
        {
            const auto speed = ui::analyzerSpeedFromIndex (s);
            render (p, loud);
            graph.poll();
            graph.snapDisplay();      // the loud spectrum is shown
            p.setAnalyzerSpeed (s);
            render (p, silence);
            graph.poll();             // the quiet frame arrives (poll may advance a little real time)
            const auto start = graph.getFrame().input;
            const auto changed = graph.advanceDisplay (0.1);
            const auto& now = graph.getFrame().input;
            const auto& target = graph.getTargetFrame().input;
            double sum = 0.0;
            int count = 0;
            for (size_t i = 0; i < start.size(); ++i)
                if (start[i] - target[i] > 6.0f)
                {
                    sum += (now[i] - target[i]) / (start[i] - target[i]);
                    ++count;
                }
            const auto got = count > 0 ? sum / count : -1.0;
            const auto expected = std::pow (10.0, -0.1 / (ui::analyzerTimes (speed).fallMs * 0.001));
            check (changed && count > 20 && std::abs (got - expected) < 0.02,
                   std::string ("graph at ") + ui::analyzerSpeedName (speed) + ": " + fmt (100.0 * got, 1) + " % of the drop left after 0.1 s (expected "
                       + fmt (100.0 * expected, 1) + " %, " + std::to_string (count) + " points)");
            graphSummary += std::string (ui::analyzerSpeedName (speed)) + " " + fmt (100.0 * got, 1) + " % ";
        }
        std::cout << "    graph, drop left after 0.1 s: " << graphSummary << "\n";
        graph.snapDisplay();
        check (graph.getFrame().input == graph.getTargetFrame().input, "snapDisplay shows the latest frame");

        // The Advanced panel's selector: visible there, under the mouse (not covered), lists the
        // four speeds, and choosing one sets the processor; a session change updates it.
        p.setAnalyzerSpeed ((int) AnalyzerSpeed::medium);
        TestWindow window (*editor);
        editor->setAdvancedShown (false);
        auto& box = editor->getAnalyzerSpeedBox();
        check (! box.isShowing(), "the Analyzer Speed selector is hidden in the band view");
        editor->setAdvancedShown (true);
        editor->refresh();
        const auto centre = editor->getLocalPoint (&box, box.getLocalBounds().getCentre());
        auto* hit = editor->getComponentAt (centre);
        check (box.isShowing() && box.getWidth() >= 80 && box.getHeight() >= 20 && (hit == &box || box.isParentOf (hit)),
               "the Analyzer Speed selector is in the Advanced panel and under the mouse (" + std::to_string (box.getWidth()) + "x" + std::to_string (box.getHeight()) + ")");
        check (box.getNumItems() == ui::numAnalyzerSpeeds && box.getItemText (0) == "Slow" && box.getItemText (3) == "Very Fast"
                   && box.getText() == "Medium" && box.getTooltip().isNotEmpty(),
               "the selector lists Slow / Medium / Fast / Very Fast, shows Medium, has a tooltip");
        box.setSelectedId ((int) AnalyzerSpeed::slow + 1, juce::sendNotificationSync);
        check (p.getAnalyzerSpeed() == (int) AnalyzerSpeed::slow, "choosing Slow in the selector sets the analyzer speed");
        p.setAnalyzerSpeed ((int) AnalyzerSpeed::veryFast);
        editor->refresh();
        check (box.getText() == "Very Fast", "the selector follows a change from the session");
        p.setAnalyzerSpeed ((int) AnalyzerSpeed::medium);
        editor->refresh();
        editor->setAdvancedShown (false);
    }

    void testDisplaySmoothness()
    {
        // A spectrally smooth input: one impulse per FFT frame through a one-pole low-pass at 60 Hz
        // (flat below, -6 dB / octave above), so the low end of the display must be a smooth slope.
        auto p = makeProcessor();
        const auto period = std::max (64, p->getLatencySamples());
        const auto n = (size_t) defaultRate;
        Stereo in (std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f));
        const auto a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 60.0 / defaultRate);
        float z = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            z = (1.0f - a) * ((i % (size_t) period) == 0 ? 8.0f : 0.0f) + a * z;
            in.l[i] = in.r[i] = z;
        }
        render (*p, in);
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "smoothness: editor");
        if (editor == nullptr) return;

        for (auto size : editorSizes)
        {
            editor->setSize (size.x, size.y);
            auto& graph = editor->getGraph();
            graph.poll();
            (void) graph.createComponentSnapshot (graph.getLocalBounds(), true, 1.0f); // builds the paths
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            const auto x0 = (int) std::ceil (graph.xForFrequency (40.0f)), x1 = (int) std::floor (graph.xForFrequency (500.0f));
            for (int which = 0; which < 2; ++which)
            {
                const auto& path = which == 0 ? graph.getOutputPath() : graph.getReductionEdge();
                const auto st = stepStats (sampleColumns (path, x0, x1));
                const auto name = juce::String (which == 0 ? "output spectrum" : "reduction edge").toStdString();
                check (st.missing == 0 && st.steps == 0 && st.maxBend < 0.5f,
                       label + ": " + name + " 40..500 Hz is smooth (" + std::to_string (st.steps) + " steps, max slope change "
                           + fmt (st.maxBend, 3) + " px, max slope " + fmt (st.maxSlope, 2) + " px / column, " + std::to_string (x1 - x0) + " columns)");
            }
        }
        base.reset();

        // Real resonances stay visible: a 220 Hz sine (low end, smoothed) and a 2 kHz sine (high
        // end, drawn as analysed) still show their reduction peaks.
        EditorFixture f;
        if (f.editor == nullptr) return;
        auto& graph = f.editor->getGraph();
        graph.poll();
        const auto& fr = graph.getFrame();
        const auto& drawn = graph.getDrawnReduction();
        const auto peakNear = [] (const auto& values, float lo, float hi)
        {
            auto best = std::numeric_limits<float>::lowest();
            for (int i = 0; i < dsp::DisplayFrame::numPoints; ++i)
                if (const auto fq = dsp::DisplayFrame::pointFrequency (i); fq >= lo && fq <= hi)
                    best = std::max (best, values[(size_t) i]);
            return best;
        };
        // The low sine is a narrow spectral peak (the default low band leaves it uncut): compare its
        // height over the level an octave below.
        const auto lowRaw = peakNear (fr.input, 190.0f, 250.0f) - peakNear (fr.input, 100.0f, 125.0f);
        const auto lowDrawn = peakNear (graph.getDrawnInput(), 190.0f, 250.0f) - peakNear (graph.getDrawnInput(), 100.0f, 125.0f);
        const auto highRaw = peakNear (fr.reduction, 1800.0f, 2200.0f), highDrawn = peakNear (drawn, 1800.0f, 2200.0f);
        check (lowRaw > 6.0f && lowDrawn >= 0.75f * lowRaw, "the 220 Hz sine still stands out of the drawn spectrum after smoothing (" + db (lowDrawn) + " of " + db (lowRaw) + ")");
        check (highRaw > 1.0f && std::abs (highDrawn - highRaw) < 1.0e-4f, "the 2 kHz resonance is drawn as analysed (" + db (highDrawn) + " of " + db (highRaw) + ")");
        int changedHigh = 0;
        for (int i = 0; i < dsp::DisplayFrame::numPoints; ++i)
            if (dsp::DisplayFrame::pointFrequency (i) >= 1500.0f && std::abs (drawn[(size_t) i] - fr.reduction[(size_t) i]) > 1.0e-4f)
                ++changedHigh;
        check (changedHigh == 0, "no smoothing above 1.5 kHz (" + std::to_string (changedHigh) + " points changed)");
    }

    void testCompare()
    {
        auto p = makeProcessor();
        set (*p, id::depth, 60.0f);
        p->selectCompareSlot (1);
        check (p->getCompareSlot() == 1 && std::abs (get (*p, id::depth) - 60.0f) < 0.05f, "B starts as a copy of A");
        set (*p, id::depth, 10.0f);
        set (*p, bandFreq (4), 4321.0f);
        p->selectCompareSlot (0);
        check (std::abs (get (*p, id::depth) - 60.0f) < 0.05f && std::abs (get (*p, bandFreq (4)) - 4321.0f) > 10.0f, "A restores its settings");
        p->selectCompareSlot (1);
        check (std::abs (get (*p, id::depth) - 10.0f) < 0.05f && std::abs (get (*p, bandFreq (4)) - 4321.0f) < 1.0f, "B restores its settings");
        p->copyToOtherCompareSlot();
        p->selectCompareSlot (0);
        check (std::abs (get (*p, id::depth) - 10.0f) < 0.05f, "copy B -> A");
        check (! p->getUndoHistory().canUndo(), "switching A / B clears undo");
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                   testMetadata },
        { "latency reported and measured",              testLatency },
        { "depth 0 / bypass / mix 0 null",              testDepthZeroNull },
        { "a resonant peak is reduced",                 testResonantPeak },
        { "broadband noise mostly untouched",           testBroadband },
        { "delta = input - output",                     testDelta },
        { "sidechain keys the detector",                testSidechain },
        { "band listen",                                testListen },
        { "stereo link, focus, M/S, mono",              testStereo },
        { "extremes: no NaN / denormals, 44.1k..192k",  testExtremes },
        { "value text: no exponents, no -0",            testValueText },
        { "state round trip",                           testStateRoundTrip },
        { "factory and user presets",                   testPresets },
        { "no dead parameters",                         testNoDeadParameters },
        { "undo history",                               testUndoHistory },
        { "real-time safety and CPU",                   testRealtimeAndCpu },
        { "editor: size, version, one control each, layout fit", testEditor },
        { "graph: node drag, wheel, add / remove",      testGraphMouse },
        { "real mouse: every control at 3 sizes",       testRealMouse },
        { "weighting: pulled down = more reduction",    testWeightingInversion },
        { "0.1.0 state and preset migration",           testStateMigration },
        { "A / B compare",                              testCompare },
        { "display: smooth low end, detailed highs",    testDisplaySmoothness },
        { "display: Analyzer Speed ballistics and setting", testAnalyzerSpeed },
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
