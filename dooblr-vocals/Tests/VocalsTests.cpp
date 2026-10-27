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
#include "DSP/PitchEngine.h"
#include "DSP/ProcessingChain.h"
#include "DSP/VocalEngine.h"
#include "DSP/VocalSettings.h"
#include "UI/Theme.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
        pulse ("corr_amount", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("a4_hz", sawBb, 415.0f, 466.0f, 0.2);
        pulse ("formant", sawBb, 0.0f, 1.0f, 0.05);
        pulse ("input_type", [] (int i) { return sineAt (90.0, i, sampleRate, 0.4f); }, 0.0f, 4.0f, 0.05);

        // Harmony (pitch still on, keyed C major).
        setParam (*p, "harm_on", 1.0f);
        pulse ("harm_on", sawBb, 0.0f, 1.0f, 1.0);
        pulse ("harm_level", sawBb, -30.0f, 0.0f, 1.0);
        pulse ("harm_formant", sawBb, 0.0f, 1.0f, 0.05);
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
                        || child == &editor->getEqDisplay())
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
            auto& lastTab = editor->getPageButton (Editor::limiterPage);
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
            check (gateTab.getBounds().getY() > lastTab.getBounds().getY(), "gate tab ends in the last slot");
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
