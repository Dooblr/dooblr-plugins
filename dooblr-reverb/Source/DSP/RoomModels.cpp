#include "RoomModels.h"

namespace dooblrreverb::dsp
{
    namespace
    {
        struct Anchor { const char* name; RoomModel model; };

        const std::vector<Anchor>& anchors()
        {
            //                                     space  size  erSpan dens  erLvl diff  bass   air
            static const std::vector<Anchor> list {
                { "Ambience",       { 0.0f,    5.0f,  10.0f, 0.55f, 1.00f, 0.55f, 0.00f, -0.05f } },
                { "Small room",     { 10.0f,   7.5f,  16.0f, 0.60f, 0.95f, 0.60f, 0.10f, -0.10f } },
                { "Room",           { 20.0f,  10.5f,  24.0f, 0.70f, 0.90f, 0.62f, 0.15f, -0.15f } },
                { "Studio",         { 30.0f,  14.0f,  32.0f, 0.75f, 0.85f, 0.64f, 0.20f, -0.20f } },
                { "Chamber",        { 40.0f,  18.0f,  40.0f, 0.85f, 0.80f, 0.70f, 0.25f, -0.25f } },
                { "Club",           { 50.0f,  24.0f,  48.0f, 0.80f, 0.72f, 0.66f, 0.30f, -0.30f } },
                { "Hall",           { 58.0f,  31.0f,  58.0f, 0.90f, 0.64f, 0.72f, 0.35f, -0.35f } },
                { "Concert hall",   { 66.0f,  40.0f,  68.0f, 0.95f, 0.58f, 0.74f, 0.40f, -0.40f } },
                { "Large hall",     { 75.0f,  52.0f,  78.0f, 1.00f, 0.52f, 0.75f, 0.45f, -0.45f } },
                { "Arena",          { 84.0f,  68.0f,  92.0f, 0.70f, 0.46f, 0.70f, 0.50f, -0.50f } },
                { "Cathedral",      { 92.0f,  88.0f, 105.0f, 0.90f, 0.42f, 0.78f, 0.55f, -0.55f } },
                { "Infinite space", { 100.0f, 110.0f, 118.0f, 1.00f, 0.36f, 0.80f, 0.60f, -0.60f } },
            };
            return list;
        }
    }

    const juce::Array<RoomModel>& roomAnchors()
    {
        static const juce::Array<RoomModel> models = []
        {
            juce::Array<RoomModel> a;
            for (const auto& anchor : anchors())
                a.add (anchor.model);
            return a;
        }();
        return models;
    }

    const juce::StringArray& roomNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray n;
            for (const auto& anchor : anchors())
                n.add (anchor.name);
            return n;
        }();
        return names;
    }

    RoomModel roomAt (float space) noexcept
    {
        const auto& list = anchors();
        space = juce::jlimit (0.0f, 100.0f, space);
        size_t i = 0;
        while (i + 2 < list.size() && space > list[i + 1].model.spacePercent)
            ++i;
        const auto& a = list[i].model;
        const auto& b = list[i + 1].model;
        const auto t = juce::jlimit (0.0f, 1.0f, (space - a.spacePercent) / std::max (1.0e-3f, b.spacePercent - a.spacePercent));
        const auto lerp = [t] (float x, float y) { return x + t * (y - x); };
        const auto lerpLog = [t] (float x, float y) { return x * std::pow (y / x, t); };

        RoomModel m;
        m.spacePercent = space;
        m.sizeMs = lerpLog (a.sizeMs, b.sizeMs);
        m.erSpanMs = lerpLog (a.erSpanMs, b.erSpanMs);
        m.erDensity = lerp (a.erDensity, b.erDensity);
        m.erLevel = lerp (a.erLevel, b.erLevel);
        m.diffusion = lerp (a.diffusion, b.diffusion);
        m.bassLog2 = lerp (a.bassLog2, b.bassLog2);
        m.airLog2 = lerp (a.airLog2, b.airLog2);
        return m;
    }

    juce::String roomNameAt (float space)
    {
        const auto& list = anchors();
        size_t best = 0;
        for (size_t i = 1; i < list.size(); ++i)
            if (std::abs (list[i].model.spacePercent - space) < std::abs (list[best].model.spacePercent - space))
                best = i;
        return list[best].name;
    }
}
