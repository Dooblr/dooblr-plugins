#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/Analyzer.h"
#include "Theme.h"

namespace dooblrmeter::ui
{
    using SnapshotPtr = std::shared_ptr<const dsp::Snapshot>;

    /** Display settings the views need besides the snapshot. */
    struct ViewContext
    {
        dsp::DeliveryTarget target { "Streaming -14", -14.0f, -1.0f, dsp::DeliveryTarget::Policy::upPeakLimited };
        int targetIndex = 0;
        float slopeDbPerOct = 3.0f;
        int listenProfile = -1;      // the profile selected in Listen (-1 Off)
        int selectedFinding = -1;    // index into snapshot findings
    };

    /** Base: a titled panel that redraws from the latest snapshot. */
    class SnapshotView : public juce::Component
    {
    public:
        explicit SnapshotView (juce::String panelTitle) : title (std::move (panelTitle)) {}
        void update (SnapshotPtr s, const ViewContext& c) { snap = std::move (s); ctx = c; changed(); repaint(); }
        void paint (juce::Graphics&) override;

    protected:
        virtual void changed() {}
        virtual void paintContent (juce::Graphics&, juce::Rectangle<int> area) = 0;
        virtual juce::String headerNote() const { return {}; }
        juce::Rectangle<int> contentArea() const;

        juce::String title;
        SnapshotPtr snap;
        ViewContext ctx;
    };

    class ReadoutView final : public SnapshotView
    {
    public:
        ReadoutView() : SnapshotView ("LOUDNESS") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    class HistoryView final : public SnapshotView
    {
    public:
        HistoryView() : SnapshotView ("LOUDNESS HISTORY") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
        juce::String headerNote() const override;
    };

    class SpectrumView final : public SnapshotView
    {
    public:
        SpectrumView() : SnapshotView ("SPECTRUM") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
        juce::String headerNote() const override;
    };

    class StereoView final : public SnapshotView
    {
    public:
        StereoView() : SnapshotView ("STEREO") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    /** The eight translation tiles. Clicking a tile toggles the Listen audition for it. */
    class TranslationView final : public SnapshotView
    {
    public:
        TranslationView() : SnapshotView ("TRANSLATION") {}
        std::function<void (int profile)> onTileClicked;
        juce::Rectangle<int> tileBounds (int index) const;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    /** Scrollable list of findings; clicking one highlights its time span and band. */
    class FindingsView final : public juce::Component
    {
    public:
        FindingsView();
        ~FindingsView() override;
        void update (SnapshotPtr s, const ViewContext& c);
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void (int index)> onSelect;
        int getNumRows() const noexcept { return snap != nullptr ? (int) snap->findings.size() : 0; }

    private:
        class List;
        SnapshotPtr snap;
        ViewContext ctx;
        juce::Viewport viewport;
        std::unique_ptr<List> list;
    };

    class DeliveryView final : public SnapshotView
    {
    public:
        DeliveryView() : SnapshotView ("DELIVERY TARGETS") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    class MetricsView final : public SnapshotView
    {
    public:
        MetricsView() : SnapshotView ("ALL METRICS") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    class HistogramView final : public SnapshotView
    {
    public:
        HistogramView() : SnapshotView ("SHORT-TERM DISTRIBUTION") {}
    private:
        void paintContent (juce::Graphics&, juce::Rectangle<int>) override;
    };

    /** Value text: proper minus sign, fixed decimals, "--" for no measurement. */
    juce::String fmt (double value, int decimals, double floor = -100.0);
    juce::String signedFmt (double value, int decimals);
    juce::Colour scoreColour (int score);
}
