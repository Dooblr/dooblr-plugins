#include "ProvenVoice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <espeak-ng/speak_lib.h>
#include <onnxruntime_cxx_api.h>

#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/harvest.h"
#include "world/stonemask.h"
#include "world/synthesis.h"

#if defined(__APPLE__)
 #include <mach-o/dyld.h>
#endif

#ifndef DOOBLRTHROAT_ESPEAK_DATA_PARENT
 #define DOOBLRTHROAT_ESPEAK_DATA_PARENT ""
#endif
#ifndef DOOBLRTHROAT_PIPER_ONNX
 #define DOOBLRTHROAT_PIPER_ONNX ""
#endif

namespace dooblrthroat::dsp
{
    namespace
    {
        std::mutex gInitMutex;
        std::mutex gSynthMutex; // espeak-ng and ORT are process-global and not re-entrant
        int gEngineCount = 0;
        bool gEspeakReady = false;
        int gEspeakRate = 22050;
        std::unique_ptr<Ort::Env> gOrtEnv;
        std::unique_ptr<Ort::Session> gPiperSession;
        std::unique_ptr<Ort::SessionOptions> gPiperOpts;
        std::unordered_map<char32_t, int64_t> gPhonemeIds;
        float gNoiseScale = 0.667f, gLengthScale = 1.0f, gNoiseW = 0.8f;
        int gPiperRate = 16000;
        std::string gPiperVoice = "en-us";
        bool gPiperConfigLoaded = false;

        std::vector<float>* gEspeakSink = nullptr;

        std::string envOr (const char* key, const char* fallback)
        {
            if (const char* v = std::getenv (key); v != nullptr && v[0] != 0)
                return v;
            return fallback ? fallback : "";
        }

        std::string exeDir()
        {
#if defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath (nullptr, &size);
            std::string buf (size + 1, '\0');
            if (_NSGetExecutablePath (buf.data(), &size) == 0)
            {
                buf.resize (std::strlen (buf.c_str()));
                auto slash = buf.find_last_of ('/');
                if (slash != std::string::npos)
                    return buf.substr (0, slash);
            }
#endif
            return ".";
        }

        bool dirHasData (const std::string& parent)
        {
            if (parent.empty()) return false;
            std::ifstream a (parent + "/espeak-ng-data/en_dict");
            std::ifstream b (parent + "/en_dict");
            return a.good() || b.good();
        }

        std::string espeakDataParent()
        {
            auto env = envOr ("DOOBLRTHROAT_ESPEAK_DATA", "");
            if (dirHasData (env))
            {
                if (std::ifstream (env + "/en_dict"))
                {
                    auto slash = env.find_last_of ('/');
                    return slash == std::string::npos ? env : env.substr (0, slash);
                }
                return env;
            }
            const char* compiled = DOOBLRTHROAT_ESPEAK_DATA_PARENT;
            if (dirHasData (compiled)) return compiled;
            auto exe = exeDir();
            if (dirHasData (exe)) return exe;
            if (dirHasData (exe + "/../Resources")) return exe + "/../Resources";
            return compiled ? compiled : "";
        }

        bool fileExists (const std::string& path)
        {
            if (path.empty()) return false;
            std::ifstream in (path);
            return bool (in);
        }

        std::string piperOnnxPath()
        {
            auto env = envOr ("DOOBLRTHROAT_PIPER_MODEL", "");
            if (fileExists (env)) return env;
            const auto exe = exeDir();
            const char* candidates[] = {
                "/../Resources/en_US-lessac-low.onnx",
                "/en_US-lessac-low.onnx",
                "/../Resources/models/en_US-lessac-low.onnx",
            };
            for (auto rel : candidates)
            {
                auto path = exe + rel;
                if (fileExists (path)) return path;
            }
            const char* compiled = DOOBLRTHROAT_PIPER_ONNX;
            if (compiled && compiled[0] && fileExists (compiled)) return compiled;
            return compiled && compiled[0] ? compiled : exe + "/../Resources/en_US-lessac-low.onnx";
        }

        int espeakCallback (short* wav, int n, espeak_EVENT*)
        {
            if (wav != nullptr && n > 0 && gEspeakSink != nullptr)
            {
                gEspeakSink->reserve (gEspeakSink->size() + size_t (n));
                for (int i = 0; i < n; ++i)
                    gEspeakSink->push_back (wav[i] / 32768.0f);
            }
            return 0;
        }

        bool initEspeak (std::string& err)
        {
            std::lock_guard<std::mutex> g (gInitMutex);
            if (gEspeakReady) return true;
            const auto parent = espeakDataParent();
            if (parent.empty())
            {
                err = "espeak-ng-data not found (set DOOBLRTHROAT_ESPEAK_DATA)";
                return false;
            }
            const auto sr = espeak_Initialize (AUDIO_OUTPUT_SYNCHRONOUS, 20, parent.c_str(), 0);
            if (sr < 8000)
            {
                err = "espeak_Initialize failed, data parent=" + parent;
                return false;
            }
            gEspeakRate = sr;
            espeak_SetSynthCallback (espeakCallback);
            espeak_SetParameter (espeakRANGE, 0, 0);
            espeak_SetParameter (espeakINTONATION, 0, 0);
            gEspeakReady = true;
            return true;
        }

        std::string xmlEscape (const std::string& s)
        {
            std::string o;
            o.reserve (s.size() + 8);
            for (char c : s)
            {
                if (c == '&') o += "&amp;";
                else if (c == '<') o += "&lt;";
                else if (c == '>') o += "&gt;";
                else o += c;
            }
            return o;
        }

        bool synthEspeak (const std::string& text, double midiHz, const char* voice,
                          std::vector<float>& out, int& nativeRate, std::string& err)
        {
            if (! initEspeak (err)) return false;
            if (espeak_SetVoiceByName (voice) != EE_OK)
            {
                // Fall back to default American English if a variant is missing.
                if (espeak_SetVoiceByName ("en-us") != EE_OK)
                {
                    err = std::string ("espeak_SetVoiceByName failed: ") + voice;
                    return false;
                }
            }
            espeak_SetParameter (espeakRANGE, 0, 0);
            espeak_SetParameter (espeakINTONATION, 0, 0);
            espeak_SetParameter (espeakPUNCTUATION, 0, 0);
            const int hz = int (std::round (std::clamp (midiHz, 50.0, 800.0)));
            std::ostringstream ssml;
            ssml << "<speak><prosody pitch='" << hz << "' range='0'>"
                 << xmlEscape (text) << "</prosody></speak>";
            const auto u = ssml.str();
            out.clear();
            gEspeakSink = &out;
            const auto ok = espeak_Synth (u.c_str(), u.size() + 1, 0, POS_CHARACTER, 0,
                                          espeakCHARS_UTF8 | espeakSSML, nullptr, nullptr);
            espeak_Synchronize();
            gEspeakSink = nullptr;
            nativeRate = gEspeakRate;
            if (ok != EE_OK)
            {
                err = "espeak_Synth failed";
                return false;
            }
            return ! out.empty();
        }

