/*
    dooblr-distressor console tests: the real processor, compressor engine and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/DistressorEngine.h"
#include "UI/GrMeterModel.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace dooblrdistressor;
    using namespace dooblrdistressor::params;
    using Processor = DooblrDistressorProcessor;
    using Editor = DooblrDistressorEditor;
    using Engine = dsp::DistressorEngine;

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

    void setMono (Processor& p)
    {
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p.setBusesLayout (layout), "mono layout accepted");
        p.prepareToPlay (p.getSampleRate(), p.getBlockSize());
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

    Stereo sine (size_t n, double hz, double amp, double rate = defaultRate)
    {
        Stereo s (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto v = (float) (amp * std::sin (twoPi * hz * (double) i / rate));
            s.l[i] = s.r[i] = v;
        }
        return s;
    }

    float rms (const std::vector<float>& x, size_t start, size_t count)
    {
        if (count == 0) return 0.0f;
        double acc = 0.0;
        for (size_t i = 0; i < count; ++i)
            acc += (double) x[start + i] * (double) x[start + i];
        return (float) std::sqrt (acc / (double) count);
    }

    float peak (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (auto v : x) m = std::max (m, std::abs (v));
        return m;
    }

    bool allFinite (const Stereo& s)
    {
        for (auto v : s.l) if (! std::isfinite (v)) return false;
        for (auto v : s.r) if (! std::isfinite (v)) return false;
        return true;
    }

    std::complex<double> bin (const std::vector<float>& x, double hz, double rate)
    {
        const auto w = twoPi * hz / rate;
        std::complex<double> acc { 0.0, 0.0 };
        for (size_t i = 0; i < x.size(); ++i)
            acc += (double) x[i] * std::exp (std::complex<double> (0.0, -w * (double) i));
        return acc / (double) x.size();
    }

    void defaults (Processor& p)
    {
        for (auto* q : p.getParameters())
            q->setValueNotifyingHost (q->getDefaultValue());
    }

    //------------------------------------------------------------------------------
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-distressor", "name is dooblr-distressor (got " + p->getName().toStdString() + ")");
        check (JucePlugin_IsSynth == 0 && ! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRDISTRESSOR_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.1.1", "version 0.1.1");
        check (p->getBypassParameter() == p->getState().getParameter (id::bypass), "Bypass is the host bypass parameter");
        check (p->getLatencySamples() == 0, "zero latency");

        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->checkBusesLayoutSupported (layout), "mono in / out is supported");
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! p->checkBusesLayoutSupported (layout), "mono in / stereo out is refused");
        note ("version " JucePlugin_VersionString);
        check ((int) p->getParameters().size() == 13, "13 automatable parameters");
    }

    void testOneToOneUnity()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::ratio, 0.0f); // 1:1
        set (*p, id::distMode, 0.0f);
        set (*p, id::audioHp, 0.0f);
        set (*p, id::input, 5.0f);
        set (*p, id::output, 5.0f);
        set (*p, id::mix, 100.0f);
        set (*p, id::bypass, 0.0f);

        const auto n = (size_t) (0.5 * defaultRate);
        const auto in = sine (n, 1000.0, std::pow (10.0, -18.0 / 20.0));
        const auto out = render (*p, in);
        const auto inRms = rms (in.l, n / 4, n / 2);
        const auto outRms = rms (out.l, n / 4, n / 2);
        const auto errDb = 20.0 * std::log10 (outRms / inRms);
        check (std::abs (errDb) < 0.15, "1:1, Dist off, knobs at 5: near unity (" + fmt (errDb, 3) + " dB)");
        check (p->getEngine().lastGainReductionDb() < 0.05f, "1:1 produces no gain reduction (" + fmt (p->getEngine().lastGainReductionDb(), 3) + " dB)");
        note ("1:1 gain " + fmt (errDb, 3) + " dB, GR " + fmt (p->getEngine().lastGainReductionDb(), 3) + " dB");
    }

    void testRatioBehaviour()
    {
        const auto n = (size_t) (0.6 * defaultRate);
        const auto in = sine (n, 1000.0, std::pow (10.0, -6.0 / 20.0));
        const auto settle = n / 2;

        auto measure = [&] (int ratioIndex)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, (float) ratioIndex);
            set (*p, id::input, 8.0f);
            set (*p, id::output, 5.0f);
            set (*p, id::attack, 0.0f);
            set (*p, id::release, 5.0f);
            set (*p, id::distMode, 0.0f);
            set (*p, id::mix, 100.0f);
            const auto out = render (*p, in);
            return std::pair<float, float> { rms (out.l, settle, n - settle), p->getEngine().lastGainReductionDb() };
        };

        const auto r1 = measure (0);
        const auto r2 = measure (1);
        const auto r4 = measure (3);
        const auto r6 = measure (4);
        const auto r10 = measure (5);
        const auto r20 = measure (6);
        const auto nuke = measure (7);

        check (r1.second < 0.15f, "1:1 GR near 0 (" + fmt (r1.second, 2) + " dB)");
        check (r2.second > 1.0f, "2:1 compresses (" + fmt (r2.second, 2) + " dB GR)");
        check (r6.second > r2.second, "6:1 GR > 2:1 GR (" + fmt (r6.second, 2) + " vs " + fmt (r2.second, 2) + ")");
        check (r10.second > r6.second, "10:1 GR > 6:1 GR (" + fmt (r10.second, 2) + " vs " + fmt (r6.second, 2) + ")");
        check (nuke.second > 15.0f && r20.second > 8.0f, "20:1 and Nuke both slam (" + fmt (r20.second, 2) + " / " + fmt (nuke.second, 2) + " dB GR)");
        check (nuke.first < r1.first * 0.5f, "Nuke output well below 1:1");

        // 20:1 is brickwall: doubling a signal already over its (higher) threshold must not
        // raise the output by 6 dB. 2:1's long knee still lets most of that extra through.
        // Input at 5 keeps the saturator out of the comparison.
        auto peakFor = [] (int ratioIndex, double amp)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, (float) ratioIndex);
            set (*p, id::input, 5.0f);
            set (*p, id::output, 5.0f);
            set (*p, id::attack, 0.0f);
            set (*p, id::release, 5.0f);
            set (*p, id::distMode, 0.0f);
            const auto frames = (size_t) (0.5 * defaultRate);
            const auto out = render (*p, sine (frames, 1000.0, amp));
            return peak (std::vector<float> (out.l.begin() + (long) frames / 2, out.l.end()));
        };
        const auto wallLo = peakFor (6, 0.45);
        const auto wallHi = peakFor (6, 0.90);
        const auto mildLo = peakFor (1, 0.45);
        const auto mildHi = peakFor (1, 0.90);
        const auto wallRiseDb = 20.0 * std::log10 (wallHi / juce::jmax (1.0e-8f, wallLo));
        const auto mildRiseDb = 20.0 * std::log10 (mildHi / juce::jmax (1.0e-8f, mildLo));
        check (wallRiseDb < 1.5, "20:1 brickwall: +6 dB input raises output by < 1.5 dB (" + fmt (wallRiseDb, 2) + " dB)");
        check (mildRiseDb > wallRiseDb + 2.0, "2:1 lets more of a +6 dB input through than 20:1 (" + fmt (mildRiseDb, 2) + " vs " + fmt (wallRiseDb, 2) + ")");

        const auto atk = 5.0f, rel = 5.0f;
        check (std::abs (Engine::staticReductionDb (-6.0f, 0, atk, rel)) < 1.0e-6f, "static 1:1 is 0");
        check (Engine::staticReductionDb (12.0f, 1, atk, rel) < -0.5f, "static 2:1 reduces at +12 dB");
        check (Engine::staticReductionDb (12.0f, 4, atk, rel) < Engine::staticReductionDb (12.0f, 1, atk, rel),
               "static 6:1 reduces more than 2:1 at +12 dB");
        check (Engine::staticReductionDb (12.0f, 7, atk, rel) < Engine::staticReductionDb (12.0f, 6, atk, rel),
               "static Nuke reduces more than 20:1 at +12 dB");
        const auto wallAt12 = Engine::staticReductionDb (4.0f, 6, atk, rel); // 12 dB over the −8 dB 20:1 threshold
        check (wallAt12 < -10.5f && wallAt12 > -12.5f, "static 20:1 brickwall holds ~threshold at +12 dB over (" + fmt (wallAt12, 2) + " dB)");

        const auto knee2fast = Engine::curveFor (1, 0.0f, 0.0f).kneeDb;
        const auto knee2slow = Engine::curveFor (1, 10.0f, 10.0f).kneeDb;
        check (knee2fast >= 14.5f && knee2fast <= 16.0f, "2:1 knee starts near 15 dB (" + fmt (knee2fast, 2) + ")");
        check (knee2slow > knee2fast && knee2slow <= 31.0f, "2:1 knee grows toward 30 dB with slower times (" + fmt (knee2slow, 2) + ")");

        note ("GR 1:1=" + fmt (r1.second, 2) + " 2:1=" + fmt (r2.second, 2) + " 4:1=" + fmt (r4.second, 2)
              + " 6:1=" + fmt (r6.second, 2) + " 10:1=" + fmt (r10.second, 2) + " 20:1=" + fmt (r20.second, 2)
              + " Nuke=" + fmt (nuke.second, 2));
    }

    void testAttackRelease()
    {
        constexpr double rate = 192000.0;
        auto run = [&] (int ratio, float atkKnob, float relKnob, bool stepUp)
        {
            auto p = makeProcessor (rate, 64);
            defaults (*p);
            set (*p, id::ratio, (float) ratio);
            set (*p, id::input, 8.0f);
            set (*p, id::output, 5.0f);
            set (*p, id::attack, atkKnob);
            set (*p, id::release, relKnob);
            set (*p, id::distMode, 0.0f);
            set (*p, id::mix, 100.0f);

            const auto n = (int) std::lround (rate * 0.08);
            const auto hold = (int) std::lround (rate * 0.04);
            std::vector<float> gr ((size_t) n, 0.0f);
            juce::AudioBuffer<float> buffer (2, 1);
            juce::MidiBuffer midi;
            p->prepareToPlay (rate, 1);

            for (int i = 0; i < n; ++i)
            {
                const auto on = stepUp ? (i >= 64) : (i >= 64 && i < hold);
                const auto v = on ? 0.6f : 0.0f;
                buffer.setSample (0, 0, v);
                buffer.setSample (1, 0, v);
                p->processBlock (buffer, midi);
                gr[(size_t) i] = p->getEngine().lastGainReductionDb();
            }
            return gr;
        };

        const auto fast = run (4, 0.0f, 10.0f, true);
        const auto slow = run (4, 10.0f, 10.0f, true);
        const auto peakFast = *std::max_element (fast.begin(), fast.end());
        const auto peakSlow = *std::max_element (slow.begin(), slow.end());
        check (peakFast > 2.0f && peakSlow > 2.0f, "both attack settings reach several dB of GR");

        auto timeTo63 = [] (const std::vector<float>& gr, float peak)
        {
            const auto target = 0.63f * peak;
            for (size_t i = 64; i < gr.size(); ++i)
                if (gr[i] >= target)
                    return (int) i - 64;
            return (int) gr.size();
        };

        const auto tFast = timeTo63 (fast, peakFast);
        const auto tSlow = timeTo63 (slow, peakSlow);
        const auto expectFast = Engine::attackSeconds (0.0f, 4) * rate;
        const auto expectSlow = Engine::attackSeconds (10.0f, 4) * rate;
        check (tFast < tSlow / 4, "attack 0 reaches 63% GR much sooner than attack 10 (" + std::to_string (tFast) + " vs " + std::to_string (tSlow) + " samples)");
        check (std::abs ((double) tFast - expectFast) < std::max (8.0, 0.5 * expectFast),
               "fast attack 63% time matches tau (" + std::to_string (tFast) + " vs " + fmt (expectFast, 1) + " samples)");
        check (std::abs ((double) tSlow - expectSlow) < 0.35 * expectSlow,
               "slow attack 63% time matches tau (" + std::to_string (tSlow) + " vs " + fmt (expectSlow, 1) + " samples)");

        check (Engine::usesFastMinAttack (1) && Engine::usesFastMinAttack (2) && Engine::usesFastMinAttack (3),
               "2:1, 3:1 and 4:1 use the faster minimum attack");
        check (! Engine::usesFastMinAttack (4) && ! Engine::usesFastMinAttack (0), "6:1 and 1:1 keep the 50 us floor");
        check (Engine::attackSeconds (0.0f, 1) < Engine::attackSeconds (0.0f, 4),
               "2:1 min attack is faster than 6:1 (" + fmt (Engine::attackSeconds (0.0f, 1) * 1.0e6, 1) + " vs "
               + fmt (Engine::attackSeconds (0.0f, 4) * 1.0e6, 1) + " us)");
        check (std::abs (Engine::attackSeconds (0.0f, 4) - Engine::attackMinSec) < 1.0e-9f, "6:1 min attack is 50 us");
        check (std::abs (Engine::attackSeconds (0.0f, 1) - Engine::attackMinFastSec) < 1.0e-9f, "2:1 min attack is 20 us");
        check (std::abs (Engine::attackSeconds (10.0f, 4) - Engine::attackMaxSec) < 1.0e-6f, "attack 10 is 30 ms");
        check (std::abs (Engine::releaseSeconds (0.0f, 4) - Engine::releaseMinSec) < 1.0e-9f, "release 0 is 50 ms");
        check (std::abs (Engine::releaseSeconds (10.0f, 4) - Engine::releaseMaxSec) < 1.0e-6f, "release 10 is 3.5 s");

        // Release: after a burst, fast release returns toward 0 sooner.
        const auto relFast = run (4, 0.0f, 0.0f, false);
        const auto relSlow = run (4, 0.0f, 10.0f, false);
        const auto hold = (int) std::lround (rate * 0.04);
        const auto afterFast = relFast[(size_t) std::min ((int) relFast.size() - 1, hold + (int) std::lround (rate * 0.02))];
        const auto afterSlow = relSlow[(size_t) std::min ((int) relSlow.size() - 1, hold + (int) std::lround (rate * 0.02))];
        check (afterFast < afterSlow, "50 ms release lets go more than 3.5 s release 20 ms after the burst ("
               + fmt (afterFast, 2) + " vs " + fmt (afterSlow, 2) + " dB)");

        note ("attack 63% samples fast=" + std::to_string (tFast) + " slow=" + std::to_string (tSlow));
    }

    void testOptoAndNukeRelease()
    {
        constexpr double rate = 48000.0;
        auto sustainThenRelease = [&] (int ratio, float secondsOn)
        {
            auto p = makeProcessor (rate, 64);
            defaults (*p);
            set (*p, id::ratio, (float) ratio);
            set (*p, id::input, 9.0f);
            set (*p, id::attack, ratio == 5 ? 10.0f : 2.0f);
            set (*p, id::release, 0.0f);
            set (*p, id::distMode, 0.0f);
            const auto onN = (int) std::lround (rate * secondsOn);
            const auto offN = (int) std::lround (rate * 1.5);
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            float last = 0.0f;
            for (int i = 0; i < onN; i += 64)
            {
                const auto n = std::min (64, onN - i);
                buffer.setSize (2, n, false, false, true);
                for (int s = 0; s < n; ++s)
                    buffer.setSample (0, s, 0.6f), buffer.setSample (1, s, 0.6f);
                p->processBlock (buffer, midi);
                last = p->getEngine().lastGainReductionDb();
            }
            const auto peakGr = last;
            int samplesToHalf = offN;
            int seen = 0;
            for (int i = 0; i < offN; i += 64)
            {
                const auto n = std::min (64, offN - i);
                buffer.setSize (2, n, false, false, true);
                buffer.clear();
                p->processBlock (buffer, midi);
                last = p->getEngine().lastGainReductionDb();
                seen += n;
                if (last <= 0.5f * peakGr)
                {
                    samplesToHalf = seen;
                    break;
                }
            }
            return std::pair<float, int> { peakGr, samplesToHalf };
        };

        const auto optoShort = sustainThenRelease (5, 0.05);
        const auto optoLong = sustainThenRelease (5, 1.2);
        check (optoShort.first > 2.0f && optoLong.first > 2.0f, "opto 10:1 compresses");
        check (optoLong.second > optoShort.second * 1.5, "10:1 opto release lengthens after a long sustain ("
               + std::to_string (optoLong.second) + " vs " + std::to_string (optoShort.second) + " samples to half GR)");
        check (optoLong.second < (int) (Engine::optoReleaseMaxSec * rate), "opto release stays under 20 s");

        // Nuke logarithmic: remaining GR drops faster at first than later.
        auto p = makeProcessor (rate, 1);
        defaults (*p);
        set (*p, id::ratio, 7.0f);
        set (*p, id::input, 9.0f);
        set (*p, id::attack, 0.0f);
        set (*p, id::release, 5.0f);
        juce::AudioBuffer<float> buffer (2, 1);
        juce::MidiBuffer midi;
        p->prepareToPlay (rate, 1);
        for (int i = 0; i < (int) rate / 10; ++i)
        {
            buffer.setSample (0, 0, 0.7f);
            buffer.setSample (1, 0, 0.7f);
            p->processBlock (buffer, midi);
        }
        const auto peakGr = p->getEngine().lastGainReductionDb();
        const auto tau = Engine::releaseSeconds (5.0f, 7);
        const auto nTau = (int) std::lround (tau * rate);
        std::vector<float> curve;
        for (int i = 0; i < nTau; ++i)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
            curve.push_back (p->getEngine().lastGainReductionDb());
        }
        const auto first = peakGr - curve[(size_t) nTau / 4];
        const auto third = curve[(size_t) nTau / 2] - curve[(size_t) (nTau * 3 / 4)];
        check (peakGr > 5.0f, "Nuke holds a lot of GR (" + fmt (peakGr, 2) + " dB)");
        check (first > third, "Nuke logarithmic release: more dB in [0, τ/4] than in [τ/2, 3τ/4] ("
               + fmt (first, 2) + " vs " + fmt (third, 2) + ")");
        note ("opto half-GR samples short=" + std::to_string (optoShort.second) + " long=" + std::to_string (optoLong.second)
              + "; nuke first/third quarter drop " + fmt (first, 2) + "/" + fmt (third, 2) + " dB");
    }

    void testAudioHp()
    {
        const auto mag60 = Engine::audioHpMagnitudeDb (60.0f, defaultRate);
        const auto mag30 = Engine::audioHpMagnitudeDb (30.0f, defaultRate);
        check (std::abs (mag60 + 3.0f) < 0.4f, "audio HP ~ -3 dB at 60 Hz (" + fmt (mag60, 2) + " dB)");
        check (std::abs (mag30 + 12.0f) < 0.8f, "audio HP ~ -12 dB at 30 Hz (" + fmt (mag30, 2) + " dB)");
        check (Engine::audioHpMagnitudeDb (1000.0f, defaultRate) > -0.3f, "audio HP is flat in the mids");

        auto measure = [] (double hz, bool hp)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, 0.0f);
            set (*p, id::audioHp, hp ? 1.0f : 0.0f);
            set (*p, id::distMode, 0.0f);
            const auto n = (size_t) (1.0 * defaultRate);
            const auto in = sine (n, hz, std::pow (10.0, -24.0 / 20.0));
            const auto out = render (*p, in);
            return 20.0 * std::log10 (rms (out.l, n / 2, n / 4) / rms (in.l, n / 2, n / 4));
        };

        const auto extra60 = measure (60.0, false) - measure (60.0, true);
        const auto extra30 = measure (30.0, false) - measure (30.0, true);
        check (std::abs (extra60 - 3.0) < 0.6, "processor audio HP: ~3 dB extra cut at 60 Hz (" + fmt (extra60, 2) + " dB)");
        check (std::abs (extra30 - 12.0) < 1.0, "processor audio HP: ~12 dB extra cut at 30 Hz (" + fmt (extra30, 2) + " dB)");
        note ("HP mag 60=" + fmt (mag60, 2) + " dB, 30=" + fmt (mag30, 2) + " dB; extra " + fmt (extra60, 2) + "/" + fmt (extra30, 2));
    }

    void testDetector()
    {
        auto grFor = [] (double hz, bool hp, bool band)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, 4.0f);
            set (*p, id::input, 8.0f);
            set (*p, id::attack, 0.0f);
            set (*p, id::release, 5.0f);
            set (*p, id::detHp, hp ? 1.0f : 0.0f);
            set (*p, id::detBand, band ? 1.0f : 0.0f);
            const auto n = (size_t) (0.5 * defaultRate);
            const auto in = sine (n, hz, std::pow (10.0, -8.0 / 20.0));
            render (*p, in);
            return p->getEngine().lastGainReductionDb();
        };

        const auto bass = grFor (50.0, false, false);
        const auto bassHp = grFor (50.0, true, false);
        const auto mid = grFor (1000.0, false, false);
        const auto harsh = grFor (6000.0, false, false);
        const auto harshBand = grFor (6000.0, false, true);
        const auto midHp = grFor (200.0, true, false);
        check (bass > 1.0f, "50 Hz drives the compressor without Det HP");
        check (bassHp < bass, "Det HP reduces 50 Hz GR (" + fmt (bassHp, 2) + " vs " + fmt (bass, 2) + ")");
        check (bassHp < midHp, "with Det HP, 50 Hz triggers less than 200 Hz (" + fmt (bassHp, 2) + " vs " + fmt (midHp, 2) + ")");
        check (harshBand > harsh + 1.0f, "Band emphasis raises GR at 6 kHz (" + fmt (harshBand, 2) + " vs " + fmt (harsh, 2) + ")");
        check (std::abs (grFor (1000.0, false, true) - mid) < 2.5f, "Band emphasis does not smash 1 kHz the way it does 6 kHz");
        note ("GR 50 Hz " + fmt (bass, 2) + " / HP " + fmt (bassHp, 2) + "; 6 kHz " + fmt (harsh, 2) + " / band " + fmt (harshBand, 2));
    }

    void testStereoAndMonoLink()
    {
        const auto n = (size_t) (0.4 * defaultRate);
        Stereo in (n);
        for (size_t i = 0; i < n; ++i)
        {
            in.l[i] = (float) (0.5 * std::sin (twoPi * 1000.0 * (double) i / defaultRate));
            in.r[i] = (float) (0.05 * std::sin (twoPi * 1000.0 * (double) i / defaultRate));
        }

        auto run = [&] (bool link)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, 4.0f);
            set (*p, id::input, 8.0f);
            set (*p, id::attack, 0.0f);
            set (*p, id::stereoLink, link ? 1.0f : 0.0f);
            return render (*p, in);
        };

        const auto unlinked = run (false);
        const auto linked = run (true);
        const auto uL = rms (unlinked.l, n / 2, n / 4), uR = rms (unlinked.r, n / 2, n / 4);
        const auto lL = rms (linked.l, n / 2, n / 4), lR = rms (linked.r, n / 2, n / 4);
        check (uR / uL > 0.12f, "unlinked: quiet right stays relatively louder vs left");
        check (lR / lL < uR / uL, "linked: left/right ratio moves toward equal GR");
        // Linked GR is the average detector, so the loud left is compressed less than unlinked.
        check (lL > uL, "linked: loud left is compressed less than when unlinked (shared detector)");

        auto p = makeProcessor();
        setMono (*p);
        defaults (*p);
        set (*p, id::ratio, 4.0f);
        set (*p, id::input, 8.0f);
        set (*p, id::attack, 0.0f);
        Stereo mono (n);
        mono.l = in.l;
        mono.r.assign (n, 0.0f);
        set (*p, id::stereoLink, 0.0f);
        render (*p, mono);
        const auto monoOff = p->getEngine().lastGainReductionDb();
        set (*p, id::stereoLink, 1.0f);
        render (*p, mono);
        const auto monoOn = p->getEngine().lastGainReductionDb();
        check (monoOn < monoOff, "mono + Link is the dead-patch: less GR (" + fmt (monoOn, 2) + " vs " + fmt (monoOff, 2) + ")");
        note ("unlinked L/R rms " + fmt (uL, 4) + "/" + fmt (uR, 4) + "; linked " + fmt (lL, 4) + "/" + fmt (lR, 4));
    }

    void testMixPhase()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::ratio, 4.0f);
        set (*p, id::input, 7.0f);
        set (*p, id::audioHp, 0.0f);
        set (*p, id::distMode, 0.0f);

        Stereo impulse (2048);
        impulse.l[128] = 1.0f;
        impulse.r[128] = 1.0f;

        set (*p, id::mix, 0.0f);
        const auto dry = render (*p, impulse);
        check (std::abs (dry.l[128] - 1.0f) < 1.0e-6f && std::abs (dry.r[128] - 1.0f) < 1.0e-6f, "Mix 0 is the dry impulse at the same sample");
        float others = 0.0f;
        for (size_t i = 0; i < dry.l.size(); ++i)
            if (i != 128) others = std::max (others, std::abs (dry.l[i]));
        check (others < 1.0e-6f, "Mix 0 copies the input with no smearing");

        set (*p, id::mix, 100.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto wet = render (*p, impulse);
        auto wetPeakAt = 0;
        float wetPeak = 0.0f;
        for (int i = 0; i < (int) wet.l.size(); ++i)
            if (std::abs (wet.l[(size_t) i]) > wetPeak)
            {
                wetPeak = std::abs (wet.l[(size_t) i]);
                wetPeakAt = i;
            }
        check (wetPeakAt == 128, "Mix 100 impulse peaks at the same sample (phase aligned, got " + std::to_string (wetPeakAt) + ")");

        set (*p, id::mix, 50.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto blend = render (*p, impulse);
        check (std::abs (blend.l[128] - 0.5f * (dry.l[128] + wet.l[128])) < 1.0e-5f, "Mix 50 is the 0.5 dry + 0.5 wet sum at lag 0");

        // Sine: wet/dry cross-correlation peaks at lag 0.
        const auto tone = sine (4096, 1000.0, 0.2);
        set (*p, id::mix, 0.0f);
        const auto dryT = render (*p, tone);
        set (*p, id::mix, 100.0f);
        const auto wetT = render (*p, tone);
        double best = -1.0e9, at0 = 0.0;
        int bestLag = 999;
        for (int lag = -32; lag <= 32; ++lag)
        {
            double acc = 0.0;
            int count = 0;
            for (int i = 256; i < 4096 - 256; ++i)
            {
                const auto j = i + lag;
                if (j < 0 || j >= 4096) continue;
                acc += (double) dryT.l[(size_t) i] * (double) wetT.l[(size_t) j];
                ++count;
            }
            acc /= (double) count;
            if (lag == 0) at0 = acc;
            if (acc > best) { best = acc; bestLag = lag; }
        }
        check (bestLag == 0, "dry/wet cross-correlation peaks at lag 0 (got " + std::to_string (bestLag) + ")");
        check (at0 > 0.0, "Mix is in phase (positive correlation at lag 0)");
        note ("impulse wet peak sample " + std::to_string (wetPeakAt) + ", corr lag " + std::to_string (bestLag));
    }

    void testBypassKeepsMeter()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::ratio, 6.0f);
        set (*p, id::input, 9.0f);
        set (*p, id::attack, 0.0f);
        set (*p, id::bypass, 1.0f);
        const auto n = (size_t) (0.3 * defaultRate);
        const auto in = sine (n, 1000.0, 0.5);
        const auto out = render (*p, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < n; ++i)
            maxDiff = std::max (maxDiff, std::abs (out.l[i] - in.l[i]));
        check (maxDiff < 1.0e-6f, "Bypass is a bit-identical copy of the input");
        check (p->getEngine().lastGainReductionDb() > 2.0f, "Bypass still runs the GR meter (" + fmt (p->getEngine().lastGainReductionDb(), 2) + " dB)");
    }

    void testDistortion()
    {
        auto harmonics = [] (int dist)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, 0.0f);
            set (*p, id::input, 5.0f);
            set (*p, id::output, 5.0f);
            set (*p, id::distMode, (float) dist);
            const auto n = (size_t) 4800;
            const auto in = sine (n, 1000.0, 0.4);
            const auto out = render (*p, in);
            const auto h1 = std::abs (bin (out.l, 1000.0, defaultRate));
            const auto h2 = std::abs (bin (out.l, 2000.0, defaultRate));
            const auto h3 = std::abs (bin (out.l, 3000.0, defaultRate));
            return std::array<double, 3> { h1, h2, h3 };
        };

        const auto clean = harmonics (0);
        const auto d2 = harmonics (1);
        const auto d3 = harmonics (2);
        check (d2[1] / d2[0] > clean[1] / clean[0] * 3.0, "Dist 2 raises the 2nd harmonic vs clean");
        check (d2[1] > d2[2], "Dist 2: 2nd harmonic stronger than 3rd");
        check (d3[2] / d3[0] > clean[2] / clean[0] * 3.0, "Dist 3 raises the 3rd harmonic vs clean");
        check (d3[2] > d3[1], "Dist 3: 3rd harmonic stronger than 2nd");

        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::ratio, 0.0f);
        set (*p, id::distMode, 2.0f);
        set (*p, id::input, 10.5f);
        render (*p, sine ((size_t) defaultRate / 2, 200.0, 0.9));
        const auto m = p->takeMeters();
        check (m.led1pct, "1% LED lights under Dist 3 drive (UA 1%, not the PDF's 0.25%)");
        check (m.led3pct, "3%/clip LED lights under Dist 3 drive");
        note ("harmonics Dist2 h2/h1=" + fmt (d2[1] / d2[0], 3) + " Dist3 h3/h1=" + fmt (d3[2] / d3[0], 3));
    }

    void testHeadroom()
    {
        auto gr = [] (int hrIndex)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::ratio, 4.0f);
            set (*p, id::input, 6.0f);
            set (*p, id::headroom, (float) hrIndex);
            set (*p, id::attack, 0.0f);
            render (*p, sine ((size_t) (0.4 * defaultRate), 1000.0, 0.3));
            return p->getEngine().lastGainReductionDb();
        };
        const auto lo = gr (0);  // 4 dB displayed = more headroom = less processing
        const auto mid = gr (3); // 16 dB default
        const auto hi = gr (6);  // 28 dB = less headroom = more processing
        check (hi > mid && mid > lo, "higher Headroom dB value drives more GR (" + fmt (lo, 2) + " / " + fmt (mid, 2) + " / " + fmt (hi, 2) + ")");
        check (headroomDbForIndex (3) == 16 && headroomDbForIndex (0) == 4 && headroomDbForIndex (6) == 28, "headroom steps 4..28");
    }

    void testNoNanDenormalsAutomation()
    {
        auto p = makeProcessor();
        defaults (*p);
        Stereo in ((size_t) defaultRate);
        for (size_t i = 0; i < in.size(); ++i)
        {
            in.l[i] = (i % 17 == 0) ? 1.5f : (i % 31 == 0) ? 1.0e-30f : (float) (0.2 * std::sin (0.03 * (double) i));
            in.r[i] = -in.l[i] * 0.7f;
        }

        const std::vector<const char*> ids { id::input, id::attack, id::release, id::output, id::ratio,
                                             id::audioHp, id::distMode, id::detHp, id::detBand, id::stereoLink,
                                             id::headroom, id::mix, id::bypass };
        size_t cursor = 0;
        Stereo out (in.size());
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        while (cursor < in.size())
        {
            const auto n = (int) std::min ((size_t) defaultBlock, in.size() - cursor);
            buffer.setSize (2, n, false, false, true);
            for (int s = 0; s < n; ++s)
            {
                buffer.setSample (0, s, in.l[cursor + (size_t) s]);
                buffer.setSample (1, s, in.r[cursor + (size_t) s]);
            }
            auto* q = param (*p, ids[(cursor / (size_t) defaultBlock) % ids.size()]);
            if (q != nullptr)
                q->setValueNotifyingHost (std::fmod ((float) cursor * 0.0017f, 1.0f));
            p->processBlock (buffer, midi);
            for (int s = 0; s < n; ++s)
            {
                out.l[cursor + (size_t) s] = buffer.getSample (0, s);
                out.r[cursor + (size_t) s] = buffer.getSample (1, s);
            }
            cursor += (size_t) n;
        }
        check (allFinite (out), "automation sweep: every sample finite");
        check (peak (out.l) < 20.0f && peak (out.r) < 20.0f, "automation sweep: no runaway gain");

        // Pure denormals / zeros
        defaults (*p);
        Stereo den ((size_t) 2048);
        for (size_t i = 0; i < den.size(); ++i)
            den.l[i] = den.r[i] = (i & 1) ? 1.0e-38f : 0.0f;
        const auto denOut = render (*p, den);
        check (allFinite (denOut), "denormal input stays finite");
    }

    void testNoDeadParameters()
    {
        const auto n = (size_t) (0.35 * defaultRate);
        Stereo in (n);
        for (size_t i = 0; i < n; ++i)
        {
            in.l[i] = (float) (0.35 * std::sin (twoPi * 80.0 * (double) i / defaultRate)
                               + 0.25 * std::sin (twoPi * 6000.0 * (double) i / defaultRate));
            in.r[i] = (float) (0.08 * std::sin (twoPi * 120.0 * (double) i / defaultRate));
        }

        auto snapshot = [&] (Processor& p)
        {
            p.prepareToPlay (defaultRate, defaultBlock);
            return render (p, in);
        };

        auto p = makeProcessor();
        defaults (*p);
        const auto base = snapshot (*p);

        struct Case { const char* pid; float value; const char* why; };
        const Case cases[] {
            { id::input, 9.5f, "Input" },
            { id::output, 8.0f, "Output" },
            { id::attack, 0.0f, "Attack" },
            { id::release, 0.0f, "Release" },
            { id::ratio, 7.0f, "Ratio Nuke" },
            { id::audioHp, 1.0f, "Audio HP" },
            { id::distMode, 2.0f, "Dist 3" },
            { id::detHp, 1.0f, "Detector HP" },
            { id::detBand, 1.0f, "Band emphasis" },
            { id::stereoLink, 1.0f, "Link" },
            { id::headroom, 6.0f, "Headroom 28" },
            { id::mix, 40.0f, "Mix" },
            { id::bypass, 1.0f, "Bypass" },
        };

        for (const auto& c : cases)
        {
            defaults (*p);
            set (*p, c.pid, c.value);
            const auto out = snapshot (*p);
            float diff = 0.0f;
            for (size_t i = n / 4; i < n; ++i)
                diff = std::max (diff, std::abs (out.l[i] - base.l[i]));
            check (diff > 1.0e-4f, std::string (c.why) + " changes the audio (max diff " + fmt (diff, 6) + ")");
        }
    }

    void testStatePresetsUndo()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::input, 7.25f);
        set (*p, id::ratio, 7.0f);
        juce::MemoryBlock blob;
        p->getStateInformation (blob);
        defaults (*p);
        p->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*p, id::input) - 7.25f) < 0.02f && std::abs (get (*p, id::ratio) - 7.0f) < 0.1f, "state round-trip");

        p->setCurrentProgram (1);
        check (p->getProgramName (1) == "Vocals" && get (*p, id::audioHp) > 0.5f, "factory Vocals preset");

        auto& history = p->getUndoHistory();
        auto* input = param (*p, id::input);
        const auto before = input->getValue();
        input->beginChangeGesture();
        input->setValueNotifyingHost (0.2f);
        input->endChangeGesture();
        check (history.canUndo(), "a gesture is undoable");
        history.undo();
        check (std::abs (input->getValue() - before) < 1.0e-5f, "undo restores the value");
        history.redo();
        check (std::abs (input->getValue() - 0.2f) < 1.0e-5f, "redo reapplies the value");

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("dooblr-distressor-tests-" + juce::String (juce::Time::currentTimeMillis()));
        dir.createDirectory();
        const auto file = dir.getChildFile ("room" + juce::String (presets::fileExtension));
        set (*p, id::output, 8.5f);
        check (p->saveUserPreset (file).startsWith ("Saved"), "save user preset");
        defaults (*p);
        check (p->loadUserPreset (file).startsWith ("Loaded") && std::abs (get (*p, id::output) - 8.5f) < 0.05f, "load user preset");
        file.deleteFile();
        dir.deleteRecursively();
    }

    void testValueText()
    {
        check (formatValue (0.0, 1) == "0.0" && formatValue (-0.004, 1) == "0.0", "never -0");
        check (formatValue (5.049, 1) == "5.0" || formatValue (5.049, 1) == "5.1", "one decimal");
        auto p = makeProcessor();
        auto* mix = param (*p, id::mix);
        const auto text = mix->getText (mix->convertTo0to1 (50.0f), 16);
        check (text.contains ("50") && text.contains ("%"), "Mix value text includes % (" + text.toStdString() + ")");
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
                check (false, "createEditor returns DooblrDistressorEditor");
            else
                editor->addToDesktop (0);
        }
        ~EditorFixture()
        {
            if (editor != nullptr)
            {
                editor->removeFromDesktop();
                delete editor;
            }
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

        auto* version = dynamic_cast<juce::Label*> (editor->findControl ("version"));
        if (version == nullptr)
        {
            juce::Component* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (c.getComponentID() == "version") found = &c;
                for (auto* child : c.getChildren()) if (child != nullptr) walk (*child);
            };
            walk (*editor);
            version = dynamic_cast<juce::Label*> (found);
        }
        check (version != nullptr && version->getText().contains (JucePlugin_VersionString),
               "the version label shows " JucePlugin_VersionString);

        for (auto* raw : f.processor->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto pid = rp->getParameterID();
            int count = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (c.getComponentID() == pid && isControl (c)) ++count;
                for (auto* child : c.getChildren()) if (child != nullptr) walk (*child);
            };
            walk (*editor);
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

            const auto display = editor->getCompressionDisplay().getBounds();
            check (editor->getLocalBounds().contains (display) && display.getWidth() >= 200 && display.getHeight() >= 80,
                   label + " compression display fits (" + display.toString().toStdString() + ")");
            check (display.getIntersection (editor->getRatioStrip().getBounds()).isEmpty(),
                   label + " compression display does not overlap the ratio strip");
        }
    }

    void testGrMeterBallistics()
    {
        ui::GrMeterModel m;
        m.tick (12.0f, 0.0, 0.0);
        check (std::abs (m.getDisplayedDb() - 12.0f) < 1.0e-5f, "GR attack is instant (" + fmt (m.getDisplayedDb(), 3) + ")");
        m.tick (0.0f, 0.5, 500.0);
        const auto after = m.getDisplayedDb();
        check (after > 5.5f && after < 6.5f, "GR release is 12 dB/s (0.5 s from 12 dB -> ~6 dB, got " + fmt (after, 2) + ")");
        m.tick (0.0f, 1.0, 1500.0);
        check (m.getDisplayedDb() < 0.05f, "GR display reaches 0 after the rest of the release (" + fmt (m.getDisplayedDb(), 3) + ")");
        m.tick (30.0f, 0.0, 1600.0);
        check (std::abs (m.getDisplayedDb() - ui::GrMeterModel::rangeDb) < 1.0e-5f, "incoming GR clamps to the 24 dB range");
        m.reset();
        m.tick (-3.0f, 0.0, 0.0);
        check (m.getDisplayedDb() >= 0.0f && m.getDisplayedDb() < 1.0e-5f, "negative GR is clamped to 0");
    }

    void testGrMeterPeakHold()
    {
        ui::GrMeterModel m;
        m.tick (10.0f, 0.0, 0.0);
        check (std::abs (m.getPeakHoldDb() - 10.0f) < 1.0e-5f, "peak-hold captures a spike");
        check (m.getPeakHoldUntilMs() >= 700.0, "hold deadline is 700 ms after the peak");
        m.tick (0.0f, 0.2, 200.0);
        check (std::abs (m.getPeakHoldDb() - 10.0f) < 0.05f, "peak-hold stays put 200 ms after the spike (" + fmt (m.getPeakHoldDb(), 2) + ")");
        m.tick (0.0f, 0.49, 690.0);
        check (std::abs (m.getPeakHoldDb() - 10.0f) < 0.05f, "peak-hold still held just before 700 ms (" + fmt (m.getPeakHoldDb(), 2) + ")");
        m.tick (0.0f, 0.5, 1190.0);
        const auto decayed = m.getPeakHoldDb();
        const auto expect = 10.0f - ui::GrMeterModel::holdDecayDbPerSecond * 0.5f;
        check (decayed < 10.0f * 0.95f, "peak-hold decays after the hold (" + fmt (decayed, 2) + " dB)");
        check (std::abs (decayed - expect) < 0.15f, "peak-hold decays at 18 dB/s (" + fmt (decayed, 2) + " vs " + fmt (expect, 2) + ")");
        m.tick (0.0f, 1.0, 2190.0);
        check (m.getPeakHoldDb() <= m.getDisplayedDb() + 0.05f, "peak-hold does not sit below the displayed GR");
    }

    void testGrMeterHistory()
    {
        ui::GrMeterModel m;
        const auto dt = 1.0 / ui::GrMeterModel::historyHz;
        for (int i = 0; i < 60; ++i)
            m.tick (i < 30 ? 6.0f : 2.0f, dt, (double) (i + 1) * dt * 1000.0);
        check (m.getHistoryFilled() == 60, "1 s at 60 Hz fills 60 history samples (" + std::to_string (m.getHistoryFilled()) + ")");
        check (std::abs (m.historyOldest (0) - 6.0f) < 0.05f, "oldest sample is the first value written");
        check (std::abs (m.historyNewest() - 2.0f) < 0.05f, "newest sample is the latest value");
        check (m.historyOldest (29) > 5.5f, "the 6 dB hold occupies the first half of the buffer (" + fmt (m.historyOldest (29), 2) + ")");
        check (m.historyOldest (30) < m.historyOldest (29) && m.historyOldest (30) > 5.0f,
               "history records the 12 dB/s release after the step (" + fmt (m.historyOldest (30), 2) + ")");
        check (m.historyOldest (50) < 2.2f, "history has settled at 2 dB after the release (" + fmt (m.historyOldest (50), 2) + ")");

        for (int i = 0; i < ui::GrMeterModel::historyLength + 20; ++i)
            m.tick (1.0f, dt, 2000.0 + (double) i * dt * 1000.0);
        check (m.getHistoryFilled() == ui::GrMeterModel::historyLength, "history caps at 180 samples (3 s)");
        check (std::abs (m.historyOldest (0) - 1.0f) < 0.05f, "after wrapping, the oldest sample is the retained 1 dB");
        check (std::abs (m.historyNewest() - 1.0f) < 0.05f, "newest after wrapping is still 1 dB");
        check (std::abs (m.historyOldest (-1)) < 1.0e-6f && std::abs (m.historyOldest (m.getHistoryFilled())) < 1.0e-6f, "out-of-range history reads 0");
    }

    void testCompressionMap()
    {
        using Map = ui::CompressionMap;
        Map::Params p;

        const auto mid = Map::fromParams (p);
        check (std::abs (mid.kneeX - 5.0f / 10.5f) < 1.0e-6f && std::abs (mid.kneeY - 5.0f / 10.5f) < 1.0e-6f,
               "default Input/Output land at the centre of the pad");
        check (std::abs (mid.slopeT - 4.0f / 7.0f) < 1.0e-6f, "default 6:1 is slope step 4 of 0..7");
        check (std::abs (mid.attackT - 5.0f / 10.5f) < 1.0e-6f && std::abs (mid.releaseT - 5.0f / 10.5f) < 1.0e-6f,
               "default Attack/Release land at mid-travel");

        for (float knob : { 0.0f, 0.01f, 5.0f, 10.0f, 10.5f })
        {
            const auto n = Map::knobToNorm (knob);
            check (std::abs (Map::normToKnob (n) - knob) < 1.0e-4f, "knob " + fmt (knob, 2) + " round-trips through norm");
        }

        for (int r = 0; r < Map::numRatios; ++r)
        {
            Map::Params q;
            q.ratio = r;
            const auto back = Map::applySlope (q, Map::fromParams (q).slopeT);
            check (back.ratio == r, "ratio " + std::to_string (r) + " round-trips through the slope handle");
        }

        p = Map::applyKnee (p, 0.0f, 1.0f);
        check (std::abs (p.input - 0.0f) < 1.0e-4f && std::abs (p.output - 10.5f) < 1.0e-4f, "knee bottom-left/top maps to Input 0, Output 10.5");
        const auto kneePts = Map::fromParams (p);
        check (std::abs (kneePts.kneeX) < 1.0e-6f && std::abs (kneePts.kneeY - 1.0f) < 1.0e-6f, "knee point round-trips");

        p = Map::applyKnee (p, -0.4f, 1.7f);
        check (std::abs (p.input - 0.0f) < 1.0e-4f && std::abs (p.output - 10.5f) < 1.0e-4f, "knee coordinates clamp to 0..1");

        check (Map::snapRatio (-1.0f) == 0 && Map::snapRatio (0.0f) == 0, "slope below 0 snaps to 1:1");
        check (Map::snapRatio (1.0f) == 7 && Map::snapRatio (2.0f) == 7, "slope above 1 snaps to Nuke");
        check (Map::snapRatio (0.5f) == 4, "slope 0.5 snaps to 6:1 (index 4)");
        check (std::abs (Map::normToKnob (-2.0f)) < 1.0e-6f && std::abs (Map::normToKnob (3.0f) - 10.5f) < 1.0e-6f, "knob norms clamp at the edges");

        p = Map::applyAttack (Map::Params {}, -0.2f);
        check (std::abs (p.attack - 0.0f) < 1.0e-4f, "attack handle clamps at 0");
        p = Map::applyRelease (Map::Params {}, 1.4f);
        check (std::abs (p.release - 10.5f) < 1.0e-4f, "release handle clamps at 10.5");

        Map::Params round;
        round.input = 2.5f;
        round.output = 8.0f;
        round.attack = 1.0f;
        round.release = 9.5f;
        round.ratio = 6;
        const auto pts = Map::fromParams (round);
        auto back = Map::applyKnee (round, pts.kneeX, pts.kneeY);
        back = Map::applySlope (back, pts.slopeT);
        back = Map::applyAttack (back, pts.attackT);
        back = Map::applyRelease (back, pts.releaseT);
        check (std::abs (back.input - round.input) < 0.02f && std::abs (back.output - round.output) < 0.02f
               && std::abs (back.attack - round.attack) < 0.02f && std::abs (back.release - round.release) < 0.02f
               && back.ratio == round.ratio,
               "full handle set round-trips to the same parameters");
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                      testMetadata },
        { "1:1 near-unity with distortion off",            testOneToOneUnity },
        { "ratio curves and GR ordering",                  testRatioBehaviour },
        { "attack / release timing",                       testAttackRelease },
        { "10:1 opto and Nuke logarithmic release",        testOptoAndNukeRelease },
        { "audio high-pass 60 / 30 Hz",                    testAudioHp },
        { "detector HP and 6 kHz band emphasis",           testDetector },
        { "stereo link and mono dead-patch",               testStereoAndMonoLink },
        { "Mix phase alignment",                           testMixPhase },
        { "Bypass copies input and keeps GR metering",     testBypassKeepsMeter },
        { "Dist 2 even / Dist 3 odd, 1% and 3% LEDs",      testDistortion },
        { "Headroom 4..28 dB",                             testHeadroom },
        { "no NaN / denormals under automation",           testNoNanDenormalsAutomation },
        { "no dead parameters",                            testNoDeadParameters },
        { "state, presets, undo",                          testStatePresetsUndo },
        { "value text",                                    testValueText },
        { "editor: size, version, one control each, layout", testEditor },
        { "GR meter ballistics",                           testGrMeterBallistics },
        { "GR meter peak-hold",                            testGrMeterPeakHold },
        { "GR meter history buffer",                       testGrMeterHistory },
        { "compression-display handle map",                testCompressionMap },
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
