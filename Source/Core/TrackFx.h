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

    bool distOn() const   { return dsMix > 0.001f; }
    bool delayOn() const  { return dlMix > 0.001f; }
    bool reverbOn() const { return rvMix > 0.001f; }
    bool any() const      { return distOn() || delayOn() || reverbOn(); }
    float delayMs (double bpm) const
    {
        return dlSync ? (float) (delayDivBeats (dlDiv) * 60000.0 / std::max (20.0, bpm)) : dlTimeMs;
    }
};

class TrackFx
{
public:
    void prepare (float sampleRate)
    {
        dist.prepare (sampleRate);
        delay.prepare (sampleRate);
        reverb.prepare (sampleRate);
        wasDist = wasDelay = wasReverb = false;
    }

    // Bearbeitet die Spur an Ort und Stelle. Ausgeschaltete Effekte kosten keine Rechenzeit;
    // beim Einschalten starten sie mit leeren Puffern (kein Rest alter Hallfahnen).
    void process (float* l, float* r, int n, const FxSettings& s, double bpm)
    {
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
    Distortion dist;
    Delay delay;
    Reverb reverb;
    bool wasDist = false, wasDelay = false, wasReverb = false;
};
} // namespace ad
