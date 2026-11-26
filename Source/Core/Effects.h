// Admiral 1506 - Effekte je Spur: Distortion, Delay (Ping-Pong), Reverb.
// Übernommen aus dem Diplomat 1506 (gleicher Klang), ergänzt um reset() ohne Speicheranforderung (Audio-Thread).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ad
{
constexpr float kPi = 3.14159265358979f;

inline float fastTanh (float x)
{
    x = std::clamp (x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

// ---------------------------------------------------------------------------------------------
// Halbband-FIR (Kaiser-Fenster) für das Über-/Unterabtasten; Verstärkung 1 bei DC
struct HalfBand
{
    std::vector<float> h;
    void design (int taps, float beta)
    {
        h.assign ((size_t) taps, 0.f);
        const int c = taps / 2;
        auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        double sum = 0.0;
        for (int n = 0; n < taps; ++n)
        {
            const double x = n - c;
            const double sinc = std::abs (x) < 1.0e-12 ? 0.5 : std::sin (kPi * 0.5 * x) / (kPi * x);
            const double r = 2.0 * n / (double) (taps - 1) - 1.0;
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            h[(size_t) n] = (float) (sinc * w);
            sum += h[(size_t) n];
        }
        for (auto& v : h) v = (float) (v / sum);
    }
};

// Zweifach-Überabtastung: ein Eingangssample -> zwei Ausgangssamples (up) bzw. zwei -> eins (down)
struct Up2
{
    const HalfBand* f = nullptr; std::vector<float> z; int pos = 0;
    void init (const HalfBand* hb) { f = hb; z.assign (f->h.size() / 2 + 2, 0.f); pos = 0; }
    void process (float x, float& y0, float& y1)
    {
        const int n = (int) z.size();
        z[(size_t) pos] = x;
        const int taps = (int) f->h.size();
        float a = 0.f, b = 0.f;
        for (int j = 0, k = 0; j < taps; j += 2, ++k) a += f->h[(size_t) j] * z[(size_t) ((pos - k + n) % n)];
        for (int j = 1, k = 0; j < taps; j += 2, ++k) b += f->h[(size_t) j] * z[(size_t) ((pos - k + n) % n)];
        y0 = 2.f * a; y1 = 2.f * b;
        pos = (pos + 1) % n;
    }
};
struct Down2
{
    const HalfBand* f = nullptr; std::vector<float> z; int pos = 0;
    void init (const HalfBand* hb) { f = hb; z.assign (f->h.size() + 2, 0.f); pos = 0; }
    float process (float x0, float x1)   // x0 ist das ältere der beiden Samples
    {
        const int n = (int) z.size();
        z[(size_t) pos] = x0; pos = (pos + 1) % n;
        z[(size_t) pos] = x1;
        const int taps = (int) f->h.size();
        float a = 0.f;
        for (int j = 0; j < taps; ++j) a += f->h[(size_t) j] * z[(size_t) ((pos - j + n) % n)];
        pos = (pos + 1) % n;
        return a;
    }
};

// ---------------------------------------------------------------------------------------------
// Distortion: Soft (tanh) / Hard (Clip) / Fold (Wellenfalter), 4-fach überabgetastet (Halbband-Filter),
// Klangblende (Tiefpass). Das trockene Signal wird um die Filterlaufzeit verzögert, damit bei Mix < 100 % keine Kammfilter entstehen.
class Distortion
{
public:
    static constexpr int kLatency = 26;   // Samples: (2*(47-1) + (13-1)) / 4

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        hbA.design (47, 8.f); hbB.design (13, 6.f);
        for (auto& c : ch) { c = {}; c.u1.init (&hbA); c.u2.init (&hbB); c.d2.init (&hbB); c.d1.init (&hbA); c.dry.assign (kLatency + 1, 0.f); c.di = 0; }
    }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.u1.z.begin(), c.u1.z.end(), 0.f); std::fill (c.u2.z.begin(), c.u2.z.end(), 0.f);
            std::fill (c.d1.z.begin(), c.d1.z.end(), 0.f); std::fill (c.d2.z.begin(), c.d2.z.end(), 0.f);
            std::fill (c.dry.begin(), c.dry.end(), 0.f);
            c.lp = 0.f;
        }
    }

    // type: 0 Soft, 1 Hard, 2 Fold | drive 0..1 | tone 0..1 | mix 0..1
    void process (float& l, float& r, int type, float drive, float tone, float mix)
    {
        const float pre  = 1.f + drive * drive * 39.f;
        const float comp = 1.f / std::pow (pre, 0.6f);                         // Pegelausgleich: mehr Drive = weniger Ausgangsverstärkung
        const float fc   = 800.f * std::pow (15.f, tone);                       // 800 Hz .. 12 kHz
        const float a    = 1.f - std::exp (-2.f * kPi * std::min (fc, sr * 0.45f) / sr);

        auto run = [&] (Chan& c, float x, float& dryDelayed)
        {
            // trockenes Signal um kLatency Samples verzögern
            c.dry[(size_t) c.di] = x;
            c.di = (c.di + 1) % (kLatency + 1);
            dryDelayed = c.dry[(size_t) c.di];

            float a0, a1, b0, b1, b2, b3;
            c.u1.process (x, a0, a1);                                            // fs -> 2 fs
            c.u2.process (a0, b0, b1);                                           // 2 fs -> 4 fs (zwei Samples nacheinander)
            c.u2.process (a1, b2, b3);
            const float s0 = shape (b0 * pre, type), s1 = shape (b1 * pre, type), s2 = shape (b2 * pre, type), s3 = shape (b3 * pre, type);
            const float d0 = c.d2.process (s0, s1);                              // 4 fs -> 2 fs
            const float d1 = c.d2.process (s2, s3);
            const float y = c.d1.process (d0, d1) * comp;                        // 2 fs -> fs
            c.lp += a * (y - c.lp);
            return c.lp * 0.9f;
        };

        float dl, dr;
        const float wl = run (ch[0], l, dl), wr = run (ch[1], r, dr);
        l = dl * (1.f - mix) + wl * mix;
        r = dr * (1.f - mix) + wr * mix;
    }