        // Band-limited (Hann-windowed sinc, 24 zero crossings) resampler for the
        // worker. Used to bring WORLD output at the voice's native rate up to the
        // host rate without the imaging a linear interpolator leaves above 8 kHz.
        std::vector<float> resampleSinc (const std::vector<float>& in, int inRate, int outRate)
        {
            if (in.empty() || inRate <= 0 || outRate <= 0) return {};
            if (inRate == outRate) return in;
            const auto outN = std::max<size_t> (1, size_t (std::llround (double (in.size()) * outRate / inRate)));
            std::vector<float> out (outN);
            const double step = double (inRate) / double (outRate);
            const double cutoff = 0.95 * std::min (1.0, double (outRate) / double (inRate)); // of input Nyquist
            constexpr int half = 24;
            const double pi = 3.14159265358979323846;
            const auto inN = long (in.size());
            for (size_t i = 0; i < outN; ++i)
            {
                const double x = double (i) * step;
                const long c0 = long (std::floor (x));
                double acc = 0.0, wsum = 0.0;
                for (long k = c0 - half + 1; k <= c0 + half; ++k)
                {
                    const double d = x - double (k);
                    const double ad = std::abs (d);
                    if (ad >= double (half)) continue;
                    const double arg = pi * cutoff * d;
                    const double sinc = (std::abs (arg) < 1.0e-9) ? 1.0 : std::sin (arg) / arg;
                    const double win = 0.5 + 0.5 * std::cos (pi * d / double (half));
                    const double w = cutoff * sinc * win;
                    wsum += w;
                    if (k >= 0 && k < inN)
                        acc += w * double (in[size_t (k)]);
                }
                out[i] = float (wsum > 1.0e-9 ? acc / wsum : 0.0);
            }
            return out;
        }

        constexpr double kWorldFloor = 40.0;

        bool worldAnalyze (const std::vector<float>& audio, int fs, WorldFeatures& feat, std::string& err)
        {
            if (audio.size() < size_t (std::max (1, fs / 20)))
            {
                err = "audio too short for WORLD";
                return false;
            }
            std::vector<double> wave (audio.size());
            for (size_t i = 0; i < audio.size(); ++i)
                wave[i] = audio[i];

            HarvestOption hopt;
            InitializeHarvestOption (&hopt);
            hopt.frame_period = 5.0;
            hopt.f0_floor = kWorldFloor;
            const int f0n = GetSamplesForHarvest (fs, int (wave.size()), hopt.frame_period);
            if (f0n <= 2)
            {
                err = "WORLD Harvest produced too few frames";
                return false;
            }
            std::vector<double> temporal (static_cast<size_t> (f0n));
            std::vector<double> f0c (static_cast<size_t> (f0n));
            std::vector<double> refined (static_cast<size_t> (f0n));
            Harvest (wave.data(), int (wave.size()), fs, &hopt, temporal.data(), f0c.data());
            StoneMask (wave.data(), int (wave.size()), fs, temporal.data(), f0c.data(), f0n, refined.data());

            CheapTrickOption copt;
            InitializeCheapTrickOption (fs, &copt);
            copt.f0_floor = kWorldFloor;
            copt.fft_size = GetFFTSizeForCheapTrick (fs, &copt);
            const int fft = copt.fft_size;
            const int bins = fft / 2 + 1;
            std::vector<double> spStore (size_t (f0n) * size_t (bins), 0.0);
            std::vector<double> apStore (size_t (f0n) * size_t (bins), 0.0);
            std::vector<double*> specPtr (static_cast<size_t> (f0n));
            std::vector<double*> apPtr (static_cast<size_t> (f0n));
            for (int i = 0; i < f0n; ++i)
            {
                specPtr[size_t (i)] = spStore.data() + i * bins;
                apPtr[size_t (i)] = apStore.data() + i * bins;
            }
            CheapTrick (wave.data(), int (wave.size()), fs, temporal.data(), refined.data(), f0n, &copt, specPtr.data());
            D4COption dopt;
            InitializeD4COption (&dopt);
            // Default threshold is 0.85. A slightly lower value keeps low-F0
            // frames voiced instead of collapsing to noisy aperiodicity.
            dopt.threshold = 0.80;
            D4C (wave.data(), int (wave.size()), fs, temporal.data(), refined.data(), f0n, fft, &dopt, apPtr.data());

            feat.fs = fs;
            feat.f0n = f0n;
            feat.fft = fft;
            feat.bins = bins;
            feat.framePeriod = hopt.frame_period;
            feat.temporal = std::move (temporal);
            feat.f0 = std::move (refined);
            feat.sp = std::move (spStore);
            feat.ap = std::move (apStore);
            return feat.valid();
        }

        void shiftSpectrum (std::vector<double>& sp, int f0n, int bins, double factor)
        {
            if (f0n <= 0 || bins <= 0 || ! std::isfinite (factor) || std::abs (factor - 1.0) < 1.0e-4)
                return;
            factor = std::clamp (factor, 0.70, 1.30);
            std::vector<double> tmp (static_cast<size_t> (bins));
            for (int i = 0; i < f0n; ++i)
            {
                double* row = sp.data() + i * bins;
                for (int k = 0; k < bins; ++k)
                {
                    const auto src = double (k) / factor;
                    if (src >= double (bins - 1))
                    {
                        tmp[size_t (k)] = row[bins - 1];
                        continue;
                    }
                    const auto j = int (src);
                    const auto t = src - double (j);
                    auto a = std::max (row[j], 1.0e-16);
                    auto b = std::max (row[std::min (j + 1, bins - 1)], 1.0e-16);
                    tmp[size_t (k)] = std::exp ((1.0 - t) * std::log (a) + t * std::log (b));
                }
                std::copy (tmp.begin(), tmp.end(), row);
            }
        }

        double formantFactor (double hz, double formant, int voice)
        {
            const auto safeHz = std::max (40.0, hz);
            const auto t = std::clamp ((std::log2 (safeHz) - std::log2 (65.40639132514966))
                                           / (std::log2 (261.6255653005986) - std::log2 (65.40639132514966)),
                                       0.0, 1.0);
            double range = 0.84 + 0.16 * t; // C2 ~0.84, C4 and above 1.0
            if (voice == 1) range = 0.5 * range + 0.5; // Bright: half the auto drop
            if (voice == 2) range *= 0.90;             // Deep: extra lowering
            const auto user = 0.75 + 0.50 * std::clamp (formant, 0.0, 1.0);
            return std::clamp (range * user, 0.70, 1.30);
        }

