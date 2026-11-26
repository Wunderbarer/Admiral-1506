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

class Generator
{
public:
    // Erzeugt ein Pattern mit 48 Schritten (3 Takte mit leichter Entwicklung), gelegentlich einer kleinen
    // Variation im letzten Viertel und einem Fill. Gesperrte Spuren (locked[t]) werden aus "previous" übernommen.
    static Pattern generate (const GenSettings& s, uint32_t seed,
                             const std::array<bool, NUM_TRACKS>& locked, const Pattern& previous);

    // Nur die Fill-Ebene neu erzeugen (aus dem Ende des Grooves abgeleitet)
    static void makeFill (Pattern& p, const GenSettings& s, uint32_t seed);

    // Empfohlenes Tempo der Genres (nur Anzeige / Export ohne Host)
    static int suggestedBpm (int genre);
};
} // namespace ad