private:
    struct Chan
    {
        Up2 u1, u2; Down2 d2, d1;
        std::vector<float> dry; int di = 0;
        float lp = 0.f;
    };
    static float shape (float x, int type)
    {
        switch (type)
        {
            case 1:  return std::clamp (x, -1.f, 1.f);
            case 2:  return std::sin (0.5f * kPi * std::clamp (x, -8.f, 8.f));
            default: return fastTanh (x);
        }
    }
    Chan ch[2];
    HalfBand hbA, hbB;
    float sr = 44100.f;
};

// ---------------------------------------------------------------------------------------------
class Delay
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        const size_t n = (size_t) (sr * 4.2f) + 8;   // bis 1 Takt bei 57 BPM
        bl.assign (n, 0.f); br.assign (n, 0.f);
        w = 0; cur = 0.38f * sr; lpl = lpr = 0.f;
    }

    void reset (float timeMs)
    {
        std::fill (bl.begin(), bl.end(), 0.f); std::fill (br.begin(), br.end(), 0.f);
        lpl = lpr = 0.f;
        cur = std::clamp (timeMs * 0.001f * sr, 8.f, (float) bl.size() - 4.f);   // ohne Gleiten starten
    }

    void process (float& l, float& r, float timeMs, float fb, float mix, bool ping)
    {
        const int n = (int) bl.size();
        const float target = std::clamp (timeMs * 0.001f * sr, 8.f, (float) n - 4.f);
        cur += (target - cur) * 0.00015f; // weiches "Tape"-artiges Gleiten bei Zeitänderung

        auto read = [&] (const std::vector<float>& b)
        {
            float rp = (float) w - cur;
            while (rp < 0.f) rp += (float) n;
            const int i0 = (int) rp;
            const float fr = rp - (float) i0;
            return b[(size_t) i0 % (size_t) n] * (1.f - fr) + b[(size_t) (i0 + 1) % (size_t) n] * fr;
        };

        const float dl = read (bl), dr = read (br);
        lpl += 0.45f * (dl - lpl); // Höhendämpfung in der Rückkopplung
        lpr += 0.45f * (dr - lpr);

        if (ping)
        {
            bl[(size_t) w] = (l + r) * 0.5f + lpr * fb;
            br[(size_t) w] = lpl * fb;
        }
        else
        {
            bl[(size_t) w] = l + lpl * fb;
            br[(size_t) w] = r + lpr * fb;
        }

        l += dl * mix;
        r += dr * mix;
        w = (w + 1) % n;
    }

private:
    std::vector<float> bl, br;
    float sr = 44100.f, cur = 0.f, lpl = 0.f, lpr = 0.f;
    int w = 0;
};

