/*
    dooblr-kick frame-time bench.

    Opens the real plugin editor in an on-screen window and, on every display vertical blank,
    moves a simulated mouse drag one step (a rotary knob, or a Body pitch-envelope point),
    exactly as a user dragging continuously would. It records, per frame:

      - input ms   : the mouse-drag handler (slider / envelope -> parameter -> listeners)
      - paint ms   : the editor repaint the OS performs for that frame (root paint start to
                     paintOverChildren end, i.e. every dirty component)
      - interval ms: time between consecutive vblank callbacks. Anything else the message
                     thread does (timers, preview work) shows up here as long intervals.
      - background previews: waveform renders the worker thread finished during the drag.

    v0.3.0 adds: masterenv (a master envelope point), drywet (the Dry/Wet view), drive (the
    transfer curve) and chain (dragging a processing tab by its handle up and down past its
    neighbours, which reorders the live chain on every crossing).

    The EQ / Dynamics / Limiter and v0.3.0 scenarios also play the processor on a separate "audio" thread
    in real time (512-sample blocks at 48 kHz, a hit every 250 ms), so the live views animate
    from the meter tap while a handle / threshold / ceiling is dragged. "view frames" counts
    the meter frames (or FFT frames for the EQ) the view consumed during the scenario.

    Usage: DooblrKickFrameBench [frames-per-scenario]
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    juce::MouseEvent makeMouseEvent (juce::Component& component, juce::Point<float> position,
                                     juce::Point<float> downPosition, bool dragged)
    {
        using Source = juce::MouseInputSource;
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position,
                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 Source::defaultPressure, Source::defaultOrientation, Source::defaultRotation,
                 Source::defaultTiltX, Source::defaultTiltY,
                 &component, &component, now, downPosition, now, 1, dragged };
    }

    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->getComponentID() == id && child->isShowing())
                return child;

            if (auto* found = findById (*child, id))
                return found;
        }

        return nullptr;
    }

    struct Summary { double mean = 0, p95 = 0, max = 0; };

    Summary summarise (std::vector<double> v)
    {
        Summary s;

        if (v.empty())
            return s;

        std::sort (v.begin(), v.end());
        for (auto x : v) s.mean += x;
        s.mean /= (double) v.size();
        s.p95 = v[(size_t) std::min (v.size() - 1, (size_t) std::ceil (0.95 * (double) v.size()) - 1)];
        s.max = v.back();
        return s;
    }

    /** Root component of the window: times the whole paint of each frame.

        Uses JUCE's paint diagnostics (ComponentListener::componentPainted) for the root, whose
        total paint duration covers every dirty component painted in that window paint. (Timing
        root paint() -> paintOverChildren() is wrong once children are opaque: JUCE then skips
        the root's paint() and the start time goes stale.) */
    class Root final : public juce::Component, private juce::ComponentListener
    {
    public:
        Root() { addComponentListener (this); }
        ~Root() override { removeComponentListener (this); }

        double lastPaintMs = 0;
        int paints = 0;

    private:
        void componentPainted (juce::Component&, const juce::ComponentPaintDiagnostics& d) override
        {
            lastPaintMs += d.totalPaintDuration.get<std::chrono::duration<double, std::milli>>();
            ++paints;
        }
    };

    enum class Kind { knob, envelope, eq, dynamics, limiter, masterEnvelope, mix, drive, chain };
    struct Scenario { int width, height; Kind kind; };

    const char* kindName (Kind k)
    {
        switch (k)
        {
            case Kind::knob:     return "knob";
            case Kind::envelope: return "envelope";
            case Kind::eq:       return "eq";
            case Kind::dynamics: return "dynamics";
            case Kind::limiter:  return "limiter";
            case Kind::masterEnvelope: return "masterenv";
            case Kind::mix:      return "drywet";
            case Kind::drive:    return "drive";
            case Kind::chain:    return "chain";
        }
        return "?";
    }

    /** Plays the processor in real time like a host's audio thread. */
    class AudioFeeder final : public juce::Thread
    {
    public:
        explicit AudioFeeder (DooblrKickAudioProcessor& p) : juce::Thread ("bench audio"), processor (p) {}
        ~AudioFeeder() override { stopThread (2000); }

        void run() override
        {
            constexpr int block = 512, hitEvery = 12000;
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            juce::int64 position = 0;
            auto due = nowMs();

            while (! threadShouldExit())
            {
                midi.clear();
                const auto nextHit = (position + hitEvery - 1) / hitEvery * hitEvery;
                if (nextHit < position + block)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), (int) (nextHit - position));

                buffer.clear();
                processor.processBlock (buffer, midi);
                position += block;
                due += 1000.0 * block / 48000.0;
                const auto wait = due - nowMs();
                if (wait > 0.5)
                    juce::Thread::sleep ((int) wait);
            }
        }

    private:
        DooblrKickAudioProcessor& processor;
    };

    class Bench final : private juce::Timer
    {
    public:
        explicit Bench (int framesToRun) : frames (framesToRun)
        {
            processor.prepareToPlay (48000.0, 512);
            processor.setCurrentProgram (2); // Techno Rumble: Drive, compressor, a long sample tail
            editor.reset (static_cast<DooblrKickAudioProcessorEditor*> (processor.createEditorAndMakeActive()));

            root.setOpaque (true);
            root.addAndMakeVisible (*editor);
            root.addToDesktop (0); // borderless, so large sizes are not constrained to the screen
            root.setTopLeftPosition (20, 40);
            root.setVisible (true);

            if (auto* peer = root.getPeer())
                std::printf ("display scale %.1f, window peer ok\n", (double) peer->getPlatformScaleFactor());

            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display != nullptr)
                std::printf ("primary display %dx%d @%.1fx, %.0f Hz\n", display->totalArea.getWidth(), display->totalArea.getHeight(),
                             display->scale, display->verticalFrequencyHz.value_or (0.0));

            // Preview render cost (what the waveform does on every parameter change).
            std::vector<double> renders;
            for (int i = 0; i < 10; ++i)
            {
                const auto t0 = nowMs();
                juce::ignoreUnused (processor.renderPreview (48000.0));
                renders.push_back (nowMs() - t0);
            }
            const auto r = summarise (renders);
            std::printf ("preview render (Techno Rumble, 48 kHz): mean %.2f ms, max %.2f ms\n", r.mean, r.max);

            for (const auto [w, h] : { std::pair { 1080, 660 }, std::pair { 2400, 1400 } })
                for (auto kind : { Kind::knob, Kind::envelope, Kind::eq, Kind::dynamics, Kind::limiter,
                                    Kind::masterEnvelope, Kind::mix, Kind::drive, Kind::chain })
                    scenarios.push_back ({ w, h, kind });
            startTimer (300);
        }

        ~Bench() override
        {
            vblank = {};
            audio.stopThread (2000);
            root.removeAllChildren();
            editor.reset();
        }

    private:
        void timerCallback() override
        {
            // Between scenarios: set up the next one once the window has settled.
            stopTimer();

            if (current >= (int) scenarios.size())
            {
                juce::JUCEApplicationBase::quit();
                return;
            }

            const auto& sc = scenarios[(size_t) current];
            root.setSize (sc.width, sc.height);
            editor->setBounds (root.getLocalBounds());
            using E = DooblrKickAudioProcessorEditor;
            editor->selectPage (sc.kind == Kind::eq ? E::eqPage : sc.kind == Kind::dynamics ? E::dynamicsPage
                                : sc.kind == Kind::limiter ? E::outputPage
                                : (sc.kind == Kind::masterEnvelope || sc.kind == Kind::mix) ? E::envelopePage
                                : (sc.kind == Kind::drive || sc.kind == Kind::chain) ? E::drivePage : 1);

            switch (sc.kind)
            {
                case Kind::knob:     target = findById (*editor, "body_tune"); break;
                case Kind::envelope: target = &editor->getEnvelopeEditor(); break;
                case Kind::eq:       target = &editor->getEqDisplay(); break;
                case Kind::dynamics: target = &editor->getDynamicsDisplay(); break;
                case Kind::limiter:  target = &editor->getLimiterDisplay(); break;
                case Kind::masterEnvelope: target = &editor->getMasterEnvelopeEditor(); break;
                case Kind::mix:      target = &editor->getMixDisplay(); break;
                case Kind::drive:    target = &editor->getDriveDisplay(); break;
                case Kind::chain:    target = &editor->getPageButton (E::pageForStage (editor->getShownChainOrder()[1])); break;
            }

            if (target == nullptr)
            {
                std::printf ("FAIL: drag target not found\n");
                juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (1);
                juce::JUCEApplicationBase::quit();
                return;
            }

            switch (sc.kind)
            {
                case Kind::knob:     downPos = target->getLocalBounds().toFloat().getCentre(); break;
                case Kind::envelope: downPos = editor->getEnvelopeEditor().getNodePosition (1); break;
                case Kind::eq:       downPos = editor->getEqDisplay().getHandlePosition (dooblrkick::ui::EqDisplay::mid); break;
                case Kind::dynamics: downPos = { editor->getDynamicsDisplay().getHistoryArea().toFloat().getCentreX(),
                                                 editor->getDynamicsDisplay().getThresholdLineY() }; break;
                case Kind::limiter:  downPos = { editor->getLimiterDisplay().getHistoryArea().toFloat().getCentreX(),
                                                 editor->getLimiterDisplay().getCeilingY() }; break;
                case Kind::masterEnvelope: downPos = editor->getMasterEnvelopeEditor().getNodePosition (1) - juce::Point<float> (60.0f, 60.0f); break;
                case Kind::mix:      downPos = editor->getMixDisplay().getPlotArea().getCentre(); break;
                case Kind::drive:    downPos = editor->getDriveDisplay().getTransferArea().getCentre(); break;
                case Kind::chain:
                {
                    auto& button = editor->getPageButton (E::pageForStage (editor->getShownChainOrder()[1]));
                    downPos = button.getHandleArea().toFloat().getCentre();
                    chainDownInEditor = button.getPosition().toFloat() + downPos;
                    chainPitch = (float) (editor->getPageButton (E::pageForStage (editor->getShownChainOrder()[2])).getY() - button.getY());
                    break;
                }
            }

            // Master envelope: drag an existing point (move it off the 100 % line first).
            if (sc.kind == Kind::masterEnvelope)
            {
                auto& env = editor->getMasterEnvelopeEditor();
                const auto from = env.getNodePosition (1);
                env.mouseDown (makeMouseEvent (env, from, from, false));
                env.mouseDrag (makeMouseEvent (env, downPos, from, true));
                env.mouseUp (makeMouseEvent (env, downPos, from, true));
                downPos = env.getNodePosition (1);
            }

            const auto live = sc.kind != Kind::knob && sc.kind != Kind::envelope;
            if (live && ! audio.isThreadRunning())
                audio.startThread (juce::Thread::Priority::highest);
            viewFramesAtStart = viewFrames();
            frameIndex = -1;
            input.clear(); paint.clear(); interval.clear();
            lastVBlank = 0;
            rendersAtStart = editor->getWaveformView().getNumBackgroundRenders();
            vblank = juce::VBlankAttachment (&root, [this] { onFrame(); });
        }

        void onFrame()
        {
            const auto t = nowMs();

            // Paint performed since the previous frame's input belongs to that frame.
            if (frameIndex >= warmup)
            {
                interval.push_back (t - lastVBlank);
                paint.push_back (root.lastPaintMs);
            }

            root.lastPaintMs = 0;
            lastVBlank = t;
            ++frameIndex;

            if (frameIndex == 0)
            {
                target->mouseDown (makeMouseEvent (*target, downPos, downPos, false));
                return;
            }

            if (frameIndex > frames + warmup)
            {
                target->mouseUp (makeMouseEvent (*target, lastPos, downPos, true));
                report();
                vblank = {};
                ++current;
                startTimer (300);
                return;
            }

            // Continuous drag: a slow sine sweep (knob: vertical, envelope: diagonal).
            const auto& sc = scenarios[(size_t) current];
            const auto phase = (float) frameIndex / 90.0f * juce::MathConstants<float>::twoPi;

            switch (sc.kind)
            {
                case Kind::envelope: lastPos = downPos + juce::Point<float> (40.0f * std::sin (phase), 50.0f * std::sin (phase * 0.7f)); break;
                case Kind::eq:       lastPos = downPos + juce::Point<float> (60.0f * std::sin (phase), 40.0f * std::sin (phase * 0.7f)); break;
                case Kind::dynamics:
                case Kind::limiter:  lastPos = downPos + juce::Point<float> (0.0f, 30.0f * std::sin (phase)); break;
                case Kind::knob:     lastPos = downPos + juce::Point<float> (0.0f, 80.0f * std::sin (phase)); break;
                case Kind::masterEnvelope: lastPos = downPos + juce::Point<float> (40.0f * std::sin (phase), 40.0f * std::sin (phase * 0.7f)); break;
                case Kind::mix:      lastPos = downPos + juce::Point<float> (0.4f * editor->getMixDisplay().getPlotArea().getWidth() * std::sin (phase), 0.0f); break;
                case Kind::drive:    lastPos = downPos + juce::Point<float> (40.0f * std::sin (phase * 0.7f), 50.0f * std::sin (phase)); break;
                case Kind::chain:    // the tab moves with the drag, so the position is relative to where it is now
                    lastPos = chainDownInEditor + juce::Point<float> (0.0f, 1.6f * chainPitch * std::sin (phase)) - target->getPosition().toFloat(); break;
            }

            const auto t0 = nowMs();
            target->mouseDrag (makeMouseEvent (*target, lastPos, downPos, true));

            if (frameIndex > warmup)
                input.push_back (nowMs() - t0);
        }

        void report()
        {
            const auto& sc = scenarios[(size_t) current];
            const auto in = summarise (input), pa = summarise (paint), iv = summarise (interval);
            const auto budget = iv.mean > 0 ? juce::jmin (16.7, iv.mean) : 16.7;
            auto late = 0;

            for (auto x : interval)
                if (x > 1.5 * 16.7)
                    ++late;

            std::printf ("%-8s %4dx%-4d frames %3d | view frames %5d | input mean %.2f p95 %.2f max %.2f | paint mean %.2f p95 %.2f max %.2f"
                         " | interval mean %.2f p95 %.2f max %.2f | >25ms %d (%.1f%%) | background previews %d\n",
                         kindName (sc.kind), sc.width, sc.height, (int) interval.size(), viewFrames() - viewFramesAtStart,
                         in.mean, in.p95, in.max, pa.mean, pa.p95, pa.max, iv.mean, iv.p95, iv.max,
                         late, 100.0 * late / juce::jmax (1.0, (double) interval.size()),
                         editor->getWaveformView().getNumBackgroundRenders() - rendersAtStart);
            juce::ignoreUnused (budget);
            std::fflush (stdout);
            audio.stopThread (2000);
        }

        int viewFrames() const
        {
            return editor->getEqDisplay().getAnalyzer().getFramesAnalysed() + editor->getDynamicsDisplay().getFramesReceived()
                 + editor->getLimiterDisplay().getFramesReceived() + editor->getMixDisplay().getFramesReceived()
                 + editor->getDriveDisplay().getFramesReceived();
        }

        DooblrKickAudioProcessor processor;
        AudioFeeder audio { processor };
        std::unique_ptr<DooblrKickAudioProcessorEditor> editor;
        Root root;
        juce::VBlankAttachment vblank;
        std::vector<Scenario> scenarios;
        int current = 0, frames, frameIndex = -1, rendersAtStart = 0, viewFramesAtStart = 0;
        static constexpr int warmup = 20;
        juce::Component* target = nullptr;
        juce::Point<float> downPos, lastPos, chainDownInEditor;
        float chainPitch = 0;
        double lastVBlank = 0;
        std::vector<double> input, paint, interval;
    };

    int framesArg = 300;

    class BenchApp final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return "DooblrKickFrameBench"; }
        const juce::String getApplicationVersion() override { return "1"; }
        bool moreThanOneInstanceAllowed() override { return true; }
        void initialise (const juce::String&) override { bench = std::make_unique<Bench> (framesArg); }
        void shutdown() override { bench.reset(); }

    private:
        std::unique_ptr<Bench> bench;
    };
}

int main (int argc, char* argv[])
{
    if (argc > 1)
        framesArg = juce::jmax (30, juce::String (argv[1]).getIntValue());

    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return new BenchApp(); };
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
}
