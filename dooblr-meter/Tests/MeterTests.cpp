/*
    dooblr-meter console tests: the real processor, analyzer, DSP and editor.

    Every check prints nothing on success; failures print what was expected. Each test ends with
    "[PASS]" or "[FAIL]" and the summary line counts all checks. All test signals are generated
    here (EBU Tech 3341 / 3342 cases are synthesised from their published definitions; no
    third-party files). Set DOOBLRMETER_EDITOR_SNAPSHOT=/path/prefix to write editor PNGs.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "DSP/Analyzer.h"
#include "DSP/Insights.h"
#include "DSP/KWeighting.h"
#include "DSP/Profiles.h"
#include "DSP/Targets.h"
#include "DSP/Translation.h"
#include "DSP/TruePeak.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <new>
#include <random>
#include <set>
#include <sstream>
#include <string>
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
    using namespace dooblrmeter;
    using Processor = DooblrMeterProcessor;
    using Editor = DooblrMeterEditor;
    namespace id = params::id;

    int failedChecks = 0, passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition) { ++passedChecks; return; }
        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n"; }

    // Delivers the same command message Button::triggerClick() posts, synchronously, so no
    // message loop is needed in a console test.
    void pressButton (juce::Button& b)
    {
        static_cast<juce::Component&> (b).handleCommandMessage (0x2f3f4f99);
    }

    std::string fmt (double value, int decimals = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (decimals) << value;
        return s.str();
    }

    constexpr double pi = 3.14159265358979323846;
    double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

    //==============================================================================
    // Signals
    using Generator = std::function<void (float* l, float* r, int n, juce::int64 start)>;

    Generator sine (double sr, double hz, double ampL, double ampR, double phase = 0.0)
    {
        return [=] (float* l, float* r, int n, juce::int64 start)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto v = std::sin (2.0 * pi * hz * (double) (start + i) / sr + phase);
                l[i] = (float) (ampL * v);
                r[i] = (float) (ampR * v);
            }
        };
    }

    /** Piecewise: list of (seconds, generator). */
    Generator sequence (double sr, std::vector<std::pair<double, Generator>> parts)
    {
        return [=] (float* l, float* r, int n, juce::int64 start)
        {
            for (int i = 0; i < n; ++i)
            {
                auto t = (double) (start + i) / sr;
                for (const auto& [len, gen] : parts)
                {
                    if (t < len || &gen == &parts.back().second)
                    {
                        gen (l + i, r + i, 1, start + i);
                        break;
                    }
                    t -= len;
                }
            }
        };
    }

    /** Periodic noise with a curve-shaped long-term spectrum (2^17 samples, dense spectrum). */
    struct ShapedNoise
    {
        std::vector<float> a, b; // two independent noises; L = a + w b, R = a - w b

        ShapedNoise (double sr, int curve, double rmsDb, std::function<double (double)> extraDb = {}, unsigned seed = 7)
        {
            constexpr int order = 17, n = 1 << order;
            juce::dsp::FFT fft (order);
            std::mt19937 rng (seed);
            std::uniform_real_distribution<double> phase (0.0, 2.0 * pi);
            for (int pass = 0; pass < 2; ++pass)
            {
                std::vector<juce::dsp::Complex<float>> in ((size_t) n), out ((size_t) n);
                for (int k = 1; k < n / 2; ++k)
                {
                    const auto hz = k * sr / n;
                    if (hz < 15.0 || hz > 21000.0)
                        continue;
                    double db = (double) dsp::curves::levelDb (curve, hz);
                    if (extraDb)
                        db += extraDb (hz);
                    const auto mag = std::sqrt (std::pow (10.0, db / 10.0) / hz); // band power -> bin power
                    const auto ph = phase (rng);
                    in[(size_t) k] = { (float) (mag * std::cos (ph)), (float) (mag * std::sin (ph)) };
                    in[(size_t) (n - k)] = std::conj (in[(size_t) k]);
                }
                fft.perform (in.data(), out.data(), true);
                auto& dst = pass == 0 ? a : b;
                dst.resize ((size_t) n);
                double sq = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    dst[(size_t) i] = out[(size_t) i].real();
                    sq += (double) dst[(size_t) i] * dst[(size_t) i];
                }
                const auto g = dbToGain (rmsDb) / std::sqrt (sq / n);
                for (auto& v : dst)
                    v = (float) (v * g);
            }
        }

        Generator generator (double width = 0.3, double gain = 1.0) const
        {
            auto self = *this;
            return [self, width, gain] (float* l, float* r, int n, juce::int64 start)
            {
                const auto size = (juce::int64) self.a.size();
                for (int i = 0; i < n; ++i)
                {
                    const auto j = (size_t) ((start + i) % size);
                    l[i] = (float) (gain * (self.a[j] + width * self.b[j]));
                    r[i] = (float) (gain * (self.a[j] - width * self.b[j]));
                }
            };
        }
    };

    Generator add (Generator x, Generator y)
    {
        return [=] (float* l, float* r, int n, juce::int64 start)
        {
            std::vector<float> l2 ((size_t) n), r2 ((size_t) n);
            x (l, r, n, start);
            y (l2.data(), r2.data(), n, start);
            for (int i = 0; i < n; ++i) { l[i] += l2[(size_t) i]; r[i] += r2[(size_t) i]; }
        };
    }

    //==============================================================================
    // Processor rig
    struct FakeHead final : juce::AudioPlayHead
    {
        bool playing = false;
        juce::int64 time = 0;
        double bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setTimeInSamples (time);
            info.setBpm (bpm);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };

    void setParam (Processor& p, const char* pid, float plainValue)
    {
        auto* param = p.getState().getParameter (pid);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    float getParam (Processor& p, const char* pid) { return p.getState().getRawParameterValue (pid)->load(); }

    std::unique_ptr<Processor> makeProcessor (double sr = 48000.0, int block = 512, bool songMode = false, bool mono = false)
    {
        auto p = std::make_unique<Processor>();
        p->setAnalysisThreadEnabled (false);
        if (mono)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::mono());
            layout.outputBuses.add (juce::AudioChannelSet::mono());
            p->setBusesLayout (layout);
        }
        setParam (*p, id::mode, songMode ? 1.0f : 0.0f);
        p->setRateAndBufferSizeDetails (sr, block); // as a host does before prepareToPlay
        p->prepareToPlay (sr, block);
        return p;
    }

    struct RunOptions
    {
        int block = 512;
        bool randomBlocks = false;
        FakeHead* head = nullptr;
        juce::int64 songStart = 0;
        std::vector<float>* outL = nullptr;
        std::vector<float>* outR = nullptr;
        std::vector<float>* inL = nullptr;
        std::vector<float>* inR = nullptr;
    };

    /** Processes `seconds` of the generator, pumping the analyzer every 0.25 s; returns samples. */
    juce::int64 run (Processor& p, const Generator& gen, double seconds, RunOptions o = {}, juce::int64 genStart = 0)
    {
        const auto sr = p.getSampleRate();
        const auto total = (juce::int64) std::llround (seconds * sr);
        const auto channels = p.getTotalNumInputChannels();
        std::mt19937 rng (3);
        std::uniform_int_distribution<int> blocks (1, 2000);
        juce::AudioBuffer<float> buffer (std::max (2, channels), std::max (4096, o.block));
        std::vector<float> l, r;
        juce::MidiBuffer midi;
        juce::int64 done = 0, sincePump = 0;
        if (o.head != nullptr)
        {
            p.setPlayHead (o.head);
            o.head->playing = true;
        }
        while (done < total)
        {
            const auto n = (int) std::min<juce::int64> (total - done, o.randomBlocks ? blocks (rng) : o.block);
            l.resize ((size_t) n);
            r.resize ((size_t) n);
            gen (l.data(), r.data(), n, genStart + done);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, n);
            std::copy (l.begin(), l.end(), view.getWritePointer (0));
            if (channels > 1)
                std::copy (r.begin(), r.end(), view.getWritePointer (1));
            if (o.inL) o.inL->insert (o.inL->end(), view.getReadPointer (0), view.getReadPointer (0) + n);
            if (o.inR && channels > 1) o.inR->insert (o.inR->end(), view.getReadPointer (1), view.getReadPointer (1) + n);
            if (o.head != nullptr)
                o.head->time = o.songStart + done;
            p.processBlock (view, midi);
            if (o.outL) o.outL->insert (o.outL->end(), view.getReadPointer (0), view.getReadPointer (0) + n);
            if (o.outR && channels > 1) o.outR->insert (o.outR->end(), view.getReadPointer (1), view.getReadPointer (1) + n);
            done += n;
            sincePump += n;
            if (sincePump >= (juce::int64) (0.25 * sr))
            {
                p.pumpAnalysis (false);
                sincePump = 0;
            }
        }
        if (o.head != nullptr)
        {
            o.head->playing = false;
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, 64);
            view.clear();
            p.processBlock (view, midi); // flushes the last song slot
            p.setPlayHead (nullptr);
        }
        p.pumpAnalysis (true);
        return done;
    }

    std::shared_ptr<const dsp::Snapshot> snap (Processor& p) { p.pumpAnalysis (true); return p.getAnalyzer().getSnapshot(); }

    bool hasFinding (const dsp::Snapshot& s, const juce::String& key)
    {
        for (const auto& f : s.findings)
            if (f.key == key)
                return true;
        return false;
    }

    const dsp::Finding* findFinding (const dsp::Snapshot& s, const juce::String& key)
    {
        for (const auto& f : s.findings)
            if (f.key == key)
                return &f;
        return nullptr;
    }

    std::string findingKeys (const dsp::Snapshot& s)
    {
        std::string out;
        for (const auto& f : s.findings)
            out += f.key.toStdString() + "(" + dsp::severityName (f.severity).toStdString() + ") ";
        return out.empty() ? "none" : out;
    }

    double loudnessOf (const std::vector<float>& l, const std::vector<float>& r, double sr, size_t from)
    {
        dsp::KWeighting kl, kr;
        kl.prepare (sr);
        kr.prepare (sr);
        double e = 0.0;
        size_t n = 0;
        for (size_t i = 0; i < l.size(); ++i)
        {
            const auto a = kl.process (l[i]), b = kr.process (r[i]);
            if (i >= from) { e += a * a + b * b; ++n; }
        }
        return dsp::meanSquareToLufs (e / (double) std::max<size_t> (1, n));
    }

    //==============================================================================
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-meter", "name is dooblr-meter (got " + p->getName().toStdString() + ")");
        check (! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p->checkBusesLayoutSupported (mono), "mono layout supported");
        const std::vector<std::string> ids { "target", "curve", "listen", "mode", "custom_lufs", "custom_tp", "history_span", "slope",
                                             "averaging", "sensitivity", "mono_audition", "listen_match", "freeze" };
        const auto& params = p->getParameters();
        check ((int) params.size() == (int) ids.size(), "13 parameters (got " + std::to_string (params.size()) + ")");
        for (size_t i = 0; i < ids.size() && i < (size_t) params.size(); ++i)
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[(int) i]);
            check (withId != nullptr && withId->paramID.toStdString() == ids[i], "parameter " + std::to_string (i) + " is " + ids[i]);
            check (! params[(int) i]->isAutomatable(), ids[i] + " is not automatable");
        }
        const auto targetNames = dsp::targets::names().joinIntoString ("|").toLowerCase();
        for (const char* brand : { "spotify", "apple", "youtube", "amazon", "tidal", "deezer", "netflix", "ebu", "atsc" })
            check (! targetNames.contains (brand), std::string ("no service name in target labels: ") + brand);
        check (getParam (*p, id::listen) == 0.0f && getParam (*p, id::monoAudition) == 0.0f, "auditions default to Off");
    }

    void testLatency()
    {
        auto p = makeProcessor();
        check (p->getLatencySamples() == 0, "zero latency");
        check (p->getTailLengthSeconds() == 0.0, "no tail");
    }

    void testPassThrough()
    {
        for (int block : { 1, 64, 512, 3000 })
        {
            auto p = makeProcessor (48000.0, block);
            std::vector<float> inL, inR, outL, outR;
            std::mt19937 rng (11);
            std::normal_distribution<float> dist (0.0f, 0.3f);
            const Generator noise = [&] (float* l, float* r, int n, juce::int64) { for (int i = 0; i < n; ++i) { l[i] = dist (rng); r[i] = dist (rng); } };
            RunOptions o;
            o.block = block;
            o.inL = &inL; o.inR = &inR; o.outL = &outL; o.outR = &outR;
            run (*p, noise, 1.0, o);
            check (inL.size() == 48000 && outL.size() == 48000 && outR.size() == 48000 && std::memcmp (inL.data(), outL.data(), inL.size() * sizeof (float)) == 0
                       && std::memcmp (inR.data(), outR.data(), inR.size() * sizeof (float)) == 0,
                   "Listen Off: output is bit-identical to the input (block " + std::to_string (block) + ")");
        }

        // After a Listen on / off cycle the output is bit-identical again once the fade ends.
        auto p = makeProcessor();
        std::mt19937 rng (12);
        std::normal_distribution<float> dist (0.0f, 0.3f);
        const Generator noise = [&] (float* l, float* r, int n, juce::int64) { for (int i = 0; i < n; ++i) { l[i] = dist (rng); r[i] = dist (rng); } };
        setParam (*p, id::listen, 3.0f);
        run (*p, noise, 0.5);
        setParam (*p, id::listen, 0.0f);
        run (*p, noise, 0.2);
        std::vector<float> inL, inR, outL, outR;
        RunOptions o;
        o.inL = &inL; o.inR = &inR; o.outL = &outL; o.outR = &outR;
        run (*p, noise, 0.5, o);
        check (inL.size() == 24000 && outL.size() == 24000 && outR.size() == 24000
                   && std::memcmp (inL.data(), outL.data(), inL.size() * sizeof (float)) == 0
                   && std::memcmp (inR.data(), outR.data(), inR.size() * sizeof (float)) == 0,
               "after Listen is switched off again the output is bit-identical");
    }

    void testKWeighting()
    {
        dsp::KWeighting k48;
        k48.prepare (48000.0);
        const auto at1k = k48.magnitudeDb (1000.0, 48000.0);
        check (std::abs (at1k - 0.691) < 0.03, "K-weighting gain at 1 kHz is about +0.69 dB (" + fmt (at1k, 3) + ")");
        for (double sr : { 44100.0, 96000.0, 192000.0 })
        {
            dsp::KWeighting k;
            k.prepare (sr);
            double worst = 0.0;
            for (double hz : { 30.0, 100.0, 1000.0, 3000.0, 10000.0 })
                worst = std::max (worst, std::abs (k.magnitudeDb (hz, sr) - k48.magnitudeDb (hz, 48000.0)));
            check (worst < 0.1, "K-weighting at " + fmt (sr / 1000.0, 1) + " kHz matches 48 kHz within 0.1 dB (worst " + fmt (worst, 3) + ")");
        }
    }

    void testEbuTech3341()
    {
        const double sr = 48000.0;
        struct Case { const char* name; std::vector<std::pair<double, double>> parts; double expected; };
        const std::vector<Case> cases {
            { "case 1: 1 kHz -23 dBFS 20 s", { { 20.0, -23.0 } }, -23.0 },
            { "case 2: 1 kHz -33 dBFS 20 s", { { 20.0, -33.0 } }, -33.0 },
            { "case 3: -36 / -23 / -36 (10 / 60 / 10 s)", { { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } }, -23.0 },
            { "case 4: -72 / -36 / -23 / -36 / -72", { { 10.0, -72.0 }, { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 }, { 10.0, -72.0 } }, -23.0 },
            { "case 5: -26 / -20 / -26 (20 / 20.1 / 20 s)", { { 20.0, -26.0 }, { 20.1, -20.0 }, { 20.0, -26.0 } }, -23.0 },
        };
        for (const auto& c : cases)
        {
            auto p = makeProcessor (sr);
            std::vector<std::pair<double, Generator>> parts;
            double length = 0.0;
            for (const auto& [len, db] : c.parts)
            {
                parts.push_back ({ len, sine (sr, 1000.0, dbToGain (db), dbToGain (db)) });
                length += len;
            }
            run (*p, sequence (sr, parts), length);
            const auto s = snap (*p);
            check (s->loudness.hasIntegrated && std::abs (s->loudness.integrated - c.expected) <= 0.1,
                   std::string ("EBU Tech 3341 ") + c.name + ": integrated " + fmt (c.expected, 1) + " +-0.1 (got " + fmt (s->loudness.integrated, 3) + ")");
            if (c.parts.size() == 1)
            {
                check (std::abs (s->momentary - c.expected) <= 0.1 && std::abs (s->shortTerm - c.expected) <= 0.1,
                       std::string (c.name) + ": momentary and short-term " + fmt (c.expected, 1) + " +-0.1 (M " + fmt (s->momentary, 3)
                           + ", S " + fmt (s->shortTerm, 3) + ")");
            }
        }

        for (double rate : { 44100.0, 96000.0 })
        {
            auto p = makeProcessor (rate);
            run (*p, sine (rate, 1000.0, dbToGain (-23.0), dbToGain (-23.0)), 20.0);
            const auto s = snap (*p);
            check (std::abs (s->loudness.integrated + 23.0) <= 0.1,
                   "1 kHz -23 dBFS at " + fmt (rate / 1000.0, 1) + " kHz reads -23.0 +-0.1 (got " + fmt (s->loudness.integrated, 3) + ")");
        }

        // Mono: BS.1770 counts the single channel once (-3 dB against dual mono).
        auto m = makeProcessor (48000.0, 512, false, true);
        run (*m, sine (48000.0, 1000.0, dbToGain (-23.0), dbToGain (-23.0)), 10.0);
        const auto s = snap (*m);
        check (std::abs (s->loudness.integrated + 26.01) <= 0.1, "mono 1 kHz -23 dBFS reads -26.0 LUFS (got " + fmt (s->loudness.integrated, 3) + ")");
    }

    void testEbuTech3342()
    {
        const double sr = 48000.0;
        struct Case { const char* name; std::vector<double> levels; double expected; };
        const std::vector<Case> cases {
            { "case 1: -20 / -30 dBFS", { -20.0, -30.0 }, 10.0 },
            { "case 2: -20 / -15 dBFS", { -20.0, -15.0 }, 5.0 },
            { "case 3: -40 / -20 dBFS", { -40.0, -20.0 }, 20.0 },
            { "case 4: -50 / -35 / -20 / -35 / -50", { -50.0, -35.0, -20.0, -35.0, -50.0 }, 15.0 },
        };
        for (const auto& c : cases)
        {
            auto p = makeProcessor (sr);
            std::vector<std::pair<double, Generator>> parts;
            for (auto db : c.levels)
                parts.push_back ({ 20.0, sine (sr, 1000.0, dbToGain (db), dbToGain (db)) });
            run (*p, sequence (sr, parts), 20.0 * (double) c.levels.size());
            const auto s = snap (*p);
            check (s->loudness.hasLra && std::abs (s->loudness.lra - c.expected) <= 1.0,
                   std::string ("EBU Tech 3342 ") + c.name + ": LRA " + fmt (c.expected, 0) + " +-1 LU (got " + fmt (s->loudness.lra, 2) + ")");
        }
    }

    void testTruePeak()
    {
        const double sr = 48000.0;
        struct Case { const char* name; double hz, amp, phaseDeg, expectedDb; };
        const std::vector<Case> cases {
            { "fs/4, 45 deg, 0.5", 12000.0, 0.5, 45.0, -6.02 },
            { "fs/6, 60 deg, 0.5", 8000.0, 0.5, 60.0, -6.02 },
            { "fs/8, 67.5 deg, 0.5", 6000.0, 0.5, 67.5, -6.02 },
            { "fs/4, 45 deg, 1.41 (+3 dBTP)", 12000.0, 1.41253754, 45.0, 3.0 },
            { "1 kHz, 0.5", 1000.0, 0.5, 0.0, -6.02 },
        };
        for (const auto& c : cases)
        {
            // The vector starts mid-cycle, so its first sample is a step from silence that makes any
            // band-limited interpolator ring. Measure the steady tone: 0.5 s settle, Reset, 2 s.
            auto p = makeProcessor (sr);
            const auto gen = sine (sr, c.hz, c.amp, c.amp, c.phaseDeg * pi / 180.0);
            run (*p, gen, 0.5);
            p->resetMeasurements();
            run (*p, gen, 2.0, {}, (juce::int64) (0.5 * sr));
            const auto s = snap (*p);
            const auto err = s->truePeakMax - c.expectedDb;
            check (err <= 0.2 && err >= -0.4, std::string ("true peak ") + c.name + ": " + fmt (c.expectedDb, 2) + " +0.2/-0.4 dBTP (got "
                                                   + fmt (s->truePeakMax, 3) + ", sample peak " + fmt (s->samplePeakMax, 2) + ")");
        }
        // Sample peak is exact.
        auto p = makeProcessor (sr);
        run (*p, sine (sr, 12000.0, 0.5, 0.5, 45.0 * pi / 180.0), 1.0);
        const auto s = snap (*p);
        check (std::abs (s->samplePeakMax - (float) dsp::gainToDb (0.5 * std::sin (pi / 4))) < 0.01, "sample peak reads the largest sample");

        for (double rate : { 96000.0, 192000.0 })
        {
            dsp::TruePeak tp;
            tp.prepare (rate);
            check (tp.getFactor() == (rate < 176000.0 ? 2 : 1), "oversampling factor at " + fmt (rate / 1000.0, 0) + " kHz keeps the effective rate >= 176.4 kHz");
        }
    }

    void testSongMode()
    {
        const double sr = 48000.0;
        auto p = makeProcessor (sr, 512, true);
        FakeHead head;
        RunOptions o;
        o.head = &head;
        o.songStart = 0;
        run (*p, sine (sr, 1000.0, dbToGain (-20.0), dbToGain (-20.0)), 8.0, o);
        const auto first = snap (*p);
        run (*p, sine (sr, 1000.0, dbToGain (-20.0), dbToGain (-20.0)), 8.0, o); // replay bars 1-4
        const auto second = snap (*p);
        check (std::abs (second->analysedSeconds - first->analysedSeconds) < 0.05,
               "Song mode: replaying a section replaces it (" + fmt (first->analysedSeconds) + " s then " + fmt (second->analysedSeconds) + " s)");
        check (std::abs (second->loudness.integrated - first->loudness.integrated) < 0.01, "Song mode: integrated unchanged by the replay");

        o.songStart = (juce::int64) (8.0 * sr);
        run (*p, sine (sr, 1000.0, dbToGain (-30.0), dbToGain (-30.0)), 8.0, o);
        const auto third = snap (*p);
        check (std::abs (third->analysedSeconds - 16.0) < 0.1, "Song mode: the next section adds to the song (" + fmt (third->analysedSeconds) + " s)");
        check (third->loudness.integrated < first->loudness.integrated - 1.0, "Song mode: quieter second half lowers the integrated loudness");

        // Live mode counts every pass, in playing order.
        setParam (*p, id::mode, 0.0f);
        const auto live = snap (*p);
        check (live->analysedSeconds > 23.5, "Live mode counts all three passes (" + fmt (live->analysedSeconds) + " s)");

        // Not playing: Song mode records nothing.
        auto q = makeProcessor (sr, 512, true);
        run (*q, sine (sr, 1000.0, 0.1, 0.1), 3.0);
        check (snap (*q)->analysedSeconds == 0.0, "Song mode with the transport stopped measures no programme time");
        check (snap (*q)->shortTerm > -30.0f, "...but the live readouts still move");

        // Bars in finding spans use the host tempo.
        check (dsp::formatSpan (96.0, 128.0, 120.0, 4.0, true).startsWith ("bars 49"), "formatSpan reports bars at 120 BPM 4/4 ("
                                                                                       + dsp::formatSpan (96.0, 128.0, 120.0, 4.0, true).toStdString() + ")");
    }

    void testBlockSizeIndependence()
    {
        const double sr = 48000.0;
        ShapedNoise noise (sr, 0, -18.0);
        const auto gen = add (noise.generator(), sine (sr, 997.0, 0.2, 0.15));
        std::vector<double> integrated, lra, tp;
        const std::vector<int> sizes { 1, 17, 64, 512, 4096, -1 };
        for (int size : sizes)
        {
            auto p = makeProcessor (sr, size > 0 ? size : 2048);
            RunOptions o;
            o.block = size > 0 ? size : 2048;
            o.randomBlocks = size < 0;
            run (*p, gen, 12.0, o);
            const auto s = snap (*p);
            integrated.push_back (s->loudness.integrated);
            lra.push_back (s->loudness.lra);
            tp.push_back (s->truePeakMax);
        }
        bool same = true;
        for (size_t i = 1; i < integrated.size(); ++i)
            same = same && std::abs (integrated[i] - integrated[0]) < 1.0e-6 && std::abs (lra[i] - lra[0]) < 1.0e-6 && std::abs (tp[i] - tp[0]) < 1.0e-6;
        check (same, "integrated, LRA and true peak are identical for block sizes 1, 17, 64, 512, 4096 and random (I " + fmt (integrated[0], 3) + ")");
    }

    void testListen()
    {
        const double sr = 48000.0;
        const auto levelOf = [] (const std::vector<float>& v, size_t from)
        {
            double sq = 0.0;
            for (size_t i = from; i < v.size(); ++i) sq += (double) v[i] * v[i];
            return dsp::powerToDb (sq / (double) std::max<size_t> (1, v.size() - from));
        };

        // Phone, no level match: 50 Hz vanishes, 2 kHz passes.
        for (auto [hz, expectAbove, expectBelow] : { std::tuple<double, double, double> { 50.0, -200.0, -40.0 }, { 2000.0, -9.0, 3.0 } })
        {
            auto p = makeProcessor (sr);
            setParam (*p, id::listen, 1.0f + (float) dsp::profiles::phone);
            setParam (*p, id::listenMatch, 0.0f);
            std::vector<float> outL, inL;
            RunOptions o;
            o.outL = &outL; o.inL = &inL;
            run (*p, sine (sr, hz, 0.5, 0.5), 2.0, o);
            const auto change = levelOf (outL, (size_t) sr) - levelOf (inL, (size_t) sr);
            check (change > expectAbove && change < expectBelow, "Phone audition at " + fmt (hz, 0) + " Hz changes the level by " + fmt (change, 1) + " dB");
        }

        // Level match keeps a full-range mix near its loudness.
        {
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -20.0);
            setParam (*p, id::listen, 1.0f + (float) dsp::profiles::laptop);
            std::vector<float> inL, inR, outL, outR;
            RunOptions o;
            o.inL = &inL; o.inR = &inR; o.outL = &outL; o.outR = &outR;
            run (*p, noise.generator(), 10.0, o);
            const auto diff = loudnessOf (outL, outR, sr, (size_t) (6 * sr)) - loudnessOf (inL, inR, sr, (size_t) (6 * sr));
            check (std::abs (diff) < 1.5, "Laptop audition, level matched: loudness within 1.5 dB of the dry mix (" + fmt (diff, 2) + " dB)");
            check (p->getActiveListenProfile() == dsp::profiles::laptop, "the processor reports the active audition");
        }

        // Every profile is stable and finite on a loud signal.
        for (int prof = 0; prof < dsp::profiles::count; ++prof)
        {
            auto p = makeProcessor (sr);
            setParam (*p, id::listen, 1.0f + (float) prof);
            std::vector<float> outL;
            RunOptions o;
            o.outL = &outL;
            ShapedNoise noise (sr, 3, -6.0);
            run (*p, noise.generator(), 2.0, o);
            bool finite = true;
            float peak = 0.0f;
            for (auto v : outL) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            check (finite && peak < 16.0f, std::string ("audition ") + dsp::profiles::get (prof).name + " is finite and bounded (peak " + fmt (peak, 2) + ")");
        }

        // Mono audition: left-only input comes out on both channels at half level.
        {
            auto p = makeProcessor (sr);
            setParam (*p, id::monoAudition, 1.0f);
            std::vector<float> outL, outR;
            RunOptions o;
            o.outL = &outL; o.outR = &outR;
            run (*p, sine (sr, 440.0, 0.8, 0.0), 0.5, o);
            const auto i = outL.size() - 10;
            check (std::abs (outL[i] - outR[i]) < 1.0e-6 && std::abs (levelOf (outL, outL.size() / 2) - dsp::gainToDb (0.4 / std::sqrt (2.0))) < 0.2,
                   "Mono audition sums to mono");
        }

        // Switching between profiles crossfades: no sample-to-sample jump larger than the signal allows.
        {
            auto p = makeProcessor (sr);
            std::vector<float> outL;
            RunOptions o;
            o.outL = &outL;
            const auto gen = sine (sr, 300.0, 0.5, 0.5);
            setParam (*p, id::listen, 1.0f + (float) dsp::profiles::car);
            run (*p, gen, 0.5, o, 0);
            setParam (*p, id::listen, 1.0f + (float) dsp::profiles::tv);
            run (*p, gen, 0.5, o, (juce::int64) (0.5 * sr));
            setParam (*p, id::listen, 0.0f);
            run (*p, gen, 0.5, o, (juce::int64) sr);
            float worst = 0.0f;
            for (size_t i = 1; i < outL.size(); ++i)
                worst = std::max (worst, std::abs (outL[i] - outL[i - 1]));
            const auto natural = (float) (2.0 * 2.0 * pi * 300.0 / sr); // max slope of a level-2 sine
            check (worst < natural, "profile switches crossfade without clicks (largest step " + fmt (worst, 4) + ")");
        }
    }

    void testAuditionsLoadOff()
    {
        auto p = makeProcessor();
        setParam (*p, id::listen, 4.0f);
        setParam (*p, id::monoAudition, 1.0f);
        setParam (*p, id::target, 5.0f);
        setParam (*p, id::curve, 2.0f);
        juce::MemoryBlock data;
        p->getStateInformation (data);
        auto q = makeProcessor();
        q->setStateInformation (data.getData(), (int) data.getSize());
        check (getParam (*q, id::listen) == 0.0f, "Listen loads Off even when saved on");
        check (getParam (*q, id::monoAudition) == 0.0f, "Mono loads Off even when saved on");
        check (getParam (*q, id::target) == 5.0f && getParam (*q, id::curve) == 2.0f, "other settings are restored");
    }

    void testStatePersistence()
    {
        const double sr = 48000.0;
        auto p = makeProcessor (sr, 512, true);
        FakeHead head;
        RunOptions o;
        o.head = &head;
        ShapedNoise noise (sr, 0, -20.0);
        run (*p, noise.generator(), 12.0, o);
        const auto before = snap (*p);
        juce::MemoryBlock data;
        p->getStateInformation (data);
        note ("saved state with 12 s of Song analysis: " + std::to_string (data.getSize()) + " bytes");

        auto q = makeProcessor (sr, 512, true);
        q->setStateInformation (data.getData(), (int) data.getSize());
        const auto after = snap (*q);
        check (after->loudness.hasIntegrated && std::abs (after->loudness.integrated - before->loudness.integrated) < 0.02,
               "integrated loudness survives save / load (" + fmt (before->loudness.integrated, 3) + " -> " + fmt (after->loudness.integrated, 3) + ")");
        check (std::abs (after->analysedSeconds - before->analysedSeconds) < 0.05, "measured time survives save / load");
        check (std::abs (after->truePeakMax - before->truePeakMax) < 0.01, "true peak survives save / load");
        check (after->findings.size() == before->findings.size(), "findings re-derive identically after load");

        // Reset clears.
        q->resetMeasurements();
        check (snap (*q)->analysedSeconds == 0.0, "Reset clears the measurements");
    }

    void testDelivery()
    {
        using dsp::targets::get;
        using dsp::targets::predictedGainDb;
        const auto near = [] (float a, float b) { return std::abs (a - b) < 0.01f; };
        check (near (predictedGainDb (get (0), -8.6f, 0.4f), -5.4f), "Streaming -14: -8.6 LUFS master is turned down 5.4 dB");
        check (near (predictedGainDb (get (1), -8.6f, 0.4f), -7.4f), "Streaming -16: -7.4 dB");
        check (near (predictedGainDb (get (2), -8.6f, 0.4f), -5.4f), "Video -14: -5.4 dB");
        check (near (predictedGainDb (get (5), -8.6f, 0.4f), -14.4f), "Broadcast EU -23: -14.4 dB");
        check (near (predictedGainDb (get (6), -8.6f, 0.4f), -15.4f), "Broadcast US -24: -15.4 dB");
        check (near (predictedGainDb (get (7), -8.6f, 0.4f), 0.0f), "Club: no normalisation");
        check (near (predictedGainDb (get (0), -20.0f, -6.0f), 5.0f), "Streaming -14 turns a quiet master up only to the -1 dBTP ceiling (+5 dB)");
        check (near (predictedGainDb (get (2), -20.0f, -6.0f), 0.0f), "Video -14 never turns up");
        check (near (predictedGainDb (get (8, -18.0f, -2.0f), -20.0f, -6.0f), 2.0f), "Custom uses the custom loudness");
    }

    void testTranslation()
    {
        const double sr = 48000.0;
        {
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -20.0);
            run (*p, noise.generator(), 12.0);
            const auto s = snap (*p);
            check (s->translationValid, "translation scores exist after 12 s");
            std::string scores;
            bool inRange = true;
            for (int i = 0; i < dsp::profiles::count; ++i)
            {
                const auto& t = s->translation[(size_t) i];
                inRange = inRange && t.score >= 0 && t.score <= 100 && ! t.reasons.isEmpty();
                scores += std::string (dsp::profiles::get (i).shortName) + " " + std::to_string (t.score) + ", ";
            }
            note ("curve-matched mix: " + scores);
            check (inRange, "every score is 0..100 with at least one reason");
            check (s->translation[dsp::profiles::headphones].score >= 80, "a curve-matched mix scores 80+ on headphones");
            check (s->translation[dsp::profiles::club].score >= 70, "a curve-matched mix scores 70+ on a club system");
            check (s->translation[dsp::profiles::phone].score < s->translation[dsp::profiles::headphones].score, "phone scores below headphones");
        }
        {
            auto p = makeProcessor (sr);
            run (*p, sine (sr, 35.0, 0.3, 0.3), 12.0);
            const auto s = snap (*p);
            const auto& phone = s->translation[dsp::profiles::phone];
            check (phone.score < 40, "35 Hz sub only: phone score below 40 (got " + std::to_string (phone.score) + ")");
            check (phone.reasons.joinIntoString (" ").containsIgnoreCase ("vanish") || phone.reasons.joinIntoString (" ").containsIgnoreCase ("disappear"),
                   "...and the reason says the low end vanishes (" + phone.reasons.joinIntoString (" | ").toStdString() + ")");
            check (phone.lowEndLostPercent > 80.0f, "...with most of the low end lost (" + fmt (phone.lowEndLostPercent, 0) + "%)");
        }
        {
            // Out-of-phase low end loses level on mono systems.
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -22.0);
            run (*p, add (noise.generator(), sine (sr, 60.0, 0.25, -0.25)), 12.0);
            const auto s = snap (*p);
            check (s->translation[dsp::profiles::bluetooth].monoLossDb < -1.0f,
                   "anti-phase 60 Hz: the Bluetooth model loses level in mono (" + fmt (s->translation[dsp::profiles::bluetooth].monoLossDb, 2) + " dB)");
        }
    }

    void testInsights()
    {
        const double sr = 48000.0;
        const std::set<juce::String> tonal { "harsh", "mud", "boom", "thin", "rumble", "dull", "resonance" };

        {
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -20.0);
            run (*p, noise.generator(), 14.0);
            const auto s = snap (*p);
            bool anyFix = false, anyTonal = false;
            for (const auto& f : s->findings)
            {
                anyFix = anyFix || f.severity == dsp::Finding::Severity::fix;
                anyTonal = anyTonal || tonal.count (f.key) > 0;
            }
            check (! anyFix && ! anyTonal, "a clean curve-matched mix gets no Fix and no tonal findings (" + findingKeys (*s) + ")");
        }
        {
            auto p = makeProcessor (sr);
            ShapedNoise harsh (sr, 0, -20.0, [] (double hz) { return hz > 2300.0 && hz < 5400.0 ? 6.0 : 0.0; });
            run (*p, harsh.generator(), 14.0);
            const auto s = snap (*p);
            const auto* f = findFinding (*s, "harsh");
            check (f != nullptr, "+6 dB at 2.3-5.4 kHz raises a harshness finding (" + findingKeys (*s) + ")");
            if (f != nullptr)
            {
                note ("finding: " + f->title.toStdString() + " | " + f->detail.toStdString() + " | Try: " + f->fix.toStdString());
                check (f->fLo > 2000.0f && f->fHi < 6000.0f, "the harshness finding names the 2-6 kHz band");
            }
        }
        {
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -24.0);
            run (*p, add (noise.generator(), sine (sr, 60.0, 0.3, -0.3)), 12.0);
            const auto s = snap (*p);
            check (hasFinding (*s, "phase0") || hasFinding (*s, "mono"), "anti-phase low end raises a phase / mono finding (" + findingKeys (*s) + ")");
        }
        {
            auto p = makeProcessor (sr);
            ShapedNoise noise (sr, 0, -20.0);
            const Generator dc = [] (float* l, float* r, int n, juce::int64) { for (int i = 0; i < n; ++i) { l[i] = 0.01f; r[i] = 0.0f; } };
            run (*p, add (noise.generator(), dc), 10.0);
            check (hasFinding (*snap (*p), "dc"), "a 0.01 DC offset raises a DC finding (" + findingKeys (*snap (*p)) + ")");
        }
        {
            auto p = makeProcessor (sr);
            const Generator clipped = [sr] (float* l, float* r, int n, juce::int64 start)
            {
                for (int i = 0; i < n; ++i)
                    l[i] = r[i] = (float) juce::jlimit (-1.0, 1.0, 2.0 * std::sin (2.0 * pi * 220.0 * (double) (start + i) / sr));
            };
            run (*p, clipped, 10.0);
            const auto s = snap (*p);
            check (hasFinding (*s, "clip") && hasFinding (*s, "truepeak"), "a hard-clipped sine raises clip and true-peak findings (" + findingKeys (*s) + ")");
            check (s->clipRuns > 100, "clip runs are counted (" + std::to_string (s->clipRuns) + ")");
        }
        {
            auto p = makeProcessor (sr);
            ShapedNoise loud (sr, 0, -8.0);
            run (*p, loud.generator(), 12.0);
            const auto s = snap (*p);
            const auto* f = findFinding (*s, "loudness");
            check (f != nullptr && f->severity == dsp::Finding::Severity::watch, "a loud master raises a 'louder than target' finding (" + findingKeys (*s) + ")");
        }
        {
            // Sibilant bursts: short 7 kHz noise bursts over a quiet bed.
            auto p = makeProcessor (sr);
            ShapedNoise bed (sr, 0, -30.0);
            ShapedNoise hiss (sr, 0, -8.0, [] (double hz) { return hz > 5500.0 && hz < 9000.0 ? 30.0 : -60.0; }, 9);
            const auto bedGen = bed.generator();
            const auto hissGen = hiss.generator (0.0);
            const Generator gen = [=] (float* l, float* r, int n, juce::int64 start)
            {
                bedGen (l, r, n, start);
                std::vector<float> hl ((size_t) n), hr ((size_t) n);
                hissGen (hl.data(), hr.data(), n, start);
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (start + i) % (juce::int64) (0.75 * sr);
                    if (t < (juce::int64) (0.04 * sr)) { l[i] += hl[(size_t) i]; r[i] += hr[(size_t) i]; }
                }
            };
            run (*p, gen, 12.0);
            check (hasFinding (*snap (*p), "sibilance"), "sharp 6-9 kHz bursts raise a sibilance finding (" + findingKeys (*snap (*p)) + ")");
        }
    }

    /** Every parameter changes what the meter measures, shows or outputs. */
    void testNoDeadParameters()
    {
        const double sr = 48000.0;
        ShapedNoise harsh (sr, 0, -11.0, [] (double hz) { return hz > 2300.0 && hz < 5400.0 ? 3.6 : 0.0; });
        auto p = makeProcessor (sr);
        run (*p, harsh.generator(), 12.0);

        const auto keys = [&] { return findingKeys (*snap (*p)); };
        const auto baseKeys = keys();

        setParam (*p, id::target, 5.0f); // Broadcast EU -23
        const auto broadcastKeys = keys();
        check (broadcastKeys != baseKeys || findFinding (*snap (*p), "loudness")->severity == dsp::Finding::Severity::fix, "target changes the findings");
        setParam (*p, id::target, 8.0f); // Custom
        // A custom loudness at the measured level (within the 1 LU spec window), then 8 LU away.
        const auto measured = (float) snap (*p)->loudness.integrated;
        const auto atLevel = juce::jlimit (-30.0f, -5.0f, std::round (measured * 2.0f) / 2.0f);
        const auto away = atLevel > -17.0f ? atLevel - 8.0f : atLevel + 8.0f;
        setParam (*p, id::customLufs, atLevel);
        const auto customA = findFinding (*snap (*p), "loudness") != nullptr;
        setParam (*p, id::customLufs, away);
        const auto customB = findFinding (*snap (*p), "loudness") != nullptr;
        check (! customA && customB, "custom_lufs changes the loudness finding (measured " + fmt (measured, 1) + ", custom " + fmt (atLevel, 1)
                                         + " then " + fmt (away, 1) + ")");
        setParam (*p, id::customTp, -3.0f);
        const auto oversA = snap (*p)->overRuns;
        setParam (*p, id::customTp, 0.0f);
        const auto oversB = snap (*p)->overRuns;
        check (oversA != oversB, "custom_tp changes the true-peak over count (" + std::to_string (oversA) + " vs " + std::to_string (oversB) + ")");
        setParam (*p, id::target, 0.0f);

        const auto curveA = snap (*p)->curveDb;
        setParam (*p, id::curve, 2.0f);
        check (snap (*p)->curveDb != curveA, "curve changes the reference curve");
        setParam (*p, id::curve, 0.0f);

        setParam (*p, id::sensitivity, 0.0f);
        const auto lenient = snap (*p)->findings.size();
        setParam (*p, id::sensitivity, 2.0f);
        const auto strict = snap (*p)->findings.size();
        check (strict > lenient, "sensitivity changes how much is flagged (" + std::to_string (lenient) + " vs " + std::to_string (strict) + ")");
        setParam (*p, id::sensitivity, 1.0f);

        setParam (*p, id::historySpan, 0.0f);
        const auto spanA = snap (*p)->historyT1 - snap (*p)->historyT0;
        setParam (*p, id::historySpan, 3.0f);
        const auto spanB = snap (*p)->historyT1 - snap (*p)->historyT0;
        check (std::abs (spanA - 30.0) < 0.01 && std::abs (spanB - 600.0) < 0.01, "history_span sets the history window");

        setParam (*p, id::mode, 1.0f);
        const auto songTime = snap (*p)->analysedSeconds;
        setParam (*p, id::mode, 0.0f);
        check (songTime == 0.0 && snap (*p)->analysedSeconds > 11.0, "mode switches between the Song and Live measurements");

        // averaging: after a level drop, Fast follows sooner than Slow.
        {
            float fastDrop = 0.0f, slowDrop = 0.0f;
            for (int avg : { 0, 2 })
            {
                auto q = makeProcessor (sr);
                setParam (*q, id::averaging, (float) avg);
                ShapedNoise loud (sr, 0, -12.0), quiet (sr, 0, -36.0);
                run (*q, loud.generator(), 4.0);
                const auto before = snap (*q)->liveSpectrum[120];
                run (*q, quiet.generator(), 0.6);
                const auto after = snap (*q)->liveSpectrum[120];
                (avg == 0 ? fastDrop : slowDrop) = before - after;
            }
            check (fastDrop > slowDrop + 3.0f, "averaging changes how fast the live spectrum follows (" + fmt (fastDrop, 1) + " vs " + fmt (slowDrop, 1) + " dB)");
        }

        // freeze: live spectrum and scope stop moving.
        {
            auto q = makeProcessor (sr);
            ShapedNoise loud (sr, 0, -12.0), quiet (sr, 0, -36.0);
            run (*q, loud.generator(), 3.0);
            setParam (*q, id::freeze, 1.0f);
            const auto before = snap (*q);
            run (*q, quiet.generator(), 2.0);
            const auto after = snap (*q);
            check (before->liveSpectrum == after->liveSpectrum && before->scope == after->scope, "freeze holds the live spectrum and the vectorscope");
            check (after->analysedSeconds > before->analysedSeconds, "...while measuring continues");
        }

        // listen_match: the audition level depends on it.
        {
            float levels[2] {};
            for (int match : { 0, 1 })
            {
                auto q = makeProcessor (sr);
                setParam (*q, id::listen, 1.0f + (float) dsp::profiles::phone);
                setParam (*q, id::listenMatch, (float) match);
                std::vector<float> outL, outR;
                RunOptions o;
                o.outL = &outL; o.outR = &outR;
                ShapedNoise noise (sr, 0, -20.0);
                run (*q, noise.generator(), 6.0, o);
                levels[match] = (float) loudnessOf (outL, outR, sr, (size_t) (4 * sr));
            }
            check (std::abs (levels[1] - levels[0]) > 2.0f, "listen_match changes the audition level (" + fmt (levels[0], 1) + " vs " + fmt (levels[1], 1) + " LUFS)");
        }
        // listen and mono_audition change the output: covered in "Listen auditions". slope: covered in the editor test.
    }

    void testRealtimeAndCpu()
    {
        const double sr = 48000.0;
        ShapedNoise noise (sr, 0, -14.0);
        const auto gen = noise.generator();
        for (int listen : { 0, 1 + dsp::profiles::phone })
        {
            auto p = makeProcessor (sr);
            setParam (*p, id::listen, (float) listen);
            run (*p, gen, 0.5); // settle
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            allocationCount = 0;
            countAllocations = true;
            for (int b = 0; b < 200; ++b)
            {
                gen (buffer.getWritePointer (0), buffer.getWritePointer (1), 512, b * 512);
                p->processBlock (buffer, midi);
            }
            countAllocations = false;
            check (allocationCount.load() == 0, std::string ("processBlock never allocates (Listen ") + (listen ? "Phone" : "Off") + ", "
                                                    + std::to_string (allocationCount.load()) + " allocations)");
        }

        // CPU: 60 s of stereo audio in 512-sample blocks, Listen off and on (budget from the concept:
        // audio thread < 0.3% of a core, < 0.5% with an audition on; analysis thread < 3%).
        for (int listen : { 0, 1 + dsp::profiles::car })
        {
            auto p = makeProcessor (sr);
            setParam (*p, id::listen, (float) listen);
            juce::AudioBuffer<float> buffer (2, 512);
            std::vector<float> l (512), r (512);
            juce::MidiBuffer midi;
            double audioSeconds = 0.0, pumpSeconds = 0.0;
            const int blocks = (int) (60.0 * sr / 512);
            for (int b = 0; b < blocks; ++b)
            {
                gen (l.data(), r.data(), 512, (juce::int64) b * 512);
                buffer.copyFrom (0, 0, l.data(), 512);
                buffer.copyFrom (1, 0, r.data(), 512);
                const auto t0 = std::chrono::steady_clock::now();
                p->processBlock (buffer, midi);
                const auto t1 = std::chrono::steady_clock::now();
                audioSeconds += std::chrono::duration<double> (t1 - t0).count();
                if (b % 3 == 0) // ~30 analysis passes per second, as the thread runs
                {
                    p->pumpAnalysis (false);
                    pumpSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
                }
            }
            const auto audioPct = 100.0 * audioSeconds / 60.0, pumpPct = 100.0 * pumpSeconds / 60.0;
            const std::string what = listen ? "Listen Car" : "Listen Off";
            const double budget = listen ? 0.5 : 0.3;
            note (what + ": audio thread " + fmt (audioPct, 3) + "% of real time; analysis thread " + fmt (pumpPct, 2) + "% (60 s, 48 kHz stereo, 512 blocks)");
            check (audioPct < budget, what + ": audio thread under " + fmt (budget, 1) + "% of one core (" + fmt (audioPct, 3) + "%)");
            check (pumpPct < 3.0, what + ": analysis thread under 3% of one core (" + fmt (pumpPct, 2) + "%)");
            check (p->getAnalyzer().droppedChunkCount() == 0, what + ": no analysis audio dropped while the analysis keeps up");
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
        const auto* prefix = std::getenv ("DOOBLRMETER_EDITOR_SNAPSHOT");
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

    void testEditor()
    {
        const double sr = 48000.0;
        auto p = makeProcessor (sr);
        // A mix-like programme with something to find: harsh top, a little wide sub, a few overs.
        ShapedNoise mix (sr, 0, -14.5, [] (double hz) { return (hz > 2300.0 && hz < 5400.0 ? 5.0 : 0.0) + (hz < 35.0 ? 6.0 : 0.0); });
        run (*p, add (mix.generator (0.35), sine (sr, 55.0, 0.12, 0.05)), 40.0);
        p->pumpAnalysis (true);

        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "createEditor returns the dooblr-meter editor");
        if (editor == nullptr)
            return;
        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080x660");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600 && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400,
                   "resize limits 940x600 .. 2400x1400");

        const auto layoutChecks = [&] (const std::string& where)
        {
            // Everything lives on one surface that fills the window (scaled up above 1080x660).
            juce::Component* surface = nullptr;
            for (auto* c : editor->getChildren())
                if (c->getComponentID() == "surface")
                    surface = c;
            check (surface != nullptr, where + ": the editor has its surface");
            if (surface == nullptr)
                return;
            const auto covered = editor->getLocalArea (surface, surface->getLocalBounds());
            check (std::abs (covered.getWidth() - editor->getWidth()) <= 2 && std::abs (covered.getHeight() - editor->getHeight()) <= 2
                       && covered.getX() == 0 && covered.getY() == 0,
                   where + ": the surface fills the window (" + covered.toString().toStdString() + ")");
            check (surface->getWidth() >= 940 && surface->getHeight() >= 600 && surface->getWidth() <= 1240 && surface->getHeight() <= 700,
                   where + ": layout size stays within the designed range (" + surface->getLocalBounds().toString().toStdString() + ")");
            const auto bounds = surface->getLocalBounds();
            std::vector<juce::Component*> visible;
            for (auto* c : surface->getChildren())
                if (c->isVisible() && dynamic_cast<juce::TooltipWindow*> (c) == nullptr)
                    visible.push_back (c);
            bool inside = true, noOverlap = true;
            std::string offenders;
            for (auto* c : visible)
            {
                if (! bounds.contains (c->getBounds()) || c->getWidth() < 8 || c->getHeight() < 8)
                {
                    inside = false;
                    offenders += c->getComponentID().toStdString() + " " + c->getBounds().toString().toStdString() + "; ";
                }
            }
            for (size_t i = 0; i < visible.size(); ++i)
                for (size_t j = i + 1; j < visible.size(); ++j)
                    if (visible[i]->getBounds().intersects (visible[j]->getBounds()))
                    {
                        // Captions sit above their control; labels never overlap other controls.
                        noOverlap = false;
                        offenders += "[" + visible[i]->getComponentID().toStdString() + " " + visible[i]->getBounds().toString().toStdString() + " x "
                                   + visible[j]->getComponentID().toStdString() + " " + visible[j]->getBounds().toString().toStdString() + "] ";
                    }
            check (inside, where + ": every visible component is inside the window and usable " + offenders);
            check (noOverlap, where + ": no visible components overlap " + offenders);
        };

        for (auto size : { juce::Point<int> (940, 600), juce::Point<int> (1080, 660), juce::Point<int> (2400, 1400) })
        {
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            editor->setSize (size.x, size.y);
            editor->showPage (0);
            editor->refreshNow();
            layoutChecks ("Main " + label);
            saveSnapshot (*editor, "-" + juce::String (label));
            editor->showPage (1);
            editor->refreshNow();
            layoutChecks ("Advanced " + label);
            if (size.x == 1080)
                saveSnapshot (*editor, "-" + juce::String (label) + "-advanced");
        }
        editor->setSize (1080, 660);
        editor->showPage (0);
        editor->refreshNow();

        // Translation tiles toggle Listen.
        juce::Component* tiles = nullptr;
        forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == "translation") tiles = &c; });
        if (auto* tv = dynamic_cast<ui::TranslationView*> (tiles))
        {
            const auto centre = tv->tileBounds (dsp::profiles::phone).getCentre();
            const auto click = [&] {
                juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, tv, tv,
                                    juce::Time::getCurrentTime(), centre.toFloat(), juce::Time::getCurrentTime(), 1, false);
                tv->mouseUp (e);
            };
            click();
            check (juce::roundToInt (getParam (*p, id::listen)) == 1 + dsp::profiles::phone, "clicking the Phone tile starts the Phone audition");
            editor->refreshNow();
            layoutChecks ("Main 1080x660 while listening");
            editor->setSize (940, 600);
            editor->refreshNow();
            layoutChecks ("Main 940x600 while listening");
            editor->setSize (1080, 660);
            editor->refreshNow();
            click();
            check (getParam (*p, id::listen) == 0.0f, "clicking it again stops the audition");
        }
        else
            check (false, "translation view found");

        // Page tabs, mode buttons and Reset.
        juce::Button* advancedTab = nullptr;
        juce::Button* liveButton = nullptr;
        juce::Button* songButton = nullptr;
        juce::Button* reset = nullptr;
        forEachComponent (*editor, [&] (juce::Component& c)
        {
            if (c.getComponentID() == "pageAdvanced") advancedTab = dynamic_cast<juce::Button*> (&c);
            if (c.getComponentID() == "modeLive") liveButton = dynamic_cast<juce::Button*> (&c);
            if (c.getComponentID() == "modeSong") songButton = dynamic_cast<juce::Button*> (&c);
            if (c.getComponentID() == "reset") reset = dynamic_cast<juce::Button*> (&c);
        });
        if (advancedTab && liveButton && songButton && reset)
        {
            pressButton (*advancedTab);
                        check (editor->getPage() == 1, "the Advanced tab opens the Advanced page");
            pressButton (*songButton);
                        check (juce::roundToInt (getParam (*p, id::mode)) == 1, "the Song button selects Song mode");
            pressButton (*liveButton);
                        check (juce::roundToInt (getParam (*p, id::mode)) == 0, "the Live button selects Live mode");
            pressButton (*reset);
                        check (snap (*p)->analysedSeconds == 0.0, "the Reset button clears the measurements");
            editor->showPage (0);
        }
        else
            check (false, "tabs, mode buttons and Reset found");

        // slope: the spectrum display changes with it.
        run (*p, add (mix.generator (0.35), sine (sr, 55.0, 0.12, 0.05)), 10.0);
        juce::Component* spectrumView = nullptr;
        forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == "spectrum") spectrumView = &c; });
        if (spectrumView != nullptr)
        {
            setParam (*p, id::freeze, 1.0f);
            p->pumpAnalysis (true);
            setParam (*p, id::slope, 0.0f);
            editor->refreshNow();
            const auto a = spectrumView->createComponentSnapshot (spectrumView->getLocalBounds());
            setParam (*p, id::slope, 2.0f);
            editor->refreshNow();
            const auto b = spectrumView->createComponentSnapshot (spectrumView->getLocalBounds());
            int differing = 0;
            for (int y = 0; y < a.getHeight(); y += 2)
                for (int x = 0; x < a.getWidth(); x += 2)
                    differing += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
            check (differing > 100, "slope changes the spectrum display (" + std::to_string (differing) + " pixels differ)");
            setParam (*p, id::freeze, 0.0f);
        }

        // Findings list: there is something to show and a click selects it.
        juce::Component* findingsView = nullptr;
        forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == "findings") findingsView = &c; });
        if (auto* fv = dynamic_cast<ui::FindingsView*> (findingsView))
            check (fv->getNumRows() > 0, "the findings list shows findings for the test mix (" + std::to_string (fv->getNumRows()) + ")");
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };
}

int main()
{
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata and parameters",                          testMetadata },
        { "zero latency",                                     testLatency },
        { "bit-exact pass-through with Listen off",           testPassThrough },
        { "K-weighting across sample rates",                  testKWeighting },
        { "EBU Tech 3341 loudness cases",                     testEbuTech3341 },
        { "EBU Tech 3342 loudness range cases",               testEbuTech3342 },
        { "true-peak vectors",                                testTruePeak },
        { "Song mode by position",                            testSongMode },
        { "block-size independence",                          testBlockSizeIndependence },
        { "Listen auditions",                                 testListen },
        { "auditions always load Off",                        testAuditionsLoadOff },
        { "measurements persist with the session",            testStatePersistence },
        { "delivery-target gain predictions",                 testDelivery },
        { "translation scores",                               testTranslation },
        { "rule-based findings",                              testInsights },
        { "no dead parameters",                               testNoDeadParameters },
        { "real-time safety and CPU budget",                  testRealtimeAndCpu },
        { "editor: sizes, layout, controls, snapshots",       testEditor },
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
