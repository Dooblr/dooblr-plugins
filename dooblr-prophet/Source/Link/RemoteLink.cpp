#include "RemoteLink.h"

#include <cmath>

#include "../ControlRules.h"
#include "../DSP/Effects.h"
#include "../DSP/Primitives.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../Presets.h"
#include "../UI/Displays.h"
#include "../UI/Layout.h"
#include "../UI/Theme.h"

namespace dooblrprophet::link
{
    namespace
    {
        constexpr int graphPoints = 160;
        constexpr const char* emptyString = "~"; // Max drops empty OSC strings; "~" stands for ""

        juce::String orEmpty (const juce::String& s) { return s.isEmpty() ? juce::String (emptyString) : s; }

        juce::String kindName (ui::ControlPanel::Kind k)
        {
            switch (k)
            {
                case ui::ControlPanel::Kind::toggle: return "toggle";
                case ui::ControlPanel::Kind::combo:  return "combo";
                case ui::ControlPanel::Kind::knob:   break;
            }
            return "knob";
        }

        juce::String colourString (juce::Colour c) { return c.toDisplayString (true); }
    }

    //==============================================================================
    void PairingDetector::scan (const juce::MidiBuffer& midi) noexcept
    {
        juce::uint32 seen = 0, onChannel = 0;
        for (const auto metadata : midi)
        {
            ++seen;
            if (metadata.numBytes != 3)
                continue;
            const auto* d = metadata.data;
            if (d[0] != (juce::uint8) (0xb0 | (pairChannel - 1)))
                continue;
            ++onChannel;

            // Order independent: a host may deliver the three CCs in any order within a block.
            const int cc = d[1], value = d[2] & 127;
            if (cc == pairCcHigh)            feed (main, 0, value);
            else if (cc == pairCcLow)        feed (main, 1, value);
            else if (cc == pairCcCheck)      feed (main, 2, value);
            else if (cc == pairCcHighAlt)    feed (alt, 0, value);
            else if (cc == pairCcLowAlt)     feed (alt, 1, value);
            else if (cc == pairCcCheckAlt)   feed (alt, 2, value);
        }
        if (seen > 0)
            events.fetch_add (seen, std::memory_order_relaxed);
        if (onChannel > 0)
            channelCcs.fetch_add (onChannel, std::memory_order_relaxed);
    }

    void PairingDetector::feed (Partial& part, int which, int value) noexcept
    {
        (which == 0 ? part.high : which == 1 ? part.low : part.check) = value;

        if (part.high >= 0 && part.low >= 0 && part.check >= 0 && part.check == pairCheck (part.high, part.low))
        {
            const auto token = (part.high << 7) | part.low;
            if (token >= 1 && token <= maxToken)
            {
                pending.store (token);
                tokens.fetch_add (1, std::memory_order_relaxed);
            }
            part = {};
        }
    }

    std::array<juce::MidiMessage, 3> PairingDetector::messagesFor (int token, bool alt)
    {
        const auto high = (token >> 7) & 127, low = token & 127;
        return { juce::MidiMessage::controllerEvent (pairChannel, alt ? pairCcHighAlt : pairCcHigh, high),
                 juce::MidiMessage::controllerEvent (pairChannel, alt ? pairCcLowAlt : pairCcLow, low),
                 juce::MidiMessage::controllerEvent (pairChannel, alt ? pairCcCheckAlt : pairCcCheck, pairCheck (high, low)) };
    }

