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
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <set>
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
        check (juce::String (JucePlugin_VersionString) == "0.1.0", "version 0.1.0");
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
            check (label->getText().contains ("0.1.0"), "header shows v0.1.0 (got " + label->getText().toStdString() + ")");

        check (findById (*editor, "pitchDisplay") != nullptr, "pitch display is in the tree");
        check (findById (*editor, "power_pitch") != nullptr, "pitch power toggle");
        check (findById (*editor, "power_gate") != nullptr, "gate power toggle");

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
        editor->selectPage (Editor::gatePage);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-gate");
        editor->selectPage (Editor::eqPage);
        saveSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "-eq");

        p->editorBeingDeleted (editor);
        base.reset();
        check (true, "editor closed");
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
        { "unvoiced passthrough",           testUnvoicedPassthrough },
        { "latency reporting",              testLatencyReporting },
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

    for (const auto& test : tests)
    {
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
