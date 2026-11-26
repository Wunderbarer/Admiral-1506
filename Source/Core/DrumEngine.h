// Admiral 1506 - Sampler-Engine und Sequencer (ohne JUCE-Abhängigkeit)
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "Pattern.h"

namespace ad
{
struct SampleData
{
    std::vector<float> data;     // mono
    double sampleRate = 44100.0;
    std::string name;
};

struct TrackSettings
{
    float level = 0.8f, pan = 0.f, tune = 0.f, decay = 1.f;
    bool mute = false;
};

struct NoteEvent
{
    int track = 0;
    float velocity = 1.f;
    int offset = 0;              // Sample-Position im aktuellen Block (kann bei Humanize >= Blocklänge sein)
};

// Sequencer: wandelt die Host-Position (PPQ) in Schlag-Ereignisse für den aktuellen Block um.
class Sequencer
{
public:
    struct Block
    {
        double ppqStart = 0.0;   // Position am Blockanfang in Viertelnoten
        double bpm = 120.0;
        double sampleRate = 44100.0;
        int numSamples = 0;
        float swing = 0.f, humanize = 0.f;
        int fillEvery = 0;       // Fill in jedem n-ten Durchlauf (0 = aus)
        int length = MAX_STEPS;  // Pattern-Länge in Sechzehnteln
    };

    // Ereignisse für den Block anhängen
    void collect (const Block& b, const Pattern& pat, std::vector<NoteEvent>& out);

    int lastStep() const { return lastStep_; }        // zuletzt gespielter Schritt im Pattern, -1 = keiner
    bool lastWasFill() const { return lastFill_; }
    long long lastCycle() const { return lastCycle_; }
    void reset() { lastStep_ = -1; lastFill_ = false; lastCycle_ = 0; }

private:
    uint32_t rng = 0x1506u;
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) / 16777216.f; }
    int lastStep_ = -1;
    bool lastFill_ = false;
    long long lastCycle_ = 0;
};

class DrumEngine
{
public:
    static constexpr int kVoices = 48;

    void prepare (double sampleRate);
    void setSample (int track, const SampleData* s) { slot[(size_t) track].store (s); }
    const SampleData* getSample (int track) const { return slot[(size_t) track].load(); }

    // Schlag auslösen (offset = Verzögerung in Samples ab Beginn des nächsten render-Aufrufs)
    void trigger (int track, float velocity, int offset, const TrackSettings& ts);

    // Rendert alle Stimmen. outL[t]/outR[t]: Ziel der Spur t (mehrere Spuren dürfen dasselbe Ziel haben).
    // Die Ziele werden addiert, nicht gelöscht.
    void render (float* const* outL, float* const* outR, int numSamples);

    void allSoundOff();
    bool active() const;
    // Pegel der Spuren (Anzeige): Spitzenwert seit dem letzten Abruf
    float takeMeter (int track) { return meter[(size_t) track].exchange (0.f); }

private:
    struct Voice
    {
        int track = -1;
        const SampleData* s = nullptr;
        double pos = 0.0, rate = 1.0;
        float gl = 0.f, gr = 0.f;
        float env = 1.f, envMul = 1.f;
        bool flt = false;          // Klangfarbe nach Anschlagstärke (SVF-Tiefpass)
        float a1 = 0.f, a2 = 0.f, a3 = 0.f, ic1 = 0.f, ic2 = 0.f;
        int delay = 0;             // Samples bis zum Start
        int fadeDelay = -1;        // Samples bis zum Ausblenden (-1 = nicht)
        int fadeLeft = 0;          // laufendes Ausblenden: verbleibende Samples
        float fadeGain = 1.f, fadeStep = 0.f;
    };
    void startFade (Voice& v, int afterSamples);

    std::array<Voice, kVoices> voices {};
    std::array<std::atomic<const SampleData*>, NUM_TRACKS> slot {};
    std::array<std::atomic<float>, NUM_TRACKS> meter {};
    double sr = 44100.0;
    int fadeSamples = 132;
    uint32_t age = 0;
    uint32_t rng = 0x2545F491u;
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) / 16777216.f * 2.f - 1.f; }   // -1..1
    std::array<uint32_t, kVoices> started {};
};
} // namespace ad
