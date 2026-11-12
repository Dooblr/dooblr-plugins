#include "Durations.h"
#include <sys/types.h>
#include <map>

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        bool table (std::string_view ipa, double& inh, double& min)
        {
            auto set = [&] (double i, double m) { inh = i; min = m; return true; };
            if (ipa == "i") return set (155, 55);
            if (ipa == "ɪ") return set (135, 40);
            if (ipa == "e") return set (190, 70);
            if (ipa == "eɪ") return set (190, 70);
            if (ipa == "ɛ") return set (150, 70);
            if (ipa == "æ") return set (230, 80);
            if (ipa == "a") return set (230, 80);
            if (ipa == "ɑ") return set (240, 100);
            if (ipa == "ɒ") return set (240, 100);
            if (ipa == "ɔ") return set (240, 100);
            if (ipa == "o") return set (220, 80);
            if (ipa == "oʊ") return set (220, 80);
            if (ipa == "ʊ") return set (160, 60);
            if (ipa == "u") return set (210, 70);
            if (ipa == "ʌ") return set (140, 60);
            if (ipa == "ə") return set (120, 60);
            if (ipa == "ɚ") return set (180, 80);
            if (ipa == "ɝ") return set (180, 80);
            if (ipa == "aɪ") return set (250, 150);
            if (ipa == "aʊ") return set (240, 100);
            if (ipa == "ɔɪ") return set (280, 150);
            if (ipa == "ɨ") return set (110, 40);
            if (ipa == "p") return set (85, 50);
            if (ipa == "t") return set (75, 50);
            if (ipa == "k") return set (80, 60);
            if (ipa == "b") return set (85, 60);
            if (ipa == "d") return set (75, 50);
            if (ipa == "g") return set (80, 60);
            if (ipa == "ʔ") return set (40, 30);
            if (ipa == "tʃ") return set (70, 50);
            if (ipa == "dʒ") return set (50, 30);
            if (ipa == "f") return set (100, 80);
            if (ipa == "v") return set (60, 40);
            if (ipa == "θ") return set (90, 60);
            if (ipa == "ð") return set (50, 30);
            if (ipa == "s") return set (105, 60);
            if (ipa == "z") return set (75, 40);
            if (ipa == "ʃ") return set (105, 80);
            if (ipa == "ʒ") return set (70, 40);
            if (ipa == "h") return set (80, 20);
            if (ipa == "m") return set (70, 60);
            if (ipa == "n") return set (60, 50);
            if (ipa == "ŋ") return set (95, 45);
            if (ipa == "l") return set (80, 40);
            if (ipa == "ɫ") return set (80, 40);
            if (ipa == "ɹ") return set (80, 30);
            if (ipa == "w") return set (80, 60);
            if (ipa == "j") return set (80, 40);
            if (ipa == "ɾ") return set (20, 20);
            return false;
        }
    }

    std::vector<double> klattDurations (const std::vector<Segment>& segs, const std::vector<SegInfo>& inf)
    {
        const auto n = segs.size();
        std::vector<double> out (n, 0.08);
        if (n == 0) return out;

        std::vector<size_t> word (n, 0), wordStart;
        for (size_t i = 0; i < n; ++i)
        {
            if (i == 0 || segs[i - 1].wordEnd)
                wordStart.push_back (i);
            word[i] = wordStart.size() - 1;
        }
        std::map<size_t, size_t> vowelsInWord;
        ssize_t lastVowel = -1;
        for (size_t i = 0; i < n; ++i)
            if (inf[i].vowel) { vowelsInWord[word[i]] += 1; lastVowel = ssize_t (i); }

        for (size_t i = 0; i < n; ++i)
        {
            if (segs[i].dur.has_value()) { out[i] = *segs[i].dur; continue; }
            const auto fallback = inf[i].dur.value_or (0.08);
            double inh0, min0;
            if (! table (segs[i].ipa, inh0, min0)) { inh0 = 1000.0 * fallback; min0 = 600.0 * fallback; }
            auto inh = inh0, min = min0, p = 1.0;
            if (inf[i].vowel)
            {
                const int stress = segs[i].stress.value_or (0);
                const bool finalSyl = ssize_t (i) == lastVowel;
                bool wordFinalSyl = true;
                for (size_t k = i + 1; k < n && word[k] == word[i]; ++k)
                    if (inf[k].vowel) wordFinalSyl = false;
                p *= finalSyl ? 1.4 : 0.6;
                if (! wordFinalSyl) p *= 0.85;
                if (vowelsInWord[word[i]] > 1) p *= 0.8;
                if (stress == 0) { min *= 0.5; p *= 0.7; }
                const bool hasNx = i + 1 < n && word[i + 1] == word[i] && ! inf[i + 1].vowel;
                if (hasNx)
                {
                    const auto& nx = inf[i + 1];
                    const bool voiced = nx.voiced == true;
                    double f = 1.0;
                    if (nx.manner == Manner::Fricative) f = voiced ? 1.6 : 1.0;
                    else if (nx.manner == Manner::Stop || nx.manner == Manner::Affricate) f = voiced ? 1.2 : 0.7;
                    else if (nx.manner == Manner::Nasal) f = 0.85;
                    p *= finalSyl ? f : (0.7 + 0.3 * f);
                }
                if (i + 1 < n && inf[i + 1].vowel) p *= 1.2;
                if (i > 0 && inf[i - 1].vowel) p *= 0.7;
                auto dur = min + (inh - min) * p;
                if (stress > 0 && i > 0 && inf[i - 1].manner == Manner::Stop && inf[i - 1].voiced != true)
                    dur += 20.0;
                out[i] = dur / 1000.0;
                continue;
            }
            const bool wordInitial = i == wordStart[word[i]];
            if (ssize_t (i) > lastVowel
                && (inf[i].manner == Manner::Nasal || inf[i].manner == Manner::Lateral || inf[i].manner == Manner::Approximant))
                p *= 1.4;
            if (! wordInitial) p *= 0.85;
            const bool prevC = i > 0 && ! inf[i - 1].vowel;
            const bool nextC = i + 1 < n && ! inf[i + 1].vowel;
            if (prevC && nextC) p *= 0.5;
            else if (prevC || nextC) p *= 0.7;
            out[i] = (min + (inh - min) * p) / 1000.0;
        }
        return out;
    }
}
