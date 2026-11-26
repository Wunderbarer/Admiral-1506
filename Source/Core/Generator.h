// Admiral 1506 - Pattern-Generator: Genre-Vorlagen + Wahrscheinlichkeiten + Zufall
#pragma once
#include <array>
#include <cstdint>
#include "Pattern.h"

namespace ad
{
struct GenSettings
{
    int genre = 0;
    float density = 0.5f;     // 0..1: wie viele der optionalen Schläge gespielt werden
    float variation = 0.35f;  // 0..1: zusätzliche Zufallsschläge, Ghost-Notes, Anschlags-Streuung
};

struct GenResult
{
    Pattern main, fill;
};

class Generator
{
public:
    // Erzeugt Haupt- und Fill-Takt. Gesperrte Spuren (locked[t] = true) werden aus "previous" übernommen.
    static GenResult generate (const GenSettings& s, uint32_t seed,
                               const std::array<bool, NUM_TRACKS>& locked, const GenResult& previous);

    // Nur den Fill-Takt neu erzeugen (aus dem Haupt-Takt abgeleitet)
    static Pattern makeFill (const Pattern& main, const GenSettings& s, uint32_t seed);

    // Empfohlenes Tempo der Genres (nur Anzeige / Export ohne Host)
    static int suggestedBpm (int genre);
};
} // namespace ad
