// Admiral 1506 - Effektkette einer Spur: Distortion -> Delay -> Reverb (ohne JUCE-Abhängigkeit)
#pragma once
#include <cmath>
#include "Effects.h"
#include "Params.h"

namespace ad
{
struct FxSettings
{
    int dsType = 0;
    float dsDrive = 0.4f, dsTone = 0.6f, dsMix = 0.f;
    bool dlSync = true, dlPing = true;
    float dlTimeMs = 300.f, dlFb = 0.35f, dlMix = 0.f;
    int dlDiv = 3;
    float rvSize = 0.5f, rvDamp = 0.5f, rvMix = 0.f;
    float hp = 20.f, lp = 20000.f, res = 0.f;

    bool filterOn() const { return hp > 21.f || lp < 19900.f; }
    bool distOn() const   { return dsMix > 0.001f; }
    bool delayOn() const  { return dlMix > 0.001f; }
    bool reverbOn() const { return rvMix > 0.001f; }
    bool any() const      { return filterOn() || distOn() || delayOn() || reverbOn(); }
    float delayMs (double bpm) const
    {
        return dlSync ? (float) (delayDivBeats (dlDiv) * 60000.0 / std::max (20.0, bpm)) : dlTimeMs;
    }
};

// Zustandsvariablen-Filter (Cytomic/TPT), stereo, Hochpass + Tiefpass hintereinander
class TrackFilter
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& s : st) s = {}; }

    void process (float* l, float* r, int n, float hpHz, float lpHz, float res)
    {
        // Koeffizienten weich nachführen (kein Zippern beim Drehen)
        tHp += (hpHz - tHp) * 0.5f;
        tLp += (lpHz - tLp) * 0.5f;
        const float nyq = sr * 0.45f;
        const Coef h = coef (std::min (tHp, nyq), 0.70710678f);
        const Coef p = coef (std::min (tLp, nyq), 0.70710678f + res * 9.f);     // Resonanz nur am Tiefpass
        const bool doHp = tHp > 21.f, doLp = tLp < 19900.f;
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                if (doHp) x[c] = run (st[c].hp, h, x[c], true);
                if (doLp) x[c] = run (st[c].lp, p, x[c], false);
            }
            // Resonanz kann Spitzen erzeugen: weich begrenzen
            l[i] = res > 0.01f ? fastTanh (x[0]) : x[0];
            r[i] = res > 0.01f ? fastTanh (x[1]) : x[1];
        }
    }

private:
    struct Coef { float a1, a2, a3, k; };
    struct State { float ic1 = 0.f, ic2 = 0.f; };
    struct Chan { State hp, lp; };
    Coef coef (float fc, float q) const
    {
        const float g = std::tan (kPi * std::max (10.f, fc) / sr);
        const float k = 1.f / q;
        Coef c; c.k = k;
        c.a1 = 1.f / (1.f + g * (g + k)); c.a2 = g * c.a1; c.a3 = g * c.a2;
        return c;
    }
    static float run (State& s, const Coef& c, float x, bool high)
    {
        const float v3 = x - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.f * v1 - s.ic1;
        s.ic2 = 2.f * v2 - s.ic2;
        return high ? x - c.k * v1 - v2 : v2;
    }
    Chan st[2];
    float sr = 44100.f, tHp = 20.f, tLp = 20000.f;
};

class TrackFx
{
public:
    void prepare (float sampleRate)
    {
        filter.prepare (sampleRate);
        dist.prepare (sampleRate);
        delay.prepare (sampleRate);
        reverb.prepare (sampleRate);
        wasDist = wasDelay = wasReverb = false;
    }

    // Bearbeitet die Spur an Ort und Stelle. Ausgeschaltete Effekte kosten keine Rechenzeit;
    // beim Einschalten starten sie mit leeren Puffern (kein Rest alter Hallfahnen).
    void process (float* l, float* r, int n, const FxSettings& s, double bpm)
    {
        const bool f = s.filterOn();
        if (f && ! wasFilter) filter.reset();
        wasFilter = f;
        if (f) filter.process (l, r, n, s.hp, s.lp, s.res);

        const bool d = s.distOn(), e = s.delayOn(), v = s.reverbOn();
        const float ms = s.delayMs (bpm);
        if (d && ! wasDist) dist.reset();
        if (e && ! wasDelay) delay.reset (ms);
        if (v && ! wasReverb) reverb.reset();
        wasDist = d; wasDelay = e; wasReverb = v;
        if (! (d || e || v)) return;

        const float fb = std::min (s.dlFb, 0.9f);
        for (int i = 0; i < n; ++i)
        {
            float L = l[i], R = r[i];
            if (d) dist.process (L, R, s.dsType, s.dsDrive, s.dsTone, s.dsMix);
            if (e) delay.process (L, R, ms, fb, s.dlMix, s.dlPing);
            if (v) reverb.process (L, R, s.rvSize, s.rvDamp, s.rvMix);
            l[i] = L; r[i] = R;
        }
    }

private:
    TrackFilter filter;
    bool wasFilter = false;
    Distortion dist;
    Delay delay;
    Reverb reverb;
    bool wasDist = false, wasDelay = false, wasReverb = false;
};
} // namespace ad