// ---------------------------------------------------------------------------------------------
// Reverb: 8-Kanal-Feedback-Delay-Network (Householder-Matrix) mit modulierten Verzögerungen, Eingangs-Diffusion
// und dekorreliertem Stereo-Ausgang. SIZE = Nachhallzeit, DAMP = Höhendämpfung in der Rückkopplung.
class Reverb
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        static const int len[kN] = { 1087, 1283, 1447, 1621, 1823, 2039, 2243, 2467 };   // teilerfremd (Primzahlen bei 44,1 kHz)
        const float sc = sr / 44100.f * 1.3f;
        modDepth = 5.f * sr / 44100.f;
        for (int i = 0; i < kN; ++i)
        {
            base[i] = (float) ((int) (len[i] * sc) | 1);
            line[i].assign ((size_t) base[i] + (size_t) modDepth * 2 + 8, 0.f);
            lp[i] = 0.f; ph[i] = (float) i / (float) kN; rate[i] = 0.11f + 0.043f * (float) i;
        }
        for (auto& x : wi) x = 0;
        static const int at[2] = { 113, 337 };
        for (int i = 0; i < 2; ++i) { ap[i].assign ((size_t) std::max (1, (int) (at[i] * sr / 44100.f)), 0.f); api[i] = 0; }
        cachedSize = -1.f;
    }

    void reset()
    {
        for (auto& v : line) std::fill (v.begin(), v.end(), 0.f);
        for (auto& v : ap) std::fill (v.begin(), v.end(), 0.f);
        for (auto& x : lp) x = 0.f;
    }

    void process (float& l, float& r, float size, float damp, float mix)
    {
        if (std::abs (size - cachedSize) > 1.0e-6f)
        {
            cachedSize = size;
            // Nachhallzeit (s): Stützpunkte so gewählt, dass SIZE dem bisherigen Hall entspricht (logarithmisch interpoliert)
            static const float rx[8] = { 0.f, 0.1f, 0.3f, 0.5f, 0.8f, 0.9f, 0.95f, 1.f }, ry[8] = { 0.45f, 0.69f, 0.87f, 1.19f, 2.6f, 4.5f, 6.1f, 10.6f };
            const float szr = std::clamp (size, 0.f, 1.f);
            int kr = 0; while (kr < 6 && szr > rx[kr + 1]) ++kr;
            const float rt = std::exp (std::log (ry[kr]) + (std::log (ry[kr + 1]) - std::log (ry[kr])) * (szr - rx[kr]) / (rx[kr + 1] - rx[kr]));
            for (int i = 0; i < kN; ++i) gain[i] = std::pow (10.f, -3.f * base[i] / (sr * rt));
            // Pegel an den bisherigen Hall angleichen (gemessen), damit vorhandene Presets gleich laut bleiben
            static const float xs[6] = { 0.f, 0.1f, 0.3f, 0.5f, 0.8f, 1.f }, ys[6] = { 3.25f, 3.05f, 2.8f, 2.5f, 2.2f, 2.0f };
            const float sz = std::clamp (size, 0.f, 1.f);
            int k = 0; while (k < 4 && sz > xs[k + 1]) ++k;
            outScale = kOutScale * (ys[k] + (ys[k + 1] - ys[k]) * (sz - xs[k]) / (xs[k + 1] - xs[k]));        }
        const float d1 = damp * 0.55f, d2 = 1.f - d1;

        // Eingang: Mono-Summe, zwei Allpass-Diffusoren
        float in = (l + r) * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            auto& b = ap[i]; const float bo = b[(size_t) api[i]];
            const float v = in - 0.6f * bo;
            b[(size_t) api[i]] = v;
            in = bo + 0.6f * v;
            if (++api[i] >= (int) b.size()) api[i] = 0;
        }

        float y[kN], sum = 0.f;
        for (int i = 0; i < kN; ++i)
        {
            ph[i] += rate[i] / sr; if (ph[i] >= 1.f) ph[i] -= 1.f;
            const float d = base[i] + modDepth * std::sin (2.f * kPi * ph[i]);
            const int n = (int) line[i].size();
            float rp = (float) wi[i] - d; while (rp < 0.f) rp += (float) n;
            const int i0 = (int) rp; const float fr = rp - (float) i0;
            const float out = line[i][(size_t) (i0 % n)] * (1.f - fr) + line[i][(size_t) ((i0 + 1) % n)] * fr;
            lp[i] = out * d2 + lp[i] * d1;                                    // Höhendämpfung
            y[i] = lp[i] * gain[i];
            sum += y[i];
        }
        static const float sIn[kN] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        static const float sL[kN]  = { 1, 1, -1, -1, 1, -1, 1, -1 };
        static const float sR[kN]  = { 1, -1, 1, -1, -1, 1, 1, -1 };
        const float h = sum * (2.f / (float) kN);                              // Householder: y - 2/N * sum(y)
        float oL = 0.f, oR = 0.f;
        for (int i = 0; i < kN; ++i)
        {
            const int n = (int) line[i].size();
            line[i][(size_t) wi[i]] = in * 0.25f * sIn[i] + (y[i] - h);
            if (++wi[i] >= n) wi[i] = 0;
            oL += y[i] * sL[i]; oR += y[i] * sR[i];
        }
        l += oL * outScale * mix;
        r += oR * outScale * mix;
    }

private:
    static constexpr int kN = 8;
    static constexpr float kOutScale = 0.55f;
    std::vector<float> line[kN], ap[2];
    float base[kN] = {}, lp[kN] = {}, ph[kN] = {}, rate[kN] = {}, gain[kN] = {};
    int api[2] = { 0, 0 }, wi[kN] = {};
    float sr = 44100.f, modDepth = 5.f, cachedSize = -1.f, outScale = 0.55f;
};
} // namespace ad
