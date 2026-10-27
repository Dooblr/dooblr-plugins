#pragma once

#include <array>
#include <functional>
#include <mutex>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblrkick::ui
{
    /**
        Shows the rendered kick (one hit at the Key). Clicking it auditions the kick.

        The render runs on a background thread: when the change counter moves, the view asks
        for a render job (captured on the message thread), and hands it to its worker. Only the
        newest job is kept, so a fast knob drag never queues renders up ("latest wins"). The
        worker reduces the hit to min/max peaks; the message thread only rebuilds a cached Path
        from them, so paint() does no audio work and no allocation.
    */
    class WaveformView final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Thread,
                               private juce::AsyncUpdater
    {
    public:
        using RenderJob = std::function<juce::AudioBuffer<float>()>;
        using JobFactory = std::function<RenderJob()>; // called on the message thread
        using ChangeCounter = std::function<juce::uint32()>;

        WaveformView (JobFactory factory, ChangeCounter counter, double renderSampleRate);
        ~WaveformView() override;

        std::function<void()> onClick;

        void setStatus (const juce::String& text);

        /** Renders synchronously on the calling (message) thread, e.g. when the editor opens. */
        void refreshNow();

        /** Test / benchmark helper: waits (pumping nothing) until the worker has no job left,
            then applies its result. Message thread only. */
        bool waitForBackgroundRender (int timeoutMs);

        /** Test / benchmark helper: does what a VBlank does, without the debounce. */
        void pollNow() { lastRequestTime = 0; poll(); }

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent& e) override;

        float getPeakDb() const noexcept { return peakDb; }
        float getLengthMs() const noexcept { return lengthMs; }
        bool hasContent() const noexcept { return ! peaks.empty(); }
        int getNumBackgroundRenders() const noexcept { return backgroundRenders; }

    private:
        static constexpr int numPeaks = 2048; // min/max pairs across the trimmed hit

        struct Result
        {
            std::vector<std::pair<float, float>> peaks;
            float peakDb = -120.0f, lengthMs = 0.0f;
            juce::uint32 count = 0;
        };

        void run() override;
        void handleAsyncUpdate() override;
        void poll();
        static Result analyse (const juce::AudioBuffer<float>& rendered, double sampleRate, juce::uint32 count);
        void apply (Result&& result);
        void rebuildPath();

        JobFactory factory;
        ChangeCounter counter;
        double sampleRate;
        juce::uint32 requestedCount = 0, shownCount = 0;
        juce::uint32 lastRequestTime = 0;

        std::mutex lock; // guards pendingJob / pendingCount / ready / readyResult
        RenderJob pendingJob;
        juce::uint32 pendingCount = 0;
        bool ready = false, busy = false;
        Result readyResult;
        juce::WaitableEvent wake;

        std::vector<std::pair<float, float>> peaks;
        float peakDb = -120.0f, lengthMs = 0.0f;
        int backgroundRenders = 0;
        juce::Path wavePath;
        juce::String infoText, status;
        juce::uint32 statusTime = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
