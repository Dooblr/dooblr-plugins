#include "ThroatEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

        while (audioBusy_.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        old.reset();
    }

    void ThroatEngine::publishPhrase (std::unique_ptr<phonetics::PublishedPhrase> next)
    {
        if (next == nullptr)
            next = std::make_unique<phonetics::PublishedPhrase>();

        auto* raw = next->empty() ? nullptr : next.get();
        const int count = raw != nullptr ? int (raw->plans.size()) : 0;
        std::unique_ptr<phonetics::PublishedPhrase> old;
        {
            std::lock_guard<std::mutex> lock (swapMutex_);
            old = std::move (ownedPhrase_);
            ownedPhrase_ = std::move (next);
            livePhrase_.store (raw, std::memory_order_release);
            sylCount_.store (count, std::memory_order_release);
        }

        while (audioBusy_.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        old.reset();
        restartFlag_.store (1u, std::memory_order_release);
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
        talk_ = Talk::Off;
        syl_ = -1;
        if (auto* v = live())
        {
            v->clearScore();
            v->reset();
        }
        sylIndex_.store (0, std::memory_order_release);
        {
            const auto seq = readoutSeq_.load (std::memory_order_relaxed);
            readoutSeq_.store (seq + 1, std::memory_order_release);
            readout_[0] = 0;
            readoutSeq_.store (seq + 2, std::memory_order_release);
        }
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
        if (s.speechMode < 0) s.speechMode = 0;
        if (s.speechMode > 1) s.speechMode = 1;
        s.speechRate = float (clamp (double (fin (s.speechRate, 1.0f)), 0.25, 2.5));
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
            if (talk_ == Talk::Off)
                applyVowel (*v, settings_);
            else
                v->scoreNasal = settings_.nasal;
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

    double ThroatEngine::currentHz() const
    {
        const auto note = currentNote_ >= 0 ? currentNote_ : 60;
        return safeHz (double (note) + double (bendSemis_));
    }

    void ThroatEngine::clearTalk (HumanVoice* v)
    {
        talk_ = Talk::Off;
        if (v != nullptr)
            v->clearScore();
    }

    void ThroatEngine::startSpeak (HumanVoice& v, const phonetics::PublishedPhrase& ph, int midi)
    {
        (void) midi;
        talk_ = Talk::Speak;
        v.scoreGestures = ph.speak.gestures.data();
        v.scoreCount = ph.speak.gestures.size();
        v.scoreT = 0.0;
        v.scoreRate = settings_.speechRate;
        v.scoreF0Scale = currentHz() / js_max (1.0, ph.speak.f0Ref);
        v.scoreF0Override = 0.0;
        v.scoreNasal = settings_.nasal;
        v.scoreActive = ! ph.speak.gestures.empty();
        syl_ = 0;
        sylIndex_.store (0, std::memory_order_release);
    }

    void ThroatEngine::startHold (HumanVoice& v, const phonetics::PublishedPhrase& ph, int syl, int midi)
    {
        (void) midi;
        if (ph.plans.empty()) { clearTalk (&v); return; }
        const auto n = int (ph.plans.size());
        syl = ((syl % n) + n) % n;
        syl_ = syl;
        const auto& plan = ph.plans[size_t (syl)];
        talk_ = Talk::Hold;
        v.scoreGestures = plan.hold.data();
        v.scoreCount = plan.hold.size();
        v.scoreT = phonetics::kLiveT0;
        v.scoreRate = 1.0;
        v.scoreF0Scale = 1.0;
        v.scoreF0Override = currentHz();
        v.scoreNasal = settings_.nasal;
        v.scoreActive = ! plan.hold.empty();
        sylIndex_.store (syl, std::memory_order_release);
    }

    void ThroatEngine::startCoda (HumanVoice& v, const phonetics::PublishedPhrase& ph, int syl, int midi)
    {
        (void) midi;
        if (ph.plans.empty()) { clearTalk (&v); return; }
        const auto n = int (ph.plans.size());
        syl = ((syl % n) + n) % n;
        const auto& plan = ph.plans[size_t (syl)];
        if (plan.coda.empty())
        {
            clearTalk (&v);
            if (stage_ != Stage::idle) stage_ = Stage::release;
            return;
        }
        talk_ = Talk::Coda;
        codaSyl_ = syl;
        v.scoreGestures = plan.coda.data();
        v.scoreCount = plan.coda.size();
        v.scoreT = phonetics::kLiveT0;
        v.scoreRate = js_max (0.25, settings_.speechRate);
        v.scoreF0Scale = 1.0;
        v.scoreF0Override = currentHz();
        v.scoreNasal = settings_.nasal;
        v.scoreActive = true;
        sylIndex_.store (syl, std::memory_order_release);
    }

    void ThroatEngine::handleRestart()
    {
        if (restartFlag_.exchange (0u, std::memory_order_acq_rel) == 0)
            return;
        syl_ = -1;
        nextSyl_ = 0;
        codaSyl_ = 0;
        if (auto* v = live())
            v->clearScore();
        talk_ = Talk::Off;
        sylIndex_.store (0, std::memory_order_release);
    }

    void ThroatEngine::copyReadout (char* buf, size_t cap) const noexcept
    {
        if (buf == nullptr || cap == 0) return;
        for (;;)
        {
            const auto s1 = readoutSeq_.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            std::strncpy (buf, readout_, cap);
            buf[cap - 1] = 0;
            const auto s2 = readoutSeq_.load (std::memory_order_acquire);
            if (s1 == s2) return;
        }
    }

    void ThroatEngine::updateReadout (const phonetics::PublishedPhrase* ph)
    {
        char tmp[64] {};
        if (ph == nullptr || talk_ == Talk::Off)
            tmp[0] = 0;
        else if (talk_ == Talk::Speak)
        {
            auto* v = live();
            const auto t = v != nullptr ? v->scoreT : 0.0;
            auto ipa = phonetics::segmentAt (ph->speak.segments, t);
            int idx = 0, n = int (ph->speak.segments.size());
            for (int i = 0; i < n; ++i)
                if (t >= ph->speak.segments[size_t (i)].t0)
                    idx = i;
            std::snprintf (tmp, sizeof (tmp), "%d/%d  %s", n ? idx + 1 : 0, n, ipa.c_str());
        }
        else if (! ph->plans.empty() && syl_ >= 0)
        {
            const auto n = int (ph->plans.size());
            const auto i = ((syl_ % n) + n) % n;
            const auto& plan = ph->plans[size_t (i)];
            auto* v = live();
            const auto t = v != nullptr ? v->scoreT : 0.0;
            const auto& segs = (talk_ == Talk::Coda || talk_ == Talk::CodaThenNext) ? plan.codaSegments : plan.holdSegments;
            auto ipa = phonetics::segmentAt (segs, t);
            if (ipa.empty()) ipa = plan.readout;
            std::snprintf (tmp, sizeof (tmp), "%d/%d  %s", i + 1, n, ipa.c_str());
        }

        const auto seq = readoutSeq_.load (std::memory_order_relaxed);
        readoutSeq_.store (seq + 1, std::memory_order_release);
        std::strncpy (readout_, tmp, sizeof (readout_));
        readout_[sizeof (readout_) - 1] = 0;
        readoutSeq_.store (seq + 2, std::memory_order_release);
    }

    void ThroatEngine::tickPhrase (HumanVoice& v, const phonetics::PublishedPhrase* ph)
    {
        if (ph == nullptr || talk_ == Talk::Off)
            return;
        if (talk_ == Talk::Speak)
        {
            if (v.scoreT >= ph->speak.end)
            {
                clearTalk (&v);
                if (held_.empty() && stage_ != Stage::idle)
                    stage_ = Stage::release;
                else if (held_.empty())
                    stage_ = Stage::release;
            }
            return;
        }
        if ((talk_ == Talk::Coda || talk_ == Talk::CodaThenNext) && ! ph->plans.empty())
        {
            const auto n = int (ph->plans.size());
            const auto i = ((codaSyl_ % n) + n) % n;
            const auto end = ph->plans[size_t (i)].codaEnd;
            if (v.scoreT >= end)
            {
                if (talk_ == Talk::CodaThenNext)
                    startHold (v, *ph, nextSyl_, currentNote_);
                else
                {
                    clearTalk (&v);
                    if (stage_ != Stage::idle)
                        stage_ = Stage::release;
                }
            }
        }
    }

    void ThroatEngine::noteOn (int midiNote, float velocity)
    {
        handleRestart();
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

        auto* ph = livePhrase();
        auto* v = live();
        if (ph != nullptr && v != nullptr && ! ph->empty())
        {
            if (settings_.speechMode == 1) // Speak
            {
                startSpeak (*v, *ph, midiNote);
            }
            else
            {
                const int n = int (ph->plans.size());
                if (n <= 0)
                    clearTalk (v);
                else if (talk_ == Talk::Hold)
                {
                    nextSyl_ = (syl_ + 1) % n;
                    codaSyl_ = syl_;
                    if (ph->plans[size_t (codaSyl_)].coda.empty())
                        startHold (*v, *ph, nextSyl_, midiNote);
                    else
                    {
                        startCoda (*v, *ph, codaSyl_, midiNote);
                        talk_ = Talk::CodaThenNext;
                    }
                }
                else
                {
                    syl_ = (syl_ + 1) % n;
                    if (syl_ < 0) syl_ = 0;
                    startHold (*v, *ph, syl_, midiNote);
                }
            }
        }
    }

    void ThroatEngine::noteOff (int midiNote)
    {
        midiNote = std::clamp (midiNote, 0, 127);
        held_.erase (std::remove (held_.begin(), held_.end(), midiNote), held_.end());
        auto* ph = livePhrase();
        auto* v = live();
        if (held_.empty())
        {
            currentNote_ = -1;
            if (talk_ == Talk::Speak)
            {
                clearTalk (v);
                if (stage_ != Stage::idle) stage_ = Stage::release;
            }
            else if (talk_ == Talk::Hold && ph != nullptr && v != nullptr)
            {
                startCoda (*v, *ph, syl_, midiNote);
                if (talk_ == Talk::Off && stage_ != Stage::idle)
                    stage_ = Stage::release;
                // keep envelope in sustain until coda finishes (tickPhrase releases)
            }
            else if (stage_ != Stage::idle)
                stage_ = Stage::release;
        }
        else
        {
            currentNote_ = held_.back();
            if (v != nullptr)
            {
                auto src = v->source;
                src.f0 = safeHz (double (currentNote_) + double (bendSemis_));
                v->set_source (src);
                if (talk_ == Talk::Hold || talk_ == Talk::Coda || talk_ == Talk::CodaThenNext)
                    v->scoreF0Override = src.f0;
            }
        }
    }

    void ThroatEngine::allNotesOff()
    {
        held_.clear();
        currentNote_ = -1;
        if (auto* v = live())
            clearTalk (v);
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
            if (talk_ != Talk::Off)
            {
                if (talk_ == Talk::Speak)
                {
                    if (auto* ph = livePhrase())
                        v->scoreF0Scale = src.f0 / js_max (1.0, ph->speak.f0Ref);
                }
                else
                    v->scoreF0Override = src.f0;
            }
        }
    }

    void ThroatEngine::process (float* left, float* right, int n)
    {
        if (n <= 0 || left == nullptr)
            return;
        if (right == nullptr)
            right = left;

        handleRestart();

        auto* voice = live();
        if (voice == nullptr)
        {
            for (int i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
            lastPeak_ = 0.0f;
            return;
        }

        if (tmp_.empty())
        {
            for (int i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
            lastPeak_ = 0.0f;
            return;
        }

        auto* ph = livePhrase();
        if (talk_ != Talk::Off)
        {
            voice->scoreRate = (talk_ == Talk::Speak) ? settings_.speechRate : voice->scoreRate;
            voice->scoreNasal = settings_.nasal;
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
                tickPhrase (*voice, ph);
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
        updateReadout (ph);
    }
}