        // residual: how much of the voice's own contour survives (0 = flat note).
        // maxCents caps the deviation so a sung note stays in tune.
        void applyTargetF0 (std::vector<double>& f0, double target, double residual, double maxCents)
        {
            std::vector<double> voiced;
            voiced.reserve (f0.size());
            for (auto v : f0)
                if (v > 40.0) voiced.push_back (v);
            double median = target;
            if (! voiced.empty())
            {
                std::nth_element (voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end());
                median = voiced[voiced.size() / 2];
            }
            target = std::clamp (target, 50.0, 800.0);
            for (auto& v : f0)
            {
                if (v <= 40.0) continue;
                if (residual > 0.0 && median > 40.0)
                {
                    auto cents = 1200.0 * std::log2 (1.0 + residual * (v / median - 1.0));
                    if (! std::isfinite (cents)) cents = 0.0;
                    cents = std::clamp (cents, -maxCents, maxCents);
                    v = target * std::pow (2.0, cents / 1200.0);
                }
                else
                    v = target;
            }
        }

        // Loop on the vowel nucleus: centre on the loudest voiced frame, keep the
        // window inside the voiced span and make its length a whole number of
        // periods at the note F0 so the crossfade joins in phase.
        void findLoop (const std::vector<float>& audio, const WorldFeatures& feat, double hz, int fs,
                       int& loopStart, int& loopEnd, bool lastVowel = false)
        {
            loopStart = 0;
            loopEnd = 0;
            if (! feat.valid() || audio.size() < 64 || ! (hz > 20.0) || fs <= 0) return;
            const int n = int (audio.size());
            int first = -1, last = -1;
            if (lastVowel)
            {
                // Speak: the last voiced run that is long enough to be a vowel.
                int runStart = -1;
                for (int i = 0; i <= feat.f0n; ++i)
                {
                    const bool v = i < feat.f0n && feat.f0[size_t (i)] > 40.0;
                    if (v && runStart < 0) runStart = i;
                    if (! v && runStart >= 0)
                    {
                        if (i - 1 - runStart >= 12) { first = runStart; last = i - 1; }
                        runStart = -1;
                    }
                }
                // A fully voiced line ("I love you") is one long run; keep only its
                // last ~250 ms so the loop lands on the final vowel, not an early one.
                if (first >= 0 && feat.framePeriod > 0.0)
                    first = std::max (first, last - int (250.0 / feat.framePeriod));
            }
            else
            {
                for (int i = 0; i < feat.f0n; ++i)
                    if (feat.f0[size_t (i)] > 40.0)
                    {
                        if (first < 0) first = i;
                        last = i;
                    }
            }
            if (first < 0 || last - first < 8) return;
            auto toSamp = [&] (int i) {
                i = std::clamp (i, 0, feat.f0n - 1);
                return std::clamp (int (std::llround (feat.temporal[size_t (i)] * fs)), 0, n - 1);
            };
            const int vs = toSamp (first + 1), ve = toSamp (last - 1);
            if (ve - vs < fs / 20) return;
            // Loop length: whole pitch periods, about 60 ms (at least two periods).
            // 0.4.0 used ~120 ms, which in a short or gliding vowel spans an
            // audible level / formant swing that repeats at ~8 Hz ("na-na").
            const double period = double (fs) / hz;
            const int periods = std::max (2, int (std::floor (0.06 * fs / period)));
            int len = int (std::llround (periods * period));
            const int maxLen = (ve - vs) * 3 / 4;
            while (len > maxLen && len > int (2 * period) + 1)
                len = int (std::llround ((std::floor (len / period) - 1.0) * period));
            // Place it where the voiced span is loud AND steady: score each
            // candidate by its energy times the quietest / loudest energy of its
            // sub-blocks (one pitch period or 5 ms, whichever is longer).
            const int sub = std::max ({ 16, fs / 200, int (period) });
            double best = -1.0;
            int bestCentre = (vs + ve) / 2;
            for (int c = vs + len / 2; c + len / 2 < ve; c += std::max (8, sub / 2))
            {
                double total = 0.0, lo = 1.0e30, hi = 0.0;
                for (int k0 = c - len / 2; k0 + sub <= c + len / 2; k0 += sub)
                {
                    double e = 0.0;
                    for (int k = k0; k < k0 + sub; ++k)
                        e += double (audio[size_t (k)]) * audio[size_t (k)];
                    total += e;
                    lo = std::min (lo, e);
                    hi = std::max (hi, e);
                }
                const double steady = hi > 0.0 ? lo / hi : 0.0;
                const double score = total * steady;
                if (score > best) { best = score; bestCentre = c; }
            }
            if (len < 64) return;
            int s = std::clamp (bestCentre - len / 2, vs, std::max (vs, ve - len));
            // Trim the length to the clip's real period: Sing keeps a little of
            // the voice's own intonation, so `period` from the MIDI note can be off
            // by a few samples per cycle, and the 10 ms seam crossfade would then
            // blend two glottal pulses out of phase (a soft flam every loop).
            // Pick the length within +/-4% whose seam region (the 10 ms before the
            // loop end vs the 10 ms before the loop start) correlates best.
            {
                const int w = std::max (16, fs / 100);
                if (s >= w)
                {
                    const int lo = std::max (int (2 * period * 0.9), int (len * 0.96));
                    const int hi = std::min (n - 1 - s, int (len * 1.04) + 1);
                    double bestR = -2.0;
                    int bestLen = len;
                    for (int L = lo; L <= hi; ++L)
                    {
                        double xy = 0.0, xx = 0.0, yy = 0.0;
                        for (int k = 0; k < w; ++k)
                        {
                            const double a = audio[size_t (s - w + k)], b = audio[size_t (s + L - w + k)];
                            xy += a * b; xx += a * a; yy += b * b;
                        }
                        const double r = (xx > 0.0 && yy > 0.0) ? xy / std::sqrt (xx * yy) : -1.0;
                        if (r > bestR) { bestR = r; bestLen = L; }
                    }
                    len = bestLen;
                }
            }
            loopStart = s;
            loopEnd = std::min (n - 1, s + len);
            if (loopEnd - loopStart < 64) { loopStart = loopEnd = 0; }
        }

