// Admiral 1506 - Master-Bus: GLUE (Bus-Kompressor) und TAPE (Band-Sättigung), ohne JUCE-Abhängigkeit
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace ad
{
class MasterBus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        att = std::exp (-1.f / (0.010f * sr));      // 10 ms Attack: lässt den Anschlag durch
        rel = std::exp (-1.f / (0.160f * sr));      // 160 ms Release: "atmet" mit dem Groove
        scHp = std::exp (-2.f * 3.14159265f * 90.f / sr);   // Sidechain-Hochpass 90 Hz: die Kick pumpt nicht so stark
        env = 0.f; gainSm = 1.f;
        for (auto& c : ch) c = {};
        gr.store (0.f);
    }

    // glue/tape: 0..1. Bearbeitet die Haupt-Summe an Ort und Stelle.
    void process (float* l, float* r, int n, float glue, float tape)
    {
        const bool comp = glue > 0.001f, sat = tape > 0.001f;
        float maxGr = 0.f;

        // Kompressor-Kennlinie
        const float thr = -4.f - 20.f * glue;            // dB
        const float ratio = 1.5f + 4.5f * glue;
        const float knee = 6.f;
        const float makeup = std::pow (10.f, (-thr * (1.f - 1.f / ratio) * 0.35f) / 20.f);

        // Band: Drive, Höhenverlust
        const float drive = 1.f + 4.f * tape;
        const float bias = 0.12f * tape;                 // leichte Asymmetrie = geradzahlige Obertöne
        const float smallGain = drive * (1.f - std::tanh (bias) * std::tanh (bias));
        const float lpCoef = 1.f - std::exp (-2.f * 3.14159265f * std::min (22000.f - 9000.f * tape, sr * 0.45f) / sr);

        for (int i = 0; i < n; ++i)
        {
            float L = l[i], R = r[i];

            if (comp)
            {
                // Detektor (stereo verkoppelt, Sidechain mit Hochpass)
                float d = std::max (std::abs (hpSide (0, L)), std::abs (hpSide (1, R)));
                env = d > env ? att * env + (1.f - att) * d : rel * env + (1.f - rel) * d;
                const float lev = 20.f * std::log10 (env + 1.0e-9f);
                const float over = lev - thr;
                float red = 0.f;                          // Pegelreduktion in dB (>= 0)
                if (over > knee * 0.5f)       red = over * (1.f - 1.f / ratio);
                else if (over > -knee * 0.5f) red = (1.f - 1.f / ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.f * knee);
                const float g = std::pow (10.f, -red / 20.f) * makeup;
                gainSm += (g - gainSm) * 0.02f;           // Klick-freie Regelung
                L *= gainSm; R *= gainSm;
                maxGr = std::max (maxGr, red);
            }

            if (sat)
            {
                L = tapeCh (ch[0], L, drive, bias, smallGain, lpCoef);
                R = tapeCh (ch[1], R, drive, bias, smallGain, lpCoef);
            }
            l[i] = L; r[i] = R;
        }
        // Anzeige: Pegelreduktion langsam abfallend
        const float shown = gr.load();
        gr.store (std::max (maxGr, shown * std::exp (-(float) n / (0.3f * sr))));   // Anzeige fällt in ~300 ms ab
        float pk = peakGr.load();
        while (maxGr > pk && ! peakGr.compare_exchange_weak (pk, maxGr)) {}
    }

    float getGainReductionDb() const { return gr.load(); }
    float takePeakGainReductionDb() { return peakGr.exchange (0.f); }   // Höchstwert seit dem letzten Abruf

private:
    struct Chan { float xPrev = 0.f, fPrev = 0.f, lp = 0.f, dcX = 0.f, dcY = 0.f, scX = 0.f, scY = 0.f; };

    float hpSide (int c, float x)
    {
        auto& s = ch[c];
        const float y = scHp * (s.scY + x - s.scX);
        s.scX = x; s.scY = y;
        return y;
    }

    // tanh mit Antiderivative-Antialiasing (ADAA 1. Ordnung): kaum Aliasing, keine Latenz
    static float logCosh (float x)
    {
        const float a = std::abs (x);
        return a > 15.f ? a - 0.69314718f : std::log (std::cosh (x));
    }
    static float tapeCh (Chan& c, float x, float drive, float bias, float smallGain, float lpCoef)
    {
        const float u = x * drive + bias;
        const float du = u - c.xPrev;
        const float F = logCosh (u);
        const float y0 = std::abs (du) < 1.0e-4f ? std::tanh (0.5f * (u + c.xPrev)) : (F - c.fPrev) / du;
        c.fPrev = F;
        c.xPrev = u;
        // Pegel bei leisen Signalen unverändert; etwas Ausgleich für die abgerundeten Spitzen
        float y = (y0 - std::tanh (bias)) / smallGain * (1.f + 0.3f * (drive - 1.f) / 4.f);
        // Gleichanteil der Asymmetrie entfernen, dann sanfter Höhenverlust
        const float dc = y - c.dcX + 0.9995f * c.dcY;
        c.dcX = y; c.dcY = dc;
        c.lp += lpCoef * (dc - c.lp);
        return c.lp;
    }

    float sr = 44100.f, att = 0.f, rel = 0.f, scHp = 0.f, env = 0.f, gainSm = 1.f;
    Chan ch[2];
    std::atomic<float> gr { 0.f }, peakGr { 0.f };
};
} // namespace ad