    //==============================================================================
    RemoteLink::RemoteLink (DooblrProphetProcessor& p) : processor (p)
    {
        for (auto* param : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                parameters.push_back (ranged);
        sentValues.assign (parameters.size(), -1.0f);

        // Loopback only; the first free port. A unicast UDP port can't be bound twice (no
        // SO_REUSEPORT: port reuse stays off), so two instances never share one.
        for (int candidate = firstPort; candidate < firstPort + numPorts; ++candidate)
        {
            auto s = std::make_unique<juce::DatagramSocket> (false);
            if (s->bindToPort (candidate, "127.0.0.1"))
            {
                socket = std::move (s);
                port = candidate;
                break;
            }
        }

        if (socket != nullptr)
            startTimerHz (timerHz);
    }

    RemoteLink::~RemoteLink()
    {
        stopTimer();
        if (clientPort != 0)
            sendNow (clientPort, OscMessage ("/dp/gone"));
        endOpenGestures();
    }

    //==============================================================================
    void RemoteLink::poll (juce::uint32 nowMs)
    {
        if (socket == nullptr)
            return;

        char buffer[8192];
        for (int count = 0; count < 256 && socket->waitUntilReady (true, 0) == 1; ++count)
        {
            juce::String senderIp;
            int senderPort = 0;
            const auto bytes = socket->read (buffer, (int) sizeof (buffer), false, senderIp, senderPort);
            if (bytes <= 0)
                break;
            if (senderIp != "127.0.0.1")
                continue;
            if (const auto message = decode (buffer, (size_t) bytes))
                handle (*message, nowMs);
        }

        // A token from this track's MIDI: tell that device where we are.
        const auto token = pairing.take();
        if (token > 0)
            sendNow (replyPortBase + token, OscMessage ("/dp/announce").add (port).add (JucePlugin_VersionString).add (token));

        if (clientPort != 0 && nowMs - lastHeardMs > clientTimeoutMs)
            dropClient();

        if (clientPort != 0)
        {
            queueChanges (nowMs);
            flush();
        }
    }

    void RemoteLink::handle (const OscMessage& m, juce::uint32 nowMs)
    {
        const auto& a = m.address;

        if (a == "/dp/ping")
        {
            const auto reply = m.intArg (0);
            if (reply >= 1024 && reply <= 65535)
                sendNow (reply, OscMessage ("/dp/here").add (port).add (JucePlugin_VersionString).add (clientPort != 0 ? 1 : 0)
                                     .add ((int) (pairing.eventsSeen() & 0x7fffffff))
                                     .add ((int) (pairing.channelCcsSeen() & 0x7fffffff))
                                     .add ((int) (pairing.tokensSeen() & 0x7fffffff)));
            return;
        }

        if (a == "/dp/hello")
        {
            const auto reply = m.intArg (0);
            if (reply < 1024 || reply > 65535)
                return;
            if (reply != clientPort)
                setClient (reply, nowMs);
            lastHeardMs = nowMs;
            return;
        }

        if (clientPort == 0)
            return; // commands only from a paired device
        lastHeardMs = nowMs;

        if (a == "/dp/bye")          { dropClient(); return; }
        if (a == "/dp/sync")         { queueFullSync(); return; }

        if (a == "/dp/begin")
        {
            if (auto* p = parameterAt (m, 0))
                if (openGestures.insert (p).second)
                    p->beginChangeGesture();
            return;
        }

        if (a == "/dp/set" || a == "/dp/setplain")
        {
            if (auto* p = parameterAt (m, 0))
            {
                if (! m.isNumber (1))
                    return;
                const auto value = m.floatArg (1);
                setNormalised (*p, a == "/dp/set" ? value : p->convertTo0to1 (value));
            }
            return;
        }

        if (a == "/dp/end")
        {
            if (auto* p = parameterAt (m, 0))
                if (openGestures.erase (p) > 0)
                    p->endChangeGesture();
            return;
        }

        if (a == "/dp/default")
        {
            if (auto* p = parameterAt (m, 0))
                setNormalised (*p, p->getDefaultValue());
            return;
        }

        if (a == "/dp/random")
        {
            endOpenGestures();
            juce::Random random; // seeded from the clock, like the Random button
            processor.randomisePatch (random);
            return;
        }

        if (a == "/dp/undo" || a == "/dp/redo")
        {
            endOpenGestures();
            if (a == "/dp/undo") processor.getUndoHistory().undo(); else processor.getUndoHistory().redo();
            return;
        }

        if (a == "/dp/program")
        {
            const auto index = m.intArg (0, -1);
            if (index >= 0 && index < processor.getNumPrograms())
            {
                endOpenGestures();
                processor.setCurrentProgram (index);
            }
            return;
        }

        if (a == "/dp/userpreset")
        {
            const auto files = presets::userPresets();
            const auto index = m.intArg (0, -1);
            if (juce::isPositiveAndBelow (index, files.size()))
            {
                endOpenGestures();
                setNote (processor.loadUserPreset (files[index]));
                lastPresetList = {};
            }
            return;
        }

        if (a == "/dp/fxpreset")
        {
            const auto module = m.intArg (0, -1), index = m.intArg (1, -1);
            const auto count = module == 0 ? dsp::numEchoPresets : dsp::numReverbPresets;
            if ((module == 0 || module == 1) && juce::isPositiveAndBelow (index, count))
                processor.applyFxPreset (module, index);
            return;
        }

        if (a == "/dp/voices")
        {
            if (auto* p = processor.getState().getParameter (params::id::voices))
            {
                const auto voices = juce::jlimit (1, dsp::maxVoices, m.intArg (0, 1));
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) voices));
                p->endChangeGesture();
            }
            return;
        }

        if (a == "/dp/save" || a == "/dp/load")
            launchChooser (a == "/dp/save");
    }

    juce::RangedAudioParameter* RemoteLink::parameterAt (const OscMessage& m, int arg) const
    {
        if (! m.isNumber (arg))
            return nullptr;
        const auto index = m.intArg (arg, -1);
        return juce::isPositiveAndBelow (index, (int) parameters.size()) ? parameters[(size_t) index] : nullptr;
    }

    void RemoteLink::setNormalised (juce::RangedAudioParameter& p, float norm)
    {
        norm = juce::jlimit (0.0f, 1.0f, norm);
        if (openGestures.count (&p) > 0)
        {
            p.setValueNotifyingHost (norm);
        }
        else
        {
            // A one-off change (a click, a menu pick): its own gesture, so it is one undo step.
            p.beginChangeGesture();
            p.setValueNotifyingHost (norm);
            p.endChangeGesture();
        }
    }

    void RemoteLink::endOpenGestures()
    {
        for (auto* p : openGestures)
            p->endChangeGesture();
        openGestures.clear();
    }

    void RemoteLink::setClient (int replyPort, juce::uint32 nowMs)
    {
        endOpenGestures();
        clientPort = replyPort;
        lastHeardMs = lastAliveMs = nowMs;
        outgoing.clear();
        queueFullSync();
    }

    void RemoteLink::dropClient()
    {
        endOpenGestures();
        clientPort = 0;
        outgoing.clear();
    }

    //==============================================================================
    void RemoteLink::sendNow (int toPort, const OscMessage& m)
    {
        if (socket == nullptr)
            return;
        const auto block = encode (m);
        socket->write ("127.0.0.1", toPort, block.getData(), (int) block.getSize());
    }

    void RemoteLink::queue (const OscMessage& m)
    {
        if ((int) outgoing.size() >= maxQueuedMessages)
            outgoing.pop_front();
        outgoing.push_back (encode (m));
    }

    void RemoteLink::flush()
    {
        for (int n = 0; n < maxMessagesPerTick && ! outgoing.empty(); ++n)
        {
            const auto& block = outgoing.front();
            socket->write ("127.0.0.1", clientPort, block.getData(), (int) block.getSize());
            outgoing.pop_front();
        }
    }

    void RemoteLink::queueIfChanged (juce::MemoryBlock& last, const OscMessage& m)
    {
        auto block = encode (m);
        if (block == last)
            return;
        last = block;
        if ((int) outgoing.size() >= maxQueuedMessages)
            outgoing.pop_front();
        outgoing.push_back (std::move (block));
    }

    //==============================================================================
    void RemoteLink::queueFullSync()
    {
        queue (OscMessage ("/dp/syncstart").add (JucePlugin_VersionString).add ((int) parameters.size()));
        queueMeta();
        queueLayout();

        std::fill (sentValues.begin(), sentValues.end(), -1.0f);
        lastPreset = lastPresetList = lastUndo = lastDim = lastStatus = lastLive = {};
        for (auto& g : lastGraphs)
            g = {};

        queueChanges (lastHeardMs);
        lastPresetScanMs = lastHeardMs;
        queue (OscMessage ("/dp/synced"));
    }

    void RemoteLink::queueMeta()
    {
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto* p = parameters[i];
            int kind = 0;
            juce::String choices;
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                kind = 2;
                choices = c->choices.joinIntoString ("|");
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr) kind = 1;
            else if (dynamic_cast<juce::AudioParameterInt*> (p) != nullptr)  kind = 3;

            const auto& range = p->getNormalisableRange();
            // Bipolar knobs draw their value arc from zero, like ui::Knob.
            const auto origin = range.start < 0.0f && range.end > 0.0f ? range.convertTo0to1 (0.0f) : 0.0f;

            queue (OscMessage ("/dp/meta")
                       .add ((int) i)
                       .add (p->getParameterID())
                       .add (orEmpty (p->getName (64)))
                       .add (orEmpty (p->getLabel()))
                       .add (kind)
                       .add (p->getDefaultValue())
                       .add (p->isDiscrete() ? p->getNumSteps() : 0)
                       .add (origin)
                       .add (range.start)
                       .add (range.end)
                       .add (orEmpty (choices)));
        }
    }

    void RemoteLink::queueLayout()
    {
        const auto& pages = layout::pages();
        for (size_t page = 0; page < pages.size(); ++page)
        {
            const auto& def = pages[page];
            OscMessage pageMessage ("/dp/page");
            pageMessage.add ((int) page).add (def.name).add (colourString (ui::pageColour ((int) page)));
            // Rows: panel indices per row, rows separated by "|", e.g. "0,1|2".
            juce::StringArray rows;
            for (const auto& row : def.rows)
            {
                juce::StringArray cells;
                for (auto index : row)
                    cells.add (juce::String (index));
                rows.add (cells.joinIntoString (","));
            }
            pageMessage.add (orEmpty (rows.joinIntoString ("|")));
            queue (pageMessage);

            for (size_t t = 0; t < def.toggles.size(); ++t)
            {
                const auto& toggle = def.toggles[t];
                queue (OscMessage ("/dp/mode").add ((int) page).add ((int) t).add (toggle.caption).add (toggle.id).add (toggle.labels.joinIntoString ("|")));
            }

            for (size_t panel = 0; panel < def.panels.size(); ++panel)
            {
                const auto& p = def.panels[panel];
                OscMessage m ("/dp/panel");
                m.add ((int) page).add ((int) panel).add (p.title).add (p.voicePresets ? 1 : 0).add (p.fxPicker);
                for (const auto& c : p.controls)
                    m.add (kindName (c.kind)).add (c.id).add (c.caption);
                queue (m);
            }
        }

        juce::StringArray counts;
        for (auto c : layout::voicePresetCounts)
            counts.add (juce::String (c));
        queue (OscMessage ("/dp/fxnames").add (0).add (dsp::echoPresetNames().joinIntoString ("|")));
        queue (OscMessage ("/dp/fxnames").add (1).add (dsp::reverbPresetNames().joinIntoString ("|")));
        queue (OscMessage ("/dp/voicepresets").add (counts.joinIntoString ("|")));
        queue (OscMessage ("/dp/layoutdone").add ((int) pages.size()));
    }

    void RemoteLink::queueChanges (juce::uint32 nowMs)
    {
        // Parameter values (host automation, presets, Random, the plugin's own window, this device).
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto v = parameters[i]->getValue();
            if (juce::exactlyEqual (v, sentValues[i]))
                continue;
            sentValues[i] = v;
            queue (OscMessage ("/dp/v").add ((int) i).add (v).add (orEmpty (parameters[i]->getText (v, 0))));
        }

        // Preset list: rescanned every couple of seconds (files can be added in the Finder).
        if (lastPresetList.isEmpty() || nowMs - lastPresetScanMs >= presetScanIntervalMs)
        {
            lastPresetScanMs = nowMs;
            OscMessage list ("/dp/presets");
            const auto& factory = presets::factory();
            list.add ((int) factory.size());
            for (const auto& f : factory)
                list.add (f.name);
            const auto user = presets::userPresets();
            for (int i = 0; i < juce::jmin (user.size(), 120); ++i)
                list.add (user[i].getFileNameWithoutExtension());
            queueIfChanged (lastPresetList, list);
        }

        queueIfChanged (lastPreset, OscMessage ("/dp/preset").add (orEmpty (processor.getPresetName())).add (processor.getCurrentProgram()));
        queueIfChanged (lastUndo, OscMessage ("/dp/undo").add (processor.getUndoHistory().canUndo() ? 1 : 0).add (processor.getUndoHistory().canRedo() ? 1 : 0));

        auto& state = processor.getState();
        const auto dimmed = rules::dimmedControls ([&state] (const char* pid)
        {
            auto* p = state.getParameter (pid);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        });
        OscMessage dim ("/dp/dim");
        for (const auto& d : dimmed)
            dim.add (d);
        queueIfChanged (lastDim, dim);

        const auto s = processor.readSettings();
        const auto activeVoices = processor.getEngine().getNumActiveVoices();
        OscMessage status ("/dp/status");
        for (int page = 0; page < (int) layout::pages().size(); ++page)
            status.add (orEmpty (rules::pageStatus (page, s, activeVoices)));
        status.add (orEmpty (note)).add (noteSerial);
        queueIfChanged (lastStatus, status);

        // Graphs: the same maths as UI/Displays.cpp, sampled at graphPoints.
        {
            OscMessage osc ("/dp/g/osc");
            osc.add (graphPoints);
            for (const auto* o : { &s.osc1, &s.osc2 })
            {
                const auto weights = o->switched ? dsp::WaveWeights::fromSwitches (o->saw, o->tri, o->pulse) : dsp::WaveWeights::fromShape (o->shape);
                const auto peak = juce::jmax (1.0f, std::abs (weights.tri) + std::abs (weights.saw) + std::abs (weights.pulse));
                for (int n = 0; n <= graphPoints; ++n)
                {
                    auto phase = 2.0f * (float) n / (float) graphPoints;
                    phase -= std::floor (phase);
                    osc.add (dsp::Oscillator::naive (phase, o->pulseWidth, weights) / peak);
                }
            }
            queueIfChanged (lastGraphs[0], osc);
        }

        {
            using F = ui::FilterDisplay;
            OscMessage flt ("/dp/g/flt");
            flt.add (graphPoints).add (s.lpfCutoff).add (s.lpfReso).add (s.hpfCutoff).add (s.hpfReso);
            for (int which = 0; which < 2; ++which)
                for (int n = 0; n <= graphPoints; ++n)
                {
                    const auto hz = F::minHz * std::pow (F::maxHz / F::minHz, (float) n / (float) graphPoints);
                    flt.add (juce::jlimit (F::minDb, F::maxDb, which == 0 ? F::lowPassDb (s, hz) : F::highPassDb (s, hz)));
                }
            queueIfChanged (lastGraphs[1], flt);
        }

        {
            const auto span = ui::EnvelopeDisplay::spanSeconds (s);
            const auto fenv = s.effectiveFilterEnv();
            const auto keyUp = juce::jmax (fenv.attack + fenv.decay, s.ampEnv.attack + s.ampEnv.decay) + 0.15f * span;
            OscMessage env ("/dp/g/env");
            env.add (graphPoints).add (span).add (keyUp).add (s.filterEnvSync ? 1 : 0);
            for (const auto* e : { &fenv, &s.ampEnv })
            {
                const auto pointRate = (double) graphPoints / span;
                dsp::Envelope envelope;
                envelope.setTimes (pointRate, e->attack, e->decay, e->sustain, e->release);
                envelope.noteOn();
                const auto hold = keyUp - e->attack - e->decay;
                const auto releaseAt = juce::roundToInt ((e->attack + e->decay + hold) * pointRate);
                for (int n = 0; n <= graphPoints; ++n)
                {
                    if (n == releaseAt)
                        envelope.noteOff();
                    const auto level = envelope.next();
                    env.add (n == 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, level));
                }
            }
            queueIfChanged (lastGraphs[2], env);
        }

        {
            OscMessage lfo ("/dp/g/lfo");
            lfo.add (graphPoints).add (s.lfoNoiseMix > 0.5f ? 1 : 0);
            dsp::Random random;
            random.seed (0x51ab1e);
            float held = random.bipolar();
            int lastCycle = 0;
            for (int n = 0; n <= graphPoints; ++n)
            {
                const auto cycles = 2.0f * (float) n / (float) graphPoints;
                const auto cycle = juce::jmin (1, (int) cycles);
                if (cycle != lastCycle) { held = random.bipolar(); lastCycle = cycle; }
                const auto p = cycles - std::floor (cycles);
                float v = 0.0f;
                switch (s.lfoWave)
                {
                    case dsp::LfoWave::triangle:   v = 1.0f - 4.0f * std::abs (p - 0.5f); break;
                    case dsp::LfoWave::saw:        v = 2.0f * p - 1.0f; break;
                    case dsp::LfoWave::reverseSaw: v = 1.0f - 2.0f * p; break;
                    case dsp::LfoWave::square:     v = p < 0.5f ? 1.0f : -1.0f; break;
                    case dsp::LfoWave::sampleHold: v = held; break;
                }
                lfo.add (v);
            }
            queueIfChanged (lastGraphs[3], lfo);
        }

        {
            const auto& fx = s.fx;
            const auto bpm = processor.getHostBpm();
            const auto span = ui::FxDisplay::spanSeconds (fx, bpm);
            const auto& verb = dsp::reverbPresets()[(size_t) juce::jlimit (0, dsp::numReverbPresets - 1, fx.reverbPreset)];
            const auto pre = verb.preDelayMs * 0.001f;
            const auto rt60 = (float) dsp::reverbDecaySeconds (fx);
            const auto reverbLevel = 0.25f + 0.75f * fx.reverbMix;
            constexpr int reverbPoints = 120;

            OscMessage m ("/dp/g/fx");
            m.add (span).add (fx.reverbOn ? 1 : 0).add (fx.echoOn ? 1 : 0).add (pre).add (reverbPoints);
            for (int n = 0; n <= reverbPoints; ++n)
            {
                const auto t = pre + (span - pre) * (float) n / (float) reverbPoints;
                m.add (reverbLevel * std::pow (10.0f, -3.0f * (t - pre) / rt60));
            }

            // Echo taps: time, side (1 left / up, -1 right / down), amplitude.
            const auto& echo = dsp::echoPresets()[(size_t) juce::jlimit (0, dsp::numEchoPresets - 1, fx.echoPreset)];
            const auto time = (float) dsp::echoSeconds (fx, bpm);
            const auto echoLevel = 0.25f + 0.75f * fx.echoMix;
            for (int n = 1; n <= ui::FxDisplay::maxTaps; ++n)
            {
                const auto a = echoLevel * std::pow (fx.echoFeedback, (float) (n - 1));
                if (a < 0.01f)
                    break;
                if (fx.echoPingPong)
                {
                    const auto t = time * (float) n;
                    if (t > span) break;
                    m.add (t).add ((n % 2) == 1 ? 1 : -1).add (a);
                }
                else
                {
                    const auto tl = time * (float) n, tr = time * (1.0f + echo.spread) * (float) n;
                    if (tl > span) break;
                    m.add (tl).add (1).add (a);
                    if (tr <= span)
                        m.add (tr).add (-1).add (a);
                }
            }
            queueIfChanged (lastGraphs[4], m);
        }

        {
            const auto inUse = s.mode == dsp::VoiceMode::poly ? s.voices : s.mode == dsp::VoiceMode::unison ? s.unisonVoices : 1;
            queueIfChanged (lastGraphs[5], OscMessage ("/dp/g/voc").add (inUse).add (dsp::maxVoices));
        }

        // Live data: LFO now, output peaks since the last tick, voices.
        {
            auto& tap = processor.getEngine().getVoiceTap();
            const auto [left, right] = peakTap.take();
            OscMessage live ("/dp/live");
            live.add (tap.lfo.load (std::memory_order_relaxed)).add (left).add (right).add (activeVoices);
            for (int i = 0; i < dsp::maxVoices; ++i)
                live.add (tap.notes[(size_t) i].load (std::memory_order_relaxed));
            for (int i = 0; i < dsp::maxVoices; ++i)
                live.add (tap.levels[(size_t) i].load (std::memory_order_relaxed));
            queueIfChanged (lastLive, live);
        }

        if (nowMs - lastAliveMs >= aliveIntervalMs)
        {
            lastAliveMs = nowMs;
            queue (OscMessage ("/dp/alive").add (port));
        }
    }

    //==============================================================================
    void RemoteLink::launchChooser (bool save)
    {
        const auto folder = presets::userDirectory();
        const auto pattern = juce::String ("*") + presets::fileExtension;

        if (save)
        {
            folder.createDirectory();
            auto name = juce::File::createLegalFileName (processor.getPresetName());
            if (name.isEmpty())
                name = "Patch";
            chooser = std::make_unique<juce::FileChooser> ("Save dooblr-prophet preset", folder.getChildFile (name + presets::fileExtension), pattern);
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File())
                                          return;
                                      setNote (processor.saveUserPreset (fc.getResult().withFileExtension (presets::fileExtension)));
                                      lastPresetList = {};
                                  });
        }
        else
        {
            chooser = std::make_unique<juce::FileChooser> ("Load dooblr-prophet preset", folder.isDirectory() ? folder : juce::File(), pattern);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (! fc.getResult().existsAsFile())
                                          return;
                                      endOpenGestures();
                                      setNote (processor.loadUserPreset (fc.getResult()));
                                      lastPresetList = {};
                                  });
        }
    }
}
