#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

namespace lowblow
{
struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };

// mode: 0 Lows Only, 1 Full. Anything not listed stays at its default.
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> p {
        { "Init (Rock)",              { } },
        { "Metal: Tight Double Kick", { { "depth", 6 },  { "attack", 0.5f }, { "hold", 5 },  { "release", 40 },  { "crossover", 130 } } },
        { "Rock: Punchy",             { { "depth", 5 },  { "attack", 1 },    { "hold", 20 }, { "release", 90 } } },
        { "Punk: Fast & Loose",       { { "depth", 4 },  { "attack", 0.5f }, { "hold", 10 }, { "release", 60 } } },
        { "Pop: Clean Low End",       { { "depth", 8 },  { "attack", 1.5f }, { "hold", 25 }, { "release", 120 }, { "crossover", 150 } } },
        { "Gentle Clearance",         { { "depth", 3 },  { "attack", 2 },    { "hold", 15 }, { "release", 120 } } },
        { "Sub Only",                 { { "depth", 8 },  { "crossover", 70 }, { "release", 80 } } },
        { "Full Duck",                { { "depth", 6 },  { "mode", 1 },      { "hold", 10 }, { "release", 80 } } },
        { "Pump It",                  { { "depth", 12 }, { "mode", 1 },      { "attack", 3 }, { "hold", 0 }, { "release", 220 }, { "sensitivity", 70 } } },
    };
    return p;
}
} // namespace lowblow
