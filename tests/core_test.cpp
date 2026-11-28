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
#include "Core/KitDesigner.h"

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

static void printPattern (const Pattern& p, int len, bool fill, const char* title)
{
    std::printf ("  %s (%d Schritte, | = Variations-Zone)\n", title, len);
    const int zone = len - Pattern::zoneFor (len, fill);
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        std::printf ("    %-8s ", tracks()[(size_t) t].name);
        for (int k = 0; k < len; ++k)
        {
            if (k == zone) std::printf ("|");
            else if (k > 0 && k % 16 == 0) std::printf (" ");
            const float v = p.value (t, k, len, fill);
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

    std::vector<std::unique_ptr<SampleData>> kit[NUM_KITS];
    for (int k = 0; k < NUM_KITS; ++k)
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
        const Pattern g = Generator::generate (gs, 1234u + (uint32_t) genre, locks, Pattern());
        std::printf ("\n== %s (%d BPM) ==\n", genreNames()[(size_t) genre].c_str(), Generator::suggestedBpm (genre));
        printPattern (g, 48, false, "Groove 48");
        printPattern (g, 48, true, "Fill-Durchlauf 48");
        printPattern (g, 16, false, "gekürzt auf 16");

        if (g.body[T_KICK][0] <= 0.f) fail ("Kick fehlt auf der Eins");
        if (g.count (48) < 24) fail ("Pattern zu leer");

        // Gleicher Seed -> gleiches Ergebnis
        const Pattern g2 = Generator::generate (gs, 1234u + (uint32_t) genre, locks, Pattern());
        if (std::memcmp (&g, &g2, sizeof (Pattern)) != 0) fail ("Generator nicht deterministisch");

        // Sperre: Kick bleibt beim Neu-Erzeugen erhalten (Groove, Ending und Fill)
        std::array<bool, NUM_TRACKS> lk {}; lk[T_KICK] = true;
        const Pattern g3 = Generator::generate (gs, 999u, lk, g);
        if (g3.body[T_KICK] != g.body[T_KICK] || g3.ending[T_KICK] != g.ending[T_KICK] || g3.fillEnd[T_KICK] != g.fillEnd[T_KICK])
            fail ("Sperre der Kick-Spur wirkt nicht");

        // Statistik über viele Seeds: Dichte, Entwicklung über 3 Takte, gelegentliche Variation am Ende
        int lo = 0, hi = 0, evolving = 0, withEnding = 0;
        for (uint32_t sd = 1; sd <= 100; ++sd)
        {
            GenSettings a = gs; a.density = 0.1f; lo += Generator::generate (a, sd, locks, Pattern()).count (48);
            GenSettings b = gs; b.density = 0.9f; hi += Generator::generate (b, sd, locks, Pattern()).count (48);
            const Pattern q = Generator::generate (gs, sd * 31u, locks, Pattern());
            bool end = false;
            int d2 = 0, d3 = 0;
            for (int t = 0; t < NUM_TRACKS; ++t)
            {
                for (int k = 0; k < 16; ++k)
                {
                    if (std::abs (q.body[(size_t) t][(size_t) k] - q.body[(size_t) t][(size_t) k + 16]) > 1e-4f) ++d2;
                    if (std::abs (q.body[(size_t) t][(size_t) k] - q.body[(size_t) t][(size_t) k + 32]) > 1e-4f) ++d3;
                }
                for (float x : q.ending[(size_t) t]) if (x >= 0.f) end = true;
            }
            evolving += (d2 >= 2 && d3 >= 3) ? 1 : 0;      // Takt 2: mind. 2, Takt 3: mind. 3 Schritte anders
            withEnding += end ? 1 : 0;
        }
        std::printf ("  100 Pattern: %d entwickeln sich über 3 Takte, %d mit Variation im letzten Viertel\n", evolving, withEnding);
        if (hi <= lo) fail ("Dichte wirkt nicht");
        if (evolving < 100) fail ("Takte 2/3 unterscheiden sich nicht in jedem Pattern hörbar");
        if (withEnding < 25 || withEnding > 95) fail ("Variation am Ende nicht 'hin und wieder'");

        // Kürzen: die Variation wandert ans neue Ende
        for (int len : { 16, 24, 32 })
            for (int t = 0; t < NUM_TRACKS; ++t)
                if (g.ending[(size_t) t][END_STEPS - 1] >= 0.f && std::abs (g.value (t, len - 1, len, false) - g.ending[(size_t) t][END_STEPS - 1]) > 1e-6f)
                    fail ("Variation fehlt am Ende bei Länge " + std::to_string (len));

        // Abspielen: 4 Durchläufe bei Länge 48 und 16 (Fill jeden 2. Durchlauf, Swing, Humanize)
        for (int len : { 48, 16 })
        {
            DrumEngine eng;
            eng.prepare (sr);
            const int kitIdx = genre % NUM_KITS;
            for (int t = 0; t < NUM_TRACKS; ++t) eng.setSample (t, kit[kitIdx][(size_t) t].get());
            std::array<TrackSettings, NUM_TRACKS> ts {};
            for (int t = 0; t < NUM_TRACKS; ++t) { ts[(size_t) t].level = tracks()[(size_t) t].level; ts[(size_t) t].pan = tracks()[(size_t) t].pan; }

            Sequencer seq;
            const double bpm = Generator::suggestedBpm (genre);
            const double totalPpq = 4 * len / 4.0;
            const int total = (int) (totalPpq * 60.0 / bpm * sr) + (int) sr;
            std::vector<float> L ((size_t) total, 0.f), R ((size_t) total, 0.f);
            std::vector<NoteEvent> ev;
            int crashes = 0, events = 0;
            double ppq = 0.0;
            for (int pos = 0; pos + block <= total; pos += block)
            {
                ev.clear();
                if (ppq < totalPpq)
                {
                    Sequencer::Block b; b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = sr; b.numSamples = block;
                    b.swing = genre == 3 ? 0.4f : 0.f; b.humanize = 0.2f; b.fillEvery = 2; b.length = len;
                    seq.collect (b, g, ev);
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

            float peak = 0.f; double rms = 0.0; bool nan = false;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) nan = true;
                peak = std::fmax (peak, std::fmax (std::fabs (L[i]), std::fabs (R[i])));
                rms += (double) L[i] * L[i];
            }
            rms = std::sqrt (rms / (double) L.size());
            // Durchläufe 0 und 2 normal, 1 und 3 mit Fill, Crash zu Beginn von Durchlauf 2
            const int crashOn1 = g.value (T_CRASH, 0, len, false) > 0.f ? 0 : 1;
            const int expected = g.count (len, false) * 2 + g.count (len, true) * 2 + crashOn1;
            std::printf ("  Länge %d, 4 Durchläufe: %d Ereignisse (erwartet %d), Crashs %d, Spitze %.2f, RMS %.3f\n", len, events, expected, crashes, peak, rms);
            if (nan) fail ("NaN im Ausgang");
            if (rms < 0.01) fail ("Ausgang zu leise / still");
            if (peak > 1.6f) fail ("Ausgang übersteuert stark");
            if (events < expected - 2 || events > expected + 2) fail ("Anzahl der Ereignisse stimmt nicht");
            if (crashes < 1) fail ("Crash nach dem Fill fehlt");

            if (! outDir.empty() && len == 48)
            {
                std::string nm = genreNames()[(size_t) genre];
                for (auto& ch : nm) if (! std::isalnum ((unsigned char) ch)) ch = '_';
                writeWav (outDir + "/" + std::to_string (genre) + "_" + nm + ".wav", L, R, (int) sr);
            }
        }
    }

    // Sequencer: Block-Grenzen dürfen keine Schläge verlieren oder verdoppeln (verschiedene Blockgrößen und Längen)
    {
        Pattern p; p.clear();
        for (int k = 0; k < MAX_STEPS; ++k) p.body[T_CHAT][(size_t) k] = 0.8f;
        for (int len : { 16, 24, 48 })
            for (int bs : { 1, 17, 64, 333, 4096 })
            {
                Sequencer sq; int n = 0; double ppq = 0.0; std::vector<NoteEvent> ev;
                const double bpm = 137.0;
                while (ppq < 16.0)
                {
                    Sequencer::Block b; b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = 44100.0; b.numSamples = bs; b.swing = 0.6f; b.length = len;
                    ev.clear(); sq.collect (b, p, ev);
                    n += (int) ev.size();
                    ppq += bs / (44100.0 * 60.0 / bpm);
                }
                if (n < 64 || n > 65) fail ("Länge " + std::to_string (len) + ", Blockgröße " + std::to_string (bs) + ": " + std::to_string (n) + " statt 64 Schläge");
            }
    }

    // Sequencer im Host-Loop, Längenwechsel und FILL NOW
    {
        Pattern p; p.clear();
        for (int k = 0; k < MAX_STEPS; ++k) p.body[T_CHAT][(size_t) k] = 0.5f;
        p.body[T_KICK][0] = 1.f;
        for (int i = 0; i < END_STEPS; ++i) p.fillEnd[T_SNARE][(size_t) i] = 0.9f;
        const double srr = 48000.0, bpm = 120.0;
        const int bs = 480;                                    // 1/100 s: Blockgrenzen fallen auf Sechzehntel-Raster
        auto play = [&] (Sequencer& s, double fromPpq, double toPpq, int len, int every, int& kicks, int& snares, int& hats)
        {
            std::vector<NoteEvent> ev;
            for (double ppq = fromPpq; ppq < toPpq - 1e-9; ppq += bs / (srr * 60.0 / bpm))
            {
                Sequencer::Block b; b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = srr; b.numSamples = bs; b.fillEvery = every; b.length = len;
                ev.clear(); s.collect (b, p, ev);
                for (auto& e : ev) { if (e.track == T_KICK) ++kicks; if (e.track == T_SNARE) ++snares; if (e.track == T_CHAT) ++hats; }
            }
        };
        // FL-Pattern mit 4 Takten, 6 Mal geloopt: Admiral (48 Schritte, Fill jeden 2. Durchlauf) läuft durch
        {
            Sequencer s; int kicks = 0, snares = 0, hats = 0;
            for (int loop = 0; loop < 6; ++loop) play (s, 0.0, 16.0, 48, 2, kicks, snares, hats);
            std::printf ("\nHost-Loop 6 x 4 Takte: %d Pattern-Anfänge (erwartet 8), %d Fill-Schläge (erwartet 4 Fills x 12 = 48), %d Hats\n", kicks, snares, hats);
            if (kicks != 8 || snares != 48) fail ("Pattern/Fill laufen im Host-Loop nicht durch");
        }
        // Längenwechsel mitten im Durchlauf: der laufende Durchlauf wird zu Ende gespielt
        {
            Sequencer s; int kicks = 0, snares = 0, hats = 0;
            play (s, 0.0, 4.0, 48, 0, kicks, snares, hats);          // 1 Takt mit 48
            const int lenBefore = s.playingLength();
            play (s, 4.0, 12.0, 16, 0, kicks, snares, hats);         // umgestellt auf 16: erst Takt 2+3 (Rest von 48)
            const int stillOld = s.playingLength();
            play (s, 12.0, 20.0, 16, 0, kicks, snares, hats);        // danach 16er-Durchläufe
            std::printf ("Längenwechsel 48 -> 16 nach Takt 1: Länge im Durchlauf %d, nach Takt 3 %d, Pattern-Anfänge %d (erwartet 3)\n", lenBefore, stillOld, kicks);
            if (lenBefore != 48 || s.playingLength() != 16 || kicks != 3) fail ("Längenwechsel nicht sauber am Durchlauf-Ende");
        }
        // FILL NOW: Fill im laufenden Durchlauf, wenn die Fill-Zone noch nicht erreicht ist
        {
            Sequencer s; int kicks = 0, snares = 0, hats = 0;
            play (s, 0.0, 1.0, 16, 0, kicks, snares, hats);
            s.requestFill();
            play (s, 1.0, 8.0, 16, 0, kicks, snares, hats);
            std::printf ("FILL NOW (Länge 16, Fill aus): %d Fill-Schläge (erwartet 8)\n", snares);
            if (snares != 8) fail ("FILL NOW wirkt nicht");
        }
    }

    // Klangfarbe nach Anschlag und Round Robin (Snare des Kits ANALOG)
    {
        DrumEngine e;
        auto render = [&] (float vel)
        {
            e.prepare (48000.0);
            e.setSample (T_SNARE, kit[0][T_SNARE].get());
            TrackSettings t; t.level = 1.f;
            e.trigger (T_SNARE, vel, 0, t);
            std::vector<float> l (24000, 0.f), r (24000, 0.f);
            float* ls[NUM_TRACKS]; float* rs[NUM_TRACKS];
            for (int i = 0; i < NUM_TRACKS; ++i) { ls[i] = l.data(); rs[i] = r.data(); }
            e.render (ls, rs, 24000);
            return l;
        };
        auto brightness = [] (const std::vector<float>& x)
        {
            double e = 0, d = 0;
            for (size_t i = 1; i < x.size(); ++i) { e += (double) x[i] * x[i]; d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]); }
            return std::sqrt (d / std::max (e, 1e-12));
        };
        const double hard = brightness (render (1.f)), soft = brightness (render (0.3f));
        std::printf ("\nKlangfarbe Snare: Helligkeit laut %.3f, leise %.3f\n", hard, soft);
        if (soft >= hard * 0.85) fail ("Leise Schläge klingen nicht dumpfer");
        const auto a = render (1.f), b = render (1.f);
        double diff = 0, en = 0;
        for (size_t i = 0; i < a.size(); ++i) { diff += (double) (a[i] - b[i]) * (a[i] - b[i]); en += (double) a[i] * a[i]; }
        std::printf ("Round Robin: Unterschied zweier gleicher Schläge %.1f %%\n", 100.0 * std::sqrt (diff / en));
        if (diff / en < 1e-4) fail ("Round Robin wirkt nicht");
    }

    // Kit-Designer: Regeln, Sperren, Pegelausgleich
    {
        KitLoudness loud {};
        for (int k = 0; k < NUM_KITS; ++k)
            for (int t = 0; t < NUM_TRACKS; ++t)
            {
                const auto& d = kit[k][(size_t) t]->data;
                const size_t n = std::min (d.size(), (size_t) (0.15 * kit[k][(size_t) t]->sampleRate));
                double e = 0; for (size_t i = 0; i < n; ++i) e += (double) d[i] * d[i];
                loud[(size_t) k][(size_t) t] = (float) (10.0 * std::log10 (e / (double) std::max<size_t> (n, 1) + 1e-12));
            }
        int bad = 0, chars[5] = {}, mixed = 0;
        double maxDev = 0;
        KitSnapshot base;
        for (int t = 0; t < NUM_TRACKS; ++t) base.t[(size_t) t].level = tracks()[(size_t) t].level;
        std::array<bool, NUM_TRACKS> none {};
        for (int g = 0; g < 7; ++g)
            for (uint32_t sd = 1; sd <= 200; ++sd)
            {
                const auto k = KitDesigner::create (g, sd * 7919u + (uint32_t) g, none, base, loud);
                ++chars[k.character];
                bool mix = false;
                for (int t = 0; t < NUM_TRACKS; ++t)
                {
                    const auto& x = k.t[(size_t) t];
                    if (x.src != 0) mix = true;
                    if (x.tune < -12.f || x.tune > 12.f || x.decay < 0.f || x.decay > 1.f || std::abs (x.pan) > 1.f || x.level < 0.15f || x.level > 1.f
                        || x.hp < 20.f || x.hp > 2000.f || x.lp < 200.f || x.lp > 20000.f || x.dsMix > 1.f || x.rvMix > 1.f || x.dlMix > 1.f || x.dlFb > 0.9f) ++bad;
                    // wirksame Lautheit gegenüber dem Grundkit
                    const int kk = KitDesigner::resolvedKit (k, t);
                    const double eff = loud[(size_t) kk][(size_t) t] + 20.0 * std::log10 (x.level * x.level) + x.dsMix * x.dsDrive * 4.0 + x.rvMix * 3.0 + x.dlMix * 2.0;
                    const double ref = loud[0][(size_t) t] + 20.0 * std::log10 (tracks()[(size_t) t].level * tracks()[(size_t) t].level);
                    if (x.level > 0.151f && x.level < 0.999f) maxDev = std::max (maxDev, std::abs (eff - ref));
                }
                if (mix) ++mixed;
                if (k.t[T_KICK].dlMix > 0.f || k.t[T_KICK].pan != 0.f) ++bad;
                if (KitDesigner::resolvedKit (k, T_CHAT) != KitDesigner::resolvedKit (k, T_OHAT) || k.t[T_CHAT].tune != k.t[T_OHAT].tune) ++bad;
                if (KitDesigner::resolvedKit (k, T_LTOM) != KitDesigner::resolvedKit (k, T_HTOM) || k.t[T_LTOM].pan >= 0.f || k.t[T_HTOM].pan <= 0.f) ++bad;
            }
        std::printf ("\nKit-Designer: 1400 Kits, Charaktere %d/%d/%d/%d/%d, gemischte Kits %d, Regelverstöße %d, max. Pegelabweichung %.1f dB\n",
                     chars[0], chars[1], chars[2], chars[3], chars[4], mixed, bad, maxDev);
        if (bad > 0) fail ("Kit-Designer verletzt Regeln");
        if (maxDev > 2.5) fail ("Pegelausgleich ungenau");
        if (mixed < 300) fail ("Kits werden zu selten gemischt");

        // Sperre: gesperrte Snare bleibt exakt gleich, auch bei MUTATE
        std::array<bool, NUM_TRACKS> lk {}; lk[T_SNARE] = true;
        auto k1 = KitDesigner::create (0, 42, none, base, loud);
        auto k2 = KitDesigner::create (1, 43, lk, k1, loud);
        auto k3 = KitDesigner::mutate (44, lk, k2, loud);
        if (std::memcmp (&k1.t[T_SNARE], &k3.t[T_SNARE], sizeof (TrackSound)) != 0) fail ("Sound-Sperre wirkt nicht");
        // MUTATE bleibt in der Nähe
        float maxTune = 0;
        for (int t = 0; t < NUM_TRACKS; ++t) maxTune = std::max (maxTune, std::abs (k3.t[(size_t) t].tune - k2.t[(size_t) t].tune));
        if (maxTune > 0.55f) fail ("MUTATE verändert zu stark");
    }

    std::printf ("\n%s (%d Fehler)\n", failures == 0 ? "ALLE TESTS BESTANDEN" : "TESTS FEHLGESCHLAGEN", failures);
    return failures == 0 ? 0 : 1;
}
