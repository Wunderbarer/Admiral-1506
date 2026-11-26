// Admiral 1506 - Offline-Test des Kerns (ohne JUCE):
// erzeugt für jedes Genre ein Pattern, spielt 8 Takte mit dem eingebauten Kit ab und prüft das Ergebnis
// (keine NaN, keine Stille, keine Übersteuerung, Fill/Crash an der richtigen Stelle, Sperren wirken).
// Aufruf: core_test <Kit-Ordner> [Ausgabe-Ordner für WAVs]
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "Core/DrumEngine.h"
#include "Core/Generator.h"

using namespace ad;

static bool readWav (const std::string& path, SampleData& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::vector<char> b ((std::istreambuf_iterator<char> (f)), {});
    if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) != 0) return false;
    size_t p = 12;
    int ch = 1, bits = 16, rate = 44100;
    while (p + 8 <= b.size())
    {
        const uint32_t sz = *(const uint32_t*) &b[p + 4];
        if (std::memcmp (&b[p], "fmt ", 4) == 0)
        {
            ch = *(const uint16_t*) &b[p + 10];
            rate = (int) *(const uint32_t*) &b[p + 12];
            bits = *(const uint16_t*) &b[p + 22];
        }
        else if (std::memcmp (&b[p], "data", 4) == 0)
        {
            if (bits != 16) return false;
            const size_t n = sz / (size_t) (2 * ch);
            out.data.resize (n);
            for (size_t i = 0; i < n; ++i) out.data[i] = (float) *(const int16_t*) &b[p + 8 + i * 2 * (size_t) ch] / 32768.f;
            out.sampleRate = rate;
            out.name = path;
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr);
    u32 ((uint32_t) sr * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float x : { l[i], r[i] }) { const int16_t s = (int16_t) std::lround (std::fmax (-1.f, std::fmin (1.f, x)) * 32767.f); f.write ((const char*) &s, 2); }
}

