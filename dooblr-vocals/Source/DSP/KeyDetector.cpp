#include "KeyDetector.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr double targetRate = 5333.0;     // decimated analysis rate (48 kHz / 9)
        constexpr double lowpassHz = 1700.0;      // anti-alias corner, also the top of the chroma range
        constexpr double lowestHz = 55.0;         // A1
        constexpr float silenceRms = 0.001f;      // -60 dBFS
        constexpr float peakFloor = 0.01f;        // spectral peaks within 40 dB of the frame's loudest
        constexpr double evidenceCap = 60.0;

        // Decision constants (tuned on synthetic progressions; see Tests "key follow").
        constexpr double slowTau = 8.0, fastTau = 2.0;             // seconds (sidechain)
        constexpr double marginSteady = 0.06, marginResponsive = 0.02;
        constexpr double thresholdSteady = 0.5, thresholdResponsive = 0.2;
        constexpr double firstKeyEvidence = 6.0;
        constexpr float minCorrelation = 0.3f;
        constexpr double vocalWeakness = 1.5;

        int argmax (const std::array<float, 24>& r) noexcept
        {
            int best = 0;
            for (int k = 1; k < 24; ++k)
                if (r[(size_t) k] > r[(size_t) best])
                    best = k;
            return best;
        }

        // Krumhansl-Kessler probe-tone profiles (C major, C minor).
        constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        struct Profiles
        {
            std::array<std::array<float, 12>, 24> rows {};

            Profiles()
            {
                for (int k = 0; k < 24; ++k)
                {
                    const auto& base = k < 12 ? majorProfile : minorProfile;
                    const auto tonic = k % 12;
                    double mean = 0.0;
                    for (auto v : base)
                        mean += v / 12.0;
                    double norm = 0.0;
                    for (int pc = 0; pc < 12; ++pc)
                    {
                        const auto v = base[(size_t) (((pc - tonic) % 12 + 12) % 12)] - mean;
                        norm += v * v;
                    }
                    norm = std::sqrt (norm);
                    for (int pc = 0; pc < 12; ++pc)
                        rows[(size_t) k][(size_t) pc] = (float) ((base[(size_t) (((pc - tonic) % 12 + 12) % 12)] - mean) / norm);
                }
            }
        };

        const Profiles& profiles()
        {
            static const Profiles p; // built at static-init time below, never first on the audio thread
            return p;
        }

        [[maybe_unused]] const Profiles& warmProfiles = profiles();

        // Butterworth Q values for a 6th-order low-pass as three biquads.
        constexpr std::array<double, 3> butterworthQ { 0.51763809, 0.70710678, 1.93185165 };
    }

    //==============================================================================
    void ChromaAccumulator::prepare (double sampleRate, float a4Hz)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        decimation = std::max (1, (int) std::lround (rate / targetRate));
        decimatedRate = rate / decimation;
        hopSeconds = hopSize / decimatedRate;

        for (size_t i = 0; i < lowpass.size(); ++i)
        {
            // RBJ low-pass at the full rate.
            const auto w0 = 2.0 * juce::MathConstants<double>::pi * std::min (lowpassHz, 0.45 * rate) / rate;
            const auto alpha = std::sin (w0) / (2.0 * butterworthQ[i]);
            const auto cosw = std::cos (w0);
            const auto a0 = 1.0 + alpha;
            auto& f = lowpass[i];
            f.b0 = (float) ((1.0 - cosw) * 0.5 / a0);
            f.b1 = (float) ((1.0 - cosw) / a0);
            f.b2 = f.b0;
            f.a1 = (float) (-2.0 * cosw / a0);
            f.a2 = (float) ((1.0 - alpha) / a0);
        }

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        ring.assign ((size_t) fftSize, 0.0f);
        work.assign ((size_t) fftSize * 2, 0.0f);
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
        binWeight.assign ((size_t) fftSize / 2 + 1, 0.0f);
        binClass.assign ((size_t) fftSize / 2 + 1, 0);
        a4 = a4Hz;
        rebuildBinMap();
        reset();
    }

    void ChromaAccumulator::reset() noexcept
    {
        for (auto& f : lowpass)
            f.z1 = f.z2 = 0.0f;
        std::fill (ring.begin(), ring.end(), 0.0f);
        phase = write = sinceHop = filled = 0;
        chroma.fill (0.0f);
        fastChroma.fill (0.0f);
        evidence = 0.0;
        sinceSignal = 1.0e9;
    }

    void ChromaAccumulator::setA4 (float a4Hz) noexcept
    {
        if (std::abs (a4Hz - a4) < 1.0e-3f)
            return;
        a4 = a4Hz;
        rebuildBinMap();
    }

    void ChromaAccumulator::rebuildBinMap() noexcept
    {
        if (binWeight.empty())
            return;
        const auto binHz = decimatedRate / fftSize;
        lowBin = std::max (1, (int) std::ceil (lowestHz / binHz));
        highBin = std::min (fftSize / 2 - 1, (int) std::floor (std::min (lowpassHz, 0.48 * decimatedRate) / binHz));
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const auto hz = std::max (1.0, k * binHz);
            const auto midi = 69.0 + 12.0 * std::log2 (hz / a4);
            const auto nearest = std::round (midi);
            const auto dev = midi - nearest;
            binClass[(size_t) k] = (((int) nearest % 12) + 12) % 12;
            const auto c = std::cos (juce::MathConstants<double>::pi * dev);
            binWeight[(size_t) k] = (k >= lowBin && k <= highBin) ? (float) (c * c) : 0.0f;
        }
    }

    int ChromaAccumulator::push (const float* left, const float* right, int n, double tauSeconds, double fastTauSeconds) noexcept
    {
        if (fft == nullptr || left == nullptr)
            return 0;

        int frames = 0;
        for (int i = 0; i < n; ++i)
        {
            auto x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            for (auto& f : lowpass)
                x = f.process (x);

            if (++phase < decimation)
                continue;
            phase = 0;

            ring[(size_t) write] = x;
            write = (write + 1) % fftSize;
            filled = std::min (filled + 1, fftSize);

            if (++sinceHop >= hopSize)
            {
                sinceHop = 0;
                if (filled == fftSize)
                {
                    analyseFrame (tauSeconds, fastTauSeconds);
                    ++frames;
                }
            }
        }

        // Denormal guard for long silences.
        for (auto& f : lowpass)
        {
            if (std::abs (f.z1) < 1.0e-20f) f.z1 = 0.0f;
            if (std::abs (f.z2) < 1.0e-20f) f.z2 = 0.0f;
        }
        return frames;
    }

    void ChromaAccumulator::analyseFrame (double tauSeconds, double fastTauSeconds) noexcept
    {
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto s = ring[(size_t) ((write + i) % fftSize)];
            energy += (double) s * s;
            work[(size_t) i] = s * window[(size_t) i];
        }
        std::fill (work.begin() + fftSize, work.end(), 0.0f);

        const auto rms = (float) std::sqrt (energy / fftSize);
        if (rms < silenceRms)
        {
            sinceSignal = std::min (1.0e9, sinceSignal + hopSeconds);
            return;
        }

        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        float loudest = 0.0f;
        for (int k = lowBin; k <= highBin; ++k)
            loudest = std::max (loudest, work[(size_t) k]);

        std::array<float, 12> frame {};
        float sum = 0.0f;
        const auto floor = loudest * peakFloor;
        for (int k = lowBin; k <= highBin; ++k)
        {
            const auto m = work[(size_t) k];
            if (m > floor && m > work[(size_t) k - 1] && m >= work[(size_t) k + 1])
            {
                const auto v = m * binWeight[(size_t) k];
                frame[(size_t) binClass[(size_t) k]] += v;
                sum += v;
            }
        }

        if (sum <= 0.0f)
        {
            sinceSignal = std::min (1.0e9, sinceSignal + hopSeconds);
            return;
        }

        const auto a = (float) std::exp (-hopSeconds / std::max (0.1, tauSeconds));
        const auto af = (float) std::exp (-hopSeconds / std::max (0.1, fastTauSeconds));
        for (size_t pc = 0; pc < 12; ++pc)
        {
            chroma[pc] = a * chroma[pc] + (1.0f - a) * (frame[pc] / sum);
            fastChroma[pc] = af * fastChroma[pc] + (1.0f - af) * (frame[pc] / sum);
        }

        evidence = std::min (evidenceCap, evidence + hopSeconds);
        sinceSignal = 0.0;
    }

    //==============================================================================
    void KeyDetector::prepare (double sampleRate)
    {
        sidechainChroma.prepare (sampleRate, (float) lastA4);
        vocalChroma.prepare (sampleRate, (float) lastA4);
        reset();
    }

    void KeyDetector::reset() noexcept
    {
        sidechainChroma.reset();
        vocalChroma.reset();
        current = -1;
        lastSource = 0;
        credit.fill (0.0f);
        key.store (-1);
        candidate.store (-1);
        activeSource.store (0);
        confidence.store (0.0f);
        sidechainLive.store (false);
    }

    void KeyDetector::correlate (const std::array<float, 12>& chroma, std::array<float, 24>& out) noexcept
    {
        float mean = 0.0f;
        for (auto v : chroma)
            mean += v / 12.0f;
        std::array<float, 12> x {};
        float norm = 0.0f;
        for (size_t pc = 0; pc < 12; ++pc)
        {
            x[pc] = chroma[pc] - mean;
            norm += x[pc] * x[pc];
        }
        norm = std::sqrt (norm);
        const auto& p = profiles();
        for (size_t k = 0; k < 24; ++k)
        {
            if (norm <= 1.0e-9f)
            {
                out[k] = 0.0f;
                continue;
            }
            float dot = 0.0f;
            for (size_t pc = 0; pc < 12; ++pc)
                dot += x[pc] * p.rows[k][pc];
            out[k] = dot / norm;
        }
    }

    juce::String KeyDetector::keyName (int k)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (k < 0 || k >= 24)
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")); // em dash
        return juce::String (names[k % 12]) + (k < 12 ? " major" : " minor");
    }

    void KeyDetector::process (const float* const* sidechain, int sidechainChannels,
                               const float* const* vocal, int vocalChannels, int numSamples,
                               const Settings& s) noexcept
    {
        if (numSamples <= 0)
            return;

        if (std::abs ((double) s.a4Hz - lastA4) > 1.0e-3)
        {
            lastA4 = s.a4Hz;
            sidechainChroma.setA4 (s.a4Hz);
            vocalChroma.setA4 (s.a4Hz);
        }

        const auto sens = std::clamp (s.sensitivity, 0.0f, 1.0f);

        int sidechainFrames = 0, vocalFrames = 0;
        const auto hasSidechain = sidechain != nullptr && sidechainChannels > 0 && sidechain[0] != nullptr;
        if (hasSidechain && s.source != KeySource::vocal)
            sidechainFrames = sidechainChroma.push (sidechain[0], sidechainChannels > 1 ? sidechain[1] : nullptr, numSamples, slowTau, fastTau);
        if (vocal != nullptr && vocalChannels > 0 && s.source != KeySource::sidechain)
            vocalFrames = vocalChroma.push (vocal[0], vocalChannels > 1 ? vocal[1] : nullptr, numSamples, 2.0 * slowTau, 2.0 * fastTau);

        const auto scLive = hasSidechain && sidechainChroma.getEvidenceSeconds() > 0.0
                         && sidechainChroma.getSecondsSinceSignal() < sidechainHoldoffSeconds;
        sidechainLive.store (scLive, std::memory_order_relaxed);

        const auto useSidechain = s.source == KeySource::sidechain || (s.source == KeySource::automatic && scLive);
        const auto& from = useSidechain ? sidechainChroma : vocalChroma;
        const auto frames = useSidechain ? sidechainFrames : vocalFrames;

        const auto voicedNow = from.getSecondsSinceSignal() < 1.0;
        activeSource.store (voicedNow ? (useSidechain ? 1 : 2) : 0, std::memory_order_relaxed);

        // A change of source invalidates any challenger credit earned on the other source.
        if (const auto src = useSidechain ? 1 : 2; src != lastSource)
        {
            lastSource = src;
            credit.fill (0.0f);
        }

        // One decision per new voiced frame; silence holds the current key.
        if (frames > 0 && from.getSecondsSinceSignal() == 0.0)
            decide (from, ! useSidechain, sens, frames * from.getHopSeconds());
    }

    void KeyDetector::decide (const ChromaAccumulator& from, bool fromVocal, float sens, double seconds) noexcept
    {
        std::array<float, 24> r {}, rf {};
        correlate (from.getChroma(), r);
        correlate (from.getFastChroma(), rf);
        const auto best = argmax (r);
        const auto fastBest = argmax (rf);
        candidate.store (best, std::memory_order_relaxed);

        const auto weak = fromVocal ? vocalWeakness : 1.0;
        const auto margin = (float) ((marginSteady + (marginResponsive - marginSteady) * sens) * weak);
        const auto threshold = (float) ((thresholdSteady + (thresholdResponsive - thresholdSteady) * sens) * weak);

        if (current < 0)
        {
            if (from.getEvidenceSeconds() >= firstKeyEvidence * weak && r[(size_t) best] >= minCorrelation && best == fastBest)
            {
                current = best;
                credit.fill (0.0f);
                changes.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else
        {
            // CUSUM: only the key that leads on both averages earns credit; everyone else decays.
            const auto dt = (float) seconds;
            for (size_t k = 0; k < 24; ++k)
            {
                auto inc = -margin;
                if ((int) k == best && best == fastBest && (int) k != current && r[k] >= minCorrelation)
                    inc = r[k] - r[(size_t) current] - margin;
                credit[k] = std::max (0.0f, credit[k] + inc * dt);
            }
            credit[(size_t) current] = 0.0f;

            if (credit[(size_t) best] >= threshold)
            {
                current = best;
                credit.fill (0.0f);
                changes.fetch_add (1, std::memory_order_relaxed);
            }
        }

        key.store (current, std::memory_order_relaxed);
        confidence.store (current >= 0 ? std::clamp (r[(size_t) current], 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    }
}
