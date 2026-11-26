/*
    dooblr-L console tests: the real processor, limiter engine, loudness meter and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.

    Set DOOBLRL_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/HistoryTap.h"
#include "DSP/LearnSetup.h"
#include "DSP/LimiterEngine.h"
#include "DSP/Loudness.h"
#include "DSP/TruePeak.h"
#include "UI/Meters.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    thread_local bool countAllocations = false;
    thread_local int allocationCount = 0;
}

void* operator new (std::size_t size)
{
    if (countAllocations)
        ++allocationCount;
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
    using namespace dooblrl;
    using namespace dooblrl::params;
    using Processor = DooblrLProcessor;
    using Editor = DooblrLEditor;
    using Engine = dsp::LimiterEngine;

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

    Stereo sine (size_t n, double hz, double amp, double rate = defaultRate, double phase = 0.0)
    {
        Stereo s (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto v = (float) (amp * std::sin (twoPi * hz * (double) i / rate + phase));
            s.l[i] = s.r[i] = v;
        }
        return s;
    }

    float peak (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (auto v : x) m = std::max (m, std::abs (v));
        return m;
    }

    float rms (const std::vector<float>& x, size_t start, size_t count)
    {
        if (count == 0) return 0.0f;
        double acc = 0.0;
        for (size_t i = 0; i < count; ++i)
            acc += (double) x[start + i] * (double) x[start + i];
        return (float) std::sqrt (acc / (double) count);
    }

    bool allFinite (const Stereo& s)
    {
        for (auto v : s.l) if (! std::isfinite (v)) return false;
        for (auto v : s.r) if (! std::isfinite (v)) return false;
        return true;
    }

    void defaults (Processor& p)
    {
        for (auto* q : p.getParameters())
            q->setValueNotifyingHost (q->getDefaultValue());
    }

    bool isControl (juce::Component& c)
    {
        return dynamic_cast<juce::Slider*> (&c) != nullptr
            || dynamic_cast<juce::Button*> (&c) != nullptr
            || dynamic_cast<juce::ComboBox*> (&c) != nullptr
            || dynamic_cast<ui::SegmentedToggle*> (&c) != nullptr;
    }

    template <typename Fn>
    void forEachVisible (juce::Component& root, Fn&& fn)
    {
        if (! root.isVisible() || root.getWidth() <= 0 || root.getHeight() <= 0)
            return;
        fn (root);
        for (auto* child : root.getChildren())
            if (child != nullptr)
                forEachVisible (*child, fn);
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
                check (false, "createEditor returns DooblrLEditor");
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

    juce::MouseEvent makeMouseEvent (juce::Component& component, juce::Point<float> position,
                                     juce::Point<float> downPosition, int clicks, bool dragged,
                                     juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, clicks, dragged };
    }

    void simulateDrag (juce::Component& component, juce::Point<float> from, juce::Point<float> to,
                       juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        const auto flags = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
        component.mouseDown (makeMouseEvent (component, from, from, 1, false, flags));
        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            component.mouseDrag (makeMouseEvent (component, from + (to - from) * ((float) i / (float) steps),
                                                 from, 1, true, flags));
        component.mouseUp (makeMouseEvent (component, to, from, 1, true, flags));
    }

    void simulateDoubleClick (juce::Component& component, juce::Point<float> position)
    {
        const auto flags = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        component.mouseDown (makeMouseEvent (component, position, position, 1, false, flags));
        component.mouseUp (makeMouseEvent (component, position, position, 1, false, flags));
        component.mouseDoubleClick (makeMouseEvent (component, position, position, 2, false, flags));
    }

    float snapThreshold (Processor& p, float db)
    {
        auto* q = param (p, id::threshold);
        if (q == nullptr)
            return db;
        return q->convertFrom0to1 (q->convertTo0to1 (q->getNormalisableRange().snapToLegalValue (db)));
    }

    float independentYToThresholdDb (juce::Rectangle<float> plot, float y)
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - y) / juce::jmax (1.0f, plot.getHeight()));
        return ui::GrHistory::thresholdMinDb + t * (ui::GrHistory::thresholdMaxDb - ui::GrHistory::thresholdMinDb);
    }

    struct GestureCount final : juce::AudioProcessorParameter::Listener
    {
        int begins = 0, ends = 0;
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            if (starting) ++begins;
            else          ++ends;
        }
    };

    //------------------------------------------------------------------------------
    void testMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-L", "name is dooblr-L (got " + p->getName().toStdString() + ")");
        check (JucePlugin_IsSynth == 0 && ! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "an audio effect without MIDI");
        check (p->getMainBusNumInputChannels() == 2 && p->getMainBusNumOutputChannels() == 2, "stereo in / out by default");
        check (p->hasEditor(), "has an editor");
        check (juce::String (JucePlugin_VersionString) == DOOBLRL_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.1.3", "version 0.1.3");
        check (p->getBypassParameter() == p->getState().getParameter (id::bypass), "Bypass is the host bypass parameter");
        check ((int) p->getParameters().size() == 20, "20 automatable parameters (got " + std::to_string ((int) p->getParameters().size()) + ")");

        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->checkBusesLayoutSupported (layout), "mono in / out is supported");
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! p->checkBusesLayoutSupported (layout), "mono in / stereo out is refused");

        const char* ids[] {
            id::threshold, id::attack, id::release, id::knee, id::enhance, id::style,
            id::oversampling, id::lookahead, id::transientLink, id::releaseLink,
            id::inputGain, id::ceiling, id::unityGain, id::delta, id::autoComp,
            id::dither, id::shaping, id::lufsTarget, id::bypass, id::latencyMode
        };
        for (auto* pid : ids)
            check (p->getState().getParameter (pid) != nullptr, std::string (pid) + " is in the layout");

        check (std::abs (get (*p, id::oversampling) - 2.0f) < 0.01f, "default oversampling is 4x (index 2)");
        check (std::abs (get (*p, id::enhance)) < 0.01f, "default Enhance is 0");
        check (std::abs (get (*p, id::dither)) < 0.01f, "dither is Off by default");
        check (get (*p, id::autoComp) > 0.5f, "Auto Comp is on by default");
        check (std::abs (get (*p, id::latencyMode)) < 0.01f, "default latency mode is Zero");
        check (p->getLatencySamples() == 0, "Zero mode reports 0 latency (got " + std::to_string (p->getLatencySamples()) + ")");
        check (p->getLatencySamples() == p->getEngine().latencySamples(), "reported latency matches the engine");
        note ("version " JucePlugin_VersionString ", latency " + std::to_string (p->getLatencySamples()) + " samples");
    }

    void testCeiling()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::oversampling, 0.0f); // Off: isolate the sample-peak clip
        set (*p, id::dither, 0.0f);
        set (*p, id::unityGain, 0.0f);
        set (*p, id::delta, 0.0f);
        set (*p, id::inputGain, 12.0f);
        set (*p, id::threshold, -12.0f);
        set (*p, id::ceiling, -1.0f);
        set (*p, id::autoComp, 0.0f);
        set (*p, id::enhance, 50.0f);
        p->prepareToPlay (defaultRate, defaultBlock);

        const auto ceilLin = dsp::dbToLinear (-1.0f);
        const auto in = sine ((size_t) (0.6 * defaultRate), 997.0, 0.95, defaultRate, 0.4);
        const auto out = render (*p, in);
        check (allFinite (out), "ceiling test output is finite");
        const auto sp = std::max (peak (out.l), peak (out.r));
        check (sp <= ceilLin + 1.0e-4f, "sample peak never exceeds -1 dBTP ceiling ("
               + fmt (dsp::linearToDb (sp), 3) + " dB, ceiling " + fmt (dsp::linearToDb (ceilLin), 3) + ")");

        defaults (*p);
        set (*p, id::latencyMode, 1.0f); // Natural: Auto Comp has the 11-sample reconstruction delay
        set (*p, id::oversampling, 0.0f);
        set (*p, id::dither, 0.0f);
        set (*p, id::unityGain, 0.0f);
        set (*p, id::delta, 0.0f);
        set (*p, id::inputGain, 8.0f);
        set (*p, id::threshold, -8.0f);
        set (*p, id::ceiling, -0.3f);
        set (*p, id::autoComp, 1.0f); // true-peak catcher on
        set (*p, id::enhance, 40.0f);
        p->prepareToPlay (defaultRate, defaultBlock);

        const auto ceilTp = dsp::dbToLinear (-0.3f);
        const auto hot = sine ((size_t) (0.8 * defaultRate), 12007.0, 0.99, defaultRate, 0.7);
        const auto tpOut = render (*p, hot);
        check (allFinite (tpOut), "true-peak test output is finite");
        const auto sampleP = std::max (peak (tpOut.l), peak (tpOut.r));
        check (sampleP <= ceilTp + 1.5e-4f, "sample peak honours ceiling with Auto Comp ("
               + fmt (dsp::linearToDb (sampleP), 3) + " dB)");

        dsp::TruePeak meter;
        meter.prepare (defaultRate);
        const auto tp = std::max (meter.measureBuffer (tpOut.l.data(), (int) tpOut.l.size(), 0),
                                  meter.measureBuffer (tpOut.r.data(), (int) tpOut.r.size(), 1));
        check (tp <= ceilTp + 2.5e-3f, "true peak with Auto Comp never exceeds ceiling ("
               + fmt (dsp::linearToDb (tp), 3) + " dBTP vs " + fmt (dsp::linearToDb (ceilTp), 3) + ")");
        note ("sample " + fmt (dsp::linearToDb (sampleP), 3) + " dB, true-peak " + fmt (dsp::linearToDb (tp), 3) + " dBTP");
    }

    int impulsePeakIndex (const Stereo& out)
    {
        int peakAt = 0;
        float maxv = 0;
        for (int i = 0; i < (int) out.l.size(); ++i)
        {
            const auto v = std::abs (out.l[(size_t) i]);
            if (v > maxv) { maxv = v; peakAt = i; }
        }
        return peakAt;
    }

    void configureImpulse (Processor& p)
    {
        set (p, id::dither, 0.0f);
        set (p, id::autoComp, 0.0f);
        set (p, id::enhance, 0.0f);
        set (p, id::inputGain, 0.0f);
        set (p, id::threshold, 0.0f);
        set (p, id::unityGain, 0.0f);
        set (p, id::delta, 0.0f);
        set (p, id::bypass, 0.0f);
        set (p, id::lookahead, 2.0f);
    }

    void testLatency()
    {
        auto p = makeProcessor();
        defaults (*p);
        configureImpulse (*p);
        set (*p, id::latencyMode, 1.0f); // Natural: look + FIR
        set (*p, id::oversampling, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);

        const auto expected = p->getLatencySamples();
        check (expected == p->getEngine().latencySamples(), "getLatencySamples matches engine");
        check (expected == p->getEngine().lookaheadSamples(), "Natural, OS Off: latency is the lookahead delay ("
               + std::to_string (expected) + ")");

        Stereo in ((size_t) defaultRate / 10);
        in.l[0] = in.r[0] = 0.1f;
        const auto out = render (*p, in);
        const auto peakAt = impulsePeakIndex (out);
        check (std::abs (peakAt - expected) <= 1, "impulse peak at reported latency (got "
               + std::to_string (peakAt) + ", expected " + std::to_string (expected) + ")");

        set (*p, id::oversampling, 2.0f); // 4x
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto lat4 = p->getLatencySamples();
        check (lat4 == p->getEngine().latencySamples(), "4x: host latency matches engine");
        check (lat4 > expected, "4x oversampling adds FIR latency (" + std::to_string (lat4)
               + " vs " + std::to_string (expected) + " at 1x)");

        Stereo in4 ((size_t) defaultRate / 5);
        in4.l[0] = in4.r[0] = 0.08f;
        const auto out4 = render (*p, in4);
        const auto peak4 = impulsePeakIndex (out4);
        check (std::abs (peak4 - lat4) <= 2, "4x impulse peak at reported latency (got "
               + std::to_string (peak4) + ", expected " + std::to_string (lat4) + ")");
        note ("1x latency " + std::to_string (expected) + ", 4x " + std::to_string (lat4));

        defaults (*p);
        configureImpulse (*p);
        set (*p, id::latencyMode, 0.0f);
        set (*p, id::oversampling, 2.0f);
        set (*p, id::lookahead, 20.0f);
        set (*p, id::autoComp, 1.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        check (p->getLatencySamples() == 0, "Zero mode reports 0 latency with OS, look and Auto Comp on (got "
               + std::to_string (p->getLatencySamples()) + ")");
        Stereo zIn ((size_t) defaultRate / 10);
        zIn.l[0] = zIn.r[0] = 0.1f;
        const auto zOut = render (*p, zIn);
        const auto zPeak = impulsePeakIndex (zOut);
        check (std::abs (zPeak) <= 1, "Zero mode impulse is not delayed (peak at " + std::to_string (zPeak) + ")");

        defaults (*p);
        configureImpulse (*p);
        set (*p, id::latencyMode, 2.0f); // High: at least 8 ms look + FIR
        set (*p, id::oversampling, 2.0f);
        set (*p, id::lookahead, 2.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto latHigh = p->getLatencySamples();
        check (latHigh == p->getEngine().latencySamples(), "High: host latency matches engine");
        check (latHigh > lat4, "High latency exceeds Natural 4x (" + std::to_string (latHigh)
               + " vs " + std::to_string (lat4) + ")");
        Stereo hIn ((size_t) defaultRate / 5);
        hIn.l[0] = hIn.r[0] = 0.08f;
        const auto hOut = render (*p, hIn);
        const auto hPeak = impulsePeakIndex (hOut);
        check (std::abs (hPeak - latHigh) <= 2, "High impulse peak at reported latency (got "
               + std::to_string (hPeak) + ", expected " + std::to_string (latHigh) + ")");
    }

    void testEnhanceTransparency()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::oversampling, 0.0f);
        set (*p, id::dither, 0.0f);
        set (*p, id::autoComp, 0.0f);
        set (*p, id::unityGain, 0.0f);
        set (*p, id::delta, 0.0f);
        set (*p, id::inputGain, 6.0f);
        set (*p, id::threshold, -8.0f);
        set (*p, id::ceiling, -0.3f);
        set (*p, id::style, 3.0f);

        Stereo burst ((size_t) (0.4 * defaultRate));
        for (size_t i = 0; i < burst.size(); ++i)
        {
            const auto env = i < 64 ? 1.0f : std::exp (-(float) (i - 64) / 400.0f);
            const auto v = env * (float) (0.9 * std::sin (twoPi * 80.0 * (double) i / defaultRate));
            burst.l[i] = burst.r[i] = v;
        }

        set (*p, id::enhance, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto out0 = render (*p, burst);

        // Same settings through the engine's pre-process-only path.
        Engine e;
        dsp::Settings s = p->readSettings();
        s.enhance = 0;
        e.setSettings (s);
        e.prepare (defaultRate, defaultBlock, 0);
        Stereo pre (burst.size());
        juce::AudioBuffer<float> buf (2, defaultBlock);
        size_t idx = 0;
        while (idx < burst.size())
        {
            const auto n = (int) std::min ((size_t) defaultBlock, burst.size() - idx);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, burst.l[idx + (size_t) i]);
                buf.setSample (1, i, burst.r[idx + (size_t) i]);
            }
            float* io[2] { buf.getWritePointer (0), buf.getWritePointer (1) };
            e.processPreProcessOnly (io, 2, n);
            for (int i = 0; i < n; ++i)
            {
                pre.l[idx + (size_t) i] = buf.getSample (0, i);
                pre.r[idx + (size_t) i] = buf.getSample (1, i);
            }
            idx += (size_t) n;
        }

        float maxDiff = 0;
        for (size_t i = 0; i < out0.l.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (out0.l[i] - pre.l[i]));
        check (maxDiff < 1.5e-4f, "Enhance=0 nulls against pre-process-only (max diff " + fmt (maxDiff, 7) + ")");

        set (*p, id::enhance, 80.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto outH = render (*p, burst);
        float maxDiffH = 0;
        for (size_t i = 0; i < out0.l.size(); ++i)
            maxDiffH = std::max (maxDiffH, std::abs (outH.l[i] - out0.l[i]));
        check (maxDiffH > 1.0e-3f, "Enhance 80 reshapes transients vs Enhance 0 (max diff " + fmt (maxDiffH, 5) + ")");

        // Below threshold: enhance has no residual to work on.
        defaults (*p);
        set (*p, id::oversampling, 0.0f);
        set (*p, id::dither, 0.0f);
        set (*p, id::autoComp, 0.0f);
        set (*p, id::threshold, 0.0f);
        set (*p, id::inputGain, -12.0f);
        set (*p, id::enhance, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto quiet = sine ((size_t) (0.25 * defaultRate), 440.0, 0.05);
        const auto q0 = render (*p, quiet);
        set (*p, id::enhance, 100.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto q1 = render (*p, quiet);
        float qDiff = 0;
        for (size_t i = 0; i < q0.l.size(); ++i)
            qDiff = std::max (qDiff, std::abs (q0.l[i] - q1.l[i]));
        check (qDiff < 2.0e-3f, "Enhance is transparent below threshold (max diff " + fmt (qDiff, 6) + ")");
    }

    void testStyles()
    {
        Stereo burst ((size_t) (0.35 * defaultRate));
        for (size_t i = 0; i < burst.size(); ++i)
        {
            const auto click = i < 32 ? 0.95f * (1.0f - (float) i / 32.0f) : 0.0f;
            const auto body = (float) (0.7 * std::sin (twoPi * 55.0 * (double) i / defaultRate)
                                       * std::exp (-(double) i / (0.08 * defaultRate)));
            burst.l[i] = burst.r[i] = click + body;
        }

        std::array<Stereo, 8> outs;
        std::array<float, 8> peaks {}, rmses {}, thd {};

        for (int st = 0; st < 8; ++st)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::oversampling, 0.0f);
            set (*p, id::dither, 0.0f);
            set (*p, id::autoComp, 0.0f);
            set (*p, id::unityGain, 0.0f);
            set (*p, id::delta, 0.0f);
            set (*p, id::style, (float) st);
            set (*p, id::inputGain, 6.0f);
            set (*p, id::threshold, -8.0f);
            set (*p, id::enhance, 25.0f);
            set (*p, id::attack, 0.5f);
            set (*p, id::release, 50.0f);
            p->prepareToPlay (defaultRate, defaultBlock);
            outs[(size_t) st] = render (*p, burst);
            const auto skip = (size_t) (0.02 * defaultRate);
            peaks[(size_t) st] = peak (outs[(size_t) st].l);
            rmses[(size_t) st] = rms (outs[(size_t) st].l, skip, outs[(size_t) st].l.size() - skip);

            // Rough 3rd-harmonic probe at 165 Hz vs 55 Hz.
            std::complex<double> f1 { 0, 0 }, f3 { 0, 0 };
            const auto w1 = twoPi * 55.0 / defaultRate, w3 = twoPi * 165.0 / defaultRate;
            for (size_t i = skip; i < outs[(size_t) st].l.size(); ++i)
            {
                const auto x = (double) outs[(size_t) st].l[i];
                f1 += x * std::exp (std::complex<double> (0.0, -w1 * (double) i));
                f3 += x * std::exp (std::complex<double> (0.0, -w3 * (double) i));
            }
            thd[(size_t) st] = (float) (std::abs (f3) / std::max (1.0e-12, std::abs (f1)));
            check (allFinite (outs[(size_t) st]), styleNames()[st].toStdString() + " output is finite");
        }

        int distinctPairs = 0;
        for (int a = 0; a < 8; ++a)
            for (int b = a + 1; b < 8; ++b)
            {
                float md = 0;
                const auto n = std::min (outs[(size_t) a].l.size(), outs[(size_t) b].l.size());
                for (size_t i = n / 10; i < n; ++i)
                    md = std::max (md, std::abs (outs[(size_t) a].l[i] - outs[(size_t) b].l[i]));
                const auto rmsDiff = std::abs (dsp::linearToDb (rmses[(size_t) a] + 1.0e-12f)
                                               - dsp::linearToDb (rmses[(size_t) b] + 1.0e-12f));
                const auto thdDiff = std::abs (thd[(size_t) a] - thd[(size_t) b]);
                const auto different = md > 2.0e-3f || rmsDiff > 0.15f || thdDiff > 0.02f;
                check (different, styleNames()[a].toStdString() + " vs " + styleNames()[b].toStdString()
                       + " is a different algorithm (diff " + fmt (md, 5) + ", RMS "
                       + fmt (rmsDiff, 2) + " dB, H3 " + fmt (thdDiff, 3) + ")");
                if (different) ++distinctPairs;
            }
        check (distinctPairs == 28, "all 28 style pairs differ (" + std::to_string (distinctPairs) + ")");
    }

    void testLufs()
    {
        // EBU Tech 3341-style 1 kHz stereo sine. Peak amplitude 10^(-23/20) → -23 LUFS.
        dsp::LoudnessMeter meter;
        meter.prepare (defaultRate);
        const auto amp = std::pow (10.0, -23.0 / 20.0);
        const auto n = (size_t) (5.0 * defaultRate);
        std::vector<float> l (n), r (n);
        for (size_t i = 0; i < n; ++i)
            l[i] = r[i] = (float) (amp * std::sin (twoPi * 1000.0 * (double) i / defaultRate));

        const auto block = 4096;
        for (size_t i = 0; i < n; i += (size_t) block)
        {
            const auto m = std::min ((size_t) block, n - i);
            meter.process (l.data() + i, r.data() + i, (int) m, 2);
        }

        check (std::abs (meter.momentaryLufs() + 23.0f) < 0.3f, "momentary LUFS of -23 dBFS 1 kHz sine is -23 ± 0.3 (got "
               + fmt (meter.momentaryLufs(), 2) + ")");
        check (std::abs (meter.shortTermLufs() + 23.0f) < 0.3f, "short-term LUFS of the same tone is -23 ± 0.3 (got "
               + fmt (meter.shortTermLufs(), 2) + ")");
        check (meter.hasIntegrated() && std::abs (meter.integratedLufs() + 23.0f) < 0.3f,
               "integrated LUFS of the same tone is -23 ± 0.3 (got " + fmt (meter.integratedLufs(), 2) + ")");
        check (meter.loudnessRange() < 1.0f, "LRA of a constant sine is under 1 LU (got " + fmt (meter.loudnessRange(), 2) + ")");

        meter.reset();
        const auto amp20 = std::pow (10.0, -20.0 / 20.0);
        for (size_t i = 0; i < n; ++i)
            l[i] = r[i] = (float) (amp20 * std::sin (twoPi * 1000.0 * (double) i / defaultRate));
        for (size_t i = 0; i < n; i += (size_t) block)
        {
            const auto m = std::min ((size_t) block, n - i);
            meter.process (l.data() + i, r.data() + i, (int) m, 2);
        }
        check (std::abs (meter.momentaryLufs() + 20.0f) < 0.3f, "-20 dBFS 1 kHz sine reads -20 LUFS (got "
               + fmt (meter.momentaryLufs(), 2) + ")");

        // Plugin-level: the processor's meter agrees on a 1 kHz tone (bypass so the limiter is out).
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::bypass, 1.0f);
        set (*p, id::oversampling, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);
        p->resetMeters();
        const auto tone = sine (n, 1000.0, amp);
        render (*p, tone);
        const auto m = p->takeMeters();
        check (std::abs (m.momentaryLufs + 23.0f) < 0.5f, "processor momentary LUFS on bypassed -23 dBFS sine (got "
               + fmt (m.momentaryLufs, 2) + ")");
        note ("M " + fmt (meter.momentaryLufs(), 2) + "  S " + fmt (meter.shortTermLufs(), 2)
              + "  I " + fmt (meter.integratedLufs(), 2) + "  LRA " + fmt (meter.loudnessRange(), 2));
    }

    void testStereoLink()
    {
        Stereo in ((size_t) (0.4 * defaultRate));
        for (size_t i = 0; i < in.size(); ++i)
        {
            const auto burst = i > 2000 && i < 4000 ? 0.95f : 0.02f;
            in.l[i] = burst * (float) std::sin (twoPi * 90.0 * (double) i / defaultRate);
            in.r[i] = 0.02f * (float) std::sin (twoPi * 90.0 * (double) i / defaultRate);
        }

        auto run = [&] (float tLink, float rLink)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::oversampling, 0.0f);
            set (*p, id::dither, 0.0f);
            set (*p, id::autoComp, 0.0f);
            set (*p, id::enhance, 0.0f);
            set (*p, id::inputGain, 0.0f);
            set (*p, id::threshold, -6.0f);
            set (*p, id::attack, 0.05f);
            set (*p, id::release, 80.0f);
            set (*p, id::transientLink, tLink);
            set (*p, id::releaseLink, rLink);
            p->prepareToPlay (defaultRate, defaultBlock);
            return render (*p, in);
        };

        const auto unlinked = run (0.0f, 0.0f);
        const auto linked = run (100.0f, 100.0f);

        const auto skip = 2100, count = 1500;
        const auto unL = rms (unlinked.l, (size_t) skip, (size_t) count);
        const auto unR = rms (unlinked.r, (size_t) skip, (size_t) count);
        const auto lnL = rms (linked.l, (size_t) skip, (size_t) count);
        const auto lnR = rms (linked.r, (size_t) skip, (size_t) count);

        check (unL > 1.0e-4f && unR > 1.0e-5f, "unlinked output has energy on both channels");
        check (unR > lnR * 1.25f, "0% link leaves the quiet right channel louder than 100% link (unlinked R "
               + fmt (unR, 4) + ", linked R " + fmt (lnR, 4) + ")");
        check (lnR < unR * 0.85f && lnL > 1.0e-5f,
               "100% link ducks the quiet right channel with the left (linked R "
               + fmt (lnR, 4) + " vs unlinked " + fmt (unR, 4) + ")");

        const auto transOnly = run (100.0f, 0.0f);
        const auto relOnly = run (0.0f, 100.0f);
        float d = 0;
        for (size_t i = (size_t) skip; i < (size_t) (skip + count); ++i)
            d = std::max (d, std::abs (transOnly.r[i] - relOnly.r[i]));
        check (d > 1.0e-4f, "transient link and release link are independent (max R diff " + fmt (d, 5) + ")");
    }

    void testOversampling()
    {
        // 19 kHz sine, clipped by Aggressive: 5th harmonic aliases to 1 kHz at 48 kHz.
        const auto in = sine ((size_t) (0.25 * defaultRate), 19000.0, 0.95);

        std::array<int, 6> latencies {};
        std::array<float, 6> hf {};
        for (int idx = 0; idx < 6; ++idx)
        {
            auto p = makeProcessor();
            defaults (*p);
            set (*p, id::latencyMode, 1.0f); // Natural: FIR oversampling reports its delay
            set (*p, id::oversampling, (float) idx);
            set (*p, id::dither, 0.0f);
            set (*p, id::autoComp, 0.0f);
            set (*p, id::enhance, 0.0f);
            set (*p, id::style, 4.0f); // Aggressive: lots of harmonics to alias
            set (*p, id::inputGain, 12.0f);
            set (*p, id::threshold, -12.0f);
            set (*p, id::enhance, 60.0f);
            p->prepareToPlay (defaultRate, defaultBlock);
            latencies[(size_t) idx] = p->getLatencySamples();
            check (latencies[(size_t) idx] == p->getEngine().latencySamples(),
                   oversamplingNames()[idx].toStdString() + " latency matches engine ("
                   + std::to_string (latencies[(size_t) idx]) + ")");
            check (p->getEngine().oversamplingFactor() == oversamplingFactor (idx),
                   oversamplingNames()[idx].toStdString() + " factor is " + std::to_string (oversamplingFactor (idx)));

            const auto out = render (*p, in);
            check (allFinite (out), oversamplingNames()[idx].toStdString() + " output is finite");

            // 1 kHz bin: 5 * 19 kHz = 95 kHz → 95k - 2*48k = -1 kHz.
            double tot = 0;
            std::complex<double> bin {};
            const auto w = twoPi * 1000.0 / defaultRate;
            const auto skip = (size_t) latencies[(size_t) idx] + 64;
            for (size_t i = skip; i < out.l.size(); ++i)
            {
                const auto x = (double) out.l[i];
                tot += x * x;
                bin += x * std::exp (std::complex<double> (0.0, -w * (double) i));
            }
            hf[(size_t) idx] = (float) (std::abs (bin) / std::max (1.0e-12, std::sqrt (tot * (double) std::max ((size_t) 1, out.l.size() - skip))));
        }

        check (latencies[0] < latencies[2] && latencies[2] <= latencies[5],
               "latency grows with oversampling (1x " + std::to_string (latencies[0])
               + ", 4x " + std::to_string (latencies[2]) + ", 32x " + std::to_string (latencies[5]) + ")");
        check (hf[0] > 1.0e-4f, "Off produces measurable 1 kHz alias energy from a clipped 19 kHz tone ("
               + fmt (hf[0], 5) + ")");
        check (hf[2] < hf[0] * 0.85f, "4x reduces 1 kHz alias energy vs Off ("
               + fmt (hf[2], 5) + " vs " + fmt (hf[0], 5) + ")");
        note ("OS latencies: Off " + std::to_string (latencies[0]) + "  2x " + std::to_string (latencies[1])
              + "  4x " + std::to_string (latencies[2]) + "  8x " + std::to_string (latencies[3])
              + "  16x " + std::to_string (latencies[4]) + "  32x " + std::to_string (latencies[5]));
    }

    void testStatePresetsUndo()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::threshold, -12.0f);
        set (*p, id::enhance, 40.0f);
        set (*p, id::style, 4.0f);
        set (*p, id::ceiling, -1.5f);

        juce::MemoryBlock blob;
        p->getStateInformation (blob);
        check (blob.getSize() > 16, "state blob is non-empty");

        auto q = makeProcessor();
        defaults (*q);
        q->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (get (*q, id::threshold) + 12.0f) < 0.05f, "state restores Threshold");
        check (std::abs (get (*q, id::enhance) - 40.0f) < 0.05f, "state restores Enhance");
        check (std::abs (get (*q, id::style) - 4.0f) < 0.05f, "state restores Style");
        check (std::abs (get (*q, id::ceiling) + 1.5f) < 0.05f, "state restores Ceiling");

        const juce::File dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("dooblr-L-preset-test"));
        dir.createDirectory();
        const auto file = dir.getChildFile ("roundtrip.dl");
        file.deleteFile();
        const auto saveErr = p->saveUserPreset (file);
        check (saveErr.startsWith ("Saved"), "saveUserPreset: " + saveErr.toStdString());
        defaults (*q);
        const auto loadErr = q->loadUserPreset (file);
        check (loadErr.startsWith ("Loaded"), "loadUserPreset: " + loadErr.toStdString());
        check (std::abs (get (*q, id::enhance) - 40.0f) < 0.05f, "user preset restores Enhance");
        file.deleteFile();
        dir.deleteRecursively();

        check ((int) presets::factory().size() >= 8, "factory presets cover each style");
        int modernIndex = -1;
        for (int i = 0; i < q->getNumPrograms(); ++i)
            if (q->getProgramName (i) == "Modern Stream")
                modernIndex = i;
        check (modernIndex >= 0, "Modern Stream is a factory preset");
        if (modernIndex >= 0)
        {
            q->setCurrentProgram (modernIndex);
            check (std::abs (get (*q, id::style) - 5.0f) < 0.01f, "Modern Stream factory preset selects Modern");
        }

        {
            auto saved = makeProcessor();
            defaults (*saved);
            set (*saved, id::latencyMode, 2.0f);
            set (*saved, id::threshold, -9.0f);
            juce::MemoryBlock highBlob;
            saved->getStateInformation (highBlob);
            auto loaded = makeProcessor();
            defaults (*loaded);
            check (std::abs (get (*loaded, id::latencyMode)) < 0.01f, "fresh instance is Zero");
            loaded->setStateInformation (highBlob.getData(), (int) highBlob.getSize());
            check (std::abs (get (*loaded, id::latencyMode) - 2.0f) < 0.01f, "state restores Latency High");
            check (std::abs (get (*loaded, id::threshold) + 9.0f) < 0.05f, "state still restores Threshold");
        }
    }

    void testNoDeadParameters()
    {
        Stereo in ((size_t) (0.55 * defaultRate));
        for (size_t i = 0; i < in.size(); ++i)
        {
            const auto burst = ((i / 900) % 2 == 0) ? 1.0f : 0.12f;
            const auto click = (i % 900) < 12 ? 0.95f : 0.0f;
            in.l[i] = burst * (float) (0.72 * std::sin (twoPi * 90.0 * (double) i / defaultRate)
                                       + 0.40 * std::sin (twoPi * 12007.0 * (double) i / defaultRate))
                      + click;
            in.r[i] = 0.10f * (float) std::sin (twoPi * 140.0 * (double) i / defaultRate);
        }

        auto snapshot = [&] (Processor& p)
        {
            p.prepareToPlay (defaultRate, defaultBlock);
            return render (p, in);
        };

        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::latencyMode, 1.0f); // Natural so Lookahead and OS delays are audible
        set (*p, id::oversampling, 0.0f); // keep OS Off so other params aren't fighting FIR delay
        set (*p, id::threshold, -8.0f);
        set (*p, id::inputGain, 6.0f);
        set (*p, id::enhance, 0.0f);
        const auto base = snapshot (*p);

        struct Case { const char* pid; float value; const char* why; };
        const Case cases[] {
            { id::threshold, -18.0f, "Threshold" },
            { id::attack, 8.0f, "Attack" },
            { id::release, 400.0f, "Release" },
            { id::knee, 10.0f, "Knee" },
            { id::enhance, 70.0f, "Enhance" },
            { id::style, 4.0f, "Style Aggressive" },
            { id::lookahead, 10.0f, "Lookahead" },
            { id::latencyMode, 2.0f, "Latency High" },
            { id::transientLink, 0.0f, "Transient Link" },
            { id::releaseLink, 0.0f, "Release Link" },
            { id::inputGain, 8.0f, "Input" },
            { id::ceiling, -3.0f, "Ceiling" },
            { id::unityGain, 1.0f, "Unity Gain" },
            { id::delta, 1.0f, "Delta" },
            { id::autoComp, 0.0f, "Auto Comp off" },
            { id::bypass, 1.0f, "Bypass" },
        };

        for (const auto& c : cases)
        {
            defaults (*p);
            set (*p, id::latencyMode, 1.0f);
            set (*p, id::oversampling, 0.0f);
            set (*p, id::threshold, -8.0f);
            set (*p, id::inputGain, 6.0f);
            set (*p, id::enhance, 0.0f);
            set (*p, c.pid, c.value);
            const auto out = snapshot (*p);
            float diff = 0.0f;
            const auto skip = (size_t) p->getLatencySamples() + 64;
            for (size_t i = skip; i < in.size(); ++i)
            {
                diff = std::max (diff, std::abs (out.l[i] - base.l[i]));
                diff = std::max (diff, std::abs (out.r[i] - base.r[i]));
            }
            check (diff > 1.0e-4f, std::string (c.why) + " changes the audio (max diff " + fmt (diff, 6) + ")");
        }

        {
            // LUFS target waits for a full 3 s short-term window, then rides level.
            Stereo longTone ((size_t) (4.2 * defaultRate));
            const auto amp = (float) std::pow (10.0, -10.0 / 20.0);
            for (size_t i = 0; i < longTone.size(); ++i)
                longTone.l[i] = longTone.r[i] = amp * (float) std::sin (twoPi * 1000.0 * (double) i / defaultRate);

            auto renderLong = [&] (float target)
            {
                defaults (*p);
                set (*p, id::oversampling, 0.0f);
                set (*p, id::autoComp, 0.0f);
                set (*p, id::threshold, 0.0f);
                set (*p, id::inputGain, 0.0f);
                set (*p, id::enhance, 0.0f);
                set (*p, id::lufsTarget, target);
                p->prepareToPlay (defaultRate, defaultBlock);
                p->resetMeters();
                return render (*p, longTone);
            };
            const auto off = renderLong (0.0f);
            const auto on = renderLong (1.0f);
            float diff = 0;
            const auto skip = (size_t) (3.2 * defaultRate);
            for (size_t i = skip; i < off.l.size(); ++i)
                diff = std::max (diff, std::abs (off.l[i] - on.l[i]));
            check (diff > 1.0e-3f, "LUFS Target changes the audio after the short-term window fills (max diff "
                   + fmt (diff, 6) + ")");
        }

        {
            defaults (*p);
            set (*p, id::oversampling, 0.0f);
            set (*p, id::dither, 1.0f);
            set (*p, id::shaping, 0.0f);
            const auto d0 = snapshot (*p);
            set (*p, id::shaping, 3.0f);
            const auto d1 = snapshot (*p);
            float diff = 0;
            for (size_t i = 0; i < d0.l.size(); ++i)
                diff = std::max (diff, std::abs (d0.l[i] - d1.l[i]));
            check (diff > 1.0e-6f, "Shaping changes dithered audio (max diff " + fmt (diff, 7) + ")");
        }

        {
            defaults (*p);
            set (*p, id::latencyMode, 1.0f);
            set (*p, id::oversampling, 0.0f);
            const auto o0 = snapshot (*p);
            set (*p, id::oversampling, 2.0f);
            const auto o1 = snapshot (*p);
            float diff = 0;
            for (size_t i = 0; i < o0.l.size(); ++i)
                diff = std::max (diff, std::abs (o0.l[i] - o1.l[i]));
            check (diff > 1.0e-4f, "Oversampling 4x changes the audio vs Off (max diff " + fmt (diff, 6) + ")");
        }
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
                rects.push_back (std::make_pair (&c, editor->getLocalArea (c.getParentComponent(), c.getBounds())));
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
                    check (false, label + ": " + c->getComponentID().toStdString() + " large enough ("
                           + r.toString().toStdString() + ")");
                }
                for (size_t j = i + 1; j < rects.size(); ++j)
                {
                    const auto& [d, s] = rects[j];
                    if (c->isParentOf (d) || d->isParentOf (c)) continue;
                    if (! r.getIntersection (s).isEmpty())
                    {
                        ++overlaps;
                        check (false, label + ": " + c->getComponentID().toStdString() + " overlaps "
                               + d->getComponentID().toStdString() + " " + r.toString().toStdString()
                               + " / " + s.toString().toStdString());
                    }
                }
            }
            check (overlaps == 0 && outside == 0 && tooSmall == 0,
                   label + " layout fit (" + std::to_string (rects.size()) + " controls)");

            int valueBoxesOutside = 0;
            forEachVisible (*editor, [&] (juce::Component& c)
            {
                auto* slider = dynamic_cast<juce::Slider*> (&c);
                if (slider == nullptr) return;
                auto* panel = slider->getParentComponent();
                if (panel == nullptr) return;
                const auto sliderInPanel = panel->getLocalArea (slider, slider->getLocalBounds());
                if (! panel->getLocalBounds().contains (sliderInPanel))
                {
                    ++valueBoxesOutside;
                    check (false, label + ": " + slider->getComponentID().toStdString()
                           + " slider inside its panel (" + sliderInPanel.toString().toStdString()
                           + " in " + panel->getLocalBounds().toString().toStdString() + ")");
                }
                for (auto* child : slider->getChildren())
                {
                    if (child == nullptr) continue;
                    if (dynamic_cast<juce::Label*> (child) == nullptr
                        && dynamic_cast<juce::TextEditor*> (child) == nullptr)
                        continue;
                    const auto box = panel->getLocalArea (child, child->getLocalBounds());
                    if (! panel->getLocalBounds().contains (box))
                    {
                        ++valueBoxesOutside;
                        check (false, label + ": " + slider->getComponentID().toStdString()
                               + " value box inside its panel (" + box.toString().toStdString()
                               + " in " + panel->getLocalBounds().toString().toStdString() + ")");
                    }
                }
            });
            check (valueBoxesOutside == 0, label + " knob value boxes sit fully inside their panel");

            const auto hist = editor->getGrHistory().getBounds();
            check (editor->getLocalBounds().contains (hist) && hist.getWidth() >= 160 && hist.getHeight() >= 60,
                   label + " GR history fits (" + hist.toString().toStdString() + ")");
        }

        // Paint budget: scrolling I/O + GR history at 1x, all three sizes.
        {
            std::string report;
            double worstAvg = 0.0;
            const juce::Point<int> paintSizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
            for (auto size : paintSizes)
            {
                editor->setSize (size.x, size.y);
                auto& g = editor->getGrHistory();
                juce::Image image (juce::Image::ARGB, juce::jmax (2, g.getWidth()), juce::jmax (2, g.getHeight()), true);
                juce::Graphics gr (image);
                g.paintEntireComponent (gr, true);
                constexpr int frames = 80;
                double total = 0.0;
                for (int i = 0; i < frames; ++i)
                {
                    const auto t = (float) i * 0.2f;
                    g.pushColumn ({ -4.0f + 3.0f * std::sin (t), -8.0f + 2.0f * std::sin (t), 6.0f + 2.0f * std::sin (t) });
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    g.paintEntireComponent (gr, true);
                    total += juce::Time::getMillisecondCounterHiRes() - t0;
                }
                const auto avg = total / frames;
                worstAvg = std::max (worstAvg, avg);
                report += "  " + std::to_string (size.x) + "x" + std::to_string (size.y) + ": " + fmt (avg, 2) + " ms avg;";
            }
            note ("level history paint (software renderer):" + report);
            check (worstAvg < 4.0, "a level-history frame paints in under 4 ms on average at 1x ("
                   + fmt (worstAvg, 2) + " ms)");
        }
    }

    void testThresholdLineDrag()
    {
        EditorFixture f;
        if (f.editor == nullptr)
            return;

        auto& editor = *f.editor;
        auto& proc = *f.processor;
        auto& graph = editor.getGrHistory();
        auto* knob = dynamic_cast<juce::Slider*> (editor.findControl (id::threshold));
        auto* threshold = param (proc, id::threshold);
        check (knob != nullptr && threshold != nullptr, "Thresh knob and threshold parameter exist");
        if (knob == nullptr || threshold == nullptr)
            return;

        GestureCount gestures;
        threshold->addListener (&gestures);

        const juce::Point<int> sizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
        for (auto size : sizes)
        {
            editor.setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            set (proc, id::threshold, params::thresholdDefault);
            const auto plot = graph.plotBounds();
            check (plot.getWidth() > 40.0f && plot.getHeight() > 20.0f,
                   label + " GR plot is laid out (" + plot.toString().toStdString() + ")");

            const auto from = juce::Point<float> (plot.getCentreX(), graph.getThresholdLineY());
            const auto targetY = plot.getY() + plot.getHeight() * 0.65f;
            const auto linear = independentYToThresholdDb (plot, targetY);
            check (std::abs (graph.yToThresholdDb (targetY) - linear) < 0.02f,
                   label + " yToThresholdDb matches 0 dB at top / −40 dB at bottom");

            const auto expected = snapThreshold (proc, linear);
            const auto beginsBefore = gestures.begins, endsBefore = gestures.ends;
            simulateDrag (graph, from, { from.x, targetY });

            const auto got = get (proc, id::threshold);
            check (std::abs (got - expected) < 0.15f,
                   label + " drag maps to " + fmt (expected, 1) + " dB (got " + fmt (got, 1) + ")");
            check (std::abs (graph.getThresholdDb() - got) < 0.05f
                       && std::abs ((float) knob->getValue() - got) < 0.05f,
                   label + " knob and line follow the drag (" + fmt ((float) knob->getValue(), 1)
                       + " / " + fmt (graph.getThresholdDb(), 1) + ")");
            check (std::abs (graph.getThresholdLineY() - graph.thresholdDbToY (got)) < 0.6f,
                   label + " line Y matches the parameter");
            check (gestures.begins == beginsBefore + 1 && gestures.ends == endsBefore + 1,
                   label + " drag sends one begin and one end gesture (begin "
                       + std::to_string (gestures.begins - beginsBefore) + ", end "
                       + std::to_string (gestures.ends - endsBefore) + ")");
        }

        editor.setSize (1080, 660);
        set (proc, id::threshold, -18.0f);
        check (std::abs (graph.getThresholdDb() + 18.0f) < 0.05f
                   && std::abs ((float) knob->getValue() + 18.0f) < 0.05f
                   && std::abs (graph.getThresholdLineY() - graph.thresholdDbToY (-18.0f)) < 0.6f,
               "setting the parameter moves the knob and the line");

        {
            const auto onLine = juce::Point<float> (graph.plotBounds().getCentreX(), graph.getThresholdLineY());
            const auto beginsBefore = gestures.begins, endsBefore = gestures.ends;
            simulateDoubleClick (graph, onLine);
            check (std::abs (get (proc, id::threshold) - params::thresholdDefault) < 0.05f
                       && std::abs (graph.getThresholdDb() - params::thresholdDefault) < 0.05f
                       && std::abs ((float) knob->getValue() - params::thresholdDefault) < 0.05f,
                   "double-click on the line resets threshold to " + fmt (params::thresholdDefault, 1) + " dB");
            check (gestures.begins > beginsBefore && gestures.ends == gestures.begins,
                   "double-click reset sends begin and end gestures (begin "
                       + std::to_string (gestures.begins - beginsBefore) + ", end "
                       + std::to_string (gestures.ends - endsBefore) + ")");
        }

        {
            set (proc, id::threshold, params::thresholdDefault);
            const auto plot = graph.plotBounds();
            const auto from = juce::Point<float> (plot.getCentreX(), graph.getThresholdLineY());
            const auto to = from + juce::Point<float> (0.0f, plot.getHeight() * 0.4f);
            simulateDrag (graph, from, to);
            const auto coarse = get (proc, id::threshold);
            set (proc, id::threshold, params::thresholdDefault);
            simulateDrag (graph, from, to, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
            const auto fineShift = get (proc, id::threshold);
            set (proc, id::threshold, params::thresholdDefault);
            simulateDrag (graph, from, to, juce::ModifierKeys (juce::ModifierKeys::commandModifier));
            const auto fineCmd = get (proc, id::threshold);
            check (std::abs (fineShift - params::thresholdDefault) < 0.35f * std::abs (coarse - params::thresholdDefault)
                       && std::abs (fineCmd - params::thresholdDefault) < 0.35f * std::abs (coarse - params::thresholdDefault),
                   "Shift and Cmd drags are fine (coarse " + fmt (coarse, 1) + ", shift " + fmt (fineShift, 1)
                       + ", cmd " + fmt (fineCmd, 1) + ")");
        }

        {
            set (proc, id::threshold, params::thresholdDefault);
            const auto onLine = juce::Point<float> (graph.plotBounds().getCentreX(), graph.getThresholdLineY());
            graph.mouseMove (makeMouseEvent (graph, onLine, onLine, 0, false, juce::ModifierKeys()));
            check (graph.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor),
                   "hovering the line uses the up/down resize cursor");
            const auto away = juce::Point<float> (onLine.x, graph.plotBounds().getBottom() - 2.0f);
            graph.mouseMove (makeMouseEvent (graph, away, away, 0, false, juce::ModifierKeys()));
            check (graph.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::NormalCursor),
                   "leaving the line restores the normal cursor");
        }

        threshold->removeListener (&gestures);
    }

    void configureGrabbingLimiter (Processor& p)
    {
        set (p, id::oversampling, 0.0f);
        set (p, id::latencyMode, 0.0f);
        set (p, id::threshold, -12.0f);
        set (p, id::attack, 0.01f);
        set (p, id::release, 5.0f);
        set (p, id::lookahead, 0.0f);
        set (p, id::knee, 0.0f);
        set (p, id::enhance, 0.0f);
        set (p, id::style, 0.0f); // Transparent
        set (p, id::inputGain, 0.0f);
        set (p, id::ceiling, -0.3f);
        set (p, id::autoComp, 0.0f);
        set (p, id::lufsTarget, 0.0f);
        set (p, id::unityGain, 0.0f);
        set (p, id::delta, 0.0f);
        set (p, id::bypass, 0.0f);
    }

    void pollUntilEmpty (Editor& editor)
    {
        for (int i = 0; i < 8; ++i)
            editor.pollMeters();
    }

    void writeSnapshot (Editor& editor, const char* suffix)
    {
        const auto prefix = std::getenv ("DOOBLRL_EDITOR_SNAPSHOT");
        if (prefix == nullptr)
            return;
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const juce::File file (juce::String (prefix) + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            note ("snapshot: " + file.getFullPathName().toStdString());
        else
            check (false, "could not write snapshot " + file.getFullPathName().toStdString());
    }

    void testHistoryFifo()
    {
        dsp::HistoryTap tap;
        tap.prepare (48000.0);
        check (tap.getSamplesPerFrame() == 256, "48 kHz frames are 256 samples (~5 ms)");

        dsp::HistoryFrame a { -0.4f, 0.5f, 0.5f, 0.25f, 6.0f };
        tap.addBlock (a, 128);
        dsp::HistoryFrame out {};
        check (tap.popFrames (&out, 1) == 0, "a half-frame stays in the accumulator");

        dsp::HistoryFrame b { -0.6f, 0.3f, 0.6f, 0.2f, 3.0f };
        tap.addBlock (b, 128);
        check (tap.popFrames (&out, 1) == 1, "two half-blocks emit one frame");
        check (std::abs (out.inMin + 0.6f) < 1.0e-6f && std::abs (out.inMax - 0.5f) < 1.0e-6f,
               "min/max keep the extrema across the window");
        check (std::abs (out.inPeak - 0.6f) < 1.0e-6f && std::abs (out.outPeak - 0.25f) < 1.0e-6f
                   && std::abs (out.grDb - 6.0f) < 1.0e-6f,
               "peak and GR keep the window maxima");
        check (tap.getFramesWritten() == 1, "one frame written");

        for (int i = 0; i < 8; ++i)
            tap.addBlock (a, 256);
        dsp::HistoryFrame buf[16] {};
        const auto n = tap.popFrames (buf, 16);
        check (n == 8, "pop returns the queued frames (" + std::to_string (n) + ")");
        tap.discardAll();
        check (tap.popFrames (buf, 16) == 0, "discardAll empties the FIFO");
    }

    void testBurstOverlayAndPaint()
    {
        EditorFixture f;
        if (f.editor == nullptr)
            return;

        auto& editor = *f.editor;
        auto& proc = *f.processor;
        configureGrabbingLimiter (proc);
        proc.prepareToPlay (defaultRate, defaultBlock);

        const auto quiet = (size_t) std::lround (0.25 * defaultRate);
        const auto burst = (size_t) std::lround (0.20 * defaultRate);
        const auto after = (size_t) std::lround (0.35 * defaultRate);
        Stereo in (quiet + burst + after);
        for (size_t i = 0; i < quiet; ++i)
            in.l[i] = in.r[i] = 0.01f * (float) std::sin (twoPi * 1000.0 * (double) i / defaultRate);
        for (size_t i = 0; i < burst; ++i)
        {
            const auto s = (float) std::sin (twoPi * 1000.0 * (double) (quiet + i) / defaultRate);
            in.l[quiet + i] = in.r[quiet + i] = s;
        }
        for (size_t i = 0; i < after; ++i)
        {
            const auto idx = quiet + burst + i;
            in.l[idx] = in.r[idx] = 0.01f * (float) std::sin (twoPi * 1000.0 * (double) idx / defaultRate);
        }

        render (proc, in);
        pollUntilEmpty (editor);

        auto& graph = editor.getGrHistory();
        check (graph.getFilledColumns() > 20, "history filled from the burst render ("
               + std::to_string (graph.getFilledColumns()) + " columns)");

        const auto perFrame = proc.getHistoryTap().getSamplesPerFrame();
        const auto burstStartCol = (int) (quiet / (size_t) perFrame);
        const auto burstEndCol = (int) ((quiet + burst) / (size_t) perFrame);
        const auto tailCol = (int) ((quiet + burst + (size_t) std::lround (0.08 * defaultRate)) / (size_t) perFrame);

        int hotWithGr = 0, quietBeforeGr = 0, quietAfterGr = 0, hotWithGap = 0;
        const auto n = graph.getFilledColumns();
        for (int i = 0; i < n; ++i)
        {
            const auto c = graph.getColumnFromOldest (i);
            const auto hasGr = c.grDb > 0.75f;
            if (i < burstStartCol - 2 && hasGr)
                ++quietBeforeGr;
            if (i >= burstStartCol && i <= burstEndCol)
            {
                if (hasGr) ++hotWithGr;
                if (c.inPeakDb > c.outPeakDb + 0.5f) ++hotWithGap;
            }
            if (i >= tailCol && hasGr)
                ++quietAfterGr;
        }

        check (quietBeforeGr == 0, "GR overlay is off before the burst ("
               + std::to_string (quietBeforeGr) + " quiet columns with GR)");
        check (hotWithGr >= 4 && hotWithGap >= 4,
               "GR overlay and in>out only during the burst (GR cols " + std::to_string (hotWithGr)
                   + ", in>out " + std::to_string (hotWithGap) + ")");
        check (quietAfterGr == 0, "GR overlay is off after the burst tail ("
               + std::to_string (quietAfterGr) + " late columns with GR)");

        const juce::Point<int> sizes[] { { 940, 600 }, { 1080, 660 }, { 2400, 1400 } };
        for (auto size : sizes)
        {
            editor.setSize (size.x, size.y);
            const auto label = std::to_string (size.x) + "x" + std::to_string (size.y);
            const auto plot = graph.plotBounds();
            juce::Image image (juce::Image::ARGB, juce::jmax (2, graph.getWidth()), juce::jmax (2, graph.getHeight()), true);
            juce::Graphics g (image);
            graph.paintEntireComponent (g, true);

            int grCol = -1;
            for (int i = 0; i < graph.getFilledColumns(); ++i)
                if (graph.getColumnFromOldest (i).grDb > 2.0f)
                    grCol = i;
            check (grCol >= 0, label + " has a column with GR > 2 dB");
            if (grCol < 0)
                continue;

            const auto visible = juce::jmin (graph.getFilledColumns(), (int) plot.getWidth());
            const auto x0 = (int) plot.getX() + ((int) plot.getWidth() - visible);
            const auto x = juce::jlimit ((int) plot.getX() + 1, (int) plot.getRight() - 2,
                                         x0 + visible - (graph.getFilledColumns() - grCol));
            const auto col = graph.getColumnFromOldest (grCol);
            const auto yGr = juce::jlimit ((int) plot.getY() + 2, (int) plot.getBottom() - 4,
                                           (int) plot.getY() + 4);
            const auto yOut = juce::jlimit ((int) plot.getY() + 2, (int) plot.getBottom() - 8,
                                            (int) graph.levelDbToY (col.outPeakDb) + 6);
            const auto yIn = juce::jlimit ((int) plot.getY() + 2, (int) plot.getBottom() - 8,
                                           (int) graph.levelDbToY (col.inPeakDb) + 2);

            const auto grPx = image.getPixelAt (x, yGr);
            const auto outPx = image.getPixelAt (x, yOut);
            const auto inPx = image.getPixelAt (x, yIn);
            check (grPx.getRed() > grPx.getBlue() && grPx.getFloatAlpha() > 0.05f,
                   label + " GR overlay is warm/visible at the top (rgba "
                       + std::to_string (grPx.getRed()) + "," + std::to_string (grPx.getGreen()) + ","
                       + std::to_string (grPx.getBlue()) + ")");
            check (outPx.getGreen() > outPx.getRed() && outPx.getFloatAlpha() > 0.05f,
                   label + " output overlay is teal/visible (rgba "
                       + std::to_string (outPx.getRed()) + "," + std::to_string (outPx.getGreen()) + ","
                       + std::to_string (outPx.getBlue()) + ")");
            check (std::abs (grPx.getRed() - outPx.getRed()) > 8
                       || std::abs (grPx.getGreen() - outPx.getGreen()) > 8,
                   label + " GR and output overlays are distinct colours");
            check (inPx.getFloatAlpha() > 0.02f, label + " input fill is visible");
        }

        // Sustained hot tone so snapshots show input, output and GR together.
        Stereo hot ((size_t) std::lround (1.2 * defaultRate));
        for (size_t i = 0; i < hot.size(); ++i)
        {
            const auto s = 0.95f * (float) std::sin (twoPi * 1000.0 * (double) i / defaultRate);
            hot.l[i] = hot.r[i] = s;
        }
        graph.clear();
        proc.resetMeters();
        render (proc, hot);
        pollUntilEmpty (editor);
        check (graph.getFilledColumns() > 40 && graph.getColumnFromNewest (0).grDb > 1.0f,
               "sustained tone fills the history with GR");

        editor.setSize (1080, 660);
        writeSnapshot (editor, "-1080x660");
        editor.setSize (940, 600);
        writeSnapshot (editor, "-940x600");
        editor.setSize (2400, 1400);
        writeSnapshot (editor, "-2400x1400");
    }

    void testNoAudioThreadAllocation()
    {
        auto p = makeProcessor();
        configureGrabbingLimiter (*p);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto warmup = sine ((size_t) defaultBlock * 4, 1000.0, 0.9);
        render (*p, warmup); // first buffers may warm caches

        const auto in = sine ((size_t) defaultBlock * 40, 1000.0, 0.9);
        int allocations = 0;
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        size_t i = 0;
        while (i < in.size())
        {
            const auto n = (int) std::min ((size_t) defaultBlock, in.size() - i);
            buffer.setSize (2, n, false, false, true);
            for (int s = 0; s < n; ++s)
            {
                buffer.setSample (0, s, in.l[i + (size_t) s]);
                buffer.setSample (1, s, in.r[i + (size_t) s]);
            }
            allocationCount = 0;
            countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            allocations += allocationCount;
            i += (size_t) n;
        }
        check (allocations == 0, "processBlock allocates nothing on the audio thread ("
               + std::to_string (allocations) + " news)");
    }

    Stereo drumLoop (size_t n, double rate = defaultRate)
    {
        Stereo s (n);
        const auto kickEvery = (size_t) std::lround (0.5 * rate);
        const auto hatEvery = (size_t) std::lround (0.125 * rate);
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / rate;
            float v = (float) (0.72 * std::sin (twoPi * 80.0 * t));
            const auto kickPhase = i % kickEvery;
            const auto kickEnv = std::exp (-(double) kickPhase / (0.12 * rate));
            v += (float) (0.95 * kickEnv * std::sin (twoPi * (55.0 + 80.0 * kickEnv) * t));
            if ((i % kickEvery) == kickEvery / 2)
            {
                // snare: short noise burst on the backbeat
            }
            const auto snarePhase = (i + kickEvery / 2) % kickEvery;
            if (snarePhase < (size_t) (0.04 * rate))
            {
                const auto snEnv = std::exp (-(double) snarePhase / (0.03 * rate));
                const auto noise = (float) (2.0 * ((double) ((i * 1103515245u + 12345u) % 10000) / 10000.0 - 0.5));
                v += (float) (0.7 * snEnv) * noise;
            }
            if (i % hatEvery < (size_t) (0.008 * rate))
            {
                const auto noise = (float) (2.0 * ((double) ((i * 1664525u + 1013904223u) % 10000) / 10000.0 - 0.5));
                v += 0.18f * noise;
            }
            s.l[i] = s.r[i] = v;
        }
        for (size_t i = 0; i < n; ++i)
        {
            s.l[i] = juce::jlimit (-0.95f, 0.95f, s.l[i]);
            s.r[i] = juce::jlimit (-0.95f, 0.95f, s.r[i]);
        }
        return s;
    }

    float integratedLufsOf (const Stereo& x, double rate, int latency)
    {
        dsp::LoudnessMeter loud;
        loud.prepare (rate);
        const auto start = std::min ((size_t) std::max (0, latency), x.size());
        const auto n = (int) (x.size() - start);
        if (n <= 0)
            return -70.0f;
        loud.process (x.l.data() + start, x.r.data() + start, n, 2);
        return loud.hasIntegrated() ? loud.integratedLufs() : loud.shortTermLufs();
    }

    float onsetRetain (const Stereo& in, const Stereo& out, int hop)
    {
        if ((int) in.size() < hop * 4)
            return 1.0f;
        double retain = 0;
        int count = 0;
        float prevIn = 0;
        const auto n = (int) std::min (in.size(), out.size());
        for (int i = 0; i + hop < n; i += hop)
        {
            float inPeak = 0, outPeak = 0;
            for (int k = 0; k < hop; ++k)
            {
                inPeak = std::max (inPeak, std::max (std::abs (in.l[(size_t) (i + k)]), std::abs (in.r[(size_t) (i + k)])));
                outPeak = std::max (outPeak, std::max (std::abs (out.l[(size_t) (i + k)]), std::abs (out.r[(size_t) (i + k)])));
            }
            if (inPeak > prevIn * 1.6f && inPeak > 0.05f)
            {
                retain += outPeak / std::max (1.0e-6f, inPeak);
                ++count;
            }
            prevIn = inPeak;
        }
        return count > 0 ? (float) (retain / (double) count) : 1.0f;
    }

    void testLearn()
    {
        auto p = makeProcessor();
        defaults (*p);
        set (*p, id::oversampling, 0.0f);
        p->prepareToPlay (defaultRate, defaultBlock);

        const auto drums = drumLoop ((size_t) std::lround (1.4 * defaultRate));
        const auto defaultOut = render (*p, drums);
        const auto defaultLufs = integratedLufsOf (defaultOut, defaultRate, p->getLatencySamples());

        const auto a = dsp::LearnSetup::analyseAndSearch (drums.l.data(), drums.r.data(), (int) drums.size(), 2,
                                                          defaultRate, p->readSettings());
        const auto b = dsp::LearnSetup::analyseAndSearch (drums.l.data(), drums.r.data(), (int) drums.size(), 2,
                                                          defaultRate, p->readSettings());
        check (a.valid && b.valid, "Learn search returns a valid result");
        check (std::abs (a.settings.thresholdDb - b.settings.thresholdDb) < 1.0e-5f
                   && std::abs (a.settings.attackMs - b.settings.attackMs) < 1.0e-5f
                   && a.settings.style == b.settings.style
                   && std::abs (a.settings.enhance - b.settings.enhance) < 1.0e-5f
                   && std::abs (a.targetLufs - b.targetLufs) < 1.0e-4f,
               "Learn search is deterministic");

        GestureCount gestures;
        for (auto* pid : { id::threshold, id::attack, id::release, id::knee, id::enhance, id::style })
            if (auto* q = param (*p, pid))
                q->addListener (&gestures);

        const auto applied = p->runLearnOnBuffer (drums.l.data(), drums.r.data(), (int) drums.size(), 2);
        check (applied.valid, "runLearnOnBuffer applies a result");
        check (gestures.begins == gestures.ends && gestures.begins >= 1,
               "Learn apply is one balanced host gesture (begin " + std::to_string (gestures.begins)
                   + ", end " + std::to_string (gestures.ends) + ")");
        check (p->canRevertLearn(), "Learn stores a revert snapshot");
        check (p->lastLearnTargetLufs() > -70.0f, "Learn reports a target loudness ("
               + fmt (p->lastLearnTargetLufs(), 2) + " LUFS)");

        const auto learnedOut = render (*p, drums);
        check (allFinite (learnedOut), "learned output is finite");
        const auto ceilLin = dsp::dbToLinear (get (*p, id::ceiling));
        const auto sp = std::max (peak (learnedOut.l), peak (learnedOut.r));
        check (sp <= ceilLin + 1.5e-4f, "Learn honours the ceiling (peak "
               + fmt (dsp::linearToDb (sp), 3) + " dB vs " + fmt (dsp::linearToDb (ceilLin), 3) + ")");

        const auto learnedLufs = integratedLufsOf (learnedOut, defaultRate, p->getLatencySamples());
        check (learnedLufs > defaultLufs + 0.15f, "Learn on drums is louder than default ("
               + fmt (learnedLufs, 2) + " vs " + fmt (defaultLufs, 2) + " LUFS)");

        const auto retain = onsetRetain (drums, learnedOut, std::max (32, (int) std::lround (0.01 * defaultRate)));
        check (retain > 0.55f, "Learn keeps transient peaks (onset retain " + fmt (retain, 3) + ")");

        p->revertLearn();
        check (std::abs (get (*p, id::threshold) - params::thresholdDefault) < 0.05f,
               "Revert restores the default Threshold");
        check (! p->canRevertLearn(), "Revert clears the snapshot");

        for (auto* pid : { id::threshold, id::attack, id::release, id::knee, id::enhance, id::style })
            if (auto* q = param (*p, pid))
                q->removeListener (&gestures);
    }

    void testLearnThread()
    {
        auto p = makeProcessor();
        defaults (*p);
        p->prepareToPlay (defaultRate, defaultBlock);
        const auto drums = drumLoop ((size_t) std::lround (0.4 * defaultRate));

        p->startLearn (10.0f);
        check (p->isLearning(), "Learn arms a listening take");

        int allocations = 0;
        juce::AudioBuffer<float> buffer (2, defaultBlock);
        juce::MidiBuffer midi;
        size_t i = 0;
        while (i < drums.size())
        {
            const auto n = (int) std::min ((size_t) defaultBlock, drums.size() - i);
            buffer.setSize (2, n, false, false, true);
            for (int s = 0; s < n; ++s)
            {
                buffer.setSample (0, s, drums.l[i + (size_t) s]);
                buffer.setSample (1, s, drums.r[i + (size_t) s]);
            }
            allocationCount = 0;
            countAllocations = true;
            p->processBlock (buffer, midi);
            countAllocations = false;
            allocations += allocationCount;
            i += (size_t) n;
        }
        check (allocations == 0, "Learn capture does not allocate on the audio thread ("
               + std::to_string (allocations) + " news)");
        check (p->isLearning(), "analysis stays off the audio thread after capture blocks");
        check (p->learnProgress() >= 0.0f, "Learn reports progress");

        p->cancelLearn();
        check (! p->isLearning(), "Cancel stops Learn");

        EditorFixture f;
        if (f.editor != nullptr)
        {
            check (f.editor->findControl (id::latencyMode) != nullptr, "Latency combo is in the editor");
            auto& button = f.editor->getLearnButton();
            check (button.getComponentID() == "learn" && button.getButtonText() == "Learn",
                   "Learn button is in the header");
            // triggerClick posts a command message; invoke the handler directly.
            button.onClick();
            f.editor->refreshLearnUi();
            check (f.processor->isLearning() && button.getButtonText() == "Cancel",
                   "Learn button arms and becomes Cancel");
            button.onClick();
            f.editor->refreshLearnUi();
            check (! f.processor->isLearning() && button.getButtonText() == "Learn",
                   "second click cancels Learn");
        }
    }

    struct TestCase { const char* name; std::function<void()> run; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "metadata",                                      testMetadata },
        { "ceiling never exceeded (sample and true-peak)", testCeiling },
        { "latency reporting matches impulse delay",       testLatency },
        { "Enhance=0 transparency / null vs pre-process",  testEnhanceTransparency },
        { "each style is a different algorithm",           testStyles },
        { "LUFS meter vs EBU 3341-style tones",            testLufs },
        { "stereo transient / release link",               testStereoLink },
        { "oversampling factors and latency",              testOversampling },
        { "state, presets, undo-ready round-trip",         testStatePresetsUndo },
        { "no dead parameters",                            testNoDeadParameters },
        { "editor: size, version, one control, layout",    testEditor },
        { "editor: draggable threshold line",              testThresholdLineDrag },
        { "history FIFO min/max/peak/GR",                  testHistoryFifo },
        { "burst GR overlay, paint, snapshots",            testBurstOverlayAndPaint },
        { "no audio-thread allocation",                    testNoAudioThreadAllocation },
        { "Learn drums, determinism, revert",              testLearn },
        { "Learn thread never blocks audio",               testLearnThread },
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