static void printPattern (const Pattern& p, const char* title)
{
    std::printf ("  %s\n", title);
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        std::printf ("    %-8s ", tracks()[(size_t) t].name);
        for (int k = 0; k < NUM_STEPS; ++k)
        {
            const float v = p.v[(size_t) t][(size_t) k];
            std::printf ("%c", v <= 0.f ? (k % 4 == 0 ? ':' : '.') : v >= 0.95f ? 'X' : v >= 0.6f ? 'x' : 'o');
        }
        std::printf ("\n");
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("Aufruf: core_test <Kit-Ordner> [Ausgabe-Ordner]\n"); return 2; }
    const std::string kitDir = argv[1];
    const std::string outDir = argc > 2 ? argv[2] : "";
    int failures = 0;
    auto fail = [&] (const std::string& m) { std::printf ("FEHLER: %s\n", m.c_str()); ++failures; };

    std::vector<std::unique_ptr<SampleData>> kit[2];
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < NUM_TRACKS; ++t)
        {
            auto s = std::make_unique<SampleData>();
            const std::string path = kitDir + "/" + kitNames()[(size_t) k] + "_" + tracks()[(size_t) t].file + ".wav";
            if (! readWav (path, *s)) { fail ("Kit-Datei fehlt/ungültig: " + path); s->data.assign (100, 0.f); }
            kit[k].push_back (std::move (s));
        }

    const double sr = 48000.0;
    const int block = 256;

    for (int genre = 0; genre < (int) genreNames().size(); ++genre)
    {
        GenSettings gs; gs.genre = genre; gs.density = 0.5f; gs.variation = 0.4f;
        std::array<bool, NUM_TRACKS> locks {};
        const GenResult g = Generator::generate (gs, 1234u + (uint32_t) genre, locks, {});
        std::printf ("\n== %s (%d BPM) ==\n", genreNames()[(size_t) genre].c_str(), Generator::suggestedBpm (genre));
        printPattern (g.main, "Haupt-Takt");
        printPattern (g.fill, "Fill-Takt");

        if (g.main.v[T_KICK][0] <= 0.f) fail ("Kick fehlt auf der Eins");
        if (g.main.count() < 8) fail ("Pattern zu leer");

        // Gleicher Seed -> gleiches Ergebnis
        const GenResult g2 = Generator::generate (gs, 1234u + (uint32_t) genre, locks, {});
        if (std::memcmp (&g.main, &g2.main, sizeof (Pattern)) != 0) fail ("Generator nicht deterministisch");

        // Sperre: Kick bleibt beim Neu-Erzeugen erhalten
        std::array<bool, NUM_TRACKS> lk {}; lk[T_KICK] = true;
        const GenResult g3 = Generator::generate (gs, 999u, lk, g);
        if (g3.main.v[T_KICK] != g.main.v[T_KICK] || g3.fill.v[T_KICK] != g.fill.v[T_KICK]) fail ("Sperre der Kick-Spur wirkt nicht");

        // Dichte: mehr Dichte -> im Mittel mehr Schläge
        int lo = 0, hi = 0;
        for (uint32_t s = 1; s <= 40; ++s)
        {
            GenSettings a = gs; a.density = 0.1f; lo += Generator::generate (a, s, locks, {}).main.count();
            GenSettings b = gs; b.density = 0.9f; hi += Generator::generate (b, s, locks, {}).main.count();
        }
        if (hi <= lo) fail ("Dichte wirkt nicht");

        // 8 Takte abspielen (Fill alle 4 Takte, Swing, Humanize)
        DrumEngine eng;
        eng.prepare (sr);
        const int kitIdx = genre % 2;
        for (int t = 0; t < NUM_TRACKS; ++t) eng.setSample (t, kit[kitIdx][(size_t) t].get());
        std::array<TrackSettings, NUM_TRACKS> ts {};
        for (int t = 0; t < NUM_TRACKS; ++t) { ts[(size_t) t].level = tracks()[(size_t) t].level; ts[(size_t) t].pan = tracks()[(size_t) t].pan; }

        Sequencer seq;
        const double bpm = Generator::suggestedBpm (genre);
        const double totalPpq = 8 * 4.0;
        const int total = (int) (totalPpq * 60.0 / bpm * sr) + (int) sr;
        std::vector<float> L ((size_t) total, 0.f), R ((size_t) total, 0.f);
        std::vector<NoteEvent> ev;
        int crashes = 0, fillSnares = 0, events = 0;
        double ppq = 0.0;
        for (int pos = 0; pos + block <= total; pos += block)
        {
            ev.clear();
            if (ppq < totalPpq)
            {
                Sequencer::Block b; b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = sr; b.numSamples = block;
                b.swing = genre == 3 ? 0.4f : 0.f; b.humanize = 0.2f; b.fillEvery = 4;
                seq.collect (b, g.main, g.fill, ev);
                // nur 8 Takte: Schläge ab Takt 9 verwerfen
                const double limit = (totalPpq - ppq) * (sr * 60.0 / bpm);
                ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const NoteEvent& e) { return e.offset >= limit - 1.0; }), ev.end());
            }
            for (auto& e : ev)
            {
                eng.trigger (e.track, e.velocity, e.offset, ts[(size_t) e.track]);
                ++events;
                if (e.track == T_CRASH) ++crashes;
            }
            float* ls[NUM_TRACKS]; float* rs[NUM_TRACKS];
            for (int t = 0; t < NUM_TRACKS; ++t) { ls[t] = &L[(size_t) pos]; rs[t] = &R[(size_t) pos]; }
            eng.render (ls, rs, block);
            ppq += block / (sr * 60.0 / bpm);
        }
        (void) fillSnares;

        float peak = 0.f; double rms = 0.0; bool nan = false;
        for (size_t i = 0; i < L.size(); ++i)
        {
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) nan = true;
            peak = std::fmax (peak, std::fmax (std::fabs (L[i]), std::fabs (R[i])));
            rms += (double) L[i] * L[i];
        }
        rms = std::sqrt (rms / (double) L.size());
        const int expected = g.main.count() * 6 + g.fill.count() * 2 + 1;   // + Crash nach dem ersten Fill (Takt 5)
        std::printf ("  8 Takte: %d Ereignisse (erwartet %d), Crashs %d, Spitze %.2f, RMS %.3f\n", events, expected, crashes, peak, rms);
        if (nan) fail ("NaN im Ausgang");
        if (rms < 0.01) fail ("Ausgang zu leise / still");
        if (peak > 1.6f) fail ("Ausgang übersteuert stark");
        if (events < expected - 2 || events > expected + 2) fail ("Anzahl der Ereignisse stimmt nicht");
        if (crashes < 1) fail ("Crash nach dem Fill fehlt");

        if (! outDir.empty())
            {
            std::string nm = genreNames()[(size_t) genre];
            for (auto& ch : nm) if (! std::isalnum ((unsigned char) ch)) ch = '_';
            writeWav (outDir + "/" + std::to_string (genre) + "_" + nm + ".wav", L, R, (int) sr);
        }
    }

    // Sequencer: Block-Grenzen dürfen keine Schläge verlieren oder verdoppeln (verschiedene Blockgrößen)
    {
        Pattern p; p.clear();
        for (int k = 0; k < NUM_STEPS; ++k) p.v[T_CHAT][(size_t) k] = 0.8f;
        for (int bs : { 1, 17, 64, 333, 4096 })
        {
            Sequencer s; int n = 0; double ppq = 0.0; std::vector<NoteEvent> ev;
            const double bpm = 137.0;
            while (ppq < 16.0)
            {
                Sequencer::Block b; b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = 44100.0; b.numSamples = bs; b.swing = 0.6f;
                ev.clear(); s.collect (b, p, p, ev);
                n += (int) ev.size();
                ppq += bs / (44100.0 * 60.0 / bpm);
            }
            if (n < 64 || n > 65) fail ("Blockgröße " + std::to_string (bs) + ": " + std::to_string (n) + " statt 64 Schläge");
        }
    }

    std::printf ("\n%s (%d Fehler)\n", failures == 0 ? "ALLE TESTS BESTANDEN" : "TESTS FEHLGESCHLAGEN", failures);
    return failures == 0 ? 0 : 1;
}
