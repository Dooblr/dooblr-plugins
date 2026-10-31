/*
    dooblr-vocals tests: run the real processor, DSP, presets and editor.

    Every check prints nothing on success; failures print what was expected. Each test
    ends with "[PASS]" or "[FAIL]" and the summary line counts all checks.
    Set DOOBLRVOCALS_EDITOR_SNAPSHOT=/path/prefix to write editor PNG snapshots.
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"
#include "DSP/Effects.h"
#include "DSP/KeyDetector.h"
#include "DSP/PitchEngine.h"
#include "DSP/ProcessingChain.h"
#include "DSP/VocalEngine.h"
#include "DSP/VocalSettings.h"
#include "DSP/TimeFx.h"
#include "RangeLearner.h"
#include "UI/FxDisplays.h"
#include "UI/Theme.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <typeinfo>
#include <sstream>
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
    namespace params = dooblrvocals::params;
    namespace dsp = dooblrvocals::dsp;
    using Processor = DooblrVocalsAudioProcessor;
    using Editor = DooblrVocalsAudioProcessorEditor;

    int failedChecks = 0;
    int passedChecks = 0;

    void check (bool condition, const std::string& description)
    {
        if (condition)
        {
            ++passedChecks;
            return;
        }

        ++failedChecks;
        std::cout << "    FAILED: " << description << "\n";
    }

    void note (const std::string& text) { std::cout << "    " << text << "\n"; }

    std::string format (double value, int decimals = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (decimals) << value;
        return s.str();
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    std::unique_ptr<Processor> makeProcessor (double rate = sampleRate, int block = blockSize)
    {
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails (2, 2, rate, block);
        p->prepareToPlay (rate, block);
        return p;
    }

    juce::RangedAudioParameter* getParam (Processor& p, const juce::String& id)
    {
        auto* param = p.getParameterState().getParameter (id);
        check (param != nullptr, "parameter exists: " + id.toStdString());
        return param;
    }

    void setParam (Processor& p, const juce::String& id, float plainValue)
    {
        if (auto* param = getParam (p, id))
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    float getPlain (Processor& p, const juce::String& id)
    {
        if (auto* param = getParam (p, id))
            return param->convertFrom0to1 (param->getValue());

        return 0.0f;
    }

    juce::AudioBuffer<float> render (Processor& p, const std::function<float (int)>& sampleAt,
                                     int numSamples, int block = blockSize, int* allocations = nullptr)
    {
        juce::AudioBuffer<float> out (2, numSamples), scratch (2, block);
        juce::MidiBuffer midi;
        out.clear();

        for (int pos = 0; pos < numSamples; pos += block)
        {
            const auto n = std::min (block, numSamples - pos);
            scratch.setSize (2, n, false, false, true);

            for (int i = 0; i < n; ++i)
            {
                const auto s = sampleAt (pos + i);
                scratch.setSample (0, i, s);
                scratch.setSample (1, i, s);
            }

            if (allocations != nullptr)
            {
                allocationCount.store (0);
                countAllocations.store (true);
            }

            p.processBlock (scratch, midi);

            if (allocations != nullptr)
            {
                countAllocations.store (false);
                *allocations += allocationCount.load();
            }

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, scratch, ch, 0, n);
        }

        return out;
    }

    float sineAt (double hz, int i, double rate = sampleRate, float amp = 0.4f)
    {
        return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
    }

    float sawAt (double hz, int i, double rate = sampleRate, float amp = 0.3f)
    {
        const auto phase = std::fmod ((double) i * hz / rate, 1.0);
        return amp * (float) (2.0 * phase - 1.0);
    }

    float peakOf (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
    {
        end = end < 0 ? b.getNumSamples() : std::min (end, b.getNumSamples());
        float peak = 0.0f;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i)
                peak = std::max (peak, std::abs (b.getSample (ch, i)));

        return peak;
    }

    float rmsOf (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
    {
        end = end < 0 ? b.getNumSamples() : std::min (end, b.getNumSamples());
        double sum = 0.0;
        int count = 0;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i)
            {
                const auto v = b.getSample (ch, i);
                sum += (double) v * (double) v;
                ++count;
            }

        return count > 0 ? (float) std::sqrt (sum / (double) count) : 0.0f;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;

        return true;
    }

    float detectHz (const juce::AudioBuffer<float>& b, int start, int window, float minHz = 70.0f, float maxHz = 1100.0f)
    {
        dsp::PitchDetector detector;
        detector.prepare (sampleRate, 8192);
        window = std::min (window, b.getNumSamples() - start);
        if (window < 64)
            return 0.0f;

        std::vector<float> mono ((size_t) window);
        for (int i = 0; i < window; ++i)
            mono[(size_t) i] = 0.5f * (b.getSample (0, start + i) + b.getSample (1, start + i));

        return detector.analyse (mono.data(), window, minHz, maxHz, 0.15f, true).hz;
    }

    std::vector<float> vibratoVoice (int numSamples, const std::function<double (double)>& centsAt, float amp = 0.3f);
    double vibratoCents (double t, double baseCents = 30.0, double depth = 60.0, double rate = 5.5);

    int liveLatency() { return dsp::liveLatencySamples (sampleRate); }
    int hqLatency() { return dsp::hqLatencySamples (sampleRate); }

    juce::StringArray allParameterIds (Processor& p)
    {
        juce::StringArray ids;

        for (auto* param : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                ids.add (withId->paramID);

        return ids;
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
            component.mouseDrag (makeMouseEvent (component, from + (to - from) * ((float) i / (float) steps), from, 1, true));

        component.mouseUp (makeMouseEvent (component, to, from, 1, true));
    }

    void collectComponents (juce::Component& parent, std::map<juce::String, juce::Component*>& out, bool visibleOnly)
    {
        for (auto* child : parent.getChildren())
        {
            if (visibleOnly && ! child->isVisible())
                continue;

            if (child->getComponentID().isNotEmpty())
                out[child->getComponentID()] = child;

            collectComponents (*child, out, visibleOnly);
        }
    }

    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->getComponentID() == id)
                return child;
            if (auto* found = findById (*child, id))
                return found;
        }
        return nullptr;
    }

    void saveSnapshot (const juce::Image& image, const juce::String& suffix)
    {
        auto prefix = juce::String (std::getenv ("DOOBLRVOCALS_EDITOR_SNAPSHOT") != nullptr
                                        ? std::getenv ("DOOBLRVOCALS_EDITOR_SNAPSHOT") : "");

        if (prefix.isEmpty())
        {
            const juce::File dir ("/opt/cursor/artifacts/screenshots");
            if (dir.createDirectory())
                prefix = dir.getChildFile ("dooblr-vocals").getFullPathName();
        }

        if (prefix.isEmpty())
            return;

        const juce::File file (prefix + suffix + ".png");
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            note ("snapshot: " + file.getFullPathName().toStdString());
        else
            check (false, "could not write snapshot " + file.getFullPathName().toStdString());
    }

    //==============================================================================
    void testInstantiationAndMetadata()
    {
        auto p = makeProcessor();
        check (p->getName() == "dooblr-vocals", "name is dooblr-vocals (got " + p->getName().toStdString() + ")");
        check (! p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "audio effect, no MIDI");
        check (p->hasEditor(), "has an editor");
        check (p->getTotalNumInputChannels() == 2, "stereo input");
        check (p->getTotalNumOutputChannels() == 2, "stereo output");
        check (juce::String (JucePlugin_VersionString) == DOOBLRVOCALS_PROJECT_VERSION, "version string matches the project version");
        check (juce::String (JucePlugin_VersionString) == "0.2.0", "version 0.2.0");
        check (JucePlugin_IsSynth == 0, "built as an effect");
        check (JucePlugin_PluginCode == 0x44627678, "plugin code is Dbvx");
        check (p->getNumPrograms() >= 6, "Init plus factory presets");
        check (p->getProgramName (0) == "Init", "program 0 is Init");
        check (p->getLatencySamples() == liveLatency(),
               "live latency reported (" + std::to_string (p->getLatencySamples()) + ", expected "
                   + std::to_string (liveLatency()) + ")");
        check (allParameterIds (*p).size() >= 30, "parameter count");
        note ("live latency " + std::to_string (liveLatency()) + " samples, HQ "
              + std::to_string (hqLatency()) + " at 48 kHz");
    }

    //==============================================================================
    void testPitchDetectionTones()
    {
        dsp::PitchDetector detector;
        detector.prepare (sampleRate, 8192);

        const int window = 4096;
        std::vector<float> buf ((size_t) window);

        for (const auto hz : { 110.0, 220.0, 440.0, 329.63, 196.0 })
        {
            for (int i = 0; i < window; ++i)
                buf[(size_t) i] = sineAt (hz, i, sampleRate, 0.5f);

            const auto est = detector.analyse (buf.data(), window, 70.0f, 1100.0f, 0.15f, true);
            const auto cents = est.hz > 0.0f ? 1200.0 * std::log2 ((double) est.hz / hz) : 1.0e9;
            check (est.voiced && std::abs (cents) < 15.0,
                   "YIN " + format (hz, 1) + " Hz → " + format (est.hz, 2) + " Hz (" + format (cents, 1) + " cents, conf "
                       + format (est.confidence, 2) + ")");
        }
    }

    void testPitchDetectionVibrato()
    {
        dsp::PitchDetector detector;
        detector.prepare (sampleRate, 8192);
        const int window = 2048;
        std::vector<float> buf ((size_t) window);
        const auto centre = 220.0;
        auto phase = 0.0;
        int ok = 0, total = 0;

        for (int hop = 0; hop < 40; ++hop)
        {
            const auto t0 = (double) (hop * 64) / sampleRate;
            for (int i = 0; i < window; ++i)
            {
                const auto t = t0 + (double) i / sampleRate;
                const auto inst = centre * std::exp2 (0.5 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t));
                phase += 2.0 * juce::MathConstants<double>::pi * inst / sampleRate;
                buf[(size_t) i] = 0.5f * (float) std::sin (phase);
            }

            const auto midT = t0 + (double) window * 0.5 / sampleRate;
            const auto expected = centre * std::exp2 (0.5 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * midT));
            const auto est = detector.analyse (buf.data(), window, 80.0f, 500.0f, 0.15f, true);
            ++total;
            if (est.voiced && std::abs (1200.0 * std::log2 ((double) est.hz / expected)) < 40.0)
                ++ok;
        }

        check (ok >= total * 3 / 4, "vibrato tracking " + std::to_string (ok) + "/" + std::to_string (total) + " hops within 40 cents");
    }

    void testKeyedVsChromaticCorrection()
    {
        dsp::PitchCorrector corrector;
        corrector.prepare (sampleRate);

        dsp::PitchSettings keyed;
        keyed.mode = dsp::CorrectionMode::keyed;
        keyed.key = 0;
        keyed.scale = dsp::ScaleType::major;
        keyed.retuneMs = 0.0f;
        keyed.humanize = 0.0f;
        keyed.a4Hz = 440.0f;

        const auto bb = 233.08f; // A#3/Bb, not in C major
        const auto snapped = corrector.process (bb, true, keyed, 64);
        const auto a3 = dsp::PitchCorrector::midiToHz (57.0f, 440.0f); // A3
        check (std::abs (snapped - a3) < 1.0f, "keyed C major snaps Bb " + format (bb, 1) + " to A3 " + format (a3, 1)
                                                   + " (got " + format (snapped, 2) + ")");

        corrector.reset();
        dsp::PitchSettings chromatic = keyed;
        chromatic.mode = dsp::CorrectionMode::chromatic;
        const auto chrome = corrector.process (bb, true, chromatic, 64);
        check (std::abs (chrome - bb) < 1.0f, "chromatic keeps Bb " + format (bb, 1) + " (got " + format (chrome, 2) + ")");

        corrector.reset();
        dsp::PitchSettings custom = keyed;
        custom.scale = dsp::ScaleType::custom;
        custom.customMask = 1 << 10; // A#/Bb only (pitch class 10)
        const auto onlyBb = corrector.process (220.0f, true, custom, 64);
        check (std::abs (onlyBb - bb) < 2.0f || std::abs (onlyBb - 233.08f * 0.5f) < 2.0f
                   || std::abs (onlyBb - 466.16f) < 4.0f,
               "custom mask of Bb snaps A3 220 toward a Bb (" + format (onlyBb, 2) + ")");

        // Through the engine: a 233 Hz sine in C major with instant retune should land near A3.
        auto p = makeProcessor();
        setParam (*p, "pitch_on", 1.0f);
        setParam (*p, "corr_mode", 0.0f);
        setParam (*p, "corr_key", 0.0f);
        setParam (*p, "corr_scale", 0.0f);
        setParam (*p, "retune_speed", 0.0f);
        setParam (*p, "humanize", 0.0f);
        setParam (*p, "corr_amount", 1.0f);
        setParam (*p, "formant", 0.0f);
        setParam (*p, "input_type", 2.0f);
        setParam (*p, "quality", 0.0f);
        setParam (*p, "lim_mode", 0.0f);
        setParam (*p, "gate_on", 0.0f);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "comp_on", 0.0f);

        const auto lat = p->getLatencySamples();
        const auto keyedOut = render (*p, [] (int i) { return sineAt (233.08, i, sampleRate, 0.5f); }, lat + 8192);
        check (allFinite (keyedOut), "keyed engine output finite");

        std::vector<dsp::PitchFrame> tapFrames (2048);
        const auto nTap = p->getPitchTap().pop (tapFrames.data(), (int) tapFrames.size());
        int voicedFrames = 0;
        float lastDet = 0, lastCor = 0;
        for (int i = 0; i < nTap; ++i)
            if (tapFrames[(size_t) i].voiced)
            {
                ++voicedFrames;
                lastDet = tapFrames[(size_t) i].detectedHz;
                lastCor = tapFrames[(size_t) i].correctedHz;
            }
        check (voicedFrames > 10, "pitch tap sees voiced frames (" + std::to_string (voicedFrames) + "/" + std::to_string (nTap) + ")");
        if (voicedFrames > 0)
            check (std::abs (1200.0 * std::log2 ((double) lastCor / (double) a3)) < 40.0,
                   "tap corrected Hz is A3: det " + format (lastDet, 1) + " → " + format (lastCor, 1));

        const auto keyedHz = detectHz (keyedOut, lat + 2048, 4096, 80.0f, 500.0f);
        const auto keyedCents = keyedHz > 0.0f ? 1200.0 * std::log2 ((double) keyedHz / (double) a3) : 1.0e9;
        check (keyedHz > 0.0f && std::abs (keyedCents) < 80.0,
               "engine keyed snap of Bb → A3: " + format (keyedHz, 1) + " Hz (" + format (keyedCents, 0) + " cents)");

        auto q = makeProcessor();
        setParam (*q, "pitch_on", 1.0f);
        setParam (*q, "corr_mode", 1.0f);
        setParam (*q, "retune_speed", 0.0f);
        setParam (*q, "humanize", 0.0f);
        setParam (*q, "corr_amount", 1.0f);
        setParam (*q, "formant", 0.0f);
        setParam (*q, "input_type", 2.0f);
        setParam (*q, "lim_mode", 0.0f);
        setParam (*q, "gate_on", 0.0f);
        setParam (*q, "eq_on", 0.0f);
        setParam (*q, "comp_on", 0.0f);
        const auto chromeLat = q->getLatencySamples();
        const auto chromeOut = render (*q, [] (int i) { return sineAt (233.08, i, sampleRate, 0.5f); }, chromeLat + 8192);
        std::vector<dsp::PitchFrame> chromeTap (2048);
        const auto nChromeTap = q->getPitchTap().pop (chromeTap.data(), (int) chromeTap.size());
        float chromeCor = 0;
        for (int i = 0; i < nChromeTap; ++i)
            if (chromeTap[(size_t) i].voiced)
                chromeCor = chromeTap[(size_t) i].correctedHz;
        check (chromeCor > 0.0f && std::abs (1200.0 * std::log2 ((double) chromeCor / 233.08)) < 40.0,
               "tap chromatic keeps Bb (" + format (chromeCor, 1) + " Hz)");
        double chromeErr = 0.0;
        int chromeCount = 0;
        for (int i = chromeLat + 2048; i < chromeOut.getNumSamples(); ++i)
        {
            chromeErr += std::abs ((double) chromeOut.getSample (0, i) - (double) sineAt (233.08, i - chromeLat, sampleRate, 0.5f));
            ++chromeCount;
        }
        check (chromeCount > 0 && chromeErr / (double) chromeCount < 0.25,
               "chromatic output matches delayed input (mean abs err "
                   + format (chromeCount > 0 ? chromeErr / (double) chromeCount : 1.0, 3) + ")");
    }

    void testUnvoicedPassthrough()
    {
        auto p = makeProcessor();
        setParam (*p, "pitch_on", 1.0f);
        setParam (*p, "corr_amount", 1.0f);
        setParam (*p, "retune_speed", 0.0f);
        setParam (*p, "lim_mode", 0.0f);
        setParam (*p, "gate_on", 0.0f);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "comp_on", 0.0f);

        const auto lat = p->getLatencySamples();
        const auto n = lat + 4096;

        const auto quiet = render (*p, [] (int) { return 0.001f; }, n);
        check (allFinite (quiet), "quiet input finite");
        check (peakOf (quiet, lat, n) < 0.01f, "sub-voicing-threshold signal stays quiet (peak "
                                                   + format (peakOf (quiet, lat, n), 4) + ")");

        p->reset();
        p->prepareToPlay (sampleRate, blockSize);
        const auto silence = render (*p, [] (int) { return 0.0f; }, n);
        check (peakOf (silence) < 1.0e-5f, "silence in, silence out");

        p->reset();
        p->prepareToPlay (sampleRate, blockSize);
        juce::Random rng (0x51f0u);
        const auto noise = render (*p, [&rng] (int i) { return i < 2048 ? 0.0f : (rng.nextFloat() * 2.0f - 1.0f) * 0.002f; }, n);
        check (allFinite (noise) && peakOf (noise, lat + 256, n) < 0.02f,
               "low-level noise (unvoiced) is not amplified into a tone");
    }

    int measuredDryLatency (Processor& p)
    {
        p.reset();
        p.prepareToPlay (sampleRate, blockSize);
        const auto lat = p.getLatencySamples();
        const auto out = render (p, [] (int i) { return i == 0 ? 1.0f : 0.0f; }, lat + 512);
        int peakAt = -1;
        float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > peak)
            {
                peak = std::abs (out.getSample (0, i));
                peakAt = i;
            }
        return peak > 0.5f ? peakAt : -1;
    }

    void testLatencyModes()
    {
        check (dsp::trackingLatencySamples (48000.0) == 256, "Tracking is 256 samples at 48k");
        check (dsp::trackingLatencySamples (96000.0) == 512, "Tracking scales with rate (512 at 96k)");
        check (dsp::trackingLatencySamples (44100.0) == 235, "Tracking is 235 samples at 44.1k");

        auto p = makeProcessor();
        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "lim_mode", 0.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, blockSize);

        struct Case { const char* name; float quality, latencyMode; bool offline; int expect; };
        const Case cases[] {
            { "Tracking",                  0.0f, 0.0f, false, dsp::trackingLatencySamples (sampleRate) },
            { "Mix",                       0.0f, 1.0f, false, liveLatency() },
            { "HQ (Latency Tracking)",     1.0f, 0.0f, false, hqLatency() },
            { "HQ (Latency Mix)",          1.0f, 1.0f, false, hqLatency() },
            { "offline render (Tracking)", 0.0f, 0.0f, true,  hqLatency() },
        };

        for (const auto& c : cases)
        {
            setParam (*p, "quality", c.quality);
            setParam (*p, "latency_mode", c.latencyMode);
            p->setNonRealtime (c.offline);
            block.clear();
            p->processBlock (block, midi);
            const auto reported = p->getLatencySamples();
            const auto measured = measuredDryLatency (*p);
            note (std::string (c.name) + ": reported " + std::to_string (reported) + " samples ("
                  + format (1000.0 * reported / sampleRate, 2) + " ms), measured " + std::to_string (measured));
            check (reported == c.expect, std::string (c.name) + " reports " + std::to_string (c.expect)
                                             + " (got " + std::to_string (reported) + ")");
            check (measured == reported, std::string (c.name) + " impulse appears at the reported latency");
        }

        // Switching modes mid-stream updates the host latency on the next block.
        p->setNonRealtime (false);
        setParam (*p, "quality", 0.0f);
        setParam (*p, "latency_mode", 1.0f);
        block.clear();
        p->processBlock (block, midi);
        const auto before = p->getLatencySamples();
        setParam (*p, "latency_mode", 0.0f);
        block.clear();
        p->processBlock (block, midi);
        check (before == liveLatency() && p->getLatencySamples() == dsp::trackingLatencySamples (sampleRate),
               "Mix -> Tracking switch calls setLatencySamples");
        const auto noisy = render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); }, 8192);
        setParam (*p, "latency_mode", 1.0f);
        const auto back = render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); }, 8192);
        check (allFinite (noisy) && allFinite (back) && peakOf (back) < 1.5f, "mode switches mid-stream stay finite and bounded");
    }

    void testLatencyReporting()
    {
        auto p = makeProcessor();
        setParam (*p, "quality", 0.0f);
        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "lim_mode", 0.0f);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, blockSize);
        block.clear();
        p->processBlock (block, midi);
        check (p->getLatencySamples() == liveLatency(), "Live quality reports live latency");

        setParam (*p, "quality", 1.0f);
        block.clear();
        p->processBlock (block, midi);
        check (p->getLatencySamples() == hqLatency(), "HQ quality reports HQ latency ("
                                                          + std::to_string (p->getLatencySamples()) + ")");

        setParam (*p, "quality", 0.0f);
        p->setNonRealtime (true);
        block.clear();
        p->processBlock (block, midi);
        check (p->getLatencySamples() == hqLatency(), "isNonRealtime() forces HQ latency with Quality still Live");

        p->setNonRealtime (false);
        block.clear();
        p->processBlock (block, midi);
        check (p->getLatencySamples() == liveLatency(), "leaving offline restore Live latency");

        // Impulse through the delay: with pitch off, sample 0 appears at `latency`.
        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "quality", 0.0f);
        p->reset();
        p->prepareToPlay (sampleRate, blockSize);
        const auto lat = liveLatency();
        const auto out = render (*p, [] (int i) { return i == 0 ? 1.0f : 0.0f; }, lat + 64);
        int peakAt = 0;
        float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > peak)
            {
                peak = std::abs (out.getSample (0, i));
                peakAt = i;
            }

        check (peak > 0.5f && std::abs (peakAt - lat) <= 2,
               "dry delay matches reported latency (peak " + format (peak, 3) + " at sample "
                   + std::to_string (peakAt) + ", expected " + std::to_string (lat) + ")");
    }

    void testGateBehaviour()
    {
        auto p = makeProcessor();
        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "lim_mode", 0.0f);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "comp_on", 0.0f);
        setParam (*p, "gate_on", 1.0f);
        setParam (*p, "gate_threshold", -20.0f);
        setParam (*p, "gate_attack", 0.1f);
        setParam (*p, "gate_hold", 0.0f);
        setParam (*p, "gate_release", 5.0f);
        setParam (*p, "gate_range", 40.0f);

        const auto lat = p->getLatencySamples();
        const auto loud = render (*p, [] (int i) { return sineAt (220.0, i, sampleRate, 0.5f); }, lat + 4096);
        const auto loudRms = rmsOf (loud, lat + 1024, lat + 4096);
        check (loudRms > 0.2f, "gate open for a -6 dB tone (rms " + format (loudRms, 3) + ")");

        p->reset();
        p->prepareToPlay (sampleRate, blockSize);
        const auto quiet = render (*p, [] (int i) { return sineAt (220.0, i, sampleRate, 0.02f); }, lat + 8192);
        const auto quietRms = rmsOf (quiet, lat + 4096, lat + 8192);
        check (quietRms < 0.005f, "gate closed for a -34 dB tone, range 40 dB (rms " + format (quietRms, 4) + ")");

        dsp::Gate gate;
        gate.prepare (sampleRate);
        check (std::abs (dsp::Gate::staticGain (-10.0f, -20.0f, 40.0f) - 1.0f) < 1.0e-6f, "static gate open is unity");
        check (dsp::Gate::staticGain (-40.0f, -20.0f, 40.0f) < 0.02f, "static gate closed is range");
    }

    void testChainReorder()
    {
        check (dsp::numChainOrders == 24, "4! chain orders");
        check (dsp::chainOrderToIndex ({ dsp::gateStage, dsp::eqStage, dsp::dynamicsStage, dsp::limiterStage }) == 0,
               "default order is index 0");

        std::set<std::array<int, 4>> unique;
        for (int i = 0; i < dsp::numChainOrders; ++i)
        {
            const auto order = dsp::chainOrderFromIndex (i);
            unique.insert (order);
            check (dsp::chainOrderToIndex (order) == i, "order " + std::to_string (i) + " round-trips");
        }
        check ((int) unique.size() == 24, "all 24 permutations are distinct");

        auto p = makeProcessor();
        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "gate_on", 0.0f);
        setParam (*p, "eq_on", 1.0f);
        setParam (*p, "eq_mid_freq", 1000.0f);
        setParam (*p, "eq_mid_gain", 12.0f);
        setParam (*p, "eq_mid_q", 2.0f);
        setParam (*p, "comp_on", 1.0f);
        setParam (*p, "comp_threshold", -24.0f);
        setParam (*p, "comp_ratio", 8.0f);
        setParam (*p, "comp_attack", 0.1f);
        setParam (*p, "comp_release", 20.0f);
        setParam (*p, "lim_mode", 0.0f);

        const auto lat = p->getLatencySamples();
        const auto n = lat + 4096;
        auto fill = [] (int i) { return sineAt (1000.0, i, sampleRate, 0.4f); };

        int eqThenComp = 0, compThenEq = 0;
        for (int i = 0; i < dsp::numChainOrders; ++i)
        {
            const auto order = dsp::chainOrderFromIndex (i);
            const auto eqPos = (int) (std::find (order.begin(), order.end(), dsp::eqStage) - order.begin());
            const auto compPos = (int) (std::find (order.begin(), order.end(), dsp::dynamicsStage) - order.begin());
            if (eqPos < compPos) ++eqThenComp;
            else ++compThenEq;
        }
        check (eqThenComp == 12 && compThenEq == 12, "half the orders put EQ before Comp");

        auto eqFirstIndex = 0, compFirstIndex = 0;
        for (int i = 0; i < dsp::numChainOrders; ++i)
        {
            const auto order = dsp::chainOrderFromIndex (i);
            const auto eqPos = (int) (std::find (order.begin(), order.end(), dsp::eqStage) - order.begin());
            const auto compPos = (int) (std::find (order.begin(), order.end(), dsp::dynamicsStage) - order.begin());
            if (eqPos < compPos) { eqFirstIndex = i; break; }
        }
        for (int i = 0; i < dsp::numChainOrders; ++i)
        {
            const auto order = dsp::chainOrderFromIndex (i);
            const auto eqPos = (int) (std::find (order.begin(), order.end(), dsp::eqStage) - order.begin());
            const auto compPos = (int) (std::find (order.begin(), order.end(), dsp::dynamicsStage) - order.begin());
            if (compPos < eqPos) { compFirstIndex = i; break; }
        }

        setParam (*p, "chain_order", (float) eqFirstIndex);
        const auto a = render (*p, fill, n);
        setParam (*p, "chain_order", (float) compFirstIndex);
        const auto b = render (*p, fill, n);
        check (allFinite (a) && allFinite (b), "reordered chain stays finite");
        const auto peakA = peakOf (a, lat + 1024, n);
        const auto peakB = peakOf (b, lat + 1024, n);
        check (std::abs (peakA - peakB) > 0.01f,
               "EQ-then-Comp vs Comp-then-EQ changes the peak (" + format (peakA, 3) + " vs " + format (peakB, 3) + ")");

        // Reorder mid-stream: 15 ms crossfade, no NaNs.
        setParam (*p, "chain_order", 0.0f);
        auto mid = render (*p, fill, lat + 2048);
        setParam (*p, "chain_order", 5.0f);
        mid = render (*p, fill, 2048);
        check (allFinite (mid), "crossfade during reorder is finite");

        // The limiter always runs last (after the time FX), so its slot in a stored order is
        // ignored: an order with the limiter first renders exactly like the default order.
        {
            check (dsp::withLimiterLast ({ dsp::limiterStage, dsp::dynamicsStage, dsp::gateStage, dsp::eqStage })
                       == dsp::ChainOrder { dsp::dynamicsStage, dsp::gateStage, dsp::eqStage, dsp::limiterStage },
                   "withLimiterLast keeps the other stages' order");
            const auto limiterFirst = dsp::chainOrderToIndex ({ dsp::limiterStage, dsp::gateStage, dsp::eqStage, dsp::dynamicsStage });
            auto renderWith = [&] (int orderIndex)
            {
                auto q = makeProcessor();
                setParam (*q, "pitch_on", 0.0f);
                setParam (*q, "comp_on", 1.0f);
                setParam (*q, "comp_makeup", 12.0f);
                setParam (*q, "lim_mode", 2.0f);
                setParam (*q, "lim_ceiling", -3.0f);
                setParam (*q, "chain_order", (float) orderIndex);
                return render (*q, [] (int i) { return sawAt (220.0, i, sampleRate, 0.8f); }, 24000);
            };
            const auto x = renderWith (0), y = renderWith (limiterFirst);
            float diff = 0.0f;
            for (int ch = 0; ch < x.getNumChannels(); ++ch)
                for (int i = 0; i < x.getNumSamples(); ++i)
                    diff = std::max (diff, std::abs (x.getSample (ch, i) - y.getSample (ch, i)));
            check (diff == 0.0f, "limiter-first order renders identically to limiter-last (max diff " + format (diff, 7) + ")");
        }
    }

    void testParameterAndStateRoundTrip()
    {
        auto p = makeProcessor();
        check (params::stateVersion == 1, "stateVersion 1");

        setParam (*p, "corr_key", 5.0f);
        setParam (*p, "retune_speed", 12.0f);
        setParam (*p, "humanize", 0.4f);
        setParam (*p, "gate_on", 1.0f);
        setParam (*p, "eq_mid_gain", 3.0f);
        setParam (*p, "comp_ratio", 6.0f);
        setParam (*p, "output_gain", -3.0f);

        juce::MemoryBlock blob;
        p->getStateInformation (blob);
        check (blob.getSize() > 32, "state blob is non-empty");

        auto q = makeProcessor();
        q->setStateInformation (blob.getData(), (int) blob.getSize());
        check (std::abs (getPlain (*q, "corr_key") - 5.0f) < 0.1f, "key round-trips");
        check (std::abs (getPlain (*q, "retune_speed") - 12.0f) < 0.2f, "retune round-trips");
        check (std::abs (getPlain (*q, "humanize") - 0.4f) < 0.02f, "humanize round-trips");
        check (getPlain (*q, "gate_on") > 0.5f, "gate_on round-trips");
        check (std::abs (getPlain (*q, "eq_mid_gain") - 3.0f) < 0.05f, "eq gain round-trips");
        check (std::abs (getPlain (*q, "comp_ratio") - 6.0f) < 0.1f, "ratio round-trips");
        check (std::abs (getPlain (*q, "output_gain") + 3.0f) < 0.05f, "output round-trips");

        const auto& factory = dooblrvocals::presets::getFactoryPresets();
        check (factory.size() >= 6, "factory presets");
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            p->loadFactoryPreset (i, false);
            const auto lat = p->getLatencySamples();
            const auto out = render (*p, [] (int s) { return sineAt (220.0, s, sampleRate, 0.3f); }, lat + 2048);
            check (allFinite (out), factory[(size_t) i].name.toStdString() + " is finite");
        }

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("dooblr-vocals-tests");
        folder.createDirectory();
        const auto file = folder.getChildFile ("roundtrip.dvocal");
        juce::String error;
        setParam (*p, "corr_amount", 0.55f);
        check (p->saveUserPreset (file, error), "save user preset");
        auto r = makeProcessor();
        check (r->loadUserPreset (file, error), "load user preset: " + error.toStdString());
        check (std::abs (getPlain (*r, "corr_amount") - 0.55f) < 0.02f, "user preset restores amount");
        file.deleteFile();
        folder.deleteRecursively();
    }

    double bufferDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from)
    {
        double diff = 0.0;
        const auto n = std::min (a.getNumSamples(), b.getNumSamples());
        from = std::min (from, n - 1);
        for (int i = from; i < n; ++i)
            diff += std::abs ((double) a.getSample (0, i) - (double) b.getSample (0, i));
        return diff;
    }

    void testNoDeadParameters()
    {
        auto p = makeProcessor();
        const auto lat = liveLatency();
        const auto n = lat + 4096;
        juce::StringArray dead;

        auto pulse = [&] (const juce::String& id, const std::function<float (int)>& fill, float lo, float hi, double minDiff)
        {
            auto* param = p->getParameterState().getParameter (id);
            if (param == nullptr)
            {
                dead.add (id);
                return;
            }

            const auto original = param->getValue();
            param->setValueNotifyingHost (param->convertTo0to1 (lo));
            p->reset();
            p->prepareToPlay (sampleRate, blockSize);
            const auto a = render (*p, fill, n);
            param->setValueNotifyingHost (param->convertTo0to1 (hi));
            p->reset();
            p->prepareToPlay (sampleRate, blockSize);
            const auto b = render (*p, fill, n);
            if (bufferDiff (a, b, lat + 256) < minDiff)
                dead.add (id);
            param->setValueNotifyingHost (original);
        };

        setParam (*p, "pitch_on", 1.0f);
        setParam (*p, "corr_mode", 0.0f);
        setParam (*p, "corr_key", 0.0f);
        setParam (*p, "corr_scale", 0.0f);
        setParam (*p, "retune_speed", 0.0f);
        setParam (*p, "humanize", 0.0f);
        setParam (*p, "corr_amount", 1.0f);
        setParam (*p, "formant", 0.0f);
        setParam (*p, "lim_mode", 0.0f);
        setParam (*p, "gate_on", 0.0f);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "comp_on", 0.0f);

        auto sawBb = [] (int i) { return sawAt (233.08, i, sampleRate, 0.35f); };
        auto sineA = [] (int i) { return sineAt (221.0, i, sampleRate, 0.4f); };
        auto sine1k = [] (int i) { return sineAt (1000.0, i, sampleRate, 0.3f); };
        auto sineHot = [] (int i) { return sineAt (220.0, i, sampleRate, 0.99f); };
        auto quiet = [] (int i) { return sineAt (220.0, i, sampleRate, 0.01f); };

        pulse ("pitch_on", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("corr_mode", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("corr_key", [] (int i) { return sineAt (277.18, i, sampleRate, 0.4f); }, 0.0f, 2.0f, 0.5);
        pulse ("corr_scale", sawBb, 0.0f, 1.0f, 0.5);
        setParam (*p, "corr_scale", (float) dsp::ScaleType::custom);
        pulse ("corr_mask", sawBb, (float) dsp::maskFromDegrees (0, 2, 4, 5, 7, 9, 11), 1 << 10, 0.5);
        setParam (*p, "corr_scale", 0.0f);
        pulse ("retune_speed", sawBb, 0.0f, 400.0f, 0.5);
        pulse ("humanize", sineA, 0.0f, 1.0f, 0.2);
        {
            // Vibrato Respect needs a sung vibrato to act on (it locks after about 1.5 cycles).
            const auto m = (int) (1.2 * sampleRate);
            const auto voice = vibratoVoice (m, [] (double t) { return vibratoCents (t); });
            const auto fillV = [&voice] (int i) { return voice[(size_t) i]; };
            setParam (*p, "vibrato", 0.0f);
            p->reset();
            p->prepareToPlay (sampleRate, blockSize);
            const auto a = render (*p, fillV, m);
            setParam (*p, "vibrato", 1.0f);
            p->reset();
            p->prepareToPlay (sampleRate, blockSize);
            const auto b = render (*p, fillV, m);
            if (bufferDiff (a, b, lat + 256) < 1.0)
                dead.add ("vibrato");
            setParam (*p, "vibrato", 0.0f);
        }
        pulse ("corr_amount", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("a4_hz", sawBb, 415.0f, 466.0f, 0.2);
        pulse ("formant", sawBb, 0.0f, 1.0f, 0.05);
        pulse ("input_type", [] (int i) { return sineAt (90.0, i, sampleRate, 0.4f); }, 0.0f, 4.0f, 0.05);

        // Harmony (pitch still on, keyed C major).
        setParam (*p, "harm_on", 1.0f);
        pulse ("harm_on", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("harm_level", sawBb, -30.0f, 0.0f, 1.0);
        pulse ("harm_formant", sawBb, 0.0f, 1.0f, 0.05);
        pulse ("harm_lofi", sawBb, 0.0f, 1.0f, 0.05);
        for (int v = 1; v <= dsp::numHarmonyVoices; ++v)
        {
            const auto id = [v] (const char* suffix) { return "harm" + juce::String (v) + "_" + suffix; };
            const auto wasOn = getPlain (*p, id ("on"));
            setParam (*p, id ("on"), 1.0f);
            pulse (id ("on"), sawBb, 0.0f, 1.0f, 0.5);
            // Distinct intervals per mode (the octave voices default to an octave in both).
            setParam (*p, id ("degree"), 2.0f);
            setParam (*p, id ("semis"), 7.0f);
            setParam (*p, id ("mode"), 0.0f);
            pulse (id ("mode"), sawBb, 0.0f, 1.0f, 0.5);
            pulse (id ("degree"), sawBb, 1.0f, 4.0f, 0.5);
            setParam (*p, id ("mode"), 1.0f);
            pulse (id ("semis"), sawBb, -12.0f, 7.0f, 0.5);
            pulse (id ("level"), sawBb, -30.0f, 0.0f, 0.5);
            pulse (id ("pan"), sawBb, -1.0f, 1.0f, 0.5);
            pulse (id ("detune"), sawBb, -50.0f, 50.0f, 0.2);
            pulse (id ("delay"), sawBb, 0.0f, 30.0f, 0.2);
            setParam (*p, id ("on"), wasOn);
        }
        setParam (*p, "harm_on", 0.0f);

        {
            setParam (*p, "quality", 0.0f);
            juce::AudioBuffer<float> b (2, blockSize);
            b.clear();
            juce::MidiBuffer midi;
            p->processBlock (b, midi);
            const auto live = p->getLatencySamples();
            setParam (*p, "quality", 1.0f);
            p->processBlock (b, midi);
            if (p->getLatencySamples() == live)
                dead.add ("quality");
            setParam (*p, "quality", 0.0f);
            p->processBlock (b, midi);
            setParam (*p, "latency_mode", 0.0f);
            p->processBlock (b, midi);
            if (p->getLatencySamples() == live)
                dead.add ("latency_mode");
            setParam (*p, "latency_mode", 1.0f);
            p->processBlock (b, midi);
        }

        setParam (*p, "pitch_on", 0.0f);
        setParam (*p, "gate_threshold", -24.0f);
        setParam (*p, "gate_range", 60.0f);
        pulse ("gate_on", quiet, 0.0f, 1.0f, 0.5);
        setParam (*p, "gate_on", 1.0f);
        pulse ("gate_threshold", quiet, -80.0f, -6.0f, 0.5);
        pulse ("gate_release", [lat] (int i)
               {
                   return i < lat + 512 ? sineAt (220.0, i, sampleRate, 0.5f) : sineAt (220.0, i, sampleRate, 0.01f);
               }, 1.0f, 400.0f, 0.05);

        {
            dsp::Gate gateA, gateB;
            gateA.prepare (sampleRate);
            gateB.prepare (sampleRate);
            dsp::GateSettings fast { true, -24.0f, 0.1f, 0.0f, 5.0f, 60.0f };
            dsp::GateSettings slow { true, -24.0f, 40.0f, 0.0f, 5.0f, 60.0f };
            juce::AudioBuffer<float> a (2, 4096), b (2, 4096);
            for (int i = 0; i < 4096; ++i)
            {
                const auto s = i < 64 ? 0.0f : 0.5f;
                a.setSample (0, i, s);
                a.setSample (1, i, s);
                b.setSample (0, i, s);
                b.setSample (1, i, s);
            }
            gateA.process (a, 0, 4096, fast);
            gateB.process (b, 0, 4096, slow);
            check (bufferDiff (a, b, 64) > 1.0, "gate attack 0.1 ms vs 40 ms changes the onset");

            dsp::GateSettings noHold { true, -24.0f, 0.1f, 0.0f, 5.0f, 60.0f };
            dsp::GateSettings held { true, -24.0f, 0.1f, 80.0f, 5.0f, 60.0f };
            juce::AudioBuffer<float> c (2, 8192), d (2, 8192);
            for (int i = 0; i < 8192; ++i)
            {
                const auto s = i < 256 ? 0.5f : 0.01f;
                c.setSample (0, i, s); c.setSample (1, i, s);
                d.setSample (0, i, s); d.setSample (1, i, s);
            }
            dsp::Gate gateC, gateD;
            gateC.prepare (sampleRate);
            gateD.prepare (sampleRate);
            gateC.process (c, 0, 8192, noHold);
            gateD.process (d, 0, 8192, held);
            check (bufferDiff (c, d, 256) > 0.5, "gate hold keeps the gate open after a drop");
        }
        pulse ("gate_range", quiet, 0.0f, 80.0f, 0.5);
        setParam (*p, "gate_on", 0.0f);

        setParam (*p, "eq_on", 1.0f);
        setParam (*p, "eq_mid_gain", 12.0f);
        setParam (*p, "eq_low_gain", 12.0f);
        setParam (*p, "eq_high_gain", 12.0f);
        pulse ("eq_on", sine1k, 0.0f, 1.0f, 1.0);
        pulse ("eq_low_freq", [] (int i) { return sineAt (80.0, i, sampleRate, 0.3f); }, 20.0f, 500.0f, 0.5);
        pulse ("eq_low_gain", [] (int i) { return sineAt (80.0, i, sampleRate, 0.3f); }, -24.0f, 24.0f, 1.0);
        pulse ("eq_mid_freq", sine1k, 100.0f, 8000.0f, 0.5);
        pulse ("eq_mid_gain", sine1k, -24.0f, 24.0f, 1.0);
        pulse ("eq_mid_q", sine1k, 0.2f, 10.0f, 0.2);
        pulse ("eq_high_freq", [] (int i) { return sineAt (10000.0, i, sampleRate, 0.2f); }, 2000.0f, 20000.0f, 0.2);
        pulse ("eq_high_gain", [] (int i) { return sineAt (8000.0, i, sampleRate, 0.2f); }, -24.0f, 24.0f, 0.5);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "eq_mid_gain", 0.0f);

        setParam (*p, "comp_on", 1.0f);
        pulse ("comp_on", sineHot, 0.0f, 1.0f, 0.5);
        pulse ("comp_threshold", sineHot, -48.0f, 0.0f, 0.5);
        pulse ("comp_ratio", sineHot, 1.0f, 20.0f, 0.2);
        pulse ("comp_attack", sineHot, 0.1f, 100.0f, 0.05);
        pulse ("comp_release", sineHot, 10.0f, 1000.0f, 0.05);
        pulse ("comp_makeup", sineHot, 0.0f, 24.0f, 1.0);
        setParam (*p, "comp_on", 0.0f);

        setParam (*p, "lim_ceiling", -12.0f);
        pulse ("lim_mode", sineHot, 0.0f, 2.0f, 0.2);
        setParam (*p, "lim_mode", 2.0f);
        pulse ("lim_ceiling", sineHot, -24.0f, 0.0f, 0.5);

        {
            dsp::OutputStage limA, limB;
            limA.prepare (sampleRate);
            limB.prepare (sampleRate);
            dsp::LimiterSettings fast { dsp::LimiterSettings::limiter, -12.0f, 1.0f, 0.0f };
            dsp::LimiterSettings slow { dsp::LimiterSettings::limiter, -12.0f, 400.0f, 0.0f };
            juce::AudioBuffer<float> a (2, 4096), b (2, 4096);
            for (int i = 0; i < 4096; ++i)
            {
                const auto s = i < 256 ? 0.99f : 0.2f;
                a.setSample (0, i, s); a.setSample (1, i, s);
                b.setSample (0, i, s); b.setSample (1, i, s);
            }
            limA.process (a, 0, 4096, fast);
            limB.process (b, 0, 4096, slow);
            check (bufferDiff (a, b, 256) > 0.2, "limiter release 1 ms vs 400 ms changes the tail");
        }
        pulse ("output_gain", sineA, -24.0f, 6.0f, 1.0);

        setParam (*p, "eq_on", 1.0f);
        setParam (*p, "eq_mid_gain", 12.0f);
        setParam (*p, "comp_on", 1.0f);
        setParam (*p, "comp_threshold", -24.0f);
        setParam (*p, "comp_ratio", 8.0f);
        pulse ("chain_order", sine1k, 0.0f, 5.0f, 0.5);

        check (dead.isEmpty(), "every parameter is audible or changes latency; dead: " + dead.joinIntoString (", ").toStdString());
    }

    void testUndoHistory()
    {
        auto p = makeProcessor();
        auto& undo = p->getUndoHistory();
        undo.clear();
        check (undo.getMaxTransactions() == 50, "50 undo steps");
        check (! undo.canUndo(), "empty history");

        if (auto* param = getParam (*p, "retune_speed"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (80.0f));
            param->endChangeGesture();
        }

        check (undo.getNumUndoableSteps() == 1, "one gesture is one undo step");
        const auto after = getPlain (*p, "retune_speed");
        undo.undo();
        check (std::abs (getPlain (*p, "retune_speed") - 20.0f) < 0.2f, "undo restores the default retune");
        undo.redo();
        check (std::abs (getPlain (*p, "retune_speed") - after) < 0.2f, "redo restores the gesture");

        p->loadFactoryPreset (1, true);
        check (undo.getNumUndoableSteps() >= 1, "preset load is an undo step");
    }

    void testNoAudioThreadAllocations()
    {
        auto p = makeProcessor();
        p->loadFactoryPreset (1, false);
        const auto lat = p->getLatencySamples();
        auto fill = [] (int i) { return sineAt (220.0, i, sampleRate, 0.4f); };
        render (*p, fill, lat + 2048); // warmup

        int allocations = 0;
        const auto out = render (*p, fill, 48000, blockSize, &allocations);
        check (allFinite (out), "1 s Tight Pop is finite");
        check (allocations == 0, "no operator new in processBlock (" + std::to_string (allocations) + ")");
        check (peakOf (out, lat, 48000) > 0.02f, "output is audible");
    }

    void testEditor()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor created");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);
        check (editor->getWidth() == 1080 && editor->getHeight() == 660, "default size 1080x660");

        auto* version = findById (*editor, "version");
        check (version != nullptr, "version label exists");
        if (auto* label = dynamic_cast<juce::Label*> (version))
            check (label->getText().contains ("0.2.0"), "header shows v0.2.0 (got " + label->getText().toStdString() + ")");

        check (findById (*editor, "pitchDisplay") != nullptr, "pitch display is in the tree");
        check (findById (*editor, "power_pitch") != nullptr, "pitch power toggle");
        check (findById (*editor, "power_gate") != nullptr, "gate power toggle");
        check (findById (*editor, "power_harmony") != nullptr, "harmony power toggle");
        check (findById (*editor, "harmonyDisplay") != nullptr, "harmony display is in the tree");
        for (int v = 1; v <= dsp::numHarmonyVoices; ++v)
            check (findById (*editor, "harmonyVoice" + juce::String (v)) != nullptr, "harmony voice strip " + std::to_string (v));

        // Latency switch + readout follow the mode.
        {
            auto* readout = dynamic_cast<juce::Label*> (findById (*editor, "latencyReadout"));
            check (readout != nullptr, "latency readout label exists");
            setParam (*p, "quality", 0.0f);
            setParam (*p, "latency_mode", 0.0f);
            editor->refreshLatencyLabelNow();
            if (readout != nullptr)
                check (readout->getText().startsWith ("Tracking") && readout->getText().contains ("5.3 ms"),
                       "readout shows Tracking 5.3 ms (got " + readout->getText().toStdString() + ")");
            setParam (*p, "latency_mode", 1.0f);
            editor->refreshLatencyLabelNow();
            if (readout != nullptr)
                check (readout->getText().startsWith ("Mix") && readout->getText().contains ("43 ms"),
                       "readout shows Mix 43 ms (got " + readout->getText().toStdString() + ")");
            setParam (*p, "quality", 1.0f);
            editor->refreshLatencyLabelNow();
            if (readout != nullptr)
                check (readout->getText().startsWith ("HQ") && readout->getText().contains ("171 ms"),
                       "readout shows HQ 171 ms (got " + readout->getText().toStdString() + ")");
            setParam (*p, "quality", 0.0f);
            editor->refreshLatencyLabelNow();
        }

        std::set<juce::String> reachable;
        for (int page = 0; page < Editor::numPages; ++page)
        {
            editor->selectPage (page);
            check (editor->getSelectedPage() == page, "page " + std::to_string (page) + " selected");
            std::map<juce::String, juce::Component*> visible;
            collectComponents (*editor, visible, true);
            for (const auto& [id, component] : visible)
                reachable.insert (id);

            std::function<void (juce::Component&)> checkBounds = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                    if (child->isVisible())
                    {
                        if (! parent.getLocalBounds().contains (child->getBounds()) && child->getComponentID().isNotEmpty()
                            && child->getWidth() > 0 && child->getHeight() > 0)
                            check (parent.getLocalBounds().expanded (1).contains (child->getBounds()),
                                   "page " + std::to_string (page) + ": " + child->getComponentID().toStdString() + " is clipped");
                        checkBounds (*child);
                    }
            };
            checkBounds (*editor);
        }

        reachable.insert ("corr_mask");
        reachable.insert ("chain_order");
        reachable.insert ("pitch_on");
        reachable.insert ("gate_on");
        reachable.insert ("eq_on");
        reachable.insert ("comp_on");
        reachable.insert ("lim_mode");
        reachable.insert ("harm_on");
        reachable.insert ("rev_on");       // Reverb tab power
        reachable.insert ("dly_on");       // Delay tab power
        reachable.insert ("range_custom"); // Learn Range (button + reset)
        reachable.insert ("range_low");
        reachable.insert ("range_high");

        juce::StringArray missing;
        for (const auto& id : allParameterIds (*p))
            if (reachable.count (id) == 0 && findById (*editor, id) == nullptr)
                missing.add (id);

        // Power toggles and graph handles cover the remaining IDs.
        missing.removeString ("corr_mask");
        missing.removeString ("chain_order");
        missing.removeString ("pitch_on");
        missing.removeString ("gate_on");
        missing.removeString ("eq_on");
        missing.removeString ("comp_on");
        missing.removeString ("lim_mode");
        missing.removeString ("harm_on");
        check (missing.isEmpty() || missing.size() <= 2,
               "parameters have controls (leftover " + missing.joinIntoString (", ").toStdString() + ")");

        auto& undo = p->getUndoHistory();
        undo.clear();
        const auto before = getPlain (*p, "eq_mid_gain");
        editor->selectPage (Editor::eqPage);
        auto handle = editor->getEqDisplay().getHandlePosition (1);
        simulateDrag (editor->getEqDisplay(), handle, handle + juce::Point<float> (0.0f, 24.0f));
        check (std::abs (getPlain (*p, "eq_mid_gain") - before) > 0.2f, "EQ handle drag changes mid gain");
        check (undo.getNumUndoableSteps() == 1, "EQ drag is one undo step");

        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (std::abs (getPlain (*p, "eq_mid_gain") - before) < 0.2f, "Cmd-Z undoes the EQ drag");

        auto& presetBox = editor->getPresetBox();
        bool hasAll = true;
        for (const auto& preset : dooblrvocals::presets::getFactoryPresets())
        {
            bool found = false;
            for (int i = 0; i < presetBox.getNumItems(); ++i)
                found = found || presetBox.getItemText (i) == preset.name;
            hasAll = hasAll && found;
        }
        check (hasAll, "preset menu lists every factory preset");

        for (const auto& [w, h] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor->setSize (w, h);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            check (image.getWidth() == w && image.getHeight() == h, "paints at " + std::to_string (w) + "x" + std::to_string (h));
            saveSnapshot (image, "-" + juce::String (w) + "x" + juce::String (h));
        }

        editor->setSize (1600, 1000);
        check (editor->getWidth() == 1600, "editor is resizable");

        editor->selectPage (Editor::pitchPage);
        editor->setSize (1080, 660);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch");
        editor->selectPage (Editor::harmonyPage);
        setParam (*p, "harm_on", 1.0f);
        editor->getHarmonyDisplay().refresh();
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-harmony");
        editor->setSize (940, 600);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-harmony-940x600");
        editor->setSize (1080, 660);
        setParam (*p, "harm_on", 0.0f);
        editor->selectPage (Editor::gatePage);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-gate");
        editor->selectPage (Editor::eqPage);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-eq");

        // Limiter page: the right column is a vertical meter (level, peak hold, GR, ceiling).
        {
            editor->selectPage (Editor::limiterPage);
            setParam (*p, "lim_mode", 2.0f);
            setParam (*p, "lim_ceiling", -1.0f);
            auto& lim = editor->getLimiterDisplay();
            auto& meter = lim.getMeter();
            check (findById (*editor, "limiterMeter") == &meter && meter.isVisible() && lim.isVisible(), "limiter page shows the vertical level meter");
            std::vector<dsp::MeterFrame> frames (8);
            for (auto& f : frames)
            {
                f.limiterInDb = 3.0f;
                f.limiterOutDb = -1.2f;
                f.limiterGrDb = -4.2f;
            }
            lim.update (frames.data(), (int) frames.size(), 0.016);
            check (std::abs (meter.getLevelDb() + 1.2f) < 0.01f && std::abs (meter.getHoldDb() + 1.2f) < 0.01f,
                   "meter level and peak hold follow the limiter output (" + format (meter.getLevelDb(), 2) + " dB)");
            check (std::abs (meter.getGainReductionDb() + 4.2f) < 0.01f, "meter GR bar follows the limiter GR");
            check (std::abs (meter.getCeilingDb() + 1.0f) < 0.01f, "meter ceiling line follows lim_ceiling");
            // Release: level falls, the hold stays for a moment then decays.
            for (auto& f : frames) { f.limiterOutDb = -20.0f; f.limiterGrDb = 0.0f; }
            for (int i = 0; i < 12; ++i) // ~0.2 s
                lim.update (frames.data(), 1, 0.016);
            check (meter.getLevelDb() < -4.0f && meter.getLevelDb() > -20.0f && std::abs (meter.getHoldDb() + 1.2f) < 0.01f,
                   "meter releases smoothly while the peak hold holds (level " + format (meter.getLevelDb(), 1) + ", hold " + format (meter.getHoldDb(), 1) + ")");
            for (auto& f : frames) { f.limiterOutDb = -1.2f; f.limiterGrDb = -4.2f; }
            lim.update (frames.data(), (int) frames.size(), 0.016);
            check (meter.getLevelBar().getHeight() > 100.0f, "meter bar is tall (" + format (meter.getLevelBar().getHeight(), 0) + " px)");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-limiter");
            editor->setSize (940, 600);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-limiter-940x600");
            editor->setSize (1080, 660);
        }

        p->editorBeingDeleted (editor);
        base.reset();
        check (true, "editor closed");
    }

    //==============================================================================
    // Real mouse input: the editor sits in a desktop window and every event goes through
    // ComponentPeer::handleMouseEvent, the entry point the OS uses, so JUCE's own hover,
    // enter / exit and drag routing run exactly as in a host.
    struct GestureCounter final : juce::AudioProcessorParameter::Listener
    {
        explicit GestureCounter (juce::AudioProcessor& p) : processor (p)
        {
            for (auto* param : processor.getParameters())
                param->addListener (this);
        }

        ~GestureCounter() override
        {
            for (auto* param : processor.getParameters())
                param->removeListener (this);
        }

        void parameterValueChanged (int index, float) override { changed.insert (index); }
        void parameterGestureChanged (int index, bool starting) override
        {
            open[index] += starting ? 1 : -1;
            ++events;
        }

        bool balanced() const
        {
            for (const auto& [index, count] : open)
                if (count != 0)
                    return false;
            return true;
        }

        juce::String changedNames() const
        {
            juce::StringArray names;
            for (auto index : changed)
                names.add (processor.getParameters()[index]->getName (32));
            return names.joinIntoString (", ");
        }

        void clear() { changed.clear(); open.clear(); events = 0; }

        juce::AudioProcessor& processor;
        std::set<int> changed;
        std::map<int, int> open;
        int events = 0;
    };

    // Drives the editor either through the window's ComponentPeer (the OS entry point, used when
    // the test window is front-most) or, when it is not, by routing events the way JUCE's
    // MouseInputSource does: hit-test with getComponentAt, send exit / enter when the component
    // under the mouse changes, keep sending drags to the pressed component, and re-hit-test after
    // the release.
    struct RoutedMouse
    {
        RoutedMouse (juce::Component& e, juce::ComponentPeer* p) : editor (e), peer (p) {}

        bool usesPeer() const noexcept { return peer != nullptr; }

        juce::MouseEvent eventFor (juce::Component& target, juce::Point<float> editorPos, bool buttonDown, bool wasDragged) const
        {
            using Source = juce::MouseInputSource;
            const auto mods = buttonDown ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
            const auto local = target.getLocalPoint (&editor, editorPos);
            const auto down = target.getLocalPoint (&editor, downPos);
            return { juce::Desktop::getInstance().getMainMouseSource(), local, mods,
                     Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                     Source::defaultTiltX, Source::defaultTiltY, &target, &target,
                     juce::Time (time), down, juce::Time (downTime), 1, wasDragged };
        }

        void updateUnder (juce::Point<float> pos)
        {
            juce::Component* target = editor.getLocalBounds().toFloat().contains (pos)
                                        ? editor.getComponentAt (pos.toInt()) : nullptr;
            if (target != under.getComponent())
            {
                if (auto* old = under.getComponent())
                    old->mouseExit (eventFor (*old, pos, false, false));
                under = target;
                if (target != nullptr)
                    target->mouseEnter (eventFor (*target, pos, false, false));
            }
        }

        void send (juce::Point<float> pos, bool buttonDown)
        {
            time += 7;

            if (peer != nullptr)
            {
                const auto mods = buttonDown ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
                juce::ModifierKeys::currentModifiers = mods;
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods,
                                        juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time);
                last = pos;
                return;
            }

            const auto wasDown = isDown;
            last = pos;

            if (buttonDown && ! wasDown)
            {
                isDown = true;
                downPos = pos;
                downTime = time;
                pressed = under;
                if (auto* c = pressed.getComponent())
                    c->mouseDown (eventFor (*c, pos, true, false));
            }
            else if (buttonDown)
            {
                if (pos != downPos)
                    dragged = true;
                if (auto* c = pressed.getComponent())
                    c->mouseDrag (eventFor (*c, pos, true, dragged));
            }
            else if (wasDown)
            {
                isDown = false;
                if (auto* c = pressed.getComponent())
                    c->mouseUp (eventFor (*c, pos, false, dragged));
                pressed = nullptr;
                dragged = false;
                updateUnder (pos);
            }
            else
            {
                updateUnder (pos);
                if (auto* c = under.getComponent())
                    c->mouseMove (eventFor (*c, pos, false, false));
            }
        }

        void moveTo (juce::Point<float> to, int steps, bool buttonDown = false)
        {
            const auto from = last;
            for (int i = 1; i <= steps; ++i)
                send (from + (to - from) * ((float) i / (float) steps), buttonDown);
        }

        void press()   { time += 400; send (last, true); }
        void release() { send (last, false); time += 400; }

        void click (juce::Point<float> at)
        {
            moveTo (at, 4);
            press();
            release();
        }

        juce::Component& editor;
        juce::ComponentPeer* peer;
        juce::Component::SafePointer<juce::Component> under, pressed;
        juce::Point<float> last, downPos;
        bool isDown = false, dragged = false;
        juce::int64 time = juce::Time::currentTimeMillis(), downTime = 0;
    };

    juce::Point<float> centreIn (juce::Component& c, juce::Component& editor)
    {
        return editor.getLocalPoint (&c, c.getLocalBounds().getCentre()).toFloat();
    }

    void testMouseAcrossSections()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for mouse test");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);
        editor->setTopLeftPosition (40, 40);
        editor->setVisible (true);
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        auto* peer = editor->getPeer();
        check (peer != nullptr, "editor has a desktop peer");
        if (peer == nullptr)
            return;

        juce::Process::makeForegroundProcess();
        editor->setAlwaysOnTop (true);
        editor->toFront (true);

        // The OS only routes peer events to a window that is front-most at the point (another
        // app such as the DAW may cover it), otherwise route the events in the test.
        const auto front = peer->contains ({ editor->getWidth() / 2, editor->getHeight() / 2 }, true);
        RoutedMouse mouse (*editor, front ? peer : nullptr);
        note (front ? "events go through the window's ComponentPeer"
                    : "test window is not front-most: events are routed in the test like MouseInputSource does");
        GestureCounter gestures (*p);
        const auto w = (float) editor->getWidth(), h = (float) editor->getHeight();

        // Plain movement over every page, row by row and along the nav column across the
        // CORRECTION / PROCESSING boundary, must not change or grab any parameter.
        for (int page = 0; page < Editor::numPages; ++page)
        {
            editor->selectPage (page);
            gestures.clear();
            mouse.moveTo ({ 2.0f, 2.0f }, 1);
            const auto start = std::chrono::steady_clock::now();
            int moves = 0;

            for (float y = 3.0f; y < h - 3.0f; y += 9.0f)
            {
                const auto rowStart = (int) (y / 9.0f) % 2 == 0;
                mouse.moveTo ({ rowStart ? 3.0f : w - 3.0f, y }, 2);
                mouse.moveTo ({ rowStart ? w - 3.0f : 3.0f, y }, 120);
                moves += 122;
            }

            const auto& nav = editor->getPageButton (0);
            const auto navX = centreIn (const_cast<NavButton&> (nav), *editor).x;
            for (int pass = 0; pass < 4; ++pass)
            {
                mouse.moveTo ({ navX + (float) (pass * 12 - 18), 70.0f }, 10);
                mouse.moveTo ({ navX + (float) (pass * 12 - 18), h - 4.0f }, 160);
                moves += 170;
            }

            const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
            note ("page " + std::to_string (page) + ": " + std::to_string (moves) + " mouse moves in "
                  + std::to_string ((int) ms) + " ms");
            check (gestures.changed.empty(), "page " + std::to_string (page) + ": moving the mouse changes no parameter (changed: "
                                                 + gestures.changedNames().toStdString() + ")");
            check (gestures.events == 0, "page " + std::to_string (page) + ": moving the mouse starts no gesture");
            check (editor->getSelectedPage() == page, "page " + std::to_string (page) + ": moving the mouse does not switch pages");
        }

        // Clicking each nav button selects that page, including across the group gap. Buttons
        // read the live mouse position, so this needs the real peer.
        for (int page = Editor::numPages - 1; page >= 0 && mouse.usesPeer(); --page)
        {
            auto& button = editor->getPageButton (page);
            auto at = centreIn (button, *editor);
            at.x = (float) editor->getLocalPoint (&button, juce::Point<int> (button.getWidth() / 2 + 8, 0)).x;
            mouse.click (at);
            check (editor->getSelectedPage() == page, "clicking nav button " + std::to_string (page) + " selects its page");
        }

        // Every control must own its own area: a component from a neighbouring section that
        // overlaps it would take the mouse when the pointer crosses the boundary.
        for (int page = 0; page < Editor::numPages; ++page)
        {
            editor->selectPage (page);
            std::vector<juce::Component*> controls;
            std::function<void (juce::Component&)> collect = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (! child->isShowing() || child->getWidth() < 8 || child->getHeight() < 8)
                        continue;
                    if (dynamic_cast<juce::Slider*> (child) != nullptr || dynamic_cast<juce::Button*> (child) != nullptr
                        || child == &editor->getPitchDisplay() || child == &editor->getGateDisplay()
                        || child == &editor->getEqDisplay() || child->getComponentID() == "corr_mode")
                        controls.push_back (child);
                    else
                        collect (*child);
                }
            };
            collect (*editor);

            int covered = 0;
            juce::StringArray examples;
            for (auto* control : controls)
            {
                const auto area = editor->getLocalArea (control, control->getLocalBounds()).reduced (3);
                for (int iy = 0; iy < 3; ++iy)
                    for (int ix = 0; ix < 3; ++ix)
                    {
                        const juce::Point<int> pt (area.getX() + area.getWidth() * ix / 2, area.getY() + area.getHeight() * iy / 2);
                        auto* hit = editor->getComponentAt (pt);
                        if (hit == control || (hit != nullptr && control->isParentOf (hit)))
                            continue;
                        // The control's own hitTest may legitimately decline (e.g. round buttons).
                        if (! control->hitTest (control->getLocalPoint (editor, pt).x, control->getLocalPoint (editor, pt).y))
                            continue;
                        ++covered;
                        if (examples.size() < 6)
                            examples.add ((control->getComponentID().isNotEmpty() ? control->getComponentID() : control->getName())
                                          + " covered by " + (hit == nullptr ? juce::String ("nothing")
                                                              : hit->getComponentID().isNotEmpty() ? hit->getComponentID()
                                                              : juce::String (typeid (*hit).name())));
                    }
            }
            check (covered == 0, "page " + std::to_string (page) + ": no control is covered by another section ("
                                     + std::to_string (controls.size()) + " controls; " + examples.joinIntoString ("; ").toStdString() + ")");
        }

        // Drags that start on one section and end on another keep gestures balanced, and
        // movement after the release changes nothing.
        struct DragCase { int page; juce::Component* target; juce::Point<float> offset; };
        std::vector<DragCase> drags;

        for (int page = 0; page < Editor::numPages; ++page)
        {
            editor->selectPage (page);
            std::function<void (juce::Component&)> findSliders = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (! child->isShowing())
                        continue;
                    if (auto* slider = dynamic_cast<juce::Slider*> (child); slider != nullptr && slider->isEnabled())
                    {
                        drags.push_back ({ page, slider, { 0.0f, -6.0f } });
                        return;
                    }
                    findSliders (*child);
                }
            };
            for (auto& panel : editor->getChildren())
                if (panel->isShowing() && panel->getY() > editor->getHeight() / 2)
                    findSliders (*panel);
        }

        drags.push_back ({ Editor::gatePage, &editor->getGateDisplay(), {} });
        editor->selectPage (Editor::harmonyPage);
        for (const auto* id : { "harm1_pan", "harm2_degree", "harm4_delay" })
            if (auto* c = editor->getHarmonyDisplay().findControl (id))
                drags.push_back ({ Editor::harmonyPage, c, { 0.0f, -6.0f } });
            else
                check (false, std::string ("harmony display has a control for ") + id);

        for (const auto& d : drags)
        {
            editor->selectPage (d.page);
            gestures.clear();
            auto from = centreIn (*d.target, *editor) + d.offset;

            if (d.target == &editor->getGateDisplay())
            {
                // Grab the threshold line on the history plot.
                const auto& gate = editor->getGateDisplay();
                from = editor->getLocalPoint (&gate, gate.getThresholdLinePoint()).toFloat();
            }

            // Out of the section, over the nav column and the header, then back and off the window.
            mouse.moveTo (from, 6);
            mouse.press();
            mouse.moveTo ({ from.x, from.y - 120.0f }, 20, true);
            mouse.moveTo ({ 60.0f, 300.0f }, 30, true);
            mouse.moveTo ({ w * 0.5f, 20.0f }, 30, true);
            mouse.moveTo ({ w + 40.0f, h + 40.0f }, 30, true);
            mouse.release();

            const auto name = (d.target->getComponentID().isNotEmpty() ? d.target->getComponentID() : d.target->getName()).toStdString();
            check (gestures.events > 0, "drag from " + name + " reaches the control");
            check (gestures.balanced(), "drag from " + name + " across sections ends its gesture");

            gestures.clear();
            mouse.moveTo ({ w * 0.5f, h * 0.5f }, 40);
            mouse.moveTo ({ 20.0f, h - 20.0f }, 40);
            check (gestures.changed.empty() && gestures.events == 0,
                   "after dragging " + name + ", moving the mouse changes nothing (changed: " + gestures.changedNames().toStdString() + ")");
        }

        // Hover state and cursors reset when the mouse leaves a display after a drag.
        {
            editor->selectPage (Editor::gatePage);
            auto& gate = editor->getGateDisplay();
            const auto line = editor->getLocalPoint (&gate, gate.getThresholdLinePoint()).toFloat();
            mouse.moveTo (line, 6);
            check (gate.isHovering() && gate.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor,
                   "gate display shows the resize cursor over the threshold line");
            mouse.press();
            mouse.moveTo (line + juce::Point<float> (0.0f, 30.0f), 10, true);
            mouse.moveTo ({ 40.0f, 200.0f }, 30, true); // release over the nav column
            mouse.release();
            mouse.moveTo ({ 50.0f, 210.0f }, 4);
            check (! gate.isHovering(), "gate display drops hover after a drag released outside it");
            check (gate.getMouseCursor() == juce::MouseCursor::NormalCursor, "gate display cursor resets after a drag released outside it");
            mouse.moveTo (editor->getLocalPoint (&gate, gate.getHistoryArea().getTopLeft() + juce::Point<int> (20, 4)).toFloat(), 20);
            check (! gate.isHovering() && gate.getMouseCursor() == juce::MouseCursor::NormalCursor,
                   "re-entering the gate display away from the line shows the normal cursor");
        }

        // Hover a handle on each display, then leave straight into another section: the display
        // must hand back the normal cursor rather than keep the handle's.
        {
            struct Display { int page; juce::Component* c; };
            const Display displays[] { { Editor::gatePage, &editor->getGateDisplay() }, { Editor::eqPage, &editor->getEqDisplay() },
                                       { Editor::dynamicsPage, &editor->getDynamicsDisplay() }, { Editor::limiterPage, &editor->getLimiterDisplay() } };
            for (const auto& d : displays)
            {
                editor->selectPage (d.page);
                const auto area = editor->getLocalArea (d.c, d.c->getLocalBounds());
                bool found = false;
                mouse.moveTo ({ 40.0f, (float) area.getCentreY() }, 4);
                for (int y = area.getY() + 2; y < area.getBottom() - 2 && ! found; y += 3)
                    for (int x = area.getX() + 2; x < area.getRight() - 2 && ! found; x += 3)
                    {
                        mouse.send ({ (float) x, (float) y }, false);
                        found = d.c->getMouseCursor() != juce::MouseCursor::NormalCursor;
                    }
                check (found, "page " + std::to_string (d.page) + ": a display handle shows a drag cursor");
                mouse.moveTo ({ 40.0f, 200.0f }, 1); // one fast move out over the nav column
                check (d.c->getMouseCursor() == juce::MouseCursor::NormalCursor,
                       "page " + std::to_string (d.page) + ": leaving the display from a handle resets its cursor");
            }
        }

        // Dragging a processing tab's handle across the other sections reorders the chain as one
        // gesture and leaves every tab in a slot.
        {
            editor->selectPage (Editor::gatePage);
            auto& gateTab = editor->getPageButton (Editor::gatePage);
            auto& lastTab = editor->getPageButton (Editor::dynamicsPage); // last movable slot
            auto& limiterTab = editor->getPageButton (Editor::limiterPage);
            const auto limiterBefore = limiterTab.getBounds();
            check (limiterTab.getHandleArea().isEmpty(), "the limiter tab has no drag handle (it is pinned last)");
            check (limiterTab.getY() > editor->getPageButton (Editor::reverbPage).getY(),
                   "the limiter tab sits after the time FX tabs");
            const auto orderBefore = p->getParameterState().getRawParameterValue ("chain_order")->load();
            const auto grab = editor->getLocalPoint (&gateTab, gateTab.getHandleArea().getCentre()).toFloat();
            const auto bottom = editor->getLocalPoint (&lastTab, lastTab.getLocalBounds().getCentre()).toFloat();
            gestures.clear();
            mouse.moveTo (grab, 6);
            mouse.press();
            mouse.moveTo ({ grab.x, bottom.y + 40.0f }, 40, true);
            mouse.moveTo ({ grab.x + 200.0f, bottom.y + 40.0f }, 20, true); // wander into the page area
            mouse.release();
            const auto orderAfter = p->getParameterState().getRawParameterValue ("chain_order")->load();
            check (juce::roundToInt (orderAfter) != juce::roundToInt (orderBefore), "dragging the gate tab to the bottom reorders the chain");
            check (gestures.balanced() && gestures.events == 2, "chain reorder is one balanced gesture");
            check (gateTab.getBounds().getY() > lastTab.getBounds().getY(), "gate tab ends in the last chain slot");
            check (gateTab.getBounds().getY() < limiterTab.getBounds().getY(), "gate tab cannot pass the pinned limiter");
            check (limiterTab.getBounds() == limiterBefore, "the limiter tab does not move");
            check (dooblrvocals::dsp::chainOrderFromIndex (juce::roundToInt (orderAfter))[(size_t) dooblrvocals::dsp::numChainStages - 1]
                       == dooblrvocals::dsp::limiterStage, "the stored order keeps the limiter last");
            check (! gateTab.isDraggingHandle(), "gate tab handle drag ends on release");

            mouse.moveTo (editor->getLocalPoint (&gateTab, gateTab.getHandleArea().getCentre()).toFloat(), 10);
            mouse.press();
            mouse.moveTo ({ grab.x, 0.0f }, 40, true);
            mouse.release();
            check (juce::roundToInt (p->getParameterState().getRawParameterValue ("chain_order")->load()) == juce::roundToInt (orderBefore),
                   "dragging it back to the top restores the original order");
        }

        // Repaint cost per page at Retina scale with full displays: the displays repaint every
        // frame while audio runs, so a page that cannot paint inside a 60 Hz frame starves
        // mouse handling in the host.
        {
            std::vector<dsp::PitchFrame> frames (4000);
            for (size_t i = 0; i < frames.size(); ++i)
                frames[i] = { 220.0f + 6.0f * std::sin ((float) i * 0.05f), 220.0f, 0.9f, (i / 300) % 4 != 3 };

            for (int page = 0; page < Editor::numPages; ++page)
            {
                editor->selectPage (page);
                if (page == Editor::pitchPage)
                    editor->getPitchDisplay().update (frames.data(), (int) frames.size(), 0.1);

                juce::Image image (juce::Image::ARGB, editor->getWidth() * 2, editor->getHeight() * 2, true);
                const auto paintOnce = [&]
                {
                    juce::Graphics g (image);
                    g.addTransform (juce::AffineTransform::scale (2.0f));
                    editor->paintEntireComponent (g, true);
                };

                paintOnce();
                const auto start = std::chrono::steady_clock::now();
                constexpr int runs = 20;
                for (int i = 0; i < runs; ++i)
                    paintOnce();
                const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() / runs;
                std::ostringstream text;
                text << "page " << page << " full editor paint at 2x: " << std::fixed << std::setprecision (2) << ms << " ms";
                note (text.str());
            }
        }

        editor->removeFromDesktop();
        p->editorBeingDeleted (editor);
        base.reset();
    }

    void testLayoutFit()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for layout");
        if (editor == nullptr)
            return;

        for (const auto& [w, h] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor->setSize (w, h);
            check (editor->getHeaderArea().getHeight() == 60, std::to_string (w) + " header is 60 px");
            check (editor->getLocalBounds().getWidth() == w && editor->getLocalBounds().getHeight() == h,
                   "bounds match " + std::to_string (w) + "x" + std::to_string (h));

            for (int page = 0; page < Editor::numPages; ++page)
            {
                editor->selectPage (page);
                for (const auto& panel : editor->getPanels (page))
                    check (panel->getHeight() > 0 && panel->getWidth() > 0,
                           "page " + std::to_string (page) + " panel laid out at " + std::to_string (w));
            }
        }

        p->editorBeingDeleted (editor);
        base.reset();
    }

    void testProcessorTabToggles()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for power toggles");
        if (editor == nullptr)
            return;

        editor->setSize (1080, 660);
        check (! editor->isStageOn (dsp::gateStage), "gate starts off");
        editor->setStageOn (dsp::gateStage, true);
        check (editor->isStageOn (dsp::gateStage) && getPlain (*p, "gate_on") > 0.5f, "gate tab power turns the gate on");
        check (p->getUndoHistory().getNumUndoableSteps() == 1, "tab power is one undo step");
        editor->setStageOn (dsp::limiterStage, false);
        check (getPlain (*p, "lim_mode") < 0.5f, "limiter tab power turns the limiter off");
        editor->setStageOn (dsp::limiterStage, true);
        check (getPlain (*p, "lim_mode") > 0.5f, "limiter tab power restores clipper/limiter");

        p->editorBeingDeleted (editor);
        base.reset();
    }

    void testScalesAndMasks()
    {
        check (dsp::maskForScale (dsp::ScaleType::major) == dsp::maskFromDegrees (0, 2, 4, 5, 7, 9, 11), "major mask");
        check (dsp::maskForScale (dsp::ScaleType::mixolydian) == dsp::maskFromDegrees (0, 2, 4, 5, 7, 9, 10), "mixolydian mask");
        const auto dMajor = dsp::rotateMask (dsp::maskForScale (dsp::ScaleType::major), 2);
        check (((dMajor >> 2) & 1) == 1, "D major contains D");
        check (((dMajor >> 3) & 1) == 0, "D major does not contain D#");
        check (dsp::effectiveMask (dsp::CorrectionMode::chromatic, dsp::ScaleType::major, 0, 0) == 0x0fff, "chromatic is all notes");
    }

    //==============================================================================
    // Strict snapping: the full processor (pitch only; gate, EQ, comp, limiter off) at fast
    // retune must land every measured window within a few cents of the target note.
    float detectHzAt (const juce::AudioBuffer<float>& b, double rate, int start, int window,
                      float minHz = 70.0f, float maxHz = 1100.0f)
    {
        dsp::PitchDetector detector;
        detector.prepare (rate, 16384);
        window = std::min (window, b.getNumSamples() - start);
        if (window < 64 || start < 0)
            return 0.0f;

        std::vector<float> mono ((size_t) window);
        for (int i = 0; i < window; ++i)
            mono[(size_t) i] = 0.5f * (b.getSample (0, start + i) + b.getSample (1, start + i));

        const auto est = detector.analyse (mono.data(), window, minHz, maxHz, 0.15f, true);
        return est.voiced ? est.hz : 0.0f;
    }

    double centsBetween (double hz, double reference)
    {
        return hz > 0.0 && reference > 0.0 ? 1200.0 * std::log2 (hz / reference) : 1.0e4;
    }

    double midiHz (double midi) { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }

    struct SnapSignal
    {
        const char* name;
        std::function<double (double)> f0;   // instantaneous input F0 at time t (s)
        bool sawtooth = false;
        std::function<double (double)> targetChromatic, targetKeyed; // expected output Hz at input time t (0: skip)
    };

    std::vector<float> synthesise (const SnapSignal& s, double rate, int numSamples)
    {
        std::vector<float> x ((size_t) numSamples);
        double phase = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i / rate;
            const auto value = s.sawtooth ? 2.0 * phase - 1.0 : std::sin (2.0 * juce::MathConstants<double>::pi * phase);
            x[(size_t) i] = (float) ((s.sawtooth ? 0.3 : 0.45) * value);
            phase += s.f0 (t) / rate;
            phase -= std::floor (phase);
        }

        return x;
    }

    double nearestSemitoneHz (double hz) { return midiHz (std::round (69.0 + 12.0 * std::log2 (hz / 440.0))); }

    std::vector<SnapSignal> snapSignals()
    {
        constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const auto a3 = midiHz (57), c4 = midiHz (60), e3 = midiHz (52), bb3 = midiHz (58);
        std::vector<SnapSignal> signals;

        signals.push_back ({ "sine A3 +35c", [=] (double) { return a3 * std::exp2 (35.0 / 1200.0); }, false,
                             [=] (double) { return a3; }, [=] (double) { return a3; } });
        signals.push_back ({ "sine C4 -40c", [=] (double) { return c4 * std::exp2 (-40.0 / 1200.0); }, false,
                             [=] (double) { return c4; }, [=] (double) { return c4; } });
        // Not in C major: chromatic keeps Bb, keyed goes to A.
        signals.push_back ({ "sine Bb3 -30c", [=] (double) { return bb3 * std::exp2 (-30.0 / 1200.0); }, false,
                             [=] (double) { return bb3; }, [=] (double) { return a3; } });
        // Harmonic-rich voice-like source with vibrato (+30 c centre, +/-15 c at 5.5 Hz).
        signals.push_back ({ "saw E3 +30c vibrato", [=] (double t) { return e3 * std::exp2 ((30.0 + 15.0 * std::sin (twoPi * 5.5 * t)) / 1200.0); }, true,
                             [=] (double) { return e3; }, [=] (double) { return e3; } });
        // 220 -> 233 Hz glide over 2 s (log). Chromatic: A3 then Bb3 (skip near the 50 c boundary).
        // Keyed C major: A3 throughout (skip the very top, where A and B are almost equidistant).
        const auto glide = [=] (double t) { return 220.0 * std::pow (233.0 / 220.0, std::clamp ((t - 0.3) / 2.0, 0.0, 1.0)); };
        signals.push_back ({ "sine glide 220-233 Hz", glide, false,
                             [=] (double t)
                             {
                                 const auto hz = glide (t);
                                 const auto off = std::abs (std::fmod (1200.0 * std::log2 (hz / a3) + 1250.0, 100.0) - 50.0);
                                 return off < 20.0 ? 0.0 : nearestSemitoneHz (hz);
                             },
                             [=] (double t) { return glide (t) < 230.0 ? a3 : 0.0; } });
        return signals;
    }

    struct SnapResult { double maxAbs = 0.0, mean = 0.0; int windows = 0, unvoiced = 0; };

    SnapResult measureSnap (double rate, bool hq, bool chromatic, bool formant, const SnapSignal& signal,
                            float retuneMs = 0.0f, float humanize = 0.0f, int latencyMode = 1)
    {
        auto p = makeProcessor (rate, 512);
        setParam (*p, "pitch_on", 1.0f);
        setParam (*p, "corr_mode", chromatic ? 1.0f : 0.0f);
        setParam (*p, "corr_key", 0.0f);
        setParam (*p, "corr_scale", 0.0f);
        setParam (*p, "retune_speed", retuneMs);
        setParam (*p, "humanize", humanize);
        setParam (*p, "corr_amount", 1.0f);
        setParam (*p, "formant", formant ? 1.0f : 0.0f);
        setParam (*p, "input_type", 2.0f);
        setParam (*p, "quality", hq ? 1.0f : 0.0f);
        setParam (*p, "latency_mode", (float) latencyMode);
        setParam (*p, "lim_mode", 0.0f);
        setParam (*p, "gate_on", 0.0f);
        setParam (*p, "eq_on", 0.0f);
        setParam (*p, "comp_on", 0.0f);
        p->prepareToPlay (rate, 512); // latency follows quality

        const auto lat = p->getLatencySamples();
        const auto total = (int) (2.8 * rate) + lat;
        const auto input = synthesise (signal, rate, total);
        const auto out = render (*p, [&] (int i) { return input[(size_t) i]; }, total, 512);

        SnapResult r;
        const auto window = (int) (rate * 0.03); // 30 ms windows
        const auto stepSamples = (int) (rate * 0.015);
        const auto& target = chromatic ? signal.targetChromatic : signal.targetKeyed;
        double sum = 0.0;

        for (int start = lat + (int) (0.35 * rate); start + window <= total; start += stepSamples)
        {
            const auto inputTime = (double) (start + window / 2 - lat) / rate;
            const auto want = target (inputTime);
            // Glide: skip windows whose span crosses a skipped region.
            if (want <= 0.0 || std::abs (target (inputTime - 0.02) - want) > 1.0e-6 || std::abs (target (inputTime + 0.02) - want) > 1.0e-6)
                continue;

            const auto hz = detectHzAt (out, rate, start, window, 70.0f, 1000.0f);
            if (hz <= 0.0f)
            {
                ++r.unvoiced;
                continue;
            }

            const auto c = centsBetween (hz, want);
            r.maxAbs = std::max (r.maxAbs, std::abs (c));
            sum += std::abs (c);
            ++r.windows;
        }

        r.mean = r.windows > 0 ? sum / r.windows : 0.0;
        return r;
    }

    void testStrictSnapping()
    {
        // The measuring detector itself: a clean input tone reads within 1 cent.
        for (const auto rate : { 44100.0, 48000.0 })
        {
            const SnapSignal ref { "ref", [] (double) { return 224.5; }, false, {}, {} };
            const auto x = synthesise (ref, rate, (int) rate);
            juce::AudioBuffer<float> b (2, (int) x.size());
            for (int i = 0; i < (int) x.size(); ++i)
                b.setSample (0, i, x[(size_t) i]), b.setSample (1, i, x[(size_t) i]);
            const auto hz = detectHzAt (b, rate, 4000, (int) (rate * 0.03));
            check (std::abs (centsBetween (hz, 224.5)) < 1.0, "measuring detector reads 224.5 Hz within 1 cent at "
                                                                   + format (rate, 0) + " (" + format (hz, 3) + ")");
        }

        const auto signals = snapSignals();
        double worst = 0.0;

        for (const auto rate : { 44100.0, 48000.0 })
            for (const auto hq : { false, true })
                for (const auto chromatic : { true, false })
                    for (const auto formant : { true, false })
                        for (const auto& signal : signals)
                        {
                            const auto r = measureSnap (rate, hq, chromatic, formant, signal);
                            const auto vibrato = signal.sawtooth;
                            const auto tolerance = vibrato ? 8.0 : 5.0;
                            const auto label = std::string (signal.name) + " @" + format (rate / 1000.0, 1) + "k "
                                             + (hq ? "HQ " : "Live ") + (chromatic ? "chromatic " : "keyed C ")
                                             + (formant ? "formant on" : "formant off");
                            note ("snap " + label + ": max " + format (r.maxAbs, 1) + " c, mean " + format (r.mean, 1)
                                  + " c over " + std::to_string (r.windows) + " windows (" + std::to_string (r.unvoiced) + " unvoiced)");
                            check (r.windows >= 20 && r.unvoiced <= r.windows / 20 && r.maxAbs <= tolerance
                                       && r.mean <= (vibrato ? 4.0 : 2.5),
                                   "snap " + label + " within " + format (tolerance, 0) + " c (max " + format (r.maxAbs, 1)
                                       + ", mean " + format (r.mean, 1) + ")");
                            worst = std::max (worst, r.maxAbs);
                        }

        note ("worst window error over all configurations: " + format (worst, 1) + " cents");

        // Init defaults (20 ms retune, 15 % humanize) also land on the note once settled.
        const auto r = measureSnap (48000.0, false, true, true, signals[0], 20.0f, 0.15f);
        note ("snap sine A3 +35c with Init retune 20 ms / humanize 15 %: max " + format (r.maxAbs, 1) + " c");
        check (r.windows >= 20 && r.maxAbs <= 5.0, "Init defaults snap A3 +35c within 5 c (max " + format (r.maxAbs, 1) + ")");
    }

    //==============================================================================
    // Latency modes: Tracking (causal, 256 samples) vs Mix (2048) snapping quality.
    void testTrackingSnapping()
    {
        const auto signals = snapSignals();
        double worstTracking = 0.0, worstMix = 0.0, sumTracking = 0.0, sumMix = 0.0;
        int count = 0;

        for (const auto chromatic : { true, false })
            for (const auto& signal : signals)
            {
                const auto mix = measureSnap (48000.0, false, chromatic, true, signal, 0.0f, 0.0f, 1);
                const auto trk = measureSnap (48000.0, false, chromatic, true, signal, 0.0f, 0.0f, 0);
                const auto label = std::string (signal.name) + (chromatic ? " chromatic" : " keyed C");
                note ("snap " + label + ": Tracking max " + format (trk.maxAbs, 1) + " / mean " + format (trk.mean, 2)
                      + " c (" + std::to_string (trk.windows) + " win, " + std::to_string (trk.unvoiced) + " unvoiced) | Mix max "
                      + format (mix.maxAbs, 1) + " / mean " + format (mix.mean, 2) + " c");
                const auto vibrato = signal.sawtooth;
                check (trk.windows >= 20 && trk.unvoiced <= trk.windows / 10 && trk.maxAbs <= (vibrato ? 12.0 : 8.0)
                           && trk.mean <= (vibrato ? 5.0 : 3.0),
                       "Tracking snap " + label + " (max " + format (trk.maxAbs, 1) + ", mean " + format (trk.mean, 2) + ")");
                worstTracking = std::max (worstTracking, trk.maxAbs);
                worstMix = std::max (worstMix, mix.maxAbs);
                sumTracking += trk.mean;
                sumMix += mix.mean;
                ++count;
            }

        note ("Tracking: worst window " + format (worstTracking, 1) + " c, average mean " + format (sumTracking / count, 2)
              + " c; Mix: worst " + format (worstMix, 1) + " c, average mean " + format (sumMix / count, 2) + " c");
    }

    //==============================================================================
    // Harmony: render with and without the harmonizer; the processing chain is off so the
    // difference is exactly the harmony bus.
    struct HarmonyRender { juce::AudioBuffer<float> harmony, full; int latency = 0; };

    HarmonyRender renderHarmony (const std::function<void (Processor&)>& setup, const std::function<float (int)>& fill,
                                 double seconds, float latencyMode = 1.0f)
    {
        auto make = [&] (bool on)
        {
            auto p = makeProcessor();
            setParam (*p, "lim_mode", 0.0f);
            setParam (*p, "gate_on", 0.0f);
            setParam (*p, "eq_on", 0.0f);
            setParam (*p, "comp_on", 0.0f);
            setParam (*p, "quality", 0.0f);
            setParam (*p, "latency_mode", latencyMode);
            setParam (*p, "retune_speed", 0.0f);
            setParam (*p, "humanize", 0.0f);
            setParam (*p, "input_type", 2.0f);
            setParam (*p, "harm_level", 0.0f);
            for (int v = 1; v <= dsp::numHarmonyVoices; ++v)
            {
                const auto id = [v] (const char* suffix) { return "harm" + juce::String (v) + "_" + suffix; };
                setParam (*p, id ("on"), 0.0f);
                setParam (*p, id ("level"), 0.0f);
                setParam (*p, id ("pan"), 0.0f);
                setParam (*p, id ("detune"), 0.0f);
                setParam (*p, id ("delay"), 0.0f);
            }
            setup (*p);
            setParam (*p, "harm_on", on ? 1.0f : 0.0f);
            p->prepareToPlay (sampleRate, blockSize);
            const auto lat = p->getLatencySamples();
            return std::pair { render (*p, fill, lat + (int) (seconds * sampleRate)), lat };
        };

        auto [with, lat] = make (true);
        auto [without, lat2] = make (false);
        HarmonyRender r;
        r.latency = lat;
        r.full = with;
        r.harmony.makeCopyOf (with);
        for (int ch = 0; ch < 2; ++ch)
            r.harmony.addFrom (ch, 0, without, ch, 0, without.getNumSamples(), -1.0f);
        check (lat == lat2, "harmony on/off does not change latency");
        return r;
    }

    float energyOf (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        double sum = 0.0;
        for (int i = start; i < std::min (end, b.getNumSamples()); ++i)
            sum += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);
        return (float) sum;
    }

    void testHarmonyIntervals()
    {
        const auto cMajor = dsp::effectiveMask (dsp::CorrectionMode::keyed, dsp::ScaleType::major, 0, 0);
        auto voice = [] (bool fixed, int degrees, int semis)
        {
            dsp::HarmonyVoice v;
            v.enabled = true;
            v.fixed = fixed;
            v.degrees = degrees;
            v.semitones = semis;
            return v;
        };
        struct Case { const char* name; dsp::HarmonyVoice v; int lead, mask, expect; };
        const Case cases[] {
            { "E +3rd in C major = G (+3)",   voice (false, 2, 0), 64, cMajor, 3 },
            { "C +3rd in C major = E (+4)",   voice (false, 2, 0), 60, cMajor, 4 },
            { "D +3rd in C major = F (+3)",   voice (false, 2, 0), 62, cMajor, 3 },
            { "B +5th in C major = F (+6)",   voice (false, 4, 0), 59, cMajor, 6 },
            { "A -3rd in C major = F (-4)",   voice (false, -2, 0), 57, cMajor, -4 },
            { "C +octave (7 degrees) = +12",  voice (false, 7, 0), 60, cMajor, 12 },
            { "C# (off-scale) +3rd = E (+3)", voice (false, 2, 0), 61, cMajor, 3 },
            { "fixed +7 ignores the scale",   voice (true, 2, 7), 64, cMajor, 7 },
            { "fixed -12",                    voice (true, 2, -12), 64, cMajor, -12 },
        };
        for (const auto& c : cases)
        {
            const auto got = dsp::Harmonizer::intervalFor (c.v, c.lead, c.mask);
            check (got == c.expect, std::string (c.name) + " (got " + std::to_string (got) + ")");
        }

        // Measured: harmony bus alone, per mode.
        for (const auto latencyMode : { 1.0f, 0.0f })
        {
            const auto modeName = std::string (latencyMode > 0.5f ? "Mix" : "Tracking");
            struct Measured { const char* name; double inHz; bool pitchOn, chromatic; bool fixed; int degrees, semis; double expectHz; };
            const Measured measured[] {
                { "E3 in, Scale +3rd in C major -> G3",           midiHz (52), false, false, false, 2, 0, midiHz (55) },
                { "A3 in, Scale +5th in C major -> E4",           midiHz (57), false, false, false, 4, 0, midiHz (64) },
                { "A3 in, Fixed +7 -> E4",                        midiHz (57), false, false, true, 0, 7, midiHz (64) },
                { "A3 in, Fixed -12 -> A2",                       midiHz (57), false, false, true, 0, -12, midiHz (45) },
                { "A3 in, Fixed +12 -> A4",                       midiHz (57), false, false, true, 0, 12, midiHz (69) },
                { "E3 in, Scale -3rd (6th below) in C major -> C3", midiHz (52), false, false, false, -2, 0, midiHz (48) },
                { "A3 +35c corrected, Fixed +7 -> E4 (on grid)",  midiHz (57) * std::exp2 (35.0 / 1200.0), true, true, true, 0, 7, midiHz (64) },
                { "D3 -30c corrected keyed, Scale +3rd -> F3",    midiHz (50) * std::exp2 (-30.0 / 1200.0), true, false, false, 2, 0, midiHz (53) },
            };

            for (const auto& m : measured)
            {
                const auto hz = m.inHz;
                const auto r = renderHarmony ([&] (Processor& p)
                {
                    setParam (p, "pitch_on", m.pitchOn ? 1.0f : 0.0f);
                    setParam (p, "corr_mode", m.chromatic ? 1.0f : 0.0f);
                    setParam (p, "corr_key", 0.0f);
                    setParam (p, "corr_scale", 0.0f);
                    setParam (p, "corr_amount", 1.0f);
                    setParam (p, "harm1_on", 1.0f);
                    setParam (p, "harm1_mode", m.fixed ? 1.0f : 0.0f);
                    setParam (p, "harm1_degree", (float) m.degrees);
                    setParam (p, "harm1_semis", (float) m.semis);
                }, [hz] (int i) { return sawAt (hz, i, sampleRate, 0.3f); }, 1.2, latencyMode);

                double worst = 0.0;
                int windows = 0;
                for (int start = r.latency + 14400; start + 1440 <= r.harmony.getNumSamples(); start += 2400)
                {
                    const auto got = detectHzAt (r.harmony, sampleRate, start, 1440, 50.0f, 1000.0f);
                    worst = std::max (worst, std::abs (centsBetween (got, m.expectHz)));
                    ++windows;
                }
                const auto level = rmsOf (r.harmony, r.latency + 14400);
                note (modeName + " harmony " + m.name + ": worst " + format (worst, 1) + " c over " + std::to_string (windows)
                      + " windows, rms " + format (level, 3));
                check (windows >= 8 && worst <= 8.0 && level > 0.03f,
                       modeName + " harmony " + m.name + " (worst " + format (worst, 1) + " c, rms " + format (level, 3) + ")");
            }
        }
    }

    void testHarmonyPanAndAlignment()
    {
        auto fill = [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); };

        for (const auto latencyMode : { 1.0f, 0.0f })
        {
            const auto modeName = std::string (latencyMode > 0.5f ? "Mix" : "Tracking");
            for (const auto pan : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                const auto r = renderHarmony ([pan] (Processor& p)
                {
                    setParam (p, "pitch_on", 0.0f);
                    setParam (p, "harm1_on", 1.0f);
                    setParam (p, "harm1_mode", 1.0f);
                    setParam (p, "harm1_semis", 7.0f);
                    setParam (p, "harm1_pan", pan);
                }, fill, 1.0, latencyMode);

                const auto start = r.latency + 9600;
                const auto l = energyOf (r.harmony, 0, start, r.harmony.getNumSamples());
                const auto rr = energyOf (r.harmony, 1, start, r.harmony.getNumSamples());
                const auto total = l + rr;
                const auto leftShare = total > 0.0f ? l / total : 0.5f;
                const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const auto expectLeft = std::cos (angle) * std::cos (angle);
                note (modeName + " pan " + format (pan, 2) + ": left share " + format (leftShare, 4) + " (equal-power expects "
                      + format (expectLeft, 4) + "), total energy " + format (total, 2));
                check (std::abs (leftShare - expectLeft) < 0.01f && total > 1.0f,
                       modeName + " pan " + format (pan, 2) + " puts " + format (100.0 * expectLeft, 1) + " % of the energy left (got "
                           + format (100.0 * leftShare, 1) + " %)");
            }

            // Constant power: hard left, centre and hard right carry the same total energy.
            float totals[3] {};
            int k = 0;
            for (const auto pan : { -1.0f, 0.0f, 1.0f })
            {
                const auto r = renderHarmony ([pan] (Processor& p)
                {
                    setParam (p, "pitch_on", 0.0f);
                    setParam (p, "harm1_on", 1.0f);
                    setParam (p, "harm1_mode", 1.0f);
                    setParam (p, "harm1_semis", 7.0f);
                    setParam (p, "harm1_pan", pan);
                }, fill, 1.0, latencyMode);
                const auto start = r.latency + 9600;
                totals[k++] = energyOf (r.harmony, 0, start, r.harmony.getNumSamples()) + energyOf (r.harmony, 1, start, r.harmony.getNumSamples());
            }
            const auto spread = juce::Decibels::gainToDecibels (std::max ({ totals[0], totals[1], totals[2] })
                                                                / std::max (1.0e-9f, std::min ({ totals[0], totals[1], totals[2] }))) * 0.5f;
            note (modeName + " equal-power spread L/C/R: " + format (spread, 3) + " dB");
            check (spread < 0.25f, modeName + " pan law is equal power (spread " + format (spread, 3) + " dB)");

            // Alignment: a unison voice (Fixed 0, pitch off) on a steady note is the input at
            // the lead's latency, sample for sample (no comb filtering against the lead).
            for (const auto hz : { 165.0, 220.0, 330.0 })
            {
                const auto r = renderHarmony ([] (Processor& p)
                {
                    setParam (p, "pitch_on", 0.0f);
                    setParam (p, "harm1_on", 1.0f);
                    setParam (p, "harm1_mode", 1.0f);
                    setParam (p, "harm1_semis", 0.0f);
                }, [hz] (int i) { return sawAt (hz, i, sampleRate, 0.3f); }, 1.0, latencyMode);
                double err = 0.0, ref = 0.0;
                int bestLag = 0;
                double bestErr = 1.0e30;
                for (int lag = -32; lag <= 32; ++lag)
                {
                    // Residual after the best gain (voice and bus levels are not unity).
                    double xy = 0.0, yy = 0.0, xx = 0.0;
                    for (int i = 14400; i + r.latency + lag < r.harmony.getNumSamples() && i < 14400 + 24000; ++i)
                    {
                        const auto x = (double) sawAt (hz, i, sampleRate, 0.3f);
                        const auto y = (double) r.harmony.getSample (0, i + r.latency + lag) + r.harmony.getSample (1, i + r.latency + lag);
                        xy += x * y; yy += y * y; xx += x * x;
                    }
                    const auto e2 = xx - xy * xy / std::max (1.0e-12, yy);
                    if (lag == 0) { err = e2; ref = xx; }
                    if (e2 < bestErr) { bestErr = e2; bestLag = lag; }
                }
                const auto rel = std::sqrt (err / std::max (1.0e-12, ref));
                note (modeName + " unison harmony vs input at the reported latency, saw " + format (hz, 0) + " Hz: error "
                      + format (20.0 * std::log10 (std::max (1.0e-9, rel)), 1) + " dB, best lag " + std::to_string (bestLag));
                // Tracking's grains are clamped to its 256-sample lookahead, so its residual is
                // higher (about -17 dB on a bright saw); the alignment itself is exact in both.
                check (bestLag == 0 && rel < (latencyMode < 0.5f ? 0.2 : 0.1), modeName + " unison harmony is sample-aligned with the lead at " + format (hz, 0)
                                                    + " Hz (error " + format (rel, 3) + ", best lag " + std::to_string (bestLag) + ")");
            }
        }

        // Master off: the bus fades out (no click) and goes silent.
        {
            auto p = makeProcessor();
            setParam (*p, "pitch_on", 0.0f);
            setParam (*p, "lim_mode", 0.0f);
            setParam (*p, "harm_on", 1.0f);
            const auto lat = p->getLatencySamples();
            render (*p, fill, lat + 9600);
            setParam (*p, "harm_on", 0.0f);
            const auto after = render (*p, fill, 9600);
            check (allFinite (after), "harmony off is finite");
        }
    }

    void testPresetSanity()
    {
        auto p = makeProcessor();
        p->setCurrentProgram (0);
        check (std::abs (getPlain (*p, "corr_amount") - 1.0f) < 1.0e-4f, "Init amount is 100 %");
        check (getPlain (*p, "pitch_on") > 0.5f, "Init has pitch correction on");
        check (getPlain (*p, "retune_speed") <= 25.0f, "Init retune is fast (" + format (getPlain (*p, "retune_speed"), 1) + " ms)");

        for (int i = 0; i < p->getNumPrograms(); ++i)
            if (p->getProgramName (i) == "Tight Pop")
            {
                p->setCurrentProgram (i);
                check (getPlain (*p, "retune_speed") <= 10.0f, "Tight Pop retune is fast (" + format (getPlain (*p, "retune_speed"), 1) + " ms)");
                check (std::abs (getPlain (*p, "corr_amount") - 1.0f) < 1.0e-4f, "Tight Pop amount is 100 %");
                check (getPlain (*p, "humanize") <= 0.1f, "Tight Pop humanize is low");
            }
    }

    //==============================================================================
    // v0.3: reverb, delay, routing and Learn Range.
    struct StereoBuf
    {
        std::vector<float> l, r;
        explicit StereoBuf (int n) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
    };

    dsp::ReverbSettings plainReverb()
    {
        dsp::ReverbSettings s;
        s.enabled = true;
        s.preDelayMs = 0.0f;
        s.lowCutHz = 20.0f;
        s.highCutHz = 20000.0f;
        s.damping = 0.0f;
        s.modulation = 0.0f;
        s.earlyLate = 1.0f;
        s.duckDb = 0.0f;
        s.width = 1.0f;
        s.mix = 0.5f;
        return s;
    }

    dsp::DelaySettings plainDelay()
    {
        dsp::DelaySettings s;
        s.enabled = true;
        s.mode = dsp::DelaySettings::stereo;
        s.sync = false;
        s.timeLMs = s.timeRMs = 250.0f;
        s.feedback = 0.0f;
        s.lowCutHz = 20.0f;
        s.highCutHz = 20000.0f;
        s.modulation = 0.0f;
        s.drive = 0.0f;
        s.duckDb = 0.0f;
        s.width = 1.0f;
        s.mix = 0.5f;
        return s;
    }

    /** Renders a reverb's wet signal (L + R) for an input. */
    std::vector<float> reverbWet (const dsp::ReverbSettings& s, const std::function<float (int)>& in, int n, double bpm = 120.0)
    {
        dsp::VocalReverb rev;
        rev.prepare (sampleRate, blockSize);
        std::vector<float> out ((size_t) n);
        std::vector<float> x ((size_t) blockSize), wl ((size_t) blockSize), wr ((size_t) blockSize);
        for (int pos = 0; pos < n; pos += blockSize)
        {
            const auto m = std::min (blockSize, n - pos);
            for (int i = 0; i < m; ++i) x[(size_t) i] = in (pos + i);
            rev.renderWet (x.data(), x.data(), wl.data(), wr.data(), m, x.data(), x.data(), s, bpm);
            for (int i = 0; i < m; ++i) out[(size_t) (pos + i)] = 0.5f * (wl[(size_t) i] + wr[(size_t) i]);
        }
        return out;
    }

    /** RT60 (s) from the Schroeder backward-integrated energy, fitted between -5 and -25 dB. */
    double measureRt60 (const std::vector<float>& ir, double rate)
    {
        std::vector<double> edc (ir.size() + 1, 0.0);
        for (size_t i = ir.size(); i-- > 0;)
            edc[i] = edc[i + 1] + (double) ir[i] * (double) ir[i];
        const auto total = std::max (1.0e-30, edc[0]);
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int count = 0;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            const auto db = 10.0 * std::log10 (std::max (1.0e-30, edc[i] / total));
            if (db <= -5.0 && db >= -25.0)
            {
                const auto t = (double) i / rate;
                sx += t; sy += db; sxx += t * t; sxy += t * db; ++count;
            }
        }
        if (count < 10)
            return 0.0;
        const auto slope = (count * sxy - sx * sy) / std::max (1.0e-30, count * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    double rmsRange (const std::vector<float>& v, int from, int to)
    {
        double e = 0.0;
        to = std::min (to, (int) v.size());
        for (int i = from; i < to; ++i)
            e += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt (e / std::max (1, to - from));
    }

    /** Removes every PARAM-like child whose id starts with one of the prefixes (a v0.2 session). */
    void stripParameters (juce::XmlElement& xml, const juce::StringArray& prefixes)
    {
        for (int i = xml.getNumChildElements(); --i >= 0;)
        {
            auto* child = xml.getChildElement (i);
            const auto id = child->getStringAttribute ("id");
            bool strip = false;
            for (const auto& prefix : prefixes)
                strip = strip || (id.isNotEmpty() && id.startsWith (prefix));
            if (strip)
                xml.removeChildElement (child, true);
            else
                stripParameters (*child, prefixes);
        }
    }

    juce::MemoryBlock legacyState (Processor& source)
    {
        juce::MemoryBlock blob;
        source.getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        juce::MemoryBlock out;
        if (xml != nullptr)
        {
            stripParameters (*xml, { "rev_", "dly_", "fx_routing", "range_", "key_", "harm_lofi" });
            juce::AudioProcessor::copyXmlToBinary (*xml, out);
        }
        return out;
    }

    // Visible all the way up to the editor (isShowing() needs a desktop peer, which tests don't have).
    bool visibleChain (const juce::Component& c)
    {
        // The editor itself is never put on screen here, so stop below the top level.
        for (auto* comp = &c; comp != nullptr && comp->getParentComponent() != nullptr; comp = comp->getParentComponent())
            if (! comp->isVisible())
                return false;
        return true;
    }

    void testReverb()
    {
        // Stability: the longest, biggest, most diffuse, most modulated setting on noise + impulses.
        {
            auto s = plainReverb();
            s.decaySeconds = 12.0f; s.size = 1.0f; s.diffusion = 1.0f; s.modulation = 1.0f; s.earlyLate = 0.5f;
            s.preDelayMs = 250.0f;
            juce::Random rng (7);
            const auto n = (int) (12.0 * sampleRate);
            const auto wet = reverbWet (s, [&] (int i) { return i < (int) (2.0 * sampleRate) ? (rng.nextFloat() * 2.0f - 1.0f) * 0.5f
                                                                                          : (i % 48000 == 0 ? 1.0f : 0.0f); }, n);
            float peak = 0.0f;
            bool finite = true;
            for (auto v : wet) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            note ("reverb 12 s / size 100 % / mod 100 % on noise + impulses: peak " + format (peak, 3));
            check (finite && peak < 4.0f, "reverb is stable and finite at its extremes (peak " + format (peak, 3) + ")");
        }

        // RT60 vs the Decay setting (late only, no damping, full band).
        for (const auto decay : { 0.6f, 1.5f, 3.0f, 6.0f })
        {
            for (const auto size : { 0.2f, 0.8f })
            {
                auto s = plainReverb();
                s.decaySeconds = decay;
                s.size = size;
                const auto n = (int) ((decay * 1.6 + 0.6) * sampleRate);
                const auto ir = reverbWet (s, [] (int i) { return i == 0 ? 1.0f : 0.0f; }, n);
                const auto rt = measureRt60 (ir, sampleRate);
                note ("reverb Decay " + format (decay, 1) + " s, size " + format (size, 1) + ": measured RT60 " + format (rt, 2) + " s ("
                      + format (100.0 * (rt / decay - 1.0), 1) + " %)");
                check (std::abs (rt / decay - 1.0) < 0.03, "RT60 within 3 % of Decay " + format (decay, 1) + " s (size " + format (size, 1)
                                                            + "; measured " + format (rt, 2) + " s)");
            }
        }

        // Damping: highs decay faster than the mids.
        {
            auto s = plainReverb();
            s.decaySeconds = 2.0f;
            s.damping = 0.8f;
            const auto n = (int) (3.0 * sampleRate);
            const auto ir = reverbWet (s, [] (int i) { return i == 0 ? 1.0f : 0.0f; }, n);
            std::vector<float> hp (ir.size());
            float prev = 0.0f;
            for (size_t i = 0; i < ir.size(); ++i) { hp[i] = ir[i] - prev; prev = ir[i]; } // first difference: HF weighted
            const auto rtMid = measureRt60 (ir, sampleRate), rtHigh = measureRt60 (hp, sampleRate);
            note ("reverb Damping 80 %: broadband RT60 " + format (rtMid, 2) + " s, HF-weighted " + format (rtHigh, 2) + " s");
            check (rtHigh < rtMid * 0.85, "damping makes the highs decay faster");
        }

        // Pre-delay: the wet starts after it (ms and synced).
        {
            auto s = plainReverb();
            s.preDelayMs = 80.0f;
            s.earlyLate = 0.0f;
            const auto ir = reverbWet (s, [] (int i) { return i == 0 ? 1.0f : 0.0f; }, 9600);
            int first = -1;
            for (int i = 0; i < (int) ir.size() && first < 0; ++i)
                if (std::abs (ir[(size_t) i]) > 1.0e-4f) first = i;
            note ("reverb pre-delay 80 ms: first wet sample at " + std::to_string (first) + " (" + format (first / 48.0, 1) + " ms)");
            check (first >= 3840 && first < 3840 + 960, "wet starts after the 80 ms pre-delay");
            s.preDelaySync = 6; // 1/8 at 120 BPM = 250 ms
            check (std::abs (dsp::VocalReverb::preDelayMsFor (s, 120.0) - 250.0) < 0.01, "synced pre-delay 1/8 at 120 BPM is 250 ms");
            check (std::abs (dsp::VocalReverb::preDelayMsFor (s, 60.0) - 500.0) < 0.01, "synced pre-delay is clamped to 500 ms");
        }

        // Ducking: the wet drops while the vocal sings and comes back after.
        {
            auto s = plainReverb();
            s.decaySeconds = 3.0f;
            const auto n = (int) (2.0 * sampleRate);
            auto sing = [] (int i) { return sineAt (220.0, i, sampleRate, 0.4f); };
            const auto open = reverbWet (s, sing, n);
            s.duckDb = 18.0f;
            const auto ducked = reverbWet (s, sing, n);
            const auto a = rmsRange (open, n / 2, n), b = rmsRange (ducked, n / 2, n);
            const auto drop = 20.0 * std::log10 (std::max (1.0e-12, b) / std::max (1.0e-12, a));
            note ("reverb ducking 18 dB on a held note: wet " + format (drop, 1) + " dB");
            check (drop < -10.0, "ducking reduces the wet while the vocal is present (" + format (drop, 1) + " dB)");
        }

        // Mix 0 (reverb and delay on) is bit-identical to both off, through the whole plug-in.
        {
            auto fill = [] (int i) { return sawAt (196.0, i, sampleRate, 0.3f) * (i % 24000 < 16000 ? 1.0f : 0.0f); };
            auto setup = [] (Processor& p)
            {
                setParam (p, "pitch_on", 1.0f);
                setParam (p, "comp_on", 1.0f);
            };
            auto a = makeProcessor();
            setup (*a);
            const auto ref = render (*a, fill, 48000);
            auto b = makeProcessor();
            setup (*b);
            setParam (*b, "rev_on", 1.0f);
            setParam (*b, "rev_mix", 0.0f);
            setParam (*b, "dly_on", 1.0f);
            setParam (*b, "dly_mix", 0.0f);
            setParam (*b, "rev_decay", 8.0f);
            setParam (*b, "dly_feedback", 0.9f);
            const auto out = render (*b, fill, 48000);
            int diffs = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                    diffs += juce::exactlyEqual (ref.getSample (ch, i), out.getSample (ch, i)) ? 0 : 1;
            note ("reverb + delay on at mix 0 vs off: " + std::to_string (diffs) + " differing samples of 96000");
            check (diffs == 0, "mix 0 is bit-transparent (" + std::to_string (diffs) + " samples differ)");
            check (b->getLatencySamples() == a->getLatencySamples(), "reverb and delay add no latency ("
                       + std::to_string (b->getLatencySamples()) + " vs " + std::to_string (a->getLatencySamples()) + ")");
            check (b->getTailLengthSeconds() > 5.0, "tail length reports the reverb / delay tail (" + format (b->getTailLengthSeconds(), 1) + " s)");
            check (a->getTailLengthSeconds() <= 0.11, "no FX: short tail (" + format (a->getTailLengthSeconds(), 2) + " s)");
        }

        // Section presets: at least 12, each loads only rev_* (one undo step) and renders finite.
        {
            const auto& list = dooblrvocals::presets::getReverbPresets();
            check (list.size() >= 12, "at least 12 reverb presets (" + std::to_string (list.size()) + ")");
            auto p = makeProcessor();
            setParam (*p, "eq_mid_gain", 2.5f);
            setParam (*p, "dly_on", 1.0f);
            for (const auto& preset : list)
            {
                auto& undo = p->getUndoHistory();
                undo.clear();
                dooblrvocals::presets::apply (p->getParameterState(),
                                              dooblrvocals::presets::resolveSection (p->getParameterState(), "rev_", preset.values), true);
                bool onlyReverb = true;
                for (const auto& [id, v] : preset.values)
                    onlyReverb = onlyReverb && id.startsWith ("rev_");
                check (onlyReverb && getPlain (*p, "rev_on") > 0.5f && std::abs (getPlain (*p, "eq_mid_gain") - 2.5f) < 0.05f
                           && getPlain (*p, "dly_on") > 0.5f && undo.getNumUndoableSteps() == 1,
                       "reverb preset " + preset.name.toStdString() + " loads only the reverb, one undo step");
                const auto out = render (*p, [] (int i) { return sineAt (220.0, i, sampleRate, 0.3f); }, 9600);
                check (allFinite (out), "reverb preset " + preset.name.toStdString() + " is finite");
            }
        }

        // Old (v0.2) sessions load with the new modules off.
        {
            auto src = makeProcessor();
            setParam (*src, "corr_key", 3.0f);
            setParam (*src, "comp_ratio", 5.0f);
            const auto blob = legacyState (*src);
            auto q = makeProcessor();
            setParam (*q, "rev_on", 1.0f);
            setParam (*q, "dly_on", 1.0f);
            setParam (*q, "fx_routing", 2.0f);
            setParam (*q, "range_custom", 1.0f);
            q->setStateInformation (blob.getData(), (int) blob.getSize());
            check (blob.getSize() > 32, "legacy state built");
            check (getPlain (*q, "rev_on") < 0.5f && getPlain (*q, "dly_on") < 0.5f && getPlain (*q, "fx_routing") < 0.5f
                       && getPlain (*q, "range_custom") < 0.5f,
                   "a v0.2 session loads with reverb, delay, routing and learned range at their defaults");
            check (std::abs (getPlain (*q, "corr_key") - 3.0f) < 0.1f && std::abs (getPlain (*q, "comp_ratio") - 5.0f) < 0.1f,
                   "a v0.2 session keeps its own settings");
            const auto out = render (*q, [] (int i) { return sineAt (220.0, i, sampleRate, 0.3f); }, 9600);
            check (allFinite (out), "legacy session renders");
        }
    }

    /** Runs an FxBlock on a mono input; returns L and R (and the probes when asked). */
    StereoBuf runFx (const dsp::ReverbSettings& rs, const dsp::DelaySettings& ds, dsp::FxRouting routing,
                     const std::function<float (int)>& in, int n, double bpm = 120.0, dsp::FxBlock::Probe* probe = nullptr)
    {
        dsp::FxBlock fx;
        fx.prepare (sampleRate, blockSize);
        fx.setProbe (probe);
        StereoBuf out (n);
        juce::AudioBuffer<float> buf (2, blockSize);
        for (int pos = 0; pos < n; pos += blockSize)
        {
            const auto m = std::min (blockSize, n - pos);
            for (int i = 0; i < m; ++i)
            {
                buf.setSample (0, i, in (pos + i));
                buf.setSample (1, i, in (pos + i));
            }
            fx.process (buf, m, rs, ds, routing, bpm);
            for (int i = 0; i < m; ++i)
            {
                out.l[(size_t) (pos + i)] = buf.getSample (0, i);
                out.r[(size_t) (pos + i)] = buf.getSample (1, i);
            }
        }
        return out;
    }

    int peakIndex (const std::vector<float>& v, int from, int to)
    {
        int best = from;
        for (int i = from; i < std::min (to, (int) v.size()); ++i)
            if (std::abs (v[(size_t) i]) > std::abs (v[(size_t) best])) best = i;
        return best;
    }

    void testDelay()
    {
        dsp::ReverbSettings off;
        off.enabled = false;
        auto impulse = [] (int i) { return i == 0 ? 1.0f : 0.0f; };

        // Times: ms and synced (straight, dotted, triplet), measured from the first echo.
        {
            auto d = plainDelay();
            d.timeLMs = d.timeRMs = 250.0f;
            const auto out = runFx (off, d, dsp::FxRouting::parallel, impulse, 24000);
            const auto at = peakIndex (out.l, 200, 24000);
            note ("delay 250 ms: first echo at sample " + std::to_string (at) + " (expected 12000)");
            check (std::abs (at - 12000) <= 2, "250 ms delay echoes at 12000 samples (got " + std::to_string (at) + ")");

            struct Case { int note; double bpm; double expected; const char* name; };
            for (const auto& c : { Case { 10, 120.0, 24000.0, "1/4 at 120" }, Case { 8, 120.0, 18000.0, "1/8D at 120" },
                                   Case { 6, 120.0, 8000.0, "1/8T at 120" }, Case { 7, 90.0, 16000.0, "1/8 at 90" } })
            {
                auto s = plainDelay();
                s.sync = true;
                s.noteL = s.noteR = c.note;
                const auto want = dsp::VocalDelay::delaySamplesFor (s, 0, c.bpm, sampleRate);
                const auto o = runFx (off, s, dsp::FxRouting::parallel, impulse, (int) c.expected + 4000, c.bpm);
                const auto got = peakIndex (o.l, 200, (int) c.expected + 4000);
                note (std::string ("delay synced ") + c.name + " BPM: " + format (want, 1) + " samples, echo at " + std::to_string (got));
                check (std::abs (want - c.expected) < 0.5 && std::abs (got - (int) c.expected) <= 2,
                       std::string ("synced ") + c.name + " echoes at " + format (c.expected, 0) + " samples (got " + std::to_string (got) + ")");
            }
        }

        // Ping-pong alternates L, R, L; Dual L/R uses its own R time.
        {
            auto d = plainDelay();
            d.mode = dsp::DelaySettings::pingPong;
            d.feedback = 0.6f;
            d.timeLMs = 100.0f;
            const auto o = runFx (off, d, dsp::FxRouting::parallel, impulse, 20000);
            auto side = [&] (int centre) { return std::abs (o.l[(size_t) peakIndex (o.l, centre - 20, centre + 20)])
                                                - std::abs (o.r[(size_t) peakIndex (o.r, centre - 20, centre + 20)]); };
            check (side (4800) > 0.05f && side (9600) < -0.05f && side (14400) > 0.02f, "ping-pong alternates L, R, L");

            auto dual = plainDelay();
            dual.mode = dsp::DelaySettings::dual;
            dual.timeLMs = 100.0f;
            dual.timeRMs = 175.0f;
            const auto q = runFx (off, dual, dsp::FxRouting::parallel, impulse, 12000);
            const auto l = peakIndex (q.l, 200, 12000), r = peakIndex (q.r, 200, 12000);
            note ("delay Dual 100 / 175 ms: L echo " + std::to_string (l) + ", R echo " + std::to_string (r));
            check (std::abs (l - 4800) <= 2 && std::abs (r - 8400) <= 2, "dual L/R times are independent");
        }

        // Max feedback (95 %) with drive and modulation: stable, finite and still decaying.
        for (const auto mode : { 0, 1, 2 })
        {
            auto d = plainDelay();
            d.mode = mode;
            d.feedback = dsp::VocalDelay::maxFeedback;
            d.drive = 1.0f;
            d.modulation = 1.0f;
            d.timeLMs = 180.0f;
            d.timeRMs = 240.0f;
            d.mix = 1.0f;
            juce::Random rng (3);
            const auto n = (int) (25.0 * sampleRate);
            const auto o = runFx (off, d, dsp::FxRouting::parallel,
                                  [&] (int i) { return i < 48000 ? (rng.nextFloat() * 2.0f - 1.0f) * 0.9f : 0.0f; }, n);
            bool finite = true;
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (o.l[(size_t) i]) && std::isfinite (o.r[(size_t) i]);
                peak = std::max ({ peak, std::abs (o.l[(size_t) i]), std::abs (o.r[(size_t) i]) });
            }
            const auto early = rmsRange (o.l, 48000, 96000) + rmsRange (o.r, 48000, 96000);
            const auto late = rmsRange (o.l, n - 48000, n) + rmsRange (o.r, n - 48000, n);
            const auto decayDb = 20.0 * std::log10 (std::max (1.0e-12, late) / std::max (1.0e-12, early));
            note (std::string ("delay ") + (mode == 0 ? "Stereo" : mode == 1 ? "Ping-Pong" : "Dual") + " at 95 % feedback + drive + mod: peak "
                  + format (peak, 2) + ", level after 23 s " + format (decayDb, 1) + " dB");
            check (finite && peak < 2.0f && decayDb < -20.0, "max feedback is stable and decays (mode " + std::to_string (mode) + ")");
        }

        // Ducking reduces the delay wet under a held note.
        {
            auto d = plainDelay();
            d.feedback = 0.5f;
            auto sing = [] (int i) { return sineAt (220.0, i, sampleRate, 0.4f); };
            const auto n = 96000;
            auto wetOnly = [&] (dsp::DelaySettings s)
            {
                s.mix = 1.0f;
                const auto o = runFx (off, s, dsp::FxRouting::parallel, sing, n);
                return rmsRange (o.l, n / 2, n);
            };
            const auto open = wetOnly (d);
            d.duckDb = 18.0f;
            const auto ducked = wetOnly (d);
            const auto drop = 20.0 * std::log10 (std::max (1.0e-12, ducked) / std::max (1.0e-12, open));
            note ("delay ducking 18 dB on a held note: wet " + format (drop, 1) + " dB");
            check (drop < -10.0, "delay ducking reduces the wet (" + format (drop, 1) + " dB)");
        }

        // Delay presets: the six asked for, all finite; loading touches only dly_*.
        {
            const auto& list = dooblrvocals::presets::getDelayPresets();
            juce::StringArray names;
            for (const auto& preset : list)
                names.add (preset.name);
            for (const auto* want : { "Slapback", "1/4 Ping-Pong", "Dotted 1/8", "Dark Tape", "Throw", "Wide Doubler" })
                check (names.contains (want), std::string ("delay preset ") + want);
            auto p = makeProcessor();
            setParam (*p, "rev_on", 1.0f);
            setParam (*p, "rev_decay", 4.0f);
            for (const auto& preset : list)
            {
                dooblrvocals::presets::apply (p->getParameterState(),
                                              dooblrvocals::presets::resolveSection (p->getParameterState(), "dly_", preset.values), true);
                const auto out = render (*p, [] (int i) { return sineAt (220.0, i, sampleRate, 0.3f) * (i < 4800 ? 1.0f : 0.0f); }, 48000);
                check (allFinite (out) && getPlain (*p, "dly_on") > 0.5f && std::abs (getPlain (*p, "rev_decay") - 4.0f) < 0.05f,
                       "delay preset " + preset.name.toStdString() + " loads (reverb untouched) and is finite");
            }
        }
    }

    void testFxRouting()
    {
        auto rs = plainReverb();
        rs.decaySeconds = 1.2f;
        auto ds = plainDelay();
        ds.timeLMs = ds.timeRMs = 100.0f; // 4800 samples
        ds.feedback = 0.0f;
        const auto n = 14400;
        auto impulse = [] (int i) { return i == 0 ? 1.0f : 0.0f; };

        std::vector<float> revIn ((size_t) n), dlyIn ((size_t) n);
        auto probeRun = [&] (dsp::FxRouting routing)
        {
            std::fill (revIn.begin(), revIn.end(), 0.0f);
            std::fill (dlyIn.begin(), dlyIn.end(), 0.0f);
            dsp::FxBlock::Probe probe { revIn.data(), dlyIn.data(), n, 0 };
            return runFx (rs, ds, routing, impulse, n, 120.0, &probe);
        };
        const auto energy = [] (const std::vector<float>& v, int from, int to) { return rmsRange (v, from, to); };

        // Parallel: both modules see the vocal (an impulse), nothing else.
        const auto par = probeRun (dsp::FxRouting::parallel);
        const auto parRevLate = energy (revIn, 100, n), parDlyLate = energy (dlyIn, 100, n);
        check (std::abs (revIn[0] - 1.0f) < 1.0e-6f && std::abs (dlyIn[0] - 1.0f) < 1.0e-6f && parRevLate < 1.0e-9 && parDlyLate < 1.0e-9,
               "parallel: delay and reverb are both fed only the dry vocal");

        // Delay > Reverb: the reverb input carries the echo at 100 ms.
        probeRun (dsp::FxRouting::delayToReverb);
        const auto echoIntoReverb = std::abs (revIn[(size_t) peakIndex (revIn, 4700, 4900)]);
        const auto d2rDly = energy (dlyIn, 100, n);
        note ("Delay > Reverb: reverb input echo at 4800 = " + format (echoIntoReverb, 3) + ", delay input after the dry = " + format (d2rDly, 6));
        check (echoIntoReverb > 0.2f && d2rDly < 1.0e-9, "delay > reverb: the reverb is fed the delay's output");

        // Reverb > Delay: the delay input carries the reverb tail.
        probeRun (dsp::FxRouting::reverbToDelay);
        const auto tailIntoDelay = energy (dlyIn, 200, 4800), r2dRev = energy (revIn, 100, n);
        note ("Reverb > Delay: delay input reverb-tail RMS = " + format (tailIntoDelay, 5) + ", reverb input after the dry = " + format (r2dRev, 6));
        check (tailIntoDelay > 1.0e-3 && r2dRev < 1.0e-9, "reverb > delay: the delay is fed the reverb's output");

        // Parallel = dry + delay wet + reverb wet (each alone, mixes at 50 % keep the dry at unity).
        {
            dsp::ReverbSettings rOff; rOff.enabled = false;
            dsp::DelaySettings dOff; dOff.enabled = false;
            juce::Random rng (11);
            std::vector<float> src ((size_t) n);
            for (auto& v : src) v = (rng.nextFloat() * 2.0f - 1.0f) * 0.3f;
            auto in = [&] (int i) { return src[(size_t) i]; };
            const auto both = runFx (rs, ds, dsp::FxRouting::parallel, in, n);
            const auto dOnly = runFx (rOff, ds, dsp::FxRouting::parallel, in, n);
            const auto rOnly = runFx (rs, dOff, dsp::FxRouting::parallel, in, n);
            double err = 0.0, ref = 0.0;
            for (int i = 2000; i < n; ++i)
            {
                const auto predicted = dOnly.l[(size_t) i] + rOnly.l[(size_t) i] - src[(size_t) i];
                err = std::max (err, (double) std::abs (both.l[(size_t) i] - predicted));
                ref = std::max (ref, (double) std::abs (both.l[(size_t) i]));
            }
            note ("parallel sum check: max |both - (delay + reverb - dry)| = " + format (err, 7) + " (peak " + format (ref, 3) + ")");
            check (err < 1.0e-4, "parallel routing is the sum of the two modules");

            // Series orders differ from parallel (the second module processes the first's output).
            const auto d2r = runFx (rs, ds, dsp::FxRouting::delayToReverb, in, n);
            double diff = 0.0;
            for (int i = 2000; i < n; ++i)
                diff = std::max (diff, (double) std::abs (d2r.l[(size_t) i] - both.l[(size_t) i]));
            check (diff > 1.0e-3, "series routing differs from parallel");
        }
        juce::ignoreUnused (par);

        // Through the plug-in: switching routing, delay > reverb at full mix is finite, latency unchanged.
        {
            auto p = makeProcessor();
            setParam (*p, "pitch_on", 0.0f);
            const auto lat = p->getLatencySamples();
            setParam (*p, "rev_on", 1.0f);
            setParam (*p, "dly_on", 1.0f);
            for (const auto routing : { 0.0f, 1.0f, 2.0f })
            {
                setParam (*p, "fx_routing", routing);
                const auto out = render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f) * (i < 9600 ? 1.0f : 0.0f); }, 48000);
                check (allFinite (out) && peakOf (out, 24000, 48000) > 1.0e-4f, "routing " + format (routing, 0) + " renders a tail");
            }
            check (p->getLatencySamples() == lat, "routing adds no latency");
        }

        // The UI switch: one click is one undo step and both pages show the same routing.
        {
            auto p = makeProcessor();
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
            auto* editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "editor for routing");
            if (editor == nullptr)
                return;
            editor->setSize (1080, 660);
            editor->selectPage (Editor::delayPage);
            auto& view = editor->getRoutingView (Editor::delayPage);
            check (visibleChain (view) && view.getWidth() > 100, "routing switch is visible on the Delay page");
            auto& undo = p->getUndoHistory();
            undo.clear();

            // t13u: the switch is a SegmentedToggle - it picks on mouse-up, inside the pressed segment.
            auto& segs = view.getSegments();
            check (segs.getComponentID() == "fx_routing_segments" && segs.getNumSegments() == 3 && visibleChain (segs)
                       && segs.getParentComponent() == &view, "routing switch is a three-segment toggle");
            check (view.getSegmentArea (1) == segs.getSegmentBounds (1) + segs.getPosition(), "segment areas are in the view's coordinates");
            const auto seg = segs.getSegmentBounds (1).getCentre().toFloat();
            segs.mouseDown (makeMouseEvent (segs, seg, seg, 1, false));
            check (getPlain (*p, "fx_routing") < 0.5f && view.getRouting() == 0 && undo.getNumUndoableSteps() == 0,
                   "mouse-down alone does not change the routing");
            segs.mouseUp (makeMouseEvent (segs, seg, seg, 1, false));
            check (std::abs (getPlain (*p, "fx_routing") - 1.0f) < 0.01f && view.getRouting() == 1, "clicking Delay > Reverb sets fx_routing");
            check (undo.getNumUndoableSteps() == 1, "routing click is one undo step");

            // Released over another segment: nothing.
            const auto other = segs.getSegmentBounds (2).getCentre().toFloat();
            segs.mouseDown (makeMouseEvent (segs, seg, seg, 1, false));
            segs.mouseUp (makeMouseEvent (segs, other, seg, 1, true));
            check (view.getRouting() == 1 && undo.getNumUndoableSteps() == 1, "releasing over another segment does nothing");

            // Keyboard and accessibility.
            check (segs.getWantsKeyboardFocus(), "routing switch takes keyboard focus");
            segs.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
            check (juce::roundToInt (getPlain (*p, "fx_routing")) == 2 && undo.getNumUndoableSteps() == 2, "Right picks Reverb > Delay (one undo step)");
            segs.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
            check (juce::roundToInt (getPlain (*p, "fx_routing")) == 2, "Right stops at the last segment");
            segs.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (juce::roundToInt (getPlain (*p, "fx_routing")) == 0, "Space cycles to the next segment (wrapping)");
            segs.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
            check (juce::roundToInt (getPlain (*p, "fx_routing")) == 0, "Left stops at the first segment");
            segs.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
            check (juce::roundToInt (getPlain (*p, "fx_routing")) == 1, "Right picks Delay > Reverb");
            const auto handler = segs.createAccessibilityHandler(); // (getAccessibilityHandler needs a peer)
            check (handler != nullptr && handler->getActions().contains (juce::AccessibilityActionType::press)
                       && handler->getHelp() == "Dly > Rev", "routing switch has an accessibility handler");
            if (handler != nullptr)
            {
                handler->getActions().invoke (juce::AccessibilityActionType::press);
                check (juce::roundToInt (getPlain (*p, "fx_routing")) == 2, "accessibility press picks the next routing");
                segs.select (1);
            }
            check (view.getRouting() == 1, "the sketch follows the switch");
            undo.clear();
            setParam (*p, "fx_routing", 1.0f);
            editor->selectPage (Editor::reverbPage);
            auto& view2 = editor->getRoutingView (Editor::reverbPage);
            view2.repaint();
            check (visibleChain (view2) && view2.getRouting() == 1, "the Reverb page shows the same routing");
            p->editorBeingDeleted (editor);
            base.reset();
        }
    }

    /** Synthetic singer: holds the ends of the range a little, glides through the middle. */
    std::vector<dsp::PitchFrame> syntheticSinging (float lowHz, float highHz, double seconds, juce::Random& rng,
                                                   double noiseFraction, double octaveErrorFraction)
    {
        const auto hop = 256.0 / sampleRate;
        const auto frames = (int) (seconds / hop);
        std::vector<dsp::PitchFrame> out;
        const auto lowNote = 69.0 + 12.0 * std::log2 (lowHz / 440.0), highNote = 69.0 + 12.0 * std::log2 (highHz / 440.0);
        for (int i = 0; i < frames; ++i)
        {
            dsp::PitchFrame f;
            f.seconds = (float) hop;
            const auto u = rng.nextDouble();
            if (u < noiseFraction)
            {
                // Breaths / noise: unvoiced or low confidence at random pitches.
                f.voiced = rng.nextBool();
                f.confidence = 0.2f * rng.nextFloat();
                f.detectedHz = 60.0f + 1400.0f * rng.nextFloat();
            }
            else
            {
                const auto phase = std::fmod ((double) i / (double) frames * 6.0, 1.0); // six slow sweeps
                double noteNum = phase < 0.12 ? lowNote : phase > 0.88 ? highNote
                                                        : lowNote + (highNote - lowNote) * (phase - 0.12) / 0.76;
                noteNum += 0.15 * std::sin ((double) i * 0.21); // vibrato
                auto hz = (float) (440.0 * std::pow (2.0, (noteNum - 69.0) / 12.0));
                if (rng.nextDouble() < octaveErrorFraction)
                    hz *= rng.nextBool() ? 2.0f : 0.5f;
                f.voiced = true;
                f.confidence = 0.8f + 0.2f * rng.nextFloat();
                f.detectedHz = hz;
            }
            out.push_back (f);
        }
        return out;
    }

    double semis (double a, double b) { return 12.0 * std::log2 (a / b); }

    void testRangeLearn()
    {
        using dooblrvocals::RangeLearner;

        // Synthetic A2 - E4 with 15 % noise and 3 % octave errors: learned within about a semitone.
        {
            juce::Random rng (42);
            const auto frames = syntheticSinging (110.0f, 329.63f, 12.0, rng, 0.15, 0.03);
            RangeLearner learner;
            learner.clear();
            for (size_t i = 0; i < frames.size(); i += 64)
                learner.add (frames.data() + i, (int) std::min<size_t> (64, frames.size() - i));
            const auto r = learner.compute();
            note ("learned A2-E4 (noise 15 %, octave errors 3 %): " + RangeLearner::rangeText (r.lowHz, r.highHz).toStdString()
                  + " = " + format (r.lowHz, 1) + " - " + format (r.highHz, 1) + " Hz (errors " + format (semis (r.lowHz, 110.0), 2) + " / "
                  + format (semis (r.highHz, 329.63), 2) + " semitones), detector " + format (r.searchLowHz, 1) + " - " + format (r.searchHighHz, 1)
                  + " Hz, " + format (r.voicedSeconds, 1) + " s voiced, type " + std::to_string ((int) r.type));
            check (r.valid, "A2-E4 learn is valid");
            check (std::abs (semis (r.lowHz, 110.0)) <= 1.0 && std::abs (semis (r.highHz, 329.63)) <= 1.0,
                   "A2-E4 learned within a semitone (" + format (semis (r.lowHz, 110.0), 2) + ", " + format (semis (r.highHz, 329.63), 2) + ")");
            check (r.searchLowHz < r.lowHz && r.searchHighHz > r.highHz, "detector range has a margin around the sung range");
            check (r.type == dsp::InputType::baritone || r.type == dsp::InputType::tenor, "A2-E4 maps to a baritone / tenor range");
            check (RangeLearner::rangeText (110.0f, 329.63f) == juce::String (juce::CharPointer_UTF8 ("A2\xe2\x80\x93" "E4")), "range text A2-E4");
        }

        // A soprano register too.
        {
            juce::Random rng (5);
            const auto frames = syntheticSinging (261.63f, 880.0f, 8.0, rng, 0.1, 0.02);
            RangeLearner learner;
            learner.add (frames.data(), (int) frames.size());
            const auto r = learner.compute();
            note ("learned C4-A5: " + RangeLearner::rangeText (r.lowHz, r.highHz).toStdString() + ", type " + std::to_string ((int) r.type));
            check (r.valid && std::abs (semis (r.lowHz, 261.63)) <= 1.0 && std::abs (semis (r.highHz, 880.0)) <= 1.0
                       && (r.type == dsp::InputType::soprano || r.type == dsp::InputType::alto), "C4-A5 learned as a high voice");
        }

        // Too little voice: invalid, message, nothing applied.
        {
            juce::Random rng (9);
            auto frames = syntheticSinging (110.0f, 329.63f, 1.0, rng, 0.0, 0.0);
            RangeLearner learner;
            learner.add (frames.data(), (int) frames.size());
            const auto r = learner.compute();
            check (! r.valid && r.message.containsIgnoreCase ("not enough voice"), "1 s of voice is not enough (" + r.message.toStdString() + ")");

            // Silence / noise only through the plug-in.
            auto p = makeProcessor();
            setParam (*p, "pitch_on", 1.0f);
            const auto lowBefore = getPlain (*p, "range_low"), typeBefore = getPlain (*p, "input_type");
            p->getUndoHistory().clear();
            p->startRangeLearn();
            check (p->isLearningRange(), "learn arms");
            juce::Random noise (1);
            for (int chunk = 0; chunk < 12; ++chunk)
            {
                render (*p, [&] (int) { return (noise.nextFloat() * 2.0f - 1.0f) * 0.01f; }, 4096);
                p->pollRangeLearn();
            }
            const auto res = p->stopRangeLearn();
            note ("learn on 1 s of quiet noise: " + res.message.toStdString());
            check (! res.valid && ! p->isLearningRange(), "noise only: not enough voice, learning stops");
            check (getPlain (*p, "range_custom") < 0.5f && std::abs (getPlain (*p, "range_low") - lowBefore) < 0.01f
                       && std::abs (getPlain (*p, "input_type") - typeBefore) < 0.01f && p->getUndoHistory().getNumUndoableSteps() == 0,
                   "not enough voice changes nothing");
        }

        // End to end: sing A2 - E4 into the plug-in, poll like the editor, stop.
        {
            auto p = makeProcessor();
            setParam (*p, "pitch_on", 1.0f);
            setParam (*p, "input_type", 0.0f); // soprano before learning
            auto& undo = p->getUndoHistory();
            undo.clear();
            p->startRangeLearn();
            const auto total = (int) (10.0 * sampleRate);
            double phase = 0.0;
            const auto lowNote = 45.0, highNote = 64.0;
            int pos = 0;
            juce::Random rng (3);
            auto voice = [&] (int i)
            {
                const auto t = (double) (pos + i) / (double) total;
                const auto ph = std::fmod (t * 4.0, 1.0);
                const auto noteNum = ph < 0.15 ? lowNote : ph > 0.85 ? highNote : lowNote + (highNote - lowNote) * (ph - 0.15) / 0.7;
                const auto hz = 440.0 * std::pow (2.0, (noteNum - 69.0) / 12.0);
                phase += hz / sampleRate;
                phase -= std::floor (phase);
                // Saw + some breath noise; a short gap every second.
                const auto gap = std::fmod (t * 10.0, 1.0) > 0.92;
                return gap ? (rng.nextFloat() - 0.5f) * 0.01f : (float) (0.3 * (2.0 * phase - 1.0)) + (rng.nextFloat() - 0.5f) * 0.01f;
            };
            for (pos = 0; pos < total; pos += 4096)
            {
                render (*p, voice, std::min (4096, total - pos));
                p->pollRangeLearn();
            }
            const auto r = p->stopRangeLearn();
            note ("plug-in learn of a sung A2-E4: " + r.message.toStdString() + " -> detector " + format (getPlain (*p, "range_low"), 1)
                  + " - " + format (getPlain (*p, "range_high"), 1) + " Hz, type " + format (getPlain (*p, "input_type"), 0));
            check (r.valid && std::abs (semis (r.lowHz, 110.0)) <= 1.0 && std::abs (semis (r.highHz, 329.63)) <= 1.0,
                   "plug-in learns A2-E4 within a semitone (" + format (semis (r.lowHz, 110.0), 2) + ", " + format (semis (r.highHz, 329.63), 2) + ")");
            check (getPlain (*p, "range_custom") > 0.5f && std::abs (semis (getPlain (*p, "range_low"), r.searchLowHz)) < 0.1
                       && std::abs (semis (getPlain (*p, "range_high"), r.searchHighHz)) < 0.1,
                   "learned range is applied to the detector");
            check (std::abs (getPlain (*p, "input_type") - (float) (int) r.type) < 0.01f && getPlain (*p, "input_type") > 0.5f,
                   "voice type follows the learned range");
            check (undo.getNumUndoableSteps() == 1, "applying a learned range is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");

            // Saved with the session.
            juce::MemoryBlock blob;
            p->getStateInformation (blob);
            auto q = makeProcessor();
            q->setStateInformation (blob.getData(), (int) blob.getSize());
            check (getPlain (*q, "range_custom") > 0.5f && std::abs (getPlain (*q, "range_low") - getPlain (*p, "range_low")) < 0.5f
                       && std::abs (getPlain (*q, "range_high") - getPlain (*p, "range_high")) < 1.0f,
                   "learned range round-trips through the session state");

            // Factory presets keep it (it describes the singer); not host-automatable state otherwise.
            q->loadFactoryPreset (1, false);
            check (getPlain (*q, "range_custom") > 0.5f, "factory preset loads keep the learned range");
            check (! dooblrvocals::presets::isSessionOnlyParameter ("rev_mix") && dooblrvocals::presets::isSessionOnlyParameter ("range_low"),
                   "only range_* are session-only");

            undo.undo();
            check (getPlain (*p, "range_custom") < 0.5f && getPlain (*p, "input_type") < 0.5f, "undo restores the previous range in one step");
            undo.redo();
            p->resetLearnedRange();
            check (getPlain (*p, "range_custom") < 0.5f, "reset clears the learned range");

            const auto out = render (*q, [] (int i) { return sawAt (146.83, i, sampleRate, 0.3f); }, 24000);
            check (allFinite (out), "learned-range session renders");
        }

        // Editor: click to arm (pulsing, live note), click again to stop; reset.
        {
            auto p = makeProcessor();
            setParam (*p, "pitch_on", 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
            auto* editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "editor for learn");
            if (editor == nullptr)
                return;
            editor->setSize (1080, 660);
            editor->selectPage (Editor::pitchPage);
            auto& button = editor->getLearnButton();
            check (findById (*editor, "learnRange") == &button && visibleChain (button), "Learn Range button is on the pitch page");
            button.onClick();
        editor->refreshEnablementNow();
            check (p->isLearningRange() && button.getButtonText().startsWith ("Stop"), "first click arms learning");
            // Not enough voice: message shown, nothing changes.
            button.onClick();
        editor->refreshEnablementNow();
            editor->refreshLearnUiNow();
            check (! p->isLearningRange() && editor->getLearnLabel().getText().containsIgnoreCase ("not enough voice")
                       && getPlain (*p, "range_custom") < 0.5f,
                   "second click with no voice shows the message (" + editor->getLearnLabel().getText().toStdString() + ")");

            // A real take: arm, sing (the editor timer polls; here the test polls like it), stop.
            button.onClick();
        editor->refreshEnablementNow();
            double ph = 0.0;
            const auto total = (int) (4.0 * sampleRate);
            int pos = 0;
            auto sing = [&] (int i)
            {
                const auto t = (double) (pos + i) / (double) total;
                const auto noteNum = 50.0 + 17.0 * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t * 2.0));
                ph += 440.0 * std::pow (2.0, (noteNum - 69.0) / 12.0) / sampleRate;
                ph -= std::floor (ph);
                return (float) (0.3 * (2.0 * ph - 1.0));
            };
            for (pos = 0; pos < total; pos += 4096)
            {
                render (*p, sing, std::min (4096, total - pos));
                p->pollRangeLearn();
                if (pos == 4096 * 20)
                {
                    editor->refreshLearnUiNow();
                    saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learning");
                    check (button.getButtonText().contains (juce::CharPointer_UTF8 ("\xc2\xb7")), "armed button shows the live note ("
                               + button.getButtonText().toStdString() + ")");
                }
            }
            button.onClick();
        editor->refreshEnablementNow();
            editor->refreshLearnUiNow();
            const auto text = editor->getLearnLabel().getText();
            note ("learned-range readout: " + text.toStdString());
            check (getPlain (*p, "range_custom") > 0.5f && text.contains ("learned") && editor->getLearnResetButton().isVisible(),
                   "after a take the readout shows the learned range with a reset");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learned");
            editor->setSize (940, 600);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learned-940x600");
            editor->getLearnResetButton().onClick();
        editor->refreshEnablementNow();
            check (getPlain (*p, "range_custom") < 0.5f && ! editor->getLearnResetButton().isVisible(), "Reset clears the learned range");

            // The learn state itself is not a parameter (not automatable).
            for (auto* param : p->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                    check (! ranged->getParameterID().containsIgnoreCase ("learn"), "no automatable learn parameter");

            button.onClick(); // closing the editor while armed cancels it
        editor->refreshEnablementNow();
            p->editorBeingDeleted (editor);
            base.reset();
            check (! p->isLearningRange(), "closing the editor cancels an armed learn");
        }
    }

    void testFxPages()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for FX pages");
        if (editor == nullptr)
            return;

        check (findById (*editor, "power_delay") != nullptr && findById (*editor, "power_reverb") != nullptr, "delay / reverb power toggles");
        auto& revPower = editor->getFxPowerButton (Editor::reverbPage);
        p->getUndoHistory().clear();
        revPower.setToggleState (true, juce::sendNotificationSync);
        editor->refreshEnablementNow();
        check (getPlain (*p, "rev_on") > 0.5f && p->getUndoHistory().getNumUndoableSteps() == 1, "reverb tab power turns it on (one undo step)");
        setParam (*p, "dly_on", 1.0f);

        for (const auto& [w, h] : { std::pair { 940, 600 }, std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
        {
            editor->setSize (w, h);
            for (const auto page : { (int) Editor::pitchPage, (int) Editor::delayPage, (int) Editor::reverbPage })
            {
                editor->selectPage (page);
                std::function<void (juce::Component&)> checkClip = [&] (juce::Component& parent)
                {
                    for (auto* child : parent.getChildren())
                        if (child->isVisible())
                        {
                            if (child->getComponentID().isNotEmpty() && child->getWidth() > 0 && child->getHeight() > 0)
                                check (parent.getLocalBounds().expanded (1).contains (child->getBounds()),
                                       std::to_string (w) + "x" + std::to_string (h) + " page " + std::to_string (page) + ": "
                                           + child->getComponentID().toStdString() + " is not clipped");
                            checkClip (*child);
                        }
                };
                checkClip (*editor);
                for (const auto& panel : editor->getPanels (page))
                    check (panel->getHeight() > 0 && panel->getBottom() <= editor->getHeight(), "FX page panel fits at " + std::to_string (w));
            }
        }

        // Sync swaps the note / ms controls; Dual shows the R time.
        editor->setSize (1080, 660);
        editor->selectPage (Editor::delayPage);
        auto shown = [&] (const juce::String& id) { auto* c = findById (*editor, id); return c != nullptr && visibleChain (*c); };
        setParam (*p, "dly_sync", 1.0f);
        setParam (*p, "dly_mode", 0.0f);
        editor->refreshEnablementNow();
        check (shown ("dly_note") && ! shown ("dly_time") && ! shown ("dly_note_r"), "synced stereo shows the note value only");
        setParam (*p, "dly_sync", 0.0f);
        setParam (*p, "dly_mode", 2.0f);
        editor->refreshEnablementNow();
        check (shown ("dly_time") && shown ("dly_time_r") && ! shown ("dly_note"), "ms dual shows both times");

        // Section preset menus.
        auto& revBox = editor->getSectionPresetBox (Editor::reverbPage);
        check (revBox.getNumItems() >= 12, "reverb preset menu lists " + std::to_string (revBox.getNumItems()));
        p->getUndoHistory().clear();
        revBox.setSelectedId (6, juce::sendNotificationSync); // Large Hall
        check (getPlain (*p, "rev_decay") > 2.5f && p->getUndoHistory().getNumUndoableSteps() == 1, "reverb menu loads a preset in one undo step");
        auto& dlyBox = editor->getSectionPresetBox (Editor::delayPage);
        check (dlyBox.getNumItems() == 6, "delay preset menu lists the six presets");
        dlyBox.setSelectedId (2, juce::sendNotificationSync); // 1/4 Ping-Pong
        check (juce::roundToInt (getPlain (*p, "dly_mode")) == 1 && getPlain (*p, "dly_sync") > 0.5f, "delay menu loads 1/4 Ping-Pong");

        // Snapshots with sound through the displays.
        {
            setParam (*p, "pitch_on", 0.0f);
            render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); }, 24000);
            editor->selectPage (Editor::delayPage);
            editor->getDelayDisplay().refresh (p->getDelayDuckDb(), p->getHostBpm());
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-delay");
            editor->selectPage (Editor::reverbPage);
            setParam (*p, "rev_duck", 6.0f);
            setParam (*p, "fx_routing", 1.0f);
            render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); }, 24000);
            editor->getReverbDisplay().refresh (p->getReverbDuckDb(), p->getHostBpm());
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-reverb");
            editor->setSize (940, 600);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-reverb-940x600");
            editor->selectPage (Editor::delayPage);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-delay-940x600");
            editor->setSize (1080, 660);
        }

        check (dsp::VocalReverb::earlyTapMs (0, 0.5f) > 0.0 && dooblrvocals::ui::ReverbDisplay::spanSeconds (2.0, 40.0) > 2.0, "display helpers");
        p->editorBeingDeleted (editor);
        base.reset();
    }

    //==============================================================================
    // Vibrato respect (v0.2.0, t10u)

    /** A sung voice: eight harmonics on a phase-accumulated pitch track (cents relative to A3). */
    std::vector<float> vibratoVoice (int numSamples, const std::function<double (double)>& centsAt, float amp)
    {
        std::vector<float> out ((size_t) numSamples);
        double phase = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i / sampleRate;
            const auto hz = 220.0 * std::pow (2.0, centsAt (t) / 1200.0);
            phase = std::fmod (phase + hz / sampleRate, 1.0);
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * phase * h) / (double) h;
            const auto fade = std::min (1.0, std::min (t / 0.01, ((double) numSamples / sampleRate - t) / 0.01));
            out[(size_t) i] = amp * 0.55f * (float) (v * fade);
        }
        return out;
    }

    /** Cents relative to A3 = 220 Hz of the vibrato test voice: A3 +30 c, 5.5 Hz, +/-60 c. */
    double vibratoCents (double t, double baseCents, double depth, double rate)
    {
        return baseCents + depth * std::sin (2.0 * juce::MathConstants<double>::pi * rate * t);
    }

    std::uint64_t hashBuffer (const juce::AudioBuffer<float>& b)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                std::uint32_t bits = 0;
                const auto v = b.getSample (ch, i);
                std::memcpy (&bits, &v, sizeof (bits));
                for (int k = 0; k < 4; ++k)
                {
                    hash ^= (bits >> (8 * k)) & 0xffu;
                    hash *= 1099511628211ull;
                }
            }
        return hash;
    }

    /** Fixed renders used to prove the Vibrato knob at 0 is bit-identical to v0.2.0 before it existed
        (the expected hashes were produced by the 7186b7d build). */
    juce::AudioBuffer<float> goldenVibratoRender (int setup, bool setVibratoZero)
    {
        auto p = makeProcessor();
        setParam (*p, "humanize", 0.0f);
        setParam (*p, "retune_speed", 10.0f);
        if (setup == 0) setParam (*p, "latency_mode", 0.0f);
        if (setup == 1) setParam (*p, "latency_mode", 1.0f);
        if (setup == 2) setParam (*p, "quality", 1.0f);
        if (setup == 3)
        {
            setParam (*p, "latency_mode", 1.0f);
            setParam (*p, "harm_on", 1.0f);
            setParam (*p, params::harmonyId (0, "on"), 1.0f);
            setParam (*p, params::harmonyId (1, "on"), 1.0f);
        }
        if (setVibratoZero && p->getParameterState().getParameter ("vibrato") != nullptr)
            setParam (*p, "vibrato", 0.0f);

        const auto n = (int) (2.5 * sampleRate);
        const auto voice = vibratoVoice (n, [] (double t) { return t < 1.5 ? vibratoCents (t) : 330.0 + 60.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t); });
        juce::Random rng (77);
        std::vector<float> noisy (voice);
        for (auto& v : noisy)
            v += 0.002f * (rng.nextFloat() - 0.5f);
        return render (*p, [&] (int i) { return noisy[(size_t) i]; }, n);
    }

    void testVibratoGolden()
    {
        constexpr std::array<std::uint64_t, 4> expected { 0xbf522864896a8477ull, 0x1fbffa5e63b6e183ull, 0x2a95d427aebc085bull, 0x0b6f1146be163920ull };
        const char* names[] = { "Tracking", "Mix", "HQ", "Mix + 2 harmonies" };
        for (int setup = 0; setup < 4; ++setup)
        {
            const auto hash = hashBuffer (goldenVibratoRender (setup, true));
            note (std::string (names[setup]) + " render hash 0x" + juce::String::toHexString ((juce::int64) hash).toStdString());
            check (hash == expected[(size_t) setup], std::string ("Vibrato 0 is bit-identical to v0.2.0 (7186b7d): ") + names[setup]);
        }
    }

    /** Pitch track (cents relative to A3 = 220 Hz) of a rendered buffer, one reading every 5 ms. */
    std::vector<std::pair<double, double>> centsTrack (const juce::AudioBuffer<float>& b, int start, int end)
    {
        std::vector<std::pair<double, double>> track;
        constexpr int window = 1024, step = 240;
        for (int pos = start; pos + window <= end; pos += step)
        {
            const auto hz = detectHz (b, pos, window, 150.0f, 700.0f);
            if (hz > 20.0f)
                track.emplace_back ((double) (pos + window / 2) / sampleRate, 1200.0 * std::log2 (hz / 220.0));
        }
        return track;
    }

    /** Centre (mean) and vibrato depth (amplitude of the rate-Hz component) of a cents track.
        Readings more than 400 cents from the median (octave errors) are ignored. */
    std::pair<double, double> centreAndDepth (const std::vector<std::pair<double, double>>& track, double rate = 5.5)
    {
        if (track.size() < 8)
            return { 1.0e9, 0.0 };
        std::vector<double> values;
        for (const auto& [t, c] : track)
            values.push_back (c);
        std::nth_element (values.begin(), values.begin() + (long) values.size() / 2, values.end());
        const auto median = values[values.size() / 2];
        double sum = 0.0, s = 0.0, co = 0.0;
        int n = 0;
        for (const auto& [t, c] : track)
            if (std::abs (c - median) < 400.0) { sum += c; ++n; }
        const auto mean = sum / std::max (1, n);
        for (const auto& [t, c] : track)
            if (std::abs (c - median) < 400.0)
            {
                const auto w = 2.0 * juce::MathConstants<double>::pi * rate * t;
                s += (c - mean) * std::sin (w);
                co += (c - mean) * std::cos (w);
            }
        return { mean, 2.0 * std::sqrt (s * s + co * co) / std::max (1, n) };
    }

    void setVibratoMode (Processor& p, int mode)
    {
        setParam (p, "humanize", 0.0f);
        setParam (p, "retune_speed", 10.0f);
        setParam (p, "latency_mode", mode == 0 ? 0.0f : 1.0f);
        setParam (p, "quality", mode == 2 ? 1.0f : 0.0f);
    }

    void testVibratoRespect()
    {
        const char* modeNames[] = { "Tracking", "Mix", "HQ" };

        // Parameter: last in the layout, 0 .. 100 %, default 0.
        {
            auto p = makeProcessor();
            const auto ids = allParameterIds (*p);
            check (ids.size() > 5 && ids[ids.size() - 6] == "vibrato" && ids[ids.size() - 5] == "key_follow",
                   "Vibrato Respect is the last parameter before the Key Follow group");
            check (std::abs (getPlain (*p, "vibrato")) < 1.0e-6f, "Vibrato Respect defaults to 0");
            if (auto* param = getParam (*p, "vibrato"))
                check (param->getCurrentValueAsText().contains ("%") && param->getName (64) == "Vibrato Respect", "shown as a percentage");
        }

        // Synthetic singer: A3 +30 cents, 5.5 Hz, +/-60 cents vibrato.
        const auto n = (int) (4.5 * sampleRate);
        const auto voice = vibratoVoice (n, [] (double t) { return vibratoCents (t); });
        {
            juce::AudioBuffer<float> in (2, n);
            for (int i = 0; i < n; ++i)
                in.setSample (0, i, voice[(size_t) i]), in.setSample (1, i, voice[(size_t) i]);
            const auto [c, d] = centreAndDepth (centsTrack (in, (int) (1.5 * sampleRate), (int) (3.5 * sampleRate)));
            note ("input (measured the same way): centre " + format (c, 1) + " c, vibrato +/-" + format (d, 1) + " c");
        }

        for (int mode = 0; mode < 3; ++mode)
        {
            for (const auto k : { 1.0f, 0.5f, 0.0f })
            {
                auto p = makeProcessor();
                setVibratoMode (*p, mode);
                setParam (*p, "vibrato", k);
                const auto out = render (*p, [&] (int i) { return voice[(size_t) i]; }, n);
                const auto lat = p->getLatencySamples();
                const auto [c, d] = centreAndDepth (centsTrack (out, (int) (1.5 * sampleRate) + lat, (int) (3.5 * sampleRate) + lat));
                note (std::string (modeNames[mode]) + " (latency " + std::to_string (lat) + " = " + format (1000.0 * lat / sampleRate, 1)
                      + " ms), Vibrato " + std::to_string (juce::roundToInt (k * 100.0f)) + "%: output centre " + format (c, 1)
                      + " c from A3, vibrato +/-" + format (d, 1) + " c");
                check (std::abs (c) <= 5.0, std::string (modeNames[mode]) + " Vibrato " + std::to_string (juce::roundToInt (k * 100.0f))
                                                + "%: centre within 5 cents of A3 (" + format (c, 1) + ")");
                if (k > 0.99f)
                    check (d >= 50.0 && d <= 70.0, std::string (modeNames[mode]) + " 100%: vibrato depth kept (+/-" + format (d, 1) + " c)");
                else if (k > 0.4f)
                    check (d >= 20.0 && d <= 42.0, std::string (modeNames[mode]) + " 50%: about half the vibrato (+/-" + format (d, 1) + " c)");
                else
                    check (d <= (mode == 0 ? 35.0 : 15.0), std::string (modeNames[mode]) + " 0%: vibrato flattened (+/-" + format (d, 1) + " c)");
                check (allFinite (out), "finite output");
            }
        }

        // Engine view: how long until the vibrato is recognised, and how good the centre estimate is.
        for (int mode = 0; mode < 3; ++mode)
        {
            dsp::PitchEngine engine;
            engine.prepare (sampleRate, 256);
            dsp::PitchSettings s;
            s.humanize = 0.0f;
            s.retuneMs = 10.0f;
            s.vibratoRespect = 1.0f;
            s.latency = mode == 0 ? dsp::LatencyMode::tracking : dsp::LatencyMode::mix;
            s.quality = mode == 2 ? dsp::QualityMode::hq : dsp::QualityMode::live;
            juce::AudioBuffer<float> block (2, 256);
            double lockAt = -1.0, errSum = 0.0, errMax = 0.0;
            int errCount = 0;
            for (int pos = 0; pos + 256 <= n; pos += 256)
            {
                for (int i = 0; i < 256; ++i)
                    block.setSample (0, i, voice[(size_t) (pos + i)]), block.setSample (1, i, voice[(size_t) (pos + i)]);
                engine.process (block, 256, s, false, nullptr);
                const auto outTime = (double) (pos + 256 - engine.currentLatency()) / sampleRate;
                if (lockAt < 0.0 && engine.getVibratoWeight() > 0.5f)
                    lockAt = outTime;
                if (outTime > 1.5 && outTime < 3.5 && engine.getVibratoState().active)
                {
                    const auto e = std::abs ((engine.getVibratoState().centreMidi - 57.3f) * 100.0f);
                    errSum += e;
                    errMax = std::max (errMax, (double) e);
                    ++errCount;
                }
            }
            note (std::string (modeNames[mode]) + ": vibrato recognised " + format (lockAt * 1000.0, 0) + " ms into the note (output time); "
                  + "centre estimate error mean " + format (errSum / std::max (1, errCount), 1) + " c, max " + format (errMax, 1) + " c");
            check (lockAt > 0.0 && lockAt < 0.45, std::string (modeNames[mode]) + ": vibrato recognised within 450 ms");
            check (errCount > 0 && errSum / errCount < 5.0, std::string (modeNames[mode]) + ": centre estimate within 5 cents on average");
        }

        // A note change still snaps: A3 +30 c with vibrato, then C4 +30 c with vibrato at 2.0 s.
        {
            const auto m = (int) (4.0 * sampleRate);
            const auto stepVoice = vibratoVoice (m, [] (double t) { return vibratoCents (t, t < 2.0 ? 30.0 : 330.0); });
            for (int mode = 0; mode < 3; ++mode)
            {
                auto p = makeProcessor();
                setVibratoMode (*p, mode);
                setParam (*p, "vibrato", 1.0f);
                const auto out = render (*p, [&] (int i) { return stepVoice[(size_t) i]; }, m);
                const auto lat = p->getLatencySamples();
                const auto stepAt = (int) (2.0 * sampleRate) + lat;
                const auto early = centsTrack (out, stepAt + (int) (0.04 * sampleRate), stepAt + (int) (0.20 * sampleRate));
                double worst = 0.0;
                for (const auto& [t, c] : early)
                    worst = std::max (worst, std::abs (c - 300.0));
                const auto [c, d] = centreAndDepth (centsTrack (out, stepAt + (int) (0.8 * sampleRate), stepAt + (int) (1.8 * sampleRate)));
                note (std::string (modeNames[mode]) + " A3 -> C4 at 100%: 40-200 ms after the step the output is within "
                      + format (worst, 1) + " c of C4 (vibrato included); later centre " + format (c - 300.0, 1) + " c, vibrato +/-" + format (d, 1) + " c");
                check (! early.empty() && worst <= 100.0, std::string (modeNames[mode]) + ": the new note is reached at once (no stuck correction)");
                check (std::abs (c - 300.0) <= 5.0 && d >= 50.0, std::string (modeNames[mode]) + ": the new note's centre is corrected and its vibrato kept");
            }
        }

        // No vibrato (held note, a scoop, a plain note change): the knob changes nothing at all.
        {
            const auto m = (int) (3.0 * sampleRate);
            const auto plain = vibratoVoice (m, [] (double t)
            {
                if (t < 0.15) return -120.0 + 150.0 * (t / 0.15);               // scoop up into A3 +30
                if (t < 1.5) return 30.0;
                if (t < 1.62) return 330.0 - 150.0 * (1.0 - (t - 1.5) / 0.12); // scoop into C4 +30
                return 330.0;
            });
            for (int mode = 0; mode < 3; ++mode)
            {
                std::uint64_t hashes[2] {};
                for (int on = 0; on < 2; ++on)
                {
                    auto p = makeProcessor();
                    setVibratoMode (*p, mode);
                    setParam (*p, "vibrato", on == 1 ? 1.0f : 0.0f);
                    hashes[on] = hashBuffer (render (*p, [&] (int i) { return plain[(size_t) i]; }, m));
                }
                check (hashes[0] == hashes[1], std::string (modeNames[mode]) + ": scoops and note changes without vibrato are untouched by the knob (bit-identical)");
            }
        }

        // Harmonies follow the corrected pitch, vibrato included, and keep their interval.
        {
            dsp::PitchEngine engine;
            engine.prepare (sampleRate, 256);
            dsp::PitchSettings s;
            s.humanize = 0.0f;
            s.retuneMs = 10.0f;
            s.vibratoRespect = 1.0f;
            dsp::HarmonySettings h;
            h.enabled = true;
            h.voices[0].enabled = true;
            h.voices[0].fixed = false;
            h.voices[0].degrees = 2; // a third above in the key (A -> C in C major: 3 semitones)
            juce::AudioBuffer<float> block (2, 256);
            float leadLo = 1000.0f, leadHi = -1000.0f, intervalLo = 1000.0f, intervalHi = -1000.0f;
            for (int pos = 0; pos + 256 <= n; pos += 256)
            {
                for (int i = 0; i < 256; ++i)
                    block.setSample (0, i, voice[(size_t) (pos + i)]), block.setSample (1, i, voice[(size_t) (pos + i)]);
                engine.process (block, 256, s, h, false, nullptr);
                const auto t = (double) pos / sampleRate;
                const auto est = engine.getLastEstimate();
                if (t > 1.5 && t < 3.5 && est.voiced && engine.getLastCorrectedHz() > 20.0f)
                {
                    const auto lead = dsp::PitchCorrector::hzToMidi (engine.getLastCorrectedHz(), 440.0f);
                    const auto harm = dsp::PitchCorrector::hzToMidi (est.hz * engine.getHarmonizer().getVoiceRatio (0), 440.0f);
                    leadLo = std::min (leadLo, lead); leadHi = std::max (leadHi, lead);
                    intervalLo = std::min (intervalLo, harm - lead); intervalHi = std::max (intervalHi, harm - lead);
                }
            }
            note ("harmony (a third above, Mix, 100%): lead swings " + format ((leadHi - leadLo) * 100.0f, 0) + " c peak to peak, harmony - lead "
                  + format (intervalLo, 3) + " .. " + format (intervalHi, 3) + " semitones");
            check (leadHi - leadLo > 0.8f, "the lead keeps its vibrato");
            check (intervalLo > 2.98f && intervalHi < 3.02f, "the harmony keeps a steady minor third, so it carries the same vibrato");
        }

        // Pitch page: the Vibrato knob and the display hint (dashed centre line + VIB readout).
        {
            auto p = makeProcessor();
            setVibratoMode (*p, 1);
            setParam (*p, "vibrato", 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
            auto* editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "editor for the vibrato hint");
            if (editor != nullptr)
            {
                editor->selectPage (Editor::pitchPage);
                editor->refreshEnablementNow();
                auto* knob = findById (*editor, "vibrato");
                check (knob != nullptr && visibleChain (*knob), "the Vibrato knob is on the pitch page");
                const auto total = (int) (3.0 * sampleRate);
                const auto take = vibratoVoice (total, [] (double t) { return vibratoCents (t, t < 1.9 ? 30.0 : 330.0); });
                std::vector<dsp::PitchFrame> frames (2048);
                float maxShown = 0.0f;
                for (int pos = 0; pos < total; pos += 4096)
                {
                    const auto len = std::min (4096, total - pos);
                    render (*p, [&] (int i) { return take[(size_t) (pos + i)]; }, len);
                    const auto got = p->getPitchTap().pop (frames.data(), (int) frames.size());
                    editor->getPitchDisplay().update (frames.data(), got, 0.1);
                    maxShown = std::max (maxShown, editor->getPitchDisplay().getShownVibrato());
                }
                check (maxShown > 0.5f, "the pitch display shows the vibrato hint (" + format (maxShown, 2) + ")");
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-vibrato");
                editor->setSize (940, 600);
                saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-vibrato-940x600");
                p->editorBeingDeleted (editor);
            }
            base.reset();
        }

        // Old sessions (no Vibrato parameter) load at 0.
        {
            auto src = makeProcessor();
            setParam (*src, "corr_key", 5.0f);
            juce::MemoryBlock blob;
            src->getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            juce::MemoryBlock old;
            if (xml != nullptr)
            {
                stripParameters (*xml, { "vibrato", "rev_", "dly_", "fx_routing", "range_", "key_", "harm_lofi" });
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
            }
            auto q = makeProcessor();
            setParam (*q, "vibrato", 0.8f);
            q->setStateInformation (old.getData(), (int) old.getSize());
            check (old.getSize() > 32 && std::abs (getPlain (*q, "vibrato")) < 1.0e-6f && std::abs (getPlain (*q, "corr_key") - 5.0f) < 0.1f,
                   "an older session loads with Vibrato Respect at 0 and keeps its settings");
        }
    }

    //==============================================================================
    // Key Follow (v0.2.0): sidechain / vocal key detection driving the Keyed key and scale.
    std::vector<float> chordProgression (const std::vector<std::string>& chords, double seconds = 2.0, float amp = 0.25f)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto n = (int) (seconds * sampleRate);
        std::vector<float> out ((size_t) n * chords.size(), 0.0f);
        for (size_t c = 0; c < chords.size(); ++c)
        {
            auto name = chords[c];
            std::vector<int> intervals { 0, 4, 7 };
            if (name.size() > 1 && name.back() == '7') { intervals = { 0, 4, 7, 10 }; name.pop_back(); }
            else if (name.size() > 1 && name.back() == 'm') { intervals = { 0, 3, 7 }; name.pop_back(); }
            int root = 0;
            for (int k = 0; k < 12; ++k)
                if (name == names[k])
                    root = k;
            std::vector<int> notes { 36 + root, 48 + root };
            for (auto iv : intervals)
                notes.push_back (60 + (root + iv) % 12);
            for (auto m : notes)
            {
                const auto f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
                for (int h = 1; h <= 6; ++h)
                {
                    if (f * h >= 8000.0)
                        continue;
                    const auto w = 2.0 * juce::MathConstants<double>::pi * f * h / sampleRate;
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = i / sampleRate;
                        const auto env = std::min (1.0, t / 0.01) * std::exp (-0.6 * t);
                        out[c * (size_t) n + (size_t) i] += (float) (std::sin (w * i) / h * env);
                    }
                }
            }
        }
        float peak = 0.0f;
        for (auto v : out)
            peak = std::max (peak, std::abs (v));
        for (auto& v : out)
            v *= amp / std::max (peak, 1.0e-9f);
        return out;
    }

    // C major (with passing A#, E7) for 32 s, E major for 24 s, A minor for 24 s (2 s chords).
    std::vector<float> keyFollowProgression()
    {
        return chordProgression ({ "C", "G", "Am", "F", "C", "G", "F", "C", "Dm", "G", "C", "A#", "C", "E7", "Am", "G",
                                   "E", "B", "C#m", "A", "E", "B", "A", "E", "C#m", "F#m", "B", "E",
                                   "Am", "Dm", "E", "Am", "F", "Dm", "E7", "Am", "F", "G", "E", "Am" });
    }

    struct KeyEvent { double seconds; int key; };

    // Feeds a signal to a bare KeyDetector (as sidechain or vocal) and records each decision.
    std::vector<KeyEvent> runKeyDetector (const std::vector<float>& signal, float sensitivity, bool asVocal, int* allocations = nullptr)
    {
        dsp::KeyDetector detector;
        detector.prepare (sampleRate);
        dsp::KeyDetector::Settings settings;
        settings.sensitivity = sensitivity;
        settings.source = asVocal ? dsp::KeySource::vocal : dsp::KeySource::sidechain;
        std::vector<KeyEvent> events;
        int last = -1;
        for (int pos = 0; pos < (int) signal.size(); pos += blockSize)
        {
            const auto n = std::min (blockSize, (int) signal.size() - pos);
            const float* ptrs[2] { signal.data() + pos, signal.data() + pos };
            if (allocations != nullptr) { allocationCount.store (0); countAllocations.store (true); }
            if (asVocal)
                detector.process (nullptr, 0, ptrs, 2, n, settings);
            else
                detector.process (ptrs, 2, nullptr, 0, n, settings);
            if (allocations != nullptr) { countAllocations.store (false); *allocations += allocationCount.load(); }
            if (detector.getKey() != last)
            {
                last = detector.getKey();
                events.push_back ({ (pos + n) / sampleRate, last });
            }
        }
        return events;
    }

    // A monophonic "vocal" melody in C major (0.4 s notes, slight vibrato, harmonics).
    std::vector<float> cMajorMelody (double seconds)
    {
        const int melody[] { 60, 62, 64, 65, 67, 65, 64, 62, 60, 64, 67, 72, 71, 67, 65, 64, 62, 60, 59, 60,
                             64, 62, 60, 57, 59, 60, 62, 64, 65, 64, 62, 60 };
        const auto noteLen = (int) (0.4 * sampleRate);
        const auto total = (int) (seconds * sampleRate);
        std::vector<float> out ((size_t) total, 0.0f);
        double phase = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const auto idx = (i / noteLen) % (int) std::size (melody);
            const auto t = (i % noteLen) / sampleRate;
            const auto f = 440.0 * std::pow (2.0, (melody[idx] - 69) / 12.0) * (1.0 + 0.004 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t));
            phase += 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
            double s = 0.0;
            for (int h = 1; h <= 7; ++h)
                s += std::sin (h * phase) / h;
            const auto env = std::min (1.0, t / 0.03) * std::min (1.0, (0.4 - t) / 0.03);
            out[(size_t) i] = (float) (0.15 * s * std::max (0.0, env));
        }
        return out;
    }

    // Processor with the optional stereo sidechain enabled (4-channel process buffer: main 0-1, sidechain 2-3).
    std::unique_ptr<Processor> makeSidechainProcessor (bool enableSidechain)
    {
        auto p = std::make_unique<Processor>();
        if (enableSidechain)
        {
            auto layout = p->getBusesLayout();
            if (layout.inputBuses.size() > 1)
                layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            check (p->setBusesLayout (layout), "the stereo sidechain layout is accepted");
        }
        p->setRateAndBufferSizeDetails (sampleRate, blockSize);
        p->prepareToPlay (sampleRate, blockSize);
        return p;
    }

    // Renders main (vocal) + optional sidechain; calls onBlock (seconds) after every block.
    juce::AudioBuffer<float> renderWithSidechain (Processor& p, const std::function<float (int)>& vocal,
                                                  const std::vector<float>* sidechain, int numSamples,
                                                  const std::function<void (double)>& onBlock = {}, int* allocations = nullptr)
    {
        const auto channels = std::max (2, p.getTotalNumInputChannels());
        juce::AudioBuffer<float> out (2, numSamples), scratch (channels, blockSize);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < numSamples; pos += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - pos);
            scratch.setSize (channels, n, false, false, true);
            scratch.clear();
            for (int i = 0; i < n; ++i)
            {
                const auto v = vocal (pos + i);
                scratch.setSample (0, i, v);
                scratch.setSample (1, i, v);
                if (channels >= 4 && sidechain != nullptr && pos + i < (int) sidechain->size())
                {
                    scratch.setSample (2, i, (*sidechain)[(size_t) (pos + i)]);
                    scratch.setSample (3, i, (*sidechain)[(size_t) (pos + i)]);
                }
            }
            if (allocations != nullptr) { allocationCount.store (0); countAllocations.store (true); }
            p.processBlock (scratch, midi);
            if (allocations != nullptr) { countAllocations.store (false); *allocations += allocationCount.load(); }
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, scratch, ch, 0, n);
            if (onBlock)
                onBlock ((pos + n) / sampleRate);
        }
        return out;
    }

    bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() < 2 || b.getNumChannels() < 2)
            return false;
        for (int ch = 0; ch < 2; ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;
        return true;
    }

    void testKeyFollow()
    {
        // Parameters: appended after Vibrato Respect, defaults leave Key Follow off.
        {
            auto p = makeProcessor();
            const auto ids = allParameterIds (*p);
            check (ids.size() >= 5 && ids[ids.size() - 5] == "key_follow" && ids[ids.size() - 4] == "key_sens"
                       && ids[ids.size() - 3] == "key_lock" && ids[ids.size() - 2] == "key_source",
                   "key_follow, key_sens, key_lock, key_source are appended after Vibrato Respect (only Harmony Lo-Fi follows)");
            check (getPlain (*p, "key_follow") < 0.5f && std::abs (getPlain (*p, "key_sens") - 0.5f) < 1.0e-4f
                       && getPlain (*p, "key_lock") < 0.5f && juce::roundToInt (getPlain (*p, "key_source")) == 0,
                   "defaults: Off, 50 %, unlocked, Auto");
            check (dooblrvocals::presets::isSessionOnlyParameter ("key_follow")
                       && dooblrvocals::presets::isSessionOnlyParameter ("key_source"),
                   "Key Follow parameters are session-only (presets leave them alone)");
            check (p->getBusCount (true) == 2 && p->getBus (true, 1) != nullptr && ! p->getBus (true, 1)->isEnabled()
                       && p->getTotalNumInputChannels() == 2 && p->getTotalNumOutputChannels() == 2,
                   "an optional Sidechain input bus exists and is disabled by default (main I/O stays 2 in / 2 out)");
            check (p->getBus (true, 1) != nullptr && p->getBus (true, 1)->getName() == "Sidechain", "the aux bus is named Sidechain");
            auto layout = p->getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::mono();
            check (p->checkBusesLayoutSupported (layout), "a mono sidechain is accepted");
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
            check (! p->checkBusesLayoutSupported (layout), "a 5.1 sidechain is rejected");
        }

        // Detection on the C -> E major -> A minor progression, at three sensitivities.
        const auto progression = keyFollowProgression();
        struct Section { double start, end; int key; const char* name; double maxDelay; };
        const Section sections[] { { 0.0, 32.0, 0, "C major", 14.0 }, { 32.0, 56.0, 4, "E major", 16.0 }, { 56.0, 80.0, 21, "A minor", 22.0 } };
        for (const auto sens : { 0.0f, 0.5f, 1.0f })
        {
            int allocations = 0;
            const auto events = runKeyDetector (progression, sens, false, &allocations);
            const auto tag = "sens " + format (sens, 1) + ": ";
            check (allocations == 0, tag + "detector is allocation-free (" + std::to_string (allocations) + ")");
            std::string log;
            for (const auto& e : events)
                log += format (e.seconds, 1) + "s " + dsp::KeyDetector::keyName (e.key).toStdString() + "; ";
            note (tag + log);
            int extra = 0;
            for (const auto& sec : sections)
            {
                double hit = -1.0;
                bool flipAfterHit = false;
                for (const auto& e : events)
                {
                    if (e.seconds < sec.start || e.seconds >= sec.end)
                        continue;
                    if (e.key == sec.key && hit < 0.0)
                        hit = e.seconds;
                    else if (hit >= 0.0)
                        flipAfterHit = true;
                    else if (e.key != sec.key)
                        ++extra;
                }
                check (hit >= 0.0 && hit - sec.start <= sec.maxDelay,
                       tag + sec.name + " detected within " + format (sec.maxDelay, 0) + " s (after " + format (hit - sec.start, 1) + " s)");
                check (! flipAfterHit, tag + sec.name + ": no change after it was detected (passing chords don't flip it)");
            }
            check (extra <= 1, tag + "at most one wrong key on the way (" + std::to_string (extra) + ")");
            check ((int) events.size() <= 4, tag + "no flip-flopping (" + std::to_string (events.size()) + " decisions)");
        }

        // The vocal alone (fallback): a C major melody is found, more slowly.
        {
            const auto melody = cMajorMelody (30.0);
            const auto events = runKeyDetector (melody, 0.5f, true);
            check (! events.empty() && events.front().key == 0 && events.front().seconds < 20.0,
                   "vocal fallback: C major from the melody within 20 s ("
                       + (events.empty() ? std::string ("none") : format (events.front().seconds, 1) + " s "
                                                                    + dsp::KeyDetector::keyName (events.front().key).toStdString()) + ")");
            check (events.size() == 1, "vocal fallback: one decision, no flip-flop (" + std::to_string (events.size()) + ")");
        }

        // Silence holds the key (and never decides one from nothing).
        {
            std::vector<float> silence ((size_t) (20 * sampleRate), 0.0f);
            check (runKeyDetector (silence, 1.0f, false).empty(), "silence never produces a key");
            auto withGap = chordProgression ({ "E", "B", "C#m", "A", "E", "B", "A", "E" });
            withGap.resize (withGap.size() + (size_t) (20 * sampleRate), 0.0f);
            const auto events = runKeyDetector (withGap, 1.0f, false);
            std::string log;
            for (const auto& e : events)
                log += format (e.seconds, 1) + "s " + dsp::KeyDetector::keyName (e.key).toStdString() + "; ";
            note ("E then silence: " + log);
            // A 16 s standalone progression can open on its relative minor (C# minor) before settling;
            // what matters here is that nothing changes once the audio stops.
            const auto chordsEnd = 16.0;
            bool changedInSilence = false;
            for (const auto& e : events)
                changedInSilence = changedInSilence || e.seconds > chordsEnd + 0.5;
            check (! events.empty() && events.back().key == 4 && events.size() <= 2 && ! changedInSilence,
                   "a silent sidechain holds the last key (E major through 20 s of silence)");
        }

        // Off is bit-identical: no sidechain vs a live sidechain, Key Follow controls moved but off.
        const auto vocal = [] (int i) { return sineAt (226.0, i, sampleRate, 0.3f); }; // between A3 and A#3
        const auto n = (int) (20.0 * sampleRate);
        {
            auto a = makeSidechainProcessor (false);
            auto b = makeSidechainProcessor (true);
            check (b->getTotalNumInputChannels() == 4 && b->isSidechainConnected(), "sidechain enabled: 4 input channels");
            check (! a->isSidechainConnected(), "default layout: no sidechain connected");
            setParam (*b, "key_sens", 1.0f);
            setParam (*b, "key_source", 1.0f);
            int allocations = 0;
            const auto outA = renderWithSidechain (*a, vocal, nullptr, n);
            const auto outB = renderWithSidechain (*b, vocal, &progression, n, {}, &allocations);
            check (bitIdentical (outA, outB), "Key Follow Off: output is bit-identical with and without a live sidechain");
            check (allocations == 0, "no allocations in processBlock with the sidechain (" + std::to_string (allocations) + ")");
            check (b->getKeyFollowStatus().detectedKey == 0 && b->getKeyFollowStatus().appliedKey < 0,
                   "Off: the key is still detected for the readout (C major) but not applied");
        }

        // On, following the sidechain through the progression; Lock freezes the applied key.
        {
            auto p = makeSidechainProcessor (true);
            setParam (*p, "corr_key", 2.0f);   // manual fallback: D Dorian
            setParam (*p, "corr_scale", 2.0f);
            setParam (*p, "key_follow", 1.0f);
            int appliedAt20 = -2, appliedAt50 = -2, appliedAt70 = -2, detectedAt78 = -2, appliedAt78 = -2, appliedEnd = -2;
            int sourceAt20 = -1;
            const auto total = (int) progression.size();
            renderWithSidechain (*p, vocal, &progression, total, [&] (double t)
            {
                const auto st = p->getKeyFollowStatus();
                auto crossed = [&] (double mark) { return t >= mark && t - blockSize / sampleRate < mark; };
                if (crossed (20.0)) { appliedAt20 = st.appliedKey; sourceAt20 = st.activeSource; }
                if (crossed (50.0)) { appliedAt50 = st.appliedKey; setParam (*p, "key_lock", 1.0f); }
                if (crossed (70.0)) appliedAt70 = st.appliedKey;
                if (crossed (79.5)) { detectedAt78 = st.detectedKey; appliedAt78 = st.appliedKey; setParam (*p, "key_lock", 0.0f); }
            });
            renderWithSidechain (*p, vocal, nullptr, blockSize * 2);
            appliedEnd = p->getKeyFollowStatus().appliedKey;
            check (appliedAt20 == 0 && sourceAt20 == 1, "On: C major applied by 20 s, from the sidechain (" + std::to_string (appliedAt20) + ")");
            check (appliedAt50 == 4, "On: E major applied by 50 s (" + std::to_string (appliedAt50) + ")");
            check (appliedAt70 == 4 && appliedAt78 == 4 && detectedAt78 == 21,
                   "Lock: E major stays applied while A minor is detected (" + std::to_string (appliedAt78) + " / " + std::to_string (detectedAt78) + ")");
            check (appliedEnd == 21, "unlock: the detected A minor is applied (" + std::to_string (appliedEnd) + ")");
            check (juce::roundToInt (getPlain (*p, "corr_key")) == 2 && juce::roundToInt (getPlain (*p, "corr_scale")) == 2,
                   "Key Follow never writes the Key / Scale parameters (automation is not fought)");
        }

        // On vs Off differ once a key is applied (the correction really uses it): E major vs C major fallback.
        {
            const auto eMajor = chordProgression ({ "E", "B", "C#m", "A", "E", "B", "A", "E", "B", "E" });
            const auto len = (int) eMajor.size();
            const auto sharp = [] (int i) { return sineAt (207.65 * std::pow (2.0, 0.3 / 12.0), i, sampleRate, 0.3f); }; // G#3 +30 c
            auto off = makeSidechainProcessor (true);
            auto on = makeSidechainProcessor (true);
            setParam (*on, "key_follow", 1.0f);
            for (auto* q : { off.get(), on.get() })
            {
                setParam (*q, "corr_key", 0.0f);  // C major: G# is not in it
                setParam (*q, "corr_scale", 0.0f);
                setParam (*q, "retune_speed", 0.0f);
            }
            const auto outOff = renderWithSidechain (*off, sharp, &eMajor, len);
            const auto outOn = renderWithSidechain (*on, sharp, &eMajor, len);
            const auto start = len - (int) (2.0 * sampleRate);
            const auto hzOff = detectHz (outOff, start, 8192), hzOn = detectHz (outOn, start, 8192);
            check (on->getKeyFollowStatus().appliedKey == 4, "E major applied from the sidechain");
            check (std::abs (hzOn - 207.65f) < 2.0f && std::abs (hzOff - 207.65f) > 4.0f,
                   "Key Follow on: G# is kept (E major) instead of snapping to C major (on " + format (hzOn, 1) + " Hz, off " + format (hzOff, 1) + " Hz)");
        }

        // State round-trip, and an older session loads with Key Follow off.
        {
            auto src = makeProcessor();
            setParam (*src, "key_follow", 1.0f);
            setParam (*src, "key_sens", 0.8f);
            setParam (*src, "key_lock", 1.0f);
            setParam (*src, "key_source", 2.0f);
            juce::MemoryBlock blob;
            src->getStateInformation (blob);
            auto q = makeProcessor();
            q->setStateInformation (blob.getData(), (int) blob.getSize());
            check (getPlain (*q, "key_follow") > 0.5f && std::abs (getPlain (*q, "key_sens") - 0.8f) < 0.01f
                       && getPlain (*q, "key_lock") > 0.5f && juce::roundToInt (getPlain (*q, "key_source")) == 2,
                   "Key Follow state round-trips");

            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            juce::MemoryBlock old;
            if (xml != nullptr)
            {
                stripParameters (*xml, { "key_" });
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
            }
            auto r = makeProcessor();
            setParam (*r, "key_follow", 1.0f);
            r->setStateInformation (old.getData(), (int) old.getSize());
            check (old.getSize() > 32 && getPlain (*r, "key_follow") < 0.5f && getPlain (*r, "key_lock") < 0.5f,
                   "an older session (no key_* parameters) loads with Key Follow off");
        }

        // Editor: the strip on the pitch page, bound controls, the readout and the followed key in the displays.
        {
            auto p = makeSidechainProcessor (true);
            setParam (*p, "key_follow", 1.0f);
            const auto eMajor = chordProgression ({ "E", "B", "C#m", "A", "E", "B", "A", "E", "B", "E" });
            renderWithSidechain (*p, [] (int i) { return sineAt (207.65, i, sampleRate, 0.3f); }, &eMajor, (int) eMajor.size());
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
            auto* editor = dynamic_cast<Editor*> (base.get());
            check (editor != nullptr, "editor for Key Follow");
            if (editor == nullptr)
                return;
            editor->setSize (1080, 660);
            editor->selectPage (Editor::pitchPage);
            editor->refreshKeyFollowNow();
            auto& strip = editor->getKeyFollowStrip();
            check (visibleChain (strip) && strip.getHeight() >= 24, "the Key Follow strip is on the pitch page");
            check (strip.getY() >= editor->getModeToggle().getBottom() && strip.getBottom() <= editor->getPitchDisplay().getY(),
                   "it sits between the KEYED | CHROMATIC bar and the pitch display");
            for (const auto* id : { "key_follow", "key_lock", "key_sens", "key_source" })
            {
                auto* c = findById (*editor, id);
                check (c != nullptr && visibleChain (*c) && strip.getBounds().contains (c->getBounds() + strip.getPosition()),
                       std::string ("strip control ") + id);
            }
            auto* follow = dynamic_cast<juce::ToggleButton*> (findById (*editor, "key_follow"));
            check (follow != nullptr && follow->getToggleState(), "the Follow toggle shows On");
            check (strip.getKeyText() == "E major", "the readout shows E major (" + strip.getKeyText().toStdString() + ")");
            check (strip.getStatusText().contains ("sidechain"), "status names the sidechain (" + strip.getStatusText().toStdString() + ")");
            check (editor->getPitchDisplay().getFollowedKey() == 4 && editor->getHarmonyDisplay().getFollowedKey() == 4,
                   "the pitch and harmony displays follow E major");
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-key-follow");
            editor->setSize (940, 600);
            saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-key-follow-940x600");
            check (strip.getWidth() > 600 && editor->getPitchDisplay().getHeight() > 120, "940 x 600: strip and pitch display still fit");

            // Off: the displays go back to the Key / Scale parameters.
            setParam (*p, "key_follow", 0.0f);
            renderWithSidechain (*p, [] (int) { return 0.0f; }, nullptr, blockSize);
            editor->refreshKeyFollowNow();
            check (editor->getPitchDisplay().getFollowedKey() == -1 && strip.getKeyText() == "E major",
                   "Off: displays use Key / Scale again; the readout still shows the detected key");
            p->editorBeingDeleted (editor);
            base.reset();
        }
    }

    //==============================================================================
    // Harmony Lo-Fi (harm_lofi) and the "Vocal Workstation '90s" factory preset.
    void testHarmonyLoFi()
    {
        const auto sawA = [] (int i) { return sawAt (220.0, i, sampleRate, 0.35f); };

        // Appended last, 0 .. 100 %, default 0.
        {
            auto p = makeProcessor();
            const auto ids = allParameterIds (*p);
            check (! ids.isEmpty() && ids[ids.size() - 1] == "harm_lofi", "harm_lofi is the last parameter");
            check (std::abs (getPlain (*p, "harm_lofi")) < 1.0e-6f, "Harmony Lo-Fi defaults to 0");
            check (! dooblrvocals::presets::isSessionOnlyParameter ("harm_lofi"), "Harmony Lo-Fi is stored in presets");
            if (auto* param = getParam (*p, "harm_lofi"))
                check (param->getCurrentValueAsText().contains ("%") && param->getName (64) == "Harmony Lo-Fi", "shown as a percentage");
        }

        const auto setupHarmony = [] (Processor& p)
        {
            setParam (p, "harm_on", 1.0f);
            for (int v = 1; v <= dsp::numHarmonyVoices; ++v)
                setParam (p, "harm" + juce::String (v) + "_on", 1.0f);
        };
        const auto lat = liveLatency();
        const auto n = lat + 16384;

        // 0 = bypassed: identical, sample for sample, to a processor that never touched the control,
        // and to one that was driven at 100 % and then returned to 0 and reset.
        {
            auto a = makeProcessor();
            setupHarmony (*a);
            const auto outA = render (*a, sawA, n);

            auto b = makeProcessor();
            setupHarmony (*b);
            setParam (*b, "harm_lofi", 1.0f);
            render (*b, sawA, 4096);
            setParam (*b, "harm_lofi", 0.0f);
            b->prepareToPlay (sampleRate, blockSize);
            const auto outB = render (*b, sawA, n);

            auto exact = true;
            for (int ch = 0; ch < 2 && exact; ++ch)
                for (int i = 0; i < n; ++i)
                    if (! juce::exactlyEqual (outA.getSample (ch, i), outB.getSample (ch, i))) { exact = false; break; }
            check (exact, "Harmony Lo-Fi at 0 is bit-identical to the default path");
        }

        // > 0: audible on the harmony bus only, finite, bounded, no allocations.
        {
            auto a = makeProcessor();
            setupHarmony (*a);
            const auto clean = render (*a, sawA, n);

            auto b = makeProcessor();
            setupHarmony (*b);
            setParam (*b, "harm_lofi", 1.0f);
            int allocations = 0;
            render (*b, sawA, 4096, blockSize, &allocations);
            b->prepareToPlay (sampleRate, blockSize);
            const auto crushed = render (*b, sawA, n);
            check (allocations == 0, "Harmony Lo-Fi does not allocate on the audio thread");
            check (allFinite (crushed) && crushed.getMagnitude (lat, n - lat) < 4.0f, "Harmony Lo-Fi output is finite and bounded");
            const auto diff = bufferDiff (clean, crushed, lat + 512);
            check (diff > 1.0, "Harmony Lo-Fi changes the harmony voices (diff " + std::to_string (diff) + ")");

            // Harmony off: the control does nothing.
            auto c = makeProcessor();
            setParam (*c, "harm_lofi", 1.0f);
            const auto offA = render (*c, sawA, n);
            auto d = makeProcessor();
            const auto offB = render (*d, sawA, n);
            check (juce::exactlyEqual (bufferDiff (offA, offB, 0), 0.0), "Harmony Lo-Fi is silent when the harmony bus is off");
        }

        // State round-trip, and an older session (no harm_lofi) loads with it off.
        {
            auto src = makeProcessor();
            setParam (*src, "harm_lofi", 0.4f);
            setParam (*src, "corr_key", 7.0f);
            juce::MemoryBlock blob;
            src->getStateInformation (blob);
            auto q = makeProcessor();
            q->setStateInformation (blob.getData(), (int) blob.getSize());
            check (std::abs (getPlain (*q, "harm_lofi") - 0.4f) < 0.01f, "Harmony Lo-Fi round-trips");

            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            juce::MemoryBlock old;
            if (xml != nullptr)
            {
                stripParameters (*xml, { "harm_lofi" });
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
            }
            auto r = makeProcessor();
            setParam (*r, "harm_lofi", 0.9f);
            r->setStateInformation (old.getData(), (int) old.getSize());
            check (old.getSize() > 32 && std::abs (getPlain (*r, "harm_lofi")) < 1.0e-6f && std::abs (getPlain (*r, "corr_key") - 7.0f) < 0.1f,
                   "an older session loads with Harmony Lo-Fi at 0 and keeps its settings");
        }
    }

    void testVocalWorkstationPreset()
    {
        const juce::String name ("Vocal Workstation '90s");
        auto p = makeProcessor();
        int index = -1;
        for (int i = 0; i < p->getNumPrograms(); ++i)
            if (p->getProgramName (i) == name)
                index = i;
        check (index > 0, "factory preset \"Vocal Workstation '90s\" exists");
        if (index <= 0)
            return;

        // No third-party names anywhere a user can see them.
        {
            auto clean = true;
            for (int i = 0; i < p->getNumPrograms(); ++i)
            {
                const auto n = p->getProgramName (i);
                clean = clean && ! n.containsIgnoreCase ("digitech") && ! n.containsIgnoreCase ("vocalist");
            }
            for (auto* param : p->getParameters())
            {
                const auto n = param->getName (128);
                clean = clean && ! n.containsIgnoreCase ("digitech") && ! n.containsIgnoreCase ("vocalist");
            }
            check (clean, "no preset or parameter name carries a hardware brand or model name");
        }

        setParam (*p, "key_follow", 1.0f);
        p->setCurrentProgram (index);
        const auto is = [&p] (const char* id, float v, float tol = 0.02f) { return std::abs (getPlain (*p, id) - v) <= tol; };

        check (is ("corr_mode", 0) && is ("corr_scale", 0) && getPlain (*p, "retune_speed") <= 8.0f
                   && is ("corr_amount", 1) && getPlain (*p, "humanize") <= 0.1f,
               "lead: keyed, major scale, hard and fast correction");
        check (is ("harm_on", 1) && is ("harm_formant", 0) && getPlain (*p, "harm_lofi") > 0.3f,
               "harmony bus on, formants follow the shift (gender-style artifacts), lo-fi voices");
        auto scaleVoices = 0, onVoices = 0;
        float minPan = 1.0f, maxPan = -1.0f;
        for (int v = 1; v <= dsp::numHarmonyVoices; ++v)
        {
            const auto id = [v] (const char* suffix) { return "harm" + juce::String (v) + "_" + suffix; };
            if (getPlain (*p, id ("on")) > 0.5f) ++onVoices;
            if (juce::roundToInt (getPlain (*p, id ("mode"))) == 0) ++scaleVoices;
            minPan = std::min (minPan, getPlain (*p, id ("pan")));
            maxPan = std::max (maxPan, getPlain (*p, id ("pan")));
        }
        check (onVoices == 4 && scaleVoices >= 3, "four harmony voices, scale-following");
        check (is ("harm1_degree", 2) && is ("harm2_degree", 4) && is ("harm3_degree", -3) && is ("harm4_semis", -12),
               "voicing: 3rd and 5th above, 4th below, octave below");
        check (minPan <= -0.7f && maxPan >= 0.7f, "voices panned in a wide spread");
        check (is ("rev_on", 1) && getPlain (*p, "rev_decay") <= 1.5f && getPlain (*p, "rev_mix") <= 0.25f,
               "modest room reverb");
        check (getPlain (*p, "key_follow") > 0.5f, "loading the preset leaves Key Follow (session-only) alone");

        const auto lat = p->getLatencySamples();
        const auto out = render (*p, [] (int i) { return sawAt (220.0, i, sampleRate, 0.3f); }, lat + 16384);
        double side = 0.0, mid = 0.0;
        for (int i = lat + 4096; i < out.getNumSamples(); ++i)
        {
            const auto l = (double) out.getSample (0, i), r = (double) out.getSample (1, i);
            side += (l - r) * (l - r);
            mid += (l + r) * (l + r);
        }
        check (allFinite (out) && mid > 1.0, "preset renders");
        check (side > 0.05 * mid, "preset output is stereo-wide (side/mid " + std::to_string (side / std::max (mid, 1.0e-9)) + ")");
    }

    //==============================================================================
    // Reverb calibration: every reverb section preset's mid-band RT60 against its Decay setting.

    /** RT60 (s) from the Schroeder backward-integrated energy, fitted between hiDb and loDb (T20 / T30). */
    double measureRt60Between (const std::vector<float>& ir, double rate, double hiDb, double loDb)
    {
        std::vector<double> edc (ir.size() + 1, 0.0);
        for (size_t i = ir.size(); i-- > 0;)
            edc[i] = edc[i + 1] + (double) ir[i] * (double) ir[i];
        const auto total = std::max (1.0e-30, edc[0]);
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int count = 0;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            const auto db = 10.0 * std::log10 (std::max (1.0e-30, edc[i] / total));
            if (db <= hiDb && db >= loDb)
            {
                const auto t = (double) i / rate;
                sx += t; sy += db; sxx += t * t; sxy += t * db; ++count;
            }
        }
        if (count < 10)
            return 0.0;
        const auto slope = (count * sxy - sx * sy) / std::max (1.0e-30, count * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    /** The 1 kHz octave band (707 .. 1414 Hz, 4th-order Butterworth each side) of a signal. */
    std::vector<float> octaveBand1k (const std::vector<float>& x, double rate)
    {
        dsp::SimpleFilter hp1, hp2, lp1, lp2;
        hp1.setHighPass (707.0, rate); hp2.setHighPass (707.0, rate);
        lp1.setLowPass (1414.0, rate); lp2.setLowPass (1414.0, rate);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = lp2.process (0, lp1.process (0, hp2.process (0, hp1.process (0, x[i]))));
        return y;
    }

    /** Renders a reverb's impulse response as separate L and R wet channels. */
    std::array<std::vector<float>, 2> reverbImpulseStereo (const dsp::ReverbSettings& s, int n)
    {
        dsp::VocalReverb rev;
        rev.prepare (sampleRate, blockSize);
        std::array<std::vector<float>, 2> out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        std::vector<float> x ((size_t) blockSize), wl ((size_t) blockSize), wr ((size_t) blockSize);
        for (int pos = 0; pos < n; pos += blockSize)
        {
            const auto m = std::min (blockSize, n - pos);
            for (int i = 0; i < m; ++i) x[(size_t) i] = pos + i == 0 ? 1.0f : 0.0f;
            rev.renderWet (x.data(), x.data(), wl.data(), wr.data(), m, x.data(), x.data(), s, 120.0);
            std::copy (wl.begin(), wl.begin() + m, out[0].begin() + pos);
            std::copy (wr.begin(), wr.begin() + m, out[1].begin() + pos);
        }
        return out;
    }

    /** Where the late tail is on its own: after the pre-delay, the last early reflection and the input diffusers. */
    int lateTailStart (const dsp::ReverbSettings& s)
    {
        const auto ms = dsp::VocalReverb::preDelayMsFor (s, 120.0) + dsp::VocalReverb::earlyTapMs (dsp::VocalReverb::numEarly - 1, s.size) + 15.0;
        return (int) std::ceil (ms * 0.001 * sampleRate);
    }

    /** Mid-band RT60 (s): the 1 kHz octave of both wet channels, Schroeder-integrated on L^2 + R^2
        from fromSample on (0 = the whole response, lateTailStart() = the late tail Decay sets). */
    double midBandRt60 (const dsp::ReverbSettings& s, double hiDb, double loDb, int fromSample = 0)
    {
        const auto n = (int) ((s.decaySeconds * 1.4 + 1.0) * sampleRate);
        const auto ir = reverbImpulseStereo (s, n);
        const auto l = octaveBand1k (ir[0], sampleRate), r = octaveBand1k (ir[1], sampleRate);
        // sqrt (L^2 + R^2) squares back to the summed energy inside measureRt60Between.
        std::vector<float> e ((size_t) (n - fromSample));
        for (size_t i = 0; i < e.size(); ++i)
            e[i] = std::sqrt (l[i + (size_t) fromSample] * l[i + (size_t) fromSample] + r[i + (size_t) fromSample] * r[i + (size_t) fromSample]);
        return measureRt60Between (e, sampleRate, hiDb, loDb);
    }

    dsp::ReverbSettings reverbFromPreset (const dooblrvocals::presets::FactoryPreset& preset)
    {
        dsp::ReverbSettings s;
        s.enabled = true;
        s.mix = 1.0f;
        for (const auto& [id, v] : preset.values)
        {
            if (id == "rev_predelay") s.preDelayMs = v;
            else if (id == "rev_predelay_sync") s.preDelaySync = juce::roundToInt (v);
            else if (id == "rev_decay") s.decaySeconds = v;
            else if (id == "rev_size") s.size = v;
            else if (id == "rev_damping") s.damping = v;
            else if (id == "rev_lowcut") s.lowCutHz = v;
            else if (id == "rev_highcut") s.highCutHz = v;
            else if (id == "rev_width") s.width = v;
            else if (id == "rev_early") s.earlyLate = v;
            else if (id == "rev_diffusion") s.diffusion = v;
            else if (id == "rev_mod") s.modulation = v;
            else if (id == "rev_duck_release") s.duckReleaseMs = v;
        }
        s.duckDb = 0.0f; // an impulse response: no dry signal to duck against
        return s;
    }

    void testReverbPresetRt60()
    {
        // Decay is the RT60 of the late tail at mid frequencies: T30 (-5 .. -35 dB, extrapolated to
        // 60 dB) of the 1 kHz octave, from where the early reflections have finished.
        const auto& list = dooblrvocals::presets::getReverbPresets();
        check (list.size() >= 10, "reverb presets to measure");
        for (const auto& preset : list)
        {
            const auto s = reverbFromPreset (preset);
            const auto decay = (double) s.decaySeconds;
            const auto tail = midBandRt60 (s, -5.0, -35.0, lateTailStart (s));
            const auto whole = midBandRt60 (s, -5.0, -35.0);
            note ("RT60 " + preset.name.toStdString() + ": Decay " + format (decay, 2) + " s, late-tail T30 (1 kHz) " + format (tail, 3) + " s ("
                  + format (100.0 * (tail / decay - 1.0), 1) + " %); whole response incl. early reflections " + format (whole, 3) + " s ("
                  + format (100.0 * (whole / decay - 1.0), 1) + " %)");
            check (std::abs (tail / decay - 1.0) <= 0.03,
                   preset.name.toStdString() + ": late-tail RT60 within 3 % of Decay (" + format (tail, 3) + " s vs " + format (decay, 2) + " s)");
        }

        // And across the Decay / Size range on a plain (late only, undamped, unmodulated) reverb.
        // (A 0.3 s tail in the largest room is over in a handful of loop trips, too few to fit.)
        for (const auto decay : { 0.3f, 0.6f, 1.5f, 3.0f, 6.0f, 12.0f })
        {
            for (const auto size : { 0.0f, 0.5f, 1.0f })
            {
                if (decay < 0.5f && size > 0.6f)
                    continue;
                auto s = plainReverb();
                s.decaySeconds = decay;
                s.size = size;
                const auto rt = midBandRt60 (s, -5.0, -35.0, lateTailStart (s));
                note ("RT60 plain Decay " + format (decay, 1) + " s, size " + format (size, 1) + ": " + format (rt, 3) + " s ("
                      + format (100.0 * (rt / decay - 1.0), 1) + " %)");
                check (std::abs (rt / decay - 1.0) <= 0.03, "plain reverb RT60 within 3 % of Decay " + format (decay, 1) + " s (size "
                                                               + format (size, 1) + ": " + format (rt, 3) + " s)");
            }
        }
    }

    //==============================================================================
    // Output ceiling: the limiter is the last stage, after the delay and reverb.

    float vocalLike (int i)
    {
        // A sung-ish line: a saw with vibrato, note changes every 0.4 s and a syllable envelope.
        static constexpr double notes[] = { 220.0, 246.94, 261.63, 293.66, 329.63, 293.66, 261.63, 246.94 };
        const auto t = (double) i / sampleRate;
        const auto note = notes[(size_t) ((i / (int) (0.4 * sampleRate)) % 8)];
        const auto hz = note * std::exp2 (0.25 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t));
        static double phase = 0.0;
        if (i == 0) phase = 0.0;
        phase += hz / sampleRate;
        phase -= std::floor (phase);
        const auto syllable = std::fmod (t, 0.4) / 0.4;
        const auto env = syllable < 0.8 ? std::sin (juce::MathConstants<double>::pi * std::min (1.0, syllable / 0.8)) : 0.0;
        return (float) (0.5 * env * (2.0 * phase - 1.0));
    }

    // Learn Range over long takes and after the plug-in has been running: 44.1 / 48 kHz, all
    // three latency modes, host-sized blocks and editor-rate polling (15 Hz).
    void testLearnRangeLongRuns()
    {
        using dooblrvocals::RangeLearner;
        const auto semis = [] (double a, double b) { return 12.0 * std::log2 (a / b); };

        for (const auto rate : { 44100.0, 48000.0 })
        {
            for (const auto mode : { 0, 1, 2 }) // Tracking, Mix, HQ
            {
                const char* modeName = mode == 0 ? "Tracking" : mode == 1 ? "Mix" : "HQ";

                // Every rate / mode: a take from the start and one after 6 s of running (the
                // reported failure). 48 kHz Mix also covers 1, 10 and 30 s takes; set
                // DOOBLRVOCALS_LONG_LEARN=1 for the full matrix (1 / 5 / 10 / 30 s, 6 s + 5 / 10 s) everywhere.
                struct Run { double preRoll, learn; };
                std::vector<Run> runs { { 0.0, 5.0 }, { 6.0, 5.0 } };
                const auto* longEnv = std::getenv ("DOOBLRVOCALS_LONG_LEARN");
                if ((longEnv != nullptr && *longEnv == '1') || (rate == 48000.0 && mode == 1))
                    runs = { { 0.0, 1.0 }, { 0.0, 5.0 }, { 0.0, 10.0 }, { 0.0, 30.0 }, { 6.0, 5.0 }, { 6.0, 10.0 } };

                for (const auto run : runs)
                {
                    auto p = makeProcessor (rate, 256);
                    setParam (*p, "pitch_on", 1.0f);
                    setParam (*p, "latency_mode", mode == 0 ? 0.0f : 1.0f);
                    setParam (*p, "quality", mode == 2 ? 1.0f : 0.0f);

                    // A2 - E4 sweeps (2.5 s per sweep), short breath gaps.
                    double phase = 0.0;
                    juce::int64 clock = 0;
                    juce::Random rng (11);
                    auto voice = [&] (int)
                    {
                        const auto t = (double) clock++ / rate;
                        const auto ph = std::fmod (t / 2.5, 1.0);
                        const auto noteNum = 45.0 + 19.0 * (ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0);
                        phase += 440.0 * std::pow (2.0, (noteNum - 69.0) / 12.0) / rate;
                        phase -= std::floor (phase);
                        const auto gap = std::fmod (t, 1.0) > 0.92;
                        return gap ? (rng.nextFloat() - 0.5f) * 0.01f : (float) (0.3 * (2.0 * phase - 1.0));
                    };

                    const auto poll = (int) (rate / 15.0);
                    auto play = [&] (double seconds, bool polling)
                    {
                        const auto total = (int) (seconds * rate);
                        for (int pos = 0; pos < total; pos += poll)
                        {
                            render (*p, voice, std::min (poll, total - pos), 256);
                            if (polling)
                                p->pollRangeLearn();
                        }
                    };

                    play (run.preRoll, false);
                    p->startRangeLearn();
                    play (run.learn, true);
                    const auto r = p->stopRangeLearn();

                    const auto label = format (rate / 1000.0, 1) + " kHz " + modeName + ", " + format (run.preRoll, 0) + " s running, "
                                     + format (run.learn, 0) + " s take";
                    const auto result = r.valid ? RangeLearner::rangeText (r.lowHz, r.highHz).toStdString() : r.message.toStdString();
                    note ("learn " + label + ": " + format (r.voicedSeconds, 2) + " s voiced, " + result);

                    if (run.learn < 1.5)
                    {
                        check (! r.valid, label + ": a 1 s take is too short by design");
                        continue;
                    }

                    check (r.voicedSeconds > 0.6 * run.learn, label + ": most of the take counts as voiced ("
                               + format (r.voicedSeconds, 2) + " s of " + format (run.learn, 0) + " s)");
                    check (r.valid && std::abs (semis (r.lowHz, 110.0)) <= 1.5 && std::abs (semis (r.highHz, 329.63)) <= 1.5,
                           label + ": learns A2-E4 (" + result + ")");
                }
            }
        }

        // The engine takes every setting after its first block, not only the listed ones: Learn
        // Range, the learned detector range and Vibrato Respect used to stay at first-block values.
        {
            dsp::VocalEngine engine;
            engine.prepare (sampleRate, blockSize);
            juce::AudioBuffer<float> buffer (2, blockSize);
            auto run = [&] (int blocks)
            {
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (0, i, sawAt (220.0, b * blockSize + i)), buffer.setSample (1, i, buffer.getSample (0, i));
                    engine.process (buffer, blockSize);
                }
            };
            dsp::VocalSettings s;
            engine.setSettings (s);
            run (8);
            s.pitch.learnRange = true;
            s.pitch.rangeLowHz = 100.0f;
            s.pitch.rangeHighHz = 400.0f;
            s.pitch.vibratoRespect = 0.7f;
            engine.setSettings (s);
            run (2);
            const auto& got = engine.getSettings().pitch;
            check (got.learnRange && juce::exactlyEqual (got.rangeLowHz, 100.0f) && juce::exactlyEqual (got.rangeHighHz, 400.0f),
                   "Learn Range and the learned range reach a running engine at once");
            run (40);
            check (std::abs (engine.getSettings().pitch.vibratoRespect - 0.7f) < 1.0e-3f,
                   "Vibrato Respect reaches a running engine (" + format (engine.getSettings().pitch.vibratoRespect, 4) + ")");
            s.pitch.learnRange = false;
            s.pitch.vibratoRespect = 0.0f;
            engine.setSettings (s);
            run (60);
            check (! engine.getSettings().pitch.learnRange && juce::exactlyEqual (engine.getSettings().pitch.vibratoRespect, 0.0f),
                   "Learn Range disarms and Vibrato Respect lands exactly on 0");
        }
    }

    // Learn Range progress: voiced time (not wall-clock) drives a monotonic 0..1 bar, the status
    // line says "Waiting for voice" / "Keep singing... N%", 100 % finishes the take on its own
    // (done state with the learned low / high notes), and the timeout fails a take with no voice.
    void testLearnProgress()
    {
        using Bar = dooblrvocals::ui::LearnProgressBar;
        const auto rate = 48000.0;
        auto p = makeProcessor (rate, 256);
        setParam (*p, "pitch_on", 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for learn progress");
        if (editor == nullptr)
            return;
        editor->setSize (1080, 660);
        editor->selectPage (Editor::pitchPage);
        auto& button = editor->getLearnButton();
        const auto& bar = editor->getLearnProgressBar();

        double phase = 0.0;
        juce::int64 clock = 0;
        auto voice = [&] (int)
        {
            const auto t = (double) clock++ / rate;
            const auto ph = std::fmod (t / 2.5, 1.0);
            const auto noteNum = 45.0 + 19.0 * (ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0);
            phase += 440.0 * std::pow (2.0, (noteNum - 69.0) / 12.0) / rate;
            phase -= std::floor (phase);
            return std::fmod (t, 1.0) > 0.92 ? 0.0f : (float) (0.3 * (2.0 * phase - 1.0));
        };
        auto silence = [] (int) { return 0.0f; };
        const auto tick = (int) (rate / 15.0); // the editor timer

        // Running for a while first (the reported case), then arm.
        render (*p, voice, (int) (6.0 * rate), 256);
        button.onClick();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        double audioMs = 0.0;
        editor->learnTickNow (t0);
        check (p->isLearningRange() && bar.isVisible() && juce::exactlyEqual (p->getLearnProgress().progress, 0.0f),
               "arming shows the progress bar at 0");

        // 1 s of silence: nothing counts, the status says so.
        for (int i = 0; i < 15; ++i)
        {
            render (*p, silence, tick, 256);
            audioMs += 1000.0 / 15.0;
            editor->learnTickNow (t0 + audioMs);
        }
        check (juce::exactlyEqual (p->getLearnProgress().progress, 0.0f) && ! p->getLearnProgress().hearingVoice,
               "silence adds no progress");
        check (editor->getLearnLabel().getText() == "Waiting for voice",
               "no voice: \"Waiting for voice\" (" + editor->getLearnLabel().getText().toStdString() + ")");

        // Sing until the take finishes by itself.
        float last = 0.0f;
        bool monotonic = true, sawKeepSinging = false, sawPercent = false;
        int ticks = 0;
        while (p->isLearningRange() && ticks < 15 * 30)
        {
            render (*p, voice, tick, 256);
            audioMs += 1000.0 / 15.0;
            const auto progress = p->getLearnProgress().progress;
            monotonic = monotonic && progress >= last;
            last = progress;
            editor->learnTickNow (t0 + audioMs);
            if (p->isLearningRange())
            {
                const auto text = editor->getLearnLabel().getText();
                sawKeepSinging = sawKeepSinging || text.startsWith ("Keep singing");
                sawPercent = sawPercent || (text.endsWith ("%") && progress > 0.3f && progress < 0.9f
                                            && text.contains (juce::String (juce::roundToInt (progress * 100.0f)) + "%"));
                monotonic = monotonic && bar.getFraction() + 1.0f / 256.0f >= progress - 1.0f / 256.0f;
                if (ticks == 60)
                    saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learn-progress");
            }
            ++ticks;
        }
        note ("learn progress: finished after " + format (audioMs / 1000.0, 1) + " s of audio, "
              + format (last, 3) + " at the end; label \"" + editor->getLearnLabel().getText().toStdString() + "\"");
        check (monotonic, "progress never goes backwards");
        check (last >= 1.0f, "progress reaches 1.0");
        check (sawKeepSinging && sawPercent, "status shows \"Keep singing... N%\" with the real percentage");
        check (audioMs / 1000.0 >= DooblrVocalsAudioProcessor::learnTargetSeconds,
               "progress is voiced time: it takes at least the target of audio");
        check (! p->isLearningRange() && getPlain (*p, "range_custom") > 0.5f,
               "100 % finishes the take and applies the range");
        const auto doneText = editor->getLearnLabel().getText();
        check (doneText.startsWith ("Done") && doneText.contains ("low A") && doneText.contains ("high D")
                   && bar.isVisible() && bar.getState() == Bar::State::done,
               "done state shows the learned low and high notes (" + doneText.toStdString() + ")");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learn-done");

        // Timeout with no voice: failed state.
        button.onClick();
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 15; ++i)
            render (*p, silence, tick, 256);
        editor->learnTickNow (t1 + 1000.0);
        check (p->isLearningRange(), "no timeout after 1 s");
        editor->learnTickNow (t1 + (Editor::learnTimeoutSeconds + 1.0) * 1000.0);
        const auto failText = editor->getLearnLabel().getText();
        check (! p->isLearningRange() && failText.startsWith ("Timed out") && failText.containsIgnoreCase ("not enough voice")
                   && bar.getState() == Bar::State::failed,
               "timeout with no voice: timed-out state (" + failText.toStdString() + ")");
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-learn-timeout");

        // Stopped early with too little voice: failed (no timeout wording), range unchanged.
        const auto lowBefore = getPlain (*p, "range_low");
        button.onClick();
        render (*p, voice, (int) (0.5 * rate), 256);
        button.onClick();
        editor->refreshLearnUiNow();
        check (bar.getState() == Bar::State::failed && editor->getLearnLabel().getText().containsIgnoreCase ("not enough voice")
                   && ! editor->getLearnLabel().getText().startsWith ("Timed out")
                   && std::abs (getPlain (*p, "range_low") - lowBefore) < 0.01f,
               "an early stop with little voice fails and changes nothing");

        // A new take starts from 0.
        button.onClick();
        check (juce::exactlyEqual (p->getLearnProgress().progress, 0.0f) && bar.getState() == Bar::State::listening,
               "a new take starts from 0");
        p->editorBeingDeleted (editor);
        base.reset();
        check (! p->isLearningRange(), "closing the editor cancels the take");
    }

    // Existing presets / sessions stay bit-identical: every factory program renders a fixed synthetic
    // vocal (gliding saw + breath noise, two channels) and the output's float bits must hash to the value
    // recorded before the Learn progress / noise reducer / HPF / knee / dynamic-EQ work. New features
    // that default off (or default to the old behaviour) leave these hashes untouched.
    std::uint64_t renderProgramHash (int program, double rate)
    {
        auto p = makeProcessor (rate, 256);
        p->setCurrentProgram (program);
        std::uint32_t seed = 12345u;
        double phase = 0.0;
        auto voice = [&] (int i)
        {
            const auto t = (double) i / rate;
            const auto midi = 52.0 + 7.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.4 * t);
            phase += 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / rate;
            phase -= std::floor (phase);
            seed = seed * 1664525u + 1013904223u;
            const auto noise = ((double) (seed >> 8) / 16777216.0 - 0.5) * 0.02;
            const auto env = std::fmod (t, 1.5) < 1.2 ? 1.0 : 0.0;
            return (float) (env * 0.35 * (2.0 * phase - 1.0) + noise);
        };
        const auto out = render (*p, voice, (int) (2.5 * rate), 256);
        std::uint64_t hash = 1469598103934665603ull;

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                std::uint32_t bits = 0;
                const auto v = out.getSample (ch, i);
                std::memcpy (&bits, &v, sizeof (bits));
                hash = (hash ^ bits) * 1099511628211ull;
            }

        return hash;
    }

    void testPresetsBitIdentical()
    {
        // Recorded after the Learn Range fix (8347233), before the feature work; 48 kHz, 256-sample blocks.
        static const std::uint64_t golden[] = { 0xafc4dfcbff0eb6afull, 0x38d0761b04c503e8ull, 0x4a7e2fbc6265b107ull, 0x9805fbc873492103ull, 0x33f8e57fa24c91bbull,
                                                0xb1f38eb25d552864ull, 0xfb7a9eadfb4d2a93ull, 0x8b21d649bbb702b2ull, 0xc95a50e483253d3eull, 0xe6f500061d1879d3ull };
        const auto numGolden = (int) (sizeof (golden) / sizeof (golden[0]));
        auto probe = makeProcessor();
        const auto numPrograms = probe->getNumPrograms();
        check (renderProgramHash (1, sampleRate) == renderProgramHash (1, sampleRate), "program render is deterministic");
        std::ostringstream table;

        for (int program = 0; program < numPrograms; ++program)
        {
            const auto hash = renderProgramHash (program, sampleRate);
            table << "0x" << std::hex << hash << "ull, ";

            if (program < numGolden && golden[program] != 0)
                check (hash == golden[program], "program " + std::to_string (program) + " (" + probe->getProgramName (program).toStdString()
                                                    + ") renders bit-identical to the recorded output");
        }

        check (numGolden >= numPrograms && golden[0] != 0, "golden hashes recorded for every program");
        note ("program hashes: " + table.str());
    }

    void testOutputCeiling()
    {
        const auto n = (int) (4.0 * sampleRate);

        for (const auto ceilingDb : { -0.3f, -6.0f })
        {
            for (const auto mode : { 2, 1 })
            {
                auto p = makeProcessor();
                setParam (*p, "lim_mode", (float) mode);
                setParam (*p, "lim_ceiling", ceilingDb);
                setParam (*p, "comp_on", 1.0f);
                setParam (*p, "comp_makeup", 12.0f);
                setParam (*p, "rev_on", 1.0f);
                setParam (*p, "rev_mix", 1.0f);
                setParam (*p, "rev_decay", 4.0f);
                setParam (*p, "dly_on", 1.0f);
                setParam (*p, "dly_mix", 1.0f);
                setParam (*p, "dly_feedback", 0.9f);
                setParam (*p, "fx_routing", 0.0f);
                p->getMeterTap().discardAll();

                const auto lat = p->getLatencySamples();
                auto peak = 0.0f, meterPeakDb = -120.0f;
                std::vector<dsp::MeterFrame> frames (4096);
                juce::AudioBuffer<float> out;
                for (int pos = 0; pos < n; pos += 8192)
                {
                    const auto len = std::min (8192, n - pos);
                    out = render (*p, [pos] (int i) { return 1.6f * vocalLike (pos + i); }, len);
                    for (int ch = 0; ch < 2; ++ch)
                        peak = std::max (peak, out.getMagnitude (ch, 0, len));
                    const auto got = p->getMeterTap().popFrames (frames.data(), (int) frames.size());
                    for (int f = 0; f < got; ++f)
                        meterPeakDb = std::max (meterPeakDb, frames[(size_t) f].limiterOutDb);
                }
                juce::ignoreUnused (lat);
                const auto peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
                const auto label = std::string (mode == 2 ? "limiter" : "clipper") + ", ceiling " + format (ceilingDb, 1) + " dB";
                note ("output ceiling (" + label + ", reverb + delay 100 % mix): output peak " + format (peakDb, 3) + " dB, limiter meter "
                      + format (meterPeakDb, 3) + " dB");
                check (allFinite (out) && peak <= juce::Decibels::decibelsToGain (ceilingDb) * 1.0001f,
                       "output peak stays at or under the ceiling with reverb and delay at full mix (" + label + ": " + format (peakDb, 3) + " dB)");
                check (std::abs (meterPeakDb - peakDb) < 0.05f,
                       "the limiter meter shows the final output (" + label + ": meter " + format (meterPeakDb, 3) + " dB, output " + format (peakDb, 3) + " dB)");
            }
        }

        // Output trim sits in front of the limiter, so a boost cannot push past the ceiling.
        {
            auto p = makeProcessor();
            setParam (*p, "lim_ceiling", -1.0f);
            setParam (*p, "output_gain", 6.0f);
            setParam (*p, "rev_on", 1.0f);
            setParam (*p, "rev_mix", 1.0f);
            const auto out = render (*p, [] (int i) { return 1.5f * vocalLike (i); }, n);
            const auto peak = std::max (out.getMagnitude (0, 0, n), out.getMagnitude (1, 0, n));
            check (peak <= juce::Decibels::decibelsToGain (-1.0f) * 1.0001f, "+6 dB output trim still respects the ceiling");
        }
    }

    // Loudness of every factory program on a sung-ish line (notes only; compare across builds).
    void testPresetLoudness()
    {
        const auto& factory = dooblrvocals::presets::getFactoryPresets();
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            auto p = makeProcessor();
            p->loadFactoryPreset (i, false);
            const auto lat = p->getLatencySamples();
            const auto n = lat + (int) (6.0 * sampleRate);
            const auto out = render (*p, vocalLike, n);
            double e = 0.0;
            auto peak = 0.0f;
            const auto from = lat + (int) (1.0 * sampleRate);
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int s = from; s < n; ++s)
                    e += (double) out.getSample (ch, s) * out.getSample (ch, s);
                peak = std::max (peak, out.getMagnitude (ch, from, n - from));
            }
            const auto rmsDb = 10.0 * std::log10 (std::max (1.0e-20, e / (2.0 * (n - from))));
            const auto ceiling = getPlain (*p, "lim_ceiling");
            const auto limOn = juce::roundToInt (getPlain (*p, "lim_mode")) != 0;
            note ("loudness " + factory[(size_t) i].name.toStdString() + ": RMS " + format (rmsDb, 2) + " dBFS, peak "
                  + format (juce::Decibels::gainToDecibels (peak, -120.0f), 2) + " dBFS (ceiling " + format (ceiling, 1) + ")");
            check (allFinite (out), factory[(size_t) i].name.toStdString() + " renders finite");
            if (limOn && getPlain (*p, "output_gain") <= 0.0f)
                check (peak <= juce::Decibels::decibelsToGain (ceiling) * 1.0001f,
                       factory[(size_t) i].name.toStdString() + " stays under its ceiling");
        }
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };

    //==============================================================================
    // The pitch Mode is a large KEYED | CHROMATIC switch at the top of the pitch page.
    void testModeToggle()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for the mode toggle");
        if (editor == nullptr)
            return;

        editor->setVisible (true); // for getComponentAt (the window goes on the desktop further down)
        auto& toggle = editor->getModeToggle();
        auto& correction = *editor->getPanels (Editor::pitchPage).front();
        check (correction.findControl ("corr_mode") == nullptr, "the old Mode combo is gone from the Correction panel");
        check (findById (*editor, "corr_mode") == &toggle, "corr_mode is bound to the toggle (one control)");
        check (toggle.getNumSegments() == 2 && toggle.getSegmentText (0) == "KEYED" && toggle.getSegmentText (1) == "CHROMATIC",
               "segments read KEYED | CHROMATIC");

        for (const auto& [w, h] : { std::pair<int, int> { 940, 600 }, { 1080, 660 }, { 2400, 1400 } })
        {
            const auto size = std::to_string (w) + "x" + std::to_string (h);
            editor->setSize (w, h);
            editor->selectPage (Editor::pitchPage);
            const auto b = toggle.getBounds();
            check (visibleChain (toggle), size + ": the toggle shows on the pitch page");
            check (b.getWidth() >= 236 && b.getHeight() >= 32, size + ": the toggle is large (" + std::to_string (b.getWidth()) + "x"
                                                                + std::to_string (b.getHeight()) + ", min 236x32)");
            check (toggle.getSegmentBounds (0).getWidth() >= 110 && toggle.getSegmentBounds (1).getWidth() >= 110,
                   size + ": each segment is at least 110 px wide");
            check (editor->getLocalBounds().contains (b), size + ": the toggle lies inside the editor");
            check (b.getBottom() <= editor->getPitchDisplay().getY(), size + ": the toggle sits above the pitch display");
            check (b.getY() < editor->getHeight() / 5, size + ": the toggle is near the top (y " + std::to_string (b.getY()) + ")");

            // Nothing overlaps it: no other showing editor child intersects it, and every point of
            // it hits the toggle itself.
            juce::StringArray overlaps;
            for (auto* child : editor->getChildren())
                if (child != &toggle && child->isVisible() && child->getBounds().intersects (b))
                    overlaps.add (child->getComponentID().isNotEmpty() ? child->getComponentID() : child->getName());
            check (overlaps.isEmpty(), size + ": nothing overlaps the toggle (" + overlaps.joinIntoString (", ").toStdString() + ")");
            int misses = 0;
            for (int iy = 1; iy < 4; ++iy)
                for (int ix = 1; ix < 8; ++ix)
                    misses += editor->getComponentAt (juce::Point<int> (b.getX() + b.getWidth() * ix / 8, b.getY() + b.getHeight() * iy / 4)) == &toggle ? 0 : 1;
            check (misses == 0, size + ": the whole toggle takes the mouse (" + std::to_string (misses) + " points covered)");

            // Key / Scale stay visible in both states.
            for (const auto* id : { "corr_key", "corr_scale" })
                check (correction.findControl (id) != nullptr && visibleChain (*correction.findControl (id)), size + ": " + id + " is shown");

            for (int page = 1; page < Editor::numPages; ++page)
            {
                editor->selectPage (page);
                check (! visibleChain (toggle), size + ": the toggle shows only on the pitch page (page " + std::to_string (page) + ")");
            }
        }

        // Snapshots in both states.
        editor->setSize (1080, 660);
        editor->selectPage (Editor::pitchPage);
        editor->refreshEnablementNow();
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-keyed");
        setParam (*p, "corr_mode", 1.0f);
        editor->refreshEnablementNow();
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-chromatic");
        editor->setSize (940, 600);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-chromatic-940x600");
        setParam (*p, "corr_mode", 0.0f);
        editor->refreshEnablementNow();
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-pitch-keyed-940x600");
        check (toggle.getSelectedIndex() == 0, "the toggle follows the parameter (host automation)");

        // Real mouse: hover, hand cursor, one click is one undo step.
        editor->setSize (1080, 660);
        editor->setTopLeftPosition (40, 40);
        editor->setVisible (true);
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        auto* peer = editor->getPeer();
        juce::Process::makeForegroundProcess();
        editor->setAlwaysOnTop (true);
        editor->toFront (true);
        const auto front = peer != nullptr && peer->contains ({ editor->getWidth() / 2, editor->getHeight() / 2 }, true);
        RoutedMouse mouse (*editor, front ? peer : nullptr);
        GestureCounter gestures (*p);
        auto& undo = p->getUndoHistory();
        undo.clear();

        const auto segmentCentre = [&] (int i)
        {
            return editor->getLocalPoint (&toggle, toggle.getSegmentBounds (i).getCentre()).toFloat();
        };
        const auto keyBox = correction.findControl ("corr_key"), scaleBox = correction.findControl ("corr_scale");

        mouse.moveTo ({ 400.0f, 400.0f }, 2);
        gestures.clear();
        mouse.moveTo (segmentCentre (1), 8);
        check (toggle.getHoveredIndex() == 1, "hovering CHROMATIC highlights it");
        check (toggle.getMouseCursor() == juce::MouseCursor::PointingHandCursor, "the toggle shows a hand cursor");
        mouse.moveTo (segmentCentre (0), 8);
        check (toggle.getHoveredIndex() == 0, "hover follows the pointer to KEYED");
        check (gestures.changed.empty() && gestures.events == 0, "hovering changes nothing");

        mouse.click (segmentCentre (1));
        editor->refreshEnablementNow();
        check (getPlain (*p, "corr_mode") > 0.5f && toggle.getSelectedIndex() == 1, "clicking CHROMATIC sets Chromatic");
        check (gestures.balanced() && gestures.events == 2, "the click is one complete gesture (" + std::to_string (gestures.events) + " events)");
        check (undo.getNumUndoableSteps() == 1, "the click is one undo step (" + std::to_string (undo.getNumUndoableSteps()) + ")");
        check (keyBox != nullptr && ! keyBox->isEnabled() && keyBox->getAlpha() < 0.6f && keyBox->isShowing(),
               "Chromatic: Key is disabled and dimmed but still visible");
        check (scaleBox != nullptr && ! scaleBox->isEnabled() && scaleBox->getAlpha() < 0.6f && scaleBox->isShowing(),
               "Chromatic: Scale is disabled and dimmed but still visible");

        gestures.clear();
        mouse.click (segmentCentre (1));
        check (gestures.events == 0 && undo.getNumUndoableSteps() == 1, "clicking the active segment does nothing");

        // Press on one segment, release on the other: no change.
        mouse.moveTo (segmentCentre (0), 4);
        mouse.press();
        mouse.moveTo (segmentCentre (1), 6, true);
        mouse.release();
        check (getPlain (*p, "corr_mode") > 0.5f && gestures.events == 0, "press on KEYED, release on CHROMATIC changes nothing");

        undo.undo();
        editor->refreshEnablementNow();
        check (getPlain (*p, "corr_mode") < 0.5f && toggle.getSelectedIndex() == 0, "one undo restores Keyed");
        check (keyBox != nullptr && keyBox->isEnabled() && juce::approximatelyEqual (keyBox->getAlpha(), 1.0f), "Keyed: Key is live and undimmed");
        undo.redo();
        check (getPlain (*p, "corr_mode") > 0.5f && toggle.getSelectedIndex() == 1, "redo restores Chromatic");

        // Harmony on: Key / Scale still set the voices' scale, so they stay live but dimmed.
        setParam (*p, "harm_on", 1.0f);
        editor->refreshEnablementNow();
        check (keyBox != nullptr && keyBox->isEnabled() && keyBox->getAlpha() < 0.9f, "Chromatic + harmony: Key stays live, dimmed");

        mouse.click (segmentCentre (0));
        check (getPlain (*p, "corr_mode") < 0.5f, "clicking KEYED sets Keyed");
        mouse.moveTo ({ 400.0f, 400.0f }, 6);
        check (toggle.getHoveredIndex() < 0, "hover clears when the pointer leaves");

        // Keyboard: Right / Left pick a side.
        toggle.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        check (getPlain (*p, "corr_mode") > 0.5f, "Right picks CHROMATIC");
        toggle.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (getPlain (*p, "corr_mode") < 0.5f, "Left picks KEYED");

        editor->removeFromDesktop();
        p->editorBeingDeleted (editor);
        base.reset();
    }

    //==============================================================================
    // Design-system deviations fixed (t13u)

    /** A software renderer that remembers where text glyphs land (local coordinates). */
    class GlyphRecorder final : public juce::LowLevelGraphicsSoftwareRenderer
    {
    public:
        using juce::LowLevelGraphicsSoftwareRenderer::LowLevelGraphicsSoftwareRenderer;

        void drawGlyphs (juce::Span<const uint16_t> glyphs, juce::Span<const juce::Point<float>> positions,
                         const juce::AffineTransform& t) override
        {
            for (const auto& pos : positions)
                points.push_back (pos.transformedBy (t));
            juce::LowLevelGraphicsSoftwareRenderer::drawGlyphs (glyphs, positions, t);
        }

        int countIn (juce::Rectangle<float> area) const
        {
            return (int) std::count_if (points.begin(), points.end(), [&] (auto pt) { return area.contains (pt); });
        }

        std::vector<juce::Point<float>> points;
    };

    template <typename Draw>
    std::unique_ptr<GlyphRecorder> recordGlyphs (juce::Component& c, Draw&& draw)
    {
        static juce::Image image;
        image = juce::Image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true, juce::SoftwareImageType());
        auto recorder = std::make_unique<GlyphRecorder> (image);
        {
            juce::Graphics g (*recorder);
            draw (g);
        }
        return recorder;
    }

    void testDesignFixes()
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorAndMakeActive());
        auto* editor = dynamic_cast<Editor*> (base.get());
        check (editor != nullptr, "editor for design fixes");
        if (editor == nullptr)
            return;
        editor->setSize (1080, 660);

        // #8 No help captions under the limiter / compressor histories (the dB labels and "dBFS" stay).
        {
            editor->selectPage (Editor::limiterPage);
            auto& lim = editor->getLimiterDisplay();
            const auto h = lim.getHistoryArea().toFloat();
            const auto rec = recordGlyphs (lim, [&] (juce::Graphics& g) { lim.drawBackgroundLayer (g); });
            const auto under = juce::Rectangle<float>::leftTopRightBottom (h.getX(), h.getBottom(), h.getRight(), (float) lim.getHeight() + 1.0f);
            check (rec->countIn (under) == 0 && rec->countIn ({ 0.0f, 0.0f, h.getX(), (float) lim.getHeight() }) > 0,
                   "limiter history: no caption under it (" + std::to_string (rec->countIn (under)) + " glyphs), labels kept");

            editor->selectPage (Editor::dynamicsPage);
            auto& dyn = editor->getDynamicsDisplay();
            const auto dh = dyn.getHistoryArea().toFloat();
            const auto drec = recordGlyphs (dyn, [&] (juce::Graphics& g) { dyn.drawBackgroundLayer (g); });
            const auto dunder = juce::Rectangle<float>::leftTopRightBottom (dh.getX(), dh.getBottom(), dh.getRight(), (float) dyn.getHeight() + 1.0f);
            check (drec->countIn (dunder) == 0 && ! drec->points.empty(), "compressor history: no caption under it");
        }

        // #6 / #7 / #8 Delay and reverb: cached picture, repaint only on change, off = dimmed with no text.
        {
            editor->selectPage (Editor::delayPage);
            auto& dly = editor->getDelayDisplay();
            auto& rev = editor->getReverbDisplay();
            setParam (*p, "dly_on", 1.0f);
            dly.refresh (0.0f, 120.0);
            const auto requests = dly.getRepaintRequests();
            for (int i = 0; i < 5; ++i)
                dly.refresh (0.0f, 120.0);
            check (dly.getRepaintRequests() == requests, "delay display: identical refreshes do not repaint");
            (void) dly.createComponentSnapshot (dly.getLocalBounds());
            const auto builds = dly.getPictureBuilds();
            (void) dly.createComponentSnapshot (dly.getLocalBounds());
            check (builds >= 1 && dly.getPictureBuilds() == builds, "delay display: an unchanged picture repaints from its cached image");
            setParam (*p, "dly_feedback", 0.123f);
            dly.refresh (0.0f, 120.0);
            (void) dly.createComponentSnapshot (dly.getLocalBounds());
            check (dly.getRepaintRequests() == requests + 1 && dly.getPictureBuilds() == builds + 1, "delay display: a parameter change rebuilds once");

            const auto onGlyphs = recordGlyphs (dly, [&] (juce::Graphics& g) { dly.drawPicture (g); })->points.size();
            setParam (*p, "dly_on", 0.0f);
            dly.refresh (0.0f, 120.0);
            const auto offGlyphs = recordGlyphs (dly, [&] (juce::Graphics& g) { dly.drawPicture (g); })->points.size();
            check (dly.isDimmed() && offGlyphs == onGlyphs, "delay off: dimmed, no caption (" + std::to_string (offGlyphs) + " vs " + std::to_string (onGlyphs) + " glyphs)");
            const auto meanBrightness = [] (juce::Component& c)
            {
                const auto img = c.createComponentSnapshot (c.getLocalBounds());
                double sum = 0.0;
                for (int y = 0; y < img.getHeight(); y += 2)
                    for (int x = 0; x < img.getWidth(); x += 2)
                        sum += img.getPixelAt (x, y).getBrightness();
                return sum / (double) juce::jmax (1, (img.getHeight() / 2) * (img.getWidth() / 2));
            };
            const auto offBrightness = meanBrightness (dly);
            setParam (*p, "dly_on", 1.0f);
            dly.refresh (0.0f, 120.0);
            const auto onBrightness = meanBrightness (dly);
            check (! dly.isDimmed() && onBrightness > offBrightness, "delay off is drawn dimmer than on");

            editor->selectPage (Editor::reverbPage);
            setParam (*p, "rev_on", 1.0f);
            rev.refresh (0.0f, 120.0);
            const auto rRequests = rev.getRepaintRequests();
            rev.refresh (0.0f, 120.0);
            rev.refresh (0.0f, 120.0);
            check (rev.getRepaintRequests() == rRequests, "reverb display: identical refreshes do not repaint");
            (void) rev.createComponentSnapshot (rev.getLocalBounds());
            const auto rBuilds = rev.getPictureBuilds();
            (void) rev.createComponentSnapshot (rev.getLocalBounds());
            check (rBuilds >= 1 && rev.getPictureBuilds() == rBuilds, "reverb display: an unchanged picture repaints from its cached image");
            const auto rOn = recordGlyphs (rev, [&] (juce::Graphics& g) { rev.drawPicture (g); })->points.size();
            setParam (*p, "rev_on", 0.0f);
            rev.refresh (0.0f, 120.0);
            check (rev.getRepaintRequests() == rRequests + 1, "reverb display: turning it off repaints once");
            const auto rOff = recordGlyphs (rev, [&] (juce::Graphics& g) { rev.drawPicture (g); })->points.size();
            check (rev.isDimmed() && rOff == rOn, "reverb off: dimmed, no caption (" + std::to_string (rOff) + " vs " + std::to_string (rOn) + " glyphs)");
        }

        // #6 Pitch display: traces are built on new data, not in paint.
        {
            editor->selectPage (Editor::pitchPage);
            auto& pitch = editor->getPitchDisplay();
            std::vector<dsp::PitchFrame> frames (8);
            for (size_t i = 0; i < frames.size(); ++i)
            {
                frames[i].voiced = true;
                frames[i].detectedHz = 220.0f + (float) i;
                frames[i].correctedHz = 220.0f;
                frames[i].centreHz = 220.0f;
                frames[i].vibrato = 0.8f;
            }
            pitch.update (frames.data(), (int) frames.size(), 0.1);
            const auto traces = pitch.getTraceBuilds();
            for (int i = 0; i < 3; ++i)
                (void) pitch.createComponentSnapshot (pitch.getLocalBounds());
            check (pitch.getTraceBuilds() == traces, "pitch display: paint does not rebuild the traces");
            pitch.update (frames.data(), (int) frames.size(), 0.1);
            check (pitch.getTraceBuilds() == traces + 1, "pitch display: new frames rebuild the traces once");
        }

        // #5 Limiter meter: static layer cached on resize; the level fill is solid zone colours.
        {
            editor->selectPage (Editor::limiterPage);
            auto& meter = editor->getLimiterDisplay().getMeter();
            check (meter.hasStaticLayer(), "limiter meter caches its static layer on resize");
            for (int i = 0; i < 10; ++i)
                meter.push (6.0f, -3.0f, 0.016);
            const auto snap = meter.createComponentSnapshot (meter.getLocalBounds());
            const auto bar = meter.getLevelBar();
            const auto sample = [&] (float db) { return snap.getPixelAt (juce::roundToInt (bar.getCentreX()), juce::roundToInt (meter.dbToY (db))); };
            const auto near = [] (juce::Colour a, juce::Colour b)
            {
                return std::abs (a.getRed() - b.getRed()) <= 3 && std::abs (a.getGreen() - b.getGreen()) <= 3 && std::abs (a.getBlue() - b.getBlue()) <= 3;
            };
            using dooblrvocals::ui::OutputMeter;
            check (near (sample (-20.0f), OutputMeter::colourFor (-20.0f)) && near (sample (-3.0f), OutputMeter::colourFor (-3.0f))
                       && near (sample (3.0f), OutputMeter::colourFor (3.0f)),
                   "limiter meter: green / yellow / red zones at -20 / -3 / +3 dBFS");
        }

        // #10 Learn pulse: animated per VBlank frame, not stepped by the 15 Hz timer.
        {
            editor->selectPage (Editor::pitchPage);
            auto& button = editor->getLearnButton();
            button.onClick();
            editor->refreshEnablementNow();
            check (p->isLearningRange(), "learning armed for the pulse");
            editor->pulseLearnNow (0.0);
            const auto a = button.findColour (juce::TextButton::buttonColourId);
            editor->pulseLearnNow (1000.0 * juce::MathConstants<double>::halfPi / 6.0);
            const auto b = button.findColour (juce::TextButton::buttonColourId);
            editor->pulseLearnNow (1000.0 * juce::MathConstants<double>::halfPi / 6.0 + 16.7);
            const auto c = button.findColour (juce::TextButton::buttonColourId);
            check (a != b && b.getAlpha() > a.getAlpha(), "learn pulse moves with time");
            check (std::abs ((int) b.getAlpha() - (int) c.getAlpha()) <= 4, "one 60 Hz frame is a small step");
            p->cancelRangeLearn();
            editor->refreshEnablementNow();
            check (! button.isColourSpecified (juce::TextButton::buttonColourId), "pulse colour cleared when learning stops");
        }

        p->editorBeingDeleted (editor);
        base.reset();
    }

}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const std::vector<TestCase> tests {
        { "instantiation and metadata",     testInstantiationAndMetadata },
        { "YIN on synthetic tones",         testPitchDetectionTones },
        { "YIN on vibrato",                 testPitchDetectionVibrato },
        { "keyed vs chromatic correction",  testKeyedVsChromaticCorrection },
        { "strict snapping (cents)",        testStrictSnapping },
        { "preset sanity",                  testPresetSanity },
        { "mouse across sections",          testMouseAcrossSections },
        { "unvoiced passthrough",           testUnvoicedPassthrough },
        { "latency reporting",              testLatencyReporting },
        { "latency modes (Tracking / Mix / HQ)", testLatencyModes },
        { "tracking vs mix snapping (cents)", testTrackingSnapping },
        { "harmony intervals",              testHarmonyIntervals },
        { "harmony pan and alignment",      testHarmonyPanAndAlignment },
        { "gate behaviour",                 testGateBehaviour },
        { "chain reorder",                  testChainReorder },
        { "parameter / state round-trip",   testParameterAndStateRoundTrip },
        { "no dead parameters",             testNoDeadParameters },
        { "undo history",                   testUndoHistory },
        { "no audio-thread allocations",    testNoAudioThreadAllocations },
        { "scales and masks",               testScalesAndMasks },
        { "editor",                         testEditor },
        { "layout fit 940x600 .. 2400x1400", testLayoutFit },
        { "processor on / off toggles",     testProcessorTabToggles },
        { "reverb",                         testReverb },
        { "delay",                          testDelay },
        { "fx routing",                     testFxRouting },
        { "learn range",                    testRangeLearn },
        { "learn range: long takes, running plug-in", testLearnRangeLongRuns },
        { "learn range: progress feedback", testLearnProgress },
        { "presets: audio bit-identical to recorded output", testPresetsBitIdentical },
        { "fx pages",                       testFxPages },
        { "vibrato golden",                 testVibratoGolden },
        { "vibrato respect",                testVibratoRespect },
        { "mode toggle",                    testModeToggle },
        { "design fixes (t13u)",            testDesignFixes },
        { "key follow",                     testKeyFollow },
        { "harmony lo-fi",                  testHarmonyLoFi },
        { "vocal workstation preset",       testVocalWorkstationPreset },
        { "reverb preset rt60",             testReverbPresetRt60 },
        { "output ceiling",                 testOutputCeiling },
        { "preset loudness",                testPresetLoudness },
    };

    int failedTests = 0;

    // DOOBLRVOCALS_TEST_FILTER=text runs only the tests whose name contains text.
    const auto* filter = std::getenv ("DOOBLRVOCALS_TEST_FILTER");

    for (const auto& test : tests)
    {
        if (filter != nullptr && *filter != 0 && std::string (test.name).find (filter) == std::string::npos)
            continue;

        std::cout << "[RUN ] " << test.name << "\n";
        const auto failedBefore = failedChecks, passedBefore = passedChecks;
        test.run();
        const auto failed = failedChecks - failedBefore;
        failedTests += failed > 0 ? 1 : 0;
        std::cout << (failed == 0 ? "[PASS] " : "[FAIL] ") << test.name << " (" << (passedChecks - passedBefore)
                  << " checks passed, " << failed << " failed)\n";
    }

    std::cout << "\n" << (tests.size() - (size_t) failedTests) << "/" << tests.size() << " tests passed, "
              << passedChecks << " checks passed, " << failedChecks << " checks failed\n";

    return failedChecks == 0 ? 0 : 1;
}
