// Admiral 1506 - Parameter-Tabelle (ohne JUCE-Abhängigkeit, damit auch die Tests sie nutzen können)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ad
{
constexpr int NUM_TRACKS = 10;
constexpr int NUM_STEPS = 16;

// Spuren in fester Reihenfolge
enum Track { T_KICK, T_SNARE, T_CLAP, T_CHAT, T_OHAT, T_LTOM, T_HTOM, T_RIM, T_COWBELL, T_CRASH };

struct TrackInfo
{
    const char* name;      // Anzeige
    const char* file;      // Teil des Dateinamens im Kit (Resources/Kits/<Kit>_<file>.wav)
    const char* id;        // Parameter-Präfix
    int gmNote;            // General-MIDI-Note (Kanal 10) für MIDI-Ausgabe/-Export
    float level, pan;      // Grundeinstellung
};

inline const std::array<TrackInfo, NUM_TRACKS>& tracks()
{
    static const std::array<TrackInfo, NUM_TRACKS> t { {
        { "KICK",     "Kick",      "kick",  36, 0.90f,  0.00f },
        { "SNARE",    "Snare",     "snare", 38, 0.80f,  0.00f },
        { "CLAP",     "Clap",      "clap",  39, 0.70f,  0.06f },
        { "CL HAT",   "ClosedHat", "chat",  42, 0.55f,  0.18f },
        { "OP HAT",   "OpenHat",   "ohat",  46, 0.50f,  0.18f },
        { "LOW TOM",  "LowTom",    "ltom",  45, 0.75f, -0.25f },
        { "HI TOM",   "HighTom",   "htom",  50, 0.72f,  0.25f },
        { "RIM",      "Rim",       "rim",   37, 0.60f, -0.12f },
        { "COWBELL",  "Cowbell",   "cow",   56, 0.50f,  0.30f },
        { "CRASH",    "Crash",     "crash", 49, 0.55f, -0.20f },
    } };
    return t;
}

// Eingehende MIDI-Noten -> Spur (General MIDI, dazu gängige Pad-Belegungen). -1 = keine.
inline int trackForNote (int note)
{
    switch (note)
    {
        case 35: case 36:          return T_KICK;
        case 38: case 40:          return T_SNARE;
        case 39:                   return T_CLAP;
        case 42: case 44:          return T_CHAT;
        case 46:                   return T_OHAT;
        case 41: case 43: case 45: return T_LTOM;
        case 47: case 48: case 50: return T_HTOM;
        case 37:                   return T_RIM;
        case 56:                   return T_COWBELL;
        case 49: case 51: case 52: case 55: case 57: return T_CRASH;
        default:                   return -1;
    }
}

// ---- Parameter pro Spur ----
enum TrackP
{
    TP_LEVEL, TP_PAN, TP_TUNE, TP_DECAY, TP_MUTE, TP_OUT,
    TP_DS_TYPE, TP_DS_DRIVE, TP_DS_TONE, TP_DS_MIX,                        // Distortion
    TP_DL_SYNC, TP_DL_TIME, TP_DL_DIV, TP_DL_FB, TP_DL_MIX, TP_DL_PING,    // Delay (HOST SYNC wie im Diplomat)
    TP_RV_SIZE, TP_RV_DAMP, TP_RV_MIX,                                     // Reverb
    TP_FLT_HP, TP_FLT_LP, TP_FLT_RES,                                      // Filter (erste Stufe der Kette)
    TP_SRC,                                                                 // Klangquelle: 0 = globales Kit, 1.. = bestimmtes Kit
    TP_COUNT
};

// ---- Globale Parameter ----
enum GlobalP
{
    GP_MASTER = TP_COUNT * NUM_TRACKS,
    GP_KIT, GP_GENRE, GP_DENSITY, GP_VARIATION, GP_SWING, GP_HUMANIZE, GP_FILL,
    GP_TEMPO, GP_RUN, GP_MIDIOUT, GP_BARS,
    GP_GLUE, GP_TAPE,                                                       // Master-Bus
    NUM_PARAMS
};

inline int tp (int track, int p) { return track * TP_COUNT + p; }

inline const std::vector<std::string>& genreNames()
{
    static const std::vector<std::string> g { "Synthwave", "Techno", "House", "Hip Hop", "Drum & Bass", "Electro", "Pop / Rock" };
    return g;
}
// Notenwerte des Delays bei HOST SYNC (wie im Diplomat) und ihre Länge in Viertelnoten
inline const std::vector<std::string>& delayDivNames()
{
    static const std::vector<std::string> d { "1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 Bar" };
    return d;
}
inline double delayDivBeats (int i)
{
    static const double b[] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return b[std::clamp (i, 0, 8)];
}

constexpr int NUM_KITS = 3;
inline const std::vector<std::string>& kitNames()
{
    static const std::vector<std::string> k { "Analog", "Punch", "Linn" };
    return k;
}

enum class Kind { Float, Choice, Toggle };
enum class Fmt { Ten, Percent, Pan, Semis, Bpm, Ms, Hz, Plain };

struct ParamSpec
{
    std::string id, name;
    Kind kind = Kind::Float;
    float min = 0.f, max = 1.f, def = 0.f;
    float step = 0.f;
    float centre = 0.f;   // >0: Skew so, dass dieser Wert in der Reglermitte liegt
    Fmt fmt = Fmt::Ten;
    std::vector<std::string> choices;
};

inline std::string formatValue (const ParamSpec& s, float v)
{
    char b[64];
    if (s.kind == Kind::Choice)
    {
        const int i = std::clamp ((int) std::lround (v), 0, (int) s.choices.size() - 1);
        return s.choices[(size_t) i];
    }
    if (s.kind == Kind::Toggle)
        return v > 0.5f ? "On" : "Off";

    switch (s.fmt)
    {
        case Fmt::Ten:     std::snprintf (b, sizeof b, "%.1f", v * 10.f); break;
        case Fmt::Percent: std::snprintf (b, sizeof b, "%.0f %%", v * 100.f); break;
        case Fmt::Pan:
            if (std::abs (v) < 0.005f) std::snprintf (b, sizeof b, "C");
            else std::snprintf (b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::abs (v) * 100.f);
            break;
        case Fmt::Semis:   std::snprintf (b, sizeof b, "%+.1f st", v); break;
        case Fmt::Bpm:     std::snprintf (b, sizeof b, "%.0f BPM", v); break;
        case Fmt::Ms:      std::snprintf (b, sizeof b, "%.0f ms", v); break;
        case Fmt::Hz:
            if (v >= 1000.f) std::snprintf (b, sizeof b, "%.2f kHz", v / 1000.f);
            else             std::snprintf (b, sizeof b, "%.0f Hz", v);
            break;
        case Fmt::Plain:
        default:           std::snprintf (b, sizeof b, "%.2f", v); break;
    }
    return b;
}

inline const std::array<ParamSpec, NUM_PARAMS>& specs()
{
    static const std::array<ParamSpec, NUM_PARAMS> table = []
    {
        std::array<ParamSpec, NUM_PARAMS> a;
        auto F = [&] (int i, const std::string& id, const std::string& name, float mn, float mx, float def, Fmt fmt, float step = 0.f, float centre = 0.f)
        {
            ParamSpec s; s.id = id; s.name = name; s.kind = Kind::Float;
            s.min = mn; s.max = mx; s.def = def; s.fmt = fmt; s.step = step; s.centre = centre;
            a[(size_t) i] = s;
        };
        auto C = [&] (int i, const std::string& id, const std::string& name, std::vector<std::string> ch, int def)
        {
            ParamSpec s; s.id = id; s.name = name; s.kind = Kind::Choice;
            s.choices = std::move (ch); s.min = 0.f; s.max = (float) s.choices.size() - 1.f; s.def = (float) def; s.step = 1.f;
            a[(size_t) i] = s;
        };
        auto T = [&] (int i, const std::string& id, const std::string& name, bool def)
        {
            ParamSpec s; s.id = id; s.name = name; s.kind = Kind::Toggle;
            s.min = 0.f; s.max = 1.f; s.def = def ? 1.f : 0.f; s.step = 1.f;
            a[(size_t) i] = s;
        };

        for (int t = 0; t < NUM_TRACKS; ++t)
        {
            const auto& ti = tracks()[(size_t) t];
            const std::string id = ti.id;
            std::string nm = ti.name;
            F (tp (t, TP_LEVEL), id + "_level", nm + " Level", 0.f, 1.f, ti.level, Fmt::Ten);
            F (tp (t, TP_PAN),   id + "_pan",   nm + " Pan",  -1.f, 1.f, ti.pan, Fmt::Pan);
            F (tp (t, TP_TUNE),  id + "_tune",  nm + " Tune", -12.f, 12.f, 0.f, Fmt::Semis, 0.1f);
            F (tp (t, TP_DECAY), id + "_decay", nm + " Decay", 0.f, 1.f, 1.f, Fmt::Percent);
            T (tp (t, TP_MUTE),  id + "_mute",  nm + " Mute", false);
            T (tp (t, TP_OUT),   id + "_out",   nm + " Separate Out", false);

            C (tp (t, TP_DS_TYPE),  id + "_ds_type",  nm + " Distortion Type", { "Soft", "Hard", "Fold" }, 0);
            F (tp (t, TP_DS_DRIVE), id + "_ds_drive", nm + " Distortion Drive", 0.f, 1.f, 0.4f, Fmt::Ten);
            F (tp (t, TP_DS_TONE),  id + "_ds_tone",  nm + " Distortion Tone", 0.f, 1.f, 0.6f, Fmt::Ten);
            F (tp (t, TP_DS_MIX),   id + "_ds_mix",   nm + " Distortion Mix", 0.f, 1.f, 0.f, Fmt::Percent);
            T (tp (t, TP_DL_SYNC),  id + "_dl_sync",  nm + " Delay Host Sync", true);
            F (tp (t, TP_DL_TIME),  id + "_dl_time",  nm + " Delay Time", 20.f, 2000.f, 300.f, Fmt::Ms, 0.f, 300.f);
            C (tp (t, TP_DL_DIV),   id + "_dl_div",   nm + " Delay Note Value", delayDivNames(), 3);
            F (tp (t, TP_DL_FB),    id + "_dl_fb",    nm + " Delay Feedback", 0.f, 0.9f, 0.35f, Fmt::Ten);
            F (tp (t, TP_DL_MIX),   id + "_dl_mix",   nm + " Delay Mix", 0.f, 1.f, 0.f, Fmt::Percent);
            T (tp (t, TP_DL_PING),  id + "_dl_ping",  nm + " Delay Ping-Pong", true);
            F (tp (t, TP_RV_SIZE),  id + "_rv_size",  nm + " Reverb Size", 0.f, 1.f, 0.5f, Fmt::Ten);
            F (tp (t, TP_RV_DAMP),  id + "_rv_damp",  nm + " Reverb Damping", 0.f, 1.f, 0.5f, Fmt::Ten);
            F (tp (t, TP_RV_MIX),   id + "_rv_mix",   nm + " Reverb Mix", 0.f, 1.f, 0.f, Fmt::Percent);
            F (tp (t, TP_FLT_HP),   id + "_hp",       nm + " Highpass", 20.f, 2000.f, 20.f, Fmt::Hz, 0.f, 200.f);
            F (tp (t, TP_FLT_LP),   id + "_lp",       nm + " Lowpass", 200.f, 20000.f, 20000.f, Fmt::Hz, 0.f, 2000.f);
            F (tp (t, TP_FLT_RES),  id + "_res",      nm + " Filter Resonance", 0.f, 1.f, 0.f, Fmt::Ten);
            {
                std::vector<std::string> src { "Global Kit" };
                for (auto& k : kitNames()) src.push_back (k);
                C (tp (t, TP_SRC), id + "_src", nm + " Sound Source", src, 0);
            }
        }
        F (GP_MASTER,    "master",    "Master", 0.f, 1.f, 0.72f, Fmt::Ten);
        C (GP_KIT,       "kit",       "Kit", kitNames(), 0);
        C (GP_GENRE,     "genre",     "Genre", genreNames(), 0);
        F (GP_DENSITY,   "density",   "Density", 0.f, 1.f, 0.5f, Fmt::Percent);
        F (GP_VARIATION, "variation", "Variation", 0.f, 1.f, 0.35f, Fmt::Percent);
        F (GP_SWING,     "swing",     "Swing", 0.f, 1.f, 0.f, Fmt::Percent);
        F (GP_HUMANIZE,  "humanize",  "Humanize", 0.f, 1.f, 0.15f, Fmt::Percent);
        C (GP_FILL,      "fill",      "Fill Every", { "Off", "4 Bars", "8 Bars", "16 Bars" }, 1);
        F (GP_TEMPO,     "tempo",     "Internal Tempo", 60.f, 200.f, 120.f, Fmt::Bpm, 1.f);
        T (GP_RUN,       "run",       "Internal Run", false);
        T (GP_MIDIOUT,   "midiout",   "MIDI Out", true);
        C (GP_BARS,      "bars",      "MIDI Export Bars", { "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }, 2);
        F (GP_GLUE,      "glue",      "Master Glue", 0.f, 1.f, 0.3f, Fmt::Ten);
        F (GP_TAPE,      "tape",      "Master Tape", 0.f, 1.f, 0.25f, Fmt::Ten);
        return a;
    }();
    return table;
}

inline int fillEveryBars (int choice) { static const int v[] = { 0, 4, 8, 16 }; return v[std::clamp (choice, 0, 3)]; }
inline int exportBars (int choice)    { static const int v[] = { 1, 2, 4, 8, 16 }; return v[std::clamp (choice, 0, 4)]; }
} // namespace ad
