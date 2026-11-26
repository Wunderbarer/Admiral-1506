#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace ad
{
namespace
{
// Kleiner, deterministischer Zufallsgenerator (gleicher Seed -> gleiches Pattern)
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed)
    {
        // Seed gründlich durchmischen (SplitMix), sonst liefern kleine Seeds anfangs kleine Zahlen
        uint64_t z = (uint64_t) seed + 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        s = (uint32_t) (z ^ (z >> 32));
        if (s == 0) s = 0x9e3779b9u;
        for (int i = 0; i < 4; ++i) next();
    }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) / 16777216.f; }                // 0..1
    float range (float a, float b) { return a + (b - a) * uni(); }
    bool chance (float p) { return uni() < p; }
    int below (int n) { return (int) (next() % (uint32_t) std::max (1, n)); }
};

// Vorlagen-Zeichen:
//   X  fester Schlag, betont          x  fester Schlag, normal
//   1..9  optional mit 10..90 % Wahrscheinlichkeit (normaler Anschlag)
//   a..i  optionale Ghost-Note mit 10..90 % Wahrscheinlichkeit (leiser Anschlag)
//   .  kein Schlag (Variation kann hier zusätzliche Schläge setzen)
// Reihenfolge der Spuren: Kick, Snare, Clap, CL Hat, OP Hat, Low Tom, Hi Tom, Rim, Cowbell, Crash
using Tmpl = std::array<const char*, NUM_TRACKS>;

const Tmpl kTemplates[] = {
    // Synthwave (ca. 100 BPM): gerade, große Snare auf 2 und 4, Achtel-Hats
    { "X.....3.X.....4.", "....X.......X...", "....4.......4...", "x.6.x.6.x.6.x.6.", "..............3.",
      "................", "................", "........2.......", "................", "................" },
    // Techno (ca. 130 BPM): Four-on-the-floor, offene Hats auf den Offbeats
    { "X...X...X...X...", "................", "....x.......x...", ".5.5.5.5.5.5.5.5", "..x...x...x...x.",
      ".......2........", "................", "...3..2....3..2.", "................", "................" },
    // House (ca. 124 BPM)
    { "X...X...X...X...", "............2...", "....X.......X...", "4.4.4.4.4.4.4.4.", "..x...x...x...x.",
      "................", "................", "..........2....3", ".......2.......2", "................" },
    // Hip Hop / Boom Bap (ca. 90 BPM)
    { "X......3..X..2..", "....X..b....X.c.", "....2.......2...", "x.2.x.2.x.2.x.2.", "..............3.",
      "................", "................", "...2......2.....", "................", "................" },
    // Drum & Bass (ca. 174 BPM): Two-Step
    { "X.....2...X..3..", "....X..b....X..c", "................", "x.x2x.x.x.x2x.x.", "..........3.....",
      "................", "................", "..2.........2...", "................", "................" },
    // Electro (ca. 125 BPM): 808-Funk mit Cowbell
    { "X.....X...3.2...", "....X.......X...", "....X.......X...", "x5x5x5x5x5x5x5x5", "................",
      ".............3..", "..............3.", "......2......2..", "..3....3..3.....", "................" },
    // Pop / Rock (ca. 110 BPM)
    { "X.....3.X.2.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.", "..............2.",
      "................", "................", "................", "................", "................" },
};

// Zusätzliche Zufallsschläge (Variation) auf leeren Schritten - Grundwahrscheinlichkeit je Spur
constexpr float kVarBase[NUM_TRACKS] = { 0.06f, 0.07f, 0.02f, 0.22f, 0.04f, 0.02f, 0.02f, 0.10f, 0.03f, 0.f };
constexpr bool kVarGhost[NUM_TRACKS] = { true, true, false, false, false, false, false, true, false, false };

float applyDensity (float p, float d)
{
    if (d <= 0.5f) return p * (d / 0.5f);
    return p + (1.f - p) * (d - 0.5f) * 2.f * 0.7f;
}

struct Bar { std::array<std::array<float, NUM_STEPS>, NUM_TRACKS> v {}; };