        bool worldSynthesize (const WorldFeatures& feat, double hz, double formant, int voice, bool sung,
                              std::vector<float>& audio, std::string& err)
        {
            if (! feat.valid())
            {
                err = "no WORLD features";
                return false;
            }
            auto f0 = feat.f0;
            auto sp = feat.sp;
            auto ap = feat.ap;
            // Speak keeps 15% of Piper's intonation; Sing holds the note (light life, within +/-15 cents).
            if (sung) applyTargetF0 (f0, hz, 0.04, 15.0);
            else      applyTargetF0 (f0, hz, 0.15, 300.0);
            shiftSpectrum (sp, feat.f0n, feat.bins, formantFactor (hz, formant, voice));

            const int yLen = int ((feat.f0n - 1) * feat.framePeriod / 1000.0 * feat.fs) + 1;
            if (yLen <= 1)
            {
                err = "WORLD synthesis length invalid";
                return false;
            }
            std::vector<double> y (size_t (yLen), 0.0);
            std::vector<double*> specPtr (static_cast<size_t> (feat.f0n));
            std::vector<double*> apPtr (static_cast<size_t> (feat.f0n));
            for (int i = 0; i < feat.f0n; ++i)
            {
                specPtr[size_t (i)] = sp.data() + i * feat.bins;
                apPtr[size_t (i)] = ap.data() + i * feat.bins;
            }
            Synthesis (f0.data(), feat.f0n, specPtr.data(), apPtr.data(),
                       feat.fft, feat.framePeriod, feat.fs, yLen, y.data());
            audio.resize (size_t (yLen));
            double peak = 1.0e-6;
            for (double v : y) peak = std::max (peak, std::abs (v));
            const auto scale = (peak > 0.95) ? (0.90 / peak) : 1.0;
            for (int i = 0; i < yLen; ++i)
                audio[size_t (i)] = float (y[size_t (i)] * scale);
            return true;
        }

