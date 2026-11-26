// Admiral 1506 - Kit-Designer: erzeugt per Zufall musikalisch sinnvolle Kits (NEW KIT) oder variiert
// das aktuelle leicht (MUTATE). Der Zufall folgt Regeln: Genre -> Charakter -> Regeln je Instrument.
// Ohne JUCE-Abhängigkeit (getestet in tests/core_test.cpp).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "Params.h"

namespace ad
{
// Klang-Einstellungen einer Spur (alles, was der Designer verändern darf - Mute, OUT, Master, Tempo, MIDI nie)
struct TrackSound
{
    int src = 0;                         // 0 = globales Kit, 1..NUM_KITS = bestimmtes Kit
    float level = 0.8f, pan = 0.f, tune = 0.f, decay = 1.f;
    float hp = 20.f, lp = 20000.f, res = 0.f;
    int dsType = 0;
    float dsDrive = 0.4f, dsTone = 0.6f, dsMix = 0.f;
    bool dlSync = true, dlPing = true;
    float dlTime = 300.f;
    int dlDiv = 3;
    float dlFb = 0.35f, dlMix = 0.f;
    float rvSize = 0.5f, rvDamp = 0.5f, rvMix = 0.f;
};

struct KitSnapshot
{
    std::array<TrackSound, NUM_TRACKS> t {};
    int globalKit = 0;
    float glue = 0.3f, tape = 0.25f;
    int character = -1;                  // -1 = von Hand eingestellt
};

// Lautheit der eingebauten Klänge in dB (RMS der ersten 150 ms), für den Pegelausgleich
using KitLoudness = std::array<std::array<float, NUM_TRACKS>, NUM_KITS>;

inline const std::vector<std::string>& characterNames()
{
    static const std::vector<std::string> c { "Dry & Tight", "Big 80s Room", "Lo-Fi", "Industrial", "Clean Modern" };
    return c;
}

class KitDesigner
{
public:
    // Neues Kit. soundLocked[t] = true: Spur t bleibt unverändert.
    static KitSnapshot create (int genre, uint32_t seed, const std::array<bool, NUM_TRACKS>& soundLocked,
                               const KitSnapshot& current, const KitLoudness& loud);

    // Leichte Variation des aktuellen Kits
    static KitSnapshot mutate (uint32_t seed, const std::array<bool, NUM_TRACKS>& soundLocked,
                               const KitSnapshot& current, const KitLoudness& loud);

    static int resolvedKit (const KitSnapshot& k, int track)
    {
        const int s = k.t[(size_t) track].src;
        return s <= 0 ? k.globalKit : s - 1;
    }
};
} // namespace ad
