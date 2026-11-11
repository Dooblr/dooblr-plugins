#include "ThroatEngine.h"
#include <algorithm>
#include <cmath>
#include <thread>

namespace dooblrthroat::dsp
{
    namespace
    {
        constexpr double kMinHz = 1.0, kMaxHz = 20000.0;
        constexpr int kMaxTmp = 4096;

        double safeHz (double midi)
        {
            if (! is_finite (midi))
                return 110.0;
            return clamp (midi_to_hz (midi), kMinHz, kMaxHz);
        }
    }

    void ThroatEngine::beginAudioCallback() noexcept
    {
        audioBusy_.store (1u, std::memory_order_release);
    }

    void ThroatEngine::endAudioCallback() noexcept
    {
        audioBusy_.store (0u, std::memory_order_release);
    }

    void ThroatEngine::publishVoice (std::unique_ptr<HumanVoice> next)
    {
        if (next == nullptr)
            return;

        auto* raw = next.get();
        std::unique_ptr<HumanVoice> old;
        {
            std::lock_guard<std::mutex> lock (swapMutex_);
            old = std::move (owned_);
            owned_ = std::move (next);
            live_.store (raw, std::memory_order_release);
        }

        // The audio thread sets audioBusy_ before loading live_. After this store,
        // any new callback uses the new voice. If a callback is in flight with the
        // old pointer, busy is 1 until it returns — only then is the old voice freed,
        // and always on this (non-audio) thread.
        while (audioBusy_.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        old.reset();
    }

    void ThroatEngine::rebuildVoiceOffAudioThread (const ThroatSettings& incoming, bool force)
    {
        auto s = incoming;
        sanitizeSettings (s);
        auto* cur = live();
        const bool bodyChanged = cur == nullptr
            || std::abs (appliedFemale_.load (std::memory_order_acquire) - s.body.female) > 1.0e-6f
            || std::abs (appliedChild_.load (std::memory_order_acquire) - s.body.child) > 1.0e-6f;
        if (! force && ! bodyChanged)
            return;

        const auto body = s.body.body();
        auto next = std::make_unique<HumanVoice> (sampleRate_, body, oversample_for (sampleRate_));
        applySource (*next, s, false);
        applyVowel (*next, s);
        next->set_gate (0.0);
        publishVoice (std::move (next));
        appliedFemale_.store (s.body.female, std::memory_order_release);
        appliedChild_.store (s.body.child, std::memory_order_release);
    }

    void ThroatEngine::prepare (double sampleRate, int maxBlock)
    {
        if (! is_finite (sampleRate) || sampleRate < 8000.0)
            sampleRate = 8000.0;
        if (sampleRate > 192000.0)
            sampleRate = 192000.0;
        sampleRate_ = sampleRate;
        const int cap = std::max (std::max (maxBlock, kMaxTmp), 512);
        tmp_.assign (size_t (cap), 0.0f);
        rebuildVoiceOffAudioThread (settings_, true);
        reset();
    }

    void ThroatEngine::reset()
    {
        held_.clear();
        currentNote_ = -1;
        bendSemis_ = 0.0f;
        gate_ = env_ = 0.0f;
        stage_ = Stage::idle;
        if (auto* v = live())
            v->reset();
    }

    void ThroatEngine::sanitizeSettings (ThroatSettings& s)
    {
        auto fin = [] (float x, float fallback) { return std::isfinite (x) ? x : fallback; };
        s.vowelHeight = float (clamp (double (fin (s.vowelHeight, 0.5f)), 0.0, 1.0));
        s.vowelBack = float (clamp (double (fin (s.vowelBack, 0.5f)), 0.0, 1.0));
        s.breathiness = float (clamp (double (fin (s.breathiness, 0.15f)), 0.0, 1.0));
        s.tension = float (clamp (double (fin (s.tension, 0.5f)), 0.0, 1.0));
        s.brightness = float (clamp (double (fin (s.brightness, 0.25f)), 0.0, 1.0));
        s.fry = float (clamp (double (fin (s.fry, 0.0f)), 0.0, 1.0));
        s.roughness = float (clamp (double (fin (s.roughness, 0.1f)), 0.0, 1.0));
        s.pressure = float (clamp (double (fin (s.pressure, 8.0f)), 0.0, 40.0));
        s.vibratoDepth = float (clamp (double (fin (s.vibratoDepth, 0.0f)), 0.0, 2.0));
        s.vibratoRate = float (clamp (double (fin (s.vibratoRate, 6.0f)), 0.1, 20.0));
        s.glide = float (clamp (double (fin (s.glide, 0.06f)), 0.001, 2.0));
        s.nasal = fin (s.nasal, -1.0f);
        s.attack = float (clamp (double (fin (s.attack, 0.03f)), 1.0e-4, 5.0));
        s.decay = float (clamp (double (fin (s.decay, 0.08f)), 1.0e-4, 5.0));
        s.sustain = float (clamp (double (fin (s.sustain, 0.85f)), 0.0, 1.0));
        s.release = float (clamp (double (fin (s.release, 0.12f)), 1.0e-4, 5.0));
        s.outputDb = float (clamp (double (fin (s.outputDb, -6.0f)), -60.0, 24.0));
        if (! std::isfinite (s.body.female)) s.body.female = 0.0f;
        if (! std::isfinite (s.body.child)) s.body.child = 0.0f;
        s.body.female = float (clamp (double (s.body.female), 0.0, 1.0));
        s.body.child = float (clamp (double (s.body.child), 0.0, 1.0));
        if (s.vowelIndex < -1) s.vowelIndex = -1;
        if (s.vowelIndex >= int (N_VOWELS)) s.vowelIndex = int (N_VOWELS) - 1;
    }

    void ThroatEngine::setSettings (const ThroatSettings& incoming)
    {
        auto s = incoming;
        sanitizeSettings (s);
        settings_ = s;
        applyToLive();
    }

    void ThroatEngine::applyToLive()
    {
        if (auto* v = live())
        {
            applySource (*v, settings_);
            applyVowel (*v, settings_);
        }
    }

    void ThroatEngine::applySource (HumanVoice& voice, const ThroatSettings& s, bool useLiveNote)
    {
        VoiceSource src = voice.source;
        src.breathiness = s.breathiness;
        src.tension = s.tension;
        src.brightness = s.brightness;
        src.fry = s.fry;
        src.roughness = s.roughness;
        src.pressure = s.pressure;
        src.vibrato_depth = s.vibratoDepth;
        src.vibrato_rate = s.vibratoRate;
        src.glide = s.glide;
        src.voiced = s.voiced;
        if (useLiveNote && currentNote_ >= 0)
            src.f0 = safeHz (double (currentNote_) + double (bendSemis_));
        voice.set_source (src);
    }

    void ThroatEngine::applyVowel (HumanVoice& voice, const ThroatSettings& s)
    {
        ArticPatch p;
        if (s.vowelIndex >= 0 && s.vowelIndex < int (N_VOWELS))
            p = vowel_patch (size_t (s.vowelIndex), s.body);
        else
            p = vowel_at (s.vowelHeight, s.vowelBack, s.body);
        if (s.nasal > 0.0f)
            p.set (ArticKey::Velum, s.nasal);
        voice.set_articulation (p, s.tipFree);
    }

    void ThroatEngine::noteOn (int midiNote, float velocity)
    {
        midiNote = std::clamp (midiNote, 0, 127);
        vel_ = std::clamp (std::isfinite (velocity) ? velocity : 0.0f, 0.0f, 1.0f);
        const bool legato = ! held_.empty();
        held_.erase (std::remove (held_.begin(), held_.end(), midiNote), held_.end());
        held_.push_back (midiNote);
        currentNote_ = midiNote;
        if (auto* v = live())
        {
            auto src = v->source;
            src.f0 = safeHz (double (midiNote) + double (bendSemis_));
            src.pressure = settings_.pressure * (0.65 + 0.7 * vel_);
            v->set_source (src);
        }
        const bool fromRelease = stage_ == Stage::release;
        if (! legato || stage_ == Stage::idle || fromRelease)
        {
            stage_ = Stage::attack;
            env_ = fromRelease ? env_ : 0.0f;
        }
    }

    void ThroatEngine::noteOff (int midiNote)
    {
        midiNote = std::clamp (midiNote, 0, 127);
        held_.erase (std::remove (held_.begin(), held_.end(), midiNote), held_.end());
        if (held_.empty())
        {
            currentNote_ = -1;
            if (stage_ != Stage::idle) stage_ = Stage::release;
        }
        else
        {
            currentNote_ = held_.back();
            if (auto* v = live())
            {
                auto src = v->source;
                src.f0 = safeHz (double (currentNote_) + double (bendSemis_));
                v->set_source (src);
            }
        }
    }

    void ThroatEngine::allNotesOff()
    {
        held_.clear();
        currentNote_ = -1;
        if (stage_ != Stage::idle) stage_ = Stage::release;
    }

    void ThroatEngine::pitchBend (float semis)
    {
        if (! std::isfinite (semis))
            semis = 0.0f;
        bendSemis_ = float (clamp (double (semis), -48.0, 48.0));
        if (auto* v = live(); v != nullptr && currentNote_ >= 0)
        {
            auto src = v->source;
            src.f0 = safeHz (double (currentNote_) + double (bendSemis_));
            v->set_source (src);
        }
    }

    void ThroatEngine::process (float* left, float* right, int n)
    {
        if (n <= 0 || left == nullptr)
            return;
        if (right == nullptr)
            right = left;

        auto* voice = live();
        if (voice == nullptr)
        {
            for (int i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
            lastPeak_ = 0.0f;
            return;
        }

        if (tmp_.empty())
        {
            // prepare() was not called; do not allocate on the audio thread.
            for (int i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
            lastPeak_ = 0.0f;
            return;
        }

        const auto sr = float (sampleRate_ > 0.0 ? sampleRate_ : 48000.0);
        const auto attackS = js_max (1e-4, settings_.attack);
        const auto decayS = js_max (1e-4, settings_.decay);
        const auto releaseS = js_max (1e-4, settings_.release);
        const auto sustain = clamp (settings_.sustain, 0.0, 1.0);
        const auto gain = float (db_to_gain (settings_.outputDb));
        const int cap = int (tmp_.size());
        const int step = 16;
        float peak = 0.0f;
        int done = 0;

        // Live may call with a block larger than prepareToPlay's maximum. Slice
        // into the preallocated tmp_ rather than resizing on the audio thread.
        while (done < n)
        {
            const int chunk = std::min (cap, n - done);
            auto& tmp = tmp_;
            int i = 0;
            while (i < chunk)
            {
                const int m = std::min (step, chunk - i);
                const auto dt = float (m) / sr;
                switch (stage_)
                {
                    case Stage::idle: env_ = 0.0f; break;
                    case Stage::attack:
                        env_ += dt / float (attackS);
                        if (env_ >= 1.0f) { env_ = 1.0f; stage_ = Stage::decay; }
                        break;
                    case Stage::decay:
                        env_ += (float (sustain) - env_) * (1.0f - float (exp (-dt / decayS)));
                        if (std::abs (env_ - float (sustain)) < 0.002f) { env_ = float (sustain); stage_ = Stage::sustain; }
                        break;
                    case Stage::sustain: env_ = float (sustain); break;
                    case Stage::release:
                        env_ *= float (exp (-dt / releaseS));
                        if (env_ < 1e-4f) { env_ = 0.0f; stage_ = Stage::idle; }
                        break;
                }
                if (! std::isfinite (env_)) env_ = 0.0f;
                gate_ = env_;
                voice->set_gate (gate_);
                voice->render (tmp.data() + i, size_t (m));
                for (int k = 0; k < m; ++k)
                {
                    auto x = tmp[size_t (i + k)] * gain;
                    if (! std::isfinite (x)) x = 0.0f;
                    tmp[size_t (i + k)] = x;
                    peak = std::max (peak, std::abs (x));
                }
                i += m;
            }
            for (int k = 0; k < chunk; ++k)
                left[done + k] = right[done + k] = tmp[size_t (k)];
            done += chunk;
        }
        lastPeak_ = peak;
    }
}
