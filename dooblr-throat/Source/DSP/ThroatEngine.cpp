#include "ThroatEngine.h"
#include <algorithm>
#include <cmath>

namespace dooblrthroat::dsp
{
    void ThroatEngine::prepare (double sampleRate, int maxBlock)
    {
        sampleRate_ = sampleRate;
        tmp_.assign (size_t (std::max (maxBlock, 512)), 0.0f);
        rebuildVoice();
        reset();
    }

    void ThroatEngine::rebuildVoice()
    {
        const auto body = settings_.body.body();
        voice_ = std::make_unique<HumanVoice> (sampleRate_, body, oversample_for (sampleRate_));
        appliedBody_ = settings_;
        applySource();
        applyVowel();
        bodyDirty_ = false;
    }

    void ThroatEngine::reset()
    {
        held_.clear();
        currentNote_ = -1;
        bendSemis_ = 0.0f;
        gate_ = env_ = 0.0f;
        stage_ = Stage::idle;
        if (voice_) voice_->reset();
    }

    void ThroatEngine::setSettings (const ThroatSettings& s)
    {
        const bool bodyChanged = s.body.female != settings_.body.female || s.body.child != settings_.body.child;
        settings_ = s;
        if (bodyChanged)
            rebuildVoice(); // off the audio thread
        else if (voice_)
        {
            applySource();
            applyVowel();
        }
    }

    void ThroatEngine::applySource()
    {
        if (! voice_) return;
        VoiceSource src = voice_->source;
        src.breathiness = settings_.breathiness;
        src.tension = settings_.tension;
        src.brightness = settings_.brightness;
        src.fry = settings_.fry;
        src.roughness = settings_.roughness;
        src.pressure = settings_.pressure;
        src.vibrato_depth = settings_.vibratoDepth;
        src.vibrato_rate = settings_.vibratoRate;
        src.glide = settings_.glide;
        src.voiced = settings_.voiced;
        if (currentNote_ >= 0)
            src.f0 = midi_to_hz (double (currentNote_) + double (bendSemis_));
        voice_->set_source (src);
    }

    void ThroatEngine::applyVowel()
    {
        if (! voice_) return;
        ArticPatch p;
        if (settings_.vowelIndex >= 0 && settings_.vowelIndex < int (N_VOWELS))
            p = vowel_patch (size_t (settings_.vowelIndex), settings_.body);
        else
            p = vowel_at (settings_.vowelHeight, settings_.vowelBack, settings_.body);
        if (settings_.nasal > 0.0f)
            p.set (ArticKey::Velum, settings_.nasal);
        voice_->set_articulation (p, settings_.tipFree);
    }

    void ThroatEngine::noteOn (int midiNote, float velocity)
    {
        vel_ = std::clamp (velocity, 0.0f, 1.0f);
        const bool legato = ! held_.empty();
        held_.erase (std::remove (held_.begin(), held_.end(), midiNote), held_.end());
        held_.push_back (midiNote);
        currentNote_ = midiNote;
        if (voice_)
        {
            auto src = voice_->source;
            src.f0 = midi_to_hz (double (midiNote) + double (bendSemis_));
            src.pressure = settings_.pressure * (0.65 + 0.7 * vel_);
            voice_->set_source (src);
        }
        if (! legato || stage_ == Stage::idle || stage_ == Stage::release)
        {
            stage_ = Stage::attack;
            env_ = (stage_ == Stage::release) ? env_ : 0.0f;
        }
    }

    void ThroatEngine::noteOff (int midiNote)
    {
        held_.erase (std::remove (held_.begin(), held_.end(), midiNote), held_.end());
        if (held_.empty())
        {
            currentNote_ = -1;
            if (stage_ != Stage::idle) stage_ = Stage::release;
        }
        else
        {
            currentNote_ = held_.back();
            if (voice_)
            {
                auto src = voice_->source;
                src.f0 = midi_to_hz (double (currentNote_) + double (bendSemis_));
                voice_->set_source (src);
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
        bendSemis_ = semis;
        if (voice_ && currentNote_ >= 0)
        {
            auto src = voice_->source;
            src.f0 = midi_to_hz (double (currentNote_) + double (bendSemis_));
            voice_->set_source (src);
        }
    }

    void ThroatEngine::process (float* left, float* right, int n)
    {
        if (! voice_)
        {
            for (int i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
            lastPeak_ = 0.0f;
            return;
        }

        const auto sr = float (sampleRate_);
        const auto attackS = js_max (1e-4, settings_.attack);
        const auto decayS = js_max (1e-4, settings_.decay);
        const auto releaseS = js_max (1e-4, settings_.release);
        const auto sustain = clamp (settings_.sustain, 0.0, 1.0);

        // Advance the envelope in small chunks so long blocks stay smooth, then render.
        if (tmp_.size() < size_t (n)) tmp_.resize (size_t (n));
        auto& tmp = tmp_;
        const int step = 16;
        int i = 0;
        float peak = 0.0f;
        const auto gain = float (db_to_gain (settings_.outputDb));
        while (i < n)
        {
            const int m = std::min (step, n - i);
            // one envelope sample at the start of the chunk
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
            gate_ = env_;
            voice_->set_gate (gate_);
            voice_->render (tmp.data() + i, size_t (m));
            for (int k = 0; k < m; ++k)
            {
                const auto x = tmp[size_t (i + k)] * gain;
                tmp[size_t (i + k)] = x;
                peak = std::max (peak, std::abs (x));
            }
            i += m;
        }
        for (int k = 0; k < n; ++k)
            left[k] = right[k] = tmp[size_t (k)];
        lastPeak_ = peak;
    }
}
