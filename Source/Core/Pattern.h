// Admiral 1506 - Pattern (10 Spuren x bis zu 48 Schritte) und Ablauflogik
//
// Aufbau:
//   body      - der Groove, 48 Schritte (3 Takte, die sich leicht weiterentwickeln)
//   ending    - kleine Variation am Ende jedes Durchlaufs ("letztes Viertel")
//   fillEnd   - großer Fill, nur in jedem n-ten Durchlauf
// Die beiden Variations-Ebenen haben 12 Schritte und sind RECHTSBÜNDIG: Wird das Pattern auf 32, 24 oder 16
// Schritte gekürzt, wandert die Variation mit ans neue Ende. Ein Wert < 0 bedeutet "keine Änderung" (body gilt).
// Kürzen ist nicht zerstörend: Die 48 Schritte bleiben gespeichert.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "Params.h"

namespace ad
{
struct Pattern
{
    std::array<std::array<float, MAX_STEPS>, NUM_TRACKS> body {};
    std::array<std::array<float, END_STEPS>, NUM_TRACKS> ending {}, fillEnd {};

    Pattern() { clear(); }
    void clear()
    {
        for (auto& r : body) r.fill (0.f);
        clearEnding();
        clearFill();
    }
    void clearEnding() { for (auto& r : ending) r.fill (-1.f); }
    void clearFill()   { for (auto& r : fillEnd) r.fill (-1.f); }

    // Variations-Zonen je Länge: Ending = letztes Viertel, Fill = letzte Hälfte (höchstens 12 Schritte)
    static int endZone (int len)  { return std::max (1, len / 4); }
    static int fillZone (int len) { return std::min (END_STEPS, len / 2); }
    static int zoneFor (int len, bool fill) { return fill ? fillZone (len) : endZone (len); }

    // Index in der Variations-Ebene für Schritt k bei Länge len, oder -1 außerhalb der Zone
    static int layerIndex (int k, int len, int zone)
    {
        const int start = len - zone;
        if (k < start || k >= len) return -1;
        return END_STEPS - zone + (k - start);
    }

    // Wirksamer Anschlag von Spur t in Schritt k. fill = Fill-Durchlauf.
    float value (int t, int k, int len, bool fill) const
    {
        if (k < 0 || k >= len) return 0.f;
        if (fill)
        {
            const int i = layerIndex (k, len, fillZone (len));
            if (i >= 0 && fillEnd[(size_t) t][(size_t) i] >= 0.f) return fillEnd[(size_t) t][(size_t) i];
        }
        const int j = layerIndex (k, len, endZone (len));
        if (j >= 0 && ending[(size_t) t][(size_t) j] >= 0.f) return ending[(size_t) t][(size_t) j];
        return body[(size_t) t][(size_t) k];
    }

    bool isVaried (int t, int k, int len, bool fill) const   // weicht der Schritt vom Groove ab?
    {
        if (fill)
        {
            const int i = layerIndex (k, len, fillZone (len));
            if (i >= 0 && fillEnd[(size_t) t][(size_t) i] >= 0.f) return true;
        }
        const int j = layerIndex (k, len, endZone (len));
        return j >= 0 && ending[(size_t) t][(size_t) j] >= 0.f;
    }

    int count (int len, bool fill = false) const
    {
        int n = 0;
        for (int t = 0; t < NUM_TRACKS; ++t)
            for (int k = 0; k < len; ++k) if (value (t, k, len, fill) > 0.f) ++n;
        return n;
    }

    void copyTrack (const Pattern& o, int t)
    {
        body[(size_t) t] = o.body[(size_t) t];
        ending[(size_t) t] = o.ending[(size_t) t];
        fillEnd[(size_t) t] = o.fillEnd[(size_t) t];
    }
};

// Durchlauf c ist ein Fill-Durchlauf, wenn er der letzte eines Blocks von "every" Durchläufen ist
inline bool isFillCycle (long long c, int every)     { return every > 0 && c >= 0 && (c % every) == every - 1; }
inline bool crashAfterFill (long long c, int every)  { return every > 0 && c > 0 && (c % every) == 0; }

// Zeitpunkt der globalen Sechzehntel n in Viertelnoten, mit Swing auf jeder zweiten Sechzehntel
inline double stepTimePpq (long long n, float swing)
{
    double p = (double) n * 0.25;
    if ((n & 1) != 0) p += 0.125 * (double) swing;
    return p;
}
} // namespace ad