void genBar (Bar& out, const GenSettings& s, Rng& r)
{
    const auto& tm = kTemplates[std::clamp (s.genre, 0, (int) (sizeof (kTemplates) / sizeof (kTemplates[0])) - 1)];
    const float d = std::clamp (s.density, 0.f, 1.f), v = std::clamp (s.variation, 0.f, 1.f);
    for (auto& row : out.v) row.fill (0.f);

    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        const char* row = tm[(size_t) t];
        for (int k = 0; k < NUM_STEPS; ++k)
        {
            const char c = row[k];
            float vel = 0.f;
            if (c == 'X') vel = 1.f;
            else if (c == 'x')
            {
                // bei sehr geringer Dichte fallen auch feste (unbetonte) Schläge weg
                if (! r.chance (std::max (0.f, 0.25f - d) * 2.5f)) vel = 0.8f;
            }
            else if (c >= '1' && c <= '9')
            {
                if (r.chance (applyDensity ((float) (c - '0') / 10.f, d))) vel = 0.72f;
            }
            else if (c >= 'a' && c <= 'i')
            {
                if (r.chance (applyDensity ((float) (c - 'a' + 1) / 10.f, d))) vel = 0.38f;
            }
            else
            {
                // leerer Schritt: Variation setzt gelegentlich zusätzliche Schläge
                float p = v * kVarBase[t];
                if (t == T_CHAT) p += std::max (0.f, d - 0.6f) * 0.8f;   // hohe Dichte: Sechzehntel-Hats
                if (p > 0.f && r.chance (p))
                    vel = kVarGhost[t] ? r.range (0.28f, 0.5f) : r.range (0.45f, 0.75f);
            }

            if (vel > 0.f && vel < 0.99f)
                vel = std::clamp (vel + r.range (-0.12f, 0.12f) * v, 0.15f, 0.95f);
            out.v[(size_t) t][(size_t) k] = vel;
        }
    }

    // Zwischen offener und geschlossener Hi-Hat: offene hat Vorrang
    for (int k = 0; k < NUM_STEPS; ++k)
        if (out.v[T_OHAT][(size_t) k] > 0.f) out.v[T_CHAT][(size_t) k] = 0.f;

    // Hohe Variation: vereinzelt einen Kick um eine Sechzehntel verschieben (nicht die "Eins")
    if (v > 0.6f && r.chance ((v - 0.6f) * 1.5f))
    {
        for (int tries = 0; tries < 6; ++tries)
        {
            const int k = 1 + r.below (NUM_STEPS - 2);
            auto& kr = out.v[T_KICK];
            if (kr[(size_t) k] > 0.f && kr[(size_t) k] < 0.99f && kr[(size_t) k + 1] <= 0.f)
            {
                kr[(size_t) k + 1] = kr[(size_t) k];
                kr[(size_t) k] = 0.f;
                break;
            }
        }
    }

    // Sicherheitsnetz: die Kick-Spur der Vorlage beginnt immer auf der Eins
    if (tm[T_KICK][0] == 'X') out.v[T_KICK][0] = 1.f;
}

// Takt 2 und 3: der Groove entwickelt sich leicht weiter (Zählzeiten und Backbeat bleiben stehen)
Bar varyBar (const Bar& a, const GenSettings& s, Rng& r, float strength)
{
    Bar b = a;
    const float v = std::clamp (s.variation, 0.f, 1.f) * strength;
    auto empty = [&] (int t, int k) { return b.v[(size_t) t][(size_t) k] <= 0.f; };

    // Kick: zusätzlicher synkopierter Schlag
    if (r.chance (0.2f + 0.45f * v))
    {
        static const int pos[] = { 3, 6, 7, 10, 11, 14 };
        for (int tries = 0; tries < 6; ++tries)
        {
            const int k = pos[r.below (6)];
            if (empty (T_KICK, k)) { b.v[T_KICK][(size_t) k] = r.range (0.6f, 0.8f); break; }
        }
    }
    // Kick: einen optionalen (nicht betonten) Schlag weglassen
    if (r.chance (0.15f * v))
        for (int k = 1; k < NUM_STEPS; ++k)
            if (b.v[T_KICK][(size_t) k] > 0.f && b.v[T_KICK][(size_t) k] < 0.99f && r.chance (0.5f)) { b.v[T_KICK][(size_t) k] = 0.f; break; }
    // Snare: Ghost-Note
    if (r.chance (0.25f + 0.45f * v))
    {
        static const int pos[] = { 1, 3, 6, 7, 9, 11, 14, 15 };
        for (int tries = 0; tries < 6; ++tries)
        {
            const int k = pos[r.below (8)];
            if (empty (T_SNARE, k)) { b.v[T_SNARE][(size_t) k] = r.range (0.3f, 0.45f); break; }
        }
    }
    // Hi-Hat: ein, zwei Sechzehntel dazu oder offene Hat als "Luftholer"
    if (r.chance (0.3f * v + 0.1f))
        for (int i = 0, added = 0; i < 16 && added < 2; ++i)
        {
            const int k = r.below (NUM_STEPS);
            if ((k & 1) && empty (T_CHAT, k) && empty (T_OHAT, k)) { b.v[T_CHAT][(size_t) k] = r.range (0.35f, 0.55f); ++added; }
        }
    if (r.chance (0.2f + 0.3f * v))
    {
        const int k = r.chance (0.6f) ? 14 : 6;
        if (empty (T_OHAT, k)) { b.v[T_OHAT][(size_t) k] = r.range (0.6f, 0.8f); b.v[T_CHAT][(size_t) k] = 0.f; }
    }
    // Percussion: einen Schlag um eine Sechzehntel verschieben
    for (int t : { (int) T_RIM, (int) T_COWBELL, (int) T_CLAP })
        if (r.chance (0.2f * v))
            for (int tries = 0; tries < 8; ++tries)
            {
                const int k = 1 + r.below (NUM_STEPS - 2);
                if (t == T_CLAP && (k == 4 || k == 12)) continue;     // Backbeat bleibt
                auto& row = b.v[(size_t) t];
                if (row[(size_t) k] > 0.f && row[(size_t) k + 1] <= 0.f) { row[(size_t) k + 1] = row[(size_t) k]; row[(size_t) k] = 0.f; break; }
            }
    return b;
}

