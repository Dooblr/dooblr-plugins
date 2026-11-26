/*
    dooblr-reverb console tests: the real processor, reverb engine and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRREVERB_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/ReverbEngine.h"
#include "DSP/RoomModels.h"
#include "UI/ReverbDisplay.h"
#include "UI/Theme.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <random>
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
    using namespace dooblrreverb;
    using namespace dooblrreverb::params;
    using Processor = DooblrReverbProcessor;
    using Editor = DooblrReverbEditor;
    using Engine = dsp::ReverbEngine;
    using Layer = ui::ReverbDisplay::Layer;

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

    std::vector<float> noise (size_t n, float level, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-level, level);
        std::vector<float> v (n);
        for (auto& x : v) x = d (rng);
        return v;
    }

    struct Stereo
    {
        std::vector<float> l, r;
        size_t size() const noexcept { return l.size(); }
    };

    /** Runs the processor over the input (zero padded to totalSamples) in host-sized blocks. */
    Stereo render (Processor& p, const std::vector<float>& inL, const std::vector<float>& inR, size_t totalSamples, int block = defaultBlock)
    {
        Stereo out { std::vector<float> (totalSamples), std::vector<float> (totalSamples) };
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos < totalSamples; pos += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, totalSamples - pos);
            buffer.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const auto k = pos + (size_t) i;
                buffer.setSample (0, i, k < inL.size() ? inL[k] : 0.0f);
                buffer.setSample (1, i, k < inR.size() ? inR[k] : 0.0f);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
            {
                out.l[pos + (size_t) i] = buffer.getSample (0, i);
                out.r[pos + (size_t) i] = buffer.getSample (1, i);
            }
        }
        return out;
    }

    std::vector<float> impulse (size_t n = 1)
    {
        std::vector<float> v (n, 0.0f);
        v[0] = 1.0f;
        return v;
    }

    /** Octave band pass (two cascaded RBJ band passes) around centre. */
    std::vector<float> octaveBand (const std::vector<float>& x, double centre, double rate)
    {
        auto y = x;
        for (int stage = 0; stage < 2; ++stage)
        {
            auto c = juce::IIRCoefficients::makeBandPass (rate, centre, 1.41);
            juce::IIRFilter f;
            f.setCoefficients (c);
            f.processSamples (y.data(), (int) y.size());
        }
        return y;
    }

    /** RT60 from the Schroeder decay of a (band-passed) impulse response: T20 x 3, from the
        -5 dB to the -25 dB points, with the noise-free tail of the synthetic response. */
    double measureRt60 (const std::vector<float>& ir, double rate)
    {
        std::vector<double> edc (ir.size() + 1, 0.0);
        for (size_t i = ir.size(); i-- > 0;)
            edc[i] = edc[i + 1] + (double) ir[i] * ir[i];
        if (edc[0] <= 0.0) return 0.0;
        // Linear fit of the dB decay between -5 and -25 dB.
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            const auto db = 10.0 * std::log10 (std::max (1.0e-30, edc[i] / edc[0]));
            if (db > -5.0) continue;
            if (db < -25.0) break;
            const auto t = (double) i / rate;
            sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
        }
        if (n < 10) return 0.0;
        const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    double rms (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
    {
        to = std::min (to, v.size());
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
        return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
    }

    bool allFinite (const Stereo& s, float limit, float& peak)
    {
        peak = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i])) return false;
            peak = std::max ({ peak, std::abs (s.l[i]), std::abs (s.r[i]) });
        }
        return peak < limit;
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
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr;
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
        const auto* prefix = std::getenv ("DOOBLRREVERB_EDITOR_SNAPSHOT");
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
        check (p->getName() == "dooblr-reverb", "name is dooblr-reverb (got " + p->getName().toStdString() + ")");
        check (! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRREVERB_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.1.0", "version 0.1.0");
        check (p->getBypassParameter() == p->getState().getParameter (id::bypass), "the host bypass is the Bypass parameter");
        check (p->getTailLengthSeconds() > 0.5, "reports a tail (" + fmt (p->getTailLengthSeconds(), 2) + " s)");
        check (p->getNumPrograms() == (int) presets::factory().size(), "factory presets exposed as programs");
    }

    void testLatency()
    {
        auto p = makeProcessor();
        check (p->getLatencySamples() == 0, "zero latency reported");
        set (*p, id::mix, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        auto out = render (*p, impulse(), impulse(), 64);
        check (std::abs (out.l[0] - 1.0f) < 1.0e-6f, "the dry impulse comes out at sample 0 (no latency)");
    }

    void testNulls()
    {
        const auto in = noise (24000, 0.4f, 3), inR = noise (24000, 0.4f, 4);
        {
            auto p = makeProcessor();
            set (*p, id::mix, 0.0f);
            p->prepareToPlay (defaultRate, defaultBlock);
            auto out = render (*p, in, inR, in.size());
            double worst = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
                worst = std::max ({ worst, (double) std::abs (out.l[i] - in[i]), (double) std::abs (out.r[i] - inR[i]) });
            check (worst < 1.0e-6, "Mix 0 % passes the input unchanged (max error " + fmt (worst, 9) + ")");
        }
        {
            auto p = makeProcessor();
            set (*p, id::bypass, 1.0f);
            p->prepareToPlay (defaultRate, defaultBlock);
            auto out = render (*p, in, inR, in.size());
            double worst = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
                worst = std::max ({ worst, (double) std::abs (out.l[i] - in[i]), (double) std::abs (out.r[i] - inR[i]) });
            check (worst < 1.0e-6, "Bypass passes the input unchanged (max error " + fmt (worst, 9) + ")");
        }
        {
            auto p = makeProcessor();
            set (*p, id::mix, 100.0f);
            auto out = render (*p, std::vector<float> (2000, 0.0f), std::vector<float> (2000, 0.0f), 48000);
            check (rms (out.l) == 0.0 && rms (out.r) == 0.0, "silence in, silence out");
        }
    }

    double bandRt (Processor& p, double centre, double seconds)
    {
        const auto total = (size_t) (seconds * defaultRate);
        auto out = render (p, impulse(), impulse(), total);
        std::vector<float> mono (total);
        for (size_t i = 0; i < total; ++i) mono[i] = 0.5f * (out.l[i] + out.r[i]);
        return measureRt60 (octaveBand (mono, centre, defaultRate), defaultRate);
    }

    std::unique_ptr<Processor> wetProcessor (float space)
    {
        auto p = makeProcessor();
        set (*p, id::mix, 100.0f);
        set (*p, id::space, space);
        set (*p, eqActive (0), 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock); // snap the smoothing to the new settings
        return p;
    }

    void testDecayTimes()
    {
        for (float space : { 20.0f, 45.0f, 75.0f })
        {
            auto p = wetProcessor (space);
            const auto target = Engine::targetDecaySeconds (p->readSettings(), 1000.0);
            const auto measured = bandRt (*p, 1000.0, std::min (14.0, 3.0 * target + 1.0));
            check (measured > 0.7 * target && measured < 1.3 * target,
                   "Space " + fmt (space, 0) + " %: RT60 at 1 kHz " + fmt (measured, 2) + " s matches the target " + fmt (target, 2) + " s");
            note ("Space " + fmt (space, 0) + " % (" + dsp::roomNameAt (space).toStdString() + "): readout " + fmt (Engine::decaySecondsFor (space, 100.0f), 2)
                  + " s, target at 1 kHz " + fmt (target, 2) + " s, measured " + fmt (measured, 2) + " s");
        }

        auto a = wetProcessor (40.0f);
        auto b = wetProcessor (40.0f);
        set (*b, id::decayRate, 200.0f);
        b->prepareToPlay (defaultRate, defaultBlock);
        const auto ra = bandRt (*a, 1000.0, 8.0), rb = bandRt (*b, 1000.0, 10.0);
        check (rb / ra > 1.6 && rb / ra < 2.4, "Decay Rate 200 % doubles the decay (" + fmt (ra, 2) + " s -> " + fmt (rb, 2) + " s)");

        auto c = wetProcessor (40.0f);
        set (*c, decayActive (0), 1.0f);
        set (*c, decayShape (0), (float) (int) DecayShape::lowShelf);
        set (*c, decayFreq (0), 300.0f);
        set (*c, decayRateId (0), 300.0f);
        c->prepareToPlay (defaultRate, defaultBlock);
        auto base = wetProcessor (40.0f);
        const auto lowBase = bandRt (*base, 125.0, 10.0), lowShelf = bandRt (*c, 125.0, 14.0);
        check (lowShelf / lowBase > 1.8, "a 300 % low-shelf decay band lengthens the lows (" + fmt (lowBase, 2) + " s -> " + fmt (lowShelf, 2) + " s at 125 Hz)");
        auto c2 = wetProcessor (40.0f);
        set (*c2, decayActive (0), 1.0f);
        set (*c2, decayShape (0), (float) (int) DecayShape::lowShelf);
        set (*c2, decayFreq (0), 300.0f);
        set (*c2, decayRateId (0), 300.0f);
        c2->prepareToPlay (defaultRate, defaultBlock);
        auto base2 = wetProcessor (40.0f);
        const auto midBase = bandRt (*base2, 2000.0, 8.0), midShelf = bandRt (*c2, 2000.0, 8.0);
        check (std::abs (midShelf / midBase - 1.0) < 0.2, "and leaves 2 kHz about the same (" + fmt (midBase, 2) + " s -> " + fmt (midShelf, 2) + " s)");

        auto dark = wetProcessor (50.0f), bright = wetProcessor (50.0f);
        set (*dark, id::brightness, 0.0f);
        set (*bright, id::brightness, 100.0f);
        dark->prepareToPlay (defaultRate, defaultBlock);
        bright->prepareToPlay (defaultRate, defaultBlock);
        const auto hd = bandRt (*dark, 4000.0, 8.0), hb = bandRt (*bright, 4000.0, 8.0);
        check (hd < 0.7 * hb, "Brightness 0 % shortens the highs (" + fmt (hb, 2) + " s -> " + fmt (hd, 2) + " s at 4 kHz)");
    }

    size_t onset (const Stereo& s, float threshold)
    {
        for (size_t i = 0; i < s.size(); ++i)
            if (std::abs (s.l[i]) > threshold || std::abs (s.r[i]) > threshold)
                return i;
        return s.size();
    }

    void testPredelay()
    {
        auto p = wetProcessor (40.0f);
        const auto o0 = onset (render (*p, impulse(), impulse(), 48000), 1.0e-4f);
        auto q = wetProcessor (40.0f);
        set (*q, id::predelay, 120.0f);
        q->prepareToPlay (defaultRate, defaultBlock);
        const auto o1 = onset (render (*q, impulse(), impulse(), 48000), 1.0e-4f);
        check (o0 < (size_t) (0.03 * defaultRate), "with no predelay the reverb starts within 30 ms (" + fmt ((double) o0 / 48.0, 1) + " ms)");
        check (o1 >= (size_t) (0.119 * defaultRate) && o1 < (size_t) (0.15 * defaultRate),
               "Predelay 120 ms delays the start (" + fmt ((double) o1 / 48.0, 1) + " ms)");

        auto s = wetProcessor (40.0f);
        set (*s, id::predelaySync, 1.0f);
        set (*s, id::predelayDiv, (float) divisionNames().indexOf ("1/8"));
        check (std::abs (s->currentPredelayMs() - 250.0) < 0.5, "synced 1/8 at the default 120 BPM is 250 ms (" + fmt (s->currentPredelayMs(), 1) + ")");
        set (*s, id::predelayDiv, (float) divisionNames().indexOf ("1/2"));
        check (s->currentPredelayMs() <= 500.0, "a synced predelay is capped at 500 ms");
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = from; i < to; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
        return ab / std::sqrt (std::max (1.0e-30, aa * bb));
    }

    void testStereo()
    {
        const auto in = noise (24000, 0.3f, 7);
        double mid100 = 0.0;
        for (float width : { 0.0f, 100.0f, 150.0f })
        {
            auto p = wetProcessor (45.0f);
            set (*p, id::width, width);
            p->prepareToPlay (defaultRate, defaultBlock);
            auto out = render (*p, in, in, 96000);
            const auto corr = correlation (out.l, out.r, 24000, 96000);
            std::vector<float> mid (out.size()), side (out.size());
            for (size_t i = 0; i < out.size(); ++i) { mid[i] = out.l[i] + out.r[i]; side[i] = out.l[i] - out.r[i]; }
            if (width == 0.0f)
                check (rms (side) < 1.0e-4 * rms (mid), "Stereo Width 0 % is mono (side / mid " + fmt (rms (side) / rms (mid), 6) + ")");
            else if (width == 100.0f)
                mid100 = rms (mid), check (corr < 0.6, "Stereo Width 100 % gives a wide, decorrelated tail from a mono source (L/R correlation " + fmt (corr, 2) + ")");
            else
                check (rms (mid) > 0.7 * mid100 && rms (mid) < 1.4 * mid100,
                       "Stereo Width 150 % stays mono compatible: the mono sum keeps its level (" + fmt (mid100, 4) + " at 100 %, " + fmt (rms (mid), 4) + " at 150 %)");
        }

        // Mono and mono-to-stereo layouts.
        for (auto layoutIn : { juce::AudioChannelSet::mono() })
            for (auto layoutOut : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
            {
                auto p = std::make_unique<Processor>();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (layoutIn);
                layout.outputBuses.add (layoutOut);
                check (p->setBusesLayout (layout), "layout " + layoutIn.getDescription().toStdString() + " -> " + layoutOut.getDescription().toStdString() + " accepted");
                p->prepareToPlay (defaultRate, defaultBlock);
                set (*p, id::mix, 100.0f);
                juce::AudioBuffer<float> buffer (layoutOut.size(), defaultBlock);
                juce::MidiBuffer midi;
                double energy = 0.0;
                bool finite = true;
                for (int b = 0; b < 40; ++b)
                {
                    buffer.clear();
                    if (b < 4) for (int i = 0; i < defaultBlock; ++i) buffer.setSample (0, i, in[(size_t) (b * defaultBlock + i)]);
                    p->processBlock (buffer, midi);
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        for (int i = 0; i < defaultBlock; ++i) { const auto v = buffer.getSample (ch, i); energy += v * v; finite = finite && std::isfinite (v); }
                }
                check (finite && energy > 1.0e-3, "processes in " + layoutIn.getDescription().toStdString() + " -> " + layoutOut.getDescription().toStdString());
            }
        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::stereo());
        bad.outputBuses.add (juce::AudioChannelSet::mono());
        check (! Processor().checkBusesLayoutSupported (bad), "stereo -> mono is refused");
    }

    void testExtremes()
    {
        const auto in = noise (48000, 0.5f, 11), inR = noise (48000, 0.5f, 12);
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int block : { 1, 37, 512, 2048 })
            {
                if (rate >= 192000.0 && block == 1) continue; // slow, covered at lower rates
                for (int corner = 0; corner < 3; ++corner)
                {
                    auto p = makeProcessor (rate, block);
                    for (auto* raw : p->getParameters())
                        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw); rp != nullptr && rp->getParameterID() != id::bypass)
                        {
                            const auto pid = rp->getParameterID();
                            if (pid == id::output || pid == id::input) continue;
                            if (corner == 1 && pid.startsWithChar ('e') && pid.substring (1, 2).containsOnly ("123456")) continue;
                            rp->setValueNotifyingHost (corner == 0 ? 0.0f : (corner == 1 ? 1.0f : rp->getDefaultValue()));
                        }
                    if (corner == 2) { set (*p, id::space, 100.0f); set (*p, id::decayRate, 200.0f); set (*p, id::character, 100.0f); }
                    const auto seconds = block == 1 ? 0.5 : 2.0;
                    auto out = render (*p, in, inR, (size_t) (seconds * rate), block);
                    float peak = 0.0f;
                    check (allFinite (out, 30.0f, peak), "finite and bounded at " + fmt (rate / 1000.0, 1) + " kHz, block " + std::to_string (block)
                                                          + ", corner " + std::to_string (corner) + " (peak " + fmt (peak, 2) + ")");
                }
            }

        // Longest settings with every decay band boosted: the network must stay stable (guard grid).
        auto p = makeProcessor();
        set (*p, id::mix, 100.0f);
        set (*p, id::space, 100.0f);
        set (*p, id::decayRate, 200.0f);
        set (*p, id::character, 100.0f);
        const float freqs[] { 60.0f, 250.0f, 1000.0f, 3000.0f, 8000.0f, 16000.0f };
        for (int b = 0; b < numDecayBands; ++b)
        {
            set (*p, decayActive (b), 1.0f);
            set (*p, decayShape (b), (float) (int) DecayShape::bell);
            set (*p, decayFreq (b), freqs[b]);
            set (*p, decayRateId (b), 400.0f);
            set (*p, decayQ (b), 0.3f);
        }
        p->prepareToPlay (defaultRate, defaultBlock);
        auto out = render (*p, in, inR, (size_t) (30.0 * defaultRate));
        const auto early = rms (out.l, (size_t) (4.0 * defaultRate), (size_t) (5.0 * defaultRate));
        const auto late = rms (out.l, (size_t) (29.0 * defaultRate), (size_t) (30.0 * defaultRate));
        float peak = 0.0f;
        check (allFinite (out, 30.0f, peak) && late < early, "maximum decay with every band at 400 % still decays (RMS at 4 s " + fmt (early, 5) + ", at 29 s " + fmt (late, 5) + ")");
        check (p->getTailLengthSeconds() <= 60.0, "the tail length stays bounded (" + fmt (p->getTailLengthSeconds(), 1) + " s)");

        // Denormals: after a long silence, the output is exactly silent or tiny, never subnormal.
        auto d = makeProcessor();
        set (*d, id::mix, 100.0f);
        set (*d, id::space, 10.0f);
        d->prepareToPlay (defaultRate, defaultBlock);
        auto tail = render (*d, in, inR, (size_t) (12.0 * defaultRate));
        int subnormal = 0;
        for (size_t i = (size_t) (11.0 * defaultRate); i < tail.size(); ++i)
            if (std::fpclassify (tail.l[i]) == FP_SUBNORMAL || std::fpclassify (tail.r[i]) == FP_SUBNORMAL) ++subnormal;
        check (subnormal == 0, "no subnormal output samples in a long tail (" + std::to_string (subnormal) + ")");
    }

    void testEqExtreme()
    {
        // Every Post EQ parameter at its maximum: six resonant (Q 10) 48 dB high cuts stacked at
        // 20 kHz. Very loud near the top by design, but it must stay finite and stable.
        const auto in = noise (48000, 0.5f, 13);
        for (double rate : { 44100.0, 96000.0 })
        {
            auto p = makeProcessor (rate);
            for (auto* raw : p->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw); rp != nullptr && rp->getParameterID().startsWithChar ('e'))
                    rp->setValueNotifyingHost (1.0f);
            set (*p, id::mix, 100.0f);
            auto out = render (*p, in, in, (size_t) (3.0 * rate));
            float peak = 0.0f;
            const auto finite = allFinite (out, 1.0e7f, peak);
            const auto early = rms (out.l, 0, (size_t) rate), late = rms (out.l, (size_t) (2.0 * rate));
            check (finite && late < early, "Post EQ at its extremes stays finite and decays at " + fmt (rate / 1000.0, 1) + " kHz (peak " + fmt (peak, 1) + ")");
        }
    }

    void testValueText()
    {
        auto p = makeProcessor();
        const auto text = [&] (const juce::String& pid, float v) { auto* q = param (*p, pid); return q->getText (q->convertTo0to1 (v), 32); };
        check (text (id::space, 45.0f).endsWith (" s"), "Space reads in seconds (" + text (id::space, 45.0f).toStdString() + ")");
        check (text (id::predelay, 25.0f).contains ("ms"), "Predelay reads in ms (" + text (id::predelay, 25.0f).toStdString() + ")");
        check (text (id::decayRate, 150.0f).contains ("%"), "Decay Rate reads in % (" + text (id::decayRate, 150.0f).toStdString() + ")");
        check (text (eqFreq (1), 1500.0f).contains ("kHz"), "frequencies read in kHz above 1 kHz (" + text (eqFreq (1), 1500.0f).toStdString() + ")");
        check (! text (id::output, -0.001f).startsWith ("-0.0"), "no -0.0 (" + text (id::output, -0.001f).toStdString() + ")");
        check (! text (id::input, 0.00001f).containsChar ('e'), "no exponent notation");
        auto* space = param (*p, id::space);
        const auto parsed = space->convertFrom0to1 (space->getValueForText ("2.0 s"));
        check (std::abs (spaceToSeconds (parsed) - 2.0) < 0.05, "typing \"2.0 s\" into Space sets 2 s (" + fmt (spaceToSeconds (parsed), 2) + ")");
    }

    void testStateRoundTrip()
    {
        auto a = makeProcessor();
        set (*a, id::space, 72.0f);
        set (*a, id::character, 80.0f);
        set (*a, id::predelaySync, 1.0f);
        set (*a, decayActive (2), 1.0f);
        set (*a, decayRateId (2), 250.0f);
        set (*a, eqShape (3), (float) (int) EqShape::highShelf);
        set (*a, eqGain (3), -7.5f);
        a->setAnalyzerMode (2);
        a->setPresetName ("Round Trip");
        juce::MemoryBlock block;
        a->getStateInformation (block);

        auto b = makeProcessor();
        b->setStateInformation (block.getData(), (int) block.getSize());
        int mismatches = 0;
        for (auto* raw : a->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw))
                if (std::abs (rp->getValue() - param (*b, rp->getParameterID())->getValue()) > 1.0e-6f)
                {
                    ++mismatches;
                    check (false, rp->getParameterID().toStdString() + " restored");
                }
        check (mismatches == 0, "every parameter is restored");
        check (b->getAnalyzerMode() == 2, "the analyzer display mode is restored");
        check (b->getPresetName() == "Round Trip", "the preset name is restored");
        check ((int) b->getState().state.getProperty ("stateVersion") == stateVersion, "the state carries its version");
        b->setStateInformation ("junk", 4);
        check (std::abs (get (*b, id::space) - 72.0f) < 0.01f, "junk state is ignored");
    }

    void testPresets()
    {
        auto p = makeProcessor();
        set (*p, id::mix, 42.0f);
        set (*p, id::output, -3.0f);
        std::set<std::string> names;
        const auto in = noise (12000, 0.3f, 21);
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            names.insert (p->getProgramName (i).toStdString());
            check (p->getPresetName() == p->getProgramName (i), "preset " + p->getProgramName (i).toStdString() + " sets the name");
            check (std::abs (get (*p, id::mix) - 42.0f) < 0.01f && std::abs (get (*p, id::output) + 3.0f) < 0.01f,
                   "preset " + p->getProgramName (i).toStdString() + " keeps Mix and Output");
            auto out = render (*p, in, in, 24000);
            float peak = 0.0f;
            check (allFinite (out, 10.0f, peak), "preset " + p->getProgramName (i).toStdString() + " renders cleanly");
        }
        check (names.size() == presets::factory().size(), "preset names are unique");
        check (p->getProgramName (0) == "Default", "the first preset is Default");
        p->setCurrentProgram (0);
        bool allDefault = true;
        for (auto* raw : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw); rp != nullptr && ! presets::isKeptOnPresetLoad (rp->getParameterID()))
                allDefault = allDefault && std::abs (rp->getValue() - rp->getDefaultValue()) < 1.0e-6f;
        check (allDefault, "Default resets every other parameter to its default");

        // User preset round trip.
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-reverb-test.dreverb");
        set (*p, id::space, 66.0f);
        set (*p, eqActive (4), 1.0f);
        check (p->saveUserPreset (file).startsWith ("Saved"), "a user preset saves");
        set (*p, id::space, 10.0f);
        set (*p, eqActive (4), 0.0f);
        check (p->loadUserPreset (file).startsWith ("Loaded"), "and loads");
        check (std::abs (get (*p, id::space) - 66.0f) < 0.01f && get (*p, eqActive (4)) > 0.5f, "restoring its values");
        file.deleteFile();
        const auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dooblr-reverb-junk.dreverb");
        junk.replaceWithText ("<NotAPreset/>");
        check (p->loadUserPreset (junk).startsWith ("Could not"), "a foreign file is refused");
        junk.deleteFile();
        check (presets::userDirectory().getFullPathName().contains ("dooblr-reverb/Presets"), "user presets live in ~/Music/dooblr-reverb/Presets");
    }

    /** Fresh processor, context and change applied, 1.2 s of output from a 0.3 s noise burst. */
    Stereo renderWith (const std::vector<std::pair<juce::String, float>>& settings)
    {
        auto p = makeProcessor();
        for (auto& [pid, v] : settings) set (*p, pid, v);
        p->prepareToPlay (defaultRate, defaultBlock);
        static const auto inL = noise (14400, 0.3f, 31), inR = noise (14400, 0.3f, 32);
        return render (*p, inL, inR, 57600);
    }

    void testNoDeadParameters()
    {
        auto probe = makeProcessor();
        for (auto* raw : probe->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            std::vector<std::pair<juce::String, float>> context { { id::mix, 60.0f } };
            const auto band = pid.substring (1, 2).getIntValue() - 1;
            if (pid.startsWithChar ('d') && pid != id::decayRate && pid != id::distance && band >= 0)
            {
                context.push_back ({ decayActive (band), 1.0f });
                if (! pid.endsWith ("Rate")) context.push_back ({ decayRateId (band), 250.0f });
                if (pid.endsWith ("Q")) context.push_back ({ decayShape (band), (float) (int) DecayShape::bell });
            }
            if (pid.startsWithChar ('e') && band >= 0)
            {
                context.push_back ({ eqActive (band), 1.0f });
                if (pid.endsWith ("Slope")) context.push_back ({ eqShape (band), (float) (int) EqShape::highCut });
                else if (pid.endsWith ("Gain")) context.push_back ({ eqShape (band), (float) (int) EqShape::bell });
                else
                {
                    if (! pid.endsWith ("Shape")) context.push_back ({ eqShape (band), (float) (int) EqShape::bell });
                    context.push_back ({ eqGain (band), 9.0f });
                }
                if (pid.endsWith ("Shape")) context.push_back ({ eqFreq (band), 2000.0f });
                if (pid.endsWith ("Slope") || pid.endsWith ("Shape")) context.push_back ({ eqFreq (band), 3000.0f });
            }
            if (pid == id::predelayDiv) context.push_back ({ id::predelaySync, 1.0f });

            auto changed = context;
            const auto def = rp->getDefaultValue();
            auto alt = def < 0.5f ? std::min (1.0f, def + 0.4f) : def - 0.4f;
            if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) alt = def > 0.5f ? 0.0f : 1.0f;
            if (pid.endsWith ("Active") && pid.startsWithChar ('e') && band == 0) alt = 0.0f;
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (rp)) alt = choice->convertTo0to1 ((float) ((choice->getIndex() + 1) % choice->choices.size()));
            changed.push_back ({ pid, rp->convertFrom0to1 (alt) });
            if (pid.endsWith ("Active")) // the context switched it on: compare off with on
            {
                context.erase (std::remove_if (context.begin(), context.end(), [&] (auto& kv) { return kv.first == pid; }), context.end());
                context.push_back ({ pid, 0.0f });
                changed.back().second = 1.0f;
            }

            const auto a = renderWith (context), b = renderWith (changed);
            double diff = 0.0, level = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                diff += std::pow (a.l[i] - b.l[i], 2) + std::pow (a.r[i] - b.r[i], 2);
                level += std::pow (a.l[i], 2) + std::pow (a.r[i], 2);
            }
            check (diff > 1.0e-8 * level, pid.toStdString() + " changes the sound (difference " + fmt (10.0 * std::log10 (std::max (1.0e-30, diff / level)), 1) + " dB)");
        }
    }

    void testUndoHistory()
    {
        auto p = makeProcessor();
        auto& history = p->getUndoHistory();
        auto* space = param (*p, id::space);
        const auto before = space->getValue();
        space->beginChangeGesture();
        space->setValueNotifyingHost (0.6f);
        space->setValueNotifyingHost (0.7f);
        space->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (space->getValue() - before) < 1.0e-5f, "undo restores the value before the gesture");
        history.redo();
        check (std::abs (space->getValue() - 0.7f) < 1.0e-5f, "redo re-applies the whole gesture");
        history.clear();
        space->setValueNotifyingHost (0.9f);
        check (! history.canUndo(), "automation (no gesture) is not an undo step");
        p->setCurrentProgram (3);
        check (! history.canUndo(), "loading a preset clears the history");
    }

    void testCompare()
    {
        auto p = makeProcessor();
        set (*p, id::space, 20.0f);
        p->selectCompareSlot (1);
        check (std::abs (get (*p, id::space) - 20.0f) < 0.01f, "B starts as a copy of A");
        set (*p, id::space, 80.0f);
        p->selectCompareSlot (0);
        check (std::abs (get (*p, id::space) - 20.0f) < 0.01f, "A is restored");
        p->selectCompareSlot (1);
        check (std::abs (get (*p, id::space) - 80.0f) < 0.01f, "B is restored");
        p->copyToOtherCompareSlot();
        p->selectCompareSlot (0);
        check (std::abs (get (*p, id::space) - 80.0f) < 0.01f, "copy B to A");
    }

    void testRealtimeAndCpu()
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        const auto source = noise ((size_t) defaultBlock * 8, 0.3f, 91);
        const std::map<int, std::vector<std::pair<juce::String, float>>> changes {
            { 20, { { id::space, 95.0f }, { id::character, 100.0f } } }, { 50, { { decayActive (2), 1.0f }, { decayRateId (2), 300.0f } } },
            { 80, { { eqActive (3), 1.0f }, { eqShape (3), 4.0f }, { eqSlope (3), 2.0f } } }, { 110, { { id::predelaySync, 1.0f } } },
            { 140, { { id::width, 0.0f }, { id::distance, 100.0f } } }, { 170, { { id::bypass, 1.0f } } },
            { 200, { { id::bypass, 0.0f }, { id::space, 5.0f }, { id::decayRate, 50.0f } } }, { 230, { { id::brightness, 0.0f } } },
        };
        p->setAnalyzerMode (2);
        allocationCount = 0;
        for (int b = 0; b < 300; ++b)
        {
            if (auto it = changes.find (b); it != changes.end())
                for (auto& [pid, v] : it->second) set (*p, pid, v);
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, source.data() + (size_t) ((b + ch) % 7) * defaultBlock, defaultBlock);
            if (b >= 5) countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            if (b % 4 == 0) { p->getPostTap().discardAll(); p->getPreTap().discardAll(); }
        }
        check (allocationCount.load() == 0, "processBlock does not allocate (incl. Space, bands, EQ, sync, width, bypass changes): "
                                                + std::to_string (allocationCount.load()) + " allocations");

        auto c = makeProcessor();
        set (*c, id::character, 60.0f);
        set (*c, decayActive (1), 1.0f);
        set (*c, eqActive (2), 1.0f);
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
        check (factor > 8.0, "runs well faster than real time (" + fmt (factor, 1) + "x)");
        note ("CPU (one core, 48 kHz stereo, share of real time): " + fmt (100.0 / factor, 2) + " %");
    }


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

    struct EditorFixture
    {
        std::unique_ptr<Processor> processor;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor = nullptr;

        explicit EditorFixture (int program = -1)
        {
            processor = makeProcessor();
            if (program >= 0) processor->setCurrentProgram (program);
            base.reset (processor->createEditor());
            editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "the editor is a DooblrReverbEditor");
        }
        ~EditorFixture() { base.reset(); }

        /** Pink-ish noise through the processor, then the display analyses it. */
        void feedSpectrum (int mode = 2)
        {
            processor->setAnalyzerMode (mode);
            auto raw = noise (48000, 0.5f, 77);
            float lp = 0.0f;
            for (auto& x : raw) { lp += 0.08f * (x - lp); x = 0.6f * lp + 0.15f * x; }
            juce::AudioBuffer<float> buffer (2, defaultBlock);
            juce::MidiBuffer midi;
            auto& display = editor->getDisplay();
            for (size_t pos = 0; pos + defaultBlock <= raw.size(); pos += defaultBlock)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, raw.data() + pos, defaultBlock);
                processor->processBlock (buffer, midi);
                display.poll();
            }
        }
    };

    const std::vector<juce::Point<int>> editorSizes { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };

    int presetIndex (const juce::String& name)
    {
        const auto& all = presets::factory();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].name == name) return i;
        return 0;
    }

    void testEditor()
    {
        EditorFixture f;
        auto* editor = f.editor;
        if (editor == nullptr) return;
        auto& p = *f.processor;

        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080 x 660");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == 940 && c->getMinimumHeight() == 600 && c->getMaximumWidth() == 2400 && c->getMaximumHeight() == 1400, "resize limits 940 x 600 .. 2400 x 1400");
        check (editor->isResizable(), "resizable");
        auto* version = dynamic_cast<juce::Label*> (findById (*editor, "version"));
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString), "the version label shows " JucePlugin_VersionString);

        // Every parameter has exactly one control, except the EQ bands, which live in the display.
        for (auto* raw : p.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            const auto inDisplay = (pid.startsWithChar ('d') || pid.startsWithChar ('e')) && pid.substring (1, 2).containsOnly ("123456");
            int count = 0;
            forEachComponent (*editor, [&] (juce::Component& c) { if (c.getComponentID() == pid && isControl (c)) ++count; });
            if (inDisplay)
                check (count == 0, pid.toStdString() + " is edited in the display");
            else
                check (count == 1 && editor->findControl (pid) != nullptr, pid.toStdString() + " has exactly one control (" + std::to_string (count) + ")");
        }

        // The Space readout follows Space x Decay Rate.
        set (p, id::space, 60.0f);
        set (p, id::decayRate, 150.0f);
        editor->refresh();
        check (editor->getDecayReadout() == formatSeconds (Engine::decaySecondsFor (60.0f, 150.0f)),
               "the big readout shows the decay time (" + editor->getDecayReadout().toStdString() + ")");
        check (editor->getRoomReadout() == dsp::roomNameAt (60.0f), "and the room (" + editor->getRoomReadout().toStdString() + ")");

        // Dim, don't hide: the ms slider while synced, the division while free.
        set (p, id::predelaySync, 1.0f);
        editor->refresh();
        check (ui::isDimmed (*editor->findControl (id::predelay)) && ! ui::isDimmed (*editor->findControl (id::predelayDiv)), "synced: predelay ms dimmed, division live");
        set (p, id::predelaySync, 0.0f);
        editor->refresh();
        check (! ui::isDimmed (*editor->findControl (id::predelay)) && ui::isDimmed (*editor->findControl (id::predelayDiv)), "free: division dimmed, predelay live");

        // Layout fit at three sizes.
        for (auto size : editorSizes)
        {
            editor->setSize (size.x, size.y);
            editor->refresh();
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> rects;
            forEachVisible (*editor, [&] (juce::Component& c)
            {
                if (! isControl (c) || dynamic_cast<ui::ReverbDisplay*> (c.getParentComponent()) != nullptr) return;
                rects.push_back ({ &c, editor->getLocalArea (c.getParentComponent(), c.getBounds()) });
            });
            int problems = 0;
            for (size_t i = 0; i < rects.size(); ++i)
            {
                const auto& [c, r] = rects[i];
                const auto name = c->getComponentID().isNotEmpty() ? c->getComponentID() : c->getName();
                if (! editor->getLocalBounds().contains (r)) { ++problems; check (false, label + ": " + name.toStdString() + " inside the window"); }
                if (r.getWidth() < 16 || r.getHeight() < 14) { ++problems; check (false, label + ": " + name.toStdString() + " large enough (" + r.toString().toStdString() + ")"); }
                for (size_t j = i + 1; j < rects.size(); ++j)
                    if (r.intersects (rects[j].second))
                    {
                        ++problems;
                        check (false, label + ": " + name.toStdString() + " does not overlap " + rects[j].first->getComponentID().toStdString()
                                      + " (" + r.toString().toStdString() + " / " + rects[j].second.toString().toStdString() + ")");
                    }
            }
            auto& display = editor->getDisplay();
            check (display.getWidth() > size.x / 2 && display.getHeight() > size.y / 3, label + ": the display fills the middle ("
                   + display.getBounds().toString().toStdString() + ")");
            for (int k = 0; k < 7; ++k)
            {
                const auto* knob = dynamic_cast<juce::Slider*> (editor->findControl (std::array<const char*, 7> { id::space, id::decayRate, id::brightness,
                                                                                         id::character, id::distance, id::width, id::mix }[(size_t) k]));
                check (knob != nullptr && knob->getHeight() >= 60, label + ": knob " + std::to_string (k) + " has room");
            }
            if (problems == 0) note (label + ": " + std::to_string (rects.size()) + " controls fit without overlaps");
        }
    }

    void testSnapshots()
    {
        EditorFixture f (presetIndex ("Vocal Hall"));
        auto* editor = f.editor;
        if (editor == nullptr) return;
        set (*f.processor, id::mix, 30.0f);
        f.feedSpectrum (2);
        auto& display = editor->getDisplay();
        display.poll();
        check (! display.getPostSpectrumPath().isEmpty(), "the analyzer draws the reverb spectrum");
        check (! display.getDecayPath().isEmpty() && ! display.getEqPath().isEmpty(), "both curves are drawn");
        for (auto size : editorSizes)
        {
            editor->setSize (size.x, size.y);
            editor->refresh();
            display.poll();
            saveSnapshot (*editor, "-" + juce::String (size.x) + "x" + juce::String (size.y));
        }
        editor->setSize (1080, 660);
        display.setEditLayer (Layer::eq);
        saveSnapshot (*editor, "-1080x660-posteq");
        f.processor->setAnalyzerMode (0);
        display.poll();
        check (display.getPostSpectrumPath().isEmpty(), "Analyzer Off hides the spectrum");
    }

    void testDisplayMouse()
    {
        EditorFixture f (presetIndex ("Vocal Hall"));
        auto* editor = f.editor;
        if (editor == nullptr) return;
        auto& p = *f.processor;
        TestWindow window (*editor);
        auto* peer = window.getPeer();
        check (peer != nullptr, "the test window has a peer");
        if (peer == nullptr) return;
        PeerMouse mouse { *peer };
        auto& display = editor->getDisplay();
        display.poll();
        const auto toWindow = [&] (juce::Point<float> local) { return editor->getLocalPoint (&display, local); };
        auto& history = p.getUndoHistory();
        history.clear();

        // Drag Decay band 4 (a bell): frequency and rate follow, as one undo step.
        const int band = 3;
        check (get (p, decayActive (band)) > 0.5f, "Vocal Hall uses decay band 4");
        const auto f0 = get (p, decayFreq (band)), r0 = get (p, decayRateId (band));
        const auto handle = display.getHandlePosition (Layer::decay, band);
        const auto hit = display.handleAt (handle);
        check (hit.first == Layer::decay && hit.second == band, "the handle is hit at its position");
        const auto target = handle + juce::Point<float> (70.0f, -30.0f);
        mouse.drag (toWindow (handle), toWindow (target));
        const auto f1 = get (p, decayFreq (band)), r1 = get (p, decayRateId (band));
        const auto expectedF = display.frequencyForX (target.x), expectedR = 100.0f * std::exp2 (display.decayLog2ForY (target.y));
        check (std::abs (std::log2 (f1 / expectedF)) < 0.05f && std::abs (std::log2 (r1 / expectedR)) < 0.05f && f1 > f0 * 1.2f && r1 > r0 * 1.2f,
               "dragging a decay handle sets frequency and rate (" + fmt (f0, 0) + " Hz " + fmt (r0, 0) + " % -> " + fmt (f1, 0) + " Hz " + fmt (r1, 0)
               + " %, expected " + fmt (expectedF, 0) + " Hz " + fmt (expectedR, 0) + " %)");
        check (display.getEditLayer() == Layer::decay, "pressing a decay handle picks the Decay Rate EQ layer");
        history.undo();
        check (std::abs (get (p, decayFreq (band)) - f0) < 0.5f && std::abs (get (p, decayRateId (band)) - r0) < 0.05f, "one undo reverts the whole drag");
        history.redo();
        check (std::abs (get (p, decayFreq (band)) - f1) < 0.5f, "redo re-applies it");

        // Drag a Post EQ high-cut handle sideways: frequency only.
        display.poll();
        const int eqBand = 5;
        check (get (p, eqActive (eqBand)) > 0.5f, "Vocal Hall uses EQ band 6 (high cut)");
        const auto eh = display.getHandlePosition (Layer::eq, eqBand);
        const auto ef0 = get (p, eqFreq (eqBand));
        mouse.drag (toWindow (eh), toWindow (eh + juce::Point<float> (-60.0f, 0.0f)));
        check (get (p, eqFreq (eqBand)) < ef0 * 0.85f, "dragging the Post EQ high cut moves its frequency (" + fmt (ef0, 0) + " -> " + fmt (get (p, eqFreq (eqBand)), 0) + " Hz)");
        check (display.getEditLayer() == Layer::eq, "and picks the Post EQ layer");

        // Wheel over a handle: Q.
        display.poll();
        const auto q0 = get (p, decayQ (band));
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.25f;
        mouse.time += 400;
        peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, toWindow (display.getHandlePosition (Layer::decay, band)), mouse.time, wheel);
        check (std::abs (get (p, decayQ (band)) - q0) > 0.1f * q0, "the wheel over a handle changes its Q");

        // Double-click on empty space adds a band to the chosen layer; on a handle removes it.
        const auto doubleClick = [&] (juce::Point<float> pos)
        {
            mouse.time += 1000;
            const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            for (int k = 0; k < 2; ++k)
            {
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, down, 1.0f, 0.0f, mouse.time += 40);
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, {}, 1.0f, 0.0f, mouse.time += 40);
            }
        };
        display.getLayerButton (Layer::decay).onClick();
        check (display.getEditLayer() == Layer::decay, "the DECAY RATE EQ button picks the layer");
        int activeBefore = 0;
        for (int b = 0; b < numDecayBands; ++b) activeBefore += get (p, decayActive (b)) > 0.5f ? 1 : 0;
        const auto plot = display.getPlotArea();
        const auto spot = juce::Point<float> (plot.getX() + plot.getWidth() * 0.62f, plot.getY() + plot.getHeight() * 0.3f);
        doubleClick (toWindow (spot));
        int activeAfter = 0, added = -1;
        for (int b = 0; b < numDecayBands; ++b) if (get (p, decayActive (b)) > 0.5f) { ++activeAfter; if (b == 1) added = b; }
        check (activeAfter == activeBefore + 1, "double-clicking empty space adds a decay band (" + std::to_string (activeBefore) + " -> " + std::to_string (activeAfter) + ")");
        if (added >= 0)
        {
            check (std::abs (std::log2 (get (p, decayFreq (added)) / display.frequencyForX (spot.x))) < 0.05f, "at the clicked frequency");
            check (get (p, decayRateId (added)) > 120.0f, "with a rate from the clicked height (" + fmt (get (p, decayRateId (added)), 0) + " %)");
            display.poll();
            doubleClick (toWindow (display.getHandlePosition (Layer::decay, added)));
            check (get (p, decayActive (added)) < 0.5f, "double-clicking a handle removes the band");
        }
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                        testMetadata },
        { "zero latency",                                    testLatency },
        { "mix 0 / bypass null, silence",                    testNulls },
        { "decay times: readout, Decay Rate, bands, Brightness", testDecayTimes },
        { "predelay and sync",                               testPredelay },
        { "stereo width, mono layouts",                      testStereo },
        { "extremes: stable, finite, 44.1k..192k",           testExtremes },
        { "Post EQ extremes stay finite",                   testEqExtreme },
        { "value text",                                      testValueText },
        { "state round trip",                                testStateRoundTrip },
        { "factory and user presets",                        testPresets },
        { "no dead parameters",                              testNoDeadParameters },
        { "undo history",                                    testUndoHistory },
        { "A / B compare",                                   testCompare },
        { "real-time safety and CPU",                        testRealtimeAndCpu },
        { "editor: size, controls, layout fit",              testEditor },
        { "editor snapshots and analyzer",                   testSnapshots },
        { "display: drag, wheel, add / remove",              testDisplayMouse },
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
