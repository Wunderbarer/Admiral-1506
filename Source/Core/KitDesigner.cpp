#include "KitDesigner.h"
#include <algorithm>
#include <cmath>

namespace ad
{
namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed)
    {
        uint64_t z = (uint64_t) seed + 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        s = (uint32_t) (z ^ (z >> 32));
        if (s == 0) s = 0x9e3779b9u;
        for (int i = 0; i < 4; ++i) next();
    }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) / 16777216.f; }
    float range (float a, float b) { return a + (b - a) * uni(); }
    bool chance (float p) { return uni() < p; }
    float sign() { return chance (0.5f) ? 1.f : -1.f; }
    template <size_t N> int pick (const std::array<float, N>& w)
    {
        float sum = 0.f;
        for (float x : w) sum += x;
        float r = uni() * sum;
        for (size_t i = 0; i < N; ++i) { r -= w[i]; if (r <= 0.f) return (int) i; }
        return (int) N - 1;
    }
    float logRange (float a, float b) { return a * std::pow (b / a, uni()); }   // gleichmäßig auf log. Skala (Frequenzen)
};

enum Character { DRY, BIG80S, LOFI, INDUSTRIAL, CLEAN, NUM_CHAR };

// Wahrscheinlichkeit der Charaktere je Genre (Synthwave, Techno, House, Hip Hop, DnB, Electro, Pop/Rock)
const std::array<std::array<float, NUM_CHAR>, 7> kCharByGenre { {
    { 0.20f, 0.50f, 0.15f, 0.00f, 0.15f },
    { 0.25f, 0.00f, 0.10f, 0.35f, 0.30f },
    { 0.30f, 0.10f, 0.20f, 0.00f, 0.40f },
    { 0.30f, 0.15f, 0.45f, 0.00f, 0.10f },
    { 0.25f, 0.00f, 0.20f, 0.20f, 0.35f },
    { 0.35f, 0.20f, 0.00f, 0.25f, 0.20f },
    { 0.20f, 0.45f, 0.00f, 0.00f, 0.35f },
} };

// Neigung zu den Kits (Analog, Punch, Linn) je Genre ...
const std::array<std::array<float, NUM_KITS>, 7> kKitByGenre { {
    { 0.35f, 0.15f, 0.50f }, { 0.30f, 0.60f, 0.10f }, { 0.35f, 0.45f, 0.20f }, { 0.30f, 0.20f, 0.50f },
    { 0.30f, 0.50f, 0.20f }, { 0.70f, 0.15f, 0.15f }, { 0.20f, 0.30f, 0.50f },
} };
// ... und je Charakter (wird mit der Genre-Neigung multipliziert)
const std::array<std::array<float, NUM_KITS>, NUM_CHAR> kKitByChar { {
    { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.5f, 1.6f }, { 1.0f, 0.6f, 1.5f }, { 0.6f, 1.8f, 0.6f }, { 1.0f, 1.4f, 0.7f },
} };

// Stimmbereiche je Instrument (Halbtöne)
constexpr float kTuneLo[NUM_TRACKS] = { -3.f, -2.f, -2.f, -2.f, -2.f, -3.f, -3.f, -2.f, -1.f, -2.f };
constexpr float kTuneHi[NUM_TRACKS] = {  2.f,  2.f,  2.f,  3.f,  3.f,  3.f,  3.f,  3.f,  1.f,  1.f };
constexpr float kDecayLo[NUM_TRACKS] = { 0.55f, 0.50f, 0.60f, 0.30f, 0.50f, 0.60f, 0.60f, 0.50f, 0.40f, 0.70f };

float levelFor (const TrackSound& s, int track, int kit, const KitLoudness& loud, float jitterDb)
{
    const float base = tracks()[(size_t) track].level;
    float db = loud[0][(size_t) track] - loud[(size_t) kit][(size_t) track] + jitterDb;   // Bezug: Kit ANALOG
    db -= s.dsMix * s.dsDrive * 4.f;          // Distortion verdichtet: etwas leiser
    db -= s.rvMix * 3.f + s.dlMix * 2.f;      // Hall/Delay addieren Energie
    db = std::clamp (db, -9.f, 9.f);
    const float gain = base * base * std::pow (10.f, db / 20.f);
    return std::clamp (std::sqrt (gain), 0.15f, 1.f);
}