int barDiff (const Bar& a, const Bar& b)
{
    int n = 0;
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int k = 0; k < NUM_STEPS; ++k)
            if (std::abs (a.v[(size_t) t][(size_t) k] - b.v[(size_t) t][(size_t) k]) > 1.0e-4f) ++n;
    return n;
}

// Zusätzliche, deutlich hörbare Veränderungen für Takt 2 und 3 (Zählzeit 1 und Backbeat bleiben stehen)
void extraMove (Bar& b, Rng& r, float v, bool lateBar)
{
    auto empty = [&] (int t, int k) { return b.v[(size_t) t][(size_t) k] <= 0.f; };
    const int kinds = lateBar && v > 0.3f ? 8 : 7;
    switch (r.below (kinds))
    {
        case 0: // Hi-Hat-Rhythmus in einer Takthälfte ändern: Achtel <-> Sechzehntel
        {
            const int h = r.chance (0.6f) ? 8 : 0;
            int odd = 0;
            for (int k = h + 1; k < h + 8; k += 2) if (! empty (T_CHAT, k)) ++odd;
            for (int k = h + 1; k < h + 8; k += 2)
            {
                if (odd >= 2) b.v[T_CHAT][(size_t) k] = 0.f;
                else if (empty (T_OHAT, k)) b.v[T_CHAT][(size_t) k] = r.range (0.38f, 0.5f);
            }
            break;
        }
        case 1: // Kick-Auftakt am Taktende
            b.v[T_KICK][15] = r.range (0.65f, 0.8f);
            if (r.chance (0.35f)) b.v[T_KICK][14] = r.range (0.6f, 0.75f);
            break;
        case 2: // Snare-Drag vor dem Backbeat
            for (int k : { 3, 11 })
                if (empty (T_SNARE, k) && r.chance (0.7f)) b.v[T_SNARE][(size_t) k] = r.range (0.3f, 0.42f);
            break;
        case 3: // Percussion antwortet (Rim oder Cowbell auf einem Offbeat)
        {
            const int t = r.chance (0.6f) ? T_RIM : T_COWBELL;
            static const int pos[] = { 2, 6, 7, 10, 13, 14 };
            const int k = pos[r.below (6)];
            b.v[(size_t) t][(size_t) k] = empty (t, k) ? r.range (0.5f, 0.7f) : 0.f;
            break;
        }
        case 4: // Clap / Ghost-Snare am Taktende
            if (r.chance (0.5f)) b.v[T_CLAP][15] = empty (T_CLAP, 15) ? r.range (0.5f, 0.65f) : 0.f;
            else if (empty (T_SNARE, 14)) b.v[T_SNARE][14] = r.range (0.35f, 0.45f);
            break;
        case 5: // offene Hi-Hat auf einem Offbeat
        {
            static const int pos[] = { 2, 6, 10, 14 };
            const int k = pos[r.below (4)];
            b.v[T_OHAT][(size_t) k] = empty (T_OHAT, k) ? r.range (0.6f, 0.8f) : 0.f;
            b.v[T_CHAT][(size_t) k] = 0.f;
            break;
        }
        case 6: // Kick verschieben oder ergänzen (nicht die Eins)
        {
            bool moved = false;
            for (int k = 2; k < NUM_STEPS - 1 && ! moved; ++k)
            {
                auto& kr = b.v[T_KICK];
                if (kr[(size_t) k] > 0.f && kr[(size_t) k] < 0.99f && kr[(size_t) k + 1] <= 0.f && r.chance (0.4f))
                { kr[(size_t) k + 1] = kr[(size_t) k]; kr[(size_t) k] = 0.f; moved = true; }
            }
            if (! moved)
            {
                const int k = r.chance (0.5f) ? 10 : 11;
                if (empty (T_KICK, k)) b.v[T_KICK][(size_t) k] = r.range (0.65f, 0.8f);
            }
            break;
        }
        default: // leiser Tom-Akzent (nur im dritten Takt)
            if (r.chance (0.5f)) b.v[T_HTOM][13] = r.range (0.5f, 0.65f);
            else b.v[T_LTOM][15] = r.range (0.55f, 0.7f);
            break;
    }
}

