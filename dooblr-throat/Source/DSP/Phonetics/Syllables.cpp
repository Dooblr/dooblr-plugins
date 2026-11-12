#include "Syllables.h"
#include <sys/types.h>
#include "Inventory.h"
#include <algorithm>
#include <cctype>

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        const char* kOnsets[] = {
            "p","b","t","d","k","g","f","v", "θ", "ð","s","z", "ʃ", "ʒ","h","m","n","l", "ɹ","w","j", "tʃ", "dʒ",
            "pl","bl","kl","gl","fl","sl", "pɹ", "bɹ", "tɹ", "dɹ", "kɹ", "gɹ", "fɹ", "θɹ", "ʃɹ",
            "sp","st","sk","sm","sn","sw","sf","tw","kw","dw","gw", "θw",
            "pj","bj","kj","gj","mj","nj","fj","vj","hj","lj",
            "spl", "spɹ", "stɹ", "skɹ","skw","skj","spj"
        };

        bool isOnset (std::string_view cluster)
        {
            for (auto o : kOnsets) if (cluster == o) return true;
            return false;
        }

        bool isLyricPunct (char c)
        {
            return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '\n';
        }

        std::vector<std::string> lyricTokens (const std::string& text)
        {
            std::vector<std::string> out;
            std::u32string s; // use UTF-8 walking with raw string + ASCII mostly
            // lyrics are English: walk UTF-8 as chars via bytes for ASCII tokens, letters are ASCII
            const auto n = text.size();
            size_t i = 0;
            auto isWordStart = [] (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\''; };
            auto isSpace = [] (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
            while (i < n)
            {
                const char c = text[i];
                if (isWordStart (c))
                {
                    size_t j = i + 1;
                    while (j < n && (isWordStart (text[j]) || text[j] == '-')) ++j;
                    out.push_back (text.substr (i, j - i));
                    i = j;
                    continue;
                }
                const bool melisma = (c == '~' || c == '-')
                    && (i == 0 || isSpace (text[i - 1]))
                    && (i + 1 == n || isSpace (text[i + 1]));
                if (melisma || isLyricPunct (c))
                    out.emplace_back (1, c);
                ++i;
            }
            return out;
        }

        std::vector<std::string> splitSpelling (const std::string& word, size_t nSyl)
        {
            if (nSyl <= 1) return { word };
            // Approximate hyphenation by vowel groups (ASCII English).
            std::vector<std::pair<size_t,size_t>> groups;
            const auto isV = [] (char c) {
                auto x = char (std::tolower ((unsigned char) c));
                return x=='a'||x=='e'||x=='i'||x=='o'||x=='u'||x=='y';
            };
            size_t i = 0;
            while (i < word.size())
            {
                if (isV (word[i]))
                {
                    auto s = i;
                    while (i < word.size() && isV (word[i])) ++i;
                    groups.push_back ({s, i});
                }
                else ++i;
            }
            const auto len = word.size();
            auto eq = [&] (size_t k, char lo) {
                if (k >= len) return false;
                return char (std::tolower ((unsigned char) word[k])) == lo;
            };
            const bool silentE = len >= 2 && eq (len - 1, 'e') && ! isV (word[len - 2]);
            const bool le = len >= 3 && eq (len - 1, 'e') && eq (len - 2, 'l')
                && ! isV (word[len - 3]) && ! eq (len - 3, 'l');
            if (groups.size() > 1 && silentE && ! le && groups.back().first + 1 == len)
                groups.pop_back();
            if (groups.size() < nSyl)
            {
                std::vector<std::string> out { word };
                for (size_t k = 1; k < nSyl; ++k) out.emplace_back ("~");
                return out;
            }
            while (groups.size() > nSyl)
            {
                auto last = groups.back(); groups.pop_back();
                groups.back().second = last.second;
            }
            std::vector<size_t> cuts { 0 };
            auto isDig = [&] (size_t b) {
                if (b < 2) return false;
                const char d0 = char (std::tolower ((unsigned char) word[b - 2]));
                const char d1 = char (std::tolower ((unsigned char) word[b - 1]));
                const char* digs[] = { "th","sh","ch","ph","wh","ck","gh" };
                for (auto d : digs) if (d0 == d[0] && d1 == d[1]) return true;
                return false;
            };
            for (size_t g = 0; g + 1 < groups.size(); ++g)
            {
                const auto a = groups[g].second, b = groups[g + 1].first;
                const auto gap = b - a;
                cuts.push_back (isDig (b) && gap >= 2 ? b - 2 : a + (gap <= 1 ? 0 : gap / 2));
            }
            cuts.push_back (len);
            std::vector<std::string> out;
            for (size_t k = 0; k + 1 < cuts.size(); ++k)
            {
                auto a = std::min (cuts[k], len), b = std::min (cuts[k + 1], len);
                out.push_back (a < b ? word.substr (a, b - a) : std::string());
            }
            return out;
        }
    }

    std::vector<SyllableParts> syllabifySegments (const std::vector<Segment>& segs)
    {
        std::vector<size_t> vi;
        for (size_t i = 0; i < segs.size(); ++i)
            if (isVowelIpa (segs[i].ipa, BodyMix::MALE)) vi.push_back (i);
        if (vi.empty()) return {};
        std::vector<SyllableParts> out;
        size_t start = 0;
        for (size_t n = 0; n < vi.size(); ++n)
        {
            const auto v = vi[n];
            size_t end = segs.size();
            if (n + 1 < vi.size())
            {
                const auto next = vi[n + 1];
                size_t k = 0;
                const auto betweenN = next - (v + 1);
                const auto maxLen = std::min (size_t (3), betweenN);
                for (size_t len = maxLen; len >= 1; --len)
                {
                    std::string cluster;
                    for (size_t j = next - len; j < next; ++j) cluster += segs[j].ipa;
                    if (isOnset (cluster)) { k = len; break; }
                }
                end = next - k;
            }
            SyllableParts p;
            p.onset.assign (segs.begin() + ssize_t (start), segs.begin() + ssize_t (v));
            p.nucleus = segs[v];
            p.coda.assign (segs.begin() + ssize_t (v + 1), segs.begin() + ssize_t (end));
            out.push_back (std::move (p));
            start = end;
        }
        return out;
    }

    std::vector<Syllable> lyricsToSyllables (std::string_view text, const EnglishG2P& g2p)
    {
        std::vector<Syllable> out;
        const auto normalized = [&] {
            // reuse English apostrophe fold by a tiny local copy
            std::string o;
            for (size_t i = 0; i < text.size();)
            {
                if (i + 2 < text.size()
                    && (unsigned char) text[i] == 0xe2
                    && (unsigned char) text[i + 1] == 0x80
                    && (unsigned char) text[i + 2] == 0x99)
                { o.push_back ('\''); i += 3; }
                else { o.push_back (text[i]); ++i; }
            }
            return o;
        }();
        const auto tokens = lyricTokens (normalized);
        for (const auto& tok : tokens)
        {
            if (tok.size() == 1 && isLyricPunct (tok[0]))
            {
                if (! out.empty()) out.back().phraseEnd = true;
                continue;
            }
            if (tok == "-" || tok == "~")
            {
                if (! out.empty())
                {
                    Syllable s;
                    s.text = "~";
                    s.nucleus = out.back().nucleus;
                    s.melisma = true;
                    s.wordEnd = out.back().wordEnd;
                    out.push_back (std::move (s));
                }
                continue;
            }
            std::vector<std::string> parts;
            size_t p = 0;
            while (p < tok.size())
            {
                auto h = tok.find ('-', p);
                if (h == std::string::npos) h = tok.size();
                if (h > p) parts.push_back (tok.substr (p, h - p));
                p = h + (h < tok.size() ? 1 : 0);
                if (h == tok.size()) break;
            }
            std::vector<Syllable> syls;
            for (const auto& part : parts)
            {
                auto lower = part;
                for (auto& c : lower) c = char (std::tolower ((unsigned char) c));
                auto sy = syllabifySegments (g2p.word (lower, false));
                std::vector<std::string> spell;
                if (parts.size() > 1)
                {
                    spell.push_back (part);
                    for (size_t k = 1; k < sy.size(); ++k) spell.emplace_back ("~");
                }
                else spell = splitSpelling (part, sy.size());
                for (size_t i = 0; i < sy.size(); ++i)
                {
                    Syllable s;
                    s.text = i < spell.size() ? spell[i] : "~";
                    s.onset = std::move (sy[i].onset);
                    s.nucleus = std::move (sy[i].nucleus);
                    s.coda = std::move (sy[i].coda);
                    syls.push_back (std::move (s));
                }
            }
            if (! syls.empty()) syls.back().wordEnd = true;
            out.insert (out.end(), syls.begin(), syls.end());
        }
        return out;
    }
}