void designTrack (TrackSound& s, int t, int ch, Rng& r)
{
    const bool hat = t == T_CHAT || t == T_OHAT, tom = t == T_LTOM || t == T_HTOM;
    const bool perc = t == T_RIM || t == T_COWBELL, snareLike = t == T_SNARE || t == T_CLAP;

    // --- Grundregeln ---
    s.tune = std::round (r.range (kTuneLo[t], kTuneHi[t]) * 2.f) / 2.f;      // halbe Halbtöne
    s.decay = r.range (kDecayLo[t], 1.f);
    if (s.decay > 0.93f) s.decay = 1.f;
    s.res = 0.f; s.lp = 20000.f;
    s.hp = t == T_KICK ? r.range (20.f, 35.f) : hat ? r.logRange (60.f, 500.f) : snareLike ? r.logRange (20.f, 150.f) : r.logRange (20.f, 80.f);
    s.dsMix = 0.f; s.dsType = 0; s.dsDrive = r.range (0.2f, 0.6f); s.dsTone = r.range (0.4f, 0.8f);
    s.dlMix = 0.f; s.dlSync = true; s.dlPing = true; s.dlFb = r.range (0.2f, 0.45f);
    { static const int divs[] = { 3, 2, 0, 5, 1 }; s.dlDiv = divs[r.next() % 5]; }
    s.rvMix = 0.f; s.rvSize = r.range (0.35f, 0.7f); s.rvDamp = r.range (0.3f, 0.7f);

    // Panorama: Kick mittig, Snare fast mittig, Rest verteilt
    switch (t)
    {
        case T_KICK:  s.pan = 0.f; break;
        case T_SNARE: s.pan = r.range (-0.05f, 0.05f); break;
        case T_CLAP:  s.pan = r.range (-0.15f, 0.15f); break;
        case T_CRASH: s.pan = r.range (-0.35f, 0.35f); break;
        default:      s.pan = r.sign() * r.range (0.1f, 0.35f); break;
    }

    // --- Charakter ---
    switch (ch)
    {
        case DRY:
            s.decay = std::max (0.25f, s.decay * r.range (0.6f, 0.85f));
            if (t == T_SNARE && r.chance (0.5f)) { s.rvMix = r.range (0.03f, 0.08f); s.rvSize = r.range (0.2f, 0.4f); }
            if (hat) s.hp = r.logRange (200.f, 600.f);
            break;
        case BIG80S:
            if (snareLike)     { s.rvMix = r.range (0.25f, 0.45f); s.rvSize = r.range (0.6f, 0.85f); s.rvDamp = r.range (0.4f, 0.7f); }
            else if (tom)      { s.rvMix = r.range (0.18f, 0.32f); s.rvSize = r.range (0.55f, 0.8f); }
            else if (t == T_CRASH) s.rvMix = r.range (0.1f, 0.2f);
            else if (hat || perc) s.rvMix = r.chance (0.5f) ? r.range (0.05f, 0.12f) : 0.f;
            if (t == T_SNARE && r.chance (0.35f)) { s.dlMix = r.range (0.08f, 0.15f); s.dlDiv = 3; }
            break;
        case LOFI:
            s.lp = r.logRange (3000.f, 8000.f);
            if (t == T_KICK) s.lp = r.logRange (2500.f, 6000.f);
            s.tune = std::min (s.tune, 0.f) - (r.chance (0.5f) ? 1.f : 0.f);
            if ((t == T_KICK || t == T_SNARE) && r.chance (0.7f)) { s.dsType = 0; s.dsMix = r.range (0.2f, 0.5f); s.dsDrive = r.range (0.3f, 0.6f); s.dsTone = r.range (0.3f, 0.55f); }
            if (snareLike && r.chance (0.5f)) { s.rvMix = r.range (0.08f, 0.18f); s.rvSize = r.range (0.3f, 0.5f); s.rvDamp = r.range (0.6f, 0.9f); }
            if ((hat || perc) && r.chance (0.25f)) s.res = r.range (0.1f, 0.3f);
            break;
        case INDUSTRIAL:
            s.tune = std::min (s.tune, 1.f) - (r.chance (0.4f) ? 1.5f : 0.f);
            if (t == T_KICK || snareLike || tom)
            {
                s.dsType = r.chance (0.6f) ? 1 : 2;
                s.dsMix = r.range (0.35f, 0.8f); s.dsDrive = r.range (0.4f, 0.85f); s.dsTone = r.range (0.45f, 0.8f);
            }
            if (snareLike) { s.rvMix = r.range (0.1f, 0.25f); s.rvSize = r.range (0.25f, 0.5f); }
            if (perc || t == T_CLAP) { if (r.chance (0.6f)) { s.dlMix = r.range (0.15f, 0.3f); s.dlFb = r.range (0.3f, 0.55f); } }
            if (hat && r.chance (0.3f)) { s.res = r.range (0.15f, 0.4f); s.lp = r.logRange (6000.f, 12000.f); }
            break;
        default: // CLEAN
            if (hat) { s.hp = r.logRange (300.f, 700.f); s.pan = std::copysign (r.range (0.2f, 0.4f), s.pan); }
            if (t == T_SNARE) { s.rvMix = r.range (0.08f, 0.18f); s.rvSize = r.range (0.4f, 0.6f); }
            if (t == T_CLAP && r.chance (0.5f)) { s.dlMix = r.range (0.08f, 0.18f); s.dlDiv = 2; }
            if (t == T_KICK) s.tune = std::max (s.tune, -1.f);
            break;
    }
    if (t == T_KICK) s.dlMix = 0.f;            // Kick nie mit Echo
}

