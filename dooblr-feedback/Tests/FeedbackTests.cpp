/*
    dooblr-feedback console tests: the real processor, feedback engine and editor.

    Every check prints nothing on success; failures print what was expected. Each test ends
    with "[PASS]" or "[FAIL]" and the summary line counts all checks. Measured numbers are
    printed as notes. Set DOOBLRFEEDBACK_EDITOR_SNAPSHOT=/path/prefix to write editor PNGs.
*/

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
        BandPass (double rate, double freq, double q)
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
        const auto fl = r->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
        const auto fx = r->getEngine().addNotch (2200.0f, 12.0f, 20.0f, true);
        render (*r, mono (pink ((size_t) (defaultRate * 8.0), -40.0, 41)));
        bool floatingGone = true, fixedKept = false;
        for (const auto& n : r->getNotches())
        {
            if (n.active && n.id == fl) floatingGone = false;
            if (n.active && n.id == fx && n.cutDb > 11.9f) fixedKept = true;
        }
        check (floatingGone, "Float: a floating notch releases after quiet (release 2 s, 8 s of noise); left: " + notchList (r->getNotches()));
        check (fixedKept, "a fixed notch keeps its full depth");

        auto h = makeProcessor();
        set (*h, id::release, 2.0f);
        set (*h, id::liveNotches, 1.0f);
        h->getEngine().addNotch (900.0f, 12.0f, 20.0f, false);
        render (*h, mono (pink ((size_t) (defaultRate * 8.0), -40.0, 42)));
        check (activeCount (h->getNotches()) == 1, "Hold: floating notches are kept");

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
        note ("a 3 kHz notch engaging under a 200 Hz sine: max deviation " + fmt (worst, 5) + ", max second-difference deviation " + fmt (worst2, 6));
        check (worst < 0.01 && worst2 < 1.0e-3, "no click when a notch engages");

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
        for (const auto notches : { 0, 24 })
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
            cpu += (cpu.empty() ? "" : ", ") + std::string (notches == 0 ? "detection, no notches " : "24 notches ") + fmt (share, 2) + " %";
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
        explicit EditorFixture (bool scene = true)
        {
            processor = makeProcessor();
            if (scene) addScene (*processor);
            else render (*processor, mono (pink ((size_t) defaultRate, -30.0, 62)));
            base.reset (processor->createEditor());
            editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "the editor is a DooblrFeedbackEditor");
            if (editor != nullptr) settle();
        }
        void settle()
        {
            auto& g = editor->getGraph();
            for (int i = 0; i < 60; ++i) { g.poll(); g.advanceDisplay (1.0 / 60.0); }
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
            for (auto pid : { id::detect, id::ringOut, id::mode, id::sensitivity, id::sidechain, id::rangeLow, id::rangeHigh, id::maxCut, id::notchQ, id::liveNotches, id::release, id::output, id::bypass })
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

    void testAnalyzerSpeed()
    {
        EditorFixture f (false);
        if (f.editor == nullptr) return;
        auto& box = f.editor->getAnalyzerSpeedBox();
        check (box.getNumItems() == ui::numAnalyzerSpeeds, "Analyzer Speed lists " + std::to_string (ui::numAnalyzerSpeeds) + " speeds");
        check (f.processor->getAnalyzerSpeed() == (int) ui::defaultAnalyzerSpeed, "default Analyzer Speed is Medium");
        box.setSelectedItemIndex (0, juce::sendNotificationSync);
        check (f.processor->getAnalyzerSpeed() == 0, "choosing Slow sets the processor's speed");
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
        { "state, notes and room presets",                       testStateNotesPresets },
        { "undo and A / B",                                      testUndoAndCompare },
        { "extremes: rates, blocks, NaN / denormals",            testExtremes },
        { "value text",                                          testValueText },
        { "real-time safety and CPU",                            testRealtimeAndCpu },
        { "editor: size, controls, layout, markers, snapshots",  testEditor },
        { "real mouse: every control and marker button",         testRealMouse },
        { "Analyzer Speed setting",                              testAnalyzerSpeed },
    };

    int failedTests = 0;
    for (const auto& test : tests)
    {
        const auto before = failedChecks;
        std::cout << test.name << "\n" << std::flush;
        test.run();
        const bool ok = failedChecks == before;
        if (! ok) ++failedTests;
        std::cout << "  " << (ok ? "[PASS]" : "[FAIL]") << "\n";
    }

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << " / " << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " failed\n";
    return failedChecks == 0 ? 0 : 1;
}
