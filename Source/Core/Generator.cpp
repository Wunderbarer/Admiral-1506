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

void genMain (Pattern& out, const GenSettings& s, Rng& r)
{
    const auto& tm = kTemplates[std::clamp (s.genre, 0, (int) (sizeof (kTemplates) / sizeof (kTemplates[0])) - 1)];
    const float d = std::clamp (s.density, 0.f, 1.f), v = std::clamp (s.variation, 0.f, 1.f);
    out.clear();

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
} // namespace

Pattern Generator::makeFill (const Pattern& main, const GenSettings& s, uint32_t seed)
{
    Rng r (seed * 2654435761u + 17u);
    Pattern f = main;
    const float v = std::clamp (s.variation, 0.f, 1.f), d = std::clamp (s.density, 0.f, 1.f);
    const int w = (v > 0.4f && r.chance (0.5f)) ? 8 : 4;
    const int start = NUM_STEPS - w;

    // Im Fill-Bereich werden Becken, Clap und Percussion ausgedünnt
    for (int t : { (int) T_CHAT, (int) T_OHAT, (int) T_CLAP, (int) T_RIM, (int) T_COWBELL, (int) T_SNARE, (int) T_LTOM, (int) T_HTOM })
        for (int k = start; k < NUM_STEPS; ++k) f.v[(size_t) t][(size_t) k] = 0.f;
    for (int k = start + 1; k < NUM_STEPS; ++k) f.v[T_KICK][(size_t) k] = 0.f;

    auto ramp = [&] (int k) { return 0.45f + 0.55f * (float) (k - start + 1) / (float) w; };
    const bool sixteenths = d > 0.35f;
    const int type = r.below (4);

    for (int k = start; k < NUM_STEPS; ++k)
    {
        const int i = k - start;
        const bool on8 = (i % 2) == 0;
        if (! sixteenths && ! on8) continue;
        const float vel = std::min (1.f, ramp (k) + r.range (-0.05f, 0.05f));
        switch (type)
        {
            case 0: // Snare-Wirbel
                f.v[T_SNARE][(size_t) k] = vel;
                break;
            case 1: // Toms abwärts
                f.v[i < w / 2 ? T_HTOM : T_LTOM][(size_t) k] = vel;
                break;
            case 2: // Snare / Hi Tom / Low Tom im Wechsel
            {
                const int seq[3] = { T_SNARE, T_HTOM, T_LTOM };
                f.v[(size_t) seq[(i / (sixteenths ? 1 : 2)) % 3]][(size_t) k] = vel;
                break;
            }
            default: // Snare auf den Achteln, Toms dazwischen
                if (on8) f.v[T_SNARE][(size_t) k] = vel;
                else     f.v[i < w / 2 ? T_HTOM : T_LTOM][(size_t) k] = vel * 0.9f;
                break;
        }
    }
    // Abschluss: betonter Schlag auf der letzten Sechzehntel
    f.v[T_SNARE][NUM_STEPS - 1] = std::max (f.v[T_SNARE][NUM_STEPS - 1], 0.95f);
    if (r.chance (0.5f)) f.v[T_KICK][NUM_STEPS - 1] = 0.9f;
    return f;
}

GenResult Generator::generate (const GenSettings& s, uint32_t seed,
                               const std::array<bool, NUM_TRACKS>& locked, const GenResult& previous)
{
    Rng r (seed);
    GenResult g;
    genMain (g.main, s, r);
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (locked[(size_t) t]) g.main.v[(size_t) t] = previous.main.v[(size_t) t];

    g.fill = makeFill (g.main, s, seed);
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (locked[(size_t) t]) g.fill.v[(size_t) t] = previous.fill.v[(size_t) t];
    return g;
}

int Generator::suggestedBpm (int genre)
{
    static const int b[] = { 100, 130, 124, 90, 174, 125, 110 };
    return b[std::clamp (genre, 0, 6)];
}
} // namespace ad