// Takt entwickeln, bis er sich hörbar (mindestens minDiff Schritte) vom Ausgangstakt unterscheidet
Bar developBar (const Bar& from, const Bar& ref, const GenSettings& s, Rng& r, float strength, int minDiff, bool lateBar)
{
    Bar b = varyBar (from, s, r, strength);
    const float v = std::clamp (s.variation, 0.f, 1.f);
    for (int tries = 0; tries < 24 && barDiff (b, ref) < minDiff; ++tries)
        extraMove (b, r, v, lateBar);
    return b;
}

// Kleine Variation im letzten Viertel ("hin und wieder"): wird als Ending-Ebene abgelegt,
// nur Schritte, die sich vom Groove unterscheiden. Index 8..11 = die letzten vier Schritte jeder Länge.
void makeEnding (Pattern& p, const GenSettings& s, Rng& r)
{
    p.clearEnding();
    const float v = std::clamp (s.variation, 0.f, 1.f);
    if (! r.chance (0.35f + 0.45f * v)) return;          // nicht jedes Pattern bekommt eine Variation

    std::array<std::array<float, END_STEPS>, NUM_TRACKS> e {};
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int i = 0; i < END_STEPS; ++i) e[(size_t) t][(size_t) i] = p.body[(size_t) t][(size_t) (MAX_STEPS - END_STEPS + i)];

    const int kinds = r.chance (0.4f * v) ? 2 : 1;
    for (int n = 0; n < kinds; ++n)
    {
        switch (r.below (v > 0.5f ? 6 : 5))
        {
            case 0: // Snare-Auftakt mit Ghost-Notes
                if (e[T_SNARE][10] <= 0.f) e[T_SNARE][10] = r.range (0.35f, 0.45f);
                if (e[T_SNARE][11] <= 0.f) e[T_SNARE][11] = r.range (0.5f, 0.65f);
                break;
            case 1: // Kick schiebt an
                e[T_KICK][r.chance (0.5f) ? 10 : 11] = r.range (0.7f, 0.85f);
                break;
            case 2: // offene Hi-Hat zum Abschluss
                e[T_OHAT][10] = r.range (0.65f, 0.85f);
                e[T_CHAT][10] = 0.f; e[T_CHAT][11] = 0.f;
                break;
            case 3: // kurzer Stopp, dann Clap
                for (int i = 9; i < 12; ++i) { e[T_KICK][(size_t) i] = 0.f; e[T_CHAT][(size_t) i] = 0.f; e[T_OHAT][(size_t) i] = 0.f; }
                e[T_CLAP][11] = r.range (0.8f, 0.95f);
                break;
            case 4: // Tom-Auftakt
                e[T_HTOM][10] = r.range (0.65f, 0.8f);
                e[T_LTOM][11] = r.range (0.7f, 0.85f);
                e[T_CHAT][10] = 0.f; e[T_CHAT][11] = 0.f;
                break;
            default: // Hats verdoppeln (Sechzehntel) über die letzten acht Schritte
                for (int i = 4; i < 12; ++i)
                    if (e[T_OHAT][(size_t) i] <= 0.f) e[T_CHAT][(size_t) i] = (i & 1) ? r.range (0.4f, 0.5f) : r.range (0.65f, 0.8f);
                break;
        }
    }
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int i = 0; i < END_STEPS; ++i)
            if (std::abs (e[(size_t) t][(size_t) i] - p.body[(size_t) t][(size_t) (MAX_STEPS - END_STEPS + i)]) > 1.0e-4f)
                p.ending[(size_t) t][(size_t) i] = e[(size_t) t][(size_t) i];
}
} // namespace