void mutateTrack (TrackSound& s, int t, Rng& r)
{
    auto nudge = [&] (float& v, float d, float lo, float hi) { v = std::clamp (v + r.range (-d, d), lo, hi); };
    nudge (s.tune, 0.5f, -12.f, 12.f);
    s.tune = std::round (s.tune * 10.f) / 10.f;
    if (s.decay < 0.999f || r.chance (0.3f)) { nudge (s.decay, 0.08f, kDecayLo[t] * 0.6f, 1.f); if (s.decay > 0.95f) s.decay = 1.f; }
    if (t != T_KICK) nudge (s.pan, 0.08f, -1.f, 1.f);
    if (s.hp > 21.f) s.hp = std::clamp (s.hp * r.range (0.8f, 1.25f), 20.f, 2000.f);
    if (s.lp < 19900.f) s.lp = std::clamp (s.lp * r.range (0.8f, 1.25f), 200.f, 20000.f);
    if (s.res > 0.f) nudge (s.res, 0.05f, 0.f, 1.f);
    if (s.dsMix > 0.f) { nudge (s.dsMix, 0.06f, 0.f, 1.f); nudge (s.dsDrive, 0.06f, 0.f, 1.f); }
    if (s.dlMix > 0.f) { nudge (s.dlMix, 0.04f, 0.f, 1.f); nudge (s.dlFb, 0.05f, 0.f, 0.85f); }
    if (s.rvMix > 0.f) { nudge (s.rvMix, 0.05f, 0.f, 1.f); nudge (s.rvSize, 0.06f, 0.f, 1.f); }
}
} // namespace

