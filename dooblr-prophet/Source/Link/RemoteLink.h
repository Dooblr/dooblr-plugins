#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <set>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/OutputPeakTap.h"
#include "Osc.h"

namespace dooblrprophet
{
    class DooblrProphetProcessor;
}

namespace dooblrprophet::link
{
    /**
        The remote link (0.3.0): lets the dooblr-prophet Max for Live device (m4l/) show and edit
        this instance without the plugin window. See m4l/README.md for the protocol.

        - Transport: OSC over UDP on the loopback interface only (127.0.0.1). Each instance binds
          the first free port in [firstPort, firstPort + numPorts).
        - Pairing: the device sends three MIDI CCs on channel 16 (pairCcHigh, pairCcLow,
          pairCcCheck) carrying a random token. MIDI only reaches the instrument on the device's
          own track, so the instance that sees the token is the right one; it announces its port
          to replyPortBase + token, and the device answers /dp/hello with its reply port.
          0.3.1: Live 11 did not pass these CCs to the VST3 in practice, so the device also pairs with
          no MIDI when the set holds one instance: it pings every port and says hello to the one
          unpaired /dp/here (m4l/README.md). /dp/here reports MIDI counters for diagnosis.
        - Threads: the audio thread only scans its MIDI for the token (PairingDetector: plain
          atomics) and pushes the output peak; everything else (socket, parameters, presets) runs
          on the message thread in a 30 Hz timer. Nothing here locks or allocates on the audio thread.
    */
    inline constexpr int firstPort = 47810, numPorts = 64, replyPortBase = 47900, maxToken = 16000;
    inline constexpr int pairChannel = 16, pairCcHigh = 102, pairCcLow = 103, pairCcCheck = 104;
    /** A second encoding of the same token (0.3.1) on CCs the MIDI spec defines (General Purpose
        1-3), sent alongside the first: some hosts drop the "undefined" CCs 102-104. The engine
        ignores both sets. */
    inline constexpr int pairCcHighAlt = 16, pairCcLowAlt = 17, pairCcCheckAlt = 18;

    constexpr int pairCheck (int high, int low) noexcept { return (high ^ low ^ 0x2a) & 127; }

    /**
        Finds the pairing token in a block's MIDI (audio thread; lock free). The three CCs may
        arrive in any order (the last value of each counts); the check value must match. The
        device picks a new token whose three values all differ from the last attempt's, so a
        host that skips repeated CC values cannot stall pairing.
    */
    class PairingDetector
    {
    public:
        void scan (const juce::MidiBuffer& midi) noexcept;
        /** The last complete token seen since the previous take (-1 if none). */
        int take() noexcept { return pending.exchange (-1); }

        /** The three CCs the device sends for a token (for tests); alt = the General Purpose set. */
        static std::array<juce::MidiMessage, 3> messagesFor (int token, bool alt = false);

        /** Diagnostics (any thread; relaxed counters): every MIDI event scanned, the channel-16
            CCs among them, and the complete tokens found. Reported by /dp/here, so a probe can
            tell "no MIDI reaches the plug-in" from "MIDI arrives but not the pairing CCs". */
        juce::uint32 eventsSeen() const noexcept    { return events.load (std::memory_order_relaxed); }
        juce::uint32 channelCcsSeen() const noexcept { return channelCcs.load (std::memory_order_relaxed); }
        juce::uint32 tokensSeen() const noexcept    { return tokens.load (std::memory_order_relaxed); }

    private:
        struct Partial { int high = -1, low = -1, check = -1; };
        void feed (Partial& part, int which, int value) noexcept;

        Partial main, alt;
        std::atomic<int> pending { -1 };
        std::atomic<juce::uint32> events { 0 }, channelCcs { 0 }, tokens { 0 };
    };

    class RemoteLink final : private juce::Timer
    {
    public:
        explicit RemoteLink (DooblrProphetProcessor& processor);
        ~RemoteLink() override;

        /** The bound UDP port (0 if none could be bound). */
        int getPort() const noexcept { return port; }
        /** The paired device's reply port (0 when none). */
        int getClientPort() const noexcept { return clientPort; }

        PairingDetector& getPairing() noexcept { return pairing; }
        dsp::OutputPeakTap& getPeakTap() noexcept { return peakTap; }

        /** One timer tick: reads packets, answers pairing, sends changes. Public for the tests. */
        void poll (juce::uint32 nowMs);

        static constexpr juce::uint32 clientTimeoutMs = 5000, aliveIntervalMs = 1000, presetScanIntervalMs = 2000;
        static constexpr int maxMessagesPerTick = 64, maxQueuedMessages = 4000, timerHz = 30;

    private:
        void timerCallback() override { poll (juce::Time::getMillisecondCounter()); }

        void handle (const OscMessage& m, juce::uint32 nowMs);
        void setClient (int replyPort, juce::uint32 nowMs);
        void dropClient();
        void endOpenGestures();
        void queue (const OscMessage& m);
        void sendNow (int toPort, const OscMessage& m);
        void flush();

        void queueFullSync();
        void queueLayout();
        void queueMeta();
        void queueChanges (juce::uint32 nowMs);
        void queueIfChanged (juce::MemoryBlock& last, const OscMessage& m);

        juce::RangedAudioParameter* parameterAt (const OscMessage& m, int arg) const;
        void setNormalised (juce::RangedAudioParameter& p, float norm);
        void launchChooser (bool save);

        DooblrProphetProcessor& processor;
        std::unique_ptr<juce::DatagramSocket> socket;
        int port = 0;

        PairingDetector pairing;
        dsp::OutputPeakTap peakTap;

        int clientPort = 0;
        juce::uint32 lastHeardMs = 0, lastAliveMs = 0, lastPresetScanMs = 0;

        std::vector<juce::RangedAudioParameter*> parameters;
        std::vector<float> sentValues;
        std::set<juce::RangedAudioParameter*> openGestures;

        std::deque<juce::MemoryBlock> outgoing;
        juce::MemoryBlock lastPreset, lastPresetList, lastUndo, lastDim, lastStatus, lastLive;
        std::array<juce::MemoryBlock, 6> lastGraphs;

        std::unique_ptr<juce::FileChooser> chooser;
        juce::String note; // the last Save / Load result, shown for a few seconds by the device
        int noteSerial = 0;
        void setNote (const juce::String& text) { note = text; ++noteSerial; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteLink)
    };
}