        int midiOfHz (double hz)
        {
            if (! (hz > 1.0)) return 60;
            return int (std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
        }

        std::vector<char32_t> utf32 (const std::string& s)
        {
            std::vector<char32_t> o;
            o.reserve (s.size());
            for (size_t i = 0; i < s.size();)
            {
                unsigned char c = (unsigned char) s[i];
                char32_t cp = 0;
                size_t n = 1;
                if (c < 0x80) { cp = c; n = 1; }
                else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); n = 2; }
                else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); n = 3; }
                else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); n = 4; }
                else { ++i; continue; }
                o.push_back (cp);
                i += n;
            }
            return o;
        }

        bool loadPiperConfig (const std::string& jsonPath, std::string& err)
        {
            if (gPiperConfigLoaded) return true;
            std::ifstream in (jsonPath);
            if (! in)
            {
                err = "cannot open piper json: " + jsonPath;
                return false;
            }
            std::string all ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            auto findNum = [&] (const char* key, float fallback) {
                auto k = std::string ("\"") + key + "\"";
                auto p = all.find (k);
                if (p == std::string::npos) return fallback;
                p = all.find (':', p);
                if (p == std::string::npos) return fallback;
                return float (std::atof (all.c_str() + p + 1));
            };
            gNoiseScale = findNum ("noise_scale", 0.667f);
            gLengthScale = findNum ("length_scale", 1.0f);
            gNoiseW = findNum ("noise_w", 0.8f);
            gPiperRate = int (findNum ("sample_rate", 16000.0f));

            // phoneme_id_map entries look like "ə": [59]
            gPhonemeIds.clear();
            const auto key = std::string ("\"phoneme_id_map\"");
            auto p = all.find (key);
            if (p == std::string::npos)
            {
                err = "phoneme_id_map missing in " + jsonPath;
                return false;
            }
            p = all.find ('{', p);
            if (p == std::string::npos) { err = "bad phoneme_id_map"; return false; }
            auto q = all.find ('}', p);
            if (q == std::string::npos) { err = "unterminated phoneme_id_map"; return false; }
            const auto body = all.substr (p + 1, q - p - 1);
            size_t i = 0;
            while (i < body.size())
            {
                auto a = body.find ('"', i);
                if (a == std::string::npos) break;
                // handle escaped quotes in the key
                size_t b = a + 1;
                std::string keyStr;
                while (b < body.size())
                {
                    if (body[b] == '\\' && b + 1 < body.size())
                    {
                        keyStr.push_back (body[b + 1]);
                        b += 2;
                        continue;
                    }
                    if (body[b] == '"') break;
                    keyStr.push_back (body[b]);
                    ++b;
                }
                auto br = body.find ('[', b);
                if (br == std::string::npos) break;
                const auto id = int64_t (std::atoll (body.c_str() + br + 1));
                auto cps = utf32 (keyStr);
                if (! cps.empty())
                    gPhonemeIds[cps[0]] = id;
                i = body.find (']', br);
                if (i == std::string::npos) break;
                ++i;
            }
            if (gPhonemeIds.size() < 20)
            {
                err = "failed to parse phoneme_id_map (" + std::to_string (gPhonemeIds.size()) + " entries)";
                return false;
            }
            gPiperConfigLoaded = true;
            return true;
        }

        bool initPiper (std::string& err)
        {
            std::lock_guard<std::mutex> g (gInitMutex);
            if (gPiperSession) return true;
            const auto onnx = piperOnnxPath();
            const auto json = onnx + ".json";
            if (! loadPiperConfig (json, err)) return false;
            if (! gOrtEnv)
                gOrtEnv = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "dooblr-piper");
            gPiperOpts = std::make_unique<Ort::SessionOptions>();
            gPiperOpts->SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
            gPiperOpts->SetIntraOpNumThreads (1);
            gPiperOpts->SetInterOpNumThreads (1);
            try
            {
                gPiperSession = std::make_unique<Ort::Session> (*gOrtEnv, onnx.c_str(), *gPiperOpts);
            }
            catch (const std::exception& e)
            {
                err = std::string ("ORT session: ") + e.what() + " model=" + onnx;
                return false;
            }
            return true;
        }

        bool phonemizeIpa (const std::string& text, std::vector<int64_t>& ids, std::string& err)
        {
            if (! initEspeak (err)) return false;
            if (espeak_SetVoiceByName (gPiperVoice.c_str()) != EE_OK)
                espeak_SetVoiceByName ("en-us");
            ids.clear();
            // BOS, pad, (ph, pad)*, EOS
            auto push = [&] (int64_t id) { ids.push_back (id); };
            push (1); // ^
            push (0); // _
            const void* ptr = text.c_str();
            while (ptr != nullptr && *static_cast<const char*> (ptr) != 0)
            {
                const char* ipa = espeak_TextToPhonemes (&ptr, espeakCHARS_UTF8, espeakPHONEMES_IPA);
                if (ipa == nullptr) break;
                for (auto cp : utf32 (ipa))
                {
                    if (cp == 0 || cp == char32_t ('\n')) continue;
                    auto it = gPhonemeIds.find (cp);
                    if (it == gPhonemeIds.end())
                        continue;
                    push (it->second);
                    push (0);
                }
            }
            push (2); // $
            return ids.size() > 3;
        }

        bool synthPiper (const std::string& text, std::vector<float>& out, int& nativeRate,
                         double& inferSeconds, std::string& err, double lengthScale = 1.0)
        {
            if (! initPiper (err)) return false;
            std::vector<int64_t> ids;
            if (! phonemizeIpa (text, ids, err))
            {
                if (err.empty()) err = "piper phonemize produced no ids";
                return false;
            }
            const auto length = gLengthScale * float (lengthScale > 1.0e-3 ? lengthScale : 1.0);
            std::vector<int64_t> lengths { int64_t (ids.size()) };
            std::vector<float> scales { gNoiseScale, length, gNoiseW };
            std::vector<int64_t> idsShape { 1, int64_t (ids.size()) };
            std::vector<int64_t> lenShape { 1 };
            std::vector<int64_t> scShape { 3 };
            auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            std::vector<Ort::Value> inputs;
            inputs.push_back (Ort::Value::CreateTensor<int64_t> (mem, ids.data(), ids.size(), idsShape.data(), 2));
            inputs.push_back (Ort::Value::CreateTensor<int64_t> (mem, lengths.data(), 1, lenShape.data(), 1));
            inputs.push_back (Ort::Value::CreateTensor<float> (mem, scales.data(), 3, scShape.data(), 1));
            const char* inNames[] = { "input", "input_lengths", "scales" };
            const char* outNames[] = { "output" };
            const auto t0 = std::chrono::steady_clock::now();
            auto outputs = gPiperSession->Run (Ort::RunOptions { nullptr }, inNames, inputs.data(), 3,
                                               outNames, 1);
            inferSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            if (outputs.empty() || ! outputs.front().IsTensor())
            {
                err = "piper ONNX produced no audio tensor";
                return false;
            }
            const auto info = outputs.front().GetTensorTypeAndShapeInfo();
            const auto shape = info.GetShape();
            int64_t n = 1;
            for (auto d : shape) n *= std::max<int64_t> (1, d);
            const float* data = outputs.front().GetTensorData<float>();
            float peak = 0.01f;
            for (int64_t i = 0; i < n; ++i)
                peak = std::max (peak, std::abs (data[i]));
            const auto scale = 0.90f / peak;
            out.resize (size_t (n));
            for (int64_t i = 0; i < n; ++i)
                out[size_t (i)] = data[i] * scale;
            nativeRate = gPiperRate;
            return ! out.empty();
        }

        void fadeEdges (std::vector<float>& audio, int rate)
        {
            const int fade = std::max (1, int (0.005 * rate));
            for (int i = 0; i < fade && i < int (audio.size()); ++i)
                audio[size_t (i)] *= float (i) / float (fade);
            for (int i = 0; i < fade && i < int (audio.size()); ++i)
                audio[audio.size() - 1 - size_t (i)] *= float (i) / float (fade);
        }

        bool renderClip (ProvenKind kind, const std::string& text, double midiHz, double lengthScale,
                         int outRate, ProvenClip& clip, ProvenStats& stats, std::string& err)
        {
            (void) midiHz;
            std::lock_guard<std::mutex> synth (gSynthMutex);
            std::vector<float> native;
            int nativeRate = 22050;
            double infer = 0;
            bool ok = false;
            if (kind == ProvenKind::Espeak || kind == ProvenKind::Mbrola)
            {
                auto voice = envOr ("DOOBLRTHROAT_ESPEAK_VOICE",
                                    kind == ProvenKind::Mbrola ? "mb-us1" : "en-us");
                ok = synthEspeak (text, midiHz, voice.c_str(), native, nativeRate, err);
            }
            else if (kind == ProvenKind::Piper)
            {
                ok = synthPiper (text, native, nativeRate, infer, err, lengthScale);
            }
            if (! ok || native.empty())
                return false;
            // WORLD runs at the voice's native rate (16-22 kHz): ~3x cheaper per note
            // than at 48 kHz, and the voice has no content above its Nyquist anyway.
            fadeEdges (native, nativeRate);
            if (! worldAnalyze (native, nativeRate, clip.world, err))
                return false;
            clip.samples.clear();
            clip.loopStart = 0;
            clip.loopEnd = 0;
            clip.rate = outRate;
            clip.refHz = 0.0;
            stats.inferSeconds += infer;
            stats.nativeRate = nativeRate;
            stats.outputRate = outRate;
            return true;
        }

        void shutdownGlobals()
        {
            std::lock_guard<std::mutex> synth (gSynthMutex);
            std::lock_guard<std::mutex> g (gInitMutex);
            gPiperSession.reset();
            gPiperOpts.reset();
            gOrtEnv.reset();
            if (gEspeakReady)
            {
                espeak_SetSynthCallback (nullptr);
                espeak_Terminate();
                gEspeakReady = false;
            }
            gPiperConfigLoaded = false;
            gPhonemeIds.clear();
        }
    }

    void shutdownProvenGlobals()
    {
        shutdownGlobals();
    }

    ProvenEngine::ProvenEngine()
    {
        std::lock_guard<std::mutex> g (gInitMutex);
        ++gEngineCount;
    }

    ProvenEngine::~ProvenEngine()
    {
        shutdown();
    }

    void ProvenEngine::shutdown()
    {
        if (shutDown_)
            return;
        shutDown_ = true;
        stop_.store (true, std::memory_order_release);
        cv_.notify_all();
        readyCv_.notify_all();
        if (worker_.joinable())
            worker_.join();
        started_ = false;
        pending_ = false;
        working_ = false;
        engaged_.store (false, std::memory_order_release);
        liveCache_.store (nullptr, std::memory_order_release);
        livePlay_.store (nullptr, std::memory_order_release);
        while (inRender_.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ownedCache_.reset();
        ownedPlay_.reset();
        bool last = false;
        {
            std::lock_guard<std::mutex> g (gInitMutex);
            if (gEngineCount > 0)
                --gEngineCount;
            last = (gEngineCount == 0);
        }
        if (last)
            shutdownProvenGlobals();
    }

    void ProvenEngine::clear() noexcept
    {
        engaged_.store (false, std::memory_order_release);
        playPos_.store (0.0, std::memory_order_release);
        sustaining_.store (false, std::memory_order_release);
    }

    void ProvenEngine::requestPrepare (ProvenKind kind, const std::string& phrase,
                                       const std::vector<std::string>& syllables,
                                       double lengthScale, bool wait)
    {
        if (kind == ProvenKind::None || phrase.empty())
        {
            engaged_.store (false, std::memory_order_release);
            liveCache_.store (nullptr, std::memory_order_release);
            livePlay_.store (nullptr, std::memory_order_release);
            return;
        }
        {
            std::lock_guard<std::mutex> g (mutex_);
            pendingKind_ = kind;
            pendingPhrase_ = phrase;
            pendingSyllables_ = syllables;
            pendingLength_ = lengthScale;
            pending_ = true;
            preparing_.store (true, std::memory_order_release);
            ++generation_;
            error_.clear();
            if (! started_ && ! stop_.load (std::memory_order_acquire))
            {
                worker_ = std::thread ([this] { workerLoop(); });
                started_ = true;
            }
        }
        cv_.notify_one();
        if (wait)
            waitReady();
    }

    bool ProvenEngine::waitReady()
    {
        std::unique_lock<std::mutex> lk (mutex_);
        const auto gen = generation_;
        readyCv_.wait_for (lk, std::chrono::seconds (30), [&] {
            return stop_.load (std::memory_order_acquire)
                || (doneGeneration_ >= gen && ! pending_ && ! working_);
        });
        return liveCache_.load (std::memory_order_acquire) != nullptr;
    }

    void ProvenEngine::workerLoop()
    {
        for (;;)
        {
            ProvenKind kind = ProvenKind::None;
            std::string phrase;
            std::vector<std::string> syllables;
            double length = 1.0;
            unsigned gen = 0;
            bool doCache = false;
            unsigned noteGen = 0;
            int mode = 0, syl = 0, voice = 0;
            double hz = 261.626, formant = 0.5;
            const ProvenCache* cacheCopy = nullptr;
            {
                std::unique_lock<std::mutex> lk (mutex_);
                cv_.wait_for (lk, std::chrono::milliseconds (5), [&] {
                    return stop_.load (std::memory_order_acquire)
                        || pending_
                        || (noteGen_.load (std::memory_order_acquire)
                            != noteDone_.load (std::memory_order_acquire));
                });
                if (stop_.load (std::memory_order_acquire))
                    return;
                if (pending_)
                {
                    kind = pendingKind_;
                    phrase = pendingPhrase_;
                    syllables = pendingSyllables_;
                    length = pendingLength_;
                    gen = generation_;
                    pending_ = false;
                    working_ = true;
                    doCache = true;
                }
                else if (noteGen_.load (std::memory_order_acquire)
                         != noteDone_.load (std::memory_order_acquire)
                         && ownedCache_ != nullptr)
                {
                    noteGen = noteGen_.load (std::memory_order_acquire);
                    mode = playMode_.load (std::memory_order_acquire);
                    syl = playSyl_.load (std::memory_order_acquire);
                    hz = playHz_.load (std::memory_order_acquire);
                    formant = formant_.load (std::memory_order_acquire);
                    voice = voice_.load (std::memory_order_acquire);
                    cacheCopy = ownedCache_.get();
                    // A note armed just before a phrase edit asked for the old cache;
                    // it gets the new one instead of waiting for a clip that never comes.
                    auto want = reqCacheGen_.load (std::memory_order_acquire);
                    if (cacheCopy != nullptr && want != cacheCopy->gen)
                        reqCacheGen_.compare_exchange_strong (want, cacheCopy->gen, std::memory_order_acq_rel);
                }
                else
                    continue;
            }

            if (doCache)
            {
                const auto outRate = outputRate_.load (std::memory_order_acquire);
                auto cache = buildCache (kind, phrase, syllables, length, outRate);
                if (cache != nullptr)
                    cache->gen = gen;
                {
                    std::lock_guard<std::mutex> g (mutex_);
                    working_ = false;
                    doneGeneration_ = gen;
                    if (cache != nullptr)
                        stats_ = cache->stats;
                    else if (error_.empty())
                        error_ = "proven render failed";
                }
                if (cache != nullptr)
                    publishCache (std::move (cache));
                {
                    std::lock_guard<std::mutex> g (mutex_);
                    if (! pending_) preparing_.store (false, std::memory_order_release);
                }
                readyCv_.notify_all();
                continue;
            }

            if (cacheCopy != nullptr)
            {
                auto play = synthesizeNote (*cacheCopy, mode, syl, hz, formant, voice);
                if (play != nullptr)
                    publishPlay (std::move (play));
                noteDone_.store (noteGen, std::memory_order_release);
                readyCv_.notify_all();
            }
        }
    }

    void ProvenEngine::publishCache (std::unique_ptr<ProvenCache> next)
    {
        auto* raw = next.get();
        std::unique_ptr<ProvenCache> oldCache;
        {
            // The sounding play clip is independent of the cache, so a phrase
            // edit lets it finish instead of cutting it (that cut was a click).
            std::lock_guard<std::mutex> g (mutex_);
            oldCache = std::move (ownedCache_);
            ownedCache_ = std::move (next);
            liveCache_.store (raw, std::memory_order_seq_cst);
            liveCacheGen_.store (raw != nullptr ? raw->gen : 0u, std::memory_order_seq_cst);
            readyFlag_.store (raw != nullptr, std::memory_order_release);
        }
        while (inRender_.load (std::memory_order_seq_cst) != 0)
            std::this_thread::yield();
        oldCache.reset();
    }

    void ProvenEngine::publishPlay (std::unique_ptr<ProvenPlay> next)
    {
        auto* raw = next.get();
        std::unique_ptr<ProvenPlay> old;
        {
            std::lock_guard<std::mutex> g (mutex_);
            old = std::move (ownedPlay_);
            ownedPlay_ = std::move (next);
            livePlay_.store (raw, std::memory_order_seq_cst);
        }
        while (inRender_.load (std::memory_order_seq_cst) != 0)
            std::this_thread::yield();
        old.reset();
    }

    std::unique_ptr<ProvenCache> ProvenEngine::buildCache (ProvenKind kind, const std::string& phrase,
                                                          const std::vector<std::string>& syllables,
                                                          double lengthScale, double outRate)
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto cache = std::make_unique<ProvenCache>();
        cache->kind = kind;
        cache->text = phrase;
        cache->refHz = 261.626;
        cache->rate = int (std::round (outRate > 1000.0 ? outRate : 48000.0));
        cache->lengthScale = lengthScale;
        std::string err;
        if (! renderClip (kind, phrase, cache->refHz, lengthScale, cache->rate, cache->speak, cache->stats, err))
        {
            {
                std::lock_guard<std::mutex> g (mutex_);
                error_ = err.empty() ? "speak render failed" : err;
            }
            std::cerr << "ProvenVoice error: " << err << "\n";
            return nullptr;
        }
        cache->syllables.reserve (syllables.size());
        for (const auto& syl : syllables)
        {
            ProvenClip clip;
            std::string sylErr;
            const auto text = syl.empty() ? phrase : syl;
            if (renderClip (kind, text, cache->refHz, lengthScale, cache->rate, clip, cache->stats, sylErr))
                cache->syllables.push_back (std::move (clip));
            else
                cache->syllables.push_back (ProvenClip {});
        }
        cache->stats.synthSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        cache->stats.audioSeconds = 0.0;
        return cache;
    }

    std::unique_ptr<ProvenPlay> ProvenEngine::synthesizeNote (const ProvenCache& cache, int mode, int syl,
                                                            double hz, double formant, int voice) const
    {
        const ProvenClip* clip = nullptr;
        if (mode == 1 && syl >= 0 && syl < int (cache.syllables.size())
            && cache.syllables[size_t (syl)].world.valid())
            clip = &cache.syllables[size_t (syl)];
        else
            clip = &cache.speak;
        if (clip == nullptr || ! clip->world.valid())
            return nullptr;
        auto play = std::make_unique<ProvenPlay>();
        std::string err;
        std::vector<float> native;
        if (! worldSynthesize (clip->world, hz, formant, voice, mode == 1, native, err))
        {
            std::cerr << "ProvenVoice synth error: " << err << "\n";
            return nullptr;
        }
        const int outRate = (cache.rate > 1000) ? cache.rate : 48000;
        play->samples = (clip->world.fs == outRate) ? std::move (native)
                                                    : resampleSinc (native, clip->world.fs, outRate);
        fadeEdges (play->samples, outRate);
        findLoop (play->samples, clip->world, hz, outRate, play->loopStart, play->loopEnd, mode == 0);
        play->rate = outRate;
        play->hz = hz;
        play->midi = midiOfHz (hz);
        play->mode = mode;
        play->syl = syl;
        play->cacheGen = cache.gen;
        return play;
    }

    bool ProvenEngine::playMatchesRequest (const ProvenPlay* play) const noexcept
    {
        if (play == nullptr || play->samples.empty())
            return false;
        const int mode = playMode_.load (std::memory_order_acquire);
        const int syl = playSyl_.load (std::memory_order_acquire);
        const auto hz = playHz_.load (std::memory_order_acquire);
        if (play->mode != mode)
            return false;
        if (mode == 1 && play->syl != syl)
            return false;
        if (play->cacheGen != reqCacheGen_.load (std::memory_order_acquire))
            return false;
        return midiOfHz (play->hz) == midiOfHz (hz);
    }

    void ProvenEngine::armPlayback (int mode, int syl, double midiHz) noexcept
    {
        if (! std::isfinite (midiHz) || midiHz < 1.0)
            midiHz = 261.626;
        // Guard the play pointer the same way render() does: the worker frees a
        // replaced clip only after inRender_ drops back to zero.
        inRender_.store (1, std::memory_order_seq_cst);
        const auto* current = livePlay_.load (std::memory_order_seq_cst);
        // Declick: whatever is sounding now fades out over ~5 ms instead of
        // jumping to the start of the next clip (or to silence while it renders).
        if (engaged_.load (std::memory_order_acquire) && playMatchesRequest (current))
            captureTail (current);
        playMode_.store (mode, std::memory_order_release);
        playSyl_.store (syl, std::memory_order_release);
        playPos_.store (0.0, std::memory_order_release);
        playHz_.store (midiHz, std::memory_order_release);
        playRatio_.store (1.0, std::memory_order_release);
        reqCacheGen_.store (liveCacheGen_.load (std::memory_order_acquire), std::memory_order_release);
        inXfade_ = false;
        // Stay engaged while the first cache is still building, so the Tract
        // voice never sounds in its place (it used to leak on the first notes).
        engaged_.store (liveCache_.load (std::memory_order_acquire) != nullptr
                            || preparing_.load (std::memory_order_acquire),
                        std::memory_order_release);
        if (playMatchesRequest (current))
        {
            const auto ratio = (current->hz > 1.0) ? (midiHz / current->hz) : 1.0;
            playRatio_.store (std::clamp (std::isfinite (ratio) ? ratio : 1.0, 0.5, 2.0),
                              std::memory_order_release);
            noteDone_.store (noteGen_.load (std::memory_order_acquire), std::memory_order_release);
        }
        else
        {
            noteGen_.fetch_add (1, std::memory_order_acq_rel);
        }
        inRender_.store (0, std::memory_order_seq_cst);
        // Real time: the worker polls every 5 ms, so the audio thread never
        // touches the condition variable. Offline: wake it and wait.
        if (offline_.load (std::memory_order_acquire))
            waitNoteReady();
    }

    void ProvenEngine::captureTail (const ProvenPlay* play) noexcept
    {
        // Audio thread, inside the inRender_ guard. Continue the current clip
        // from the play head for a few ms with a linear fade-out; any part of an
        // older tail still sounding is folded in so back-to-back notes stay smooth.
        float prev[kTailMax] {};
        int prevLen = 0;
        for (int k = tailPos_; k < tailLen_; ++k)
            prev[prevLen++] = tail_[k];
        tailLen_ = 0;
        tailPos_ = 0;
        const int len = std::clamp ((play != nullptr ? play->rate : 48000) / 200, 16, kTailMax);
        if (play != nullptr && ! play->samples.empty())
        {
            const auto& s = play->samples;
            const auto sz = int (s.size());
            auto pos = playPos_.load (std::memory_order_relaxed);
            auto inc = playRatio_.load (std::memory_order_relaxed);
            if (! std::isfinite (inc) || inc <= 0.0) inc = 1.0;
            const auto loopStart = play->loopStart, loopEnd = play->loopEnd;
            const bool looping = (lastLooping_ || inXfade_) && loopEnd > loopStart + 32;
            const int xfade = looping ? std::max (0, std::min ({ play->rate / 100, (loopEnd - loopStart) / 3, loopStart })) : 0;
            auto at = [&] (double p) -> float
            {
                if (p < 0.0 || p >= double (sz)) return 0.0f;
                if (p >= double (sz - 1)) return s[size_t (sz - 1)];
                const auto j = int (p);
                const auto t = float (p - double (j));
                return s[size_t (j)] + (s[size_t (j + 1)] - s[size_t (j)]) * t;
            };
            for (int k = 0; k < len; ++k)
            {
                if (looping && pos >= double (loopEnd))
                    pos -= double (loopEnd - loopStart);
                float y;
                if (looping && xfade > 0 && pos >= double (loopEnd - xfade) && pos < double (loopEnd))
                {
                    const auto t = float ((pos - double (loopEnd - xfade)) / double (xfade));
                    y = (1.0f - t) * at (pos) + t * at (pos - double (loopEnd - loopStart));
                }
                else
                    y = at (pos);
                const auto fade = 1.0f - float (k + 1) / float (len);
                tail_[k] = y * lastGate_ * fade;
                pos += inc;
            }
            tailLen_ = len;
        }
        for (int k = 0; k < prevLen; ++k)
        {
            if (k >= tailLen_) { tail_[k] = 0.0f; tailLen_ = k + 1; }
            tail_[k] += prev[k];
        }
    }

    void ProvenEngine::mixTail (float* out, size_t n) noexcept
    {
        for (size_t i = 0; i < n && tailPos_ < tailLen_; ++i)
            out[i] += tail_[tailPos_++];
    }

    void ProvenEngine::startSpeakPlayback (double midiHz) noexcept
    {
        armPlayback (0, 0, midiHz);
    }

    void ProvenEngine::startSyllablePlayback (int index, double midiHz) noexcept
    {
        armPlayback (1, index, midiHz);
    }

    void ProvenEngine::stopPlayback() noexcept
    {
        inRender_.store (1, std::memory_order_seq_cst);
        const auto* current = livePlay_.load (std::memory_order_seq_cst);
        if (engaged_.load (std::memory_order_acquire) && playMatchesRequest (current))
            captureTail (current);
        inRender_.store (0, std::memory_order_seq_cst);
        playPos_.store (0.0, std::memory_order_release);
        engaged_.store (false, std::memory_order_release);
        sustaining_.store (false, std::memory_order_release);
    }

    bool ProvenEngine::waitNoteReady()
    {
        cv_.notify_all();
        std::unique_lock<std::mutex> lk (mutex_);
        readyCv_.wait_for (lk, std::chrono::seconds (15), [&] {
            return stop_.load (std::memory_order_acquire)
                || playMatchesRequest (livePlay_.load (std::memory_order_acquire));
        });
        return playMatchesRequest (livePlay_.load (std::memory_order_acquire));
    }

    bool ProvenEngine::finished() const noexcept
    {
        if (! engaged_.load (std::memory_order_acquire))
            return true;
        if (noteGen_.load (std::memory_order_acquire) != noteDone_.load (std::memory_order_acquire)
            && ! playMatchesRequest (livePlay_.load (std::memory_order_acquire)))
            return false;
        const auto* play = livePlay_.load (std::memory_order_acquire);
        if (play == nullptr || play->samples.empty())
            return true;
        const bool canLoop = vowelLoop_.load (std::memory_order_acquire)
                          && sustaining_.load (std::memory_order_acquire)
                          && play->loopEnd > play->loopStart + 32;
        if (canLoop)
            return false;
        return playPos_.load (std::memory_order_acquire) >= double (play->samples.size());
    }

    bool ProvenEngine::render (float* out, size_t n, double gate) noexcept
    {
        if (out == nullptr || n == 0) return false;
        inRender_.store (1, std::memory_order_seq_cst);
        const auto* play = livePlay_.load (std::memory_order_seq_cst);
        // The engine envelope arrives once per 16-sample step; ramp it so the
        // attack / release is smooth instead of a staircase.
        const auto g1 = float (std::clamp (std::isfinite (gate) ? gate : 0.0, 0.0, 1.5));
        const auto g0 = lastGate_;
        lastGate_ = g1;
        if (play == nullptr || play->samples.empty()
            || ! playMatchesRequest (play))
        {
            for (size_t i = 0; i < n; ++i) out[i] = 0;
            mixTail (out, n);
            lastLooping_ = false;
            inXfade_ = false;
            inRender_.store (0, std::memory_order_seq_cst);
            return false;
        }
        const auto& s = play->samples;
        const auto sz = int (s.size());
        const auto loopStart = play->loopStart;
        const auto loopEnd = play->loopEnd;
        const bool validLoop = loopEnd > loopStart + 32;
        const bool loopWanted = validLoop
                             && vowelLoop_.load (std::memory_order_relaxed)
                             && sustaining_.load (std::memory_order_relaxed);
        // Pitch-synchronous loop (whole periods) with a 10 ms crossfade from the
        // run-in. Once a crossfade starts it always completes, so releasing the
        // key mid-fade cannot step the waveform.
        const int xfade = validLoop ? std::max (0, std::min ({ play->rate / 100, (loopEnd - loopStart) / 3, loopStart })) : 0;
        auto pos = playPos_.load (std::memory_order_relaxed);
        auto inc = playRatio_.load (std::memory_order_relaxed);
        if (! std::isfinite (inc) || inc <= 0.0) inc = 1.0;
        auto sampleAt = [&] (double p) -> float
        {
            if (p < 0.0 || p >= double (sz)) return 0.0f;
            if (p >= double (sz - 1)) return s[size_t (sz - 1)];
            const auto j = int (p);
            const auto t = float (p - double (j));
            return s[size_t (j)] + (s[size_t (j + 1)] - s[size_t (j)]) * t;
        };
        const auto invN = 1.0f / float (n);
        for (size_t i = 0; i < n; ++i)
        {
            const bool looping = validLoop && (loopWanted || inXfade_);
            if (looping && pos >= double (loopEnd))
            {
                pos -= double (loopEnd - loopStart);
                inXfade_ = false;
            }
            float y = 0;
            if (looping && xfade > 0 && pos >= double (loopEnd - xfade) && pos < double (loopEnd))
            {
                inXfade_ = true;
                const auto t = float ((pos - double (loopEnd - xfade)) / double (xfade));
                const auto a = sampleAt (pos);
                const auto b = sampleAt (pos - double (loopEnd - loopStart)); // the run-in to loopStart
                y = (1.0f - t) * a + t * b;
            }
            else
                y = sampleAt (pos);
            const auto g = g0 + (g1 - g0) * float (i + 1) * invN;
            out[i] = y * g;
            pos += inc;
        }
        lastLooping_ = loopWanted;
        mixTail (out, n);
        playPos_.store (pos, std::memory_order_relaxed);
        inRender_.store (0, std::memory_order_seq_cst);
        return true;
    }

    ProvenStats ProvenEngine::lastStats() const
    {
        std::lock_guard<std::mutex> g (mutex_);
        return stats_;
    }

    std::string ProvenEngine::lastError() const
    {
        std::lock_guard<std::mutex> g (mutex_);
        return error_;
    }
}