KitSnapshot KitDesigner::create (int genre, uint32_t seed, const std::array<bool, NUM_TRACKS>& locked,
                                 const KitSnapshot& cur, const KitLoudness& loud)
{
    Rng r (seed);
    const int g = std::clamp (genre, 0, 6);
    KitSnapshot k = cur;
    const int ch = r.pick (kCharByGenre[(size_t) g]);
    k.character = ch;

    // Kit-Neigung: Genre x Charakter
    std::array<float, NUM_KITS> w {};
    for (int i = 0; i < NUM_KITS; ++i) w[(size_t) i] = kKitByGenre[(size_t) g][(size_t) i] * kKitByChar[(size_t) ch][(size_t) i];
    k.globalKit = r.pick (w);

    // Hi-Hats und Toms jeweils aus demselben Kit (sonst passen sie nicht zusammen)
    const int hatKit = r.chance (0.35f) ? r.pick (w) : k.globalKit;
    const int tomKit = r.chance (0.3f) ? r.pick (w) : k.globalKit;
    // Toms: gemeinsame Grundstimmung, Hi Tom in einem musikalischen Abstand dazu
    const float tomBase = std::round (r.range (-3.f, 3.f));
    static const float tomInt[] = { 0.f, 2.f, 3.f, -2.f };
    const float tomStep = tomInt[r.next() % 4];
    const float hatTune = std::round (r.range (-2.f, 3.f) * 2.f) / 2.f;
    const float hatPan = r.sign() * r.range (0.12f, 0.35f);

    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        if (locked[(size_t) t]) continue;
        auto& s = k.t[(size_t) t];
        designTrack (s, t, ch, r);

        int kit = k.globalKit;
        if (t == T_CHAT || t == T_OHAT) kit = hatKit;
        else if (t == T_LTOM || t == T_HTOM) kit = tomKit;
        else if (r.chance (0.3f)) kit = r.pick (w);                    // Kits mischen
        s.src = kit == k.globalKit ? 0 : kit + 1;

        if (t == T_CHAT || t == T_OHAT) { s.tune = hatTune; s.pan = hatPan; }
        if (t == T_LTOM) { s.tune = tomBase; s.pan = -std::abs (s.pan) - 0.1f; }
        if (t == T_HTOM) { s.tune = tomBase + tomStep; s.pan = std::abs (s.pan) + 0.1f; }
        s.pan = std::clamp (s.pan, -1.f, 1.f);

        s.level = levelFor (s, t, kit, loud, r.range (-1.f, 1.f));
    }

    // Master-Bus passend zum Charakter
    switch (ch)
    {
        case DRY:        k.glue = r.range (0.25f, 0.45f); k.tape = r.range (0.1f, 0.25f); break;
        case BIG80S:     k.glue = r.range (0.35f, 0.6f);  k.tape = r.range (0.2f, 0.4f);  break;
        case LOFI:       k.glue = r.range (0.3f, 0.5f);   k.tape = r.range (0.55f, 0.9f); break;
        case INDUSTRIAL: k.glue = r.range (0.55f, 0.8f);  k.tape = r.range (0.25f, 0.5f); break;
        default:         k.glue = r.range (0.25f, 0.4f);  k.tape = r.range (0.05f, 0.2f); break;
    }
    return k;
}

KitSnapshot KitDesigner::mutate (uint32_t seed, const std::array<bool, NUM_TRACKS>& locked, const KitSnapshot& cur, const KitLoudness& loud)
{
    Rng r (seed);
    KitSnapshot k = cur;
    // höchstens eine Spur bekommt den Klang aus einem anderen Kit (Hats/Toms ausgenommen)
    const bool swap = r.chance (0.25f);
    const int swapTrack = (int) (r.next() % (uint32_t) NUM_TRACKS);
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        if (locked[(size_t) t]) continue;
        auto& s = k.t[(size_t) t];
        const float oldLevelDb = 20.f * std::log10 (std::max (1.0e-4f, s.level * s.level));
        mutateTrack (s, t, r);
        s.level = std::clamp (std::sqrt (std::pow (10.f, (oldLevelDb + r.range (-0.6f, 0.6f)) / 20.f)), 0.15f, 1.f);
        if (swap && t == swapTrack && t != T_CHAT && t != T_OHAT && t != T_LTOM && t != T_HTOM)
        {
            const int oldKit = resolvedKit (k, t);
            const int kit = (oldKit + 1 + (int) (r.next() % (uint32_t) (NUM_KITS - 1))) % NUM_KITS;
            s.src = kit == k.globalKit ? 0 : kit + 1;
            s.level = levelFor (s, t, kit, loud, 0.f);
        }
    }
    return k;
}
} // namespace ad
