#include "DrumEngine.h"
#include <algorithm>
#include <cmath>

namespace ad
{
namespace
{
// Wie stark die Anschlagstärke die Klangfarbe dämpft (0 = gar nicht) - je Instrument
constexpr float kVelTone[NUM_TRACKS]   = { 0.35f, 0.80f, 0.60f, 0.70f, 0.70f, 0.50f, 0.50f, 0.50f, 0.50f, 0.60f };
// "Round Robin": zufällige Mikro-Verstimmung (Halbtöne) und Klangfarben-Streuung je Schlag
constexpr float kRrPitch[NUM_TRACKS]   = { 0.03f, 0.12f, 0.15f, 0.20f, 0.15f, 0.08f, 0.08f, 0.15f, 0.05f, 0.10f };
constexpr float kRrTone[NUM_TRACKS]    = { 0.05f, 0.15f, 0.15f, 0.20f, 0.15f, 0.08f, 0.08f, 0.12f, 0.08f, 0.12f };
}

//==============================================================================
void Sequencer::collect (const Block& b, const Pattern& pat, std::vector<NoteEvent>& out)
{
    if (b.numSamples <= 0 || b.bpm <= 0.0) return;
    const int wantLen = std::clamp (b.length, 1, MAX_STEPS);
    const double samplesPerPpq = b.sampleRate * 60.0 / b.bpm;
    const double p0 = b.ppqStart;
    const double p1 = p0 + (double) b.numSamples / samplesPerPpq;

    // alle Sechzehntel, deren (verschwungene) Position in [p0, p1) liegt
    const long long nFirst = std::max (0LL, (long long) std::floor (p0 * 4.0) - 1);
    const long long nLast = (long long) std::floor (p1 * 4.0) + 1;
    for (long long n = nFirst; n <= nLast; ++n)
    {
        const double t = stepTimePpq (n, b.swing);
        if (t < p0 || t >= p1) continue;

        if (! started)
        {
            // Start: im ersten Takt an der passenden Stelle einsteigen (Zählzeiten liegen auf den Zählzeiten des Hosts)
            started = true;
            curLen = wantLen;
            k = (int) (n % NUM_STEPS) % curLen;
            cycle = 0;
        }
        else if (n == lastN)
        {
            continue;                                   // diese Sechzehntel wurde schon gespielt
        }
        else if (n != lastN + 1)
        {
            // Sprung des Hosts (Loop, Spulen): weiterzählen, nur die Lage im Takt angleichen
            const int phase = (int) (n % NUM_STEPS);
            int nk = k - (k % NUM_STEPS) + phase;
            if (nk < k - 8) nk += NUM_STEPS;            // nicht rückwärts springen
            if (nk < k && k - nk <= 2)
            {
                // Block lief über das Loop-Ende hinaus: diese Sechzehntel wurde schon gespielt
                lastN = n;
                continue;
            }
            if (nk >= curLen) { nk = phase % curLen; ++cycle; curLen = wantLen; nk %= curLen; }
            k = nk;
        }
        lastN = n;

        if (fillRequested)
        {
            fillRequested = false;
            const int zoneStart = curLen - Pattern::fillZone (curLen);
            manualFillCycle = k < zoneStart ? cycle : cycle + 1;
        }

        const int step = k;
        const bool isFill = isFillCycle (cycle, b.fillEvery) || manualFillCycle == cycle;
        const bool crash = step == 0 && (crashAfterFill (cycle, b.fillEvery) || (manualFillCycle >= 0 && manualFillCycle == cycle - 1));
        const int baseOffset = (int) std::floor ((t - p0) * samplesPerPpq);
        lastStep_ = step;
        lastFill_ = isFill;
        lastCycle_ = cycle;

        for (int tr = 0; tr < NUM_TRACKS; ++tr)
        {
            float vel = pat.value (tr, step, curLen, isFill);
            if (tr == T_CRASH && crash) vel = std::max (vel, 0.9f);
            if (vel <= 0.f) continue;

            int off = baseOffset;
            if (b.humanize > 0.f)
            {
                // leicht verspätet (bis 12 ms) und Anschlag gestreut - nie die betonte Eins verschieben
                if (! (step == 0 && vel >= 0.99f))
                    off += (int) (rand01() * b.humanize * 0.012 * b.sampleRate);
                vel = std::clamp (vel + (rand01() - 0.5f) * 0.3f * b.humanize, 0.05f, 1.f);
            }
            out.push_back ({ tr, vel, off });
        }

        // nächster Schritt; am Ende des Durchlaufs gilt ab jetzt die eingestellte Länge
        if (++k >= curLen)
        {
            k = 0;
            if (manualFillCycle >= 0 && manualFillCycle < cycle) manualFillCycle = -1;
            ++cycle;
            curLen = wantLen;
        }
    }
}

//==============================================================================
void DrumEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    fadeSamples = std::max (16, (int) (0.003 * sr));
    allSoundOff();
}

void DrumEngine::allSoundOff()
{
    for (auto& v : voices) v = Voice();
}

bool DrumEngine::active() const
{
    for (auto& v : voices) if (v.track >= 0) return true;
    return false;
}

void DrumEngine::startFade (Voice& v, int afterSamples)
{
    if (v.fadeDelay >= 0 && v.fadeDelay <= afterSamples) return;   // blendet schon früher aus
    if (v.fadeLeft > 0) return;
    v.fadeDelay = std::max (0, afterSamples);
}

