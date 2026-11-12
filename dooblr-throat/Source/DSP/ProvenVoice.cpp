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

        std::vector<float> resampleLinear (const std::vector<float>& in, int inRate, int outRate)
        {
            if (in.empty() || inRate <= 0 || outRate <= 0) return {};
            if (inRate == outRate) return in;
            const auto outN = std::max<size_t> (1, size_t (std::llround (double (in.size()) * outRate / inRate)));
            std::vector<float> out (outN);
            const auto scale = (in.size() - 1) / std::max (1.0, double (outN - 1));
            for (size_t i = 0; i < outN; ++i)
            {
                const auto x = double (i) * scale;
                const auto j = size_t (x);
                const auto t = float (x - double (j));
                const auto a = in[j];
                const auto b = in[std::min (j + 1, in.size() - 1)];
                out[i] = a + (b - a) * t;
            }
            return out;
        }

        bool worldRetune (std::vector<float>& audio, int fs, double midiHz, bool keepResidual)
        {
            if (audio.size() < size_t (fs / 20)) // < 50 ms, leave as-is
                return true;
            std::vector<double> wave (audio.size());
            for (size_t i = 0; i < audio.size(); ++i)
                wave[i] = audio[i];

            HarvestOption hopt;
            InitializeHarvestOption (&hopt);
            hopt.frame_period = 5.0;
            const int f0n = GetSamplesForHarvest (fs, int (wave.size()), hopt.frame_period);
            if (f0n <= 2) return true;
            std::vector<double> temporal (static_cast<size_t> (f0n));
            std::vector<double> f0c (static_cast<size_t> (f0n));
            std::vector<double> refined (static_cast<size_t> (f0n));
            Harvest (wave.data(), int (wave.size()), fs, &hopt, temporal.data(), f0c.data());
            StoneMask (wave.data(), int (wave.size()), fs, temporal.data(), f0c.data(), f0n, refined.data());

            CheapTrickOption copt;
            InitializeCheapTrickOption (fs, &copt);
            const int fft = GetFFTSizeForCheapTrick (fs, &copt);
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
            D4C (wave.data(), int (wave.size()), fs, temporal.data(), refined.data(), f0n, fft, &dopt, apPtr.data());

            // Median of voiced F0.
            std::vector<double> voiced;
            voiced.reserve (size_t (f0n));
            for (int i = 0; i < f0n; ++i)
                if (refined[i] > 40.0) voiced.push_back (refined[i]);
            double median = midiHz;
            if (! voiced.empty())
            {
                std::nth_element (voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end());
                median = voiced[voiced.size() / 2];
            }
            const auto target = std::clamp (midiHz, 50.0, 800.0);
            for (int i = 0; i < f0n; ++i)
            {
                if (refined[i] <= 40.0) continue;
                if (keepResidual && median > 40.0)
                    refined[i] = target * (0.85 + 0.15 * (refined[i] / median));
                else
                    refined[i] = target;
            }

            const int yLen = int ((f0n - 1) * hopt.frame_period / 1000.0 * fs) + 1;
            if (yLen <= 1) return true;
            std::vector<double> y (size_t (yLen), 0.0);
            Synthesis (refined.data(), f0n, specPtr.data(), apPtr.data(), fft, hopt.frame_period, fs, yLen, y.data());
            audio.resize (size_t (yLen));
            // Soft normalize to avoid WORLD gain swings, keep headroom.
            double peak = 1.0e-6;
            for (double v : y) peak = std::max (peak, std::abs (v));
            const auto scale = (peak > 0.95) ? (0.90 / peak) : 1.0;
            for (int i = 0; i < yLen; ++i)
                audio[size_t (i)] = float (y[size_t (i)] * scale);
            return true;
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
                if (ok)
                    worldRetune (native, nativeRate, midiHz, true);
            }
            if (! ok || native.empty())
                return false;
            auto resampled = resampleLinear (native, nativeRate, outRate);
            fadeEdges (resampled, outRate);
            clip.samples.swap (resampled);
            clip.rate = outRate;
            clip.refHz = midiHz;
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
        while (inRender_.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ownedCache_.reset();
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
    }

    void ProvenEngine::requestPrepare (ProvenKind kind, const std::string& phrase,
                                       const std::vector<std::string>& syllables,
                                       double lengthScale, bool wait)
    {
        if (kind == ProvenKind::None || phrase.empty())
        {
            engaged_.store (false, std::memory_order_release);
            liveCache_.store (nullptr, std::memory_order_release);
            return;
        }
        {
            std::lock_guard<std::mutex> g (mutex_);
            pendingKind_ = kind;
            pendingPhrase_ = phrase;
            pendingSyllables_ = syllables;
            pendingLength_ = lengthScale;
            pending_ = true;
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
            {
                std::unique_lock<std::mutex> lk (mutex_);
                cv_.wait (lk, [&] {
                    return stop_.load (std::memory_order_acquire) || pending_;
                });
                if (stop_.load (std::memory_order_acquire))
                    return;
                kind = pendingKind_;
                phrase = pendingPhrase_;
                syllables = pendingSyllables_;
                length = pendingLength_;
                gen = generation_;
                pending_ = false;
                working_ = true;
            }
            const auto outRate = outputRate_.load (std::memory_order_acquire);
            auto cache = buildCache (kind, phrase, syllables, length, outRate);
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
            readyCv_.notify_all();
        }
    }

    void ProvenEngine::publishCache (std::unique_ptr<ProvenCache> next)
    {
        auto* raw = next.get();
        std::unique_ptr<ProvenCache> old;
        {
            std::lock_guard<std::mutex> g (mutex_);
            old = std::move (ownedCache_);
            ownedCache_ = std::move (next);
            liveCache_.store (raw, std::memory_order_release);
            readyFlag_.store (raw != nullptr, std::memory_order_release);
        }
        while (inRender_.load (std::memory_order_acquire) != 0)
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
        cache->stats.audioSeconds = cache->speak.samples.empty()
            ? 0.0
            : double (cache->speak.samples.size()) / double (cache->rate);
        return cache;
    }

    void ProvenEngine::armPlayback (int mode, int syl, double midiHz) noexcept
    {
        const auto* cache = liveCache_.load (std::memory_order_acquire);
        const auto ref = (cache != nullptr && cache->refHz > 1.0) ? cache->refHz : 261.626;
        auto ratio = (midiHz > 1.0 && ref > 1.0) ? (midiHz / ref) : 1.0;
        if (! std::isfinite (ratio)) ratio = 1.0;
        ratio = std::clamp (ratio, 0.25, 4.0);
        playMode_.store (mode, std::memory_order_release);
        playSyl_.store (syl, std::memory_order_release);
        playPos_.store (0.0, std::memory_order_release);
        playRatio_.store (ratio, std::memory_order_release);
        engaged_.store (cache != nullptr, std::memory_order_release);
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
        playPos_.store (0.0, std::memory_order_release);
        engaged_.store (false, std::memory_order_release);
    }

    bool ProvenEngine::finished() const noexcept
    {
        const auto* cache = liveCache_.load (std::memory_order_acquire);
        if (cache == nullptr) return true;
        const int mode = playMode_.load (std::memory_order_acquire);
        const int si = playSyl_.load (std::memory_order_acquire);
        const ProvenClip* clip = nullptr;
        if (mode == 1 && si >= 0 && si < int (cache->syllables.size()))
            clip = &cache->syllables[size_t (si)];
        else
            clip = &cache->speak;
        if (clip == nullptr || clip->samples.empty()) return true;
        return playPos_.load (std::memory_order_acquire) >= double (clip->samples.size());
    }

    bool ProvenEngine::render (float* out, size_t n, double gate) noexcept
    {
        if (out == nullptr || n == 0) return false;
        inRender_.store (1, std::memory_order_release);
        const auto* cache = liveCache_.load (std::memory_order_acquire);
        const int mode = playMode_.load (std::memory_order_relaxed);
        const int si = playSyl_.load (std::memory_order_relaxed);
        const ProvenClip* clip = nullptr;
        if (cache != nullptr)
        {
            if (mode == 1 && si >= 0 && si < int (cache->syllables.size())
                && ! cache->syllables[size_t (si)].samples.empty())
                clip = &cache->syllables[size_t (si)];
            else
                clip = &cache->speak;
        }
        const auto g = float (std::clamp (gate, 0.0, 1.5));
        if (clip == nullptr || clip->samples.empty())
        {
            for (size_t i = 0; i < n; ++i) out[i] = 0;
            inRender_.store (0, std::memory_order_release);
            return false;
        }
        const auto& s = clip->samples;
        const auto sz = int (s.size());
        auto pos = playPos_.load (std::memory_order_relaxed);
        const auto inc = playRatio_.load (std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            if (pos >= 0.0 && pos < double (sz - 1))
            {
                const auto j = int (pos);
                const auto t = float (pos - double (j));
                out[i] = (s[size_t (j)] + (s[size_t (j + 1)] - s[size_t (j)]) * t) * g;
            }
            else if (pos >= 0.0 && pos < double (sz))
                out[i] = s[size_t (int (pos))] * g;
            else
                out[i] = 0;
            pos += inc;
        }
        playPos_.store (pos, std::memory_order_relaxed);
        inRender_.store (0, std::memory_order_release);
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