void Generator::makeFill (Pattern& p, const GenSettings& s, uint32_t seed)
{
    Rng r (seed * 2654435761u + 17u);
    p.clearFill();
    const float v = std::clamp (s.variation, 0.f, 1.f), d = std::clamp (s.density, 0.f, 1.f);
    int w = 4;
    if (v > 0.4f && r.chance (0.5f)) w = 8;
    if (v > 0.7f && r.chance (0.3f)) w = 12;
    const int start = END_STEPS - w;                        // Fenster rechtsbündig in der Fill-Ebene

    // Ausgangspunkt: Ende des Grooves inklusive kleiner Variation
    auto base = [&] (int t, int i)
    {
        const float e = p.ending[(size_t) t][(size_t) i];
        return e >= 0.f ? e : p.body[(size_t) t][(size_t) (MAX_STEPS - END_STEPS + i)];
    };
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int i = start; i < END_STEPS; ++i) p.fillEnd[(size_t) t][(size_t) i] = base (t, i);

    // Im Fill-Bereich werden Becken, Clap und Percussion ausgedünnt
    for (int t : { (int) T_CHAT, (int) T_OHAT, (int) T_CLAP, (int) T_RIM, (int) T_COWBELL, (int) T_SNARE, (int) T_LTOM, (int) T_HTOM })
        for (int i = start; i < END_STEPS; ++i) p.fillEnd[(size_t) t][(size_t) i] = 0.f;
    for (int i = start + 1; i < END_STEPS; ++i) p.fillEnd[T_KICK][(size_t) i] = 0.f;

    auto ramp = [&] (int i) { return 0.45f + 0.55f * (float) (i - start + 1) / (float) w; };
    const bool sixteenths = d > 0.35f;
    const int type = r.below (4);
    for (int i = start; i < END_STEPS; ++i)
    {
        const int j = i - start;
        const bool on8 = (j % 2) == 0;
        if (! sixteenths && ! on8) continue;
        const float vel = std::min (1.f, ramp (i) + r.range (-0.05f, 0.05f));
        switch (type)
        {
            case 0: p.fillEnd[T_SNARE][(size_t) i] = vel; break;                                     // Snare-Wirbel
            case 1: p.fillEnd[j < w / 2 ? T_HTOM : T_LTOM][(size_t) i] = vel; break;                  // Toms abwärts
            case 2:                                                                                    // Snare / Toms im Wechsel
            {
                const int seq[3] = { T_SNARE, T_HTOM, T_LTOM };
                p.fillEnd[(size_t) seq[(j / (sixteenths ? 1 : 2)) % 3]][(size_t) i] = vel;
                break;
            }
            default:                                                                                   // Snare auf Achteln, Toms dazwischen
                if (on8) p.fillEnd[T_SNARE][(size_t) i] = vel;
                else     p.fillEnd[j < w / 2 ? T_HTOM : T_LTOM][(size_t) i] = vel * 0.9f;
                break;
        }
    }
    p.fillEnd[T_SNARE][END_STEPS - 1] = std::max (p.fillEnd[T_SNARE][END_STEPS - 1], 0.95f);
    if (r.chance (0.5f)) p.fillEnd[T_KICK][END_STEPS - 1] = 0.9f;
}

Pattern Generator::generate (const GenSettings& s, uint32_t seed,
                             const std::array<bool, NUM_TRACKS>& locked, const Pattern& previous)
{
    Rng r (seed);
    Bar a;
    genBar (a, s, r);
    // Takt 2 und 3 entwickeln sich hörbar weiter: je mehr VARIATION, desto mehr Unterschiede
    const float v = std::clamp (s.variation, 0.f, 1.f);
    const int min2 = 2 + (int) std::lround (3.f * v);
    const int min3 = 3 + (int) std::lround (5.f * v);
    const Bar b2 = developBar (a, a, s, r, 0.6f, min2, false);
    const Bar b3 = developBar (r.chance (0.5f) ? b2 : a, a, s, r, 1.f, min3, true);

    Pattern p;
    const Bar* bars[3] = { &a, &b2, &b3 };
    for (int bar = 0; bar < 3; ++bar)
        for (int t = 0; t < NUM_TRACKS; ++t)
            for (int k = 0; k < NUM_STEPS; ++k)
                p.body[(size_t) t][(size_t) (bar * NUM_STEPS + k)] = bars[bar]->v[(size_t) t][(size_t) k];

    makeEnding (p, s, r);
    makeFill (p, s, seed);

    for (int t = 0; t < NUM_TRACKS; ++t)
        if (locked[(size_t) t]) p.copyTrack (previous, t);
    return p;
}

int Generator::suggestedBpm (int genre)
{
    static const int b[] = { 100, 130, 124, 90, 174, 125, 110 };
    return b[std::clamp (genre, 0, 6)];
}
} // namespace ad
