#include "English.h"
#include <sys/types.h>
#include <cctype>
#include <cmath>
#include <sstream>
#include <cstring>

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        const char* arpa (std::string_view base)
        {
            if (base == "AA") return "ɑ";
            if (base == "AE") return "æ";
            if (base == "AH") return "ʌ";
            if (base == "AO") return "ɔ";
            if (base == "AW") return "aʊ";
            if (base == "AY") return "aɪ";
            if (base == "EH") return "ɛ";
            if (base == "ER") return "ɝ";
            if (base == "EY") return "eɪ";
            if (base == "IH") return "ɪ";
            if (base == "IY") return "i";
            if (base == "OW") return "oʊ";
            if (base == "OY") return "ɔɪ";
            if (base == "UH") return "ʊ";
            if (base == "UW") return "u";
            if (base == "B") return "b";
            if (base == "CH") return "tʃ";
            if (base == "D") return "d";
            if (base == "DH") return "ð";
            if (base == "F") return "f";
            if (base == "G") return "g";
            if (base == "HH") return "h";
            if (base == "JH") return "dʒ";
            if (base == "K") return "k";
            if (base == "L") return "l";
            if (base == "M") return "m";
            if (base == "N") return "n";
            if (base == "NG") return "ŋ";
            if (base == "P") return "p";
            if (base == "R") return "ɹ";
            if (base == "S") return "s";
            if (base == "SH") return "ʃ";
            if (base == "T") return "t";
            if (base == "TH") return "θ";
            if (base == "V") return "v";
            if (base == "W") return "w";
            if (base == "Y") return "j";
            if (base == "Z") return "z";
            if (base == "ZH") return "ʒ";
            return nullptr;
        }

        const char* kVowels[] = {
            "ɑ", "æ", "ʌ", "ə", "ɔ", "aʊ", "aɪ", "ɛ", "ɝ", "ɚ",
            "eɪ", "ɪ", "i", "oʊ", "ɔɪ", "ʊ", "u"
        };

        bool isVowel (std::string_view ipa)
        {
            for (auto v : kVowels) if (ipa == v) return true;
            return false;
        }

        const char* homograph (std::string_view w)
        {
            if (w == "wind") return "W IH1 N D";
            if (w == "read") return "R IY1 D";
            if (w == "live") return "L IH1 V";
            if (w == "lead") return "L IY1 D";
            if (w == "close") return "K L OW1 Z";
            if (w == "wound") return "W UW1 N D";
            return nullptr;
        }

        const char* kFunction =
            "a an the of to in on at by for from with as and or but nor so if is am are was were "
            "be been do does did has have had will would can could should shall may might must it "
            "its i you he she we they me him her us them my your his our their this that than then "
            "there not no";

        bool isFunctionWord (std::string_view w)
        {
            std::string_view s = kFunction;
            while (! s.empty())
            {
                auto sp = s.find (' ');
                auto tok = s.substr (0, sp == std::string_view::npos ? s.size() : sp);
                if (tok == w) return true;
                if (sp == std::string_view::npos) break;
                s.remove_prefix (sp + 1);
            }
            return false;
        }

        const char* reduceVowel (std::string_view ipa)
        {
            if (ipa == "ʌ" || ipa == "æ" || ipa == "ɑ" || ipa == "ɛ"
                || ipa == "ɔ" || ipa == "u" || ipa == "ʊ")
                return "ə";
            return nullptr;
        }

        bool jsIsSpace (char32_t c)
        {
            return c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r' || c == ' '
                || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
                || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f
                || c == 0x3000 || c == 0xfeff;
        }

        bool isPhrasePunct (char32_t c)
        {
            return c == '.' || c == '!' || c == '?' || c == ';' || c == ':' || c == ',';
        }

        std::string toLowerAscii (std::string_view s)
        {
            std::string o;
            o.reserve (s.size());
            for (unsigned char c : s) o.push_back (char (std::tolower (c)));
            return o;
        }

        std::string replaceApostrophe (std::string_view text)
        {
            std::string o;
            o.reserve (text.size());
            for (size_t i = 0; i < text.size();)
            {
                // UTF-8 right single quotation mark e2 80 99
                if (i + 2 < text.size()
                    && (unsigned char) text[i] == 0xe2
                    && (unsigned char) text[i + 1] == 0x80
                    && (unsigned char) text[i + 2] == 0x99)
                {
                    o.push_back ('\'');
                    i += 3;
                }
                else { o.push_back (text[i]); ++i; }
            }
            return o;
        }

        // ---- numbers
        const char* ONES[] = { "zero","one","two","three","four","five","six","seven","eight","nine",
            "ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen" };
        const char* TENS[] = { "","","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety" };

        std::string jsIntegerToString (double n)
        {
            if (! std::isfinite (n)) return "Infinity";
            if (n < 1e21)
            {
                std::ostringstream os; os << n; return os.str();
            }
            std::ostringstream os; os << std::scientific << n;
            auto s = os.str();
            auto e = s.find ('e');
            if (e != std::string::npos && e + 1 < s.size() && s[e + 1] != '+' && s[e + 1] != '-')
                s.insert (e + 1, "+");
            return s;
        }

        std::string numberWords (double n)
        {
            if (n < 20.0) return ONES[size_t (n)];
            if (n < 100.0)
            {
                std::string s = TENS[size_t (std::floor (n / 10.0))];
                if (std::fmod (n, 10.0) != 0.0) { s += " "; s += ONES[size_t (std::fmod (n, 10.0))]; }
                return s;
            }
            if (n < 1000.0)
            {
                std::string s = std::string (ONES[size_t (std::floor (n / 100.0))]) + " hundred";
                if (std::fmod (n, 100.0) != 0.0) { s += " "; s += numberWords (std::fmod (n, 100.0)); }
                return s;
            }
            if (n < 1e6)
            {
                std::string s = numberWords (std::floor (n / 1000.0)) + " thousand";
                if (std::fmod (n, 1000.0) != 0.0) { s += " "; s += numberWords (std::fmod (n, 1000.0)); }
                return s;
            }
            std::string out, digits = jsIntegerToString (n);
            for (char d : digits)
            {
                if (! out.empty()) out += " ";
                if (d >= '0' && d <= '9') out += ONES[d - '0'];
            }
            return out;
        }

        std::string expandNumbers (std::string_view t)
        {
            std::string out, digits;
            auto emit = [&] {
                if (digits.empty()) return;
                out.push_back (' ');
                out += numberWords (std::stod (digits));
                out.push_back (' ');
                digits.clear();
            };
            for (char c : t)
            {
                if (c >= '0' && c <= '9') digits.push_back (c);
                else { emit(); out.push_back (c); }
            }
            emit();
            return out;
        }

        std::vector<std::string> splitKeeping (const std::string& s, bool (*delim) (char32_t))
        {
            std::vector<std::string> out;
            std::string cur;
            for (size_t i = 0; i < s.size();)
            {
                unsigned char c = (unsigned char) s[i];
                // treat as Latin-1/ASCII for punctuation (phrase punct is ASCII)
                if (! delim (c)) { cur.push_back (s[i]); ++i; continue; }
                out.push_back (std::move (cur)); cur.clear();
                std::string run (1, s[i]); ++i;
                while (i < s.size() && delim ((unsigned char) s[i])) { run.push_back (s[i]); ++i; }
                out.push_back (std::move (run));
            }
            out.push_back (std::move (cur));
            return out;
        }

        void applyAllophones (std::vector<Segment>& segs)
        {
            if (segs.size() >= 3)
            {
                for (size_t i = 1; i + 1 < segs.size(); ++i)
                {
                    const auto& prev = segs[i - 1];
                    const auto& next = segs[i + 1];
                    const bool pv = isVowel (prev.ipa) || prev.ipa == "ɹ";
                    const bool nv = isVowel (next.ipa);
                    const bool nextStressed = next.stress.has_value() && *next.stress != 0;
                    const bool prevWordEnd = prev.wordEnd;
                    auto& s = segs[i];
                    if ((s.ipa == "t" || s.ipa == "d") && pv && nv && ! nextStressed && ! prevWordEnd)
                        s.ipa = "ɾ";
                }
            }
            for (size_t i = 0; i < segs.size(); ++i)
            {
                if (segs[i].ipa != "l") continue;
                const bool nextVowel = i + 1 < segs.size() && isVowel (segs[i + 1].ipa);
                if (! nextVowel || segs[i].wordEnd)
                    segs[i].ipa = "ɫ";
            }
        }

        // letter-to-sound
        struct Rule { const char* pre; const char* g; const char* post; const char* ph; };
        const Rule kRules[] = {
            {"", "tion", "", "ʃ ə n"}, {"", "sion", "", "ʒ ə n"}, {"", "ture", "", "tʃ ɚ"},
            {"", "ough", "#", "oʊ"}, {"", "augh", "", "ɔ"},
            {"", "igh", "", "aɪ"}, {"", "eigh", "", "eɪ"}, {"", "tch", "", "tʃ"},
            {"", "dge", "", "dʒ"}, {"", "sch", "", "s k"},
            {"", "ch", "", "tʃ"}, {"", "sh", "", "ʃ"}, {"", "th", "", "θ"},
            {"", "ph", "", "f"}, {"", "wh", "", "w"}, {"", "ck", "", "k"},
            {"", "ng", "#", "ŋ"}, {"", "ng", "", "ŋ g"}, {"", "qu", "", "k w"},
            {"#", "kn", "", "n"}, {"#", "wr", "", "ɹ"}, {"", "gh", "", ""},
            {"", "ee", "", "i"}, {"", "ea", "", "i"}, {"", "oo", "", "u"},
            {"", "ou", "", "aʊ"}, {"", "ow", "#", "oʊ"}, {"", "ow", "", "aʊ"},
            {"", "oi", "", "ɔɪ"}, {"", "oy", "", "ɔɪ"}, {"", "ai", "", "eɪ"},
            {"", "ay", "", "eɪ"}, {"", "au", "", "ɔ"}, {"", "aw", "", "ɔ"},
            {"", "ie", "#", "aɪ"}, {"", "ie", "", "i"}, {"", "ei", "", "eɪ"},
            {"", "ey", "#", "i"}, {"", "oa", "", "oʊ"}, {"", "ue", "#", "u"},
            {"", "er", "#", "ɚ"}, {"", "er", "C", "ɝ"}, {"", "ir", "", "ɝ"},
            {"", "ur", "", "ɝ"}, {"", "or", "", "ɔ ɹ"}, {"", "ar", "", "ɑ ɹ"},
            {"", "le", "#", "ə ɫ"}, {"", "ed", "#", "d"}, {"", "es", "#", "z"},
            {"C", "s", "#", "s"}, {"V", "s", "#", "z"}, {"", "y", "#", "i"},
            {"#", "y", "", "j"}, {"C", "y", "", "aɪ"}, {"", "e", "#", ""},
            {"", "a", "CV", "eɪ"}, {"", "a", "", "æ"}, {"", "e", "", "ɛ"},
            {"", "i", "CV", "aɪ"}, {"", "i", "", "ɪ"}, {"", "o", "CV", "oʊ"},
            {"", "o", "", "ɑ"}, {"", "u", "CV", "u"}, {"", "u", "", "ʌ"}, {"", "y", "", "ɪ"},
            {"", "c", "e", "s"}, {"", "c", "i", "s"}, {"", "c", "y", "s"}, {"", "c", "", "k"},
            {"", "g", "e", "dʒ"}, {"", "g", "i", "dʒ"}, {"", "g", "", "g"},
            {"", "x", "", "k s"}, {"", "j", "", "dʒ"}, {"", "q", "", "k"},
            {"", "b", "", "b"}, {"", "d", "", "d"}, {"", "f", "", "f"}, {"", "h", "", "h"},
            {"", "k", "", "k"}, {"", "l", "", "l"}, {"", "m", "", "m"}, {"", "n", "", "n"},
            {"", "p", "", "p"}, {"", "r", "", "ɹ"}, {"", "s", "", "s"},
            {"", "t", "", "t"}, {"", "v", "", "v"}, {"", "w", "", "w"}, {"", "z", "", "z"},
        };

        bool isVLetter (int ch)
        {
            return ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' || ch == 'y';
        }

        int at (const std::vector<uint16_t>& word, ssize_t pos)
        {
            if (pos < 0 || size_t (pos) >= word.size()) return -1;
            return int (word[size_t (pos)]);
        }

        bool matchCtx (const std::vector<uint16_t>& word, ssize_t i, const char* ctx, ssize_t dir)
        {
            if (ctx[0] == 0) return true;
            const auto n = std::strlen (ctx);
            auto pos = i;
            for (size_t k = 0; k < n; ++k)
            {
                const auto c = (unsigned char) (dir == 1 ? ctx[k] : ctx[n - 1 - k]);
                const auto ch = at (word, pos);
                if (c == '#') { if (ch >= 0) return false; }
                else if (c == 'V') { if (! isVLetter (ch)) return false; }
                else if (c == 'C') { if (ch < 0 || isVLetter (ch)) return false; }
                else if (ch != int (c)) return false;
                pos += dir;
            }
            return true;
        }

        bool startsWithAt (const std::vector<uint16_t>& word, size_t i, const char* g)
        {
            const auto n = std::strlen (g);
            if (word.size() < i + n) return false;
            for (size_t k = 0; k < n; ++k)
                if (word[i + k] != (unsigned char) g[k]) return false;
            return true;
        }
    } // namespace

    bool isEnglishVowelIpa (std::string_view ipa) { return isVowel (ipa); }

    void EnglishG2P::loadDict (std::string_view text)
    {
        dict_.clear();
        dict_.reserve (140000);
        size_t i = 0;
        while (i < text.size())
        {
            auto nl = text.find ('\n', i);
            if (nl == std::string_view::npos) nl = text.size();
            auto line = text.substr (i, nl - i);
            if (! line.empty() && line.back() == '\r') line.remove_suffix (1);
            auto tab = line.find ('\t');
            if (tab != std::string_view::npos && tab > 0)
                dict_.emplace (std::string (line.substr (0, tab)), std::string (line.substr (tab + 1)));
            i = nl + (nl < text.size() ? 1 : 0);
            if (nl == text.size()) break;
        }
        loaded_ = true;
    }

    std::vector<Segment> arpabetToSegments (std::string_view phones)
    {
        std::vector<Segment> out;
        size_t i = 0;
        while (i < phones.size())
        {
            while (i < phones.size() && jsIsSpace ((unsigned char) phones[i])) ++i;
            if (i >= phones.size()) break;
            auto j = i;
            while (j < phones.size() && ! jsIsSpace ((unsigned char) phones[j])) ++j;
            auto ph = phones.substr (i, j - i);
            i = j;
            int digit = -1;
            if (! ph.empty())
            {
                auto last = (unsigned char) ph.back();
                if (last == '0' || last == '1' || last == '2')
                {
                    digit = last - '0';
                    ph.remove_suffix (1);
                }
            }
            if (ph.empty()) continue;
            bool ok = true;
            for (unsigned char b : ph) if (! std::isupper (b)) { ok = false; break; }
            if (! ok) continue;
            auto ipa = arpa (ph);
            if (! ipa) continue;
            std::string ipaS = ipa;
            const int stress = digit == 1 ? 1 : digit == 2 ? 2 : 0;
            if (ph == "AH" && stress == 0) ipaS = "ə";
            if (ph == "ER" && stress == 0) ipaS = "ɚ";
            Segment s;
            s.ipa = ipaS;
            s.stress = uint8_t (isVowel (ipaS) ? stress : 0);
            out.push_back (std::move (s));
        }
        return out;
    }


    std::vector<Segment> letterToSound (std::string_view word)
    {
        std::vector<uint16_t> w;
        w.reserve (word.size());
        for (unsigned char c : word) w.push_back (c);

        std::vector<std::string> phones;
        size_t i = 0;
        while (i < w.size())
        {
            bool done = false;
            for (const auto& r : kRules)
            {
                if (! startsWithAt (w, i, r.g)) continue;
                if (! matchCtx (w, ssize_t (i) - 1, r.pre, -1)) continue;
                if (! matchCtx (w, ssize_t (i + std::strlen (r.g)), r.post, 1)) continue;
                if (r.ph[0] != 0)
                {
                    std::string_view ph = r.ph;
                    size_t p = 0;
                    while (p < ph.size())
                    {
                        while (p < ph.size() && ph[p] == ' ') ++p;
                        if (p >= ph.size()) break;
                        auto q = ph.find (' ', p);
                        if (q == std::string_view::npos) q = ph.size();
                        phones.emplace_back (ph.substr (p, q - p));
                        p = q;
                    }
                }
                i += std::strlen (r.g);
                done = true;
                break;
            }
            if (! done) ++i;
        }

        std::vector<Segment> out;
        bool stressed = false;
        for (const auto& ipa : phones)
        {
            const bool v = isVowel (ipa);
            Segment s;
            s.ipa = ipa;
            s.stress = uint8_t (v && ! stressed ? 1 : 0);
            if (v) stressed = true;
            out.push_back (std::move (s));
        }
        return out;
    }

    std::vector<Segment> EnglishG2P::word (std::string_view w, bool reduce) const
    {
        auto lower = toLowerAscii (w);
        size_t a = 0, b = lower.size();
        while (a < b && lower[a] == '\'') ++a;
        while (b > a && lower[b - 1] == '\'') --b;
        const auto clean = lower.substr (a, b - a);

        const char* homo = homograph (clean);
        const char* entry = homo;
        std::string stored;
        if (entry == nullptr)
        {
            auto it = dict_.find (clean);
            if (it != dict_.end() && ! it->second.empty())
            {
                stored = it->second;
                entry = stored.c_str();
            }
        }
        if (entry != nullptr && entry[0] != 0)
        {
            auto segs = arpabetToSegments (entry);
            if (reduce && isFunctionWord (clean))
            {
                int nv = 0;
                for (const auto& s : segs) if (isVowel (s.ipa)) ++nv;
                const bool skip = (clean == "you" || clean == "do" || clean == "to"
                                   || clean == "who" || clean == "not" || clean == "no");
                for (auto& s : segs)
                {
                    s.stress = uint8_t (0);
                    if (nv == 1 && ! skip)
                        if (auto r = reduceVowel (s.ipa))
                            s.ipa = r;
                }
            }
            return segs;
        }
        if (clean.find ('-') != std::string::npos)
        {
            std::vector<Segment> all;
            size_t p = 0;
            while (p < clean.size())
            {
                auto d = clean.find ('-', p);
                if (d == std::string::npos) d = clean.size();
                if (d > p)
                {
                    auto part = word (std::string_view (clean).substr (p, d - p), reduce);
                    all.insert (all.end(), part.begin(), part.end());
                }
                p = d + (d < clean.size() ? 1 : 0);
                if (d == clean.size()) break;
            }
            return all;
        }
        return letterToSound (clean);
    }

    std::vector<Phrase> EnglishG2P::toPhrases (std::string_view text) const
    {
        std::vector<Phrase> phrases;
        const auto normalized = expandNumbers (replaceApostrophe (text));
        const auto parts = splitKeeping (normalized, isPhrasePunct);
        std::vector<std::string> words;

        auto flush = [&] (std::string_view delim)
        {
            if (words.empty()) return;
            std::vector<Segment> segs;
            std::vector<std::string> said;
            for (const auto& w : words)
            {
                auto ws = word (w, true);
                if (ws.empty()) continue;
                ws.back().wordEnd = true;
                segs.insert (segs.end(), ws.begin(), ws.end());
                said.push_back (w);
            }
            applyAllophones (segs);
            const bool q = delim.find ('?') != std::string_view::npos;
            const double pause = (delim.find ('.') != std::string_view::npos
                                  || delim.find ('!') != std::string_view::npos
                                  || delim.find ('?') != std::string_view::npos)
                                 ? 0.35
                                 : (! delim.empty() ? 0.18 : 0.3);
            words.clear();
            if (! segs.empty())
                phrases.push_back (Phrase { std::move (segs), q ? Intonation::Question : Intonation::Statement, pause, std::move (said) });
        };

        auto isWordChar = [] (char c) {
            return (c >= 'a' && c <= 'z') || c == '\'' || c == '-';
        };

        for (const auto& part : parts)
        {
            bool allPunct = ! part.empty();
            for (unsigned char c : part) if (! isPhrasePunct (c)) { allPunct = false; break; }
            if (allPunct)
            {
                flush (part);
            }
            else
            {
                auto lower = toLowerAscii (part);
                size_t i = 0;
                while (i < lower.size())
                {
                    if (! isWordChar (lower[i])) { ++i; continue; }
                    size_t j = i + 1;
                    while (j < lower.size() && isWordChar (lower[j])) ++j;
                    words.emplace_back (lower.substr (i, j - i));
                    i = j;
                }
            }
        }
        flush ("");
        return phrases;
    }
}
