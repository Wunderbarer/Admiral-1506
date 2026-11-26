// Admiral 1506 - Pattern (10 Spuren x 16 Schritte) und Ablauflogik (Haupt-Pattern / Fill / Crash)
#pragma once
#include <array>
#include <cmath>
#include "Params.h"

namespace ad
{
// Ein Takt: Anschlagstärke je Spur und Schritt (0 = aus, sonst 0..1).
struct Pattern
{
    std::array<std::array<float, NUM_STEPS>, NUM_TRACKS> v {};

    void clear() { for (auto& r : v) r.fill (0.f); }
    bool trackEmpty (int t) const { for (float x : v[(size_t) t]) if (x > 0.f) return false; return true; }
    int count() const { int n = 0; for (auto& r : v) for (float x : r) if (x > 0.f) ++n; return n; }
};

// Welches Pattern spielt in Takt "bar" (0-basiert)? Bei aktivem Fill ist der jeweils letzte
// Takt eines Blocks von fillEvery Takten der Fill-Takt.
inline bool isFillBar (long long bar, int fillEvery)
{
    if (fillEvery <= 0 || bar < 0) return false;
    return (bar % fillEvery) == fillEvery - 1;
}

// Nach einem Fill-Takt setzt auf Schritt 1 ein Crash ein.
inline bool crashAfterFill (long long bar, int fillEvery)
{
    return fillEvery > 0 && bar > 0 && (bar % fillEvery) == 0;
}

// Position eines Schritts innerhalb eines Takts in Viertelnoten (PPQ), mit Swing:
// jede zweite Sechzehntel wird um bis zu einer halben Sechzehntel nach hinten verschoben.
inline double stepOffsetPpq (int step, float swing)
{
    double p = step * 0.25;
    if ((step & 1) != 0) p += 0.125 * (double) swing;
    return p;
}
} // namespace ad
