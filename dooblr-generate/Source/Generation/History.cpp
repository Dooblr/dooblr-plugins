#include "History.h"

namespace dooblrgenerate::gen
{
    juce::var HistoryEntry::toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("prompt", prompt);
        o->setProperty ("model", model);
        o->setProperty ("seed", seed);
        o->setProperty ("seconds", seconds);
        o->setProperty ("created", createdMs);
        o->setProperty ("generateSeconds", generateSeconds);
        o->setProperty ("totalSeconds", totalSeconds);
        o->setProperty ("device", device);
        o->setProperty ("isTest", isTest);
        o->setProperty ("wav", wav.getFileName());
        return juce::var (o);
    }

    HistoryEntry HistoryEntry::fromVar (const juce::var& v, const juce::File& folder)
    {
        HistoryEntry e;
        e.id = v["id"].toString();
        e.prompt = v["prompt"].toString();
        e.model = v["model"].toString();
        e.seed = (juce::int64) v["seed"];
        e.seconds = (double) v["seconds"];
        e.createdMs = (juce::int64) v["created"];
        e.generateSeconds = (double) v["generateSeconds"];
        e.totalSeconds = (double) v["totalSeconds"];
        e.device = v["device"].toString();
        e.isTest = (bool) v["isTest"];
        e.wav = folder.getChildFile (v["wav"].toString());
        return e;
    }

    History::History (Paths p) : paths (std::move (p)) { reload(); }

    void History::reload()
    {
        const juce::ScopedLock sl (lock);
        list.clear();
        const auto parsed = juce::JSON::parse (paths.historyIndex());

        if (auto* array = parsed.getArray())
            for (const auto& v : *array)
            {
                auto e = HistoryEntry::fromVar (v, paths.history());
                if (e.id.isNotEmpty() && e.wav.existsAsFile())
                    list.push_back (e);
            }
    }

    bool History::save() const
    {
        juce::Array<juce::var> array;
        for (const auto& e : list)
            array.add (e.toVar());

        paths.history().createDirectory();
        const auto temp = paths.historyIndex().getSiblingFile ("history.json.part");
        return temp.replaceWithText (juce::JSON::toString (juce::var (array)), false, false, "\n") && temp.moveFileTo (paths.historyIndex());
    }

    bool History::add (const HistoryEntry& entry)
    {
        const juce::ScopedLock sl (lock);
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const HistoryEntry& e) { return e.id == entry.id; }), list.end());
        list.insert (list.begin(), entry);
        if ((int) list.size() > maxEntries)
            list.resize ((size_t) maxEntries);
        return save();
    }

    std::vector<HistoryEntry> History::entries() const
    {
        const juce::ScopedLock sl (lock);
        return list;
    }

    int History::size() const
    {
        const juce::ScopedLock sl (lock);
        return (int) list.size();
    }

    bool History::find (const juce::String& id, HistoryEntry& out) const
    {
        const juce::ScopedLock sl (lock);
        for (const auto& e : list)
            if (e.id == id) { out = e; return true; }
        return false;
    }

    juce::File History::newWavFile (const juce::String& prompt, juce::int64 seed, juce::String& idOut, int bpm) const
    {
        juce::String slug;
        for (auto c : prompt.toLowerCase())
        {
            if (juce::CharacterFunctions::isLetterOrDigit (c)) slug << juce::String::charToString (c);
            else if (! slug.endsWithChar ('-')) slug << "-";
            if (slug.length() >= 40) break;
        }
        slug = slug.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
        if (slug.isEmpty()) slug = "sample";

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        auto base = stamp + "-" + slug + "-s" + juce::String (seed);
        if (bpm > 0)
            base << "-" << bpm << "bpm";
        auto file = paths.history().getChildFile (base + ".wav");

        for (int n = 2; file.exists(); ++n)
            file = paths.history().getChildFile (base + "-" + juce::String (n) + ".wav");

        idOut = file.getFileNameWithoutExtension();
        return file;
    }
}
