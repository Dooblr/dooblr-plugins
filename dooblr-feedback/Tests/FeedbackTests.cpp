/*
    dooblr-feedback console tests: the real processor, feedback engine and editor.

    Every check prints nothing on success; failures print what was expected. Each test ends
    with "[PASS]" or "[FAIL]" and the summary line counts all checks. Measured numbers are
    printed as notes. Set DOOBLRFEEDBACK_EDITOR_SNAPSHOT=/path/prefix to write editor PNGs.
*/

#include <typeinfo>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "UI/ControlPanel.h"
#include "UI/FeedbackGraph.h"
#include "UI/NotchTable.h"
#include "UI/Theme.h"
#include "DSP/FeedbackEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
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
    using namespace dooblrfeedback;
    using namespace dooblrfeedback::params;
    using Processor = DooblrFeedbackProcessor;
    using Editor = DooblrFeedbackEditor;

    int failedChecks = 0, passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition) { ++passedChecks; return; }
        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n" << std::flush; }

    std::string fmt (double value, int decimals = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (decimals) << value;
        return s.str();
    }

    constexpr double defaultRate = 48000.0;
    constexpr int defaultBlock = 64;   // a live-sound buffer
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

    Stereo mono (std::vector<float> v) { auto r = v; return Stereo (std::move (v), std::move (r)); }

    std::vector<float> noise (size_t n, float amplitude, int seed)
    {
        juce::Random random (seed);
        std::vector<float> v (n);
        for (auto& x : v)
            x = amplitude * (random.nextFloat() * 2.0f - 1.0f);
        return v;
    }

    /** Pink noise (Paul Kellet's economy filter) at the given RMS (dBFS). */
    std::vector<float> pink (size_t n, double rmsDbfs, int seed)
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
        const auto gain = (float) (std::pow (10.0, rmsDbfs / 20.0) / std::sqrt (sum / (double) n));
        for (auto& x : v)
            x *= gain;
        return v;
    }

    void addSine (std::vector<float>& v, double rate, double freq, float amplitude, double phase = 0.0)
    {
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += amplitude * (float) std::sin (phase + twoPi * freq * (double) i / rate);
    }

    double rmsDb (const std::vector<float>& x, size_t start, size_t len)
    {
        double sum = 0.0;
        start = std::min (start, x.size());
        len = std::min (len, x.size() - start);
        for (size_t i = start; i < start + len; ++i)
            sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, len) + 1.0e-30);
    }

    double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        double worst = 0.0;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            worst = std::max (worst, (double) std::abs (a[i] - b[i]));
        return worst;
    }

    bool finiteAndNormal (const std::vector<float>& x)
    {
        for (auto v : x)
            if (! std::isfinite (v) || (v != 0.0f && std::abs (v) < FLT_MIN))
                return false;
        return true;
    }

    std::string db (double value) { return fmt (value, 1) + " dB"; }

    int activeCount (const dsp::NotchArray& notches)
    {
        int n = 0;
        for (const auto& x : notches) if (x.active) ++n;
        return n;
    }

    std::string notchList (const dsp::NotchArray& notches)
    {
        std::vector<dsp::Notch> v;
        for (const auto& x : notches) if (x.active) v.push_back (x);
        std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.freq < b.freq; });
        std::string s;
        for (const auto& x : v)
            s += (s.empty() ? "" : ", ") + fmt (x.freq, 0) + " Hz -" + fmt (x.cutDb, 1) + " dB" + (x.fixed ? " fixed" : "");
        return s.empty() ? "none" : s;
    }

    //==============================================================================
    /** RBJ band-pass (0 dB at the centre): the resonant path of the room / wedge. */
    struct BandPass
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        BandPass (double rate, double freq, double q) { retune (rate, freq, q); }
        void retune (double rate, double freq, double q) noexcept // keeps the state: a gliding resonance
        {
            const auto w = twoPi * freq / rate, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = alpha / a0; b2 = -alpha / a0; a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - alpha) / a0;
        }
        double process (double x) noexcept
        {
            const auto y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    struct LoopSpec
    {
        std::vector<double> freqs { 2000.0 };
        double gain = 1.2;        // loop gain at each resonance (> 1 runs away)
        double seconds = 6.0;
        int delay = 480;          // 10 ms: mic to speaker ~3.4 m
        double q = 8.0;
        double noiseDb = -40.0;   // the room / talker into the mic (RMS)
    };

    struct LoopRun
    {
        std::vector<float> mic;   // the mic signal (the loop's state)
        double firstNotchSec = -1.0;
        dsp::NotchArray notches {};
    };

    /** A closed acoustic loop: mic = tanh(noise + G * resonance(speaker delayed)), speaker = the
        plugin's output (or the mic itself without the plugin). tanh stands in for the PA clipping. */
    LoopRun runLoop (Processor* p, const LoopSpec& s, double rate = defaultRate, int block = defaultBlock)
    {
        const auto n = (size_t) (s.seconds * rate);
        const auto src = pink (n, s.noiseDb, 77);
        std::vector<BandPass> bands;
        for (auto f : s.freqs) bands.emplace_back (rate, f, s.q);
        std::vector<float> speaker ((size_t) 8192, 0.0f);
        // A real round trip can't be shorter than the audio buffer: the speaker plays a block only
        // after the plugin has processed it.
        const auto delay = std::max ((size_t) s.delay, (size_t) block);
        LoopRun run;
        run.mic.assign (n, 0.0f);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos < n; pos += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, n - pos);
            buffer.setSize (2, len, false, false, true);
            for (int i = 0; i < len; ++i)
            {
                const auto t = pos + (size_t) i;
                const auto delayed = t >= delay ? (double) speaker[(t - delay) & 8191] : 0.0;
                double fb = 0.0;
                for (auto& b : bands) fb += b.process (delayed);
                const auto x = (float) std::tanh ((double) src[t] + s.gain * fb);
                run.mic[t] = x;
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            if (p != nullptr)
                p->processBlock (buffer, midi);
            for (int i = 0; i < len; ++i)
                speaker[(pos + (size_t) i) & 8191] = buffer.getSample (0, i);
            if (p != nullptr && run.firstNotchSec < 0.0 && p->getEngine().countActive() > 0)
                run.firstNotchSec = (double) (pos + (size_t) len) / rate;
        }
        if (p != nullptr) run.notches = p->getNotches();
        return run;
    }

    /** RMS of 10 ms windows, dB. */
    std::vector<double> envelopeDb (const std::vector<float>& x, double rate)
    {
        const auto w = (size_t) (rate * 0.01);
        std::vector<double> e;
        for (size_t i = 0; i + w <= x.size(); i += w)
            e.push_back (rmsDb (x, i, w));
        return e;
    }

    double firstTimeAbove (const std::vector<double>& env, double threshold)
    {
        for (size_t i = 0; i < env.size(); ++i)
            if (env[i] > threshold) return (double) i * 0.01;
        return -1.0;
    }

    double maxOf (const std::vector<double>& env, size_t from = 0)
    {
        double m = -300.0;
        for (size_t i = from; i < env.size(); ++i) m = std::max (m, env[i]);
        return m;
    }

    //==============================================================================
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-feedback", "the plugin is called dooblr-feedback (" + p->getName().toStdString() + ")");
        check (juce::String (JucePlugin_VersionString) == DOOBLRFEEDBACK_PROJECT_VERSION, "version " JucePlugin_VersionString);
        check (p->getLatencySamples() == 0, "reported latency 0 samples");
        check (p->getTailLengthSeconds() <= 0.0, "no tail");
        check (! p->acceptsMidi() && ! p->producesMidi(), "no MIDI");
        check (p->getBusCount (true) == 2, "main input plus a sidechain (reference mic) bus");
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->checkBusesLayoutSupported (layout), "mono in / mono out is supported");
        check (p->getBypassParameter() != nullptr, "the host bypass is the Bypass parameter");
    }

    void testLatencyAndNull()
    {
        auto p = makeProcessor();
        const auto src = mono (pink ((size_t) defaultRate, -20.0, 3));
        const auto out = render (*p, src);
        check (maxDiff (src.l, out.l) == 0.0 && maxDiff (src.r, out.r) == 0.0, "no notches: the output is bit-identical to the input");

        auto q = makeProcessor();
        set (*q, id::detect, 0.0f);
        q->getEngine().addNotch (1000.0f, 18.0f, 20.0f, true);
        q->getEngine().addNotch (4000.0f, 12.0f, 10.0f, true);
        render (*q, Stereo ((size_t) 4800)); // let the cut settle
        Stereo imp ((size_t) 4800);
        imp.l[0] = imp.r[0] = 1.0f;
        const auto ir = render (*q, imp);
        size_t argmax = 0;
        for (size_t i = 0; i < ir.size(); ++i) if (std::abs (ir.l[i]) > std::abs (ir.l[argmax])) argmax = i;
        check (argmax == 0 && q->getLatencySamples() == 0, "impulse response peaks at sample " + std::to_string (argmax) + " (0 = zero latency)");
        Stereo sine ((size_t) defaultRate);
        addSine (sine.l, defaultRate, 1000.0, 0.5f); sine.r = sine.l;
        const auto cut = render (*q, sine);
        const auto change = rmsDb (cut.l, 24000, 24000) - rmsDb (sine.l, 24000, 24000);
        check (change < -15.0 && change > -20.0, "an 18 dB notch at 1 kHz cuts a 1 kHz sine by " + db (change));
        note ("latency: 0 samples reported and measured (IIR notches in the signal path; the FFT only steers them)");
    }

    //==============================================================================
    void testFeedbackLoop()
    {
        for (const auto gain : { 1.2, 1.05 })
        {
            LoopSpec spec;
            spec.gain = gain;
            const auto runaway = runLoop (nullptr, spec);
            auto p = makeProcessor();
            const auto run = runLoop (p.get(), spec);
            const auto envRun = envelopeDb (runaway.mic, defaultRate), env = envelopeDb (run.mic, defaultRate);
            const auto noiseDb = spec.noiseDb;
            const auto audible = firstTimeAbove (envRun, noiseDb + 6.0);
            const auto loudRun = firstTimeAbove (envRun, noiseDb + 20.0);
            const auto runSteady = rmsDb (runaway.mic, runaway.mic.size() - 96000, 96000);
            const auto steady = rmsDb (run.mic, run.mic.size() - 96000, 96000);
            const auto peakDb = maxOf (env);
            const auto label = "loop gain " + fmt (gain, 2) + " (+" + fmt (20.0 * std::log10 (gain), 1) + " dB) at 2 kHz";
            note (label + ": without the plugin the howl is +6 dB over the noise at " + fmt (audible * 1000.0, 0) + " ms, +20 dB at " + fmt (loudRun * 1000.0, 0)
                  + " ms, steady " + db (runSteady) + " RMS");
            note (label + ": with the plugin the first notch is in at " + fmt (run.firstNotchSec * 1000.0, 0) + " ms ("
                  + fmt ((run.firstNotchSec - audible) * 1000.0, 0) + " ms relative to the unprotected +6 dB point), peak 10 ms RMS " + db (peakDb)
                  + " (" + db (peakDb - noiseDb) + " over the noise), steady " + db (steady) + " (" + db (steady - noiseDb) + " over the noise); notches: " + notchList (run.notches));
            check (run.firstNotchSec > 0.0 && run.firstNotchSec < (gain > 1.1 ? 0.5 : 1.5), label + ": a notch is placed quickly");
            check (steady < runSteady - 25.0, label + ": the steady state is far below the runaway (" + db (steady) + " vs " + db (runSteady) + ")");
            check (steady < noiseDb + 3.0, label + ": the steady state is within 3 dB of the loop-free noise level");
            check (peakDb < noiseDb + 20.0, label + ": the howl never got 20 dB over the noise (" + db (peakDb - noiseDb) + ")");
            check (activeCount (run.notches) <= 4, label + ": few notches used (" + std::to_string (activeCount (run.notches)) + ")");
            bool nearF = true;
            for (const auto& n : run.notches) if (n.active) nearF = nearF && std::abs (std::log2 (n.freq / 2000.0f)) < 0.2f;
            check (nearF, label + ": every notch is at the resonance");
            check (finiteAndNormal (run.mic), label + ": no NaN / denormals");
            float maxCut = 0.0f;
            for (const auto& n : run.notches) if (n.active) maxCut = std::max (maxCut, n.cutDb);
            check (maxCut <= get (*p, id::maxCut) + 0.01f, label + ": cut within Max cut");
        }
    }

    void testMultiLoop()
    {
        LoopSpec spec;
        spec.freqs = { 700.0, 2500.0, 5300.0 };
        spec.gain = 1.15;
        spec.seconds = 8.0;
        const auto runaway = runLoop (nullptr, spec);
        auto p = makeProcessor();
        const auto run = runLoop (p.get(), spec);
        const auto steady = rmsDb (run.mic, run.mic.size() - 96000, 96000);
        const auto runSteady = rmsDb (runaway.mic, runaway.mic.size() - 96000, 96000);
        std::set<int> covered;
        for (const auto& n : run.notches)
            if (n.active)
                for (int i = 0; i < 3; ++i)
                    if (std::abs (std::log2 (n.freq / (float) spec.freqs[(size_t) i])) < 0.2f) covered.insert (i);
        note ("three resonances (700 / 2500 / 5300 Hz, loop gain 1.15): first notch " + fmt (run.firstNotchSec * 1000.0, 0) + " ms, steady "
              + db (steady) + " vs runaway " + db (runSteady) + "; notches: " + notchList (run.notches));
        check (covered.size() == 3, "all three feedback frequencies get a notch (" + std::to_string (covered.size()) + ")");
        check (steady < spec.noiseDb + 3.0, "multi-frequency: steady state within 3 dB of the noise (" + db (steady - spec.noiseDb) + ")");
        check (activeCount (run.notches) <= 9, "multi-frequency: few notches (" + std::to_string (activeCount (run.notches)) + ")");
    }

    /** A howl building up: a tone rising 40 dB/s from -70 dBFS in -40 dBFS pink noise. Time is
        measured from the moment the tone's RMS reaches the noise RMS. */
    void testDetectionSpeed()
    {
        for (const auto aggressive : { false, true })
            for (const auto freq : { 250.0, 1000.0, 3150.0, 8000.0 })
            {
                auto p = makeProcessor();
                set (*p, id::mode, aggressive ? 1.0f : 0.0f);
                const auto n = (size_t) (defaultRate * 3.0);
                auto x = pink (n, -40.0, 5);
                const auto start = (size_t) (0.5 * defaultRate);
                for (size_t i = start; i < n; ++i)
                {
                    const auto t = (double) (i - start) / defaultRate;
                    const auto level = std::min (-6.0, -70.0 + 40.0 * t);
                    x[i] += (float) (std::pow (10.0, level / 20.0) * std::sqrt (2.0) * std::sin (twoPi * freq * (double) i / defaultRate));
                }
                const auto crossSec = 0.5 + 30.0 / 40.0;
                juce::AudioBuffer<float> buffer (2, defaultBlock);
                juce::MidiBuffer midi;
                double engaged = -1.0;
                for (size_t pos = 0; pos + defaultBlock <= n; pos += defaultBlock)
                {
                    buffer.copyFrom (0, 0, x.data() + pos, defaultBlock);
                    buffer.copyFrom (1, 0, x.data() + pos, defaultBlock);
                    p->processBlock (buffer, midi);
                    if (engaged < 0.0)
                        for (const auto& nn : p->getNotches())
                            if (nn.active && nn.cutDb >= 3.0f && std::abs (std::log2 (nn.freq / (float) freq)) < 0.1f)
                                engaged = (double) (pos + defaultBlock) / defaultRate;
                }
                // The tone grows 40 dB/s through -40 dB RMS pink noise. It can be caught before it is
                // as loud as the whole broadband noise (negative ms), so the check is on the level
                // the howl had reached by then, relative to the noise.
                const auto ms = (engaged - crossSec) * 1000.0;
                const auto toneDb = -70.0 + 40.0 * (engaged - 0.5);
                const auto overNoise = toneDb - (-40.0);
                const auto label = std::string (aggressive ? "aggressive " : "safe ") + fmt (freq, 0) + " Hz";
                note (label + ": 3 dB of cut at " + fmt ((engaged - 0.5) * 1000.0, 0) + " ms after the tone started, "
                      + fmt (ms, 0) + " ms relative to the tone reaching the noise RMS (tone then " + fmt (overNoise, 1) + " dB re noise); notches: " + notchList (p->getNotches()));
                check (engaged > 0.0 && overNoise < (aggressive ? 6.0 : 10.0), label + ": a growing howl is cut before it is clearly above the noise");
            }
    }

    //==============================================================================
    std::vector<float> harmonicTone (size_t n, double rate, double f0, int harmonics, float amplitude, double vibratoHz = 0.0, double vibratoDepth = 0.0, int seed = 1)
    {
        std::vector<float> v (n, 0.0f);
        juce::Random r (seed);
        std::vector<double> phase ((size_t) harmonics + 1);
        for (auto& ph : phase) ph = r.nextDouble() * twoPi;
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / rate;
            const auto f = f0 * (1.0 + vibratoDepth * std::sin (twoPi * vibratoHz * t));
            double s = 0.0;
            for (int k = 1; k <= harmonics; ++k)
            {
                phase[(size_t) k] += twoPi * f * k / rate;
                if (f * k < rate * 0.45) s += std::sin (phase[(size_t) k]) / (double) k;
            }
            v[i] = (float) (amplitude * s);
        }
        return v;
    }

    int countNotches (Processor& p, const std::vector<float>& x)
    {
        render (p, mono (x));
        return activeCount (p.getNotches());
    }

    void testMusicIsLeftAlone()
    {
        const auto n = (size_t) (defaultRate * 6.0);
        auto chord = pink (n, -50.0, 8);
        for (const auto f : { 261.63, 329.63, 392.0 })
        {
            const auto tone = harmonicTone (n, defaultRate, f, 10, 0.12f, 0.0, 0.0, (int) f);
            for (size_t i = 0; i < n; ++i) chord[i] += tone[i];
        }
        auto vowel = harmonicTone (n, defaultRate, 220.0, 12, 0.25f, 5.5, 0.01, 4);
        auto bass = harmonicTone (n, defaultRate, 55.0, 16, 0.3f, 0.0, 0.0, 9);
        std::vector<float> melody (n, 0.0f);
        {
            const double notes[] = { 523.25, 587.33, 659.25, 698.46, 783.99, 880.0, 987.77, 1046.5 };
            double phase = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const auto noteIndex = (i / (size_t) (0.3 * defaultRate)) % 8;
                phase += twoPi * notes[(noteIndex * 3) % 8] / defaultRate;
                melody[i] = (float) (0.3 * std::sin (phase));
            }
        }

        const std::vector<std::pair<std::string, std::vector<float>*>> cases {
            { "sustained C-E-G chord, 10 harmonics", &chord }, { "vibrato vowel", &vowel }, { "55 Hz bass, 16 harmonics", &bass }, { "sine melody, 300 ms notes", &melody } };
        int aggressiveNotches = 0;
        for (const auto& [name, signal] : cases)
        {
            auto safe = makeProcessor();
            const auto safeCount = countNotches (*safe, *signal);
            check (safeCount == 0, "safe mode: " + name + " gets no notches (" + std::to_string (safeCount) + ": " + notchList (safe->getNotches()) + ")");
            auto aggr = makeProcessor();
            set (*aggr, id::mode, 1.0f);
            const auto aggressiveCount = countNotches (*aggr, *signal);
            aggressiveNotches += aggressiveCount;
            note (name + ": safe " + std::to_string (safeCount) + " notches, aggressive " + std::to_string (aggressiveCount) + " (" + notchList (aggr->getNotches()) + ")");
        }
        note ("aggressive mode false positives over the 4 x 6 s music set: " + std::to_string (aggressiveNotches) + " notches");

        auto sine = pink (n, -50.0, 9);
        addSine (sine, defaultRate, 1500.0, 0.1f);
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        double caught = -1.0;
        for (size_t pos = 0; pos + defaultBlock <= n; pos += defaultBlock)
        {
            buffer.copyFrom (0, 0, sine.data() + pos, defaultBlock);
            buffer.copyFrom (1, 0, sine.data() + pos, defaultBlock);
            p->processBlock (buffer, midi);
            if (caught < 0.0 && p->getEngine().countActive() > 0) caught = (double) pos / defaultRate;
        }
        note ("a steady pure sine (-23 dBFS RMS, 1.5 kHz) is notched in safe mode after " + fmt (caught * 1000.0, 0) + " ms (by design: a stable pure sine looks like feedback)");
        check (caught > 0.0, "a stable pure sine is eventually treated as feedback");
    }

    /** Synthetic speech: a gliding pulse train through moving formants, syllables with gaps, fricatives. */
    std::vector<float> speech (size_t n, double rate, int seed)
    {
        juce::Random r (seed);
        std::vector<float> v (n, 0.0f);
        double phase = 0.0, f0 = 120.0;
        struct Formant { double f, bw; };
        const Formant vowels[][3] = { { { 730, 90 }, { 1090, 110 }, { 2440, 170 } }, { { 270, 60 }, { 2290, 100 }, { 3010, 120 } },
                                      { { 530, 70 }, { 1840, 100 }, { 2480, 120 } }, { { 300, 60 }, { 870, 90 }, { 2240, 120 } },
                                      { { 660, 80 }, { 1720, 110 }, { 2410, 150 } } };
        std::array<std::array<double, 2>, 3> st {};
        const auto syllable = (size_t) (0.18 * rate);
        for (size_t i = 0; i < n; ++i)
        {
            const auto s = i / syllable, inSyl = i % syllable;
            const auto& vw = vowels[(s * 7 + 3) % 5];
            const auto voiced = (s % 6) != 5;
            if (inSyl == 0) f0 = 100.0 + 70.0 * r.nextDouble();
            f0 *= 1.0 + 0.6 / rate * ((s % 2) ? 1.0 : -1.0);
            phase += f0 / rate;
            double src = 0.0;
            if (phase >= 1.0) { phase -= 1.0; src = 1.0; }
            const auto env = voiced ? std::sin (juce::MathConstants<double>::pi * (double) inSyl / (double) syllable) : 0.0;
            src = src * env + (inSyl < syllable / 6 && s % 3 == 0 ? 0.08 * (r.nextDouble() * 2.0 - 1.0) : 0.0);
            double out = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const auto w = twoPi * vw[k].f / rate, rr = std::exp (-juce::MathConstants<double>::pi * vw[k].bw / rate);
                const auto a1 = -2.0 * rr * std::cos (w), a2 = rr * rr;
                auto& z = st[(size_t) k];
                const auto y = src * (1.0 - rr) - a1 * z[0] - a2 * z[1];
                z[1] = z[0]; z[0] = y;
                out += y * (k == 0 ? 1.0 : 0.6);
            }
            v[i] = (float) (out * 3.0);
        }
        return v;
    }

    void testSpeechAndNoise()
    {
        const auto n = (size_t) (defaultRate * 8.0);
        const std::vector<std::pair<std::string, std::vector<float>>> cases {
            { "pink noise -20 dBFS", pink (n, -20.0, 21) }, { "white noise", noise (n, 0.3f, 22) }, { "synthetic speech", speech (n, defaultRate, 23) } };
        for (const auto& [name, x] : cases)
            for (const auto aggressive : { false, true })
            {
                auto p = makeProcessor();
                set (*p, id::mode, aggressive ? 1.0f : 0.0f);
                const auto src = mono (x);
                const auto out = render (*p, src);
                const auto count = activeCount (p->getNotches());
                const auto label = (aggressive ? "aggressive: " : "safe: ") + name;
                if (aggressive)
                    note (label + ": " + std::to_string (count) + " notches (" + notchList (p->getNotches()) + ")");
                else
                {
                    check (count == 0, label + " gets no notches (" + notchList (p->getNotches()) + ")");
                    check (maxDiff (src.l, out.l) == 0.0, label + " passes bit-exact");
                }
            }
    }

    void testSidechain()
    {
        auto p = makeProcessor (defaultRate, defaultBlock, true);
        set (*p, id::sidechain, 1.0f);
        check (p->isSidechainConnected(), "the sidechain bus is connected");
        const auto n = (size_t) (defaultRate * 3.0);
        auto howl = pink (n, -40.0, 31);
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / defaultRate;
            howl[i] += (float) (std::pow (10.0, std::min (-10.0, -60.0 + 40.0 * t) / 20.0) * std::sin (twoPi * 1800.0 * t));
        }
        const auto main = mono (pink (n, -30.0, 32));
        const auto sc = mono (howl);
        render (*p, main, &sc);
        bool found = false;
        for (const auto& x : p->getNotches()) found = found || (x.active && std::abs (x.freq - 1800.0f) < 60.0f);
        check (found, "detection follows the sidechain (reference mic) when SC is on (" + notchList (p->getNotches()) + ")");

        auto q = makeProcessor (defaultRate, defaultBlock, true);
        set (*q, id::sidechain, 1.0f);
        const auto quiet = mono (pink (n, -40.0, 33));
        render (*q, mono (howl), &quiet);
        check (q->getEngine().countActive() == 0, "with SC on, the main input does not drive detection (" + notchList (q->getNotches()) + ")");

        auto r = makeProcessor();
        set (*r, id::sidechain, 1.0f);
        check (! r->isSidechainConnected(), "no sidechain bus connected");
        render (*r, mono (howl));
        check (r->getEngine().countActive() > 0, "SC on but disconnected: detection falls back to the main input");
    }

    //==============================================================================
    void testClearLockRelease()
    {
        auto p = makeProcessor();
        auto& e = p->getEngine();
        const auto a = e.addNotch (500.0f, 10.0f, 20.0f, false);
        const auto b = e.addNotch (1200.0f, 10.0f, 20.0f, false);
        const auto c = e.addNotch (3000.0f, 10.0f, 20.0f, false);
        check (a > 0 && b > 0 && c > 0 && a != b && b != c, "added notches get unique ids");
        check (p->clearNotch (b) && ! p->clearNotch (b), "clear one notch (a second clear of the same id is refused)");
        check (activeCount (p->getNotches()) == 2, "two notches remain");
        check (p->setNotchFixed (a, true), "lock a notch");
        bool fixedA = false;
        for (const auto& n : p->getNotches()) if (n.id == a) fixedA = n.fixed;
        check (fixedA, "the locked notch is fixed");
        p->clearAllNotches();
        check (activeCount (p->getNotches()) == 0, "clear all empties the bank");

        // Release: floating notches let go after quiet, fixed ones stay.
        auto r = makeProcessor();
        set (*r, id::release, 2.0f);
        set (*r, id::liveNotches, 0.0f);
        set (*r, id::recoverSpeed, 2.0f);   // Fast: 12 dB is back to 0 in about 16 s (0.1.2 staged auto-recover)
        const auto fl = r->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
        const auto fx = r->getEngine().addNotch (2200.0f, 12.0f, 20.0f, true);
        render (*r, mono (pink ((size_t) (defaultRate * 20.0), -40.0, 41)));
        bool floatingGone = true, fixedKept = false;
        for (const auto& n : r->getNotches())
        {
            if (n.active && n.id == fl) floatingGone = false;
            if (n.active && n.id == fx && n.cutDb > 11.9f) fixedKept = true;
        }
        check (floatingGone, "Auto-recover: a floating notch recovers after quiet (Recover after 2 s, Fast, 20 s of noise); left: " + notchList (r->getNotches()));
        check (fixedKept, "a fixed notch keeps its full depth");

        auto h = makeProcessor();
        set (*h, id::release, 2.0f);
        set (*h, id::liveNotches, 1.0f);
        h->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
        render (*h, mono (pink ((size_t) (defaultRate * 8.0), -40.0, 42)));
        check (activeCount (h->getNotches()) == 1, "Auto-recover Off: floating notches are kept");

        auto ring = makeProcessor();
        set (*ring, id::ringOut, 1.0f);
        LoopSpec spec;
        spec.seconds = 3.0;
        const auto run = runLoop (ring.get(), spec);
        bool allFixed = activeCount (run.notches) > 0;
        for (const auto& n : run.notches) if (n.active) allFixed = allFixed && n.fixed;
        check (allFixed, "Ring-out: notches found are fixed (" + notchList (run.notches) + ")");

        auto off = makeProcessor();
        set (*off, id::detect, 0.0f);
        const auto offRun = runLoop (off.get(), spec);
        check (activeCount (offRun.notches) == 0, "Auto off: no new notches");

        auto full = makeProcessor();
        int added = 0;
        for (int i = 0; i < 30; ++i)
            if (full->getEngine().addNotch (100.0f * std::pow (1.2f, (float) i), 6.0f, 20.0f, true) > 0) ++added;
        check (added == dsp::maxNotches && added == 24, "the bank holds exactly 24 notches (" + std::to_string (added) + ")");
        check (full->getEngine().addNotch (19000.0f, 6.0f, 20.0f, true) == 0, "a full bank refuses a new notch");
    }

    void testMaxCutAndZipper()
    {
        auto p = makeProcessor();
        set (*p, id::maxCut, 6.0f);
        LoopSpec spec;
        spec.gain = 1.6;
        spec.seconds = 4.0;
        const auto run = runLoop (p.get(), spec);
        float deepest = 0.0f;
        for (const auto& n : run.notches) if (n.active) deepest = std::max (deepest, n.cutDb);
        note ("Max cut 6 dB at loop gain 1.6: deepest notch " + db (deepest) + ", notches " + notchList (run.notches));
        check (deepest <= 6.01f && deepest > 0.0f, "Max cut caps every notch");

        // A notch engaging far from a tone must not click.
        auto z = makeProcessor();
        set (*z, id::detect, 0.0f);
        Stereo tone ((size_t) defaultRate);
        addSine (tone.l, defaultRate, 200.0, 0.5f);
        tone.r = tone.l;
        Stereo first ((size_t) 24000), second ((size_t) 24000);
        std::copy (tone.l.begin(), tone.l.begin() + 24000, first.l.begin()); first.r = first.l;
        std::copy (tone.l.begin() + 24000, tone.l.end(), second.l.begin()); second.r = second.l;
        const auto o1 = render (*z, first);
        z->getEngine().addNotch (3000.0f, 18.0f, 20.0f, false);
        const auto o2 = render (*z, second);
        double worst = 0.0, worst2 = 0.0;
        for (size_t i = 0; i < o2.size(); ++i)
        {
            worst = std::max (worst, (double) std::abs (o2.l[i] - second.l[i]));
            if (i >= 2)
            {
                const auto d2 = (o2.l[i] - 2.0f * o2.l[i - 1] + o2.l[i - 2]) - (second.l[i] - 2.0f * second.l[i - 1] + second.l[i - 2]);
                worst2 = std::max (worst2, (double) std::abs (d2));
            }
        }
        note ("a 3 kHz band engaging under a 200 Hz sine: max deviation " + fmt (worst, 5) + ", max second-difference deviation " + fmt (worst2, 6));
        check (worst < 0.02 && worst2 < 1.0e-3, "no click when a band engages (deviation " + fmt (worst, 5) + ")");

        // Clearing a notch under its own tone ramps the level back up.
        auto u = makeProcessor();
        set (*u, id::detect, 0.0f);
        const auto nid = u->getEngine().addNotch (1000.0f, 18.0f, 20.0f, true);
        Stereo t2 ((size_t) defaultRate);
        addSine (t2.l, defaultRate, 1000.0, 0.5f); t2.r = t2.l;
        render (*u, t2);
        u->clearNotch (nid);
        const auto after = render (*u, t2);
        double jump = 0.0;
        for (size_t i = 1; i < 4800; ++i) jump = std::max (jump, (double) std::abs (std::abs (after.l[i]) - std::abs (after.l[i - 1])));
        const auto maxStep = 0.5 * twoPi * 1000.0 / defaultRate + 0.01;
        check (jump < maxStep, "clearing a notch restores the level smoothly (largest step " + fmt (jump, 4) + ")");
        check (rmsDb (after.l, 24000, 24000) - rmsDb (t2.l, 24000, 24000) > -0.5, "after clearing, the tone is back at full level");
    }

    //==============================================================================
    void testInfiniteCut()
    {
        // Text: the top of Max Cut reads infinity and parses back from several spellings.
        auto p = makeProcessor();
        auto* param = p->getState().getParameter (id::maxCut);
        const auto topText = param->getText (1.0f, 32);
        note ("Max cut at the top reads \"" + topText.toStdString() + "\"; 0.5 reads \"" + param->getText (0.5f, 32).toStdString() + "\"");
        check (topText.contains (juce::String::fromUTF8 ("\xe2\x88\x9e")), "Max cut's top reads infinity");
        for (const auto* spelling : { "inf", "Infinite", "full", "-inf dB", "\xe2\x88\x9e", "\xe2\x88\x92\xe2\x88\x9e dB" })
        {
            const auto v = param->getValueForText (juce::String::fromUTF8 (spelling));
            check (v >= 0.999f, (juce::String ("\"") + juce::String::fromUTF8 (spelling) + "\" parses to the full notch (" + juce::String (v, 4) + ")").toStdString());
        }
        check (param->getValueForText (topText) >= 0.999f, "the infinity text round-trips");
        const auto t18 = param->getValueForText ("18 dB");
        check (std::abs (param->convertFrom0to1 (t18) - 18.0f) < 0.05f, "\"18 dB\" still parses to 18");
        check (std::abs (param->convertFrom0to1 (param->getValueForText (param->getText (t18, 32))) - 18.0f) < 0.05f, "18 dB round-trips");
        check (std::abs (param->convertFrom0to1 (param->getDefaultValue()) - 18.0f) < 0.05f, "the default stays 18 dB");

        // A full notch: a zero on the unit circle. Measured against the analytic response.
        auto z = makeProcessor();
        set (*z, id::detect, 0.0f);
        set (*z, id::maxCut, params::maxCutInfinite);
        z->getEngine().addNotch (1000.0f, dsp::FeedbackEngine::fullCutDb, 20.0f, true);
        Stereo tone ((size_t) (3 * defaultRate));
        addSine (tone.l, defaultRate, 1000.0, 0.5f); tone.r = tone.l;
        const auto out = render (*z, tone);
        bool finite = true;
        for (auto v : out.l) finite = finite && std::isfinite (v);
        const auto centre = rmsDb (out.l, (size_t) (2 * defaultRate), (size_t) defaultRate) - rmsDb (tone.l, (size_t) (2 * defaultRate), (size_t) defaultRate);
        note ("full notch at 1 kHz: tone down " + db (-centre) + " (analytic "
              + db (-dsp::FeedbackEngine::notchResponseDb (1000.0f, 1000.0f, dsp::FeedbackEngine::fullCutDb, 20.0f)) + ")");
        check (finite && centre < -80.0, "a full notch takes more than 80 dB off at its centre");
        for (const auto f : { 900.0, 1050.0, 1300.0, 3000.0 })
        {
            auto w = makeProcessor();
            set (*w, id::detect, 0.0f);
            set (*w, id::maxCut, params::maxCutInfinite);
            w->getEngine().addNotch (1000.0f, dsp::FeedbackEngine::fullCutDb, 20.0f, true);
            Stereo s ((size_t) (2 * defaultRate));
            addSine (s.l, defaultRate, f, 0.5f); s.r = s.l;
            const auto o = render (*w, s);
            const auto measured = rmsDb (o.l, (size_t) defaultRate, (size_t) defaultRate) - rmsDb (s.l, (size_t) defaultRate, (size_t) defaultRate);
            const auto analytic = dsp::FeedbackEngine::bandResponseDb ((float) f, 1000.0f, dsp::FeedbackEngine::fullCutDb, 20.0f,
                                                                       dsp::FeedbackEngine::defaultBellQ);
            check (std::abs (measured - analytic) < 0.3, "full notch at " + fmt (f, 0) + " Hz matches the analytic response (" + db (measured) + " vs " + db (analytic) + ")");
        }

        // The ramp from nothing to a full notch under its own tone is smooth: the envelope falls,
        // never jumps, and nothing is NaN.
        auto r = makeProcessor();
        set (*r, id::detect, 0.0f);
        set (*r, id::maxCut, params::maxCutInfinite);
        Stereo pre ((size_t) defaultRate), post ((size_t) (2 * defaultRate));
        addSine (pre.l, defaultRate, 1000.0, 0.5f); pre.r = pre.l;
        addSine (post.l, defaultRate, 1000.0, 0.5f, twoPi * 1000.0 * (double) pre.size() / defaultRate); post.r = post.l;
        render (*r, pre);
        r->getEngine().addNotch (1000.0f, dsp::FeedbackEngine::fullCutDb, 20.0f, true);
        const auto ramp = render (*r, post);
        double jump = 0.0; bool rampFinite = true;
        for (size_t i = 1; i < ramp.size(); ++i)
        {
            rampFinite = rampFinite && std::isfinite (ramp.l[i]);
            jump = std::max (jump, (double) std::abs (ramp.l[i] - ramp.l[i - 1]));
        }
        const auto maxStep = 0.5 * twoPi * 1000.0 / defaultRate + 0.005; // a 0.5 sine's own largest step
        const auto end = rmsDb (ramp.l, (size_t) defaultRate, (size_t) defaultRate) - rmsDb (post.l, (size_t) defaultRate, (size_t) defaultRate);
        note ("ramp to a full notch: largest sample step " + fmt (jump, 4) + " (sine's own " + fmt (maxStep - 0.005, 4) + "), level after 1 s " + db (end));
        check (rampFinite && jump < maxStep, "the ramp to a full notch is click-free and NaN-free");
        check (end < -80.0, "the ramp reaches the full notch");
        // ...and back: Max Cut down to 18 dB while the notch is full, no click.
        set (*r, id::maxCut, 18.0f);
        Stereo back ((size_t) defaultRate);
        addSine (back.l, defaultRate, 1000.0, 0.5f, twoPi * 1000.0 * (double) (pre.size() + post.size()) / defaultRate); back.r = back.l;
        const auto up = render (*r, back);
        double jumpUp = 0.0;
        for (size_t i = 1; i < up.size(); ++i) jumpUp = std::max (jumpUp, (double) std::abs (up.l[i] - up.l[i - 1]));
        const auto settled = rmsDb (up.l, (size_t) (defaultRate / 2), (size_t) (defaultRate / 2)) - rmsDb (back.l, (size_t) (defaultRate / 2), (size_t) (defaultRate / 2));
        check (jumpUp < maxStep && std::abs (settled + 18.0) < 1.0, "lowering Max Cut from infinity is smooth and settles at 18 dB (" + db (settled) + ")");

        // Sessions from 0.1.0 (Max Cut stored in dB, range 3 - 30) load to the same dB.
        for (const auto oldDb : { 3.0f, 18.0f, 24.0f, 30.0f })
        {
            auto a = makeProcessor();
            juce::MemoryBlock block;
            a->getStateInformation (block);
            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            bool patched = false;
            if (xml != nullptr)
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == id::maxCut) { e->setAttribute ("value", (double) oldDb); patched = true; }
            check (patched, "found Max Cut in the saved state");
            if (! patched) continue;
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            auto b = makeProcessor();
            b->setStateInformation (old.getData(), (int) old.getSize());
            const auto loaded = b->getState().getParameter (id::maxCut);
            const auto loadedDb = loaded->convertFrom0to1 (loaded->getValue());
            check (std::abs (loadedDb - oldDb) < 0.05f && ! params::isInfiniteCut (loadedDb),
                   "a 0.1.0 session with Max Cut " + fmt (oldDb, 0) + " dB loads as " + fmt (loadedDb, 2) + " dB (finite)");
        }

        // Range high: a 0.1.0 session (no stateVersion) still at the old 16 kHz default moves to
        // 20 kHz; any other value, or a 0.1.1 session at 16 kHz, is kept.
        struct RangeCase { float saved; bool legacy; float expected; };
        for (const auto rc : { RangeCase { 16000.0f, true, 20000.0f }, RangeCase { 12000.0f, true, 12000.0f },
                               RangeCase { 16000.0f, false, 16000.0f } })
        {
            auto a = makeProcessor();
            juce::MemoryBlock block;
            a->getStateInformation (block);
            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            bool patched = false;
            if (xml != nullptr)
            {
                if (rc.legacy) xml->removeAttribute ("stateVersion");
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == id::rangeHigh) { e->setAttribute ("value", (double) rc.saved); patched = true; }
            }
            check (patched, "found Range high in the saved state");
            if (! patched) continue;
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            auto b = makeProcessor();
            b->setStateInformation (old.getData(), (int) old.getSize());
            const auto loaded = b->getState().getRawParameterValue (id::rangeHigh)->load();
            const auto what = std::string (rc.legacy ? "a 0.1.0" : "a 0.1.1") + " session with Range high " + fmt (rc.saved, 0)
                            + " Hz loads as " + fmt (loaded, 0) + " Hz";
            check (std::abs (loaded - rc.expected) < 1.0f, what);
            note (what);
        }
    }

    //==============================================================================
    void testStateNotesPresets()
    {
        auto p = makeProcessor();
        set (*p, id::sensitivity, 72.0f);
        set (*p, id::maxCut, 9.0f);
        set (*p, id::mode, 1.0f);
        const juce::String notesText = juce::String::fromUTF8 ("Main hall \xe2\x80\x94 wedge 3 rings at 2.5k; caf\xc3\xa9 stage left");
        p->setNotes (notesText);
        p->getEngine().addNotch (2510.0f, 9.0f, 18.0f, true);
        p->getEngine().addNotch (630.0f, 4.5f, 12.0f, false);
        p->setAnalyzerSpeed (2);
        juce::MemoryBlock block;
        p->getStateInformation (block);

        auto q = makeProcessor();
        q->setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (get (*q, id::sensitivity) - 72.0f) < 0.01f && std::abs (get (*q, id::maxCut) - 9.0f) < 0.01f && (int) get (*q, id::mode) == 1, "parameters round-trip");
        check (q->getNotes() == notesText, "notes round-trip (UTF-8)");
        check (notchList (q->getNotches()) == notchList (p->getNotches()), "notches round-trip (" + notchList (q->getNotches()) + ")");
        check (q->getAnalyzerSpeed() == 2, "Analyzer Speed round-trips");

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-feedback-tests-" + juce::String (juce::Time::currentTimeMillis()));
        dir.createDirectory();
        const auto file = dir.getChildFile (juce::String ("Main Hall") + presets::fileExtension);
        const auto saved = p->saveUserPreset (file);
        check (file.existsAsFile(), "save a room preset (" + saved.toStdString() + ")");

        auto r = makeProcessor();
        const auto loaded = r->loadUserPreset (file);
        check (r->getNotes() == notesText && notchList (r->getNotches()) == notchList (p->getNotches()) && std::abs (get (*r, id::maxCut) - 9.0f) < 0.01f,
               "load a room preset: notes, notches and settings (" + loaded.toStdString() + ")");
        check (r->getPresetName() == "Main Hall", "the loaded preset name shows (" + r->getPresetName().toStdString() + ")");

        juce::String error;
        const auto renamed = presets::rename (file, "Club Stage", error);
        check (renamed.existsAsFile() && ! file.existsAsFile() && error.isEmpty(), "rename a room preset (" + error.toStdString() + ")");
        auto s = makeProcessor();
        s->loadUserPreset (renamed);
        check (s->getPresetName() == "Club Stage", "the renamed preset carries its new name");

        const auto broken = dir.getChildFile (juce::String ("Broken") + presets::fileExtension);
        broken.replaceWithText ("not a preset");
        juce::String n2;
        dsp::NotchArray nn {};
        auto t = makeProcessor();
        const auto err = presets::load (t->getState(), broken, n2, nn);
        check (err.isNotEmpty(), "a broken preset file is rejected (" + err.toStdString() + ")");

        const auto& factory = presets::factory();
        check (factory.size() >= 3, "factory presets exist (" + std::to_string (factory.size()) + ")");
        auto f = makeProcessor();
        f->setNotes ("keep");
        f->getEngine().addNotch (1000.0f, 6.0f, 20.0f, true);
        for (int i = 0; i < (int) factory.size(); ++i)
            f->setCurrentProgram (i);
        check (f->getNotes() == "keep" && activeCount (f->getNotches()) == 1, "factory presets leave the room's notes and notches alone");
        dir.deleteRecursively();
    }

    void testUndoAndCompare()
    {
        auto p = makeProcessor();
        auto& history = p->getUndoHistory();
        auto* s = param (*p, id::sensitivity);
        const auto before = s->getValue();
        s->beginChangeGesture();
        s->setValueNotifyingHost (0.8f);
        s->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (s->getValue() - before) < 1.0e-5f, "undo restores the value");
        history.redo();
        check (std::abs (s->getValue() - 0.8f) < 1.0e-5f, "redo re-applies it");
        history.clear();
        s->setValueNotifyingHost (0.3f);
        check (! history.canUndo(), "automation is not an undo step");

        auto c = makeProcessor();
        set (*c, id::maxCut, 10.0f);
        c->copyToOtherCompareSlot();
        c->selectCompareSlot (1);
        set (*c, id::maxCut, 4.0f);
        c->selectCompareSlot (0);
        check (std::abs (get (*c, id::maxCut) - 10.0f) < 0.01f, "A keeps its settings");
        c->selectCompareSlot (1);
        check (std::abs (get (*c, id::maxCut) - 4.0f) < 0.01f && c->getCompareSlot() == 1, "B keeps its settings");
    }

    void testExtremes()
    {
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto p = makeProcessor (rate);
            LoopSpec spec;
            spec.seconds = 3.0;
            const auto run = runLoop (p.get(), spec, rate);
            const auto steady = rmsDb (run.mic, run.mic.size() - (size_t) rate, (size_t) rate);
            note (fmt (rate / 1000.0, 1) + " kHz: first notch " + fmt (run.firstNotchSec * 1000.0, 0) + " ms, steady " + db (steady) + "; " + notchList (run.notches));
            check (finiteAndNormal (run.mic) && steady < spec.noiseDb + 6.0, fmt (rate, 0) + " Hz: the loop is controlled with no NaN / denormals");
        }
        auto p = makeProcessor();
        Stereo burst ((size_t) 48000);
        for (size_t i = 0; i < burst.size(); ++i) burst.l[i] = burst.r[i] = (i % 2 == 0 ? 1.0f : -1.0f);
        check (finiteAndNormal (render (*p, burst).l), "full-scale Nyquist burst");
        check (finiteAndNormal (render (*p, Stereo ((size_t) 96000)).l), "silence");
        Stereo tiny ((size_t) 48000);
        for (auto& x : tiny.l) x = 1.0e-30f;
        tiny.r = tiny.l;
        p->getEngine().addNotch (1000.0f, 12.0f, 20.0f, true);
        const auto tinyOut = render (*p, tiny);
        bool denormalFree = true;
        for (auto v : tinyOut.l) denormalFree = denormalFree && std::isfinite (v);
        check (denormalFree, "denormal-level input");
        for (const auto block : { 1, 7, 33, 512, 2048 })
        {
            auto q = makeProcessor (defaultRate, block);
            LoopSpec spec;
            spec.seconds = 3.0;
            const auto run = runLoop (q.get(), spec, defaultRate, block);
            const auto steady = rmsDb (run.mic, run.mic.size() - 48000, 48000);
            note ("block " + std::to_string (block) + ": first notch " + fmt (run.firstNotchSec * 1000.0, 0) + " ms, steady " + db (steady));
            check (finiteAndNormal (run.mic) && steady < spec.noiseDb + 6.0, "block size " + std::to_string (block) + ": controlled");
        }
    }

    void testValueText()
    {
        auto p = makeProcessor();
        bool clean = true;
        for (auto* q : p->getParameters())
            for (float v = 0.0f; v <= 1.0f; v += 0.01f)
            {
                const auto t = q->getText (v, 64);
                if (t.containsChar ('e') && t.containsAnyOf ("0123456789") && t.containsAnyOf ("+-") && t.matchesWildcard ("*e[+-]*", true)) clean = false;
                if (t.startsWith ("-0") && ! t.containsAnyOf ("123456789")) clean = false;
            }
        check (clean, "no exponent notation and no -0 in parameter text");
        check (formatValue (-0.0001, 1) == "0.0", "formatValue never shows -0 (" + formatValue (-0.0001, 1).toStdString() + ")");
        check (formatFrequency (2510.0).isNotEmpty() && formatFrequency (85.0).isNotEmpty(), "formatFrequency: " + formatFrequency (85.0).toStdString() + ", " + formatFrequency (2510.0).toStdString());
        int cents = 99;
        check (noteName (440.0, &cents) == "A4" && cents == 0, "noteName(440) = A4 (" + noteName (440.0).toStdString() + ")");
        note ("2510 Hz shows as " + noteWithCents (2510.0).toStdString());
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        const auto src = pink ((size_t) defaultRate * 2, -20.0, 51);
        for (size_t pos = 0; pos + defaultBlock <= src.size(); pos += defaultBlock)
        {
            buffer.copyFrom (0, 0, src.data() + pos, defaultBlock);
            buffer.copyFrom (1, 0, src.data() + pos, defaultBlock);
            p->processBlock (buffer, midi);
        }
        allocationCount = 0;
        int i = 0;
        for (size_t pos = 0; pos + defaultBlock <= src.size(); pos += defaultBlock, ++i)
        {
            if (i % 50 == 0)
            {
                set (*p, id::sensitivity, (float) (i % 100));
                set (*p, id::mode, (float) ((i / 50) % 2));
                set (*p, id::maxCut, 6.0f + (float) (i % 12));
            }
            buffer.copyFrom (0, 0, src.data() + pos, defaultBlock);
            buffer.copyFrom (1, 0, src.data() + pos, defaultBlock);
            countAllocations = true; // only processBlock is counted (a host's parameter calls are not the audio path)
            p->processBlock (buffer, midi);
            countAllocations = false;
        }
        check (allocationCount == 0, "no allocation in processBlock across parameter changes (" + std::to_string (allocationCount.load()) + ")");

        std::string cpu;
        for (const auto notches : { 0, 1, 24 })
        {
            auto q = makeProcessor();
            if (notches > 0)
            {
                set (*q, id::detect, 0.0f);
                for (int k = 0; k < notches; ++k) q->getEngine().addNotch (80.0f * std::pow (1.25f, (float) k), 9.0f, 20.0f, true);
            }
            const auto seconds = 10.0;
            const auto x = pink ((size_t) (defaultRate * seconds), -20.0, 52);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (size_t pos = 0; pos + defaultBlock <= x.size(); pos += defaultBlock)
            {
                buffer.copyFrom (0, 0, x.data() + pos, defaultBlock);
                buffer.copyFrom (1, 0, x.data() + pos, defaultBlock);
                q->processBlock (buffer, midi);
            }
            const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const auto share = elapsed / seconds * 100.0;
            cpu += (cpu.empty() ? "" : ", ") + std::string (notches == 0 ? "detection, no notches " : std::to_string (notches) + (notches == 1 ? " notch " : " notches ")) + fmt (share, 2) + " %";
            check (seconds / elapsed > 10.0, "runs at more than 10x real time (" + fmt (seconds / elapsed, 0) + "x)");
        }
        note ("CPU (one core, 48 kHz stereo, 64-sample blocks, share of real time): " + cpu);
    }

    //==============================================================================
    juce::Component* findById (juce::Component& root, const juce::String& cid)
    {
        for (auto* child : root.getChildren())
        {
            if (child->getComponentID() == cid) return child;
            if (auto* found = findById (*child, cid)) return found;
        }
        return nullptr;
    }

    bool isControl (juce::Component& c)
    {
        return dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr || dynamic_cast<ui::SegmentedToggle*> (&c) != nullptr
            || dynamic_cast<juce::TextEditor*> (&c) != nullptr;
    }

    template <typename Fn>
    void forEachComponent (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren()) { fn (*child); forEachComponent (*child, fn); }
    }

    template <typename Fn>
    void forEachShowing (juce::Component& root, Fn&& fn)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isShowing()) continue;
            fn (*child);
            forEachShowing (*child, fn);
        }
    }

    void saveSnapshot (juce::Component& c, const juce::String& suffix)
    {
        const auto* prefix = std::getenv ("DOOBLRFEEDBACK_EDITOR_SNAPSHOT");
        if (prefix == nullptr || *prefix == 0) return;
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        const juce::File file (juce::String (prefix) + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            note ("snapshot: " + file.getFullPathName().toStdString());
        else
            check (false, "could not write snapshot " + file.getFullPathName().toStdString());
    }

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

    struct PeerMouse
    {
        juce::ComponentPeer& peer;
        juce::int64 time = 1000000;
        void event (juce::Point<float> pos, juce::ModifierKeys mods)
        {
            time += 400;
            peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods, juce::MouseInputSource::defaultPressure,
                                   juce::MouseInputSource::defaultOrientation, time);
        }
        void click (juce::Point<float> pos, bool right = false)
        {
            const auto button = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
            event (pos, juce::ModifierKeys());
            event (pos, juce::ModifierKeys (button));
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

    /** A room scene: notches at 315 / 1250 (fixed) / 2510 / 6300 Hz over a live spectrum. */
    void addScene (Processor& p)
    {
        auto x = pink ((size_t) defaultRate, -30.0, 61);
        addSine (x, defaultRate, 2510.0, 0.05f);
        addSine (x, defaultRate, 315.0, 0.02f);
        set (p, id::detect, 0.0f);
        render (p, mono (x));
        auto& e = p.getEngine();
        e.addNotch (315.0f, 4.5f, 14.0f, false, 2);
        e.addNotch (1250.0f, 9.0f, 20.0f, true, 1);
        e.addNotch (2510.0f, 12.5f, 24.0f, false, 5);
        e.addNotch (6300.0f, 6.0f, 18.0f, false, 1);
        render (p, mono (x)); // the cuts ramp in
        set (p, id::detect, 1.0f);
        p.setNotes ("Main hall, Friday soundcheck.\nWedge 2 rings near 2.5 kHz with the lead vocal mic; 1250 Hz is the stage-left monitor, locked.");
    }

    struct EditorFixture
    {
        std::unique_ptr<Processor> processor;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor = nullptr;
        double clock = 0.0; // the graph's clock (ms): driven by the test, 60 frames a second
        explicit EditorFixture (bool scene = true)
        {
            processor = makeProcessor();
            if (scene) addScene (*processor);
            else render (*processor, mono (pink ((size_t) defaultRate, -30.0, 62)));
            base.reset (processor->createEditor());
            editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "the editor is a DooblrFeedbackEditor");
            clock = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            if (editor != nullptr) settle();
        }
        void tick (int frames = 1)
        {
            auto& g = editor->getGraph();
            for (int i = 0; i < frames; ++i)
            {
                clock += 1000.0 / 60.0;
                g.setClockForTesting (clock);
                g.poll();
            }
        }
        void settle()
        {
            auto& g = editor->getGraph();
            for (int i = 0; i < 60; ++i) { tick(); g.advanceDisplay (1.0 / 60.0); }
            g.snapDisplay();
            g.refreshNotches();
            editor->getNotchTable().refresh();
            editor->refresh();
        }
        ~EditorFixture() { base.reset(); }
    };

    const std::vector<juce::Point<int>> editorSizes { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
    const std::vector<Editor::View> views { Editor::View::notches, Editor::View::notes, Editor::View::advanced };

    void testEditor()
    {
        EditorFixture f;
        auto* editor = f.editor;
        if (editor == nullptr) return;
        auto& p = *f.processor;
        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080 x 660");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600 && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400, "resize limits 940 x 600 .. 2400 x 1400");
        else
            check (false, "the editor is resizable with limits");
        auto* version = dynamic_cast<juce::Label*> (findById (*editor, "version"));
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString), "the header shows the version");
        if (auto* hold = dynamic_cast<juce::TextButton*> (editor->findControl (id::liveNotches)))
            check (hold->getButtonText() == "HOLD" && ! hold->getToggleState(), "HOLD is the liveNotches control and starts up");
        else
            check (false, "HOLD is the liveNotches control");
        auto* latency = dynamic_cast<juce::Label*> (findById (*editor, "latency"));
        check (latency != nullptr && latency->getText().contains ("0"), "the header shows the latency (" + (latency != nullptr ? latency->getText().toStdString() : std::string()) + ")");

        for (auto* q : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q))
            {
                const auto pid = rp->getParameterID();
                int count = 0;
                forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == pid && isControl (c)) ++count; });
                check (count == 1, "exactly one control for " + pid.toStdString() + " (" + std::to_string (count) + ")");
            }

        for (auto size : editorSizes)
            for (auto v : views)
            {
                editor->setSize (size.x, size.y);
                editor->setView (v);
                f.settle();
                const auto label = std::to_string (size.x) + "x" + std::to_string (size.y) + " view " + std::to_string ((int) v);
                std::vector<juce::Component*> shown;
                forEachShowing (*editor, [&] (juce::Component& c)
                {
                    if (! isControl (c)) return;
                    const auto r = editor->getLocalArea (&c, c.getLocalBounds());
                    check (editor->getLocalBounds().contains (r) && r.getWidth() > 4 && r.getHeight() > 4, label + ": " + c.getComponentID().toStdString() + " fits");
                    shown.push_back (&c);
                });
                for (size_t i = 0; i < shown.size(); ++i)
                    for (size_t j = i + 1; j < shown.size(); ++j)
                    {
                        if (shown[i]->isParentOf (shown[j]) || shown[j]->isParentOf (shown[i])) continue;
                        const auto a = editor->getLocalArea (shown[i], shown[i]->getLocalBounds());
                        const auto b = editor->getLocalArea (shown[j], shown[j]->getLocalBounds());
                        check (! a.intersects (b), label + ": " + shown[i]->getComponentID().toStdString() + " and " + shown[j]->getComponentID().toStdString() + " do not overlap");
                    }
                if (v == Editor::View::notches)
                {
                    auto& g = editor->getGraph();
                    check (g.getMarkers().size() == 4, label + ": four markers (" + std::to_string (g.getMarkers().size()) + ")");
                    const auto& m = g.getMarkers();
                    for (size_t i = 0; i < m.size(); ++i)
                    {
                        check (g.getPlotArea().expanded (2.0f).contains (m[i].handle), label + ": marker " + std::to_string (m[i].number) + " sits on the plot");
                        for (size_t j = i + 1; j < m.size(); ++j)
                            if (! m[i].compact && ! m[j].compact)
                                check (! m[i].chip.intersects (m[j].chip), label + ": marker chips do not overlap");
                    }
                    check (! g.getNotchCurve().isEmpty() && g.hasSpectrum(), label + ": the notch curve and spectrum are drawn");
                    check (editor->getNotchTable().getNumRows() == 4, label + ": four table rows");
                    saveSnapshot (*editor, "-" + juce::String (size.x) + "x" + juce::String (size.y));
                }
            }
        editor->setSize (1080, 660);
        editor->setView (Editor::View::notes);
        f.settle();
        saveSnapshot (*editor, "-1080x660-notes");
        editor->setView (Editor::View::advanced);
        f.settle();
        saveSnapshot (*editor, "-1080x660-advanced");
        editor->setView (Editor::View::notches);

        // JUCE posts a TextEditor's change message asynchronously; with no message loop here the
        // editor's timer tick (refresh) is what picks the text up, as it also would in a host.
        editor->getNotesEditor().setText ("Typed note", juce::sendNotification);
        editor->refresh();
        check (p.getNotes() == "Typed note", "typing in the notes editor stores the notes");
        p.setNotes ("From a preset");
        editor->refresh();
        check (editor->getNotesEditor().getText() == "From a preset", "notes set by the processor show in the editor");

        EditorFixture empty (false);
        if (empty.editor != nullptr)
        {
            check (empty.editor->getGraph().getMarkers().empty(), "no notches: no markers");
            saveSnapshot (*empty.editor, "-1080x660-empty");
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
            f.settle();
            TestWindow window (*editor);
            auto* peer = window.getPeer();
            if (peer == nullptr) { check (false, "the test window has a peer"); return; }
            PeerMouse mouse { *peer };
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            const auto fail = [&] (const juce::String& what) { check (false, label + ": " + what.toStdString()); };
            const auto centreOf = [&] (juce::Component& c) { return editor->getLocalPoint (&c, c.getLocalBounds().getCentre()).toFloat(); };
            const auto hits = [&] (juce::Component& c)
            {
                auto* hit = editor->getComponentAt (centreOf (c).roundToInt());
                return hit == &c || c.isParentOf (hit);
            };

            // View tabs.
            int tabsOk = 0;
            for (auto v : views)
            {
                auto& tab = editor->getViewTab (v);
                if (! hits (tab)) { fail (tab.getComponentID() + " is covered"); continue; }
                mouse.click (centreOf (tab));
                if (editor->getView() == v) ++tabsOk; else fail (tab.getComponentID() + " does not switch the view");
            }
            check (tabsOk == 3, label + ": the three view tabs switch views");

            // Every parameter control in every view.
            int controls = 0, ok = 0;
            std::set<juce::String> seen;
            for (auto v : views)
            {
                mouse.click (centreOf (editor->getViewTab (v)));
                f.settle();
                std::vector<juce::Component*> list;
                forEachShowing (*editor, [&] (juce::Component& c) { if (isControl (c)) list.push_back (&c); });
                for (auto* c : list)
                {
                    const auto name = c->getComponentID();
                    if (seen.count (name) > 0 || name.startsWith ("notchLock_") || name.startsWith ("notchClear_")) continue;
                    seen.insert (name);
                    ++controls;
                    if (! hits (*c))
                    {
                        auto* hit = editor->getComponentAt (centreOf (*c).roundToInt());
                        fail (name + " is covered by " + (hit != nullptr ? hit->getComponentID() : juce::String ("nothing")));
                        continue;
                    }
                    auto* parameter = p.getState().getParameter (name);
                    if (auto* seg = dynamic_cast<ui::SegmentedToggle*> (c))
                    {
                        const auto original = seg->getSelectedIndex();
                        bool good = true;
                        for (int i = 0; i < seg->getNumSegments(); ++i)
                        {
                            mouse.click (editor->getLocalPoint (c, seg->getSegmentBounds (i).getCentre()).toFloat());
                            good = good && seg->getSelectedIndex() == i && parameter != nullptr && juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())) == i;
                        }
                        mouse.click (editor->getLocalPoint (c, seg->getSegmentBounds (original).getCentre()).toFloat());
                        if (good) ++ok; else fail (name + " segments do not select");
                    }
                    else if (auto* button = dynamic_cast<juce::Button*> (c); button != nullptr && parameter != nullptr)
                    {
                        const auto before = parameter->getValue() > 0.5f;
                        mouse.click (centreOf (*c));
                        const auto after = parameter->getValue() > 0.5f;
                        mouse.click (centreOf (*c));
                        if (after != before && (parameter->getValue() > 0.5f) == before) ++ok; else fail (name + " does not toggle");
                        editor->refresh();
                    }
                    else if (auto* slider = dynamic_cast<juce::Slider*> (c); slider != nullptr && parameter != nullptr)
                    {
                        const auto before = parameter->getValue();
                        const auto from = slider->isRotary() ? editor->getLocalPoint (c, juce::Point<int> (c->getWidth() / 2, (c->getHeight() - 16) / 2)).toFloat()
                                                             : editor->getLocalPoint (c, slider->getLookAndFeel().getSliderLayout (*slider).sliderBounds.getCentre()).toFloat();
                        const auto step = slider->isRotary() || slider->getSliderStyle() == juce::Slider::LinearVertical ? juce::Point<float> (0.0f, -40.0f) : juce::Point<float> (40.0f, 0.0f);
                        mouse.drag (from, from + step);
                        if (std::abs (parameter->getValue() - before) < 1.0e-4f) mouse.drag (from, from - step);
                        if (std::abs (parameter->getValue() - before) > 1.0e-4f) ++ok; else fail (name + " does not move when dragged");
                        parameter->setValueNotifyingHost (before);
                    }
                    else if (auto* combo = dynamic_cast<juce::ComboBox*> (c))
                    {
                        if (combo->getNumItems() > 0) ++ok; else fail (name + " has no items");
                    }
                    else if (auto* text = dynamic_cast<juce::TextEditor*> (c))
                    {
                        mouse.click (centreOf (*c));
                        if (! text->hasKeyboardFocus (true)) { fail ("clicking the notes does not focus them"); continue; }
                        text->insertTextAtCaret ("+");
                        editor->refresh();
                        if (p.getNotes().contains ("+")) ++ok; else fail ("typing after a click does not reach the notes");
                    }
                    else
                        ++ok; // plain buttons (toolbar) are exercised below
                }
            }
            check (controls > 15 && ok == controls, label + ": every control is hit-testable and works (" + std::to_string (ok) + " / " + std::to_string (controls) + ")");
            for (auto pid : { id::detect, id::ringOut, id::mode, id::sensitivity, id::sidechain, id::rangeLow, id::rangeHigh, id::maxCut, id::notchQ, id::bellQ, id::liveNotches, id::release, id::recoverSpeed, id::output, id::bypass })
                check (seen.count (juce::String (pid)) == 1, label + ": the " + std::string (pid) + " control was reached with the mouse");

            // Graph marker buttons.
            mouse.click (centreOf (editor->getViewTab (Editor::View::notches)));
            f.settle();
            auto& g = editor->getGraph();
            const auto toEditor = [&] (juce::Point<float> pt) { return editor->getLocalPoint (&g, pt.roundToInt()).toFloat(); };
            const auto findMarker = [&] (bool wantCompact) -> const ui::FeedbackGraph::Marker*
            {
                for (const auto& m : g.getMarkers()) if (m.compact == wantCompact) return &m;
                return nullptr;
            };
            const auto notchOf = [&] (int nid) -> dsp::Notch
            {
                for (const auto& n : p.getNotches()) if (n.active && n.id == nid) return n;
                return {};
            };
            if (findMarker (false) == nullptr)
                if (const auto* dot = findMarker (true))
                {
                    // Idle labels close to numbered dots: a click on one selects it and opens its label.
                    const auto nid = dot->id;
                    mouse.click (toEditor (dot->chip.getCentre()));
                    f.settle();
                    check (g.getSelectedNotch() == nid, label + ": clicking a numbered dot selects its notch");
                }
            if (const auto* m = findMarker (false))
            {
                const auto nid = m->id;
                const auto wasFixed = notchOf (nid).fixed;
                mouse.click (toEditor (m->lockButton.getCentre()));
                f.settle();
                check (notchOf (nid).fixed != wasFixed, label + ": the marker's lock button toggles fixed");
                if (const auto* m2 = g.markerFor (nid))
                {
                    mouse.click (toEditor (m2->chip.getCentre()));
                    check (g.getSelectedNotch() == nid, label + ": clicking a marker selects its notch");
                }
                if (const auto* m3 = g.markerFor (nid))
                    mouse.click (toEditor (m3->clearButton.getCentre()));
                f.settle();
                check (! notchOf (nid).active, label + ": the marker's x clears its notch");
            }
            else
                fail ("no full marker chip to click");
            if (! g.getMarkers().empty())
            {
                const auto nid = g.getMarkers().front().id;
                mouse.click (toEditor (g.getMarkers().front().handle), true);
                f.settle();
                check (! notchOf (nid).active, label + ": right-click on a marker handle clears it");
            }

            // Table buttons.
            f.settle();
            auto& table = editor->getNotchTable();
            table.getTable().updateContent();
            const auto rowsBefore = table.getNumRows();
            if (rowsBefore > 0)
            {
                const auto nid = table.getRows().front().id;
                const auto wasFixed = notchOf (nid).fixed;
                if (auto* lock = findById (*editor, "notchLock_" + juce::String (nid)); lock != nullptr && lock->isShowing() && hits (*lock))
                {
                    mouse.click (centreOf (*lock));
                    check (notchOf (nid).fixed != wasFixed, label + ": the table's lock button toggles fixed");
                }
                else
                    fail ("the table lock button for notch " + juce::String (nid) + " is not clickable");
                f.settle();
                table.getTable().updateContent();
                if (auto* clr = findById (*editor, "notchClear_" + juce::String (nid)); clr != nullptr && clr->isShowing() && hits (*clr))
                {
                    mouse.click (centreOf (*clr));
                    check (! notchOf (nid).active, label + ": the table's clear button clears the notch");
                }
                else
                    fail ("the table clear button for notch " + juce::String (nid) + " is not clickable");
            }
            else
                fail ("the table has rows to click");

            // Clear all.
            f.settle();
            auto& clearAll = editor->getClearAllButton();
            if (hits (clearAll)) mouse.click (centreOf (clearAll)); else fail ("clearAll is covered");
            check (activeCount (p.getNotches()) == 0, label + ": Clear all clears every notch");

            // Presets, A / B, undo / redo.
            if (auto* prev = findById (*editor, "presetPrev"); prev != nullptr)
                if (auto* next = findById (*editor, "presetNext"); next != nullptr)
                {
                    const auto start = p.getCurrentProgram();
                    mouse.click (centreOf (*next));
                    const auto afterNext = p.getCurrentProgram();
                    mouse.click (centreOf (*prev));
                    check (afterNext != start && p.getCurrentProgram() == start, label + ": preset next / previous step the factory list");
                }
            if (auto* a = findById (*editor, "compareA"); a != nullptr)
                if (auto* b = findById (*editor, "compareB"); b != nullptr)
                {
                    mouse.click (centreOf (*b));
                    const auto onB = p.getCompareSlot();
                    mouse.click (centreOf (*a));
                    check (onB == 1 && p.getCompareSlot() == 0, label + ": A / B buttons switch slots");
                }
            if (auto* undo = findById (*editor, "undo"); undo != nullptr)
                if (auto* redo = findById (*editor, "redo"); redo != nullptr)
                {
                    p.getUndoHistory().clear();
                    auto* s = param (p, id::sensitivity);
                    const auto before = s->getValue();
                    s->beginChangeGesture(); s->setValueNotifyingHost (before > 0.5f ? 0.2f : 0.8f); s->endChangeGesture();
                    const auto changed = s->getValue();
                    editor->refresh();
                    mouse.click (centreOf (*undo));
                    const auto undone = s->getValue();
                    editor->refresh();
                    mouse.click (centreOf (*redo));
                    check (std::abs (undone - before) < 1.0e-5f && std::abs (s->getValue() - changed) < 1.0e-5f, label + ": undo / redo buttons");
                }
        }
    }

    //==============================================================================
    // Motion (0.1.2): the drawn notch curve and handles glide to the bank instead of snapping,
    // labels move and fade slowly, and a frame of the graph paints well inside a 60 Hz frame.
    void testMotionAndPaint()
    {
        using G = ui::FeedbackGraph;
        check (G::moveMs >= 400.0 && G::fadeMs >= 450.0 && G::valueRefreshMs >= 300.0, "slower label motion (move "
               + fmt (G::moveMs, 0) + " ms, fade " + fmt (G::fadeMs, 0) + " ms, numbers every " + fmt (G::valueRefreshMs, 0) + " ms)");

        // A new 18 dB notch grows in on the display; a cleared one shrinks back out.
        {
            EditorFixture f (false);
            if (f.editor == nullptr) return;
            auto& g = f.editor->getGraph();
            auto& e = f.processor->getEngine();
            const auto nid = e.addNotch (4000.0f, 18.0f, 20.0f, false);
            const auto drawnCut = [&] { for (const auto& n : g.getDrawnNotches()) if (n.active && n.id == nid) return n.cutDb; return -1.0f; };
            const auto frameK = 1.0 - std::exp (-(1.0 / 60.0) / G::notchEaseSec);
            float prev = 0.0f, worstStep = 0.0f;
            bool monotonic = true, handleFollows = true;
            float at100 = 0.0f, at500 = 0.0f;
            for (int i = 1; i <= 150; ++i)
            {
                f.tick();
                const auto c = drawnCut();
                monotonic = monotonic && c >= prev - 1.0e-4f && c <= 18.0f + 1.0e-4f;
                worstStep = std::max (worstStep, c - prev);
                if (const auto* m = g.markerFor (nid))
                    handleFollows = handleFollows && std::abs (m->handle.y - g.yForCut (c)) < 0.01f;
                prev = c;
                if (i == 6) at100 = c;
                if (i == 30) at500 = c;
            }
            note ("drawn depth of a new 18 dB notch: " + db (at100) + " at 0.1 s, " + db (at500) + " at 0.5 s, " + db (prev)
                  + " at 2.5 s; largest per-frame step " + db (worstStep));
            check (at100 > 3.0f && at100 < 9.0f && at500 > 12.0f && std::abs (prev - 18.0f) < 1.0e-3f, "a new notch's depth glides in on the display (no snap)");
            check (monotonic && worstStep <= (float) (18.0 * frameK) + 0.01f, "the glide is smooth and never overshoots");
            check (handleFollows, "the handle follows the drawn depth");
            e.clearNotch (nid);
            f.tick (6);
            const auto shrinking = drawnCut();
            f.tick (150);
            check (shrinking > 1.0f && shrinking < 17.0f && drawnCut() < 0.0f, "a cleared notch's curve shrinks away (" + db (shrinking) + " after 0.1 s)");
        }

        // Paint time: 24 notches, spectrum on, labels open, at the default and the largest size,
        // 1x and 2x (Retina) pixels. One frame = poll (animation) + a full repaint of the graph.
        {
            EditorFixture f;
            if (f.editor == nullptr) return;
            auto& e = f.processor->getEngine();
            e.clearAll();
            for (int i = 0; i < dsp::maxNotches; ++i)
                e.addNotch (60.0f * std::pow (2.0f, (float) i * 0.35f), 3.0f + (float) (i % 8) * 3.0f, 12.0f + (float) (i % 3) * 8.0f, i % 5 == 0, 1 + i % 4);
            render (*f.processor, mono (pink ((size_t) defaultRate, -30.0, 63)));
            f.settle();
            auto& g = f.editor->getGraph();
            std::string report;
            double worstAvg = 0.0;
            for (auto size : { juce::Point<int> { 1080, 660 }, juce::Point<int> { 2400, 1400 } })
            {
                f.editor->setSize (size.x, size.y);
                f.settle();
                for (float scale : { 1.0f, 2.0f })
                {
                    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) g.getWidth() * scale), juce::roundToInt ((float) g.getHeight() * scale), true);
                    juce::Graphics gr (image);
                    gr.addTransform (juce::AffineTransform::scale (scale));
                    g.paintEntireComponent (gr, true);   // warm up (the cached background layer)
                    constexpr int frames = 120;
                    double total = 0.0, worst = 0.0;
                    auto& eng = f.processor->getEngine();
                    for (int i = 0; i < frames; ++i)
                    {
                        if (i % 20 == 0) { eng.clearNotch (eng.getNotches()[(size_t) (i / 20)].id); eng.addNotch (900.0f + (float) i * 40.0f, 15.0f, 20.0f, false); }
                        const auto t0 = juce::Time::getMillisecondCounterHiRes();
                        f.tick();
                        g.paintEntireComponent (gr, true);
                        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
                        total += ms;
                        worst = std::max (worst, ms);
                    }
                    const auto avg = total / frames;
                    if (scale == 1.0f) worstAvg = std::max (worstAvg, avg);
                    report += "  " + std::to_string (size.x) + "x" + std::to_string (size.y) + " @" + fmt (scale, 0) + "x: " + fmt (avg, 2) + " ms avg, " + fmt (worst, 2) + " ms worst;";
                }
            }
            note ("graph frame (poll + full repaint, 24 notches, labels open, spectrum, software renderer):" + report);
            check (worstAvg < 4.0, "a graph frame paints in under 4 ms on average at 1x (" + fmt (worstAvg, 2) + " ms)");
        }
    }

    void testAnalyzerSpeed()
    {
        EditorFixture f (false);
        if (f.editor == nullptr) return;
        auto& box = f.editor->getAnalyzerSpeedBox();
        check (box.getNumItems() == ui::numAnalyzerSpeeds, "Analyzer Speed lists " + std::to_string (ui::numAnalyzerSpeeds) + " speeds");
        check (f.processor->getAnalyzerSpeed() == (int) ui::defaultAnalyzerSpeed && ui::defaultAnalyzerSpeed == ui::AnalyzerSpeed::slow,
               "default Analyzer Speed is Slow (0.1.2)");
        box.setSelectedItemIndex (1, juce::sendNotificationSync);
        check (f.processor->getAnalyzerSpeed() == 1, "choosing Medium sets the processor's speed");
        box.setSelectedItemIndex (0, juce::sendNotificationSync);
        check (f.processor->getAnalyzerSpeed() == 0, "choosing Slow sets the processor's speed");
    }

    //==============================================================================
    // High-frequency feedback (0.1.1). A more realistic loop than runLoop: programme and room noise
    // into the mic, the plugin, a PA limiter (no waveshaping, so no aliasing that a real analogue
    // PA would not make), a resonant path at the feedback frequency plus damped broadband
    // reflections back into the mic.

    /** One-pole low-pass. */
    struct OnePole
    {
        double a = 0.0, z = 0.0;
        OnePole (double rate, double hz) : a (std::exp (-twoPi * hz / rate)) {}
        double process (double x) noexcept { z = x + a * (z - x); return z; }
    };

    std::vector<float> roomNoise (size_t n, double rate, double rmsDbfs, int seed)
    {
        // Pink with an extra roll-off above 6 kHz (HVAC, audience): little HF energy, like a room.
        auto v = pink (n, 0.0, seed);
        OnePole lp (rate, 6000.0);
        for (auto& x : v) x = (float) lp.process (x);
        const auto now = rmsDb (v, 0, v.size());
        const auto g = (float) std::pow (10.0, (rmsDbfs - now) / 20.0);
        for (auto& x : v) x *= g;
        return v;
    }

    enum class Programme { roomOnly, speech, music };

    struct RoomSpec
    {
        double freq = 8000.0;
        double gain = 1.1;          // loop gain at the resonance
        double seconds = 6.0;
        double q = 8.0;
        double delayMs = 10.0;
        double noiseDb = -50.0;     // room noise RMS into the mic
        Programme programme = Programme::roomOnly;
        double programmeDb = -30.0; // talker / instrument RMS into the mic
        double driftPct = 0.0;      // the resonance glides linearly by this much over the run
        double warblePct = 0.0;     // ... and/or wobbles sinusoidally by +- this much
        double warbleHz = 0.7;
        double freq2 = 0.0;         // a second resonance (same gain and Q), 0: none
        double loopOffSec = -1.0;   // >= 0: the loop is broken from then on (the mic is moved away)
    };

    struct RoomRun
    {
        std::vector<float> mic;
        double firstNotchSec = -1.0; // first notch within 3 % of the resonance
        dsp::NotchArray notches {};
        dsp::FeedbackEngine::WatchStats watch {};
        int maxConcurrent = 0;      // most notches (not fading out) at the resonances at once
        std::set<int> ids;          // every notch id that ever sat at a resonance
        std::vector<float> out;     // the plugin's output (before the PA)
    };

    std::vector<float> brightChord (size_t n, double rate, float amplitude, int seed);

    RoomRun runRoom (Processor* p, const RoomSpec& s, double rate = defaultRate, int block = defaultBlock, bool watch = false)
    {
        const auto n = (size_t) (s.seconds * rate);
        auto src = roomNoise (n, rate, s.noiseDb, 91);
        if (s.programme != Programme::roomOnly)
        {
            auto prog = s.programme == Programme::speech ? speech (n, rate, 92) : brightChord (n, rate, 0.1f, 93);
            const auto g = (float) std::pow (10.0, (s.programmeDb - rmsDb (prog, 0, prog.size())) / 20.0);
            for (size_t i = 0; i < n; ++i) src[i] += g * prog[i];
        }
        BandPass resonance (rate, s.freq, s.q), resonance2 (rate, s.freq2 > 0.0 ? s.freq2 : 1000.0, s.q);
        const auto moving = s.driftPct != 0.0 || s.warblePct != 0.0;
        OnePole air (rate, 3000.0);
        const auto delay = std::max ((size_t) (s.delayMs * 0.001 * rate), (size_t) block);
        const size_t taps[] = { (size_t) (0.0137 * rate), (size_t) (0.0191 * rate), (size_t) (0.0273 * rate) };
        const double tapGain[] = { 0.06, -0.04, 0.03 };
        const size_t ringSize = 1 << 15, ringMask = ringSize - 1;
        std::vector<float> speaker (ringSize, 0.0f);
        double env = 0.0;
        const auto limit = std::pow (10.0, -3.0 / 20.0);
        const auto att = 1.0 - std::exp (-1.0 / (0.0005 * rate)), rel = 1.0 - std::exp (-1.0 / (0.08 * rate));
        RoomRun run;
        run.mic.assign (n, 0.0f);
        run.out.assign (n, 0.0f);
        if (p != nullptr && watch) p->getEngine().setWatch ((float) s.freq, (float) (0.03 * s.freq));
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos < n; pos += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, n - pos);
            buffer.setSize (2, len, false, false, true);
            for (int i = 0; i < len; ++i)
            {
                const auto t = pos + (size_t) i;
                const auto at = [&] (size_t d) { return t >= d ? (double) speaker[(t - d) & ringMask] : 0.0; };
                auto reflections = 0.0;
                for (int k = 0; k < 3; ++k) reflections += tapGain[k] * at (taps[k]);
                if (moving && t % 16 == 0)
                {
                    const auto sec = (double) t / rate;
                    const auto f = s.freq * (1.0 + 0.01 * (s.driftPct * sec / s.seconds + s.warblePct * std::sin (twoPi * s.warbleHz * sec)));
                    resonance.retune (rate, f, s.q);
                }
                const auto loopGain = s.loopOffSec >= 0.0 && (double) t / rate >= s.loopOffSec ? 0.0 : s.gain;
                auto x = (double) src[t] + loopGain * resonance.process (at (delay)) + air.process (reflections);
                if (s.freq2 > 0.0) x += loopGain * resonance2.process (at (delay));
                run.mic[t] = (float) x;
                buffer.setSample (0, i, (float) x);
                buffer.setSample (1, i, (float) x);
            }
            if (p != nullptr)
                p->processBlock (buffer, midi);
            for (int i = 0; i < len; ++i)
            {
                // The PA: a fast peak limiter at -3 dBFS (gain riding, no waveshaping).
                const auto y = (double) buffer.getSample (0, i);
                run.out[pos + (size_t) i] = (float) y;
                const auto a = std::abs (y);
                env += (a - env) * (a > env ? att : rel);
                const auto g = env > limit ? limit / env : 1.0;
                speaker[(pos + (size_t) i) & ringMask] = (float) (y * g);
            }
            if (p != nullptr && watch)
            {
                int at = 0;
                for (const auto& nn : p->getNotches())
                {
                    const auto near = [&] (double f) { return f > 0.0 && std::abs (nn.freq / (float) f - 1.0f) < 0.08f; };
                    if (nn.active && (near (s.freq) || near (s.freq2)))
                    {
                        run.ids.insert (nn.id);
                        if (! nn.merging) ++at;
                    }
                }
                run.maxConcurrent = std::max (run.maxConcurrent, at);
            }
            if (p != nullptr && run.firstNotchSec < 0.0)
                for (const auto& nn : p->getNotches())
                    if (nn.active && std::abs (nn.freq / (float) s.freq - 1.0f) < 0.03f)
                        run.firstNotchSec = (double) (pos + (size_t) len) / rate;
        }
        if (p != nullptr)
        {
            run.notches = p->getNotches();
            run.watch = p->getEngine().getWatch();
        }
        return run;
    }

    /** Level (dB) of a narrow band (Q 30) around `freq`, in 10 ms windows. */
    std::vector<double> bandEnvelopeDb (const std::vector<float>& x, double rate, double freq)
    {
        BandPass a (rate, freq, 30.0), b (rate, freq, 30.0);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = (float) b.process (a.process (x[i]));
        return envelopeDb (y, rate);
    }

    /** The strongest frequency of `x` near `around` (+-5 %), from a Hann FFT of [endSec - 0.25 s, endSec]. */
    double howlFrequency (const std::vector<float>& x, double rate, double around, double endSec)
    {
        const int order = 17, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        const auto end = std::min (x.size(), (size_t) (endSec * rate));
        const auto len = std::min ((size_t) (0.25 * rate), end);
        const auto start = end - len;
        for (size_t i = 0; i < len; ++i)
            data[i] = x[start + i] * (float) (0.5 - 0.5 * std::cos (twoPi * (double) i / (double) len));
        fft.performFrequencyOnlyForwardTransform (data.data(), true);
        const auto binHz = rate / size;
        size_t best = (size_t) (around * 0.95 / binHz);
        for (auto k = best; k <= (size_t) (around * 1.05 / binHz); ++k) if (data[k] > data[best]) best = k;
        const double a = data[best - 1], m = data[best], c = data[best + 1];
        const auto d = 0.5 * (a - c) / (a - 2.0 * m + c);
        return ((double) best + (std::isfinite (d) ? d : 0.0)) * binHz;
    }

    /** The real attenuation of the bank at `freq`: a sine through a copy of the notches, frozen. */
    double measuredCutDb (const dsp::NotchArray& notches, double rate, double freq)
    {
        auto q = makeProcessor (rate);
        set (*q, id::detect, 0.0f);
        q->getEngine().setNotches (notches);
        Stereo sine ((size_t) (rate * 0.6));
        addSine (sine.l, rate, freq, 0.1f);
        sine.r = sine.l;
        const auto out = render (*q, sine);
        const auto half = (size_t) (rate * 0.3);
        return rmsDb (out.l, half, half) - rmsDb (sine.l, half, half);
    }

    struct HfResult { double unprotected6dB = -1.0, firstNotch = -1.0, steadyOverBase = 0.0, peakOverBase = 0.0, howlHz = 0.0, cutAtHowl = 0.0; bool caught = false; std::string notches; dsp::FeedbackEngine::WatchStats watch; };

    HfResult hfCase (const RoomSpec& spec, double rate, int block, bool aggressive = false)
    {
        HfResult r;
        auto quiet = spec; quiet.gain = 0.0;
        const auto base = runRoom (nullptr, quiet, rate, block);
        const auto runaway = runRoom (nullptr, spec, rate, block);
        const auto baseEnv = bandEnvelopeDb (base.mic, rate, spec.freq);
        auto baseLevel = 0.0;
        for (size_t i = 50; i < baseEnv.size(); ++i) baseLevel += baseEnv[i];
        baseLevel /= (double) std::max<size_t> (1, baseEnv.size() - 50);
        r.unprotected6dB = firstTimeAbove (bandEnvelopeDb (runaway.mic, rate, spec.freq), baseLevel + 6.0);

        auto p = makeProcessor (rate, block);
        if (aggressive) set (*p, id::mode, 1.0f);
        const auto run = runRoom (p.get(), spec, rate, block, true);
        const auto env = bandEnvelopeDb (run.mic, rate, spec.freq);
        const auto last = env.size() > 100 ? env.size() - 100 : 0;
        auto steady = 0.0;
        for (auto i = last; i < env.size(); ++i) steady += env[i];
        steady /= (double) std::max<size_t> (1, env.size() - last);
        r.firstNotch = run.firstNotchSec;
        // Until the first notch the protected loop is bit-identical to the unprotected one, so the
        // howl the detector saw is the runaway's in the 250 ms before the notch.
        r.howlHz = howlFrequency (runaway.mic, rate, spec.freq, run.firstNotchSec > 0.0 ? run.firstNotchSec : spec.seconds);
        r.steadyOverBase = steady - baseLevel;
        r.peakOverBase = maxOf (env, 10) - baseLevel;
        r.caught = run.firstNotchSec > 0.0 && r.steadyOverBase < 3.0;
        r.notches = notchList (run.notches);
        r.cutAtHowl = activeCount (run.notches) > 0 ? measuredCutDb (run.notches, rate, r.howlHz) : 0.0;
        r.watch = run.watch;
        return r;
    }

    std::string watchText (const dsp::FeedbackEngine::WatchStats& w)
    {
        return "hops " + std::to_string (w.hops) + ": covered " + std::to_string (w.covered) + ", out of range " + std::to_string (w.outOfRange)
             + ", below floor " + std::to_string (w.belowFloor) + ", low prominence " + std::to_string (w.lowProminence) + ", candidate " + std::to_string (w.candidate)
             + "; confirmed " + std::to_string (w.confirmed) + " (2f " + std::to_string (w.musicalOvertone) + ", series " + std::to_string (w.musicalAround)
             + ", base " + std::to_string (w.musicalBase) + ", engaged " + std::to_string (w.engaged) + "); max prominence " + fmt (w.maxProminence, 1) + " dB, max level " + fmt (w.maxLevel, 1) + " dBFS";
    }

    std::string hfLine (const std::string& label, const HfResult& r)
    {
        return label + ": unprotected +6 dB at " + (r.unprotected6dB < 0 ? std::string ("never") : fmt (r.unprotected6dB * 1000.0, 0) + " ms")
             + (r.caught ? ", CAUGHT, notch at " + fmt (r.firstNotch * 1000.0, 0) + " ms" : std::string (", NOT caught") + (r.firstNotch > 0 ? " (notch at " + fmt (r.firstNotch * 1000.0, 0) + " ms)" : ""))
             + ", peak +" + fmt (r.peakOverBase, 1) + " dB, steady " + fmt (r.steadyOverBase, 1) + " dB re no-loop; howl " + fmt (r.howlHz, 0) + " Hz, the bank cuts it " + fmt (r.cutAtHowl, 1) + " dB; " + r.notches;
    }

    void testHighFrequencyFeedback()
    {
        if (juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_HF_TRACE", {}).isNotEmpty())
        {
            const auto f = juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_HF_TRACE", {}).getDoubleValue();
            const auto g = juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_HF_GAIN", "1.3").getDoubleValue();
            const auto blk = juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_HF_BLOCK", "64").getIntValue();
            RoomSpec spec; spec.freq = f; spec.gain = g; spec.seconds = 1.5;
            auto p = makeProcessor (defaultRate, blk);
            const auto run = runRoom (p.get(), spec, defaultRate, blk, true);
            for (int h = 0; h < 256; h += 2)
                note ("hop " + std::to_string (h) + " t " + fmt ((h + 1) * 256.0 / defaultRate * 1000.0, 0) + " ms: " + fmt (run.watch.traceFreq[(size_t) h], 0) + " Hz level " + fmt (run.watch.traceLevel[(size_t) h], 1) + " prom " + fmt (run.watch.traceProminence[(size_t) h], 1));
            note ("first notch " + fmt (run.firstNotchSec * 1000.0, 0) + " ms; " + watchText (run.watch));
            return;
        }
        const bool verbose = juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_HF_VERBOSE", {}).isNotEmpty();
        const double freqs[] = { 3000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0, 18000.0 };
        int caught = 0, total = 0;
        for (const auto prog : { Programme::roomOnly, Programme::speech })
            for (const auto gain : { 1.02, 1.1, 1.3 })
                for (const auto f : freqs)
                {
                    RoomSpec spec;
                    spec.freq = f; spec.gain = gain; spec.programme = prog;
                    const auto r = hfCase (spec, defaultRate, defaultBlock);
                    ++total; if (r.caught) ++caught;
                    const auto label = std::string (prog == Programme::speech ? "speech + room" : "room noise") + ", 48 kHz / 64, gain " + fmt (gain, 2) + ", " + fmt (f / 1000.0, 0) + " kHz";
                    note (hfLine (label, r));
                    if (verbose || ! r.caught) note ("  watch: " + watchText (r.watch));
                    check (r.caught, label + ": the feedback is caught and held within 3 dB of the loop-free level");
                    if (r.caught) check (r.cutAtHowl <= -3.0, label + ": the notch lands on the howl (" + fmt (r.cutAtHowl, 1) + " dB at " + fmt (r.howlHz, 0) + " Hz)");
                }
        for (const auto rate : { 44100.0, 96000.0 })
            for (const auto f : { 5000.0, 10000.0, 16000.0, 18000.0 })
            {
                RoomSpec spec; spec.freq = f; spec.gain = 1.1;
                const auto r = hfCase (spec, rate, defaultBlock);
                ++total; if (r.caught) ++caught;
                const auto label = "room noise, " + fmt (rate / 1000.0, 1) + " kHz / 64, gain 1.10, " + fmt (f / 1000.0, 0) + " kHz";
                note (hfLine (label, r));
                if (verbose || ! r.caught) note ("  watch: " + watchText (r.watch));
                check (r.caught, label + ": caught");
            }
        for (const auto block : { 256, 512, 2048 })
            for (const auto f : { 5000.0, 10000.0, 16000.0 })
            {
                RoomSpec spec; spec.freq = f; spec.gain = 1.1;
                const auto r = hfCase (spec, defaultRate, block);
                ++total; if (r.caught) ++caught;
                const auto label = "room noise, 48 kHz / " + std::to_string (block) + ", gain 1.10, " + fmt (f / 1000.0, 0) + " kHz";
                note (hfLine (label, r));
                if (verbose || ! r.caught) note ("  watch: " + watchText (r.watch));
                check (r.caught, label + ": caught");
            }
        note ("high-frequency feedback: caught " + std::to_string (caught) + " of " + std::to_string (total) + " cases");

        // Snapshot (only when snapshots are asked for): one editor after three high howls in a row
        // (10, 16 and 18 kHz, speech + room, loop gain 1.10), so the HF notches show on the graph.
        if (const auto* snap = std::getenv ("DOOBLRFEEDBACK_EDITOR_SNAPSHOT"); snap != nullptr && *snap != 0)
        {
            EditorFixture f (false);
            if (f.editor == nullptr) return;
            for (const auto hz : { 10000.0, 16000.0, 18000.0 })
            {
                RoomSpec spec; spec.freq = hz; spec.gain = 1.1; spec.programme = Programme::speech;
                runRoom (f.processor.get(), spec);
            }
            f.editor->setSize (1080, 660);
            f.settle();
            const auto notches = f.processor->getNotches();
            note ("HF snapshot notches: " + notchList (notches));
            check (activeCount (notches) >= 3, "the three high howls each left a notch for the snapshot (" + std::to_string (activeCount (notches)) + ")");
            saveSnapshot (*f.editor, "-hf-1080x660");
        }
    }


    //==============================================================================
    // Calm labels: no bouncing, tidy lanes, idle labels close.

    bool overlapsAny (const std::vector<ui::FeedbackGraph::Marker>& m, std::string& which)
    {
        for (size_t i = 0; i < m.size(); ++i)
            for (size_t j = i + 1; j < m.size(); ++j)
                if (m[i].chip.intersects (m[j].chip))
                {
                    which = std::to_string (m[i].number) + (m[i].compact ? " (dot)" : " (label)") + " and " + std::to_string (m[j].number) + (m[j].compact ? " (dot)" : " (label)");
                    return true;
                }
        return false;
    }

    void testCalmLabels()
    {
        // 1. Five seconds of howls coming and going at the four notches (levels +-12 dB at 0.4-3 Hz,
        //    the detector deepening / ringing / quiet): no label moves, values refresh calmly.
        {
            EditorFixture f;
            auto* editor = f.editor;
            if (editor == nullptr) return;
            auto& p = *f.processor;
            auto& g = editor->getGraph();
            set (p, id::liveNotches, 1.0f); // Hold: none released during the run
            editor->setSize (1080, 660);
            f.settle();
            const double freqs[] = { 315.0, 1250.0, 2510.0, 6300.0 }, rates[] = { 0.4, 1.3, 2.1, 3.0 };
            const auto layouts0 = g.getLayoutCount();
            const auto refreshes0 = g.getValueRefreshCount();
            std::map<int, juce::Rectangle<float>> lastRect;
            std::map<int, bool> lastCompact;
            double lastEventMs = -1.0e9, worst = 0.0;
            int events = 0, ringingFrames = 0;
            double t = 0.0;
            const auto noise = pink ((size_t) (defaultRate * 5.5), -45.0, 77);
            size_t pos = 0;
            for (int frame = 0; frame < 300; ++frame)
            {
                Stereo x (800);
                for (size_t i = 0; i < 800; ++i, t += 1.0 / defaultRate)
                {
                    auto v = (double) noise[pos++];
                    for (int k = 0; k < 4; ++k)
                        v += 0.03 * std::pow (10.0, 0.6 * std::sin (twoPi * rates[k] * t + k)) * std::sin (twoPi * freqs[k] * t);
                    x.l[i] = x.r[i] = (float) v;
                }
                render (p, x);
                f.tick();
                for (const auto& n : p.getNotches()) if (n.active && n.ringing) { ++ringingFrames; break; }
                std::set<int> ids;
                for (const auto& m : g.getMarkers()) ids.insert (m.id);
                for (const auto& m : g.getMarkers())
                {
                    const auto known = lastRect.count (m.id) > 0;
                    if (! known || lastCompact[m.id] != m.compact) { lastEventMs = f.clock; ++events; }
                    else if (f.clock - lastEventMs > ui::FeedbackGraph::moveMs)
                    {
                        const auto& r = lastRect[m.id];
                        worst = std::max ({ worst, (double) std::abs (r.getX() - m.chip.getX()), (double) std::abs (r.getY() - m.chip.getY()) });
                    }
                    lastRect[m.id] = m.chip;
                    lastCompact[m.id] = m.compact;
                }
                if (ids.size() != lastRect.size()) { lastEventMs = f.clock; ++events; }
                for (auto it = lastRect.begin(); it != lastRect.end();)
                    it = ids.count (it->first) ? std::next (it) : lastRect.erase (it);
            }
            const auto layouts = g.getLayoutCount() - layouts0;
            const auto refreshRate = (double) (g.getValueRefreshCount() - refreshes0) / (5.0 * (double) std::max<size_t> (1, g.getMarkers().size()));
            note ("5 s of fluctuating howls: labels moved at most " + fmt (worst, 2) + " px outside add / remove / open / close (" + std::to_string (events)
                  + " such events), " + std::to_string (layouts) + " re-layouts in 300 frames, value refreshes " + fmt (refreshRate, 1) + " per label per second, ringing in "
                  + std::to_string (ringingFrames) + " of 300 frames");
            check (ringingFrames > 30, "the run really exercises the detector (ringing frames " + std::to_string (ringingFrames) + ")");
            check (worst < 1.0, "labels hold still while levels fluctuate (" + fmt (worst, 2) + " px)");
            check (layouts <= 2 * events + 2, "layout only on events, not every frame (" + std::to_string (layouts) + ")");
            check (refreshRate <= 5.5, "values refresh at a calm rate (" + fmt (refreshRate, 1) + " / s)");
            saveSnapshot (*editor, "-calm-1080x660");
        }

        // 2. A full bank (24 notches, a cluster at 2-3.5 kHz) at three sizes: no overlaps.
        for (auto size : editorSizes)
        {
            EditorFixture f (false);
            auto* editor = f.editor;
            if (editor == nullptr) return;
            auto& p = *f.processor;
            set (p, id::detect, 0.0f);
            for (int k = 0; k < 16; ++k) p.getEngine().addNotch (60.0f * std::pow (300.0f, (float) k / 15.0f) * (k == 15 ? 0.9f : 1.0f), 6.0f + (float) (k % 5) * 3.0f, 20.0f, k % 3 == 0, 1);
            for (int k = 0; k < 8; ++k) p.getEngine().addNotch (2050.0f * std::pow (1.06f, (float) k), 9.0f + (float) k, 30.0f, false, 1);
            render (p, mono (pink ((size_t) (defaultRate * 0.3), -30.0, 63)));
            editor->setSize (size.x, size.y);
            f.settle();
            auto& g = editor->getGraph();
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y) + ", 24 notches";
            std::string which;
            const auto& m = g.getMarkers();
            int open = 0;
            for (const auto& mk : m) if (! mk.compact) ++open;
            check (m.size() == 24, label + ": 24 markers (" + std::to_string (m.size()) + ")");
            check (! overlapsAny (m, which), label + ": no labels or dots overlap (" + which + ")");
            for (const auto& mk : m)
                check (g.getPlotArea().expanded (1.0f).contains (mk.chip), label + ": marker " + std::to_string (mk.number) + " inside the plot");
            note (label + ": " + std::to_string (open) + " full labels, " + std::to_string (24 - open) + " numbered dots");
            saveSnapshot (*editor, "-24notches-" + juce::String (size.x) + "x" + juce::String (size.y));
        }

        // 3. Idle labels close; hover (graph or table), selection and activity open them; a
        //    removed notch's marker fades out.
        {
            EditorFixture f;
            auto* editor = f.editor;
            if (editor == nullptr) return;
            auto& p = *f.processor;
            auto& g = editor->getGraph();
            set (p, id::detect, 0.0f);
            const auto openCount = [&] { int c = 0; for (const auto& m : g.getMarkers()) if (! m.compact) ++c; return c; };
            check (openCount() == 4, "fresh notches show full labels (" + std::to_string (openCount()) + ")");
            f.tick (60 * 4 + 30);
            check (openCount() == 0 && g.getMarkers().size() == 4, "after 4 s idle every label closes to a numbered dot (" + std::to_string (openCount()) + " open, " + std::to_string (g.getMarkers().size()) + " markers)");
            f.tick (30);
            check (! g.isAnimating(), "closed labels settle (no animation running)");
            const auto id = g.getMarkers().back().id;
            editor->getNotchTable().onNotchHovered (id);
            f.tick (20);
            const auto* hm = g.markerFor (id);
            check (hm != nullptr && ! hm->compact, "hovering its table row opens a label");
            editor->getNotchTable().onNotchHovered (0);
            f.tick (60);
            hm = g.markerFor (id);
            check (hm != nullptr && hm->compact, "it closes again after the hover ends");
            g.setSelectedNotch (id);
            f.tick (20);
            hm = g.markerFor (id);
            check (hm != nullptr && ! hm->compact && ! hm->lockButton.isEmpty() && ! hm->clearButton.isEmpty(), "the selected notch shows its label with lock / clear");
            g.setSelectedNotch (0);
            p.clearNotch (id);
            f.tick (3);
            check (g.getLabelCount() == 4, "a cleared notch's marker fades out (still drawn just after)");
            f.tick ((int) std::ceil (ui::FeedbackGraph::fadeMs / (1000.0 / 60.0)) + 8);
            check (g.getLabelCount() == 3 && g.getMarkers().size() == 3, "... and is gone after the fade (" + std::to_string (g.getLabelCount()) + " labels)");
            saveSnapshot (*editor, "-idle-1080x660");
        }
    }

    //==============================================================================
    // One howl, one notch: detections near a notch re-tune / deepen / widen it, never stack.

    /** Peak dBFS of x over [from, to) seconds. */
    double peakDbBetween (const std::vector<float>& x, double rate, double from, double to)
    {
        float peak = 0.0f;
        for (auto i = (size_t) std::max (0.0, from * rate); i < std::min (x.size(), (size_t) std::max (0.0, to * rate)); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return 20.0 * std::log10 ((double) peak + 1.0e-12);
    }

    void testSafetyDuck()
    {
        const auto threshold = -6.0;
        const auto thresholdLin = (float) std::pow (10.0, threshold / 20.0);
        const auto firstOver = [&] (const std::vector<float>& x)
        {
            for (size_t i = 0; i < x.size(); ++i)
                if (std::abs (x[i]) > thresholdLin) return (double) i / defaultRate;
            return -1.0;
        };

        // 1. A runaway the notches cannot hold (Max Cut 3 dB, loop gain 1.6 = +4.1 dB): without the duck the
        //    output sits near full scale; with it the level is held near the threshold.
        RoomSpec s; s.freq = 2000.0; s.gain = 1.6; s.seconds = 6.0; s.loopOffSec = 2.5;
        RoomRun loose, held;
        dsp::FeedbackEngine::SafetyState heldSafety;
        {
            auto p = makeProcessor();
            set (*p, id::maxCut, 3.0f);
            set (*p, id::safety, 0.0f);
            loose = runRoom (p.get(), s);
        }
        {
            auto p = makeProcessor();
            set (*p, id::maxCut, 3.0f);
            held = runRoom (p.get(), s);
            heldSafety = p->getEngine().getSafety();
        }
        const auto unprotected = peakDbBetween (loose.out, defaultRate, 1.0, 2.5);
        const auto over = firstOver (held.out);
        // If the duck (armed by the detector's report) keeps the output from ever crossing, the
        // whole loop-on stretch is the held level.
        const auto wholePeak = peakDbBetween (held.out, defaultRate, 0.0, 2.5);
        const auto heldPeak = over >= 0.0 ? peakDbBetween (held.out, defaultRate, over + 0.03, 2.5) : wholePeak;
        const auto firstPeak = over >= 0.0 ? peakDbBetween (held.out, defaultRate, over, over + 0.03) : wholePeak;
        note ("  first crossing of the threshold " + (over >= 0.0 ? fmt (over, 3) + " s" : std::string ("never")) + ", loop-on peak " + fmt (wholePeak, 1) + " dBFS");
        note ("runaway the notches cannot hold (2 kHz, loop gain 1.6 = +4.1 dB, Max Cut 3 dB): without the duck "
              + fmt (unprotected, 1) + " dBFS; with it " + fmt (heldPeak, 1) + " dBFS peak after the first 30 ms over (first 30 ms "
              + fmt (firstPeak, 1) + "), " + std::to_string (heldSafety.events) + " duck(s), " + fmt (heldSafety.totalSec, 2) + " s ducked");
        check (unprotected > threshold + 3.0, "the unprotected runaway exceeds the threshold (" + fmt (unprotected, 1) + " dBFS)");
        check (heldPeak <= threshold + 2.0, "with the duck the runaway stays within 2 dB of the threshold (" + fmt (heldPeak, 1) + ")");
        check (firstPeak <= threshold + 4.0, "the duck's attack overshoots by less than 4 dB (" + fmt (firstPeak, 1) + ")");
        check (heldSafety.events >= 1, "the duck engaged");
        check (finiteAndNormal (held.out), "no NaN / denormal while ducking");

        // Recovery: the loop is broken at 2.5 s; the level comes back smoothly (no step) within
        // the release, and the duck lets go.
        {
            const auto win = (size_t) (0.005 * defaultRate);
            double worstStep = 0.0, prev = 0.0;
            bool first = true;
            for (auto i = (size_t) (2.6 * defaultRate); i + win <= held.out.size(); i += win)
            {
                // The gain, as the output / input level ratio of the same (room-noise) signal.
                const auto g = rmsDb (held.out, i, win) - rmsDb (held.mic, i, win);
                if (! first) worstStep = std::max (worstStep, std::abs (g - prev));
                prev = g; first = false;
            }
            // 0.1.2: the release comes back in three probed steps (1.5 s + 2 x 0.45 s at Recover 1.5 s).
            const auto at = (size_t) (5.6 * defaultRate), len = (size_t) (0.4 * defaultRate);
            const auto endGain = rmsDb (held.out, at, len) - rmsDb (held.mic, at, len);
            note ("recovery after the loop is broken at 2.5 s: worst gain step " + fmt (worstStep, 2) + " dB per 5 ms; gain at 3.6 - 4.0 s " + fmt (endGain, 2) + " dB");
            check (worstStep < 1.5, "the duck recovers smoothly (" + fmt (worstStep, 2) + " dB / 5 ms)");
            check (endGain > -4.0, "the level is back (less the notches' own cut) after the release (" + fmt (endGain, 2) + " dB)");
        }

        // Depth -inf mutes the runaway; still a 5 ms ramp, still finite.
        {
            auto p = makeProcessor();
            auto* depth = p->getState().getParameter (id::safetyDepth);
            const auto topText = depth->getText (1.0f, 16);
            check (topText.contains (juce::String::fromUTF8 ("\xe2\x88\x9e")) || topText.containsIgnoreCase ("mute"), "Safety depth's top reads infinity / Mute (" + topText.toStdString() + ")");
            set (*p, id::maxCut, 3.0f);
            depth->setValueNotifyingHost (1.0f);
            const auto run = runRoom (p.get(), s);
            const auto engaged = p->getEngine().getSafety().events;
            // The duck engages on the first sample over the threshold, so the output may never
            // cross it: time from where the output first reaches within 0.5 dB of it.
            auto o = -1.0;
            for (size_t i = 0; i < run.out.size(); ++i)
                if (std::abs (run.out[i]) > thresholdLin * 0.944f) { o = (double) i / defaultRate; break; }
            const auto muted = o >= 0.0 ? peakDbBetween (run.out, defaultRate, o + 0.06, o + 0.08) : 0.0;
            note ("Depth " + topText.toStdString() + ": " + fmt (muted, 1) + " dBFS 60 - 80 ms after the output first reached the threshold (at " + fmt (o, 3) + " s)"
                  + ", loop-on peak " + fmt (peakDbBetween (run.out, defaultRate, 0.0, 2.5), 1) + " dBFS, " + std::to_string (engaged) + " duck(s)");
            check (engaged >= 1 && muted < -60.0, "Depth -inf mutes the runaway (" + fmt (muted, 1) + " dBFS)");
            check (finiteAndNormal (run.out), "muting is finite");
        }

        // An extreme runaway (loop gain 2 = +6 dB, 600 dB/s): it reaches the threshold within about
        // one analysis window, before the detector can confirm it, so Feedback mode cannot duck the
        // first crossing (Always mode can). Once it is confirmed the duck holds it.
        {
            auto fast = s; fast.gain = 2.0;
            auto p = makeProcessor();
            set (*p, id::maxCut, 3.0f);
            const auto run = runRoom (p.get(), fast);
            const auto o = firstOver (run.out);
            const auto first = o >= 0.0 ? peakDbBetween (run.out, defaultRate, o, o + 0.1) : -120.0;
            const auto later = o >= 0.0 ? peakDbBetween (run.out, defaultRate, o + 0.3, 2.5) : -120.0;
            note ("extreme runaway (loop gain 2.0): first 100 ms over the threshold " + fmt (first, 1) + " dBFS (not yet confirmed), from 300 ms on "
                  + fmt (later, 1) + " dBFS, " + std::to_string (p->getEngine().getSafety().events) + " duck(s)");
            check (later <= threshold + 2.0, "a confirmed extreme runaway is held by the duck (" + fmt (later, 1) + ")");
        }

        // 2. Loud music (peaks over the threshold, no feedback) is never ducked in Feedback mode;
        //    "Always" ducks it (it is then a plain over-threshold duck).
        for (const auto always : { false, true })
        {
            auto p = makeProcessor();
            set (*p, id::safetyTrigger, always ? 1.0f : 0.0f);
            auto x = brightChord ((size_t) (4.0 * defaultRate), defaultRate, 0.1f, 95);
            const auto drums = pink (x.size(), -14.0, 96);
            for (size_t i = 0; i < x.size(); ++i) x[i] = 3.5f * x[i] + drums[i];
            const auto in = mono (x);
            const auto inPeak = peakDbBetween (x, defaultRate, 0.0, 4.0);
            render (*p, mono (std::vector<float> (2048, 0.0f))); // warm up outside the count
            countAllocations = true; allocationCount = 0;
            const auto out = render (*p, in);
            countAllocations = false;
            const auto st = p->getEngine().getSafety();
            note (std::string (always ? "Always" : "Feedback") + " mode, loud music (" + fmt (inPeak, 1) + " dBFS peaks, no feedback): "
                  + std::to_string (st.events) + " duck(s), " + std::to_string (activeCount (p->getNotches())) + " notch(es)");
            check (inPeak > threshold + 2.0, "the music peaks over the threshold (" + fmt (inPeak, 1) + ")");
            if (always) check (st.events >= 1, "Always mode ducks loud programme");
            else check (st.events == 0, "Feedback mode leaves loud music alone (" + std::to_string (st.events) + " ducks)");
            check (finiteAndNormal (out.l), "finite output");
        }

        // 3. The editor: the red badge, the status count and the meter's threshold tick.
        {
            EditorFixture f (false);
            auto* editor = f.editor;
            if (editor == nullptr) return;
            auto& p = *f.processor;
            set (p, id::safetyTrigger, 1.0f); // Always: a plain loud sine ducks
            std::vector<float> x ((size_t) (0.5 * defaultRate), 0.0f);
            addSine (x, defaultRate, 1000.0, 0.9f);
            render (p, mono (x));
            editor->refresh();
            auto& badge = editor->getSafetyBadge();
            note ("badge while ducking: \"" + badge.getText().toStdString() + "\"; status \"" + editor->getStatusLabel().getText().toStdString() + "\"");
            {
                // The status reads "ducked 1\xc3\x97 (...)": one U+00D7, no double-encoded UTF-8 (U+00C3).
                const auto status = editor->getStatusLabel().getText();
                check (status.containsChar ((juce::juce_wchar) 0x00d7) && ! status.containsChar ((juce::juce_wchar) 0x00c3), "safety status text is clean UTF-8 (\u00d7, no mojibake)");
                auto* knob = dynamic_cast<juce::SettableTooltipClient*> (editor->findControl (id::maxCut));
                const auto tip = knob != nullptr ? knob->getTooltip() : juce::String();
                check (tip.containsChar ((juce::juce_wchar) 0x221e) && ! tip.containsChar ((juce::juce_wchar) 0x00e2), "Max cut tooltip shows the infinity sign");
            }
            check (badge.isVisible() && badge.getText().startsWith ("SAFETY") && badge.getText().contains ("12"), "the toolbar shows the red SAFETY badge with the cut");
            check (editor->getStatusLabel().getText().containsIgnoreCase ("safety"), "the status line says the safety duck is on");
            check (std::abs (editor->getOutputMeter().getThresholdDb() - (float) threshold) < 0.1f, "the meter shows the threshold");
            {
                TestWindow window (*editor);
                auto* hit = editor->getComponentAt (badge.getBounds().getCentre());
                check (hit == &badge || badge.isParentOf (hit), "the badge sits clear of the toolbar's controls (hit: " + (hit != nullptr ? (hit->getComponentID() + "/" + hit->getName() + "/" + juce::String (typeid (*hit).name())).toStdString() : std::string ("none")) + ")");
            }
            // Its whole box (not just its centre) is clear of every other visible control, at every size.
            for (auto size : editorSizes)
            {
                editor->setSize (size.x, size.y);
                std::string overlap;
                if (auto* parent = badge.getParentComponent())
                    for (auto* sib : parent->getChildren())
                        if (sib != &badge && sib->isVisible() && sib->getBounds().intersects (badge.getBounds())
                            && ! sib->getBounds().contains (badge.getBounds()))
                            overlap += (overlap.empty() ? "" : ", ") + (sib->getComponentID().isNotEmpty() ? sib->getComponentID() : sib->getName()).toStdString()
                                     + (sib->getComponentID().isEmpty() && sib->getName().isEmpty() ? juce::String (typeid (*sib).name()).toStdString() : std::string());
                check (overlap.empty(), std::to_string (size.x) + "x" + std::to_string (size.y) + ": the badge overlaps no other control (" + overlap + ")");
            }
            editor->setSize (1080, 660);
            saveSnapshot (*editor, "-safety-1080x660");
            render (p, mono (pink ((size_t) (3.0 * defaultRate), -40.0, 97)));
            editor->refresh();
            check (! badge.isVisible(), "the badge goes once the level has recovered");
            check (editor->getStatusLabel().getText().contains ("ducked 1"), "the status line counts the duck (" + editor->getStatusLabel().getText().toStdString() + ")");
            set (p, id::safety, 0.0f);
            editor->refresh();
            check (std::isnan (editor->getOutputMeter().getThresholdDb()), "no threshold tick with Safety off");
        }
    }

    void testOneNotchPerHowl()
    {
        struct Case { const char* name; RoomSpec spec; int expect; };
        const auto binHz = defaultRate / 4096.0;
        std::vector<Case> cases;
        { RoomSpec s; s.freq = 2000.0; s.gain = 1.2; cases.push_back ({ "steady 2 kHz", s, 1 }); }
        { RoomSpec s; s.freq = 256.5 * binHz; s.gain = 1.2; cases.push_back ({ "between bins (256.5 bins, 3006 Hz)", s, 1 }); }
        { RoomSpec s; s.freq = 5000.0; s.gain = 1.15; s.driftPct = 1.5; cases.push_back ({ "drifting 5 kHz (+1.5 % over 6 s)", s, 1 }); }
        { RoomSpec s; s.freq = 1200.0; s.gain = 1.15; s.warblePct = 0.4; cases.push_back ({ "warbling 1.2 kHz (+-0.4 % at 0.7 Hz)", s, 1 }); }
        { RoomSpec s; s.freq = 12000.0; s.gain = 1.3; cases.push_back ({ "fast 12 kHz (gain 1.3)", s, 1 }); }
        { RoomSpec s; s.freq = 800.0; s.gain = 1.1; s.programme = Programme::speech; cases.push_back ({ "800 Hz under speech", s, 1 }); }
        { RoomSpec s; s.freq = 4000.0; s.gain = 1.15; s.driftPct = 6.0; cases.push_back ({ "drifting 4 kHz (+6 % over 6 s)", s, 1 }); }
        { RoomSpec s; s.freq = 6000.0; s.gain = 1.15; s.warblePct = 2.0; s.warbleHz = 0.5; cases.push_back ({ "wide warble 6 kHz (+-2 % at 0.5 Hz)", s, 1 }); }
        { RoomSpec s; s.freq = 2500.0; s.gain = 1.15; s.driftPct = 4.0; s.warblePct = 1.0; s.warbleHz = 0.3; cases.push_back ({ "2.5 kHz, Notch Q 60, drift +4 % and warble +-1 %", s, 1 }); }
        { RoomSpec s; s.freq = 2000.0; s.freq2 = 2000.0 * std::pow (2.0, 1.0 / 3.0); s.gain = 1.2; cases.push_back ({ "two howls 1/3 octave apart (2000 / 2520 Hz)", s, 2 }); }
        for (const auto& c : cases)
        {
            auto p = makeProcessor();
            if (std::string (c.name).find ("Q 60") != std::string::npos) set (*p, id::notchQ, 60.0f);
            const auto run = runRoom (p.get(), c.spec, defaultRate, defaultBlock, true);
            int final = 0;
            for (const auto& n : run.notches)
                if (n.active && ! n.merging) ++final;
            auto quiet = c.spec; quiet.gain = 0.0;
            const auto base = bandEnvelopeDb (runRoom (nullptr, quiet).mic, defaultRate, c.spec.freq);
            const auto env = bandEnvelopeDb (run.mic, defaultRate, c.spec.freq);
            auto baseLevel = 0.0, steady = 0.0;
            for (size_t i = 50; i < base.size(); ++i) baseLevel += base[i];
            baseLevel /= (double) (base.size() - 50);
            for (size_t i = env.size() - 100; i < env.size(); ++i) steady += env[i];
            steady = steady / 100.0 - baseLevel;
            // The howl the loop really runs at (the delay picks a mode near the resonance), at the end
            // of the run with the loop opened up: measured on a no-plugin run over the same seconds.
            auto loose = c.spec; loose.seconds = std::min (c.spec.seconds, 1.0 + (run.firstNotchSec > 0.0 ? run.firstNotchSec : 0.0));
            const auto howl = c.spec.driftPct != 0.0 || c.spec.warblePct != 0.0 || c.spec.freq2 > 0.0 ? 0.0
                            : howlFrequency (runRoom (nullptr, loose).mic, defaultRate, c.spec.freq, run.firstNotchSec > 0.0 ? run.firstNotchSec : loose.seconds);
            const auto cut = final > 0 && howl > 0.0 ? measuredCutDb (run.notches, defaultRate, howl) : 0.0;
            const auto needed = 20.0 * std::log10 (c.spec.gain);
            note (std::string (c.name) + ": " + std::to_string (final) + " notch(es) at the end, at most " + std::to_string (run.maxConcurrent)
                  + " at once, " + std::to_string (run.ids.size()) + " allocated; " + (howl > 0.0 ? "bank cut at the howl (" + fmt (howl, 0) + " Hz) " + fmt (cut, 1) + " dB" : std::string ("moving howl")) + " (loop needs > "
                  + fmt (needed, 1) + "); steady " + fmt (steady, 1) + " dB re no-loop; merges " + std::to_string (p->getEngine().getMergeCount()) + "; " + notchList (run.notches));
            check (final == c.expect, std::string (c.name) + ": " + std::to_string (c.expect) + " notch(es) (" + std::to_string (final) + ")");
            check (run.maxConcurrent <= c.expect, std::string (c.name) + ": never more than " + std::to_string (c.expect) + " at once (" + std::to_string (run.maxConcurrent) + ")");
            // Held: an unprotected loop runs 80 dB up; a held one stays near the loop-free level (a
            // loop just under unity still colours the band a few dB, as in any room).
            check (steady < 6.0, std::string (c.name) + ": held near the loop-free level (" + fmt (steady, 1) + " dB)");
            if (howl > 0.0) check (-cut >= needed + 3.0, std::string (c.name) + ": the one notch cuts at least the loop gain + 3 dB (" + fmt (-cut, 1) + " dB)");
        }

        // Two notches that drifted together are consolidated into one, crossfaded (no gap).
        auto p = makeProcessor();
        set (*p, id::detect, 1.0f);
        set (*p, id::liveNotches, 1.0f);
        auto& e = p->getEngine();
        e.addNotch (3000.0f, 12.0f, 20.0f, false);
        e.addNotch (3030.0f, 9.0f, 20.0f, false);
        Stereo sine ((size_t) (defaultRate * 1.0));
        addSine (sine.l, defaultRate, 3015.0, 0.01f);
        sine.r = sine.l;
        const auto out = render (*p, sine);
        check (e.countActive() == 1, "drifted-together notches are consolidated into one (" + std::to_string (e.countActive()) + ")");
        check (e.getMergeCount() >= 1, "consolidation counted");
        // During the crossfade the cut at the shared frequency never dips below the survivor's.
        auto worst = -200.0;
        for (size_t i = (size_t) (0.2 * defaultRate); i + 960 <= out.l.size(); i += 960)
            worst = std::max (worst, rmsDb (out.l, i, 960) - rmsDb (sine.l, i, 960));
        check (worst < -6.0, "no gap in the cut while the duplicate fades (worst " + fmt (worst, 1) + " dB)");
        check (finiteAndNormal (out.l), "consolidation: finite output");
    }

    //==============================================================================
    // Bright programme that must not trigger Safe mode.

    std::vector<float> brightChord (size_t n, double rate, float amplitude, int seed)
    {
        // Sawtooth chord (A2 E3 A3 C#4 E4, slightly detuned pairs), every harmonic up to Nyquist
        // (polyBLEP), re-voiced every 2 s.
        juce::Random r (seed);
        std::vector<float> v (n, 0.0f);
        const double chords[][5] = { { 110.0, 164.81, 220.0, 277.18, 329.63 }, { 98.0, 146.83, 196.0, 246.94, 293.66 }, { 130.81, 196.0, 261.63, 329.63, 392.0 } };
        std::array<double, 10> phase {};
        for (auto& ph : phase) ph = r.nextDouble();
        const auto blep = [] (double t, double dt)
        {
            if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        };
        for (size_t i = 0; i < n; ++i)
        {
            const auto& c = chords[(i / (size_t) (2.0 * rate)) % 3];
            double s = 0.0;
            for (int k = 0; k < 10; ++k)
            {
                const auto f = c[k / 2] * (k % 2 == 0 ? 1.0 : 1.004);
                const auto dt = f / rate;
                phase[(size_t) k] += dt;
                if (phase[(size_t) k] >= 1.0) phase[(size_t) k] -= 1.0;
                s += 2.0 * phase[(size_t) k] - 1.0 - blep (phase[(size_t) k], dt);
            }
            v[i] = (float) (amplitude * s / 10.0);
        }
        return v;
    }

    /** Cymbal: every 1.2 s a crash, ~60 inharmonic modes 3-16 kHz with 0.6-2.5 s decays plus a noise burst. */
    std::vector<float> cymbals (size_t n, double rate, int seed)
    {
        juce::Random r (seed);
        std::vector<float> v (n, 0.0f);
        struct Mode { double f, decay, amp, phase; };
        std::vector<Mode> modes;
        for (int k = 0; k < 60; ++k)
            modes.push_back ({ 3000.0 * std::pow (16000.0 / 3000.0, r.nextDouble()), 0.6 + 1.9 * r.nextDouble(), 0.3 + 0.7 * r.nextDouble(), r.nextDouble() * twoPi });
        const auto period = (size_t) (1.2 * rate);
        OnePole lp (rate, 2000.0);
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) (i % period) / rate;
            double s = 0.0;
            for (const auto& m : modes)
                s += m.amp * std::exp (-t / m.decay) * std::sin (m.phase + twoPi * m.f * (double) i / rate);
            const auto w = r.nextDouble() * 2.0 - 1.0;
            s = 0.02 * s + 0.4 * std::exp (-t / 0.25) * (w - lp.process (w));
            v[i] = (float) s;
        }
        return v;
    }

    /** Hi-hat: closed hats on 8th notes at 120 bpm (short, high-passed noise with a few metallic modes). */
    std::vector<float> hiHats (size_t n, double rate, int seed)
    {
        juce::Random r (seed);
        std::vector<float> v (n, 0.0f);
        const auto period = (size_t) (0.25 * rate);
        OnePole lp (rate, 6000.0);
        const double modes[] = { 6120.0, 7340.0, 8810.0, 10450.0, 12900.0 };
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) (i % period) / rate;
            const auto open = ((i / period) % 8) == 7; // an open hat once a bar
            const auto e = std::exp (-t / (open ? 0.35 : 0.04));
            const auto w = r.nextDouble() * 2.0 - 1.0;
            double s = w - lp.process (w);
            for (const auto f : modes) s += 0.08 * std::sin (twoPi * f * (double) i / rate);
            v[i] = (float) (0.5 * e * s);
        }
        return v;
    }

    /** Sibilant speech: the synthetic talker with an 's' or 'sh' (band-passed noise 4-10 kHz, 80-250 ms) every ~0.6 s. */
    std::vector<float> sibilantSpeech (size_t n, double rate, int seed)
    {
        auto v = speech (n, rate, seed);
        juce::Random r (seed + 1);
        BandPass s1 (rate, 6500.0, 2.5), s2 (rate, 8500.0, 3.0), sh (rate, 4200.0, 2.0);
        size_t next = (size_t) (0.3 * rate), end = 0;
        bool isSh = false;
        for (size_t i = 0; i < n; ++i)
        {
            if (i == next) { end = i + (size_t) ((0.08 + 0.17 * r.nextDouble()) * rate); next = i + (size_t) ((0.45 + 0.3 * r.nextDouble()) * rate); isSh = r.nextBool(); }
            const auto w = r.nextDouble() * 2.0 - 1.0;
            const auto a = s1.process (w) + s2.process (w);
            const auto b = sh.process (w);
            if (i < end)
                v[i] += (float) (0.9 * (isSh ? b : a));
        }
        return v;
    }

    /** Bright plucked guitar: Karplus-Strong strums of an E major chord with a bright pick (little damping). */
    std::vector<float> brightGuitar (size_t n, double rate, int seed)
    {
        juce::Random r (seed);
        std::vector<float> v (n, 0.0f);
        const double strings[] = { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 };
        std::vector<std::vector<double>> lines;
        std::vector<size_t> idx (6, 0);
        for (const auto f : strings) lines.emplace_back ((size_t) std::lround (rate / f), 0.0);
        const auto strum = (size_t) (0.75 * rate);
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t s = 0; s < 6; ++s)
            {
                if (i % strum == s * (size_t) (0.012 * rate))
                    for (auto& x : lines[s]) x = r.nextDouble() * 2.0 - 1.0;
                auto& line = lines[s];
                const auto a = line[idx[s]], b = line[(idx[s] + 1) % line.size()];
                const auto y = 0.4985 * (a + b) + 0.002 * (a - b); // bright: almost no loss per pass
                line[idx[s]] = y;
                idx[s] = (idx[s] + 1) % line.size();
                v[i] += (float) (0.12 * a);
            }
        }
        return v;
    }

    //==============================================================================
    /** A closed loop like runLoop, with a loop gain that changes over time and a hook after
        every block (to watch the notches). */
    std::vector<float> runScheduledLoop (Processor& p, double seconds, double freq, const std::function<double (double)>& gainAt,
                                         const std::function<void (double)>& afterBlock, double noiseDb = -40.0, double bandQ = 8.0)
    {
        const auto rate = defaultRate;
        const auto block = defaultBlock;
        const auto n = (size_t) (seconds * rate);
        const auto src = pink (n, noiseDb, 91);
        BandPass band (rate, freq, bandQ);
        std::vector<float> speaker ((size_t) 8192, 0.0f), mic (n, 0.0f);
        const size_t delay = 480;
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos < n; pos += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, n - pos);
            buffer.setSize (2, len, false, false, true);
            const auto g = gainAt ((double) pos / rate);
            for (int i = 0; i < len; ++i)
            {
                const auto t = pos + (size_t) i;
                const auto delayed = t >= delay ? (double) speaker[(t - delay) & 8191] : 0.0;
                const auto x = (float) std::tanh ((double) src[t] + g * band.process (delayed));
                mic[t] = x;
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < len; ++i)
                speaker[(pos + (size_t) i) & 8191] = buffer.getSample (0, i);
            afterBlock ((double) (pos + (size_t) len) / rate);
        }
        return mic;
    }

    dsp::Notch notchById (Processor& p, int nid)
    {
        for (const auto& n : p.getNotches()) if (n.active && n.id == nid) return n;
        return {};
    }

    void testAutoRecover()
    {
        using Engine = dsp::FeedbackEngine;

        // Parameters: Hold (the old Live notches / Auto-recover, same ID and values), Recover after, Recover speed.
        {
            auto p = makeProcessor();
            auto* ar = param (*p, id::liveNotches);
            auto* sp = param (*p, id::recoverSpeed);
            check (ar != nullptr && ar->getName (64) == "Hold" && ar->getCurrentValueAsText() == "Off", "Hold is off by default (bands subside)");
            check (sp != nullptr && sp->getName (64) == "Recover speed" && sp->getCurrentValueAsText() == "Slow", "Recover speed defaults to Slow (cautious)");
            check (param (*p, id::release) != nullptr && param (*p, id::release)->getName (64) == "Recover after", "Release is shown as Recover after");
            set (*p, id::liveNotches, 1.0f);
            render (*p, Stereo ((size_t) defaultBlock));   // parameters reach the engine in processBlock
            check (p->getEngine().getSettings().hold, "Hold On keeps the floating notches (was Auto-recover Off)");
        }

        // 1. Recovery time: a quiet floating notch steps back to 0 dB and is freed at the expected
        //    time, slowly (in stages, never faster than planned); a fixed notch is untouched.
        for (const auto speed : { 2, 1 })
        {
            auto p = makeProcessor();
            set (*p, id::release, 2.0f);
            set (*p, id::recoverSpeed, (float) speed);
            const auto fl = p->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
            const auto fx = p->getEngine().addNotch (2200.0f, 12.0f, 20.0f, true);
            const auto expected = Engine::expectedRecoverySec (12.0f, 2.0f, speed);
            const auto total = expected + 4.0;
            const auto noiseIn = mono (pink ((size_t) (defaultRate * total), -40.0, 43));
            double goneAt = -1.0, firstDrop = -1.0;
            float prevCut = 12.0f, fixedMin = 1000.0f, fixedMax = 0.0f, maxStepPerHalfSec = 0.0f;
            int plateaus = 0, reonsets = 0;
            float lastCut = 12.0f;
            double lastChange = 0.0;
            bool monotone = true;
            const auto chunk = (size_t) (0.05 * defaultRate);
            std::vector<std::pair<double, float>> trace;
            for (size_t pos = 0; pos < noiseIn.size(); pos += chunk)
            {
                Stereo part (std::min (chunk, noiseIn.size() - pos));
                std::copy (noiseIn.l.begin() + (long) pos, noiseIn.l.begin() + (long) (pos + part.size()), part.l.begin());
                part.r = part.l;
                render (*p, part);
                const auto now = (double) (pos + part.size()) / defaultRate;
                const auto f = notchById (*p, fl);
                const auto x = notchById (*p, fx);
                fixedMin = std::min (fixedMin, x.cutDb); fixedMax = std::max (fixedMax, x.cutDb);
                if (f.active)
                {
                    reonsets = f.reonsets;
                    if (f.cutDb > prevCut + 0.01f) monotone = false;
                    if (firstDrop < 0.0 && f.cutDb < 11.9f) firstDrop = now;
                    if (std::abs (f.cutDb - lastCut) > 0.01f)
                    {
                        if (now - lastChange > 1.0) ++plateaus;   // held still for over a second between steps
                        lastChange = now;
                        lastCut = f.cutDb;
                    }
                    prevCut = f.cutDb;
                    trace.push_back ({ now, f.cutDb });
                }
                else if (goneAt < 0.0)
                    goneAt = now;
            }
            for (size_t i = 0; i < trace.size(); ++i)
                for (size_t j = i + 1; j < trace.size() && trace[j].first - trace[i].first <= 0.5 + 1.0e-6; ++j)
                    maxStepPerHalfSec = std::max (maxStepPerHalfSec, trace[i].second - trace[j].second);
            const auto name = juce::String (recoverSpeedNames()[speed]).toStdString();
            note (name + ": 12 dB floating notch, Recover after 2 s: first step at " + fmt (firstDrop, 2) + " s, freed at " + fmt (goneAt, 2)
                  + " s (expected " + fmt (expected, 2) + " s), " + std::to_string (plateaus) + " probe plateaus, largest drop in 0.5 s "
                  + db (maxStepPerHalfSec) + ", fixed notch " + fmt (fixedMin, 2) + " - " + fmt (fixedMax, 2) + " dB");
            check (firstDrop >= 2.0 && firstDrop < 2.6, name + ": nothing moves before Recover after (" + fmt (firstDrop, 2) + " s)");
            check (goneAt > 0.0 && std::abs (goneAt - expected) < 0.5, name + ": the notch is back to 0 dB and freed at the expected time (" + fmt (goneAt, 2) + " vs " + fmt (expected, 2) + " s)");
            check (monotone && reonsets == 0, name + ": steady noise never looks like a re-onset (the cut only goes down)");
            check (plateaus >= 3, name + ": it recovers in probed stages (" + std::to_string (plateaus) + " plateaus)");
            check (maxStepPerHalfSec <= 0.5f * Engine::recoverGlideDbPerSec[speed] + 0.1f, name + ": slowly (" + db (maxStepPerHalfSec) + " per 0.5 s)");
            check (fixedMin > 11.99f && fixedMax < 12.01f, name + ": the fixed notch keeps exactly its cut");
        }

        // Ring-out notches are fixed and Auto-recover Off holds floating ones: neither recovers.
        {
            auto p = makeProcessor();
            set (*p, id::ringOut, 1.0f);
            set (*p, id::release, 2.0f);
            set (*p, id::recoverSpeed, 2.0f);
            LoopSpec spec; spec.seconds = 3.0;
            runLoop (p.get(), spec);
            const auto before = p->getNotches();
            render (*p, mono (pink ((size_t) (defaultRate * 30.0), -40.0, 44)));
            const auto after = p->getNotches();
            bool same = activeCount (before) > 0 && activeCount (before) == activeCount (after);
            for (size_t i = 0; i < before.size(); ++i)
                if (before[i].active) same = same && after[i].active && std::abs (after[i].cutDb - before[i].cutDb) < 0.01f;
            check (same, "ring-out notches never auto-recover (" + notchList (before) + " -> " + notchList (after) + ")");

            auto h = makeProcessor();
            set (*h, id::liveNotches, 1.0f);
            set (*h, id::release, 2.0f);
            set (*h, id::recoverSpeed, 2.0f);
            h->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
            render (*h, mono (pink ((size_t) (defaultRate * 30.0), -40.0, 45)));
            const auto kept = h->getNotches();
            float cut = 0.0f;
            for (const auto& n : kept) if (n.active) cut = n.cutDb;
            check (activeCount (kept) == 1 && cut > 11.99f, "Auto-recover Off: the floating notch keeps its cut (" + notchList (kept) + ")");
        }

        // 2. No clicks: a tone right at the notch (detection on a quiet reference mic) comes back up
        //    through every stage as a smooth ramp.
        {
            auto p = makeProcessor (defaultRate, defaultBlock, true);
            set (*p, id::sidechain, 1.0f);
            set (*p, id::release, 1.0f);
            set (*p, id::recoverSpeed, 2.0f);
            const auto nid = p->getEngine().addNotch (1000.0f, 12.0f, 20.0f, false);
            const auto seconds = (double) Engine::expectedRecoverySec (12.0f, 1.0f, 2) + 4.0;
            Stereo tone ((size_t) (defaultRate * seconds));
            addSine (tone.l, defaultRate, 1000.0, 0.25f); tone.r = tone.l;
            const auto ref = mono (pink (tone.size(), -50.0, 46));
            const auto out = render (*p, tone, &ref);
            double jump = 0.0, worstEnvStep = 0.0;
            for (size_t i = 1; i < out.size(); ++i) jump = std::max (jump, (double) std::abs (std::abs (out.l[i]) - std::abs (out.l[i - 1])));
            const auto win = (size_t) (0.005 * defaultRate);
            const auto settle = (size_t) (0.5 * defaultRate);   // after addNotch's own glide in
            double prev = rmsDb (out.l, settle, win), worstAt = 0.0;
            for (size_t i = settle + win; i + win <= out.size(); i += win)
            {
                const auto e = rmsDb (out.l, i, win);
                if (std::abs (e - prev) > worstEnvStep) { worstEnvStep = std::abs (e - prev); worstAt = (double) i / defaultRate; }
                prev = e;
            }
            if (std::getenv ("DOOBLRFEEDBACK_RECOVER_TRACE") != nullptr)
            {
                std::string tr;
                for (size_t i = 0; i + win <= out.size(); i += win * 20)
                    tr += fmt ((double) i / defaultRate, 2) + ":" + fmt (rmsDb (out.l, i, win) - rmsDb (tone.l, i, win), 2) + " ";
                note ("tone trace " + tr);
                auto q = makeProcessor (defaultRate, defaultBlock, true);
                set (*q, id::sidechain, 1.0f);
                set (*q, id::release, 1.0f);
                set (*q, id::recoverSpeed, 2.0f);
                q->getEngine().addNotch (1000.0f, 12.0f, 20.0f, false);
                const auto chunk = (size_t) (0.05 * defaultRate);
                std::string nt;
                for (size_t i = 0; i + chunk <= (size_t) (2.5 * defaultRate); i += chunk)
                {
                    Stereo a (std::vector<float> (tone.l.begin() + (long) i, tone.l.begin() + (long) (i + chunk)), std::vector<float> (tone.l.begin() + (long) i, tone.l.begin() + (long) (i + chunk)));
                    Stereo b (std::vector<float> (ref.l.begin() + (long) i, ref.l.begin() + (long) (i + chunk)), std::vector<float> (ref.r.begin() + (long) i, ref.r.begin() + (long) (i + chunk)));
                    render (*q, a, &b);
                    for (const auto& x : q->getNotches())
                        if (x.active) nt += fmt ((double) (i + chunk) / defaultRate, 2) + ":" + fmt (x.freq, 2) + "/" + fmt (x.cutDb, 2) + "/" + fmt (x.q, 2) + (x.recovering ? "R" : "") + (x.ringing ? "!" : "") + " ";
                }
                note ("tone notch trace " + nt);
            }
            const auto startDb = rmsDb (out.l, (size_t) (0.5 * defaultRate), 4800) - rmsDb (tone.l, (size_t) (0.5 * defaultRate), 4800);
            const auto endDb = rmsDb (out.l, out.size() - 9600, 9600) - rmsDb (tone.l, tone.size() - 9600, 9600);
            const auto maxStep = 0.25 * twoPi * 1000.0 / defaultRate + 0.002;
            note ("a 1 kHz tone under a recovering 1 kHz notch (Fast): " + db (startDb) + " -> " + db (endDb) + ", largest sample step "
                  + fmt (jump, 4) + " (a clean sine: " + fmt (0.25 * twoPi * 1000.0 / defaultRate, 4) + "), largest 5 ms level step " + fmt (worstEnvStep, 3) + " dB at " + fmt (worstAt, 2) + " s");
            const auto left = notchById (*p, nid);
            check (startDb < -9.0 && endDb > -0.5 && ! left.active, "the tone goes from under the notch back to full level as the notch is freed ("
                   + db (startDb) + " -> " + db (endDb) + (left.active ? ", still there at -" + fmt (left.cutDb, 2) + " dB, re-onsets " + std::to_string (left.reonsets) : std::string (", freed")) + ")");
            check (jump < maxStep, "no clicks while recovering (largest step " + fmt (jump, 4) + ")");
            check (worstEnvStep < 0.1, "every recovery step is a smooth ramp (" + fmt (worstEnvStep, 3) + " dB per 5 ms)");
            check (finiteAndNormal (out.l), "no NaN / denormal while recovering");
        }

        // 3. Re-onset: a howl is caught (loop gain 1.3 at 2 kHz), the loop is broken so the notch
        //    starts recovering, then a hotter loop (2.5 = +8 dB) comes back mid-recovery. The cut
        //    goes straight back (stage start + margin), there is no runaway, and the notch's next
        //    recovery is slower.
        {
            auto p = makeProcessor();
            set (*p, id::release, 2.0f);
            set (*p, id::recoverSpeed, 2.0f);
            double backAt = -1.0, caughtAt = -1.0;
            int nid = 0;
            float cutBefore = 0.0f, cutCaught = 0.0f, scale = 1.0f, stageStart = 0.0f;
            int reonsets = 0;
            double quietSince = -1.0;
            auto gainAt = [&] (double t) { return t < 3.0 ? 1.3 : (backAt < 0.0 ? 0.0 : 2.5); };
            const auto mic = runScheduledLoop (*p, 16.0, 2000.0, gainAt, [&] (double t)
            {
                if (nid == 0)
                    for (const auto& n : p->getNotches()) if (n.active) { nid = n.id; break; }
                if (nid == 0) return;
                const auto n = notchById (*p, nid);
                if (! n.active) return;
                if (t > 3.0 && backAt < 0.0)
                {
                    if (quietSince < 0.0) quietSince = t;
                    // Bring the hot loop back once the notch is probing at least 3 dB shallower.
                    if (n.recovering && n.cutDb <= n.stageTargetDb + 0.01f && n.stageStartDb - n.cutDb >= 2.9f && n.cutDb < 8.0f)
                    { backAt = t; cutBefore = n.cutDb; stageStart = n.stageStartDb; }
                }
                if (backAt > 0.0 && caughtAt < 0.0 && n.reonsets > 0) { caughtAt = t; cutCaught = n.cutDb; }
                reonsets = n.reonsets;
                scale = n.recoverScale;
            }, -40.0, 40.0);   // one loop mode (Q 40): at Q 8 a 2.5 loop gain also runs away at 1.9 and 2.1 kHz
            const auto env = envelopeDb (mic, defaultRate);
            const auto backPeak = backAt > 0.0 ? maxOf (env, (size_t) (backAt * 100.0)) : 0.0;
            const auto tail = rmsDb (mic, mic.size() - 96000, 96000);
            const auto fin = notchById (*p, nid);
            note ("re-onset: hot loop back at " + fmt (backAt, 2) + " s with the notch at -" + fmt (cutBefore, 1) + " dB (stage from -" + fmt (stageStart, 1)
                  + "); caught " + fmt ((caughtAt - backAt) * 1000.0, 0) + " ms later at -" + fmt (cutCaught, 1) + " dB; peak 10 ms RMS after "
                  + db (backPeak) + ", last 2 s " + db (tail) + "; re-onsets " + std::to_string (reonsets) + ", recovery scale " + fmt (scale, 0)
                  + "; notches " + notchList (p->getNotches()));
            check (backAt > 0.0, "the notch started recovering after the loop was broken");
            check (caughtAt > 0.0 && caughtAt - backAt < 0.5, "the re-onset is caught within 0.5 s");
            check (cutCaught >= stageStart + Engine::recoverMarginDb - 0.01f || cutCaught >= get (*p, id::maxCut) - 0.01f,
                   "the cut goes straight back to the stage start plus the margin (" + db (cutCaught) + ")");
            check (backPeak < -40.0 + 20.0, "no runaway: the howl never gets 20 dB over the noise (" + db (backPeak + 40.0) + ")");
            // Reference: the same hot loop (2.5) on a fresh plug-in, no recovery involved.
            double freshTail = 0.0;
            {
                auto f = makeProcessor();
                const auto m = runScheduledLoop (*f, 8.0, 2000.0, [] (double) { return 2.5; }, [] (double) {}, -40.0, 40.0);
                freshTail = rmsDb (m, m.size() - 96000, 96000);
            }
            note ("  the same hot loop on a fresh plug-in: last 2 s " + db (freshTail + 40.0) + " over the noise");
            check (tail < std::max (-40.0 + 3.0, freshTail + 1.5), "held afterwards as well as a fresh catch holds it (" + db (tail + 40.0) + " vs " + db (freshTail + 40.0) + " over the noise)");
            // Once the howl is back under the bell the region can already be quiet, so the next slow
            // release may have started. The doubled scale and the cut both have to still be there.
            check (fin.active && fin.recoverScale >= 2.0f && fin.cutDb >= 8.0f,
                   "the notch stays, and its next recovery is slower (scale " + fmt (fin.recoverScale, 0) + ", cut " + fmt (fin.cutDb, 1) + ")");
            check (activeCount (p->getNotches()) <= 2, "no pile of new notches (" + notchList (p->getNotches()) + ")");
            check (finiteAndNormal (mic), "no NaN / denormal");
        }

        // 4. Safety duck releases in probed steps: a runaway the notches cannot hold (Max cut 3 dB,
        //    loop gain 1.6 at 2 kHz), loop broken at 2.5 s; the duck's own gain reduction comes
        //    back in three steps with pauses, smoothly, and lets go.
        {
            auto p = makeProcessor();
            set (*p, id::maxCut, 3.0f);
            std::vector<std::pair<double, float>> red;   // (time, reduction dB) after every block
            runScheduledLoop (*p, 7.0, 2000.0, [] (double t) { return t < 2.5 ? 1.6 : 0.0; },
                              [&] (double t) { red.push_back ({ t, p->getEngine().getSafety().reductionDb }); });
            // The release that matters is the last one: before 2.5 s the duck and the howl take
            // turns (ducking breaks the loop, the howl regrows as the gain comes back).
            size_t last = 0;
            for (size_t i = 1; i < red.size(); ++i)
                if (red[i].second > red[i - 1].second + 0.001f) last = i;
            const auto startRed = red.empty() ? 0.0f : red[last].second;
            const auto releaseStart = red.empty() ? -1.0 : red[last].first;
            const auto blockSec = (double) defaultBlock / defaultRate;
            float worst = 0.0f;
            int pauses = 0;
            double flatFor = 0.0, freeAt = -1.0;
            for (size_t i = last + 1; i < red.size(); ++i)
            {
                const auto step = red[i - 1].second - red[i].second;   // reduction going down = gain coming back
                worst = std::max (worst, std::abs (step) * (float) (0.005 / blockSec));
                if (std::abs (step) <= 0.001f && red[i].second > 0.3f && red[i].second < startRed - 0.5f)
                {
                    flatFor += blockSec;
                    if (flatFor >= 0.2 && flatFor - blockSec < 0.2) ++pauses;
                }
                else flatFor = 0.0;
                if (red[i].second <= 0.0f && freeAt < 0.0) freeAt = red[i].first;
            }
            float deepest = 0.0f;
            for (const auto& r : red) deepest = std::max (deepest, r.second);
            note ("safety release after the loop is broken (2.5 s): deepest " + db (deepest) + "; last release from " + fmt (releaseStart, 2)
                  + " s at " + db (startRed) + ", back to 0 dB at " + fmt (freeAt, 2) + " s (Recover 1.5 s), " + std::to_string (pauses)
                  + " probe pause(s), worst step " + fmt (worst, 3) + " dB per 5 ms");
            if (std::getenv ("DOOBLRFEEDBACK_RECOVER_TRACE") != nullptr)
            {
                std::string tr;
                for (size_t i = 0; i < red.size(); i += 4) tr += fmt (red[i].first, 2) + ":" + fmt (red[i].second, 2) + " ";
                note ("safety trace " + tr);
            }
            check (deepest > 6.0f && startRed > 3.0f, "the duck engaged on the runaway (" + db (deepest) + ", last " + db (startRed) + ")");
            check (freeAt - releaseStart > 1.5, "the release takes at least Recover plus the probes");
            check (worst < 0.5f, "the staged duck release is smooth (" + fmt (worst, 3) + " dB per 5 ms)");
            check (pauses == Engine::safetyStages - 1, "the duck pauses to probe before each step back up (" + std::to_string (pauses) + ")");
            check (freeAt > 0.0, "and it does come back all the way");
        }
    }

    void testBrightProgrammeIsLeftAlone()
    {
        const auto n = (size_t) (defaultRate * 8.0);
        std::vector<std::pair<std::string, std::vector<float>>> cases;
        cases.emplace_back ("cymbal crashes", cymbals (n, defaultRate, 61));
        cases.emplace_back ("hi-hats (closed 8ths, open once a bar)", hiHats (n, defaultRate, 62));
        cases.emplace_back ("sibilant speech", sibilantSpeech (n, defaultRate, 63));
        cases.emplace_back ("bright sawtooth chord", brightChord (n, defaultRate, 0.3f, 64));
        cases.emplace_back ("bright strummed guitar", brightGuitar (n, defaultRate, 65));
        for (auto& [name, x] : cases)
        {
            auto peak = 0.0f;
            for (auto s : x) peak = std::max (peak, std::abs (s));
            if (peak > 0.0f) for (auto& s : x) s *= 0.5f / peak; // -6 dBFS peak
            for (const auto aggressive : { false, true })
            {
                auto p = makeProcessor();
                set (*p, id::mode, aggressive ? 1.0f : 0.0f);
                render (*p, mono (x));
                const auto count = activeCount (p->getNotches());
                const auto label = std::string (aggressive ? "aggressive: " : "safe: ") + name;
                note (label + ": " + std::to_string (count) + " notches (" + notchList (p->getNotches()) + ")");
                if (! aggressive) check (count == 0, label + " gets no notches");
            }
        }
    }

    void testParametricBell()
    {
        using Engine = dsp::FeedbackEngine;
        const auto bellQ = Engine::defaultBellQ;
        const auto levelAt = [] (Processor& p, double hz)
        {
            Stereo tone ((size_t) defaultRate);
            addSine (tone.l, defaultRate, hz, 0.5f);
            tone.r = tone.l;
            const auto out = render (p, tone);
            const auto half = (size_t) (defaultRate / 2);
            return rmsDb (out.l, half, half) - rmsDb (tone.l, half, half);
        };

        {
            auto p = makeProcessor();
            set (*p, id::detect, 0.0f);
            auto* bq = param (*p, id::bellQ);
            check (bq != nullptr && bq->getName (64) == "Bell Q" && std::abs (bq->convertFrom0to1 (bq->getValue()) - bellQ) < 0.05f,
                   "Bell Q defaults to 4");
            p->getEngine().addNotch (1000.0f, 8.0f, 20.0f, true);
            render (*p, Stereo ((size_t) 4800));
            const auto centre = levelAt (*p, 1000.0);
            const auto offsetHz = 1125.0;
            const auto offset = levelAt (*p, offsetHz);
            const auto narrow = (double) Engine::notchResponseDb ((float) offsetHz, 1000.0f, 8.0f, 20.0f);
            const auto band = (double) Engine::bandResponseDb ((float) offsetHz, 1000.0f, 8.0f, 20.0f, bellQ);
            note ("8 dB band: centre " + db (centre) + ", " + fmt (offsetHz, 0) + " Hz " + db (offset)
                  + " (bell " + db (band) + ", narrow notch alone " + db (narrow) + ")");
            check (std::abs (centre + 8.0) < 0.6, "an 8 dB band cuts about 8 dB at its centre (" + db (centre) + ")");
            check (offset < narrow - 1.0, "inside the bell is deeper than a Q 20 notch alone (" + db (offset) + " vs " + db (narrow) + ")");
            check (std::abs (offset - band) < 0.5, "the 8 dB offset matches the parametric bell (" + db (offset) + " vs " + db (band) + ")");
        }

        {
            auto p = makeProcessor();
            set (*p, id::detect, 0.0f);
            p->getEngine().addNotch (1000.0f, 18.0f, 20.0f, true);
            render (*p, Stereo ((size_t) 4800));
            const auto centre = levelAt (*p, 1000.0);
            const auto offsetHz = 1050.0;
            const auto offset = levelAt (*p, offsetHz);
            const auto band = (double) Engine::bandResponseDb ((float) offsetHz, 1000.0f, 18.0f, 20.0f, bellQ);
            note ("18 dB band: centre " + db (centre) + ", 1050 Hz " + db (offset) + " (bell plus notch " + db (band) + ")");
            check (std::abs (centre + 18.0) < 0.8, "an 18 dB band is a bell plus a notch, about 18 dB at the centre (" + db (centre) + ")");
            check (std::abs (offset - band) < 0.5, "an 18 dB band matches bell plus notch off centre (" + db (offset) + " vs " + db (band) + ")");
        }
    }

    struct TestCase { const char* name; void (*run)(); };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                            testMetadata },
        { "zero latency, null with no notches",                  testLatencyAndNull },
        { "simulated feedback loop is pulled down",              testFeedbackLoop },
        { "three simultaneous feedback frequencies",             testMultiLoop },
        { "detection speed: a growing howl",                     testDetectionSpeed },
        { "music: chords, vibrato, bass, melody",                testMusicIsLeftAlone },
        { "speech and noise untouched",                          testSpeechAndNoise },
        { "sidechain reference mic",                             testSidechain },
        { "clear, lock, release, hold, ring-out, bank size",     testClearLockRelease },
        { "max cut and no zipper noise",                         testMaxCutAndZipper },
        { "max cut infinity: full notch, ramp, text, old sessions", testInfiniteCut },
        { "state, notes and room presets",                       testStateNotesPresets },
        { "undo and A / B",                                      testUndoAndCompare },
        { "extremes: rates, blocks, NaN / denormals",            testExtremes },
        { "value text",                                          testValueText },
        { "real-time safety and CPU",                            testRealtimeAndCpu },
        { "editor: size, controls, layout, markers, snapshots",  testEditor },
        { "real mouse: every control and marker button",         testRealMouse },
        { "Analyzer Speed setting",                              testAnalyzerSpeed },
        { "high-frequency feedback (3-18 kHz, room, speech, rates, blocks)", testHighFrequencyFeedback },
        { "bright programme: cymbals, hi-hats, sibilance, saw chord, guitar", testBrightProgrammeIsLeftAlone },
        { "one howl, one notch (merge, retune, consolidate)",    testOneNotchPerHowl },
        { "safety duck: last resort, music untouched, recovery", testSafetyDuck },
        { "calm labels: steady lanes, no overlaps, idle labels close", testCalmLabels },
        { "parametric bell plus narrow notch",               testParametricBell },
        { "auto-recover: staged return, re-onset, fixed untouched, no clicks", testAutoRecover },
        { "motion: gliding notch display, slow labels, paint time", testMotionAndPaint },
    };
    const auto filter = juce::SystemStats::getEnvironmentVariable ("DOOBLRFEEDBACK_TEST_FILTER", {});

    int failedTests = 0;
    size_t ran = 0;
    for (const auto& test : tests)
    {
        if (filter.isNotEmpty() && ! juce::String (test.name).containsIgnoreCase (filter)) continue;
        ++ran;
        const auto before = failedChecks;
        std::cout << test.name << "\n" << std::flush;
        test.run();
        const bool ok = failedChecks == before;
        if (! ok) ++failedTests;
        std::cout << "  " << (ok ? "[PASS]" : "[FAIL]") << "\n";
    }

    std::cout << "\n" << (ran - (size_t) failedTests) << " / " << ran << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " failed\n";
    return failedChecks == 0 ? 0 : 1;
}