void DrumEngine::trigger (int track, float velocity, int offset, const TrackSettings& ts)
{
    if (track < 0 || track >= NUM_TRACKS || ts.mute) return;
    const SampleData* s = slot[(size_t) track].load();
    if (s == nullptr || s->data.size() < 2) return;

    // Gleiche Spur neu angeschlagen: vorige Stimme kurz ausblenden (kein Knacken).
    // Geschlossene Hi-Hat würgt die offene ab (wie bei echten Drumcomputern).
    for (auto& v : voices)
    {
        if (v.track < 0) continue;
        if (v.track == track || (track == T_CHAT && v.track == T_OHAT))
            startFade (v, offset);
    }

    // freie Stimme suchen, sonst die älteste übernehmen
    int best = -1;
    for (int i = 0; i < kVoices; ++i) if (voices[(size_t) i].track < 0) { best = i; break; }
    if (best < 0)
    {
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < kVoices; ++i)
            if (started[(size_t) i] < oldest) { oldest = started[(size_t) i]; best = i; }
    }

    Voice v;
    v.track = track;
    v.s = s;
    const float vel = std::clamp (velocity, 0.f, 1.f);
    // Round Robin: kein Schlag klingt exakt wie der vorige
    const float detune = kRrPitch[track] * rnd();
    v.rate = std::pow (2.0, (double) (ts.tune + detune) / 12.0) * s->sampleRate / sr;
    const float g = std::pow (vel, 1.4f) * ts.level * ts.level * 0.8f * (1.f + 0.04f * rnd());
    const float pan = std::clamp (ts.pan, -1.f, 1.f);
    const float a = (pan + 1.f) * 0.25f * 3.14159265f;
    v.gl = g * std::cos (a) * 1.41421356f;
    v.gr = g * std::sin (a) * 1.41421356f;
    if (ts.decay < 0.995f)
    {
        const double tau = 0.015 + 1.6 * (double) ts.decay * (double) ts.decay;
        v.envMul = (float) std::exp (-1.0 / (tau * sr));
    }

    // Klangfarbe: leise Schläge klingen dumpfer (wie bei echten Trommeln), plus leichte Streuung je Schlag
    {
        const float darkness = std::clamp ((1.f - vel) * kVelTone[track] + kRrTone[track] * 0.5f * (rnd() + 1.f) * 0.5f, 0.f, 1.f);
        const float fc = std::min (20000.f * std::pow (900.f / 20000.f, darkness), (float) sr * 0.45f);
        if (fc < 17000.f && fc < (float) sr * 0.44f)
        {
            const float gg = std::tan (3.14159265f * fc / (float) sr);
            const float k = 1.41421356f;                       // Butterworth, keine Resonanz
            v.flt = true;
            v.a1 = 1.f / (1.f + gg * (gg + k));
            v.a2 = gg * v.a1;
            v.a3 = gg * v.a2;
        }
    }
    v.delay = std::max (0, offset);
    voices[(size_t) best] = v;
    started[(size_t) best] = ++age;
}

void DrumEngine::render (float* const* outL, float* const* outR, int numSamples)
{
    for (auto& v : voices)
    {
        if (v.track < 0) continue;
        const int trk = v.track;
        float* L = outL[v.track];
        float* R = outR[v.track];
        const auto& d = v.s->data;
        const double last = (double) d.size() - 1.0;
        float peak = 0.f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.fadeDelay >= 0)
            {
                if (v.fadeDelay == 0)
                {
                    v.fadeDelay = -1;
                    v.fadeLeft = fadeSamples;
                    v.fadeStep = v.fadeGain / (float) fadeSamples;
                }
                else --v.fadeDelay;
            }
            if (v.delay > 0) { --v.delay; continue; }

            // 4-Punkt-Hermite-Interpolation (weniger Aliasing beim Umstimmen als linear)
            const int i0 = (int) v.pos;
            const float fr = (float) (v.pos - (double) i0);
            const size_t n = d.size();
            const float xm1 = d[(size_t) std::max (0, i0 - 1)];
            const float x0 = d[(size_t) i0];
            const float x1 = d[std::min (n - 1, (size_t) i0 + 1)];
            const float x2 = d[std::min (n - 1, (size_t) i0 + 2)];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            float x = ((c3 * fr + c2) * fr + c1) * fr + x0;
            if (v.flt)
            {
                const float v3 = x - v.ic2;
                const float v1 = v.a1 * v.ic1 + v.a2 * v3;
                const float v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
                v.ic1 = 2.f * v1 - v.ic1;
                v.ic2 = 2.f * v2 - v.ic2;
                x = v2;
            }
            const float y = x * v.env * v.fadeGain;
            L[i] += y * v.gl;
            R[i] += y * v.gr;
            peak = std::max (peak, std::abs (y) * std::max (v.gl, v.gr));

            v.env *= v.envMul;
            v.pos += v.rate;
            if (v.fadeLeft > 0)
            {
                v.fadeGain -= v.fadeStep;
                if (--v.fadeLeft == 0 || v.fadeGain <= 0.f) { v.track = -1; break; }
            }
            if (v.pos >= last || v.env < 1.0e-4f) { v.track = -1; break; }
        }
        if (peak > 0.f)
        {
            auto& m = meter[(size_t) trk];
            float cur = m.load();
            while (peak > cur && ! m.compare_exchange_weak (cur, peak)) {}
        }
    }
}
} // namespace ad
